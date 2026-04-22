
ARCH ?= x86-32
BUILD_CONFIGURATION ?= release
PROFILING ?= 0
USE_SYSCALL ?= 0

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

NASM    = nasm

ifeq ($(ARCH),x86-32)
TOOLCHAIN_PREFIX = i686-elf
NASM_FORMAT      = elf
CC               = $(TOOLCHAIN_PREFIX)-gcc
CXX              = $(TOOLCHAIN_PREFIX)-g++
AR               = $(TOOLCHAIN_PREFIX)-ar
CFLAGS_ARCH      =
CXXFLAGS_ARCH    =
else ifeq ($(ARCH),x86-64)
NASM_FORMAT      = elf64
CC               = gcc
CXX              = g++
AR               = ar
CFLAGS_ARCH      = -m64
CXXFLAGS_ARCH    = -m64
else
$(error Unsupported architecture $(ARCH))
endif

DEBUG_CFLAGS =
DEBUG_AFLAGS =

ifeq ($(BUILD_CONFIGURATION),debug)
DEBUG_CFLAGS += -g
DEBUG_AFLAGS += -g -F dwarf
endif

CFLAGS  = $(DEBUG_CFLAGS) -Wall -Wextra -O0 -nostdinc \
        -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-builtin -fno-pic -fno-pie \
        -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
        -fvisibility=hidden -mno-red-zone \
        -Werror-implicit-function-declaration -Wstrict-prototypes \
        -DPROFILING=$(PROFILING) -DUSE_SYSCALL=$(USE_SYSCALL) \
        -I../kernel/include \
        -Iinclude \
        $(CFLAGS_ARCH)

CXXFLAGS = $(DEBUG_CFLAGS) -Wall -Wextra -O0 -std=c++11 \
        -ffreestanding -fno-stack-protector -mno-stack-arg-probe -fno-builtin -fno-pic -fno-pie \
        -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
        -fvisibility=hidden -mno-red-zone \
        -fno-rtti -fno-threadsafe-statics \
        -DPROFILING=$(PROFILING) -DUSE_SYSCALL=$(USE_SYSCALL) \
        -Iinclude \
        $(CXXFLAGS_ARCH)

AFLAGS  = -f $(NASM_FORMAT) $(DEBUG_AFLAGS) -I source/ -DPROFILING=$(PROFILING) -DUSE_SYSCALL=$(USE_SYSCALL)

LDFLAGS = -T linker.ld -nostdlib -Map=$(BUILD_DIR)/runtime/exos-runtime.map

RUNTIME_LIB = $(BUILD_DIR)/runtime/libexos.a
TCC_EMPTY_CRT = $(BUILD_DIR)/runtime/tcc-crt-empty.o
RUNTIME_LIB_OBJECT_MANIFEST = libc-objects.txt

SRC_C_ORDERED = \
	source/exos-adaptive-delay.c \
	source/exos.c \
	source/exos-module.c \
	source/exos-runtime-c.c \
	source/exos-runtime-stdlib.c \
	source/exos-string.c \
	source/http.c \
	source/tcc-crt-empty.c
SRC_C_EXTRA = $(filter-out $(SRC_C_ORDERED),$(wildcard source/*.c))
SRC_C   = $(SRC_C_ORDERED) $(SRC_C_EXTRA)
SRC_CXX = $(wildcard source/*.cpp)
SRC_S   = $(wildcard source/*.asm) $(wildcard source/$(ARCH)/*.asm)

OBJ_C   = $(patsubst source/%.c, $(BUILD_DIR)/runtime/%.o, $(filter source/%.c, $(SRC_C)))
# OBJ_CXX = $(patsubst source/%.cpp, $(BUILD_DIR)/runtime/%.o, $(SRC_CXX))
OBJ_S   = $(patsubst source/%.asm, $(BUILD_DIR)/runtime/%.o, $(SRC_S))

OBJS    = $(OBJ_S) $(OBJ_C) # $(OBJ_CXX)

# === Build rules ===

all: $(RUNTIME_LIB) $(BUILD_DIR)/runtime/crti.o $(BUILD_DIR)/runtime/crtn.o

$(RUNTIME_LIB): $(OBJS) $(RUNTIME_LIB_OBJECT_MANIFEST)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJS)
	@$(AR) t $@ | diff -u $(RUNTIME_LIB_OBJECT_MANIFEST) -

$(BUILD_DIR)/runtime/crti.o: $(TCC_EMPTY_CRT)
	cp -f $< $@

$(BUILD_DIR)/runtime/crtn.o: $(TCC_EMPTY_CRT)
	cp -f $< $@

$(BUILD_DIR)/runtime/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/runtime/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)/runtime/*.o $(BUILD_DIR)/runtime/*/*.o $(BUILD_DIR)/runtime/*.elf $(BUILD_DIR)/runtime/*.bin $(BUILD_DIR)/runtime/*.a $(BUILD_DIR)/runtime/*.map
