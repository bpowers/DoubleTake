#if !defined(DOUBLETAKE_THREADINFO_H)
#define DOUBLETAKE_THREADINFO_H

/*
 * @file   threadinfo.h
 * @brief  Manageing the information about threads.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "globalinfo.hh"
#include "internalheap.hh"
#include "list.hh"
#include "mm.hh"
#include "real.hh"
#include "sysrecord.hh"
#include "recordentries.hh"
#include "threadmap.hh"
#include "threadstruct.hh"
#include "xcontext.hh"
#include "xdefines.hh"

typedef enum e_syncVariable {
  E_SYNCVAR_THREAD = 0,
  E_SYNCVAR_COND,
  E_SYNCVAR_MUTEX,
  E_SYNCVAR_BARRIER
} syncVariableType;

// Each synchronization variable will have a corresponding list.
struct deferSyncVariable {
  list_t list;
  syncVariableType syncVarType;
  void* variable;
};

/// @class threadinfo
class threadinfo {
public:
  explicit threadinfo()
    : _aliveThreads(0), _reapableThreads(0), _totalThreads(0), _threadIndex(0),
      _deferSyncs() {
    memset(_threads, 0, sizeof(_threads[0]) * xdefines::MAX_ALIVE_THREADS);
  }

  void initialize() {
    struct rlimit rl;

    // Get the stack size.
    if(Real::getrlimit(RLIMIT_STACK, &rl) != 0) {
      PRWRN("Get the stack size failed.\n");
      Real::exit(-1);
    }

    // if there is no limit for our stack size, then just pick a
    // reasonable limit.
    if (rl.rlim_cur == (rlim_t)-1) {
      rl.rlim_cur = 2048*4096; // 8 MB
    }

    __max_stack_size = 2048*4096; // rl.rlim_cur;

    _aliveThreads = 0;
    _reapableThreads = 0;
    _threadIndex = 0;

    _totalThreads = xdefines::MAX_ALIVE_THREADS;

    // Shared the threads information.
    memset(&_threads, 0, sizeof(_threads));

    // Initialize the backup stacking information.
    size_t perStackSize = __max_stack_size;

    unsigned long totalStackSize = (unsigned long)perStackSize * xdefines::MAX_ALIVE_THREADS;
    unsigned long perQbufSize = xdefines::QUARANTINE_BUF_SIZE * sizeof(freeObject);
    unsigned long qbufSize = perQbufSize * xdefines::MAX_ALIVE_THREADS * 2;

    char* stackStart = (char*)MM::mmapAllocatePrivate(totalStackSize);
    char* qbufStart = (char*)MM::mmapAllocatePrivate(qbufSize);

    // Initialize all mutex.
    DT::Thread* tinfo;

    for(int i = 0; i < xdefines::MAX_ALIVE_THREADS; i++) {
      tinfo = &_threads[i];

			// Those information that are only initialized once.
     	tinfo->available = true;
      tinfo->origIndex = i;
      tinfo->context.setupBackup(&stackStart[perStackSize * i]);
      tinfo->qlist.initialize(&qbufStart[perQbufSize * i * 2], perQbufSize);
    }

    // Initialize the total event list.
    listInit(&_deferSyncs);
  }

  void finalize() {}

  /// @ internal function: allocation a thread index when spawning.
  /// Since we guarantee that only one thread can be in spawning phase,
  /// there is no need to acqurie the lock here.
  DT::Thread *allocThread() {
    int index = -1;

		// Return a failure if the number of alive threads is larger than 
    if(_aliveThreads >= _totalThreads) {
      return nullptr;
    }

    int origindex = _threadIndex;
    DT::Thread* thread;
    while(true) {
      thread = getThreadInfo(_threadIndex);
      if(thread->available) {
        thread->available = false;
        index = _threadIndex;

        // A thread is counted as alive when its structure is allocated.
        _aliveThreads++;

        _threadIndex = (_threadIndex + 1) % _totalThreads;
				thread->initialize(index);
        break;
      } else {
        _threadIndex = (_threadIndex + 1) % _totalThreads;
      }

      // It is impossible that we search the whole array and we can't find
      // an available slot.
      assert(_threadIndex != origindex);
    }
    return thread;
  }

  inline DT::Thread *getThreadInfo(int index) { return &_threads[index]; }

  inline DT::Thread *getThread(pthread_t thread) {
    return threadmap::getInstance().getThreadInfo(thread);
  }

  inline char* getThreadBuffer(int index) {
    DT::Thread* thread = getThreadInfo(index);

    return thread->outputBuf;
  }

	// Everytime, a pthread_join call will put the joinee into the queue of deadthreads.
  inline int incrementReapableThreads() {
    _reapableThreads++;
    return _reapableThreads;
  }

  inline bool hasReapableThreads() {
    // fprintf(stderr, "_reapableThreads %d\n", _reapableThreads);
    return (_reapableThreads != 0);
  }

  // Insert a synchronization variable into the global list, which
  // are reaped later at commit points.
  inline bool deferSync(void* ptr, syncVariableType type) {
    struct deferSyncVariable* syncVar = NULL;
		bool toReapThreads = false;

    syncVar = (struct deferSyncVariable*)InternalHeap::getInstance().malloc(
        sizeof(struct deferSyncVariable));

    if(syncVar == NULL) {
      fprintf(stderr, "No enough private memory, syncVar %p\n", (void *)syncVar);
      return toReapThreads;
    }

    listInit(&syncVar->list);
    syncVar->syncVarType = type;
    syncVar->variable = ptr;

    //global_lock();

    listInsertTail(&syncVar->list, &_deferSyncs);
    if(type == E_SYNCVAR_THREAD) {
      int reapThreads = incrementReapableThreads();

			if((reapThreads >= xdefines::MAX_REAPABLE_THREADS) && (_aliveThreads - reapThreads) == 1) {
				toReapThreads = true;
			}
    }

    //global_unlock();

		return toReapThreads; 
  }

  void cancelAliveThread(pthread_t thread) {
    DT::Thread* deadThread = getThread(thread);

    global_lock();

    threadmap::getInstance().removeAliveThread(deadThread);
    _aliveThreads--;
    _reapableThreads--;
    global_unlock();
  }

  // We actually get those parameter about new created thread
  /*
      E_SYNCVAR_THREAD = 0,
      E_SYNCVAR_COND,
      E_SYNCVAR_MUTEX,
      E_SYNCVAR_BARRIER
  */
  void runDeferredSyncs() {
    list_t* entry;
    global_lock();

    // Get all entries from _deferSyncs.
    while((entry = listRetrieveItem(&_deferSyncs)) != NULL) {
      struct deferSyncVariable* syncvar = (struct deferSyncVariable*)entry;
      fprintf(stderr, "runDeferredSyncs with type %d variable %p\n", syncvar->syncVarType, (void *)syncvar->variable);

      switch(syncvar->syncVarType) {
				
      case E_SYNCVAR_THREAD: {
				//fprintf(stderr, "runDeferredSyncs with type %d variable %p\n", syncvar->syncVarType, (thread_t*)syncvar->variable);
        threadmap::getInstance().removeAliveThread((DT::Thread*)syncvar->variable);
        _aliveThreads--;
        _reapableThreads--;
        break;
      }

      case E_SYNCVAR_COND: {
        Real::pthread_cond_destroy((pthread_cond_t*)syncvar->variable);
        break;
      }

      case E_SYNCVAR_MUTEX: {
        Real::pthread_mutex_destroy((pthread_mutex_t*)syncvar->variable);
        break;
      }

      case E_SYNCVAR_BARRIER: {
        break;
      }

      default:
        assert(0);
        break;
      }
      
			// We should deallocate the actual synchronization variable.
			if(syncvar->syncVarType != E_SYNCVAR_THREAD) {
				void** ptr = (void**)syncvar->variable;
				InternalHeap::getInstance().free(*ptr);
			}

      // Free this entry.
      InternalHeap::getInstance().free((void*)entry);
    }

    listInit(&_deferSyncs);
		global_unlock();
  }

private:
  int _aliveThreads;    // a. How many alive threads totally.
  int _reapableThreads; // a. How many alive threads totally.
  int _totalThreads;    // b. How many alive threads we can hold
  int _threadIndex;     // b. What is the current thread index.
                        // list_t  _aliveList;    // List of alive threads.
                        // list_t  _deadList;     // List of dead threads.
  list_t _deferSyncs;   // deferred synchronizations.
                        // pthread_mutex_t _mutex; // Mutex to protect these list.
  DT::Thread _threads[xdefines::MAX_ALIVE_THREADS];
  /*
    char * position;     // c. What is the global heap metadata.
    size_t remainingsize; //
    h. User thread mapping address.
    i. Maybe the application text start and text end.
    j. Those mapping address
  */
};

#endif
