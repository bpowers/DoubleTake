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

using namespace std;

__attribute__((constructor)) void initRealFunctions() {
//	printf("calling min_init\n");
  Real::initializer();
  funcInitialized = true;
  if(!initialized) {
		// Now setup 
    xrun::getInstance().initialize();
    initialized = true;
  }
}

void initializer() {
  // Using globals to provide allocation
  // before initialized.
  // We can not use stack variable here since different process
  // may use this to share information.
  // initialize those library function entries.
//	fprintf(stderr, "initializer function now\n");
}

__attribute__((destructor)) void finalizer() {
  xrun::getInstance().finalize();

  funcInitialized = false;
}

typedef int (*main_fn_t)(int, char**, char**);
main_fn_t real_main;

void exitfunc(void) {
	PRINT("in the end of exiting now\n");
}

// Doubletake's main function
int doubletake_main(int argc, char** argv, char** envp) {
  /******** Do doubletake initialization here (runs after ctors) *********/
//	printf("doubletake_main initializer\n");
	initializer();

	// Now start the first epoch
	xrun::getInstance().epochBegin();

  // Call the program's main function
  return real_main(argc, argv, envp);
}

extern "C" int __libc_start_main(main_fn_t, int, char**, void (*)(), void (*)(), void (*)(), void*) __attribute__((weak, alias("doubletake_libc_start_main")));

extern "C" int doubletake_libc_start_main(main_fn_t main_fn, int argc, char** argv, void (*init)(), void (*fini)(), void (*rtld_fini)(), void* stack_end) {
  // Find the real __libc_start_main
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
