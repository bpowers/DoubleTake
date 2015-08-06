ROOT    := ../..
TARGETS := simple_mt_uaf
CFLAGS  := -std=c11 -g -fno-omit-frame-pointer
LDFLAGS := -L$(ROOT) -ldoubletake

include $(ROOT)/common.mk

test::
	@LD_LIBRARY_PATH=$(ROOT) ./simple_mt_uaf
