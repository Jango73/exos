SHELL = /bin/bash
.DEFAULT_GOAL := all

include ../make/arch.mk

ifeq ($(strip $(BUILD_CORE_NAME)),)
$(error BUILD_CORE_NAME is required)
endif
CORE_BUILD_DIR := ../build/core/$(BUILD_CORE_NAME)

ifeq ($(strip $(BUILD_IMAGE_NAME)),)
$(error BUILD_IMAGE_NAME is required)
endif
IMAGE_BUILD_DIR := ../build/image/$(BUILD_IMAGE_NAME)
UEFI_BUILD_DIR := $(IMAGE_BUILD_DIR)/work-uefi
IMG_SIZE_MB ?= 16
ESP_SIZE_MB ?= 4
FS_TEST_IMG_SIZE_MB ?= 80
EXT2_BLOCK_SIZE ?= 4096
EXT2_LABEL ?= exos
CONFIG_TOML_NAME ?= exos.toml
UEFI_EARLY_HALT ?= 0
UEFI_STUB_TEST ?= 0
UEFI_STUB_EARLY_CALL ?= 0
UEFI_STUB_REPLACE ?= 0
UEFI_STUB_C_FALLBACK ?= 0
UEFI_LOG_USE_UDP ?= 0
BOOT_STAGE_MARKERS ?= 0
UEFI_LOG_UDP_DEST_IP_0 ?= 192
UEFI_LOG_UDP_DEST_IP_1 ?= 168
UEFI_LOG_UDP_DEST_IP_2 ?= 50
UEFI_LOG_UDP_DEST_IP_3 ?= 1
UEFI_LOG_UDP_SOURCE_IP_0 ?= 192
UEFI_LOG_UDP_SOURCE_IP_1 ?= 168
UEFI_LOG_UDP_SOURCE_IP_2 ?= 50
UEFI_LOG_UDP_SOURCE_IP_3 ?= 2
UEFI_LOG_UDP_DEST_PORT ?= 18194
UEFI_LOG_UDP_SOURCE_PORT ?= 18195

KERNEL_BIN := $(CORE_BUILD_DIR)/kernel/exos.bin
UEFI_IMAGE := $(IMAGE_BUILD_DIR)/exos-uefi.img
UEFI_STAGING_DIR := $(UEFI_BUILD_DIR)/esp-root
EXT2_STAGING_DIR := $(UEFI_BUILD_DIR)/ext2-root
USB3_IMG := $(IMAGE_BUILD_DIR)/usb-3.img
FLOPPY_IMG := $(IMAGE_BUILD_DIR)/floppy-3.5.img
FS_TEST_EXT2_IMG := $(IMAGE_BUILD_DIR)/fs-test-ext2.img
FS_TEST_FAT32_IMG := $(IMAGE_BUILD_DIR)/fs-test-fat32.img
FS_TEST_NTFS_IMG := $(IMAGE_BUILD_DIR)/fs-test-ntfs.img
USB3_STAGING_DIR := $(UEFI_BUILD_DIR)/usb-3-root
FS_TEST_EXT2_STAGING_DIR := $(UEFI_BUILD_DIR)/fs-test-ext2-root
FS_TEST_READ_FILE := read.txt
FS_TEST_READ_SOURCE := ../boot-mbr/assets/fs-test-read.txt

KERNEL_CFG_EXT2 := ../kernel/configuration/exos.ext2.toml
KERNEL_CFG_REF  := ../kernel/configuration/exos.ref.toml
KEYBOARD_LAYOUTS := ../deploy/keyboard/*.ekm1

PORTAL_ELF      := $(CORE_BUILD_DIR)/system/portal/portal
NETGET_ELF      := $(CORE_BUILD_DIR)/system/netget/netget
HELLO_ELF       := $(CORE_BUILD_DIR)/system/hello/hello
TICTACTOE_ELF   := $(CORE_BUILD_DIR)/system/tictactoe/tictactoe
TERMTACTICS_ELF := $(CORE_BUILD_DIR)/system/terminal-tactics/terminal-tactics
INPUTINFO_ELF   := $(CORE_BUILD_DIR)/system/input-info/input-info
MEMORY_SMOKE_ELF := $(CORE_BUILD_DIR)/system/memory-stress/memory-stress
MASTER_ELF      := $(CORE_BUILD_DIR)/system/test/master/master
SLAVE_ELF       := $(CORE_BUILD_DIR)/system/test/slave/slave
MODULE_HOST_ELF := $(CORE_BUILD_DIR)/system/test/module-host/module-host
MODULE_SAMPLE_ELF := $(CORE_BUILD_DIR)/system/test/module-sample/module-sample
MODULE_IMPORT_ELF := $(CORE_BUILD_DIR)/system/test/module-import/module-import
SYSTEM_SCRIPT_FILES := $(wildcard ../system/scripts/*)
EXOS_VERSION_MAJOR = $(shell awk '/#define EXOS_VERSION_MAJOR/ {print $$3; exit}' ../kernel/include/User.h)
EXOS_VERSION_MINOR = $(shell awk '/#define EXOS_VERSION_MINOR/ {print $$3; exit}' ../kernel/include/User.h)
EPK_PACK_BIN    := $(CORE_BUILD_DIR)/tools/epk-pack
SYSTEM_TEST_EPK_STAGING_DIR := $(UEFI_BUILD_DIR)/system-test-epk-root
SYSTEM_TEST_EPK := $(UEFI_BUILD_DIR)/test.epk
MTOOLS_CONF := .mtoolsrc.tmp

ifeq ($(ARCH),x86-32)
UEFI_TARGET := i686-unknown-windows
UEFI_MACHINE := X86
UEFI_BOOT_FILE := BOOTIA32.EFI
UEFI_ARCH_SOURCE := ../boot-mbr/source/vbr-payload-x86-32-uefi.c
UEFI_JUMP_SOURCE := source/uefi-jump-x86-32.asm
NASM_FORMAT := win32
else ifeq ($(ARCH),x86-64)
UEFI_TARGET := x86_64-unknown-windows
UEFI_MACHINE := X64
UEFI_BOOT_FILE := BOOTX64.EFI
UEFI_ARCH_SOURCE := ../boot-mbr/source/vbr-payload-x86-64-uefi.c
UEFI_JUMP_SOURCE := source/uefi-jump-x86-64.asm
NASM_FORMAT := win64
else
$(error Unsupported architecture $(ARCH))
endif

CC := clang
LD := lld-link
NASM := nasm

CFLAGS := -ffreestanding -fno-stack-protector -fno-builtin -fno-pic -fno-pie \
          -fno-asynchronous-unwind-tables -fno-exceptions -fno-unwind-tables \
          -mno-red-zone -Wall -Wextra -Werror-implicit-function-declaration -Wstrict-prototypes \
          -DBOOT_UEFI=1 -DPAYLOAD_ADDRESS=0x8000 -DMEMORY_BASE=0x10000 -DMEMORY_SIZE=0x80000 \
          -DCONFIG_VMA_KERNEL=$(VMA_KERNEL) \
          -DUEFI_EARLY_HALT=$(UEFI_EARLY_HALT) \
          -DUEFI_STUB_TEST=$(UEFI_STUB_TEST) \
          -DUEFI_STUB_EARLY_CALL=$(UEFI_STUB_EARLY_CALL) \
          -DUEFI_STUB_REPLACE=$(UEFI_STUB_REPLACE) \
          -DUEFI_STUB_C_FALLBACK=$(UEFI_STUB_C_FALLBACK) \
          -DBOOT_STAGE_MARKERS=$(BOOT_STAGE_MARKERS) \
          -DUEFI_LOG_USE_UDP=$(UEFI_LOG_USE_UDP) \
          -DUEFI_LOG_UDP_DEST_IP_0=$(UEFI_LOG_UDP_DEST_IP_0) \
          -DUEFI_LOG_UDP_DEST_IP_1=$(UEFI_LOG_UDP_DEST_IP_1) \
          -DUEFI_LOG_UDP_DEST_IP_2=$(UEFI_LOG_UDP_DEST_IP_2) \
          -DUEFI_LOG_UDP_DEST_IP_3=$(UEFI_LOG_UDP_DEST_IP_3) \
          -DUEFI_LOG_UDP_SOURCE_IP_0=$(UEFI_LOG_UDP_SOURCE_IP_0) \
          -DUEFI_LOG_UDP_SOURCE_IP_1=$(UEFI_LOG_UDP_SOURCE_IP_1) \
          -DUEFI_LOG_UDP_SOURCE_IP_2=$(UEFI_LOG_UDP_SOURCE_IP_2) \
          -DUEFI_LOG_UDP_SOURCE_IP_3=$(UEFI_LOG_UDP_SOURCE_IP_3) \
          -DUEFI_LOG_UDP_DEST_PORT=$(UEFI_LOG_UDP_DEST_PORT) \
          -DUEFI_LOG_UDP_SOURCE_PORT=$(UEFI_LOG_UDP_SOURCE_PORT) \
          -DARCH=$(ARCH) \
          -Iinclude -I../boot-mbr/include -I../boot-shared/include -I../kernel/include

LDFLAGS := /subsystem:efi_application /entry:EfiMain /nodefaultlib /dll /machine:$(UEFI_MACHINE)

UEFI_BOOT_FILE_PATH := $(UEFI_BUILD_DIR)/$(UEFI_BOOT_FILE)

UEFI_C_SOURCES := source/boot-uefi.c \
                  source/uefi-log-udp.c \
                  source/uefi-compat.c \
                  ../boot-shared/source/boot-multiboot.c \
                  ../kernel/source/text/CoreString.c \
                  $(UEFI_ARCH_SOURCE)

UEFI_C_OBJS := $(UEFI_BUILD_DIR)/boot-uefi.o \
               $(UEFI_BUILD_DIR)/uefi-log-udp.o \
               $(UEFI_BUILD_DIR)/uefi-compat.o \
               $(UEFI_BUILD_DIR)/boot-multiboot.o \
               $(UEFI_BUILD_DIR)/CoreString.o \
               $(UEFI_BUILD_DIR)/vbr-payload-$(ARCH).o

ifeq ($(UEFI_STUB_C_FALLBACK),1)
UEFI_ASM_OBJ :=
else
UEFI_ASM_OBJ := $(UEFI_BUILD_DIR)/uefi-jump-$(ARCH).o
endif

AUXILIARY_BUILD_DIR := $(UEFI_BUILD_DIR)

include ../make/image-artifacts.mk

.PHONY: all clean check_tools image usb-3-image floppy-image normalize-partition-flags

all: image

image: check_tools normalize-partition-flags $(FLOPPY_IMG) $(UEFI_IMAGE)

check_tools:
	@command -v $(CC) >/dev/null 2>&1 || { echo "clang is not installed."; exit 1; }
	@command -v $(LD) >/dev/null 2>&1 || { echo "lld-link is not installed."; exit 1; }
	@command -v $(NASM) >/dev/null 2>&1 || { echo "nasm is not installed."; exit 1; }
	@command -v mformat >/dev/null 2>&1 || { echo "mtools is not installed."; exit 1; }
	@command -v mcopy >/dev/null 2>&1 || { echo "mtools is not installed."; exit 1; }
	@command -v parted >/dev/null 2>&1 || { echo "parted is not installed."; exit 1; }
	@command -v mke2fs >/dev/null 2>&1 || { echo "mke2fs is not installed. Please run: sudo apt install e2fsprogs"; exit 1; }
	@command -v mkfs.fat >/dev/null 2>&1 || { echo "mkfs.fat is not installed."; exit 1; }

normalize-partition-flags: $(UEFI_IMAGE) $(USB3_IMG) $(FS_TEST_EXT2_IMG) $(FS_TEST_FAT32_IMG) $(FS_TEST_NTFS_IMG)
	@echo "Normalizing partition active flags..."
	@if [ -f "$(UEFI_IMAGE)" ]; then parted -s "$(UEFI_IMAGE)" set 1 esp on; fi
	@if [ -f "$(USB3_IMG)" ]; then parted -s "$(USB3_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_EXT2_IMG)" ]; then parted -s "$(FS_TEST_EXT2_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_FAT32_IMG)" ]; then parted -s "$(FS_TEST_FAT32_IMG)" set 1 boot off; fi
	@if [ -f "$(FS_TEST_NTFS_IMG)" ]; then parted -s "$(FS_TEST_NTFS_IMG)" set 1 boot off; fi

$(UEFI_BUILD_DIR):
	@mkdir -p $(UEFI_BUILD_DIR)

$(UEFI_BUILD_DIR)/boot-uefi.o: source/boot-uefi.c | $(UEFI_BUILD_DIR)
	@echo "Compiling UEFI loader..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/boot-multiboot.o: ../boot-shared/source/boot-multiboot.c | $(UEFI_BUILD_DIR)
	@echo "Compiling Multiboot helpers..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/uefi-compat.o: source/uefi-compat.c | $(UEFI_BUILD_DIR)
	@echo "Compiling UEFI compat..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/uefi-log-udp.o: source/uefi-log-udp.c | $(UEFI_BUILD_DIR)
	@echo "Compiling UEFI UDP logger..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/CoreString.o: ../kernel/source/text/CoreString.c | $(UEFI_BUILD_DIR)
	@echo "Compiling CoreString..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_BUILD_DIR)/vbr-payload-$(ARCH).o: $(UEFI_ARCH_SOURCE) | $(UEFI_BUILD_DIR)
	@echo "Compiling paging helpers..."
	$(CC) -target $(UEFI_TARGET) $(CFLAGS) -c $< -o $@

$(UEFI_ASM_OBJ): $(UEFI_JUMP_SOURCE) | $(UEFI_BUILD_DIR)
	@echo "Assembling UEFI jump stub..."
	$(NASM) -DBOOT_STAGE_MARKERS=$(BOOT_STAGE_MARKERS) -f $(NASM_FORMAT) $< -o $@

$(UEFI_BOOT_FILE_PATH): $(UEFI_C_OBJS) $(UEFI_ASM_OBJ)
	@echo "Linking UEFI bootloader..."
	$(LD) $(LDFLAGS) /out:$@ $(UEFI_C_OBJS) $(UEFI_ASM_OBJ)

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

$(UEFI_IMAGE): $(UEFI_BOOT_FILE_PATH) $(KERNEL_BIN) $(SYSTEM_SCRIPT_FILES) $(SYSTEM_TEST_EPK) $(MODULE_HOST_ELF) $(MODULE_SAMPLE_ELF) $(MODULE_IMPORT_ELF) $(USB3_IMG) $(FS_TEST_EXT2_IMG) $(FS_TEST_FAT32_IMG) $(FS_TEST_NTFS_IMG)
	@mkdir -p $(UEFI_BUILD_DIR)
	@rm -rf $(UEFI_STAGING_DIR)
	@mkdir -p $(UEFI_STAGING_DIR)/EFI/BOOT
	@cp -f $(UEFI_BOOT_FILE_PATH) $(UEFI_STAGING_DIR)/EFI/BOOT/$(UEFI_BOOT_FILE)
	@cp -f $(KERNEL_BIN) $(UEFI_STAGING_DIR)/exos.bin
	@[ -f $(KERNEL_CFG_EXT2) ] || cp $(KERNEL_CFG_REF) $(KERNEL_CFG_EXT2)
	@echo "Preparing EXT2 staging directory at $(EXT2_STAGING_DIR)"
	@rm -rf $(EXT2_STAGING_DIR)
	@mkdir -p $(EXT2_STAGING_DIR)/exos/data
	@mkdir -p $(EXT2_STAGING_DIR)/exos/users
	@mkdir -p $(EXT2_STAGING_DIR)/exos/apps/test
	@mkdir -p $(EXT2_STAGING_DIR)/exos/keyboard
	@mkdir -p $(EXT2_STAGING_DIR)/exos/scripts
	@mkdir -p $(EXT2_STAGING_DIR)/exos/temp
	@cp $(KERNEL_BIN) $(EXT2_STAGING_DIR)/exos.bin
	@cp $(KERNEL_CFG_EXT2) $(EXT2_STAGING_DIR)/$(CONFIG_TOML_NAME)
	@for f in $(KEYBOARD_LAYOUTS); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $(EXT2_STAGING_DIR)/exos/keyboard/$$(basename $$f)"; \
			cp "$$f" "$(EXT2_STAGING_DIR)/exos/keyboard/$$(basename $$f)"; \
		fi; \
	done
	@cp $(PORTAL_ELF) $(EXT2_STAGING_DIR)/exos/apps/portal
	@cp $(NETGET_ELF) $(EXT2_STAGING_DIR)/exos/apps/netget
	@cp $(HELLO_ELF) $(EXT2_STAGING_DIR)/exos/apps/hello
	@cp $(TICTACTOE_ELF) $(EXT2_STAGING_DIR)/exos/apps/tictactoe
	@cp $(TERMTACTICS_ELF) $(EXT2_STAGING_DIR)/exos/apps/terminal-tactics
	@cp $(INPUTINFO_ELF) $(EXT2_STAGING_DIR)/exos/apps/input-info
	@cp $(MEMORY_SMOKE_ELF) $(EXT2_STAGING_DIR)/exos/apps/memory-stress
	@cp $(MEMORY_SMOKE_ELF) $(EXT2_STAGING_DIR)/exos/apps/memory-fragmentation
	@cp $(MEMORY_SMOKE_ELF) $(EXT2_STAGING_DIR)/exos/apps/memory-growth
	@cp $(MASTER_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/master
	@cp $(SLAVE_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/slave
	@cp $(MODULE_HOST_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/module-host
	@cp $(MODULE_SAMPLE_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/module-sample
	@cp $(MODULE_IMPORT_ELF) $(EXT2_STAGING_DIR)/exos/apps/test/module-import
	@cp $(SYSTEM_TEST_EPK) $(EXT2_STAGING_DIR)/exos/apps/test.epk
	@for f in $(SYSTEM_SCRIPT_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $(EXT2_STAGING_DIR)/exos/scripts/$$(basename $$f)"; \
			cp "$$f" "$(EXT2_STAGING_DIR)/exos/scripts/$$(basename $$f)"; \
		fi; \
	done
	@echo "Creating UEFI disk image ($(IMG_SIZE_MB)MiB)..."
	@dd if=/dev/zero of=$(UEFI_IMAGE) bs=1M count=$(IMG_SIZE_MB)
	@parted -s $(UEFI_IMAGE) mklabel gpt
	@parted -s $(UEFI_IMAGE) mkpart ESP fat32 1MiB $(ESP_SIZE_MB)MiB
	@parted -s $(UEFI_IMAGE) mkpart EXOS ext2 $(ESP_SIZE_MB)MiB 100%
	@parted -s $(UEFI_IMAGE) set 1 esp on
	@{ \
		PART1_OFFSET=$$(parted -s $(UEFI_IMAGE) unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		PART2_OFFSET=$$(parted -s $(UEFI_IMAGE) unit B print | awk '/^ 2/ { gsub("B","",$$2); print $$2; exit }'); \
		if [ -z "$$PART1_OFFSET" ] || [ "$$PART1_OFFSET" = "0" ]; then \
			echo "ERROR: ESP partition not found, or PART1_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		if [ -z "$$PART2_OFFSET" ] || [ "$$PART2_OFFSET" = "0" ]; then \
			echo "ERROR: EXT2 partition not found, or PART2_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		MTOOLS_SKIP_CHECK=1 mformat -i $(UEFI_IMAGE)@@$$PART1_OFFSET -v EXOS ::; \
		MTOOLS_SKIP_CHECK=1 mcopy -i $(UEFI_IMAGE)@@$$PART1_OFFSET -s $(UEFI_STAGING_DIR)/* ::/; \
		echo "Populating EXT2 filesystem at offset $$PART2_OFFSET from $(EXT2_STAGING_DIR)"; \
		mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -d $(EXT2_STAGING_DIR) -E offset=$$PART2_OFFSET $(UEFI_IMAGE); \
	}

clean:
	@echo "[ Cleaning UEFI build ]"
	@rm -rf $(MTOOLS_CONF) $(UEFI_BUILD_DIR) $(UEFI_IMAGE) $(USB3_IMG) $(FLOPPY_IMG) $(FS_TEST_EXT2_IMG) $(FS_TEST_FAT32_IMG) $(FS_TEST_NTFS_IMG)
