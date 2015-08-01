#if !defined(DOUBLETAKE_XCONTEXT_H)
#define DOUBLETAKE_XCONTEXT_H

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "log.hh"
#include "real.hh"
#include "xdefines.hh"

/**
 * @class xcontext
 * @brief User context to support the rollback mechanism.
 *
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

class xcontext {
public:
  xcontext() {}

  void setupBackup(void* ptr) { _backup = ptr; }

  // void initialize(void * privateStart, void * privateTop, size_t totalPrivateSize, size_t
  // backupSize)
  void setupStackInfo(void* privateTop, size_t stackSize) {
    _privateTop = privateTop;
    _stackSize = stackSize;
  }

  // Now we need to save the context
  void saveContext();

  // We already have context. How we can save this context.
  inline void saveSpecifiedContext(ucontext_t* context) {
    // Find out the esp pointer.
    size_t size;

    // Save the stack at first.
    _privateStart = (void*)getStackPointer(context);
    size = size_t((intptr_t)_privateTop - (intptr_t)_privateStart);
    _backupSize = size;

    if(size >= _stackSize) {
      PRWRN("Wrong. Current stack size (%lx = %p - %p) is larger than total size (%lx)\n", size,
            _privateTop, _privateStart, _stackSize);
      abort();
    }

    memcpy(_backup, _privateStart, size);

    // We are trying to save context at first
    memcpy(&_context, context, sizeof(ucontext_t));
  }

  /* Finish the following tasks here:
    a. Change current stack to the stack of newContext. We have to utilize
       a temporary stack to host current stack.
    b. Copy the stack from newContext to current stack.
    c. Switch back from the temporary stack to current stack.
    d. Copy the stack and context from newContext to oldContext.
    f. setcontext to the context of newContext.
   */
  inline static void resetContexts(xcontext* oldContext, xcontext* newContext) {
    // We can only do this when these two contexts are for the same thread.
    assert(oldContext->getPrivateTop() == newContext->getPrivateTop());

    // We will backup the stack and context from newContext at first.
    oldContext->backupStackAndContext(newContext);

    restoreContext(oldContext, newContext);
  }

  // Copy the stack from newContext to oldContext.
  void backupStackAndContext(xcontext* context) {
    // We first backup the context.
    _privateStart = context->getPrivateStart();
    _backupSize = context->getBackupSize();

    memcpy(_backup, context->getBackupStart(), _backupSize);

    // Now we will backup the context.
    memcpy(&_context, context->getContext(), sizeof(ucontext_t));
  }

  static void rollbackInsideSignalHandler(ucontext_t* context, xcontext* oldContext) {
    // We first rollback the stack.
		// Since the thread is inside the context of signal handler, we can simply 
		// recover the stack by copying, no need to worry about the correctness
    memcpy(oldContext->getPrivateStart(), oldContext->getBackupStart(),
           oldContext->getBackupSize());

    memcpy(context, oldContext->getContext(), sizeof(ucontext_t));
  }

  // Restore context from specified context.
  /* Finish the following tasks here:
    a. Change current stack to the stack of newContext. We have to utilize
       a temporary stack to host current stack.
    b. Copy the stack from current context to current stack.
    c. setcontext to the context of newContext.
   */
  static void restoreContext(xcontext* oldContext, xcontext* newContext);

  void* getStackTop() { return _privateTop; }

private:
  ucontext_t* getContext() { return &_context; }

  void* getPrivateStart() { return _privateStart; }

  void* getPrivateTop() { return _privateTop; }

  size_t getBackupSize() { return _backupSize; }

  size_t getStackSize() { return _stackSize; }

  void* getBackupStart() { return _backup; }
  // How to get ESP/RSP from the specified context.
  unsigned long getStackPointer(ucontext* context) {
#ifndef X86_32BITS
    return context->uc_mcontext.gregs[REG_RSP];
#else
    return context->uc_mcontext.gregs[REG_ESP];
#endif
  }

  /// The saved registers, etc.
  ucontext_t _context;
  void* _backup; // Where to _backup those thread private information.
  void* _privateStart;
  void* _privateTop;
  size_t _stackSize;
  size_t _backupSize;
};

#endif
