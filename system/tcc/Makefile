################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2026 Jango73
#
################################################################################

APP_NAME := tcc
APP_SOURCES := source/tcc-main.c

ifeq ($(ARCH),x86-32)
TCC_TARGET_CFLAGS := -DTCC_TARGET_I386=1
else ifeq ($(ARCH),x86-64)
TCC_TARGET_CFLAGS := -DTCC_TARGET_X86_64=1
else
$(error Unsupported architecture $(ARCH))
endif

APP_EXTRA_CFLAGS := -Iinclude -I../../kernel/include -I../../third/tinycc $(TCC_TARGET_CFLAGS) -DONE_SOURCE=1 -DCONFIG_TCC_STATIC=1 -DNDEBUG
APP_USE_LIBGCC := 1

include ../../runtime/make/exos.mk
