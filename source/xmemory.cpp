/*
 * @file   xmemory.cpp
 * @brief  Memory management for all.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 */

#include "xmemory.hh"

#include "internalheap.hh"
#include "mm.hh"
#include "xoneheap.hh"
#include "xpheap.hh"
#include "xrun.hh"

xpheap<xoneheap<xheap>> xmemory::_pheap;

void xmemory::realfree(void* ptr) { _pheap.realfree(ptr); }

void* InternalHeapAllocator::malloc(size_t sz) { return InternalHeap::getInstance().malloc(sz); }

void InternalHeapAllocator::free(void* ptr) { return InternalHeap::getInstance().free(ptr); }

void* InternalHeapAllocator::allocate(size_t sz) { return InternalHeap::getInstance().malloc(sz); }

void InternalHeapAllocator::deallocate(void* ptr) { return InternalHeap::getInstance().free(ptr); }
