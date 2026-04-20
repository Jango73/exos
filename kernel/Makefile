
SHELL = /bin/bash

include ../make/arch.mk
BUILD_CONFIGURATION     ?= release
DEBUG_OUTPUT            ?= 0
DEBUG_SPLIT             ?= 0
SCHEDULING_DEBUG_OUTPUT ?= 0
FORCE_PIC               ?= 0
TRACE_STACK_USAGE       ?= 0
PROFILING               ?= 0
USE_SYSCALL             ?= 0
SYSTEM_DATA_VIEW        ?= 0
KERNEL_LOG_DEFAULT_TAG_FILTER_SET ?= 0
KERNEL_LOG_DEFAULT_TAG_FILTER ?=

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

ARCH_CFLAGS =

ifeq ($(ARCH),x86-32)
TOOLCHAIN_PREFIX = i686-elf
NASM_FORMAT      = elf
CC               = $(TOOLCHAIN_PREFIX)-gcc
LD               = $(TOOLCHAIN_PREFIX)-ld
NM               = $(TOOLCHAIN_PREFIX)-nm
OBJCOPY          = $(TOOLCHAIN_PREFIX)-objcopy
READELF          = $(TOOLCHAIN_PREFIX)-readelf
else ifeq ($(ARCH),x86-64)
NASM_FORMAT      = elf64
CC               = gcc
LD               = ld
NM               = nm
OBJCOPY          = objcopy
READELF          = readelf
ARCH_CFLAGS      = -m64 -mcmodel=large
else
$(error Unsupported architecture $(ARCH))
endif

NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DDEBUG_SPLIT=$(DEBUG_SPLIT) -DFORCE_PIC=$(FORCE_PIC) -DUSE_SYSCALL=$(USE_SYSCALL) -DPROFILING=$(PROFILING)
DEBUG_CFLAGS    =
DEBUG_AFLAGS    =

ifeq ($(BUILD_CONFIGURATION),debug)
DEBUG_CFLAGS    += -g
DEBUG_AFLAGS    += -g -F dwarf
endif

CFLAGS          = -D__KERNEL__ $(DEBUG_CFLAGS) -Wall -Wextra -O0 -falign-functions=16 \
                  -ffreestanding -fno-builtin -nostdinc \
                  -fno-pic -fno-pie -fno-stack-protector \
                  -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
                  -fvisibility=hidden -mno-red-zone \
                  -Werror-implicit-function-declaration -Wstrict-prototypes \
                  -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DDEBUG_SPLIT=$(DEBUG_SPLIT) -DFORCE_PIC=$(FORCE_PIC) -DSCHEDULING_DEBUG_OUTPUT=$(SCHEDULING_DEBUG_OUTPUT) -DTRACE_STACK_USAGE=$(TRACE_STACK_USAGE) -DPROFILING=$(PROFILING) -DUSE_SYSCALL=$(USE_SYSCALL) -DSYSTEM_DATA_VIEW=$(SYSTEM_DATA_VIEW) \
                  -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) \
                  -DKERNEL_CONFIG_NAME=\"$(CONFIG_TOML_NAME)\" -DKERNEL_CONFIG_NAME_UPPER=\"$(CONFIG_TOML_NAME_UPPER)\" \
                  -Iinclude -Iinclude/desktop -I../third -I../third/include -I../runtime/include -I../third/bearssl/inc -I../third/bearssl/src -I../third/miniz -I../third/monocypher/src -I../third/monocypher/src/optional
CFLAGS         += $(ARCH_CFLAGS)
ifeq ($(KERNEL_LOG_DEFAULT_TAG_FILTER_SET),1)
CFLAGS         += -DKERNEL_LOG_DEFAULT_TAG_FILTER=\"$(KERNEL_LOG_DEFAULT_TAG_FILTER)\"
endif

NASM            = nasm

AFLAGS_INCLUDES = -I source/asm/ -I source/arch/$(ARCH)/asm/

ifeq ($(ARCH),x86-64)
AFLAGS_INCLUDES += -I source/arch/x86-32/asm/
endif

AFLAGS          = -f $(NASM_FORMAT) $(DEBUG_AFLAGS) -d__KERNEL__ -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DDEBUG_SPLIT=$(DEBUG_SPLIT) -DFORCE_PIC=$(FORCE_PIC) -DSCHEDULING_DEBUG_OUTPUT=$(SCHEDULING_DEBUG_OUTPUT) -DPROFILING=$(PROFILING) -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) -DUSE_SYSCALL=$(USE_SYSCALL) $(AFLAGS_INCLUDES)

LINKER_SCRIPT   = exos-$(ARCH).ld

LDFLAGS_ELF     = -T $(LINKER_SCRIPT) -static -nostdlib -no-pie --no-dynamic-linker --defsym=__VMA_KERNEL=$(VMA_KERNEL) -Map=$(BUILD_DIR)/kernel/exos.map

KERNEL_ELF      = $(BUILD_DIR)/kernel/exos.elf

SRC_C  = $(wildcard source/*.c) \
         $(wildcard source/core/*.c) \
         $(wildcard source/system/*.c) \
         $(wildcard source/process/*.c) \
         $(wildcard source/exec/*.c) \
         $(wildcard source/log/*.c) \
         $(wildcard source/memory/*.c) \
         $(wildcard source/fs/*.c) \
         $(wildcard source/sync/*.c) \
         $(wildcard source/user/*.c) \
         $(wildcard source/text/*.c) \
         $(wildcard source/text/font/*.c) \
         $(wildcard source/ui/layout/*.c) \
         $(wildcard source/ui/*.c) \
         $(wildcard source/desktop/*.c) \
         $(wildcard source/console/*.c) \
         $(wildcard source/input/*.c) \
         $(wildcard source/script/*.c) \
         $(wildcard source/shell/*.c) \
         $(wildcard source/arch/intel/*.c) \
         $(wildcard source/arch/$(ARCH)/*.c) \
         $(wildcard source/network/*.c) \
         $(wildcard source/drivers/filesystems/*.c) \
         $(wildcard source/drivers/platform/*.c) \
         $(wildcard source/drivers/bus/*.c) \
         $(wildcard source/drivers/interrupts/*.c) \
         $(wildcard source/drivers/input/*.c) \
         $(wildcard source/drivers/graphics/common/*.c) \
         $(wildcard source/drivers/graphics/gop/*.c) \
         $(wildcard source/drivers/graphics/igpu/*.c) \
         $(wildcard source/drivers/graphics/vesa/*.c) \
         $(wildcard source/drivers/graphics/vga/*.c) \
         $(wildcard source/drivers/usb/*.c) \
         $(wildcard source/drivers/network/*.c) \
         $(wildcard source/drivers/storage/*.c) \
         $(wildcard source/drivers/*.c) \
         $(wildcard source/expose/*.c) \
         $(wildcard source/package/*.c) \
         $(wildcard source/math/*.c) \
         $(wildcard source/utils/*.c) \
         $(wildcard source/test/*.c) \
         $(wildcard source/autotest/*.c) \
         ../third/bcrypt/blowfish.c \
         ../third/bcrypt/wrapbf.c \
         ../third/bcrypt/endian.c \
         ../third/bearssl/src/hash/sha2small.c \
         ../third/bearssl/src/codec/dec32be.c \
         ../third/bearssl/src/codec/enc32be.c \
         ../third/miniz/miniz.c \
         ../third/miniz/miniz_tdef.c \
         ../third/miniz/miniz_tinfl.c \
         ../third/monocypher/src/monocypher.c \
         ../third/monocypher/src/optional/monocypher-ed25519.c \
         ../third/utf8-hoehrmann/utf8-hoehrmann.c \
         ../runtime/source/exos-runtime-c.c
SRC_S  = $(wildcard source/asm/*.asm) \
         $(wildcard source/arch/$(ARCH)/asm/*.asm)

ifeq ($(ARCH),x86-64)
SRC_S += ../runtime/source/x86-64/exos-runtime-a.asm
else
SRC_S += ../runtime/source/x86-32/exos-runtime-a.asm
endif

OBJ_C  = $(patsubst source/%.c, $(BUILD_DIR)/kernel/%.o, $(filter source/%.c, $(SRC_C))) \
         $(patsubst ../third/bcrypt/%.c, $(BUILD_DIR)/kernel/bcrypt_%.o, $(filter ../third/bcrypt/%.c, $(SRC_C))) \
         $(patsubst ../third/bearssl/src/%.c, $(BUILD_DIR)/kernel/bearssl_%.o, $(filter ../third/bearssl/src/%.c, $(SRC_C))) \
         $(patsubst ../third/miniz/%.c, $(BUILD_DIR)/kernel/miniz_%.o, $(filter ../third/miniz/%.c, $(SRC_C))) \
         $(patsubst ../third/monocypher/src/monocypher.c, $(BUILD_DIR)/kernel/monocypher_monocypher.o, $(filter ../third/monocypher/src/monocypher.c, $(SRC_C))) \
         $(patsubst ../third/monocypher/src/optional/monocypher-ed25519.c, $(BUILD_DIR)/kernel/monocypher_opt_monocypher-ed25519.o, $(filter ../third/monocypher/src/optional/monocypher-ed25519.c, $(SRC_C))) \
         $(patsubst ../third/utf8-hoehrmann/%.c, $(BUILD_DIR)/kernel/utf8_%.o, $(filter ../third/utf8-hoehrmann/%.c, $(SRC_C))) \
         $(patsubst ../runtime/source/%.c, $(BUILD_DIR)/kernel/runtime_%.o, $(filter ../runtime/source/%.c, $(SRC_C)))
OBJ_S  = $(patsubst source/%.asm, $(BUILD_DIR)/kernel/%.o, $(filter source/%.asm, $(SRC_S))) \
         $(patsubst ../runtime/source/%.asm, $(BUILD_DIR)/kernel/runtime_%.o, $(filter ../runtime/source/%.asm, $(SRC_S)))

OBJS   = $(OBJ_S) $(OBJ_C)

# Sections to extract (edit as needed)
TEXT_BIN        = $(BUILD_DIR)/kernel/text.bin
SHARED_TEXT_BIN = $(BUILD_DIR)/kernel/shared_text.bin
DATA_BIN   		= $(BUILD_DIR)/kernel/data.bin
BSS_BIN    		= $(BUILD_DIR)/kernel/bss.bin
FINAL_IMG  		= $(BUILD_DIR)/kernel/exos.bin

ALIGN      		= 4096

.PHONY: all check_magic clean check-nogot

all: check_magic $(BUILD_DIR)/kernel/exos.sym

$(KERNEL_ELF): $(OBJS) $(LINKER_SCRIPT)
	$(LD) $(LDFLAGS_ELF) -o $@ $(OBJS)

$(TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .text section to $@"
	$(OBJCOPY) -O binary --only-section=.text $< $@

$(SHARED_TEXT_BIN): $(KERNEL_ELF)
	@echo "Extracting .shared_text section to $@"
	$(OBJCOPY) -O binary --only-section=.shared_text $< $@

$(DATA_BIN): $(KERNEL_ELF)
	@echo "Extracting .data section to $@"
	$(OBJCOPY) -O binary --only-section=.data $< $@

$(BSS_BIN): $(KERNEL_ELF)
	@echo "Extracting .bss section to $@"
	$(OBJCOPY) -O binary --only-section=.bss $< $@

# Build final padded image
$(FINAL_IMG): $(TEXT_BIN) $(SHARED_TEXT_BIN) $(DATA_BIN) $(KERNEL_ELF)
	@echo "Building paged disk image: $@ (ALIGN=$(ALIGN))"

	# Clear binary file
	rm -f -- $@

	# Append .text
	cat $(TEXT_BIN) >> $@

	# Align current image to next ALIGN boundary (before .shared_text)
	@{ \
		sz=$$(stat -c %s "$@"); \
		pad=$$(( ($(ALIGN) - (sz % $(ALIGN))) % $(ALIGN) )); \
		echo "Padding before .shared_text: $$pad bytes"; \
		if [ $$pad -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$pad >> "$@" 2>/dev/null; \
		fi; \
	}

	# Append .data
	cat $(SHARED_TEXT_BIN) >> $@

	# Align current image to next ALIGN boundary (before .data)
	@{ \
		sz=$$(stat -c %s "$@"); \
		pad=$$(( ($(ALIGN) - (sz % $(ALIGN))) % $(ALIGN) )); \
		echo "Padding before .data: $$pad bytes"; \
		if [ $$pad -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$pad >> "$@" 2>/dev/null; \
		fi; \
	}

	# Append .data
	cat $(DATA_BIN) >> $@

	# Align current image to next ALIGN boundary (before .bss)
	@{ \
		sz=$$(stat -c %s "$@"); \
		pad=$$(( ($(ALIGN) - (sz % $(ALIGN))) % $(ALIGN) )); \
		echo "Padding before .bss: $$pad bytes"; \
		if [ $$pad -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$pad >> "$@" 2>/dev/null; \
		fi; \
	}

	# Append zeroed .bss span so kernel runtime image size matches ELF layout
	@{ \
		bss_hex=$$($(READELF) -S --wide $(KERNEL_ELF) 2>/dev/null | awk '$$3 == ".bss" { print $$7; exit }'); \
		if [ -z "$$bss_hex" ]; then \
			echo "ERROR: could not resolve .bss size from $(KERNEL_ELF)"; \
			exit 1; \
		fi; \
		bss_size=$$((16#$$bss_hex)); \
		echo "Appending .bss zeros: $$bss_size bytes"; \
		if [ $$bss_size -gt 0 ]; then \
			dd if=/dev/zero bs=1 count=$$bss_size >> "$@" 2>/dev/null; \
		fi; \
	}

	# Keep magic marker as second-to-last U32 (just before checksum)
	@printf "\x45\x58\x4F\x53" >> "$@"

	# Append checksum
	@echo "Appending checksum to $(FINAL_IMG)"
	@cs=$$(head -c -0 -- "$(FINAL_IMG)" \
		| od -An -v -t u1 \
		| awk '{for(i=1;i<=NF;i++){s+=$$i; if(s>=4294967296)s-=4294967296}} END{printf "%u", s}'); \
	hex=$$(printf '%08x' $$cs); \
	b1=$${hex:0:2}; b2=$${hex:2:2}; b3=$${hex:4:2}; b4=$${hex:6:2}; \
	printf "\x$$b4\x$$b3\x$$b2\x$$b1" >> "$@"; \
	printf 'Checksum 0x%08X appended\n' $$cs

$(BUILD_DIR)/kernel/%.o: source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/%.o: source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

$(BUILD_DIR)/kernel/bcrypt_%.o: ../third/bcrypt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/bearssl_%.o: ../third/bearssl/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/miniz_%.o: ../third/miniz/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -c -o $@ $<

$(BUILD_DIR)/kernel/monocypher_monocypher.o: ../third/monocypher/src/monocypher.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/monocypher_opt_monocypher-ed25519.o: ../third/monocypher/src/optional/monocypher-ed25519.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/utf8_%.o: ../third/utf8-hoehrmann/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/runtime_%.o: ../runtime/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel/runtime_%.o: ../runtime/source/%.asm
	@mkdir -p $(dir $@)
	$(NASM) $(AFLAGS) -o $@ $<

check_magic: $(FINAL_IMG)
	@./check-exos-magic.sh $(FINAL_IMG)

# Extract debug symbols for Bochs
$(BUILD_DIR)/kernel/exos.sym: $(KERNEL_ELF)
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

# Optional sanity check to ensure the ELF has no GOT/PLT/DYNAMIC/INTERP sections
check-nogot: $(KERNEL_ELF)
	@echo "[check] scanning ELF for GOT/PLT/DYNAMIC/INTERP"
	@! $(READELF) -S $(KERNEL_ELF) 2>/dev/null | egrep -qi "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)" || \
	( echo "ERROR: unexpected GOT/PLT/DYNAMIC/INTERP sections in $(KERNEL_ELF)"; \
	  $(READELF) -S $(KERNEL_ELF) | egrep -i "(\\.(got(\\.|$$)|plt(\\.|$$)|dynamic|rela?)|interp)"; exit 1 )

clean:
	rm -rf $(BUILD_DIR)/kernel
