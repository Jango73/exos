################################################################################
#
#       EXOS TinyGL Demo
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

APP_NAME := tinygl-demo
APP_SOURCES := source/tinygl-demo.c
APP_USE_LIBGCC := 1

TINYGL_INCLUDES := -I../../third/tinygl/include -I../../third/tinygl/source/internal
APP_EXTRA_CFLAGS := $(TINYGL_INCLUDES)

include ../../runtime/make/exos.mk

LDFLAGS += --allow-multiple-definition

TINYGL_BUILD_DIR := $(APP_OUT_DIR)/tinygl

APP_EXTRA_LINK_INPUTS += \
    $(TINYGL_BUILD_DIR)/api/glmain.o \
    $(TINYGL_BUILD_DIR)/container/gllist.o \
    $(TINYGL_BUILD_DIR)/core/glraster.o \
    $(TINYGL_BUILD_DIR)/core/glutil.o \
    $(TINYGL_BUILD_DIR)/math/glvector.o

TINYGL_OBJS := $(APP_EXTRA_LINK_INPUTS)

$(TARGET): $(TINYGL_OBJS)

$(TINYGL_BUILD_DIR):
	@mkdir -p $@

$(TINYGL_BUILD_DIR)/api/glmain.o: ../../third/tinygl/source/api/glmain.c | $(TINYGL_BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fcommon -c -o $@ $<

$(TINYGL_BUILD_DIR)/container/gllist.o: ../../third/tinygl/source/container/gllist.c | $(TINYGL_BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fcommon -c -o $@ $<

$(TINYGL_BUILD_DIR)/core/glraster.o: ../../third/tinygl/source/core/glraster.c | $(TINYGL_BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fcommon -c -o $@ $<

$(TINYGL_BUILD_DIR)/core/glutil.o: ../../third/tinygl/source/core/glutil.c | $(TINYGL_BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fcommon -c -o $@ $<

$(TINYGL_BUILD_DIR)/math/glvector.o: ../../third/tinygl/source/math/glvector.c | $(TINYGL_BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -fcommon -c -o $@ $<

all: $(TARGET) $(TARGET_SYMBOLS)