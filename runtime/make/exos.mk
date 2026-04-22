################################################################################
#
#       EXOS System Programs
#       Copyright (c) 1999-2025 Jango73
#
################################################################################

ARCH ?= x86-32

EXOS_MAKE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
EXOS_ROOT := $(abspath $(EXOS_MAKE_DIR)../..)
SYSTEM_ROOT := $(EXOS_ROOT)/system

APP_MAKEFILE := $(firstword $(MAKEFILE_LIST))
APP_DIR := $(abspath $(dir $(APP_MAKEFILE)))
REL_APP_DIR := $(patsubst $(SYSTEM_ROOT)/%,%,$(APP_DIR))
REL_APP_DIR := $(patsubst %/,%, $(REL_APP_DIR))

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
BUILD_DIR := $(EXOS_ROOT)/build/core/$(BUILD_CORE_NAME)

APP_OUT_DIR := $(BUILD_DIR)/system/$(REL_APP_DIR)

ifeq ($(strip $(APP_NAME)),)
$(error APP_NAME is not set)
endif

ifeq ($(strip $(APP_SOURCES)),)
$(error APP_SOURCES is not set)
endif

APP_HEADERS ?=
APP_TYPE ?= executable

ifeq ($(ARCH),x86-32)
TOOLCHAIN_PREFIX = i686-elf
CC      = $(TOOLCHAIN_PREFIX)-gcc
LD      = $(TOOLCHAIN_PREFIX)-ld
NM      = $(TOOLCHAIN_PREFIX)-nm
ARCH_CFLAGS =
ARCH_LDFLAGS =
else ifeq ($(ARCH),x86-64)
CC      = gcc
LD      = ld
NM      = nm
ARCH_CFLAGS = -m64
ARCH_LDFLAGS = -m elf_x86_64
else
$(error Unsupported architecture $(ARCH))
endif

COMMON_CFLAGS  = -ffreestanding -Wall -Wextra -O0 -fno-stack-protector -fno-builtin \
                 -I$(EXOS_ROOT)/runtime/include \
                 $(APP_EXTRA_CFLAGS) \
                 $(ARCH_CFLAGS)
EXECUTABLE_CFLAGS = -fno-pic
MODULE_CFLAGS = -fPIC -fvisibility=hidden -ftls-model=initial-exec

EXECUTABLE_LDFLAGS = -T $(EXOS_MAKE_DIR)exos.ld -nostdlib -Map=$(APP_OUT_DIR)/$(APP_NAME).map $(ARCH_LDFLAGS)
MODULE_LDFLAGS = -shared -T $(EXOS_MAKE_DIR)exos-module.ld -nostdlib --hash-style=sysv -z noexecstack -z text \
                 -Map=$(APP_OUT_DIR)/$(APP_NAME).map $(ARCH_LDFLAGS)

ifeq ($(APP_TYPE),executable)
CFLAGS = $(COMMON_CFLAGS) $(EXECUTABLE_CFLAGS)
LDFLAGS = $(EXECUTABLE_LDFLAGS)
ifeq ($(APP_USE_LIBGCC),1)
APP_EXTRA_LINK_INPUTS += $(shell $(CC) -print-libgcc-file-name)
endif
LINK_INPUTS = $(OBJS) $(LIB_EXOS) $(APP_EXTRA_LINK_INPUTS)
LINK_SCRIPT = $(EXOS_MAKE_DIR)exos.ld
else ifeq ($(APP_TYPE),module)
CFLAGS = $(COMMON_CFLAGS) $(MODULE_CFLAGS)
LDFLAGS = $(MODULE_LDFLAGS)
LINK_INPUTS = $(OBJS) $(APP_EXTRA_LINK_INPUTS)
LINK_SCRIPT = $(EXOS_MAKE_DIR)exos-module.ld
else
$(error Unsupported APP_TYPE $(APP_TYPE))
endif

TARGET   = $(APP_OUT_DIR)/$(APP_NAME)
TARGET_SYMBOLS = $(APP_OUT_DIR)/$(APP_NAME).sym
LIB_EXOS = $(BUILD_DIR)/runtime/libexos.a

SRC_C  = $(APP_SOURCES)
OBJ_C  = $(patsubst source/%.c, $(APP_OUT_DIR)/%.o, $(SRC_C))
OBJS   = $(OBJ_C)

all: $(TARGET) $(TARGET_SYMBOLS)

$(TARGET): $(LINK_INPUTS) $(LINK_SCRIPT)
	$(LD) $(LDFLAGS) -o $@ $(LINK_INPUTS)

$(APP_OUT_DIR)/%.o: source/%.c $(APP_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET_SYMBOLS): $(TARGET)
	@echo "Extracting $(APP_NAME) symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

clean:
	rm -rf $(APP_OUT_DIR)/*.o $(TARGET) $(APP_OUT_DIR)/*.elf $(APP_OUT_DIR)/*.bin $(APP_OUT_DIR)/*.map $(TARGET_SYMBOLS)
