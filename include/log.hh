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

/**
 * Print status-information message: level 0
 */
#define _PR(n, fmt, ...)                                                                           \
  {                                                                                                \
    if(DT_LOG_LEVEL.load() < n) {                                                                  \
      doubletake::logf(__FILE__, __LINE__, n, fmt, ##__VA_ARGS__);                                 \
    }                                                                                              \
  }

#ifdef NDEBUG
#define PRINF(fmt, ...)
#define PRDBG(fmt, ...)
#define PRWRN(fmt, ...)
#else
#define PRINF(fmt, ...) _PR(1, fmt, ##__VA_ARGS__)
#define PRDBG(fmt, ...) _PR(2, fmt, ##__VA_ARGS__)
#define PRWRN(fmt, ...) _PR(3, fmt, ##__VA_ARGS__)
#endif /* NDEBUG */

#define PRERR(fmt, ...) _PR(4, fmt, ##__VA_ARGS__)
#define FATAL(fmt, ...)                                                                            \
  {                                                                                                \
    doubletake::fatalf(__FILE__, __LINE__, fmt, ##__VA_ARGS__);                                    \
  }

#define PRINT(fmt, ...)                                                                            \
  {                                                                                                \
    doubletake::printf(fmt, ##__VA_ARGS__);                                                        \
  }

// Check a condition. If false, print an error message and abort
#define REQUIRE(cond, ...)                                                                         \
  if(!(cond)) {                                                                                    \
    FATAL(__VA_ARGS__)                                                                             \
  }

#endif
