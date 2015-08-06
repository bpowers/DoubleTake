# default optimization level
O=0

SUBDIRS = test test/unit
SUBDIR_BUILDFILES = $(addsuffix /build.mk,$(SUBDIRS))

ARCH ?= amd64

# prefer clang
CC     = clang
CXX    = clang++
AR     = ar
RANLIB = ranlib

WARNFLAGS := \
        -Werror \
        -Wall -Wextra -Wpedantic \
        -Wundef \
        -Wno-unused-parameter \
        -Wno-format-pedantic \
        -Wno-nested-anon-types

FEATURE_FLAGS := \
        -DDEBUG_LEVEL=3 \
        -DDETECT_OVERFLOW \
        -DDETECT_USAGE_AFTER_FREE \
#        -DDETECT_MEMORY_LEAKS \

# want stdatomic.h in C, which is a c11 feature
CVER     := c11
CXXVER   := c++11

CFLAGS   := -g -fPIC -pedantic -fno-omit-frame-pointer -Iinclude -Iheaplayers $(WARNFLAGS) $(FEATURE_FLAGS) -D_DEFAULT_SOURCE -D_BSD_SOURCE
CXXFLAGS := -std=$(CXXVER) $(CFLAGS)
ASFLAGS  := $(WARNFLAGS)
# this is for generic linker flags - target specific $LIB dependencies
# are added later.
LDFLAGS  :=

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS += -s
endif

LIB_SRCS := $(wildcard source/*.cpp) $(wildcard source/*.c) $(wildcard source/*_$(ARCH).s)
LIB_OBJS := $(patsubst %_$(ARCH).s,%.o,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(LIB_SRCS))))
# these object files contain the global initialization and
# interposition functions needed for libdoubletake - but we don't want
# to include them when building unit tests
LIB_GLOBAL_OBJS   := source/libdoubletake.o source/interpose.o
LIB_UNITTEST_OBJS := $(filter-out $(LIB_GLOBAL_OBJS),$(LIB_OBJS))

LIB      := libdoubletake.so
LIB_DEPS := dl pthread

TESTLIB  := libdttest_s.a

TARGETS   = $(LIB)
# make sure we recompile when the Makefile (and associated
# CFLAGS/LDFLAGS change)
CONFIG   := Makefile

# test-bin is a bit of a hack so that we can declare the all target
# first (this making it the default target) before we include the
# build.mk files in subdirs, which add an arbitrary number fof entries
# to TEST_TARGETS
all: $(TARGETS) test-bin

include $(SUBDIR_BUILDFILES)

test-bin: $(TEST_BIN_TARGETS)

test: $(TESTS)

# clear out all suffixes
.SUFFIXES:
# list only those we use
.SUFFIXES: .d .c .cpp .s .o .test

%.o: %_$(ARCH).s $(CONFIG)
	@echo "  AS    $@"
	$(CC) -O$(O) $(ASFLAGS) -o $@ -c $<

%.o: %.c $(CONFIG)
	@echo "  CC    $@"
	$(CC) -O$(O) -std=$(CVER) $(CFLAGS) -MMD -o $@ -c $<

%.o: %.cpp $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) -O$(O) $(CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) $(addprefix -l,$(LIB_DEPS)) -o $@ $(LIB_OBJS)

$(TESTLIB): $(LIB_UNITTEST_OBJS) $(CONFIG)
	@echo "  AR    $@"
	$(AR) rcD $@ $(LIB_UNITTEST_OBJS)
	$(RANLIB) $@

# emacs-compatible tagfile generation - for navigating around in emacs
TAGS: include/*.hh source/*.cpp
	etags -a include/*.hh source/*.cpp

format:
	clang-format -i include/*.hh source/*.cpp

clean:
	find . -name '*.o' -print0 | xargs -0 rm -f
	find . -name '*.gcno' -print0 | xargs -0 rm -f
	find . -name '*.gcda' -print0 | xargs -0 rm -f
	find . -name '*.gcov' -print0 | xargs -0 rm -f
	rm -f $(TARGETS) $(TEST_BIN_TARGETS) $(TESTLIB)

distclean: clean
	find . -name '*.d' -print0 | xargs -0 rm -f
	find . -name '*~' -print0 | xargs -0 rm -f

-include $(LIB_OBJS:.o=.d)

.PHONY: all test-bin format clean distclean $(PHONY_TARGETS)
