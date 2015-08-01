#ifndef DOUBLETAKE_DOUBLETAKE_HH
#define DOUBLETAKE_DOUBLETAKE_HH

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#include <atomic>

// this is an amalgamation of globalinfo.hh and xrun.hh - it is the
// global interface that the interposition & syscall functions use to
// start epochs, end epochs, and detect rollback.

namespace doubletake {
  extern std::atomic_bool initialized;
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

  /// global runtime lock
  void lock();
  void unlock();

  /// To end an epoch, a thread calls epochEnd.  epochEnd returns when
  /// all threads have entered epochEnd or epochEndInHandler.  This
  /// may take some time - the end-of-epoch signal is delivered to
  /// other threads when that thread returns from a syscall or is
  /// rescheduled.  Because of this, other threads may attempt to end
  /// an epoch themselves before receiving the end-of-epoch signal.
  void epochEnd();
  void epochEndInHandler();

  /// the next epoch is started when all threads have entered
  /// waitForNextEpoch.
  void waitForNextEpoch();
};

#endif // DOUBLETAKE_DOUBLETAKE_HH
