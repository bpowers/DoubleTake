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
