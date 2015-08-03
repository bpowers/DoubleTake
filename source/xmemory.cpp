/*
 * @file   xmemory.cpp
 * @brief  Memory management for all.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include <execinfo.h>

#include "xmemory.hh"

#include "internalheap.hh"
#include "mm.hh"
#include "xoneheap.hh"
#include "xpheap.hh"
#include "xrun.hh"

// max lookback for printStackCurrent
#define MAX_FRAMES 256

// Normally, callstack only saves next instruction address.
// To get current callstack, we should substract 1 here.
// Then addr2line can figure out which instruction correctly
#define PREV_INSTRUCTION_OFFSET 1

xpheap<xoneheap<xheap>> xmemory::_pheap;

void xmemory::initialize() {
  _selfmap.initialize();

  // Call _pheap so that xheap.h can be initialized at first and then can work normally.
  _heapBegin =
    (intptr_t)_pheap.initialize((void*)xdefines::USER_HEAP_BASE, xdefines::USER_HEAP_SIZE);
  _heapEnd = _heapBegin + xdefines::USER_HEAP_SIZE;

  _globals.initialize(_selfmap);
}

void xmemory::finalize() {
  _globals.finalize();
  _pheap.finalize();
}

bool xmemory::isDoubleTake(void *pcaddr) {
  return _selfmap.isDoubleTake(pcaddr);
}

// Print out the code information about an eipaddress
// Also try to print out stack trace of given pcaddr.
void xmemory::printStackCurrent() {
  void* array[MAX_FRAMES];
  doubletake::Trace trace(MAX_FRAMES, array);

  // get void*'s for all entries on the stack
  xthread::disableCheck();

  trace.len = backtrace(array, MAX_FRAMES);
  printStack(trace);

  xthread::enableCheck();
}

// Calling system involves a lot of irrevocable system calls.
void xmemory::printStack(const doubletake::Trace &trace) {
#if 0
  char** syms = backtrace_symbols(array, frames);

  for(int i=0; i<frames; i++) {
    fprintf(stderr, "  %d: %s\n", i, syms[i]);
  }
#endif

#if 1
  char buf[256];
  //  fprintf(stderr, "printCallStack(%d, %p)\n", depth, array);
  for(size_t i=0; i < trace.len; i++) {
    void* addr = (void*)((unsigned long)trace.frames[i] - PREV_INSTRUCTION_OFFSET);

    //PRINT("\tcallstack frame %d: %p\t", i, addr);
    // Print out the corresponding source code information
    sprintf(buf, "addr2line -a -i -e %s %p", _selfmap.exeName().c_str(), addr);
    system(buf);
  }
#endif

#if 0
  // We print out the first one who do not belong to library itself
  //else if(index == 1 && !isDoubleTakeLibrary((void *)addr)) {
  else if(!isDoubleTake(addr)) {
    index++;
    PRINT("\tcallstack frame %d: %p\n", index, addr);
  }
#endif
}

void xmemory::realfree(void* ptr) { _pheap.realfree(ptr); }

void* InternalHeapAllocator::malloc(size_t sz) { return InternalHeap::getInstance().malloc(sz); }

void InternalHeapAllocator::free(void* ptr) { return InternalHeap::getInstance().free(ptr); }

void* InternalHeapAllocator::allocate(size_t sz) { return InternalHeap::getInstance().malloc(sz); }

void InternalHeapAllocator::deallocate(void* ptr) { return InternalHeap::getInstance().free(ptr); }
