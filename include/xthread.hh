#if !defined(DOUBLETAKE_XTHREAD_H)
#define DOUBLETAKE_XTHREAD_H

/*
 * @file   xthread.h
 * @brief  Handling different kinds of synchronizations, like thread creation and exit,
 *         lock, conditional variables and barriers.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <new>

#include "globalinfo.hh"
#include "internalheap.hh"
#include "internalsyncs.hh"
#include "list.hh"
#include "log.hh"
#include "real.hh"
#include "sysrecord.hh"
#include "vmmap.hh"
#include "semaphore.hh"
#include "synceventlist.hh"
#include "threadinfo.hh"
#include "threadmap.hh"
#include "threadstruct.hh"
#include "xcontext.hh"
#include "xdefines.hh"
#include "xsync.hh"

class xthread {
	// After the first pointer, we will keep 
	// a data structure to keep all information about this barrier
	// barrier is typically 32 bytes, thus, it should be enough.	
	class BarrierInfo {
	public:
		int maxThreads;
		int waitingThreads;
		bool isArrivalPhase;
	};

public:
  xthread() : _sync(), _sysrecord(), _thread() {}

  // Actually, it is not an actual singleton.
  // Every process will have one copy. They can be used
  // to hold different contents specific to different threads.
  static xthread& getInstance() {
    static char buf[sizeof(xthread)];
    static xthread* xthreadObject = new (buf) xthread();
    return *xthreadObject;
  }

  void initialize() {
    _thread.initialize();

    // Initialize the syncmap and threadmap.
    _sync.initialize();
	  threadmap::getInstance().initialize();

		// Initialize the global list for spawning operations
    void* ptr = ((void*)InternalHeap::getInstance().malloc(sizeof(SyncEventList)));
    _spawningList = new (ptr) SyncEventList(NULL, E_SYNC_SPAWN);

    // Register the first thread
    registerInitialThread();
    current->isSafe = true;
    PRINF("Done with thread initialization");
  }

  void finalize() {
		destroyAllSemaphores(); 
	}

  int getThreadIndex() const;
  char *getCurrentThreadBuffer();

  // After an epoch is end and there is no overflow,
  // we should discard those record events since there is no
  // need to rollback anymore
  // tnrere are three types of events here.
  void epochEndWell() {
		// There is no need to run deferred synchronizations 
		// since we will do this in epochBegin();
		// Cleanup all synchronization events in the global list (mostly thread creations) and lists of 
		// different synchronization variables
    _sync.epochBegin();

		// Now we will cleanup recorded sychronizations	
    _spawningList->initialization(E_SYNC_SPAWN);
  }

  // Register initial thread
  inline void registerInitialThread() {
    int tindex = allocThreadIndex();

    if (tindex == -1) {
      return;
    }

    thread_t* tinfo = getThreadInfo(tindex);

    // Set the current to corresponding tinfo.
    current = tinfo;
    current->joiner = NULL;
    current->index = tindex;
    current->parent = NULL;

    insertAliveThread(current, pthread_self());

    // Setup tindex for initial thread.
    threadRegister(true);
    current->isNewlySpawned = false;
  }

  /// Handling the specific thread event.
  void thread_exit(void*) {
    // FIXME later.
    //  abort();
  }

  // In order to improve the speed, those spawning operations will do in
  // a batched way. Everything else will be stopped except this spawning process.
  // All newly spawned children will also wait for the notification of the parent.
  // SO only the first time, the thread will wait on fence.
  // To guarantee the correctness, those newly spawned threads will issue
  // an epochBegin() to discard those possibly polluted pages.
  // For the parent, because no one is running when spawnning, so there is no
  // need to call epochBegin().
  int thread_create(pthread_t* tid, const pthread_attr_t* attr, threadFunction* fn, void* arg) {
    int tindex;
    int result;

    PRINF("process %d is before thread_create now\n", current->index);
    if(!doubletake::isRollback) {
      // Lock and record
      global_lock();

      // Allocate a global thread index for current thread.
      tindex = allocThreadIndex();

			assert(tindex != -1);

      // WRAP up the actual thread function.
      // Get corresponding thread_t structure.
      thread_t* children = getThreadInfo(tindex);

      children->isDetached = false;
      if(attr) {
        int detachState;
        pthread_attr_getdetachstate(attr, &detachState);

        // Check whether the thread is detached or not?
        if(detachState == PTHREAD_CREATE_DETACHED) {
          children->isDetached = true;
        }
      }

      children->parent = current;
      children->index = tindex;
      children->startRoutine = fn;
      children->startArg = arg;
      children->status = E_THREAD_STARTING;
      children->hasJoined = false;
      children->isSafe = false;

      // Now we set the joiner to NULL before creation.
      // It is impossible to let newly spawned child to set this correctly since
      // the parent may already sleep on that.
      children->joiner = NULL;

      PRINF("thread creation with index %d\n", tindex);
      // Now we are going to record this spawning event.
      disableCheck();
      result = Real::pthread_create(tid, attr, xthread::startThread, (void*)children);
      enableCheck();
      if(result != 0) {
        PRWRN("thread creation failed with errno %d -- %s\n", errno, strerror(errno));
        Real::exit(-1);
      }

      // Record spawning event
      _spawningList->recordSyncEvent(E_SYNC_SPAWN, result);
      _sysrecord.recordCloneOps(result, *tid);

      if(result == 0) {
        insertAliveThread(children, *tid);
      }

      global_unlock();

      if(result == 0) {
        // Waiting for the finish of registering children thread.
        lock_thread(children);

        while(children->status == E_THREAD_STARTING) {
          wait_thread(children);
          //     PRINF("Children %d status %d. now wakenup\n", children->index, children->status);
        }
        unlock_thread(children);
        //  	PRINF("Creating thread %d at %p self %p\n", tindex, children, (void*)children->self);
      }
    } else {
      result = _sync.peekSyncEvent(_spawningList);
      PRINF("process %d is before thread_create, result %d\n", current->index, result);

      _sysrecord.getCloneOps(tid, &result);
      PRINF("process %d in creation, result %d\n", current->index, result);
      if(result == 0) {
        waitSemaphore();
        PRINF("process %d is after waitsemaphore, thread %lx\n", current->index, *tid);

        // Wakeup correponding thread, now they can move on.
        thread_t* thread = getThread(*tid);

        // Wakeup corresponding thread
        thread->joiner = NULL;
       	// Check the thread's status
				if(thread->status == E_THREAD_WAITFOR_REAPING) {
        	thread->status = E_THREAD_ROLLBACK;
				 	signal_thread(thread);
				}
				else if(thread->status == E_THREAD_COND_WAITING || thread->status == E_THREAD_JOINING) {
        	PRINF("Waken up thread %d with status %d condwait %p in thread_creation\n", thread->index, thread->status, thread->condwait);
        	thread->status = E_THREAD_ROLLBACK;
        	Real::pthread_cond_signal(thread->condwait);
				}
				//FIXME Tongping 
				//while(1);
      }
      // Whenever we are calling __clone, then we can ask the thread to rollback?
      // Update the events.
      PRINF("#############process %d before updateSyncEvent now\n", current->index);
      updateSyncEvent(_spawningList);
      PRINF("#############process %d after updateSyncEvent now\n", current->index);
    }

    return result;
  }

	inline void markThreadJoining(thread_t * thread) {
		lock_thread(current);
		current->status = E_THREAD_JOINING;
		current->condwait = &thread->cond;
		unlock_thread(current);
	}

	inline void unmarkThreadJoining() {
		checkRollback(NULL);
	}

  /// @brief Wait for a thread to exit.
  inline int thread_join(pthread_t joinee, void** result) {
    thread_t* thread = NULL;

    // Try to check whether thread is empty or not?
    thread = getThread(joinee);
    assert(thread != NULL);

		PRINF("main thread is joining thread %d\n", thread->index);

		setThreadUnsafe();
	
		// If the joinee has stopped for reaping, this thread will be considered as
		// in a waiting status, but it is not?
		markThreadJoining(thread);
      
    lock_thread(thread);

		PRINF("main thread is joining thread %d with status %d\n", thread->index, thread->status);
    while(thread->status != E_THREAD_WAITFOR_REAPING) {
      // Set the joiner to current thread
      thread->joiner = current;

      // Wait for the joinee to wake me up
			setThreadSafe();
      wait_thread(thread);
    }

    // Now mark this thread's status so that the thread can be reaped.
    thread->hasJoined = true;

    // Actually, we should get the result from corresponding thread
    if(result) {
      *result = thread->result;
    }

    // Now we unlock and proceed
    unlock_thread(thread);

		// Check the status since it is possible to be waken up for rollback.
		unmarkThreadJoining();
			
		setThreadSafe();

		// Defer the reaping of this thread for memory deterministic usage.
		if(deferSync((void *)thread, E_SYNCVAR_THREAD)) {
			PRINF("Before reap dead threads!!!!\n");
			// deferSync may return TRUE if we have to reapDeadThreads now.
    	invokeCommit();
			PRINF("After reap dead threads!!!!\n");
		}
 
    return 0;
  }

  /// @brief Detach a thread
  inline int thread_detach(pthread_t thread) {
    thread_t* threadinfo = NULL;

    // Try to check whether thread is empty or not?
    threadinfo = getThreadInfo(thread);

    assert(threadinfo != NULL);

    lock_thread(threadinfo);
    threadinfo->isDetached = true;
    unlock_thread(threadinfo);

    abort();
  }

  /// @brief Do a pthread_cancel
  inline int thread_cancel(pthread_t thread) {
    int retval;
    invokeCommit();
    retval = Real::pthread_cancel(thread);
    if(retval == 0) {
      _thread.cancelAliveThread(thread);
    }
    return retval;
  }

  inline int thread_kill(pthread_t thread, int sig) { return Real::pthread_kill(thread, sig); }

  /// Save those actual mutex address in original mutex.
  int mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    // The synchronization here is totally broken: initializer should read state, then check
    // if init is needed. If so, then allocate and atomic compare exchange.

    if(!doubletake::isRollback) {
      // Allocate a mutex
      pthread_mutex_t* real_mutex =
          (pthread_mutex_t*)allocRealSyncVar(sizeof(pthread_mutex_t), E_SYNC_MUTEX_LOCK);

      // Initialize the real mutex
      int result = Real::pthread_mutex_init(real_mutex, attr);

      // If we can't setup this entry, that means that this variable has been initialized.
      setSyncEntry(E_SYNCVAR_MUTEX, mutex, real_mutex, sizeof(pthread_mutex_t));

      return result;
    }
		else {
			// In the rollback phase, we will get the actual entry at first.
			resetSyncEntry(E_SYNCVAR_MUTEX, mutex);	
		}

    return 0;
  }

  inline bool isInvalidSyncVar(void* realMutex) {
    return (((intptr_t)realMutex < xdefines::INTERNAL_HEAP_BASE) ||
                    ((intptr_t)realMutex >= xdefines::INTERNAL_HEAP_END)
                ? true
                : false);
  }

  int do_mutex_lock(void* mutex, thrSyncCmd synccmd) {
    int ret = 0;
    SyncEventList* list = NULL;
    pthread_mutex_t* realMutex = NULL;
    realMutex = (pthread_mutex_t*)getSyncEntry(mutex);
    if(isInvalidSyncVar(realMutex)) {
      mutex_init((pthread_mutex_t*)mutex, NULL);
      realMutex = (pthread_mutex_t*)getSyncEntry(mutex);
    }
      
		assert(realMutex != NULL);
    list = getSyncEventList(mutex, sizeof(pthread_mutex_t));

    if(!doubletake::isRollback) {
			// Mark the thread as unsafe.
  		setThreadUnsafe();
      switch(synccmd) {
      case E_SYNC_MUTEX_LOCK:
        ret = Real::pthread_mutex_lock(realMutex);
        break;

      case E_SYNC_MUTEX_TRY_LOCK:
        ret = Real::pthread_mutex_trylock(realMutex);
        break;

      default:
        break;
      }

			if(!current->disablecheck) {
      	// Record this event
      	list->recordSyncEvent(E_SYNC_MUTEX_LOCK, ret);
     		PRINF("Thread %d recording: mutex_lock at mutex %p realMutex %p list %p\n", current->index, mutex, realMutex, list);
			}
    } else if (!current->disablecheck) {
      // PRINF("synceventlist get mutex at %p list %p\n", mutex, list);
      PRINF("REPLAY: Thread %d: mutex_lock at mutex %p list %p.\n", current->index, mutex, list);
      assert(list != NULL);

			/* Peek the synchronization event (first event in the thread), it will confirm the following things
			 1. Whether this event is expected event? If it is not, maybe it is caused by
			    a race condition. Maybe we should restart the rollback or just simply reported this problem.
			 2. Whether the first event is on the pending list, which means it is the thread's turn? 
					If yes, then the current signal thread should increment its semaphore.
			 3. Whether the mutex_lock is successful or not? If it is not successful, 
					no need to wait for the semaphore since there is no actual lock happens.
			*/ 
      ret = _sync.peekSyncEvent(list);
     	PRINF("REPLAY: After peek: Thread %d: mutex_lock at mutex %p list %p ret %d.\n", current->index, mutex, list, ret);
      if(ret == 0) {
        waitSemaphore();
      }

     	PRINF("REPLAY: After peekandwait: Thread %d: mutex_lock at mutex %p list %p ret %d.\n", current->index, mutex, list, ret);
     	PRDBG("mutex_lock at mutex %p list %p done\n", mutex, list);
     	//PRINF("mutex_lock at mutex %p list %p done!\n", mutex, list);
			_sync.advanceThreadSyncList();
    }
    return ret;
  }

  int mutex_lock(pthread_mutex_t* mutex) {
    if (current->disablecheck)
      return Real::pthread_mutex_lock((pthread_mutex_t *)mutex);
    else
      return do_mutex_lock(mutex, E_SYNC_MUTEX_LOCK);
  }

  int mutex_trylock(pthread_mutex_t* mutex) {
    if (current->disablecheck)
      return Real::pthread_mutex_trylock((pthread_mutex_t *)mutex);
    else
      return do_mutex_lock(mutex, E_SYNC_MUTEX_TRY_LOCK);
  }

  int mutex_unlock(pthread_mutex_t* mutex) {
    int ret = 0;
    pthread_mutex_t* realMutex = NULL;

    if(!doubletake::isRollback) {
      realMutex = (pthread_mutex_t*)getSyncEntry(mutex);
      ret = Real::pthread_mutex_unlock(realMutex);

			// Now the thread is safe to be interrupted.
  		setThreadSafe();
    } else if(!current->disablecheck) {
      SyncEventList* list = getSyncEventList(mutex, sizeof(pthread_mutex_t));
      PRDBG("mutex_unlock at mutex %p list %p\n", mutex, list);
      //	PRINF("mutex_unlock at mutex %p list %p\n", mutex, list);
      struct syncEvent* nextEvent = list->advanceSyncEvent();
      if(nextEvent) {
        _sync.signalNextThread(nextEvent);
      }
      PRDBG("mutex_unlock at mutex %p list %p done\n", mutex, list);
    } else {
      Real::pthread_mutex_unlock(mutex);
    }
    // WARN("mutex_unlock mutex %p\n", mutex);
    return ret;
  }

  // Add this event into the destory list.
  int mutex_destroy(pthread_mutex_t* mutex) {
    if(!doubletake::isRollback) {
    	deferSync((void*)mutex, E_SYNCVAR_MUTEX);
		}
    return 0;
  }

  ///// conditional variable functions.
  int cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) {
    if(!doubletake::isRollback) {
      // Allocate a mutex
      pthread_cond_t* real_cond =
          (pthread_cond_t*)allocRealSyncVar(sizeof(pthread_cond_t), E_SYNC_COND);

      // Initialize the real mutex
      int result = Real::pthread_cond_init(real_cond, attr);

      // If we can't setup this entry, that means that this variable has been initialized.
      setSyncEntry(E_SYNCVAR_COND, cond, real_cond, sizeof(pthread_cond_t));
      PRINF("cond_init for thread %d. cond %p realCond %p\n", current->index, cond, real_cond);

      return result;
    }
		else {
			resetSyncEntry(E_SYNCVAR_COND, cond);	
			return 0;
		}
  }

  // Add this into destoyed list.
  void cond_destroy(pthread_cond_t* cond) { 
    if(!doubletake::isRollback) {
			deferSync((void*)cond, E_SYNCVAR_COND); 
		}
	}


	// Mark whether 
	void markThreadCondwait(pthread_cond_t * cond) {
		lock_thread(current);
		assert(current->status == E_THREAD_RUNNING);
		current->status = E_THREAD_COND_WAITING;
		current->condwait = cond;
		unlock_thread(current);

		// Now thread is safe to be interrupted
		setThreadSafe();
		PRINF("markThreadCondwait on thread %d with cond %p\n", current->index, cond);		
	}

	/*
    The thread can be waken up on two different situations. 
		One is waken up through the application.
		Another is waken up by DoubleTake because of rollback.
	*/
	void unmarkThreadCondwait(pthread_mutex_t * mutex) {
		checkRollback(mutex);
		setThreadUnsafe();
	}


	void checkRollback(pthread_mutex_t * mutex) {
		PRINF("checkRollback on thread %d with cond %p mutex %p\n", current->index, current->condwait, mutex);		
		lock_thread(current);
		// Cleanup this thread
		current->condwait = NULL;
		
		if(current->status == E_THREAD_ROLLBACK) {
      PRINF("THREAD%d (status %d) is wakenup after cond_wait, plan to rollback\n", current->index, current->status);
			unlock_thread(current);

			if(mutex) {			
				// If we are holding the lock, release it before the rollback.
				Real::pthread_mutex_unlock(mutex);
			}
				
			// Time to rollback. 
			checkRollbackCurrent();
		}
		else {
      PRINF("THREAD%d (status %d) is wakenup after cond_wait with mutex %p\n", current->index, current->status, mutex);
			current->status = E_THREAD_RUNNING;
			unlock_thread(current);
		}
	}

  int cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
		return cond_wait_core(cond, mutex, NULL);
	}

	// Cond_timedwait: since we usually get the mutex before this. So there is
  // no need to check mutex any more.
  int cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec * abstime) {
		return cond_wait_core(cond, mutex, abstime);
	}

  // Condwait: since we usually get the mutex before this. So there is
  // no need to check mutex any more.
  int cond_wait_core(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec * abstime) {
    int ret;
    pthread_mutex_t* realMutex = (pthread_mutex_t*)getSyncEntry(mutex);
    pthread_cond_t* realCond = (pthread_cond_t*)getSyncEntry(cond);
    assert(realMutex != NULL);

		// Check whether the cond is available.
		if(isInvalidSyncVar(realCond)) {
      cond_init((pthread_cond_t*)cond, NULL);
      realCond = (pthread_cond_t*)getSyncEntry(cond);
    }

    SyncEventList* list = NULL;
      
		PRINF("cond_wait_core for thread %d. cond %p mutex %p\n", current->index, cond, mutex);

    if(!doubletake::isRollback) {
      PRINF("cond_wait for thread %d. cond %p mutex %p\n", current->index, cond, mutex);
			assert(realCond != NULL);

			// Mark the status of this thread before going to sleep.
			markThreadCondwait(realCond);

      // Add the event into eventlist
			if(abstime == NULL) {
      	ret = Real::pthread_cond_wait(realCond, realMutex);
			}
			else {
      	ret = Real::pthread_cond_timedwait(realCond, realMutex, abstime);
			}
      PRINF("cond_wait wakeup for thread %d\n", current->index);
      
			// This thread exits cond_wait status	
			unmarkThreadCondwait(realMutex);

			// Record the waking up of conditional variable
			list = getSyncEventList(mutex, sizeof(pthread_mutex_t));
      list->recordSyncEvent(E_SYNC_MUTEX_LOCK, ret);
		
    } else {
			list = getSyncEventList(mutex, sizeof(pthread_mutex_t));

			// Before the condWait, we will have to release the current lock.			
			struct syncEvent* nextEvent = list->advanceSyncEvent();
      if(nextEvent) {
        _sync.signalNextThread(nextEvent);
      }

			// Then we will have a mutex_lock after wakenup.
      ret = _sync.peekSyncEvent(list);

      if(ret == 0) {
        // Now waiting for the lock
        waitSemaphore();
      }
				
			_sync.advanceThreadSyncList();
    }

    return ret;
  }
  
  int cond_broadcast(pthread_cond_t* cond) { 
    pthread_cond_t* realCond = (pthread_cond_t*)getSyncEntry(cond);
    if(isInvalidSyncVar(realCond)) {
      cond_init((pthread_cond_t*)cond, NULL);
      realCond = (pthread_cond_t*)getSyncEntry(cond);
    }

    if(!doubletake::isRollback) {
			return Real::pthread_cond_broadcast(realCond); 
		}
		else {
			// During the rollback pahse, this should be NULL. 
			assert(realCond != NULL);
			return 0;
		}
	}

  int cond_signal(pthread_cond_t* cond) { 
    pthread_cond_t* realCond = (pthread_cond_t*)getSyncEntry(cond);
    if(isInvalidSyncVar(realCond)) {
      cond_init((pthread_cond_t*)cond, NULL);
      realCond = (pthread_cond_t*)getSyncEntry(cond);
    }

    if(!doubletake::isRollback) {
			return Real::pthread_cond_signal(realCond); 
		}
		else {
			// During the rollback pahse, this should be NULL. 
			assert(realCond != NULL);
			return 0;
		}
	}

  // Barrier initialization. Typically, barrier_init is to be called initially	
  int barrier_init(pthread_barrier_t* barrier, const pthread_barrierattr_t* attr, unsigned int count) {
    int result = 0;
    
    if(!doubletake::isRollback) {
      // Allocate the mutex and cond related to this barrier.
			pthread_cond_t * cond;
			pthread_mutex_t * mutex;
			size_t totalSize = sizeof(pthread_cond_t) + sizeof(pthread_mutex_t) + sizeof(BarrierInfo);
			void * ptr = allocRealSyncVar(totalSize, E_SYNC_BARRIER);
			BarrierInfo * info;

			// Set the real barrier
			if(setSyncEntry(E_SYNCVAR_BARRIER, barrier, ptr, totalSize)) {
				PRINF("can't set synchronization entry??");
				assert(0);
			}
			PRINF("barrier_init barrier %p pointing to %p (while next %p) with count %d\n", barrier, ptr, *((void **)((intptr_t)barrier + sizeof(void *))), count); 	

			// Initialize it.
			getBarrierInfo(barrier, &mutex, &cond, &info);

      // Actual initialization
			Real::pthread_cond_init(cond, NULL);
			Real::pthread_mutex_init(mutex, NULL);

			info->maxThreads = count;
			info->isArrivalPhase = true;
			info->waitingThreads = 0;
    }
		else {
			resetSyncEntry(E_SYNCVAR_BARRIER, barrier);	
		}
    return result;
  }

  int barrier_destroy(pthread_barrier_t* barrier) {
    if(!doubletake::isRollback) {
		//	PRINF("barrier_destroy on barrier %p next %p\n", barrier, *((void **)((intptr_t)barrier + sizeof(void *))));
    	deferSync((void*)barrier, E_SYNCVAR_BARRIER);
		//	PRINF("barrier_destroy on barrier %p done\n", barrier);
		}
    return 0;
  }

  ///// mutex functions
	void getBarrierInfo(pthread_barrier_t * barrier, pthread_mutex_t ** mutex, pthread_cond_t ** cond, BarrierInfo ** info) {
		// Get the real synchronization entry			
		void * ptr = getSyncEntry(barrier);
		assert(ptr != NULL);

		*mutex = (pthread_mutex_t *)(ptr);
		*cond = (pthread_cond_t *)((unsigned long)ptr+ sizeof(pthread_mutex_t));
		*info = (BarrierInfo *)((intptr_t)(*cond) + sizeof(pthread_cond_t));
	}

  // Add the barrier support. We have to re-design the barrier since 
	// it is impossible to wake a thread up when it is waiting on the barrier.
	// Thus, it is impossible to rollback such a thread.
  int barrier_wait(pthread_barrier_t* barrier) {
    int ret = 0;
		pthread_mutex_t * mutex;
		pthread_cond_t * cond;
		BarrierInfo * info;

		// Get the actual barrier information
		getBarrierInfo(barrier, &mutex, &cond, &info);
		//PRINF("barrier_wait barrier %p mutex %p cond %p info %p\n", barrier, mutex, cond, info); 	

		assert(mutex != NULL);
		setThreadUnsafe();
	
		Real::pthread_mutex_lock(mutex);

		// Check whether it is at the arrival phase
		while(info->isArrivalPhase != true) {
     		Real::pthread_cond_wait(cond, mutex);
		}

		// Now in an arrival phase, proceed with barrier synchronization
    info->waitingThreads++;

		if(info->waitingThreads >= info->maxThreads) {
      info->isArrivalPhase = false;

			// Waking up all threads that are waiting on this barrier now.
      Real::pthread_cond_broadcast(cond);
    } 
		else {
			assert(info->isArrivalPhase == true);

			// We are waiting on the barrier now.
      while(info->isArrivalPhase == true) {
				markThreadCondwait(cond);
        Real::pthread_cond_wait(cond, mutex);
				unmarkThreadCondwait(mutex);
      }
		}

		info->waitingThreads--;	

		// When all threads leave the barrier, entering into the new arrival phase.  
    if(info->waitingThreads == 0) {
      info->isArrivalPhase = true;

			// Wakeup all threads that are waiting on the arrival fence
      Real::pthread_cond_broadcast(cond);
    }

		Real::pthread_mutex_unlock(mutex);
		setThreadSafe();

    return ret;
  }

  /*
    // Support for sigwait() functions in order to avoid deadlock.
    int sig_wait(const sigset_t *set, int *sig) {
      int ret;
      waitToken();
      epochEnd(false);

      ret = determ::getInstance().sig_wait(set, sig, _thread_index);
      if(ret == 0) {
        epochBegin(true);
      }

      return ret;
    }
  */

  /*
    // Now we need to save the context
    inline static void saveContext() {
      size_t size;

      PRINF("SAVECONTEXT: Current %p current->privateTop %p at %p thread index %d\n", current,
    current->privateTop, &current->privateTop, current->index);
      // Save the stack at first.
      current->privateStart = &size;
      size = size_t((intptr_t)current->privateTop - (intptr_t)current->privateStart);
      current->backupSize = size;

      if(size >= current->totalPrivateSize) {
        PRINF("Wrong. Current stack size (%lx = %p - %p) need to backup is larger than" \
                "total size (%lx). Either the application called setrlimit or the implementation" \
                "is wrong.\n", size, current->privateTop, current->privateStart,
    current->totalPrivateSize);
        Real::exit(-1);
      }

     // PRINF("privateStart %p size %lx backup %p\n", current->privateStart, size, current->backup);
      memcpy(current->backup, current->privateStart, size);
      // We are trying to save context at first
      getcontext(&current->context);
    }
  */

  // Save the given signal handler.
  void saveContext(ucontext_t* context) {
    current->context.save(context);
  }

  // Return actual thread index
  int getIndex() { return current->index; }

  // Preparing the rollback.
  void prepareRollback();
	void prepareRollbackAlivethreads();
	void destroyAllSemaphores();
	void initThreadSemaphore(thread_t* thread);
	void destroyThreadSemaphore(thread_t* thread);
	void wakeupOldWaitingThreads();

  static void epochBegin(thread_t * thread);

  // Rollback the current thread
  static void rollbackCurrent();
	void checkRollbackCurrent();

	// It is called when a thread has to rollback.
	// Thus, it will replace the current context (of signal handler)
	// with the old context.
  void rollbackInsideSignalHandler(ucontext* uctx) {
    current->context.rollbackInHandler(uctx);
  }

  inline static pthread_t thread_self() { return Real::pthread_self(); }

  inline static void saveContext() {
    current->context.saveCurrent();
  };

  inline static void restoreContext() {
    PRINF("restore context now (ROLLBACK)");
    current->context.rollback();
  };

  // Now we will change the newContext to old context.
  // And also, we will restore context based on newContext.
  inline static void resetContexts() {
    assert(0);
  }

  // We will rollback based on old context. We will leave newContext intactly
  inline static void rollbackContext() { assert(0); }

  // Run those deferred synchronization.
  inline void runDeferredSyncs() { _thread.runDeferredSyncs(); }

  //
  inline bool hasReapableThreads() { return _thread.hasReapableThreads(); }

  inline static void enableCheck() {
    current->internalheap = false;
    current->disablecheck = false;
    // PRINF("Enable check for current %p disablecheck %d\n", current, current->disablecheck);
  }

  inline static bool isCheckDisabled() { return current->disablecheck; }

  inline static void disableCheck() {
    current->internalheap = true;
    current->disablecheck = true;
    // PRINF("Disable check for current %p disablecheck %d\n", current, current->disablecheck);
  }

  inline static pid_t gettid() { return syscall(SYS_gettid); }

  static void invokeCommit();
  bool addQuarantineList(void* ptr, size_t sz);
  static bool isThreadSafe(thread_t * thread);

private:
  inline void* getSyncEntry(void* entry) {
    void** ptr = (void**)entry;
    return (*ptr);
  }

	inline void  resetSyncEntry(syncVariableType type, void * nominal) {
			// Geting the starting address of real synchronization variable.
			void * real = _sync.retrieveRealSyncEntry(type, nominal);
			*((void **)nominal) = real;
	}

  inline SyncEventList* getSyncEventList(void* ptr, size_t size) {
    void** entry = (void**)ptr;
    //    PRINF("ptr %p *entry is %p, size %ld\n", ptr, *entry, size);
    return (SyncEventList*)((intptr_t)(*entry) + size);
  }

  inline int setSyncEntry(syncVariableType type, void* syncvar, void* realvar, size_t size) {
		int ret = -1;
    unsigned long* target = (unsigned long*)syncvar;
    unsigned long expected = *(unsigned long*)target;

    // if(!__sync_bool_compare_and_swap(target, expected, (unsigned long)realvar)) {
    if(!__atomic_compare_exchange_n(target, &expected, (unsigned long)realvar, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
      deallocRealSyncVar(realvar);
    } else {
			ret = 0;
      // It is always safe to add this list into corresponding map since
      // only those passed the CAS instruction can call this.
      // Thus, it is not existing two lists corresponding to the same synchronization variable.
      SyncEventList* list = getSyncEventList(syncvar, size);

      // Adding this entry to global synchronization map
     	void * syncentry = _sync.recordSyncVar(type, (void*)syncvar, realvar, list);
			*((void **)((intptr_t)syncvar + sizeof(void *))) = syncentry;
			//if(type == E_SYNCVAR_BARRIER) {
			//	PRINF("Barrier setSyncEntry: syncvar %p next %p syncentry %p\n", syncvar,  *((void **)((intptr_t)syncvar + sizeof(void *))), syncentry);
			//}
    }

		return ret;
  }

  inline void* allocRealSyncVar(int size, thrSyncCmd synccmd) {
    // We allocate a synchorniation event list here and attach to this real
    // synchronziation variable so that they can be deleted in the same time.
    void* entry = ((void*)InternalHeap::getInstance().malloc(size + sizeof(SyncEventList)));
    assert(entry != NULL);
    void* ptr = (void*)((intptr_t)entry + size);

    // Using placement new here
    new (ptr) SyncEventList(entry, synccmd);
    return entry;
  }

  inline void deallocRealSyncVar(void* ptr) { InternalHeap::getInstance().free(ptr); }

  // Acquire the semaphore for myself.
  // If it is my turn, I should get the semaphore.
  static void waitSemaphore() {
    semaphore* sema = &current->sema;

		PRINF("wait on semaphore %p", sema);
    sema->get();
		PRINF("Get the semaphore %p", sema);
  }

  semaphore* getSemaphore() { return &current->sema; }

  // Newly created thread should call this.
  inline static void threadRegister(bool isMainThread) {
    pid_t tid = gettid();
    void* privateTop;
    size_t stackSize = __max_stack_size;

    current->self = Real::pthread_self();

    // Initialize event pool for this thread.
    listInit(&current->pendingSyncevents);

    // Lock the mutex for this thread.
    lock_thread(current);

    // Initialize corresponding cond and mutex.
    //listInit(&current->list);

    current->tid = tid;
    current->status = E_THREAD_RUNNING;
    current->isNewlySpawned = true;

    current->disablecheck = false;

    // FIXME: problem
    current->joiner = NULL;

    // Initially, we should set to check system calls.
    enableCheck();

    // Initialize the localized synchronization sequence number.
    // pthread_t thread = current->self;
    pthread_t thread = pthread_self();

    if(isMainThread) {
      void* stackBottom;
      current->mainThread = true;

      // First, we must get the stack corresponding information.
      regioninfo ri = doubletake::findStack(gettid());
      stackBottom = ri.start;
      privateTop = ri.end;
    } else {
      /*
        Currently, the memory layout of a thread private area is like the following.
          ----------------------  Higher address
          |      TCB           |
          ---------------------- pd (pthread_self)
          |      TLS           |
          ----------------------
          |      Stacktop      |
          ---------------------- Lower address
      */
      current->mainThread = false;
      // Calculate the top of this page.
      privateTop = (void*)(((intptr_t)thread + xdefines::PageSize) & ~xdefines::PAGE_SIZE_MASK);
    }

    current->context.setupStackInfo(privateTop, stackSize);
    current->stackTop = privateTop;
    current->stackBottom = (void*)((intptr_t)privateTop - stackSize);

    // Now we can wakeup the parent since the parent must wait for the registe
    signal_thread(current);

    PRINF("THREAD%d (pthread_t %p) registered at %p, status %d wakeup %p. lock at %p",
          current->index, (void*)current->self, current, current->status, &current->cond,
          &current->mutex);

    unlock_thread(current);
    if(!isMainThread) {
      // Save the context for non-main thread.
      saveContext();
    }

    // WARN("THREAD%d (pthread_t %p) registered at %p", current->index, current->self, current );
    PRINF("THREAD%d (pthread_t %p) registered at %p, status %d", current->index,
          (void*)current->self, current, current->status);
  }

  static bool isThreadDetached() { return current->isDetached; }

  /// @ internal function: allocation a thread index
  int allocThreadIndex() { return _thread.allocThreadIndex(); }

  inline thread_t* getThreadInfo(int index) { return _thread.getThreadInfo(index); }

  inline thread_t* getThread(pthread_t thread) {
    return threadmap::getInstance().getThreadInfo(thread);
  }

  // Actually calling both pdate both thread event list and sync event list.
  inline void updateSyncEvent(SyncEventList* list) {
		// Advance the thread eventlist
		_sync.advanceThreadSyncList();

    struct syncEvent* event = list->advanceSyncEvent();
    if(event) {
      // Actually, we will wakeup next thread on the event list.
      // Since cond_wait will call unlock at first.
      _sync.signalNextThread(event);
    }
  }

  inline void insertAliveThread(thread_t* thread, pthread_t tid) {
    threadmap::getInstance().insertAliveThread(thread, tid);
  }

  static bool isStackVariable(void* ptr) {
    return (ptr >= current->stackBottom && ptr <= current->stackTop);
  }

  // Insert a synchronization variable into the global list, which
  // are reaped later in the beginning of next epoch.
  inline bool deferSync(void* ptr, syncVariableType type) {
		if(type == E_SYNCVAR_THREAD) {
			return _thread.deferSync(ptr, type);
		}
		else {
			xsync::SyncEntry * entry = (xsync::SyncEntry *)(*((void **)((intptr_t)ptr + sizeof(void *))));

			//if(type == E_SYNCVAR_BARRIER) {
			//	PRINF("Barrier before detroy ptr %p entry %p\n", ptr, entry);
			//}
			_sync.deferSync(entry);
			return true;
		}
  }

  static void setThreadSafe();
  static void setThreadUnsafe();

  // @Global entry of all entry function.
  static void *startThread(void *arg);

  xsync _sync;
	SysRecord _sysrecord;
  threadinfo _thread;
  SyncEventList * _spawningList;
};

#endif
