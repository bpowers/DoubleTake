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

using doubletake::initialized;
using namespace std;


__attribute__((constructor)) void doubletake_init() {
  doubletake::__initialize();
}

__attribute__((destructor)) void finalizer() {
  xrun::getInstance().finalize();
}

typedef int (*main_fn_t)(int, char**, char**);
static main_fn_t real_main;

// Doubletake's main function
int doubletake_main(int argc, char** argv, char** envp) {
  int rc;

	xrun::getInstance().epochBegin();

  rc = real_main(argc, argv, envp);

  // FIXME: this should eventually work, but doesn't yet.
  // explicitly end the epoch here rather than as a result of static
  // destructors, as long as we're not in a rollback currently
  //if(!doubletake::isRollback) {
  //  xrun::getInstance().epochEnd(true);
  //  xrun::getInstance().epochBegin();
  //}

  return rc;
}

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("doubletake_libc_start_main")));

extern "C" int doubletake_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real (or next) __libc_start_main
  auto real_libc_start_main = (decltype(__libc_start_main)*)dlsym(RTLD_NEXT, "__libc_start_main");
  // Save the program's real main function
  real_main = main_fn;
  // Run the real __libc_start_main, but pass in doubletake's main function
  return real_libc_start_main(doubletake_main, argc, argv, init, fini, rtld_fini, stack_end);
}

void* call_dlsym(void* handle, const char* funcname) {
  bool isCheckDisabled = false;
  if(initialized) {
    isCheckDisabled = xthread::isCheckDisabled();

    if(!isCheckDisabled) {
      xthread::disableCheck();
    }
  }

  void* p = dlsym(handle, funcname);

  if(initialized) {
    if(!isCheckDisabled) {
      xthread::enableCheck();
    }
  }
  return p;
}
