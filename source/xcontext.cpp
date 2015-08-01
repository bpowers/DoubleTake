/**
 * @class xcontext
 * @brief User context to support the rollback mechanism.
 *
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include "xcontext.hh"

void xcontext::saveContext() {
  size_t size;
  // PRINF("SAVECONTEXT: Current %p _privateTop %p at %p _backup %p\n", getpid(), _privateTop,
  // &_privateTop, _backup);
  //    PRINF("saveContext nownow!!!!!!\n");
  // Save the stack at first.
  _privateStart = &size;
  size = size_t((intptr_t)_privateTop - (intptr_t)_privateStart);
  _backupSize = size;

  if(size >= _stackSize) {
    PRWRN("Wrong. Current stack size (%lx = %p - %p) is larger than total size (%lx)\n", size,
          _privateTop, _privateStart, _stackSize);
    Real::exit(-1);
  }
  memcpy(_backup, _privateStart, size);
  getcontext(&_context);
}

void xcontext::restoreContext(xcontext* oldContext, xcontext* newContext) {
  // We can only do this when these two contexts are for the same thread.
  assert(oldContext->getPrivateTop() == newContext->getPrivateTop());

  // Now we can mess with newContext.
  unsigned long ebp, esp;

  // The offset to the stack bottom.
  unsigned long espoffset, ebpoffset;
  unsigned long stackTop, newStackTop;
  unsigned long newebp, newesp;
// Get current esp and ebp
#if defined(X86_32BIT)
  asm volatile("movl %%ebp,%0\n"
               "movl %%esp,%1\n"
               : "=r"(ebp), "=r"(esp));
#else
  asm volatile("movq %%rbp,%0\n"
               "movq %%rsp, %1\n"
               : "=r"(ebp), "=r"(esp));
#endif

  // Calculate the offset to stack bottom for ebp and esp register.
  // Since we know that we are still using the original stack.
  stackTop = (unsigned long)newContext->getPrivateTop();
  espoffset = stackTop - esp;
  ebpoffset = stackTop - ebp;

  REQUIRE(espoffset <= xdefines::TEMP_STACK_SIZE, "Temporary stack exhausted");

  // Calculate the new ebp and esp registers.
  // We will set ebp to the bottom of temporary stack.
  newStackTop = (intptr_t)newContext->getBackupStart() + newContext->getStackSize();
  newebp = newStackTop - ebpoffset;
  newesp = newStackTop - espoffset;

  // Copy the existing stack to the temporary stack.
  // Otherwise, we can not locate those global variables???
  memcpy((void*)newesp, (void*)esp, espoffset);

// Temporarily switch the stack manually.  It is important to switch
// in this place (not using a function call), otherwise, the lowest
// level of frame will be poped out and the stack will return back to
// the original one Then techniquely we cann't switch successfully.
// What we want is that new frames are using the new stack, but we
// will recover the stack in the same function later to void
// problems!!!
#if defined(X86_32BIT)
  asm volatile(
    // Set ebp and esp to new pointer
    "movl %0, %%ebp\n"
    "movl %1, %%esp\n"
    :
    : "r"(newebp), "r"(newesp));
#else
  asm volatile(
    // Set ebp and esp to new pointer
    "movq %0,%%rbp\n"
    "movq %1,%%rsp\n"
    :
    : "r"(newebp), "r"(newesp));
#endif

  // At this

  // PRINF("________RESTORECONTEX___________at line %d\n", __LINE__);
  // Now we will recover the stack from the saved oldContext.
  memcpy(oldContext->getPrivateStart(), oldContext->getBackupStart(),
         oldContext->getBackupSize());

  // XXX: we can't log anything here - our stack is invalid
  //PRINF("Thread %p is calling actual setcontext", (void*)pthread_self());

  // After recovery of the stack, we can call setcontext to switch to original stack.
  setcontext(oldContext->getContext());

  PRINF("WE ARE TOTALLY FUCKED");
}
