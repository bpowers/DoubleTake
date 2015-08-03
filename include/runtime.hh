#ifndef DOUBLETAKE_DOUBLETAKE_HH
#define DOUBLETAKE_DOUBLETAKE_HH

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#include <atomic>

#include <sys/types.h> // pid_t

struct regioninfo {
  void* start;
  void* end;
};

// this is an amalgamation of globalinfo.hh and xrun.hh - it is the
// global interface that the interposition & syscall functions use to
// start epochs, end epochs, and detect rollback.

namespace doubletake {
  extern std::atomic_bool initialized;
  // some of the runtime + interposition functions are needed for
  // initialization of the rest of the system - they need the Real::*
  // trampolines to be populated with symbols looked up by the dynamic
  // linker.
  extern std::atomic_bool trampsInitialized;

  extern std::atomic_bool isRollback;
  extern std::atomic_bool hasRollbacked;

  enum SystemPhase {
    E_SYS_INIT,        // Initialization phase
    E_SYS_EPOCH_END,   // We are just before commit.
    E_SYS_EPOCH_BEGIN, // We have to start a new epoch when no overflow.
  };
  extern std::atomic<enum SystemPhase> phase;

  inline bool isInitPhase() { return phase == E_SYS_INIT; }
  inline bool isEpochEnd() { return phase == E_SYS_EPOCH_END; }
  inline bool isEpochBegin() { return phase == E_SYS_EPOCH_BEGIN; }

  /// initialize the doubletake runtime system.  Used internally by
  /// some of the interposition functions to get around static
  /// initializer ordering.
  void __initialize();
  /// initialize just the Real library trampolines
  void __trampsInitialize();

  int getThreadIndex();
  char *getCurrentThreadBuffer(); // for logging

  struct Trace {
    Trace(size_t len, void **frames) : len(len), frames(frames) {}
    size_t len;
    void **frames;
  };

  /// returns the bounds for the specified thread's VMA
  regioninfo findStack(pid_t tid);

  void printStackCurrent();
  void printStack(const Trace &trace);

  bool isLib(void *pcaddr);

  /// global runtime lock
  void lock();
  void unlock();

  /// To end an epoch, a thread calls epochEnd.  More than 1 thread
  /// may enter epochEnd - the end-of-epoch signal is delivered to
  /// other threads when that thread returns from a syscall or is
  /// rescheduled.  Because of this, other threads may attempt to end
  /// an epoch themselves before receiving the end-of-epoch signal.
  void epochEnd();

  /// An epoch has ended when all threads have called quiesce - it is
  /// called from inside epochEnd, or quiesceInHandler is called from
  /// the signal handler.
  void quiesce();
  void quiesceInHandler();

  /// the next epoch is started when all threads have entered
  /// waitForNextEpoch.
  void waitForNextEpoch();

  bool quarantine(void *ptr, size_t size);
};

#endif // DOUBLETAKE_DOUBLETAKE_HH
