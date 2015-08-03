# default optimization level
O=0

ARCH ?= amd64

# prefer clang
CC  = clang
CXX = clang++

# echo from modern GNU coreutils needs an explicit -e, otherwise it
# doesn't unescape control characters (like text coloring +
# formatting)
ECHO ?= echo -e

WARNFLAGS := \
        -Wall -Wextra -Wvariadic-macros \
        -Wformat -Wundef -Wpointer-arith \
        -Wcast-qual -Wwrite-strings -Wsign-compare \
        -Wstrict-aliasing=2 -Wno-unused-parameter \
        -Werror -Wno-error=unused-function \
        -Wno-gnu-zero-variadic-macro-arguments \
        -Wno-nested-anon-types -Wno-c99-extensions \
        -Wno-unused-variable -Wno-unused-private-field

CFLAGS   := -g -fPIC -pedantic -fno-omit-frame-pointer -Iinclude -Iheaplayers $(WARNFLAGS)
CXXFLAGS := -std=c++11 $(CFLAGS)
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
LIB_UNITTEST_OBJS := $(filter-out $(LIB_GLOBAL_OBJS), $(LIB_OBJS))

LIB      := libdoubletake.so
LIB_DEPS := dl pthread

TARGETS := $(LIB)
# make sure we recompile when the Makefile (and associated
# CFLAGS/LDFLAGS change)
CONFIG := Makefile

# clear out all suffixes
.SUFFIXES:
# list only those we use
.SUFFIXES: .d .c .cpp .s .o .test


all: $(TARGETS)

%.o: %_$(ARCH).s $(CONFIG)
	@echo "  AS    $@"
	$(CC) -O$(O) $(ASFLAGS) -o $@ -c $<

%.o: %.c $(CONFIG)
	@echo "  CC    $@"
	$(CC) -O$(O) $(CFLAGS) -MMD -o $@ -c $<

%.o: %.cpp $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) -O$(O) $(CXXFLAGS) -MMD -o $@ -c $<

libdoubletake.so: $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) $(addprefix -l,$(LIB_DEPS)) -o $@ $(LIB_OBJS)

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
	rm -f $(TARGETS)

distclean: clean
	find . -name '*.d' -print0 | xargs -0 rm -f
	find . -name '*~' -print0 | xargs -0 rm -f

-include $(LIB_OBJS:.o=.d)

.PHONY: all format clean distclean
