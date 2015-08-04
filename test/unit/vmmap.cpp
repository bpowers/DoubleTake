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

  // FIXME: we link the unit tests against the doubletake objects
  // without the library initialization + interposition code, so that
  // we can unit test different pieces in a more isolated function.
  // But this has the consequence that isDoubleTake always returns
  // false, as we no longer have a separate mapping for libdoubletake.
  
  ASSERT_TRUE(vmmap.isDoubleTake(doubletake::__initialize));
  ASSERT_FALSE(vmmap.isDoubleTake((void *)&nonDTfunc));
}
