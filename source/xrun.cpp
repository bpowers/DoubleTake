/*
 * @file   xrun.cpp
 * @brief  The main engine for consistency management, etc.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include "xrun.hh"

#include <assert.h>
#include <pthread.h>
#include <ucontext.h>

#include "globalinfo.hh"
#include "internalsyncs.hh"
#include "leakcheck.hh"
#include "syscalls.hh"
#include "threadmap.hh"
#include "threadstruct.hh"

static void jumpToFunction(ucontext_t* cxt, unsigned long funcaddr);

void xrun::initialize() {
  // initialized is always called before we've clone(2)'ed any other
  // threads.  Just check the global, no need for locks.
  if (doubletake::initialized) {
    return;
  }

  //    PRINT("xrun: initialization at line %d\n", __LINE__);
  struct rlimit rl;

  // Get the stack size.
  if(Real::getrlimit(RLIMIT_STACK, &rl) != 0) {
    PRWRN("Get the stack size failed.\n");
    Real::exit(-1);
  }

  // Check the stack size.
  __max_stack_size = rl.rlim_cur;

  // Initialize the internal heap at first.
  InternalHeap::getInstance().initialize();

  // next is _memory, as xthread::threadRegister calls into
  // xmemory::findStack.
  _memory.initialize();

  // Initialize the locks and condvar used in epoch switches
  global_initialize();

  installSignalHandlers();

  _thread.initialize();

  syscallsInitialize();

  doubletake::initialized = true;
}

bool xrun::isDoubleTake(void *pcaddr) {
  return _memory.isDoubleTake(pcaddr);
}

void xrun::finalize() {
  //    PRINT("%d: finalize now !!!!!\n", getpid());
  // If we are not in rollback phase, then we should check buffer overflow.
  if(!doubletake::isRollback) {
#ifdef DETECT_USAGE_AFTER_FREE
    finalUAFCheck();
#endif

    epochEnd(true);
  }

  //    PRINF("%d: finalize now !!!!!\n", getpid());
  // Now we have to cleanup all semaphores.
  _thread.finalize();
}


void xrun::syscallsInitialize() {
  syscalls::getInstance().initialize();
}

void xrun::rollback() {
  // If this is the first time to rollback,
  // then we should rollback now.
  if(doubletake::hasRollbacked) {
    FATAL("HAS rolled back, now exiting.");
  }

  // Rollback all memory before rolling back the context.
  _memory.rollback();

  PRINF("AFTER MEMORY ROLLBACK!!!\n");

  // We must prepare the rollback, for example, if multiple
  // threads is existing, we must initiate the semaphores for threads
  // Also, we should setup those synchronization event list
  _thread.prepareRollback();
  PRINF("_thread rollback and actual rollback");

  // Now we are going to rollback
  PRINF("STARTING ROLLBACK!!!\n");

  // Now time to rollback myself.
  _thread.checkRollbackCurrent();

  assert(0);
}

/// @brief Start a new epoch.
void xrun::epochBegin() {

  threadmap::aliveThreadIterator i;

  PRINF("xrun epochBegin, joinning every thread.");
  for(i = threadmap::getInstance().begin(); i != threadmap::getInstance().end(); i++) {
    thread_t* thread = i.getThread();

    lock_thread(thread);

    if(thread != current && thread->hasJoined) {
      PRINF("xrun, joining thread %d", thread->index);
      thread->status = E_THREAD_EXITING;
      Real::pthread_cond_signal(&thread->cond);
      unlock_thread(thread);
      PRINF("xrun, actually joining thread %d", thread->index);
      Real::pthread_join(thread->self, NULL);
      PRINF("xrun, after joining thread %d", thread->index);
      continue;
    }
    // Since now we are in a new epoch mark all existing threads as old threads
    thread->isNewlySpawned = false;

    // cleanup the threads's qlist, pendingSyncevents, syncevents
    xthread::epochBegin(thread);

    unlock_thread(thread);
  }
  PRINF("xrun epochBegin, joinning every thread done.");

  xthread::runDeferredSyncs();

  PRINF("xrun epochBegin, run deferred synchronizations done.");

  // Now waken up all other threads then threads can do its cleanup.
  PRINF("getpid %d: xrun::epochBegin, wakeup others.", getpid());
  global_epochBegin();

  PRINF("getpid %d: xrun::epochBegin", getpid());

  // Saving the context of the memory.
  _memory.epochBegin();

  // Save the context of this thread
  saveContext();
}

/// @brief End a transaction, aborting it if necessary.
void xrun::epochEnd(bool endOfProgram) {

  PRINF("xrun epochEnd");

  // Tell other threads to stop and save context.
  stopAllThreads();

  // if our process has already begun a rollback, proceed no further.
  // This loop terminates when SIGUSR2 is called on this thread.
  if(doubletake::isRollback) {
    // PRINF("in the end of an epoch, endOfProgram %d. global_isRollback true\n", endOfProgram);
    while(1)
      ;
  }

#if defined(DETECT_OVERFLOW)
  bool hasOverflow = false;
  hasOverflow = _memory.checkHeapOverflow();
#endif

#if defined(DETECT_MEMORY_LEAKS)
  bool hasMemoryLeak = false;
  if(endOfProgram) {
    //  PRINF("DETECTING MEMORY LEAKAGE in the end of program!!!!\n");
    hasMemoryLeak =
      leakcheck::getInstance().doFastLeakCheck(_memory.getHeapBegin(), _memory.getHeapEnd());
  } else {
    // PRINF("DETECTING MEMORY LEAKAGE inside a program!!!!\n");
    hasMemoryLeak =
      leakcheck::getInstance().doSlowLeakCheck(_memory.getHeapBegin(), _memory.getHeapEnd());
  }
#endif

#ifndef EVALUATING_PERF
// First, attempt to commit.
#if defined(DETECT_OVERFLOW) && defined(DETECT_MEMORY_LEAKS)
  PRINF("in the end of an epoch, hasOverflow %d hasMemoryLeak %d", hasOverflow, hasMemoryLeak);
  if(hasOverflow || hasMemoryLeak) {
    rollback();
  } else {
#elif defined(DETECT_OVERFLOW)
  PRINF("in the end of an epoch, hasOverflow %d", hasOverflow);
  if(hasOverflow) {
    rollback();
  } else {
#elif defined(DETECT_MEMORY_LEAKS)
  if(hasMemoryLeak) {
    // EDB FIX ME DISABLED
    // _memory.cleanupFreeList();
    rollback();
  } else {
#endif
#endif

  PRINF("before calling syscalls epochEndWell");
  syscalls::getInstance().epochEndWell();

  _thread.epochEndWell();

#ifndef EVALUATING_PERF
#if defined(DETECT_OVERFLOW) || defined(DETECT_MEMORY_LEAKS)
  }
#endif
#endif
  //PRINF("in the end of an epoch, hasOverflow %d hasMemoryLeak %d\n", hasOverflow, hasMemoryLeak);
}

#ifdef DETECT_USAGE_AFTER_FREE
void xrun::finalUAFCheck() {
  threadmap::aliveThreadIterator i;
  // Check all threads' quarantine list
  for(i = threadmap::getInstance().begin(); i != threadmap::getInstance().end(); i++) {
    thread_t* thread = i.getThread();
    if(thread->qlist.finalUAFCheck()) {
      rollback();
    }
  }
}
#endif

void waitThreadSafe(void) {
  int i = 0;
  while(i++ < 0x10000) ;
}

void xrun::stopAllThreads() {
  threadmap::aliveThreadIterator i;
  int waiters = 0;
  /* According to description of pthread_kill:
     One comment: The PID field in the TCB can temporarily be changed
     (in fork).  But this must not affect this code here.  Since this
     function would have to be called while the thread is executing
     fork, it would have to happen in a signal handler.  But this is
     no allowed, pthread_kill is not guaranteed to be async-safe.  */
  global_checkWaiters();

  // Used to tell other threads about the end of current epoch end since one have to commit.
  global_setEpochEnd();

  // Grab the global lock in order to avoid the thread spawning in this phase.
  global_lock();

  // PRINF("EPOCHEBD:Current thread at %p self %p\n", current, pthread_self());
  PRINF("---- EPOCHEBD:Current thread at %p THREAD%d self %p. "
        "Stopping other threads",
        current, current->index, (void*)pthread_self());

  // Traverse the thread map to check the status of every thread.
  for(i = threadmap::getInstance().begin(); i != threadmap::getInstance().end(); i++) {
    thread_t* thread = i.getThread();

    // we only care about other threads
    if(thread != current) {
      lock_thread(thread);

      while(!xthread::isThreadSafe(thread)) {
        // wait the thread to be safe.
        waitThreadSafe();
      }
      // If the thread's status is already at E_THREAD_WAITFOR_REAPING
      // or E_THREAD_JOINING, thus waiting on internal lock, do nothing since they have stopped.
      if((thread->status != E_THREAD_WAITFOR_REAPING) && (thread->status != E_THREAD_JOINING) && (thread->status != E_THREAD_COND_WAITING)) {
        waiters++;
        PRINF("kill thread %d\n", thread->index);
        Real::pthread_kill(thread->self, SIGUSR2);
      }
      unlock_thread(thread);
    }
  }

  if(waiters != 0) {
    global_waitThreadsStops(waiters);
  }

  global_unlock();
}

bool isNewThread() { return current->isNewlySpawned; }

void jumpToFunction(ucontext_t* cxt, unsigned long funcaddr) {
  //PRINF("%p: inside signal handler %p.\n", (void*)pthread_self(),
  //      (void*)cxt->uc_mcontext.gregs[REG_RIP]);
  // selfmap::getInstance().printCallStack(NULL, NULL, true);
  cxt->uc_mcontext.gregs[REG_RIP] = funcaddr;
}

void xrun::rollbackFromSegv()
{
  xrun::getInstance().rollbackFromSegvSignal();
}

void xrun::rollbackFromSegvSignal() {
  PRWRN("rolling back due to SEGV");

  // Check whether the segmentation fault is called by buffer overflow.
  if(_memory.checkHeapOverflow()) {
    PRINF("OVERFLOW causes segmentation fault!!!! ROLLING BACK\n");
  }

	rollback();
}

/*
  We are using the SIGUSR2 to stop other threads.
*/
void xrun::sigusr2Handler(int /* signum */, siginfo_t* /* siginfo */, void* uctx) {
  xrun::getInstance().endOfEpochSignal((ucontext_t *)uctx);
}

void xrun::endOfEpochSignal(ucontext_t *uctx) {
  // Check what is current status of the system.
  // If we are in the end of an epoch, then we save the context somewhere since
  // current thread is going to stop execution in order to commit or rollback.
  assert(doubletake::isEpochEnd());

  // Wait for notification from the commiter
  global_waitForNotification();

  // Check what is the current phase
  if(doubletake::isEpochBegin()) {
    // Current thread is going to enter a new phase
    _thread.saveContext((ucontext_t*)uctx);
    // NOTE: we do not need to reset contexts if we are still inside the signal handleer
    // since the exiting from signal handler can do this automatically.
  } else {
    PRINF("epochBegin %d rollback %d\n", doubletake::isEpochBegin(), doubletake::isRollback.load());
    assert(doubletake::isRollback);

    // Check where we should park, on my own cond or common cond
    if(isNewThread()) {
      lock_thread(current);

      // Waiting for the waking up from the its parent thread
      while(current->status != E_THREAD_ROLLBACK) {
        Real::pthread_cond_wait(&current->cond, &current->mutex);
      }

      unlock_thread(current);
    }
    // Rollback inside signal handler is different
    _thread.rollbackInsideSignalHandler((ucontext_t*)uctx);
  }
  // Jump to a function and wait for the instruction of the committer thread.
}

void xrun::sigsegvHandler(int /* signum */, siginfo_t* siginfo, void* uctx) {
    jumpToFunction((ucontext_t*)uctx, (unsigned long)rollbackFromSegv);
}

void xrun::installSignalHandlers() {
  // Set up an alternate signal stack.
  static stack_t altstack;
  memset(&altstack, 0, sizeof(altstack));
  altstack.ss_sp = MM::mmapAllocatePrivate(SIGSTKSZ);
  altstack.ss_size = SIGSTKSZ;
  altstack.ss_flags = 0;
  Real::sigaltstack(&altstack, (stack_t *)NULL);

  /**
     Some parameters used here:
     SA_RESTART: Provide behaviour compatible with BSD signal
                 semantics by making certain system calls restartable across signals.
     SA_SIGINFO: The  signal handler takes 3 arguments, not one.  In this case, sa_sigac-
                 tion should be set instead of sa_handler.
     So, we can acquire the user context inside the signal handler

     We do NOT want SA_RESTART - that will cause the signal to
     potentially be recursively received.
  */

  struct sigaction sigusr2;
  memset(&sigusr2, 0, sizeof(sigusr2));
  // no need to explicitly set SIGUSR2 in the mask - it is
  // automatically blocked while the handler is running.
  sigemptyset(&sigusr2.sa_mask);
  sigusr2.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigusr2.sa_sigaction = xrun::sigusr2Handler;
  if(Real::sigaction(SIGUSR2, &sigusr2, NULL) == -1) {
    FATAL("sigaction(SIGUSR2): %d (%s)", errno, strerror(errno));
  }

  struct sigaction sigsegv;
  memset(&sigsegv, 0, sizeof(sigsegv));
  // no need to explicitly set SIGSEGV in the mask - it is
  // automatically blocked while the handler is running.
  sigemptyset(&sigsegv.sa_mask);
  sigsegv.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigsegv.sa_sigaction = xrun::sigsegvHandler;
  if(Real::sigaction(SIGSEGV, &sigsegv, NULL) == -1) {
    FATAL("sigaction(SIGSEGV): %d (%s)", errno, strerror(errno));
  }
}
