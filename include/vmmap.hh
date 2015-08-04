#if !defined(DOUBLETAKE_SELFMAP_H)
#define DOUBLETAKE_SELFMAP_H

/*
 * @file   selfmap.h
 * @brief  Process the /proc/self/map file.
 * @author Tongping Liu <http://www.cs.umass.edu/~tonyliu>
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 */

#include <map>
#include <string>

#include "runtime.hh"
#include "interval.hh"
#include "mm.hh"

// From heaplayers
#include "wrappers/stlallocator.h"

// A single mapping parsed from a /proc/*/maps file.  Once
// initialized, this object is constant.
class mapping {
public:
  // make sure to fully initialize ourselves
  mapping()
    : _file(""), _base(0), _limit(0), _offset(0), _valid(false),
      _readable(false), _writable(false), _executable(false),
      _copyOnWrite(false), _isStack(false), _isGlobals(false) {}

  // stack begins with "[stack" - that may end with "]" or for
  // multithreaded programs ":$TID]". global mappings are RW_P, and
  // either be marked as the heap, or the mapping is backed by a file
  // (and all files have absolute paths)
  mapping(uintptr_t base, uintptr_t limit, char* perms, size_t offset, std::string file)
    : _file(file), _base(base), _limit(limit), _offset(offset), _valid(true),
      _readable(perms[0] == 'r'), _writable(perms[1] == 'w'), _executable(perms[2] == 'x'),
      _copyOnWrite(perms[3] == 'p'), _isStack(file.compare(0, strlen("[stack"), "[stack") == 0),
      _isGlobals((_readable && _writable && !_executable && _copyOnWrite) &&
                 (_file.size() > 0 && (_file == "[heap]" || _file[0] == '/'))) {}

  bool valid() const { return _valid; }

  bool isText() const { return _readable && !_writable && !_executable; }
  bool isStack() const { return _isStack; }
  bool isGlobals() const { return _isGlobals; }

  uintptr_t getBase() const { return _base; }
  uintptr_t getLimit() const { return _limit; }

  const std::string& getFile() const { return _file; }

private:
  std::string _file;
  uintptr_t _base;
  uintptr_t _limit;
  size_t _offset;
  bool _valid;
  bool _readable;
  bool _writable;
  bool _executable;
  bool _copyOnWrite;
  bool _isStack;
  bool _isGlobals;
};

/// Information on the kernel's VMAs for a given process, including
/// routines for printing backtraces.  The no-arg constructor gets the
/// VMMap for the current process.
class VMMap {
public:
  VMMap();
  explicit VMMap(int pid);

  // FIXME: is it possible to do this stuff at object construction time?
  void initialize();

  bool isDoubleTake(void* pcaddr) const;
  bool isApplication(void* pcaddr) const;

  void getGlobalRegions(regioninfo *regions, int *count) const;

  regioninfo findStack(pid_t tid); // thread-specific
  // Print out the code information about an eip address.
  // Also try to print out the stack trace of given pcaddr.
  void printCallStack();
  void printCallStack(int depth, void** array);

  const std::string &exeName() const { return _exe; }

private:
  std::map<interval, mapping, std::less<interval>,
           HL::STLAllocator<std::pair<interval, mapping>, InternalHeapAllocator>> _mappings;

  std::string _exe;
  void* _appTextStart;
  void* _appTextEnd;
  void* _doubletakeStart;
  void* _doubletakeEnd;
  bool _doubletakeMapped;
  // PID of thread this instance was constructed on, or alternatively
  // the PID passed to the constructor.  Used for stack
  // identification.
  pid_t _pid;
};

#endif
