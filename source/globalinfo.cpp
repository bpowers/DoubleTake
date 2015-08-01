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
#include "syscalls.hh"
#include "xmemory.hh"
#include "xrun.hh"
#include "xthread.hh"

size_t __max_stack_size;

bool funcInitialized = false;
bool initialized = false;

// Some global information.
std::atomic_bool g_isRollback;
std::atomic_bool g_hasRollbacked;
std::atomic_int g_numOfEnds;
std::atomic<enum SystemPhase> g_phase;

pthread_cond_t g_condCommitter;
pthread_cond_t g_condWaiters;
pthread_mutex_t g_mutex;
pthread_mutex_t g_mutexSignalhandler;

std::atomic_int g_waiters;
std::atomic_int g_waitersTotal;

bool addThreadQuarantineList(void* ptr, size_t sz) {
  return xthread::getInstance().addQuarantineList(ptr, sz);
}

void doubletake::__initialize() {

}
