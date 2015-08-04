DIR              := test/unit

UNIT_BIN         := $(DIR)/unit.test
TEST_BIN_TARGETS += $(UNIT_BIN)
TESTS            += unit-tests

UNIT_SRCS        := $(wildcard $(DIR)/*.cpp)
UNIT_OBJS        := $(patsubst %.cpp,%.o,$(UNIT_SRCS))

UNIT_LDFLAGS     += -L. -ldttest_s -ldl -lpthread

# not our code - Wextra and Wundef cause clang to bail out
GTEST_CXXFLAGS   := $(filter-out -Wextra,$(CXXFLAGS:-Wundef=)) -DGTEST_HAS_PTHREAD=1

$(DIR)/gtest-all.o: $(DIR)/gtest-all.cpp $(CONFIG) $(DIR)/build.mk
	@echo "  CXX   $@"
	$(CXX) -O0 $(GTEST_CXXFLAGS) -MMD -o $@ -c $<

$(DIR)/gtest_main.o: $(DIR)/gtest_main.cpp $(CONFIG) $(DIR)/build.mk
	@echo "  CXX   $@"
	$(CXX) -O0 $(GTEST_CXXFLAGS) -MMD -o $@ -c $<

# no optimizations - clang is smart enough to see in the 'use after
# free' test that the object is allocated, referenced, freed, and
# used-after-free all in the main function, so it simply optimizes
# _all_ of that away.
$(UNIT_BIN): $(CONFIG) $(UNIT_OBJS) $(TESTLIB) $(DIR)/build.mk
	@echo "  LD    $@"
	$(CXX) -O0 $(CFLAGS) $(LDFLAGS) $(UNIT_LDFLAGS) -MMD -o $@ $(UNIT_OBJS)

-include $(OBJS:.o=.d)

unit-tests: $(UNIT_BIN)
	./$<

PHONY_TARGETS += simple-tests
