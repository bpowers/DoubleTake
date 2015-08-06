#if !defined(DOUBLETAKE_LOG_H)
#define DOUBLETAKE_LOG_H

/*
 * @file:   log.h
 * @brief:  Logging and debug printing macros
 *          Color codes from SNAPPLE: http://sourceforge.net/projects/snapple/
 * @author: Charlie Curtsinger & Tongping Liu
 */

#include <atomic>

#include "xdefines.hh"

#define LOG_SIZE 4096

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

extern std::atomic_int DT_LOG_LEVEL;

namespace doubletake {
  void logf(const char *file, int line, int level, const char *fmt, ...) __printf_like(4, 5);
  void fatalf(const char *file, int line, const char *fmt, ...) __printf_like(3, 4) __noreturn;
  void printf(const char *fmt, ...) __printf_like(1, 2);
}

#define __PRV(n, fmt, ...) doubletake::logf(__FILE__, __LINE__, n, fmt, __VA_ARGS__)
#define __PR0(n, msg)      doubletake::logf(__FILE__, __LINE__, n, msg)
#define __FATALV(fmt, ...) doubletake::fatalf(__FILE__, __LINE__, fmt, __VA_ARGS__)
#define __FATAL0(msg)      doubletake::fatalf(__FILE__, __LINE__, msg)
#define __PRINTV(fmt, ...) doubletake::printf(fmt, __VA_ARGS__)
#define __PRINT0(msg)      doubletake::printf(msg)

#define __PRPREFIX_X(a,b,c,d,e,f,g,h,n,...) n
#define __PRPREFIX(...) __PRPREFIX_X(__VA_ARGS__,V,V,V,V,V,V,V,0,)

#define __PRCONCAT_X(a,b) a##b
#define __PRCONCAT(a,b) __PRCONCAT_X(a,b)
#define __VAFN(fn, ...) __PRCONCAT(fn,__PRPREFIX(__VA_ARGS__))

/**
 * Print status-information message: level 0
 */
#define _PR(n, ...)                                                                           \
  {                                                                                                \
    if(DT_LOG_LEVEL.load() < n)                                                                    \
      __VAFN(__PR, __VA_ARGS__)(n, __VA_ARGS__);                                              \
  }

#ifdef NDEBUG
#define PRINF(...)
#define PRDBG(...)
#define PRWRN(...)
#else
#define PRINF(...) _PR(1, __VA_ARGS__)
#define PRDBG(...) _PR(2, __VA_ARGS__)
#define PRWRN(...) _PR(3, __VA_ARGS__)
#endif /* NDEBUG */

#define PRERR(...) _PR(4, __VA_ARGS__)

#define FATAL(...) __VAFN(__FATAL, __VA_ARGS__)(__VA_ARGS__)
#define PRINT(...) __VAFN(__PRINT, __VA_ARGS__)(__VA_ARGS__)

// Check a condition. If false, print an error message and abort
#define REQUIRE(cond, ...)                                                                         \
  if(!(cond))                                                                                      \
    FATAL(__VA_ARGS__)

#endif
