/*
* @file quarantine.cpp
* @brief Manage those quarantine objects.
         Those objects are freed in a FIFO order when
         the slots is not enough to hold objects or total size is too large.
         Whichever comes first, we will evict one object.
* @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
*/

#include "quarantine.hh"

#include "xrun.hh"

void quarantine::realfree(void* ptr) {
  // Calling actual heap object to free this object.
  xrun::getInstance().memory()->realfree(ptr);
}

void quarantine::rollback() {
  // Calling the rollback.
  xrun::getInstance().memory()->rollback();
}
