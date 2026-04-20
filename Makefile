include make/arch.mk

USE_SYSCALL ?= 0
PROFILING ?= 0
BUILD_CONFIGURATION ?= release
BUILD_CORE_NAME ?=
BUILD_IMAGE_NAME ?=
BOOT_MODE ?= mbr
BUILD_IMAGES ?= 1

SUBMAKE = $(MAKE) ARCH=$(ARCH) VMA_KERNEL=$(VMA_KERNEL) USE_SYSCALL=$(USE_SYSCALL) PROFILING=$(PROFILING) BUILD_CONFIGURATION=$(BUILD_CONFIGURATION) BUILD_CORE_NAME=$(BUILD_CORE_NAME) BUILD_IMAGE_NAME=$(BUILD_IMAGE_NAME) BOOT_MODE=$(BOOT_MODE) BUILD_IMAGES=$(BUILD_IMAGES)

.PHONY: all kernel runtime system boot-qemu boot-mbr boot-uefi tools clean

all: log kernel runtime system tools
ifeq ($(BUILD_IMAGES),1)
ifeq ($(BOOT_MODE),uefi)
all: boot-uefi
else
all: boot-mbr
endif
endif

log:
	@mkdir -p log

kernel:
	@echo "[ Building kernel ]"
	@+$(SUBMAKE) -C kernel

runtime:
	@echo "[ Building runtime ]"
	@+$(SUBMAKE) -C runtime

system: runtime
	@echo "[ Building system programs ]"
	@+$(SUBMAKE) -C system

boot-mbr: kernel system
	@echo "[ Building Qemu HD image ]"
	@+$(SUBMAKE) -C boot-mbr

boot-uefi: kernel system
	@echo "[ Building UEFI image ]"
	@+$(SUBMAKE) -C boot-uefi

tools:
	@echo "[ Building tools ]"
	@+$(SUBMAKE) -C tools

clean:
	@echo "[ Cleaning all ]"
	@+$(SUBMAKE) -C kernel clean
	@+$(SUBMAKE) -C runtime clean
	@+$(SUBMAKE) -C system clean
ifeq ($(BUILD_IMAGES),1)
ifeq ($(BOOT_MODE),uefi)
	@+$(SUBMAKE) -C boot-uefi clean
else
	@+$(SUBMAKE) -C boot-mbr clean
endif
endif
	@+$(SUBMAKE) -C tools clean
