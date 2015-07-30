ROOT := .
DIRS := source tests

include $(ROOT)/common.mk

TAGS: include/*.hh sources/*.cpp
	(find include -name '*.hh' && find source -name '*.cpp') | xargs etags -a

format:
	@clang-format -i include/*.hh source/*.cpp
