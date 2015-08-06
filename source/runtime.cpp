/**
 * @file libdoubletake.cpp
 * @brief Interface with outside library.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 * @author Charlie Curtsinger <http://www.cs.umass.edu/~charlie>
 */

#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "globalinfo.hh"
#include "real.hh"
#include "xmemory.hh"
#include "xrun.hh"
#include "xthread.hh"

size_t __max_stack_size;

namespace doubletake {
  std::atomic_bool initialized(false);
  std::atomic_bool trampsInitialized(false);

  std::atomic_bool isRollback(false);
  std::atomic_bool hasRollbacked(false);

  std::atomic<enum SystemPhase> phase;
};

// the global runtime lock
static pthread_mutex_t rtLock = PTHREAD_MUTEX_INITIALIZER;

void doubletake::lock() {
  sigset_t blocked, current;
  struct timespec sleep;
  sleep.tv_sec = 0;
  sleep.tv_nsec = 10000000; // 1/100 of a second

  sigemptyset(&blocked);
  sigaddset(&blocked, SIGUSR2);

  // FIXME: we need to make sure we're not interrupted in our call to
  // pthread_mutex_(try)lock, because if we are and rollback it could
  // corrupt the state of the mutex, causing deadlocks.  This could
  // probably be done better.  Maybe we could use SIGUSR1?
  while (true) {
    Real::sigprocmask(SIG_BLOCK, &blocked, &current);

    // a return value of 0 means we acquired the lock, great!
    if (!Real::pthread_mutex_trylock(&rtLock))
      break;

    Real::sigprocmask(SIG_SETMASK, &current, nullptr);
    Real::nanosleep(&sleep, nullptr);
  }
}

void doubletake::unlock() {
  sigset_t blocked;

  Real::pthread_mutex_unlock(&rtLock);

  sigemptyset(&blocked);
  sigaddset(&blocked, SIGUSR2);

  Real::sigprocmask(SIG_UNBLOCK, &blocked, nullptr);
}

// Some global information.
std::atomic_int g_numOfEnds;

pthread_cond_t g_condCommitter;
pthread_cond_t g_condWaiters;
pthread_mutex_t g_mutex;
pthread_mutex_t g_mutexSignalhandler;

std::atomic_int g_waiters;
std::atomic_int g_waitersTotal;

void doubletake::__initialize() {
  doubletake::__trampsInitialize();
  xrun::getInstance().initialize();
}

void doubletake::__trampsInitialize() {
  if (!trampsInitialized) {
    Real::initializer();
    doubletake::trampsInitialized = true;
  }
}

bool doubletake::quarantine(void *ptr, size_t size) {
  return xrun::getInstance().addQuarantineList(ptr, size);
}

bool doubletake::isLib(void *pcaddr) {
  return xrun::getInstance().isDoubleTake(pcaddr);
}

regioninfo doubletake::findStack(pid_t tid) {
  return xrun::getInstance().findStack(tid);
}

void doubletake::printStackCurrent() {
  return xrun::getInstance().printStackCurrent();
}

void doubletake::printStack(const Trace &trace) {
  return xrun::getInstance().printStack(trace);
}

int doubletake::getThreadIndex() {
  return xrun::getInstance().getThreadIndex();
}

char *doubletake::getCurrentThreadBuffer() {
  return xrun::getInstance().getCurrentThreadBuffer();
}
