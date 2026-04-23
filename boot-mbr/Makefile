SHELL = /bin/bash
.DEFAULT_GOAL := all

include ../make/arch.mk
BUILD_CONFIGURATION ?= release

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
CORE_BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

ifeq ($(strip $(BUILD_IMAGE_NAME)),)
$(error BUILD_IMAGE_NAME is required)
endif
IMAGE_BUILD_DIR := ../build/image/$(BUILD_IMAGE_NAME)
MBR_BUILD_DIR := $(IMAGE_BUILD_DIR)/work-mbr

ifeq ($(ARCH),x86-32)
TOOLCHAIN_PREFIX = i686-elf
NASM_FORMAT = elf32
NASM_ARCH_FLAGS = -DARCH_X86_32
ARCH_CFLAGS = -DARCH_X86_32=1
else ifeq ($(ARCH),x86-64)
TOOLCHAIN_PREFIX = i686-elf
NASM_FORMAT = elf32
NASM_ARCH_FLAGS = -DARCH_X86_64
ARCH_CFLAGS = -DARCH_X86_64=1
else
$(error Unsupported architecture $(ARCH))
endif

DEBUG_OUTPUT    ?= 0
BOOT_STAGE_MARKERS ?= 0

IMG_SIZE_MB     ?= 16
FS_TEST_IMG_SIZE_MB ?= 80
FILE_SYSTEM     ?= ext2
EXT2_BLOCK_SIZE ?= 4096
KERNEL_FILE     ?= exos.bin
PAYLOAD_ADDRESS ?= 0x8000
MEMORY_BASE     ?= 0x10000
MEMORY_SIZE     ?= 0x80000
RESERVED_SECTORS= 64            # Reserved sectors for VBR payload
VBR_CODE_OFFSET = 0x5A          # Offset of code in VBR sector

KERNEL_CFG_FAT32= ../kernel/configuration/exos.fat32.toml
KERNEL_CFG_EXT2 = ../kernel/configuration/exos.ext2.toml
KERNEL_CFG_REF  = ../kernel/configuration/exos.ref.toml
KEYBOARD_LAYOUTS= ../deploy/keyboard/*.ekm1
MTOOLS_CONF     = .mtoolsrc.tmp
MBR_SOURCE      = source/mbr.asm

VBR_SRC         = source/vbr.asm
PAYLOAD_STUB    = source/vbr-payload-a.asm
PAYLOAD_C       = source/vbr-payload-c.c
PAYLOAD_STR_C   = ../kernel/source/text/CoreString.c

# Configurable payload source (can be overridden with make PAYLOAD_SOURCE=...)
PAYLOAD_SOURCE  ?= source/vbr-payload-c.c
ifeq ($(ARCH),x86-64)
PAYLOAD_ARCH_SOURCE = source/vbr-payload-x86-64-mbr.c
else ifeq ($(ARCH),x86-32)
PAYLOAD_ARCH_SOURCE = source/vbr-payload-x86-32-mbr.c
else
PAYLOAD_ARCH_SOURCE = source/vbr-payload-$(ARCH).c
endif
PAYLOAD_BUILD_ID ?= $(shell date +%Y%m%d-%H%M%S)

PAYLOAD_SOURCE_EFFECTIVE := $(PAYLOAD_SOURCE)
ifneq ($(findstring *,$(PAYLOAD_SOURCE)),)
PAYLOAD_SOURCE_WILDCARD := $(wildcard $(PAYLOAD_SOURCE))
ifneq ($(PAYLOAD_SOURCE_WILDCARD),)
PAYLOAD_SOURCE_EFFECTIVE := $(PAYLOAD_SOURCE_WILDCARD)
endif
endif

TEST_PAYLOAD := 0
ifneq ($(filter test-payloads/%,$(PAYLOAD_SOURCE_EFFECTIVE)),)
TEST_PAYLOAD := 1
endif

PAYLOAD_BIN_LD  = main-bin.ld
PAYLOAD_ELF_LD  = main-elf.ld

NASMFLAGS       = -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) -DBOOT_STAGE_MARKERS=$(BOOT_STAGE_MARKERS) -DRESERVED_SECTORS=$(RESERVED_SECTORS) -DPAYLOAD_ADDRESS=$(PAYLOAD_ADDRESS) -DMEMORY_BASE=$(MEMORY_BASE) -DMEMORY_SIZE=$(MEMORY_SIZE) -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) $(NASM_ARCH_FLAGS)
DEBUG_CFLAGS    =

ifeq ($(BUILD_CONFIGURATION),debug)
DEBUG_CFLAGS    += -g
endif

CFLAGS          = -m16 $(DEBUG_CFLAGS) -Wall -Wextra -O0 \
                  -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
                  -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
                  -fvisibility=hidden -mno-red-zone -fno-stack-protector \
                  -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2 \
                  -Werror-implicit-function-declaration -Wstrict-prototypes \
                  -DARCH=$(ARCH) \
                  -DKERNEL_FILE=\"$(KERNEL_FILE)\" \
                  -DDEBUG_OUTPUT=$(DEBUG_OUTPUT) \
                  -DBOOT_STAGE_MARKERS=$(BOOT_STAGE_MARKERS) \
                  -DPAYLOAD_ADDRESS=$(PAYLOAD_ADDRESS) \
                  -DMEMORY_BASE=$(MEMORY_BASE) \
                  -DMEMORY_SIZE=$(MEMORY_SIZE) \
                  -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) \
                  -DBOOT_PAYLOAD_BUILD_ID=\"$(PAYLOAD_BUILD_ID)\" \
                  -Iinclude -I../boot-shared/include -I../kernel/include
CFLAGS          += $(ARCH_CFLAGS)
NASMFLAGS       += -DBOOT_PAYLOAD_BUILD_ID=\"$(PAYLOAD_BUILD_ID)\"

NASM            = nasm
CC              = $(TOOLCHAIN_PREFIX)-gcc
LD              = $(TOOLCHAIN_PREFIX)-ld
NM              = $(TOOLCHAIN_PREFIX)-nm
LDFLAGS         = -nostdlib -no-pie
LDDEFS          = --defsym=PAYLOAD_ADDRESS=$(PAYLOAD_ADDRESS)
MBR_BIN         = $(MBR_BUILD_DIR)/mbr.bin
VBR_BIN         = $(MBR_BUILD_DIR)/vbr.bin
PAYLOAD_STUB_O  = $(MBR_BUILD_DIR)/vbr-payload-a.o
ifneq ($(filter test-payloads/%,$(PAYLOAD_SOURCE_EFFECTIVE)),)
PAYLOAD_C_SOURCES = $(PAYLOAD_SOURCE_EFFECTIVE)
else
ifneq ($(filter test-payloads/%,$(PAYLOAD_SOURCE_EFFECTIVE)),)
PAYLOAD_C_SOURCES = $(PAYLOAD_SOURCE_EFFECTIVE)
else
PAYLOAD_C_SOURCES = $(PAYLOAD_SOURCE_EFFECTIVE) $(PAYLOAD_ARCH_SOURCE) source/vbr-payload-fat32.c source/vbr-payload-ext2.c
endif
endif
BOOT_SHARED_C_SOURCES = ../boot-shared/source/boot-multiboot.c
PAYLOAD_C_OBJS  = $(patsubst source/%.c,$(MBR_BUILD_DIR)/%.o,$(filter source/%.c,$(PAYLOAD_C_SOURCES))) \
                  $(patsubst test-payloads/%.c,$(MBR_BUILD_DIR)/test-payloads/%.o,$(filter test-payloads/%.c,$(PAYLOAD_C_SOURCES))) \
                  $(patsubst ../boot-shared/source/%.c,$(MBR_BUILD_DIR)/boot-shared/%.o,$(BOOT_SHARED_C_SOURCES))
PAYLOAD_STR_O   = $(MBR_BUILD_DIR)/String.o
PAYLOAD_BIN     = $(MBR_BUILD_DIR)/vbr-payload.bin
PAYLOAD_ELF     = $(MBR_BUILD_DIR)/vbr-payload.elf
PAYLOAD_MAP     = $(MBR_BUILD_DIR)/vbr-payload.map
FINAL_IMG       = $(IMAGE_BUILD_DIR)/exos.img
USB3_IMG        = $(IMAGE_BUILD_DIR)/usb-3.img
FLOPPY_IMG      = $(IMAGE_BUILD_DIR)/floppy-3.5.img
FS_TEST_EXT2_IMG= $(IMAGE_BUILD_DIR)/fs-test-ext2.img
FS_TEST_FAT32_IMG= $(IMAGE_BUILD_DIR)/fs-test-fat32.img
FS_TEST_NTFS_IMG= $(IMAGE_BUILD_DIR)/fs-test-ntfs.img
NVME_IMG        = $(FS_TEST_EXT2_IMG)
NVME_UNSUPPORTED_IMG = $(FS_TEST_NTFS_IMG)
CYCLE_BIN       = $(CORE_BUILD_DIR)/tools/cycle
EPK_PACK_BIN    = $(CORE_BUILD_DIR)/tools/epk-pack
KERNEL_BIN      = $(CORE_BUILD_DIR)/kernel/exos.bin
PORTAL_ELF      = $(CORE_BUILD_DIR)/system/portal/portal
NETGET_ELF      = $(CORE_BUILD_DIR)/system/netget/netget
HELLO_ELF       = $(CORE_BUILD_DIR)/system/test/hello/hello
TICTACTOE_ELF	= $(CORE_BUILD_DIR)/system/tictactoe/tictactoe
TERMTACTICS_ELF	= $(CORE_BUILD_DIR)/system/terminal-tactics/terminal-tactics
INPUTINFO_ELF   = $(CORE_BUILD_DIR)/system/input-info/input-info
MEMORY_SMOKE_ELF = $(CORE_BUILD_DIR)/system/test/memory-stress/memory-stress
TCC_ELF         = $(CORE_BUILD_DIR)/system/tcc/tcc
TCC_CRT1_OBJ    = $(CORE_BUILD_DIR)/runtime/$(ARCH)/exos-runtime-a.o
TCC_CRTI_OBJ    = $(CORE_BUILD_DIR)/runtime/crti.o
TCC_CRTN_OBJ    = $(CORE_BUILD_DIR)/runtime/crtn.o
TCC_LIBC_A      = $(CORE_BUILD_DIR)/runtime/libexos.a
TCC_LIBC_MIT_LICENSE = ../runtime/LICENSE
MASTER_ELF      = $(CORE_BUILD_DIR)/system/test/master/master
SLAVE_ELF       = $(CORE_BUILD_DIR)/system/test/slave/slave
MODULE_HOST_ELF = $(CORE_BUILD_DIR)/system/test/module-host/module-host
MODULE_SAMPLE_ELF = $(CORE_BUILD_DIR)/system/test/module-sample/module-sample
MODULE_IMPORT_ELF = $(CORE_BUILD_DIR)/system/test/module-import/module-import
SYSTEM_TEST_EPK_STAGING_DIR = $(MBR_BUILD_DIR)/system-test-epk-root
SYSTEM_TEST_EPK = $(MBR_BUILD_DIR)/test.epk
EXOS_VERSION_MAJOR = $(shell awk '/#define EXOS_VERSION_MAJOR/ {print $$3; exit}' ../kernel/include/User.h)
EXOS_VERSION_MINOR = $(shell awk '/#define EXOS_VERSION_MINOR/ {print $$3; exit}' ../kernel/include/User.h)
SYSTEM_SCRIPT_FILES = $(wildcard ../system/scripts/*)
TCC_SAMPLE_FILES = $(wildcard ../system/tcc/samples/*)
TCC_INCLUDE_FILES = $(wildcard ../system/tcc/include/*)
RUNTIME_INCLUDE_FILES = $(wildcard ../runtime/include/*.h)
RUNTIME_INCLUDE_SYS_FILES = $(wildcard ../runtime/include/sys/*.h)
EXT2_STAGING_DIR= $(MBR_BUILD_DIR)/ext2-root
USB3_STAGING_DIR= $(MBR_BUILD_DIR)/usb-3-root
FS_TEST_EXT2_STAGING_DIR= $(MBR_BUILD_DIR)/fs-test-ext2-root
SYSTEM_STAGING_MANIFEST = $(MBR_BUILD_DIR)/system-staging-manifest.txt
COUNTERPART_IMAGE_NAME = $(subst -mbr-,-uefi-,$(BUILD_IMAGE_NAME))
COUNTERPART_SYSTEM_STAGING_MANIFEST = ../build/image/$(COUNTERPART_IMAGE_NAME)/work-uefi/system-staging-manifest.txt
FS_TEST_READ_FILE= read.txt
FS_TEST_READ_SOURCE= assets/fs-test-read.txt
EXT2_LABEL      ?= exos
CONFIG_TOML_NAME ?= exos.toml

ifeq ($(FILE_SYSTEM),ext2)
CREATE_IMAGE_MACRO = CREATE_EXT2_IMAGE
UPDATE_TARGET      = update-ext2
FS_SPECIFIC_CHECK  = @command -v mke2fs >/dev/null 2>&1 || { echo "mke2fs is not installed. Please run: sudo apt install e2fsprogs"; exit 1; }
else
CREATE_IMAGE_MACRO = CREATE_FAT32_IMAGE
UPDATE_TARGET      = update-fat32
FS_SPECIFIC_CHECK  = @command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed. Please run: sudo apt install mtools"; exit 1; }
endif

define MCOPY_IF_NEEDED
	if ! MTOOLSRC=$(MTOOLS_CONF) mdir "$(2)" >/dev/null 2>&1; then \
		echo "Copying $(1) -> $(2) (destination missing)"; \
		MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$(1)" "$(2)"; \
	else \
		SRC_MTIME=$$(stat -c %Y "$(1)"); \
		DST_INFO=$$(MTOOLSRC=$(MTOOLS_CONF) mdir -/ "$(2)" 2>/dev/null | awk 'NF>=7 {print $$6" "$$7; exit}'); \
		DST_EPOCH=$$(date -d "$$DST_INFO" +%s 2>/dev/null || echo ""); \
		if [ -z "$$DST_EPOCH" ] || [ $$SRC_MTIME -gt $$DST_EPOCH ]; then \
			if [ -z "$$DST_EPOCH" ]; then \
				echo "Copying $(1) -> $(2) (destination timestamp unavailable)"; \
			else \
				echo "Updating $(2) from $(1) (source newer)"; \
			fi; \
			MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$(1)" "$(2)"; \
		else \
			echo "Skipping $(2) (destination up-to-date)"; \
		fi; \
	fi;
endef

AUXILIARY_BUILD_DIR = $(MBR_BUILD_DIR)

ifeq ($(TEST_PAYLOAD),1)
USB3_IMAGE_DEPS = check_tools
define USB3_IMAGE_STAGING_COMMANDS
	@echo "Skipping USB MSD staging for test payload build."
endef
endif

include ../make/system-staging.mk
include ../make/image-artifacts.mk

.PHONY: all image update update-fat32 update-ext2 update-test-image install-mbr install-vbr-payload clean check_tools rebuild-image usb-3-image floppy-image normalize-partition-flags

# Default: build tools, ensure images exist (create once), then update contents
all: image $(MBR_BUILD_DIR)/payload.sym

image: check_tools normalize-partition-flags $(FLOPPY_IMG) update install-vbr-payload

normalize-partition-flags: $(FINAL_IMG) $(USB3_IMG) $(FS_TEST_EXT2_IMG) $(FS_TEST_FAT32_IMG) $(FS_TEST_NTFS_IMG)
	@echo "Normalizing partition active flags..."
	@if [ -f "$(FINAL_IMG)" ]; then parted -s "$(FINAL_IMG)" set 1 boot on; fi
	@if [ -f "$(USB3_IMG)" ]; then parted -s "$(USB3_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_EXT2_IMG)" ]; then parted -s "$(FS_TEST_EXT2_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_FAT32_IMG)" ]; then parted -s "$(FS_TEST_FAT32_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_NTFS_IMG)" ]; then parted -s "$(FS_TEST_NTFS_IMG)" set 1 boot off; fi

check_tools:
	@command -v $(NASM) >/dev/null 2>&1 || { echo "$(NASM) is not installed. Please run: sudo apt install nasm"; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed. Please run: sudo apt install parted"; exit 1; }
	@command -v $(CC) >/dev/null 2>&1 || { echo "$(CC) is not installed. Please install your cross toolchain."; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "$(LD) is not installed. Please install your cross toolchain."; exit 1; }
	@command -v mkfs.fat >/dev/null 2>&1 || { echo "mkfs.fat is not installed."; exit 1; }
	$(FS_SPECIFIC_CHECK)

$(MBR_BIN): $(MBR_SOURCE)
	@echo "Assembling custom MBR (NASM)..."
	@mkdir -p $(MBR_BUILD_DIR)
	$(NASM) $(NASMFLAGS) -f bin $(MBR_SOURCE) -o $(MBR_BIN)
	@{ \
		sz=$$(stat -c %s "$(MBR_BIN)"); \
		if [ $$sz -gt 510 ]; then \
			echo "ERROR: $(MBR_BIN) is too large ($$sz bytes). Must be <= 510."; \
			exit 1; \
		fi; \
	}

$(VBR_BIN): $(VBR_SRC)
	@echo "Assembling VBR (NASM)..."
	@mkdir -p $(MBR_BUILD_DIR)
	$(NASM) $(NASMFLAGS) -f bin $(VBR_SRC) -o $(VBR_BIN)
	@{ \
		sz=$$(stat -c %s "$(VBR_BIN)"); \
		max=$$((512 - $(VBR_CODE_OFFSET))); \
		if [ $$sz -gt $$max ]; then \
			echo "ERROR: $(VBR_BIN) is too large ($$sz bytes). Must be <= $$max."; \
			exit 1; \
		fi; \
	}

$(PAYLOAD_STUB_O): $(PAYLOAD_STUB)
	@echo "Assembling payload stub (NASM)..."
	@mkdir -p $(MBR_BUILD_DIR)
	$(NASM) $(NASMFLAGS) -f $(NASM_FORMAT) $< -o $@

$(MBR_BUILD_DIR)/%.o: source/%.c
	@echo "Compiling payload C from $<..."
	@mkdir -p $(MBR_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(MBR_BUILD_DIR)/test-payloads/%.o: test-payloads/%.c
	@echo "Compiling payload C from $<..."
	@mkdir -p $(MBR_BUILD_DIR)/test-payloads
	$(CC) $(CFLAGS) -c $< -o $@

$(MBR_BUILD_DIR)/boot-shared/%.o: ../boot-shared/source/%.c
	@echo "Compiling payload C from $<..."
	@mkdir -p $(MBR_BUILD_DIR)/boot-shared
	$(CC) $(CFLAGS) -c $< -o $@

$(PAYLOAD_STR_O): $(PAYLOAD_STR_C)
	@echo "Compiling String from $(PAYLOAD_STR_C)..."
	@mkdir -p $(MBR_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(PAYLOAD_BIN): $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O) $(PAYLOAD_BIN_LD)
	@echo "Linking payload stub + C into flat binary..."
	$(LD) $(LDDEFS) -T $(PAYLOAD_BIN_LD) $(LDFLAGS) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O)

$(PAYLOAD_ELF): $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O) $(PAYLOAD_ELF_LD)
	@echo "Linking payload stub + C into elf binary..."
	$(LD) $(LDDEFS) -T $(PAYLOAD_ELF_LD) $(LDFLAGS) -Map=$(PAYLOAD_MAP) -o $@ $(PAYLOAD_STUB_O) $(PAYLOAD_C_OBJS) $(PAYLOAD_STR_O)

$(MBR_BUILD_DIR)/payload.sym: $(PAYLOAD_ELF)
	# @echo "Extracting payload symbols for Bochs debugging"
	$(NM) $< | awk '{if($$2=="T" || $$2=="t") print $$1 " " $$3}' > $@

$(CYCLE_BIN):
	$(MAKE) -C ../tools ARCH=$(ARCH)

# Create image only if it does not exist
FINAL_IMG_DEPS_COMMON = check_tools $(MBR_BIN) $(VBR_BIN) $(PAYLOAD_BIN)
FINAL_IMG_DEPS_FULL = $(FINAL_IMG_DEPS_COMMON) $(KERNEL_BIN) $(PORTAL_ELF) $(NETGET_ELF) $(HELLO_ELF) $(TICTACTOE_ELF) $(TERMTACTICS_ELF) $(INPUTINFO_ELF) $(MASTER_ELF) $(SLAVE_ELF) $(MODULE_HOST_ELF) $(MODULE_SAMPLE_ELF) $(MODULE_IMPORT_ELF) $(SYSTEM_SCRIPT_FILES)

ifeq ($(TEST_PAYLOAD),1)
FINAL_IMG_DEPS = $(FINAL_IMG_DEPS_COMMON)
UPDATE_TARGET = update-test-image
else
FINAL_IMG_DEPS = $(FINAL_IMG_DEPS_FULL)
endif

$(FINAL_IMG): $(FINAL_IMG_DEPS)
	@mkdir -p $(dir $@)
	$(call $(CREATE_IMAGE_MACRO),$@)

$(KERNEL_CFG_FAT32): $(FINAL_IMG)
	@[ -f $@ ] || cp $(KERNEL_CFG_REF) $@

$(KERNEL_CFG_EXT2): $(FINAL_IMG)
	@[ -f $@ ] || cp $(KERNEL_CFG_REF) $@

update: $(UPDATE_TARGET)

$(EPK_PACK_BIN):
	@echo "Building epk-pack tool..."
	@+$(MAKE) -C ../tools ARCH=$(ARCH) BUILD_CORE_NAME=$(BUILD_CORE_NAME) $(EPK_PACK_BIN)

$(SYSTEM_TEST_EPK): $(EPK_PACK_BIN) $(MASTER_ELF) $(SLAVE_ELF)
	@echo "Building system test package: $(SYSTEM_TEST_EPK)"
	@rm -rf $(SYSTEM_TEST_EPK_STAGING_DIR)
	@mkdir -p $(SYSTEM_TEST_EPK_STAGING_DIR)/binary
	@cp $(MASTER_ELF) $(SYSTEM_TEST_EPK_STAGING_DIR)/binary/master
	@cp $(SLAVE_ELF) $(SYSTEM_TEST_EPK_STAGING_DIR)/binary/slave
	@printf '%s\n' \
		'name = "system.test"' \
		'version = "1.0.0"' \
		'arch = "$(ARCH)"' \
		'kernel_api = "$(EXOS_VERSION_MAJOR).$(EXOS_VERSION_MINOR)"' \
		'entry = "/binary/master"' \
		'' \
		'[commands]' \
		'master = "/binary/master"' \
		'slave = "/binary/slave"' \
		> $(SYSTEM_TEST_EPK_STAGING_DIR)/manifest.toml
	@$(EPK_PACK_BIN) pack --input $(SYSTEM_TEST_EPK_STAGING_DIR) --output $(SYSTEM_TEST_EPK)

update-test-image: check_payload_size $(FINAL_IMG)
	@echo "Skipping filesystem staging for test payload build."

update-fat32: check_payload_size $(FINAL_IMG) $(KERNEL_CFG_FAT32) $(SYSTEM_TEST_EPK)
	$(call MTOOLS_OPERATION,$(FINAL_IMG),Injecting files using mtools,\
		$(call MCOPY_IF_NEEDED,$(KERNEL_BIN),z:/EXOS.BIN) \
		$(call MCOPY_IF_NEEDED,$(KERNEL_CFG_FAT32),z:/$(CONFIG_TOML_NAME)) \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/KEYBOARD || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/USERS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TEST || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/SCRIPTS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/C || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/C/INCLUDE || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/C/INCLUDE/SYS || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TCC || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TCC/SAMPLES || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/APPS/TCC/INCLUDE || true; \
		MTOOLSRC=$(MTOOLS_CONF) mmd -o z:/EXOS/TEMP || true; \
		for f in $(KEYBOARD_LAYOUTS); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/KEYBOARD/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/KEYBOARD/$$(basename $$f)"; \
			fi; \
		done; \
		$(call MCOPY_IF_NEEDED,$(PORTAL_ELF),z:/EXOS/APPS/PORTAL) \
		$(call MCOPY_IF_NEEDED,$(NETGET_ELF),z:/EXOS/APPS/NETGET) \
		$(call MCOPY_IF_NEEDED,$(HELLO_ELF),z:/EXOS/APPS/HELLO)\
		$(call MCOPY_IF_NEEDED,$(TICTACTOE_ELF),z:/EXOS/APPS/tictactoe)\
		$(call MCOPY_IF_NEEDED,$(TERMTACTICS_ELF),z:/EXOS/APPS/terminal-tactics)\
		$(call MCOPY_IF_NEEDED,$(INPUTINFO_ELF),z:/EXOS/APPS/input-info)\
		$(call MCOPY_IF_NEEDED,$(MEMORY_SMOKE_ELF),z:/EXOS/APPS/memory-stress)\
		$(call MCOPY_IF_NEEDED,$(MEMORY_SMOKE_ELF),z:/EXOS/APPS/memory-fragmentation)\
		$(call MCOPY_IF_NEEDED,$(MEMORY_SMOKE_ELF),z:/EXOS/APPS/memory-growth)\
		if [ -f "$(TCC_ELF)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_ELF),z:/EXOS/APPS/TCC/tcc); fi; \
		$(call MCOPY_IF_NEEDED,$(MASTER_ELF),z:/EXOS/APPS/TEST/MASTER)\
		$(call MCOPY_IF_NEEDED,$(SLAVE_ELF),z:/EXOS/APPS/TEST/SLAVE)\
		$(call MCOPY_IF_NEEDED,$(MODULE_HOST_ELF),z:/EXOS/APPS/TEST/module-host)\
		$(call MCOPY_IF_NEEDED,$(MODULE_SAMPLE_ELF),z:/EXOS/APPS/TEST/module-sample)\
		$(call MCOPY_IF_NEEDED,$(MODULE_IMPORT_ELF),z:/EXOS/APPS/TEST/module-import)\
		$(call MCOPY_IF_NEEDED,$(SYSTEM_TEST_EPK),z:/EXOS/APPS/TEST.EPK)\
		for f in $(SYSTEM_SCRIPT_FILES); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/SCRIPTS/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/SCRIPTS/$$(basename $$f)"; \
			fi; \
		done; \
		for f in $(TCC_SAMPLE_FILES); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/APPS/TCC/SAMPLES/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/APPS/TCC/SAMPLES/$$(basename $$f)"; \
			fi; \
		done; \
		for f in $(TCC_INCLUDE_FILES); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/APPS/TCC/INCLUDE/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/APPS/TCC/INCLUDE/$$(basename $$f)"; \
			fi; \
		done; \
		for f in $(RUNTIME_INCLUDE_FILES); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/C/INCLUDE/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/C/INCLUDE/$$(basename $$f)"; \
			fi; \
		done; \
		for f in $(RUNTIME_INCLUDE_SYS_FILES); do \
			if [ -f "$$f" ]; then \
				echo "Copying $$f -> z:/EXOS/C/INCLUDE/SYS/$$(basename $$f)"; \
				MTOOLSRC=$(MTOOLS_CONF) mcopy -D o "$$f" "z:/EXOS/C/INCLUDE/SYS/$$(basename $$f)"; \
			fi; \
		done; \
		if [ -f "$(TCC_CRT1_OBJ)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_CRT1_OBJ),z:/EXOS/APPS/TCC/crt1.o); fi; \
		if [ -f "$(TCC_CRTI_OBJ)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_CRTI_OBJ),z:/EXOS/APPS/TCC/crti.o); fi; \
		if [ -f "$(TCC_CRTN_OBJ)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_CRTN_OBJ),z:/EXOS/APPS/TCC/crtn.o); fi; \
		if [ -f "$(TCC_LIBC_A)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_LIBC_A),z:/EXOS/APPS/TCC/libc.a); fi; \
		if [ -f "$(TCC_LIBC_MIT_LICENSE)" ]; then $(call MCOPY_IF_NEEDED,$(TCC_LIBC_MIT_LICENSE),z:/EXOS/APPS/TCC/LICENSE); fi,\
		All files injected into image.)

update-ext2: check_payload_size $(FINAL_IMG) $(KERNEL_CFG_EXT2) $(SYSTEM_TEST_EPK)
	$(call STAGE_SYSTEM_EXT2_TREE,$(EXT2_STAGING_DIR))
	$(call WRITE_SYSTEM_STAGING_MANIFEST,$(EXT2_STAGING_DIR),$(SYSTEM_STAGING_MANIFEST))
	$(call VERIFY_COUNTERPART_SYSTEM_STAGING,$(SYSTEM_STAGING_MANIFEST),$(COUNTERPART_SYSTEM_STAGING_MANIFEST))
	$(call POPULATE_EXT2_IMAGE,$(FINAL_IMG),$(EXT2_STAGING_DIR))

install-mbr: update $(MBR_BIN) $(VBR_BIN)
	@echo "Installing custom MBR into image (assembled from $(MBR_SOURCE))..."
	dd if=$(MBR_BIN) of=$(FINAL_IMG) bs=446 count=1 conv=notrunc
	printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=510 count=2 conv=notrunc
	@echo "Custom MBR written."

ifeq ($(TEST_PAYLOAD),1)
install-vbr-payload: check_payload_size install-mbr $(PAYLOAD_BIN) $(VBR_BIN) $(USB3_IMG) $(CYCLE_BIN)
else
install-vbr-payload: check_payload_size install-mbr $(PAYLOAD_BIN) $(VBR_BIN)
endif
	@echo "Injecting VBR code at offset 0x5A in VBR sector, and payload into the pre-partition gap..."
	@{ \
		PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		VBR_SIZE=$$(stat -c %s $(VBR_BIN)); \
		PART_SECTOR=$$((PART_OFFSET / 512)); \
		PAYLOAD_SIZE=$$(stat -c %s $(PAYLOAD_BIN)); \
		PAYLOAD_SECTORS_ACTUAL=$$(( (PAYLOAD_SIZE + 511) / 512 )); \
		if [ $$PAYLOAD_SECTORS_ACTUAL -gt $(RESERVED_SECTORS) ]; then \
			echo "ERROR: Payload uses $$PAYLOAD_SECTORS_ACTUAL sectors but only $(RESERVED_SECTORS) are reserved."; \
			exit 1; \
		fi; \
		PAYLOAD_START=$$((PART_SECTOR - $(RESERVED_SECTORS))); \
		if [ $$PAYLOAD_START -lt 1 ]; then \
			echo "ERROR: Not enough gap before partition to store payload."; \
			exit 1; \
		fi; \
		dd if=/dev/zero of=$(FINAL_IMG) bs=512 seek=$$PAYLOAD_START count=$(RESERVED_SECTORS) conv=notrunc; \
		dd if=$(PAYLOAD_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PAYLOAD_START * 512)) conv=notrunc; \
		dd if=$(VBR_BIN) of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + $(VBR_CODE_OFFSET))) count=$$VBR_SIZE conv=notrunc; \
		printf '\125\252' | dd of=$(FINAL_IMG) bs=1 seek=$$((PART_OFFSET + 510)) count=2 conv=notrunc; \
		echo "VBR code and payload injected."; \
	}

hexdump-check: install-vbr-payload
	@echo "--- [ MBR +0 (16 bytes) ] ---"
	@hexdump -Cv -n 16 -s 0 $(FINAL_IMG)
	@echo "--- [ MBR code ] ---"
	@MBR_OFF=0; \
	dd if=$(FINAL_IMG) bs=1 skip=$$MBR_OFF count=32 2>/dev/null | ndisasm -b 16 -o 0x7E5A -
	@echo "--- [ MBR signature ] ---"
	@hexdump -Cv -n 2 -s 510 $(FINAL_IMG)

	@echo "--- [ VBR +0 (16 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 4 -s $$((PART_OFFSET + 0)) $(FINAL_IMG)
	@echo "--- [ VBR +0x5A (16 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 16 -s $$((PART_OFFSET + 0x5A)) $(FINAL_IMG)
	@echo "--- [ VBR code ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_OFF=$$((PART_OFFSET + 0x5A)); \
	dd if=$(FINAL_IMG) bs=1 skip=$$PAYLOAD_OFF count=32 2>/dev/null | ndisasm -b 16 -o 0x7E5A -
	@echo "--- [ VBR signature ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	hexdump -Cv -n 2 -s $$((PART_OFFSET + 510)) $(FINAL_IMG)

	@echo "--- [ Payload sector +0 (64 bytes) ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_SECTOR_START=$$((PART_OFFSET / 512 - $(RESERVED_SECTORS))); \
	PAYLOAD_OFF=$$((PAYLOAD_SECTOR_START * 512)); \
	hexdump -Cv -n 64 -s $$PAYLOAD_OFF $(FINAL_IMG)
	@echo "--- [ Payload code ] ---"
	@PART_OFFSET=$$(parted -s $(FINAL_IMG) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
	PAYLOAD_SECTOR_START=$$((PART_OFFSET / 512 - $(RESERVED_SECTORS))); \
	PAYLOAD_OFF=$$((PAYLOAD_SECTOR_START * 512)); \
	dd if=$(FINAL_IMG) bs=1 skip=$$PAYLOAD_OFF count=16 2>/dev/null | ndisasm -b 16 -o $(PAYLOAD_ADDRESS) -

clean:
	rm -rf $(MTOOLS_CONF) $(MBR_BUILD_DIR) $(FINAL_IMG) $(USB3_IMG) $(FLOPPY_IMG) $(FS_TEST_EXT2_IMG) $(FS_TEST_FAT32_IMG) $(FS_TEST_NTFS_IMG)

.PHONY: check_payload_size
check_payload_size: $(PAYLOAD_BIN)
	@{ \
		PAYLOAD_SIZE=$$(stat -c %s $(PAYLOAD_BIN)); \
		MAX_SIZE=$$(( $(RESERVED_SECTORS) * 512 )); \
		if [ $$PAYLOAD_SIZE -gt $$MAX_SIZE ]; then \
			echo "ERROR: VBR payload size ($$PAYLOAD_SIZE bytes) exceeds reserved space ($$MAX_SIZE bytes)."; \
			exit 1; \
		fi; \
		PAYLOAD_END=$$(( $(PAYLOAD_ADDRESS) + PAYLOAD_SIZE )); \
                if (( PAYLOAD_END >= $(MEMORY_BASE) )); then \
			echo "ERROR: Payload end address (0x$$(printf '%X' $$PAYLOAD_END)) overlaps memory base ($(MEMORY_BASE))."; \
			exit 1; \
		fi; \
	}
