RESERVED_SECTORS ?= 64
NTFS_PARTITION_TEMP_IMG ?= $(AUXILIARY_BUILD_DIR)/fs-test-ntfs-partition-$(ARCH).img

define CREATE_FAT32_IMAGE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $(IMG_SIZE_MB)MiB image with partition table and FAT32 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$(IMG_SIZE_MB); \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary fat32 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
			MTOOLS_SKIP_CHECK=1 mformat -i $1@@$$PART_OFFSET -v EXOS -F -R $(RESERVED_SECTORS) ::; \
		}; \
	fi
endef

define CREATE_EXT2_IMAGE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $(IMG_SIZE_MB)MiB image with partition table and EXT2 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$(IMG_SIZE_MB); \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary ext2 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
			mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -E offset=$$PART_OFFSET $1; \
		}; \
	fi
endef

define CREATE_EXT2_IMAGE_SIZE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $2MiB image with partition table and EXT2 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$2; \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary ext2 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
			mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -E offset=$$PART_OFFSET $1; \
		}; \
	fi
endef

define CREATE_FAT32_IMAGE_SIZE
	@if [ -f $1 ]; then \
		echo "Skipping creation of $1 (already exists)."; \
	else \
		echo "Creating $2MiB image with partition table and FAT32 partition..."; \
		dd if=/dev/zero of=$1 bs=1M count=$2; \
		parted -s $1 mklabel msdos; \
		parted -s $1 mkpart primary fat32 2048s 100%; \
		parted -s $1 set 1 boot on; \
		{ \
			PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
			if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
				echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
				exit 1; \
			fi; \
			MTOOLS_SKIP_CHECK=1 mformat -i $1@@$$PART_OFFSET -v EXOS -F -R $(RESERVED_SECTORS) ::; \
		}; \
	fi
endef

define POPULATE_EXT2_IMAGE
	@{ \
		PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		if [ -z "$$PART_OFFSET" ] || [ "$$PART_OFFSET" = "0" ]; then \
			echo "ERROR: Partition not found, or PART_OFFSET is zero. Aborting."; \
			exit 1; \
		fi; \
		echo "Populating EXT2 filesystem at offset $$PART_OFFSET from $2"; \
		mke2fs -F -t ext2 -b $(EXT2_BLOCK_SIZE) -q -L $(EXT2_LABEL) -d $2 -E offset=$$PART_OFFSET $1; \
	}
endef

define MTOOLS_OPERATION
	@rm -f $(MTOOLS_CONF)
	@{ \
		PART_OFFSET=$$(parted -s $1 unit B print | awk '/^ 1/ { gsub("B","",$$2); print $$2; exit }'); \
		echo "drive z: file=\"$1\" offset=$$PART_OFFSET" > $(MTOOLS_CONF); \
		echo "$2 at offset $$PART_OFFSET"; \
		$3; \
		rm -f $(MTOOLS_CONF); \
		echo "$4"; \
	}
endef

ifndef USB3_IMAGE_DEPS
USB3_IMAGE_DEPS = check_tools $(TICTACTOE_ELF)
endif

ifndef USB3_IMAGE_STAGING_COMMANDS
define USB3_IMAGE_STAGING_COMMANDS
	@echo "Preparing USB MSD staging directory at $(USB3_STAGING_DIR)"
	@rm -rf $(USB3_STAGING_DIR)
	@mkdir -p $(USB3_STAGING_DIR)
	@cp $(FS_TEST_READ_SOURCE) $(USB3_STAGING_DIR)/$(FS_TEST_READ_FILE)
	@cp $(TICTACTOE_ELF) $(USB3_STAGING_DIR)/tictactoe
	$(call POPULATE_EXT2_IMAGE,$(USB3_IMG),$(USB3_STAGING_DIR))
endef
endif

.PHONY: usb-3-image floppy-image

$(FS_TEST_EXT2_IMG): check_tools
	@mkdir -p $(dir $@)
	$(call CREATE_EXT2_IMAGE_SIZE,$@,$(FS_TEST_IMG_SIZE_MB))
	@parted -s $@ set 1 boot off
	@echo "Preparing EXT2 smoke-test staging directory at $(FS_TEST_EXT2_STAGING_DIR)"
	@rm -rf $(FS_TEST_EXT2_STAGING_DIR)
	@mkdir -p $(FS_TEST_EXT2_STAGING_DIR)
	@cp $(FS_TEST_READ_SOURCE) $(FS_TEST_EXT2_STAGING_DIR)/$(FS_TEST_READ_FILE)
	$(call POPULATE_EXT2_IMAGE,$(FS_TEST_EXT2_IMG),$(FS_TEST_EXT2_STAGING_DIR))

$(FS_TEST_FAT32_IMG): check_tools
	@mkdir -p $(dir $@)
	$(call CREATE_FAT32_IMAGE_SIZE,$@,$(FS_TEST_IMG_SIZE_MB))
	@parted -s $@ set 1 boot off
	$(call MTOOLS_OPERATION,$(FS_TEST_FAT32_IMG),Injecting FAT32 smoke test file,\
		MTOOLSRC=$(MTOOLS_CONF) mcopy -D o $(FS_TEST_READ_SOURCE) z:/$(FS_TEST_READ_FILE),\
		FAT32 smoke test file injected.)

$(FS_TEST_NTFS_IMG): check_tools
	@mkdir -p $(dir $@)
	@if [ -f $@ ]; then \
		echo "Skipping creation of $@ (already exists)."; \
	else \
		echo "Creating $(FS_TEST_IMG_SIZE_MB)MiB image with real NTFS partition..."; \
		dd if=/dev/zero of=$@ bs=1M count=$(FS_TEST_IMG_SIZE_MB); \
		parted -s $@ mklabel msdos; \
		parted -s $@ mkpart primary ntfs 2048s 100%; \
		parted -s $@ set 1 boot off; \
		if command -v mkfs.ntfs >/dev/null 2>&1; then \
			PART_START=$$(parted -ms $@ unit s print | awk -F: '$$1=="1"{gsub("s","",$$2); print $$2}'); \
			PART_END=$$(parted -ms $@ unit s print | awk -F: '$$1=="1"{gsub("s","",$$3); print $$3}'); \
			PART_SECTORS=$$((PART_END - PART_START + 1)); \
			rm -f "$(NTFS_PARTITION_TEMP_IMG)"; \
			dd if=/dev/zero of="$(NTFS_PARTITION_TEMP_IMG)" bs=512 count="$$PART_SECTORS"; \
			mkfs.ntfs -F -Q -L EXOS_NTFS "$(NTFS_PARTITION_TEMP_IMG)" >/dev/null; \
			ntfscp -q -f "$(NTFS_PARTITION_TEMP_IMG)" "$(FS_TEST_READ_SOURCE)" "/$(FS_TEST_READ_FILE)"; \
			dd if="$(NTFS_PARTITION_TEMP_IMG)" of=$@ bs=512 seek="$$PART_START" conv=notrunc; \
			rm -f "$(NTFS_PARTITION_TEMP_IMG)"; \
			echo "NTFS image created. Use scripts/linux/ntfs/populate-ntfs-image.sh to add folders/files."; \
		else \
			echo "WARNING: mkfs.ntfs not found, image keeps only NTFS partition type"; \
		fi; \
	fi

$(USB3_IMG): $(USB3_IMAGE_DEPS)
	@mkdir -p $(dir $@)
	$(call CREATE_EXT2_IMAGE,$@)
	@parted -s $@ set 1 boot off
	$(USB3_IMAGE_STAGING_COMMANDS)

usb-3-image: $(USB3_IMG)

$(FLOPPY_IMG): check_tools
	@mkdir -p $(dir $@)
	@if [ -f $@ ]; then \
		echo "Skipping creation of $@ (already exists)."; \
	else \
		echo "Creating 1.44MiB floppy image with FAT12..."; \
		dd if=/dev/zero of=$@ bs=512 count=2880; \
		mkfs.fat -F 12 -n EXOSFDD $@; \
	fi

floppy-image: $(FLOPPY_IMG)
