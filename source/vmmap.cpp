#include <execinfo.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <functional>
#include <map>
#include <new>
#include <utility>

#include "vmmap.hh"
#include "log.hh"

// The current upper limit on PID numbers is 4 million (threads.h),
// but there is a comment that the max supported by the futex
// implementation is 2**29 (536870912) - a number with a length of 9
#define PROC_DIR_MAX_LEN 9
#define PROC_FILE_PATH_LEN (22 + PROC_DIR_MAX_LEN + 1)

#define MAX_BUF_SIZE 4096

/// Read a mapping from a file input stream
static std::ifstream& operator>>(std::ifstream& f, mapping& m) {
  if(f.good() && !f.eof()) {
    uintptr_t base, limit;
    char perms[5];
    size_t offset;
    size_t dev_major, dev_minor;
    int inode;
    string path;

    // Skip over whitespace
    f >> std::skipws;

    // Read in "<base>-<limit> <perms> <offset> <dev_major>:<dev_minor> <inode>"
    f >> std::hex >> base;
    if(f.get() != '-')
      return f;
    f >> std::hex >> limit;

    if(f.get() != ' ')
      return f;
    f.get(perms, 5);

    f >> std::hex >> offset;
    f >> std::hex >> dev_major;
    if(f.get() != ':')
      return f;
    f >> std::hex >> dev_minor;
    f >> std::dec >> inode;

    // Skip over spaces and tabs
    while(f.peek() == ' ' || f.peek() == '\t') {
      f.ignore(1);
    }

    // Read out the mapped file's path
    getline(f, path);

    m = mapping(base, limit, perms, offset, path);
  }

  return f;
}


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

VMMap::VMMap()
  : _mappings(), _exe(""), _appTextStart(nullptr), _appTextEnd(nullptr),
    _doubletakeStart(nullptr), _doubletakeEnd(nullptr),
    _doubletakeMapped(false), _pid(getpid()) {}
VMMap::VMMap(int pid)
  : _mappings(), _exe(""), _appTextStart(nullptr), _appTextEnd(nullptr),
    _doubletakeStart(nullptr), _doubletakeEnd(nullptr),
    _doubletakeMapped(false), _pid(pid) {}

bool VMMap::isDoubleTake(void *pcaddr) const {
  return _doubletakeMapped && ((pcaddr >= _doubletakeStart) && (pcaddr <= _doubletakeEnd));
}

bool VMMap::isApplication(void* pcaddr) const {
  return ((pcaddr >= _appTextStart) && (pcaddr <= _appTextEnd));
}

static bool isDoubleTake(const mapping &m) {
  return m.getFile().find("libdoubletake") != std::string::npos;
}

void VMMap::initialize() {
  char procDir[PROC_FILE_PATH_LEN];
  char procExe[PROC_FILE_PATH_LEN];
  char procMaps[PROC_FILE_PATH_LEN];
  char path[PATH_MAX];

  ::snprintf(procDir, PROC_FILE_PATH_LEN, "/proc/%d", _pid);
  ::snprintf(procExe, PROC_FILE_PATH_LEN, "%s/exe", procDir);
  ::snprintf(procMaps, PROC_FILE_PATH_LEN, "%s/maps", procDir);

  // readlink's result isn't necessarily null-terminated
  ssize_t len = Real::readlink(procExe, path, PATH_MAX-1);
  _exe = std::string(path, len); // XXX: allocates from tempmalloc?

  // Build the mappings data structure
  ifstream maps_file(procMaps);

  while(maps_file.good() && !maps_file.eof()) {
    mapping m;
    maps_file >> m;
    // FIXME: we get an invalid entry when parsing the [vsyscall]
    // entry on 64-bit systems, I think because the addresses are
    // quite large: ffffffffff600000.  Luckily, the vsyscall page is
    // the last one in /maps. We should fix that, and not just bail
    // out here.
    if(!m.valid()) {
      break;
    }
    _mappings[interval(m.getBase(), m.getLimit())] = m;

    // record information on the main executable and libdoubletake
    if(m.isText()) {
      if(::isDoubleTake(m)) {
        _doubletakeStart = (void*)m.getBase();
        _doubletakeEnd = (void*)m.getLimit();
        _doubletakeMapped = true;
      } else if(m.getFile() == _exe) {
        _appTextStart = (void*)m.getBase();
        _appTextEnd = (void*)m.getLimit();
      }
    }
  }
}

/// Collect all global regions.
void VMMap::getGlobalRegions(regioninfo *regions, int *count) const {

  size_t index = 0;
  for(const auto& entry : _mappings) {
    const mapping& m = entry.second;
    if(m.isGlobals() && !::isDoubleTake(m)) {
      regions[index].start = (void*)m.getBase();
      regions[index].end = (void*)m.getLimit();
      index++;
    }
  }

  *count = index;
}

regioninfo VMMap::findStack(pid_t tid) {
  regioninfo ri;

  memset(&ri, 0, sizeof(ri));

  // FIXME: check if a given stack is for the given tid
  for(const auto& entry : _mappings) {
    const mapping& m = entry.second;
    if(m.isStack()) {
      ri.start = (void *)m.getBase();
      ri.end = (void *)m.getLimit();
      return ri;
    }
  }
  FATAL("Couldn't find stack mapping. Giving up.\n");
  return ri;
}
