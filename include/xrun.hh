#if !defined(DOUBLETAKE_XRUN_H)
#define DOUBLETAKE_XRUN_H

/*
 * @file   xrun.h
 * @brief  The main engine for consistency management, etc.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

#include <new>

#include "globalinfo.hh"
#include "internalheap.hh"
#include "log.hh"
#include "leakcheck.hh"
#include "mm.hh"
#include "real.hh"
#include "watchpoint.hh"
#include "xdefines.hh"
#include "xmemory.hh"
#include "xthread.hh"

class xrun {

private:
  xrun()
      : _memory(xmemory::getInstance()), _thread(xthread::getInstance()),
        _watchpoint(watchpoint::getInstance()), _leakcheck()
  {
    // PRINF("xrun constructor\n");
  }

public:

  static xrun& getInstance() {
    static char buf[sizeof(xrun)];
    static xrun* theOneTrueObject = new (buf) xrun();
    return *theOneTrueObject;
  }

  void initialize();
  void finalize();

#ifdef DETECT_USAGE_AFTER_FREE
  void finalUAFCheck();
#endif
  // Simply commit specified memory block
  void atomicCommit(void* addr, size_t size) { _memory.atomicCommit(addr, size); }

  /* Transaction-related functions. */
  void saveContext() { _thread.saveContext(); }

  void rollback();
  void rollbackandstop();

  void epochBegin();
  void epochEnd(bool endOfProgram);

  bool isDoubleTake(void *pcaddr);

  // findStack is thread specific - either give the current thread ID,
  // or specificy who you want.
  regioninfo findStack(pid_t tid) { return _memory.findStack(tid); }
  // Print out the code information about an eip address.
  // Also try to print out the stack trace of given pcaddr.
  void printStackCurrent() { _memory.printStackCurrent(); }
  void printStack(const doubletake::Trace &trace) { _memory.printStack(trace); }

  bool addQuarantineList(void* ptr, size_t sz) { return _thread.addQuarantineList(ptr, sz); }

private:
  void syscallsInitialize();
  void stopAllThreads();

  // Handling the signal SIGUSR2
  static void sigusr2Handler(int signum, siginfo_t *siginfo, void *uctx);
  static void sigsegvHandler(int signum, siginfo_t *siginfo, void *uctx);
  static void rollbackFromSegv();

  void endOfEpochSignal(ucontext_t *uctx);
  void rollbackFromSegvSignal();

  /// @brief Install a handler for SIGUSR2 & SEGV signals.
  /// We are using the SIGUSR2 to stop all other threads, and SEGV to
  /// detect memory errors
  void installSignalHandlers();

  // Notify the system call handler about rollback phase
  void startRollback();

  /// The memory manager (for both heap and globals).
  xmemory& _memory;
  xthread& _thread;
  watchpoint& _watchpoint;

  leakcheck _leakcheck;
};

#endif
