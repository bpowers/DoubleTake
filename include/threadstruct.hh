#ifndef DOUBLETAKE_THREADSTRUCT_H
#define DOUBLETAKE_THREADSTRUCT_H

/*
 * @file   threadstruct.h
 * @brief  Definition of thread related structure.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 * @author Bobby Powers <http://www.cs.umass.edu/~bpowers>
 */

#include <pthread.h>
#include <signal.h>
#include <sys/types.h>

#include "list.hh"
#include "log.hh"
#include "quarantine.hh"
#include "recordentries.hh"
#include "semaphore.hh"
#include "xcontext.hh"
#include "xdefines.hh"


class xmemory;

typedef enum e_thrstatus {
  E_THREAD_STARTING = 0,
  E_THREAD_RUNNING,
  E_THREAD_JOINING, // The thread is trying to join other threads.
  E_THREAD_EXITING, // The thread is exiting.
                    //    E_THREAD_EXITED, // The thread is exiting.
  //  E_THREAD_SIGNALED, // The thread has been signaled, waiting for the instruction
  //  E_THREAD_CONTINUE, // The thread should move forward.
  E_THREAD_COND_WAITING, // Thre thread is waiting for a conditional variable
  E_THREAD_ROLLBACK,
  E_THREAD_WAITFOR_JOINING, // The thread has finished and wait for the joining.

  // Thread are not exiting to guarantee the reproducibility
  // It marks its status E_THREAD_WAITFOR_REAPING, one thread
  // entering the committing phase should reap all wa
  E_THREAD_WAITFOR_REAPING,
} thrStatus;

// System calls that will be recorded.
enum SyscallType {
  E_SYS_FILE_OPEN = 0,
  E_SYS_FILE_CLOSE,
  E_SYS_FILE_DUP,
  E_SYS_DIR_OPEN,
  E_SYS_DIR_CLOSE,
  E_SYS_MMAP,
  E_SYS_MUNMAP, //6
  E_SYS_TIME,
  E_SYS_GETTIMEOFDAY,
  E_SYS_TIMES,
  E_SYS_CLONE, // 10
  E_SYS_MAX,
};

struct SyscallEntry {
public:
  SyscallType syscall;
  char data[64 - sizeof(SyscallType)];
};

namespace DT {

  class Thread {
  public:
    stack_t altstack;
    bool available; // True: the thread index is free.
    bool internalheap;
    // Should we disable check or not?
    bool disablecheck;
    // bool      isSpawning; // Whether a new thread is spawning?
    bool isNewlySpawned; // whether this thread is spawned in this epoch?
    // Whether this thread has been joined or not.
    // If the thread has not been joined, then we can't reap this thread.
    // Otherwise, pthread_join may crash since the thread has exited/released.
    bool hasJoined;
    bool isSafe;   // whether a thread is safe to be interrupted
    int index;
    pid_t tid;      // Current process id of this thread.
    pthread_t self; // Results of pthread_self

    int origIndex;
    int creationEpoch;

    // What is the status of a thread.
    thrStatus status;

    // If the thread is waiting on a user-provided conditional variable,
    // we will record this conditional variable.
    pthread_cond_t * condwait;

    // if a thread is detached, then the current thread don't need to wait its parent
    bool isDetached;

    // Local output buffer for each thread. In order to avoid malloc requests in the
    // replaying.
    char outputBuf[LOG_SIZE];

    // What is the parent of this thread
    DT::Thread *parent;
    DT::Thread *joiner;

    // System calls happens on this thread.
    list_t syslist[E_SYS_MAX];
    RecordEntries<struct SyscallEntry> syscalls;

    // Synchronization events happens on this thread.
    RecordEntries<struct syncEvent> syncevents;

    quarantine qlist;

    // struct syncEventList syncevents;
    list_t pendingSyncevents;
    // struct syncEventList pendingSyncevents;

    // We used this to record the stack range
    void* stackBottom;
    void* stackTop;

    // the main thread is special because its stack came from the
    // kernel, not pthread_create
    bool isMain;

    semaphore sema;

    xcontext context;

    // The following is the parameter about starting function.
    threadFunction* startRoutine;
    void* startArg;
    void* result;

    // called in the parent thread
    void allocate(int index);
    // called in the child thread to initialize ourselves
    void initialize(bool isMain, xmemory *memory);

    // These used to be functions in internalsync, but are better as
    // members on the thread object.
    void lock();
    void unlock();
    void wait();
    void signal();
  private:
    // mutex when a thread is trying to change its state.
    // In fact, this mutex is only protect joiner.
    // Only in the beginning of a thread (register),
    // we need to care about the joiner
    pthread_mutex_t _mutex;
    pthread_cond_t _cond;
  };
}

// The following structure will be added to alivelist
struct aliveThread {
  list_t list;
  DT::Thread *thread;
};

// A pending synchronization event needed to be handled by corresponding
// thread.
struct pendingSyncEvent {
  list_t list;
  struct syncEvent* event;
};

// Each thread has corresponding status information in a global array.

// We will maintain an array about the status of each thread.
// Actually, there are two status that will be handled by us.
extern __thread DT::Thread *current;

#endif
