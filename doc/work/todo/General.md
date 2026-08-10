# General TODO list

## High priority

### Drivers

- [ ] Execute Universal-Serial-Bus.md : all remaining steps
- [ ] Execute Non-Volatile-Memory-Express.md : all remaining steps
- [ ] Execute Packaging-System-Plan.md : all remaining steps
- [ ] Execute Network.md : all remaining steps

### Multicore

- [ ] Implement Symmetric-Multiprocessing.md

### I18n

- [ ] Implement full UTF and Unicode.md
- [ ] Handle languages

### Disks

- [x] Reference disks by stable ID in the kernel, not by position index. Introduce a stable disk ID (unique per storage unit, independent of enumeration order and driver type) and use it as the primary reference in the kernel (storage objects, filesystem linkage). Scripts must be able to reference disks by ID (default method) and by index as a fallback.
  - The stable ID is derived from the hardware identity read at enumeration time, following the Linux `/dev/disk/by-id` model: ATA/SATA serial + model from IDENTIFY DEVICE (words 10-19, 27-46), NVMe serial + model from Identify Controller (serial bytes 4-19), USB vendor + product + serial from device descriptors; formatted as `vendor_model_serial`. Disks without hardware serial (RAMDisk) get a deterministic synthetic ID derived from the driver alias and the count of already-registered disks of the same driver type, so it stays stable across reboots.
  - DONE: kernel-side `DiskID` module (`kernel/source/utils/DiskID.c`) + identity capture in ATA, SATA (real AHCI IDENTIFY DEVICE), NVMe, USB and RAMDisk drivers; `id`/`vendor`/`model`/`serial` exposed to scripts via `storage[i].id` etc. Validated on x86-32 and x86-64 UEFI.
  - FOLLOW-UP: expose a script-side lookup by ID (the kernel has `DiskIdFindById` but no script-facing equivalent yet); `disk` command treats no-argument as `disk list`.
- [ ] Change partition naming (`GetDefaultFileSystemName`) so partition names embed the stable disk ID instead of the per-driver-type list index, keeping the partition index within the disk. This keeps volume names (used in VFS paths and `ActivePartitionName`) stable across disk reorder/removal.

### Network
- [ ] Create a NetworkHeapAlloc/Free and dedicated memory region for the network heap (AllocRegion).
- [ ] Optimize/evolve the network stack
- [ ] Verify downloaded file integrity: netget / HTTP_DownloadToFile only checks that the byte count matches Content-Length; no content checksum/hash is computed. The network smoke test compares only the file size (`file-size-compare`), not the bytes. Add a hash (for example CRC) exposed by the HTTP server and verified at the end of the download.

## Medium priority

### Shell

- [ ] Make the shell syscall-clean: the shell will become a userland program, so no shell command may call a kernel function directly; every service access must go through a syscall (`DoSystemCall`/`exoscall`). Existing direct kernel calls in shell commands (for example `NetworkManager_*` device checks, socket layer calls) are known and will be migrated to their syscall equivalents progressively.

### Keyboard

- [ ] Add more keyboard layouts : ja-JP, zh-CN, ko-KR, pl-PL, tr-TR, cs-CZ, hi-IN
- [ ] Add periodic keyboard state reconciliation to recover from lost key-up events.
  - Symptom: under rapid input (e.g. scripted `sendkey` bursts faster than the guest's USB poll drains the device queue), QEMU's `usb-kbd` HID event queue silently drops key-up events (a known QEMU limitation, see AGENTS.md). A dropped key-up leaves `Keyboard.UsageStatus[Usage] == 1` and `Keyboard.RepeatUsage == Usage`; `KeyboardRepeatPoll` (`Keyboard-Common.c`) then re-fires that key's key-down every 50 ms forever, so one character repeats infinitely until another key is pressed. The kernel's char buffer (`MAXKEYBUFFER`) and process message queue (capacity 100) are NOT the cause — no "Queue full" warnings are emitted, because the loss happens above the kernel, in the emulator.
  - Requirement: the fix must NOT break intentional long key holds (a user may legitimately hold a key to repeat). A blind duration cap would falsely terminate genuine holds, so it is rejected.
  - Chosen approach: a periodic GET_REPORT reconciliation that queries the device's actual current key state and clears `UsageStatus`/`RepeatUsage` entries the device no longer reports as pressed. This self-heals lost key-ups (device reports no key down while the kernel thinks one is) without affecting genuinely held keys (device reports the held key down, so the kernel state stays consistent).
  - Scope: implement for the USB HID boot-protocol path in `Keyboard-USB.c` (issue `GetReport` on the interrupt IN endpoint / HID control pipe) and, where the bus supports it, the PS/2 path in `Keyboard-PS2.c`; share the reconciliation sweep over `UsageStatus` from `Keyboard-Common.c`. Gate behind a config knob (`Keyboard.StuckKeyRecovery` / `Keyboard.ReconciliationInterval`) so it can be disabled for hosts where GET_REPORT is unsupported.

## Low priority

### Scripting

- [ ] Make E0 scripting independant and embed it as an external library in third/

### Core

- [ ] Split Kernel.c : put kernel object management functions in dedicated file

### Files

- [ ] Introduce an explicit API split between file and folder opening:
  - `DF_FS_OPENFILE` for regular file handles only
  - `DF_FS_OPENFOLDER` for folder handles and folder enumeration only
  - Keep one shared internal resolution/validation core per driver to avoid code duplication.
  - Use thin command-specific entry points on top of the shared core:
    - `DF_FS_OPENFILE` path enforces "regular file only"
    - `DF_FS_OPENFOLDER` path enforces "folder only"
  - Reject misuse explicitly:
    - opening a folder through `DF_FS_OPENFILE` must fail
    - opening a file through `DF_FS_OPENFOLDER` must fail
  - Update common kernel file routing and shell tooling to call the right command based on intent.
  - Add regression tests per driver to validate behavior parity after the split.
- [ ] Add a trash system per volume.
- [ ] Opening a file in a userland program without an absolute path should do the same as using getcwd().
- [ ] Add a getpd() that returns the folder in which the current executable's image lives.
- [ ] FileReadAll() : use HeapAlloc, NOT KernelHeapAlloc

### Memory

- [ ] Align x86-32 page directory creation (`AllocPageDirectory` and `AllocUserPageDirectory`) with the modular x86-64 region-based approach (low region, kernel region, task runner, recursive slot) while preserving current behavior. Execute this refactor in small validated steps to limit boot and paging regression risk.
- [ ] Improve the memory stress tests: the existing `memory-stress` app did not expose the region allocator race fixed in the x86-64 allocator (concurrent region carve-outs between tasks overlapping one another and corrupting descriptor slabs). Add a stress mode that runs concurrent allocation/release loops from several tasks, mixing large multi-page reservations with single-page allocations, so allocator races and descriptor/PTE inconsistencies surface deterministically.
- [ ] Region descriptor tracking is tied to `GetCurrentProcess()` instead of the actual region owner, which yields `[UpdateDescriptorsForFree] Missing descriptor` warnings during task/process teardown : rework alloc/free tracking so descriptors are registered and removed against the owning process/kernel address space, not the current execution context.

### Drivers

- [ ] Implement PCIe : Peripheral-Component-Interconnect-Express.md
- [ ] Implement VMD : Volume-Management-Device.md
- [ ] Implement NVidia
- [ ] Implement Radeon

### Session

- [ ] Lock session on inactivity in graphics display

### Shell

- [ ] Add an interactive confirmation before `kill(handle)` terminates a sensitive kernel task or process

### Console

- [ ] Implement Text-Terminal.md

### Graphics

- [ ] Implement Cursor-Bitmap-Architecture.md

### Filesystem cache

- [ ] Add a cluster-chain navigation cache to FAT16 and FAT32 (FAT sectors are already cached by the generic DiskTransferLayer sector cache)

### Security 

- [ ] NX/DEP : Prevents execution in non-executable memory regions (stack/heap), blocking classic injected shellcode attacks.
- [ ] PIE/ASLR userland : Makes userland binaries position-independent and randomizes memory layout to hinder return-oriented and memory-guessing attacks.
- [ ] Stack canaries : Places sentinel values before return addresses to detect and stop stack buffer overflows before control hijack.
- [ ] RELRO : Marks relocation tables read-only to stop attackers from modifying GOT/PLT entries at runtime.
- [ ] Signed kernel modules + Secure Boot : Allows only cryptographically signed kernel modules and verifies the boot chain to prevent unauthorized code from loading.
- [ ] KASLR : Randomizes the kernel's memory base to make kernel address offsets unpredictable for exploitation.
- [ ] Audit/fuzz pipeline + ASAN/UBSAN : Continuous auditing and fuzzing with sanitizers to catch memory errors and undefined behavior during development.

### File systems

- [ ] Implement ext3 and ext4
- [ ] Load exos.bin from the EXT2 system partition instead of the ESP in UEFI

### Other

- [ ] Implement x86-Disassembly.md
- [ ] Implement Floppy-Drive.md
