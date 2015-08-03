DIR            := test

TEST_BIN_TARGETS += $(SIMPLE_TARGETS)
TESTS += simple-tests

SIMPLE_TESTS   := simple_uaf simple_overflow simple_leak
SIMPLE_TARGETS := $(addprefix $(DIR)/, $(addsuffix /simple.test, $(SIMPLE_TESTS)))
SIMPLE_LDFLAGS += -L. -ldoubletake

# no optimization - clang is smart enough to see in the 'use after
# free' test that the object is allocated, referenced, freed, and
# used-after-free all in the main function, so it simply optimizes
# _all_ of that away.
%/simple.test: $(CONFIG) $(LIB) $(DIR)/build.mk
	@echo "  LD    $@"
	$(CC) -O0 $(CFLAGS) $(LDFLAGS) $(SIMPLE_LDFLAGS) -MMD -o $@ $(@:simple.test=*.c)

-include $(SIMPLE_TARGETS:.test=.d)

simple-tests: $(SIMPLE_TARGETS)
	for i in $(SIMPLE_TARGETS); do LD_LIBRARY_PATH=. ./$$i; done

PHONY_TARGETS += simple-tests
