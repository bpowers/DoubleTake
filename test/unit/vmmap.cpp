#include <stdint.h>
#include <stdlib.h>

#include "gtest.h"

#include "vmmap.hh"
#include "internalheap.hh"

// used when testing vmmap.isDoubleTake
void nonDTfunc() {}

TEST(VMMapTest, CheckMaps) {
  doubletake::__trampsInitialize();
  InternalHeap::getInstance().initialize();

  VMMap vmmap;

  vmmap.initialize();

  // FIXME: doubletake function pointers point to an entry in a PLT,
  // isDoubleTake test whether a piece of memory is in the text
  // section of the libdoubletake library.  Not sure a good way to
  // test that.
  ASSERT_FALSE(vmmap.isDoubleTake((void *)&nonDTfunc));
}
