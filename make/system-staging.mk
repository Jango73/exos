SYSTEM_STAGING_DEPENDENCIES = $(KERNEL_BIN) $(PORTAL_ELF) $(NETGET_ELF) $(HELLO_ELF) $(TICTACTOE_ELF) $(TERMTACTICS_ELF) $(INPUTINFO_ELF) $(MEMORY_SMOKE_ELF) $(MASTER_ELF) $(SLAVE_ELF) $(MODULE_HOST_ELF) $(MODULE_SAMPLE_ELF) $(MODULE_IMPORT_ELF) $(SYSTEM_TEST_EPK) $(SYSTEM_SCRIPT_FILES) $(TCC_SAMPLE_FILES) $(TCC_INCLUDE_FILES) $(RUNTIME_INCLUDE_FILES) $(RUNTIME_INCLUDE_SYS_FILES) $(TCC_CRT1_OBJ) $(TCC_CRTI_OBJ) $(TCC_CRTN_OBJ) $(TCC_LIBC_A) $(TCC_LIBC_MIT_LICENSE)

define STAGE_SYSTEM_EXT2_TREE
	@echo "Preparing EXT2 staging directory at $1"
	@rm -rf $1
	@mkdir -p $1/exos/data
	@mkdir -p $1/exos/users
	@mkdir -p $1/exos/apps
	@mkdir -p $1/exos/apps/test
	@mkdir -p $1/exos/c/include
	@mkdir -p $1/exos/c/include/sys
	@mkdir -p $1/exos/keyboard
	@mkdir -p $1/exos/scripts
	@mkdir -p $1/exos/apps/tcc
	@mkdir -p $1/exos/apps/tcc/include
	@mkdir -p $1/exos/apps/tcc/samples
	@mkdir -p $1/exos/temp
	@cp $(KERNEL_BIN) $1/exos.bin
	@cp $(KERNEL_CFG_EXT2) $1/$(CONFIG_TOML_NAME)
	@for f in $(KEYBOARD_LAYOUTS); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/keyboard/$$(basename $$f)"; \
			cp "$$f" "$1/exos/keyboard/$$(basename $$f)"; \
		fi; \
	done
	@cp $(PORTAL_ELF) $1/exos/apps/portal
	@cp $(NETGET_ELF) $1/exos/apps/netget
	@cp $(HELLO_ELF) $1/exos/apps/hello
	@cp $(TICTACTOE_ELF) $1/exos/apps/tictactoe
	@cp $(TERMTACTICS_ELF) $1/exos/apps/terminal-tactics
	@cp $(INPUTINFO_ELF) $1/exos/apps/input-info
	@cp $(MEMORY_SMOKE_ELF) $1/exos/apps/memory-stress
	@cp $(MEMORY_SMOKE_ELF) $1/exos/apps/memory-fragmentation
	@cp $(MEMORY_SMOKE_ELF) $1/exos/apps/memory-growth
	@if [ -f "$(TCC_ELF)" ]; then cp $(TCC_ELF) $1/exos/apps/tcc/tcc; fi
	@cp $(MASTER_ELF) $1/exos/apps/test/master
	@cp $(SLAVE_ELF) $1/exos/apps/test/slave
	@cp $(MODULE_HOST_ELF) $1/exos/apps/test/module-host
	@cp $(MODULE_SAMPLE_ELF) $1/exos/apps/test/module-sample
	@cp $(MODULE_IMPORT_ELF) $1/exos/apps/test/module-import
	@cp $(SYSTEM_TEST_EPK) $1/exos/apps/test.epk
	@for f in $(SYSTEM_SCRIPT_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/scripts/$$(basename $$f)"; \
			cp "$$f" "$1/exos/scripts/$$(basename $$f)"; \
		fi; \
	done
	@for f in $(TCC_SAMPLE_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/apps/tcc/samples/$$(basename $$f)"; \
			cp "$$f" "$1/exos/apps/tcc/samples/$$(basename $$f)"; \
		fi; \
	done
	@for f in $(TCC_INCLUDE_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/apps/tcc/include/$$(basename $$f)"; \
			cp "$$f" "$1/exos/apps/tcc/include/$$(basename $$f)"; \
		fi; \
	done
	@for f in $(RUNTIME_INCLUDE_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/c/include/$$(basename $$f)"; \
			cp "$$f" "$1/exos/c/include/$$(basename $$f)"; \
		fi; \
	done
	@for f in $(RUNTIME_INCLUDE_SYS_FILES); do \
		if [ -f "$$f" ]; then \
			echo "Copying $$f -> $1/exos/c/include/sys/$$(basename $$f)"; \
			cp "$$f" "$1/exos/c/include/sys/$$(basename $$f)"; \
		fi; \
	done
	@if [ -f "$(TCC_CRT1_OBJ)" ]; then cp $(TCC_CRT1_OBJ) $1/exos/apps/tcc/crt1.o; fi
	@if [ -f "$(TCC_CRTI_OBJ)" ]; then cp $(TCC_CRTI_OBJ) $1/exos/apps/tcc/crti.o; fi
	@if [ -f "$(TCC_CRTN_OBJ)" ]; then cp $(TCC_CRTN_OBJ) $1/exos/apps/tcc/crtn.o; fi
	@if [ -f "$(TCC_LIBC_A)" ]; then cp $(TCC_LIBC_A) $1/exos/apps/tcc/libc.a; fi
	@if [ -f "$(TCC_LIBC_MIT_LICENSE)" ]; then cp $(TCC_LIBC_MIT_LICENSE) $1/exos/apps/tcc/LICENSE; fi
endef

define WRITE_SYSTEM_STAGING_MANIFEST
	@mkdir -p $(dir $2)
	@find $1 -type f -printf '%P\n' | LC_ALL=C sort > $2
	@echo "Wrote system staging manifest: $2"
endef

define VERIFY_COUNTERPART_SYSTEM_STAGING
	@if [ -f "$2" ]; then \
		echo "Comparing system staging manifests:"; \
		echo "  current: $1"; \
		echo "  other:   $2"; \
		if ! diff -u "$2" "$1"; then \
			echo "ERROR: system staging diverges between MBR and UEFI."; \
			exit 1; \
		fi; \
	else \
		echo "Counterpart staging manifest not found ($2), skipping cross-boot comparison."; \
	fi
endef
