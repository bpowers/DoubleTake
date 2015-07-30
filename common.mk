# Build with clang
CC  := clang
CXX := clang++

# echo from modern GNU coreutils needs an explicit -e, otherwise it
# doesn't unescape control characters (like text coloring +
# formatting)
ECHO ?= echo -e

WARNFLAGS := -pedantic -Wvariadic-macros \
             -Wformat -Wall -Wextra -Wundef -Wpointer-arith \
             -Wcast-qual -Wwrite-strings -Wsign-compare \
             -Wstrict-aliasing=2 -Wno-unused-parameter \
             -Werror -Wno-error=unused-function \
             -Wno-gnu-zero-variadic-macro-arguments \
             -Wno-nested-anon-types -Wno-c99-extensions \
             -Wno-unused-variable

# Default flags
CFLAGS   += -g -O2 -fPIC $(WARNFLAGS)
CXXFLAGS += -std=c++11 $(CFLAGS)
LDFLAGS  += $(addprefix -l,$(LIBS))

# Default source and object files
SRCS    ?= $(wildcard *.cpp) $(wildcard *.c)
OBJS    ?= $(addprefix obj/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(SRCS))))

# Targets to build recirsively into $(DIRS)
RECURSIVE_TARGETS  ?= all clean bench test

# Build in parallel
MAKEFLAGS := -j

# Targets separated by type
SHARED_LIB_TARGETS := $(filter %.so, $(TARGETS))
STATIC_LIB_TARGETS := $(filter %.a, $(TARGETS))
OTHER_TARGETS      := $(filter-out %.so, $(filter-out %.a, $(TARGETS)))

# If not set, the build path is just the current directory name
MAKEPATH ?= `basename $(PWD)`

# Log the build path in gray, following by a log message in bold green
LOG_PREFIX := "\033[37;0m[$(MAKEPATH)]\033[0m\033[32;1m"
LOG_SUFFIX := "\033[0m"

# Build all targets by default
all:: $(TARGETS)

# Clean up after a build
clean::
	@for t in $(TARGETS); do \
	$(ECHO) $(LOG_PREFIX) Cleaning $$t $(LOG_SUFFIX); \
	done
	@rm -rf $(TARGETS) obj

# Prevent errors if files named all, clean, bench, or test exist
.PHONY: all clean bench test

# Compile a C++ source file (and generate its dependency rules)
obj/%.o: %.cpp $(PREREQS)
	@$(ECHO) $(LOG_PREFIX) Compiling $< $(LOG_SUFFIX)
	@mkdir -p obj
	@$(CXX) $(CXXFLAGS) -MMD -MP -o $@ -c $<

# Compile a C source file (and generate its dependency rules)
obj/%.o: %.c $(PREREQS)
	@$(ECHO) $(LOG_PREFIX) Compiling $< $(LOG_SUFFIX)
	@mkdir -p obj
	@$(CC) $(CFLAGS) -MMD -MP -o $@ -c $<

# Link a shared library 
$(SHARED_LIB_TARGETS): $(OBJS)
	@$(ECHO) $(LOG_PREFIX) Linking $@ $(LOG_SUFFIX)
	@$(CXX) -shared $(LDFLAGS) -o $@ $^

# Link binary targets
$(OTHER_TARGETS): $(OBJS)
	@$(ECHO) $(LOG_PREFIX) Linking $@ $(LOG_SUFFIX)
	@$(CXX) $(LDFLAGS) -o $@ $^

# Include dependency rules for all objects
-include $(OBJS:.o=.d)

# Build any recursive targets in subdirectories
$(RECURSIVE_TARGETS)::
	@for dir in $(DIRS); do \
	$(MAKE) -C $$dir --no-print-directory $@ MAKEPATH="$(MAKEPATH)/$$dir"; \
	done
