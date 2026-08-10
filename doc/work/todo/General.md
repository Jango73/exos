# General TODO list

## High priority

- [x] Fix build :
  - when building UEFI, MBR Makefile MUST NOT be called
  - when building MBR, UEFI Makefile MUST NOT be called
  - make boot-mbr and boot-uefi makefiles as close as possible regarding structure (functions, names, ...), they diverge too much
  - make boot-mbr and boot-uefi makefiles create the same auxilliary images (usb-3, floppy, test, ...)
  - images must be placed in build/image/x86-.../ instead of build/image/x86-.../boot-mbr or build/image/x86-.../boot-uefi (the "x86-..." path component already contains the boot type)
  - add ability NOT TO build disk images (--no-images)

- [x] User.h / Base.h MUST NOT contain Kernel function definitions : they are files visible by userland. Userland CANNOT call kernel functions directly.
  - Userland uses functions defined in exos.h and implemented in exos-runtime-c.c
  - Kernel must place those function prototypes in a different header

- [x] Scripting : fix parentheses parsing to use ScriptParseComparisonAST instead of ScriptParseExpressionAST in ScriptParseFactorAST
- [x] Scripting : add support for unary operators (-x, +x) in ScriptParseFactorAST

- [x] When running an embedded script, one must return the return value of the script, not always DF_RETURN_SUCCESS

- [x] Homogenize naming in exposed objects : do not use plural for lists (ex: usb.ports = usb.port, usb.devices = usb.device, etc...)
- [x] Homogenize the output of all listing scripts in Shell-EmbeddedScripts.c. use "nnn, field1=value1, field2=value2, field3=value3, ..."

- [x] Execute E0-Objects.md : all remaining steps

- [x] Execute Shell-Scripting-Exposure-Plan.md : all remaining steps
- [x] Rewrite ShellScriptCallFunction using a function table.

- [x] Rename `VMA_LIBRARY`: it no longer designates the module/library boundary. Re-anchor the naming to its real architectural role and increase process arena reserves significantly, especially for the module arena.

- [x] Make x86-64 high user arenas real: install a high user paging window below `VMA_USER_LIMIT`, allocate missing parent paging structures on demand, and size module/stack arenas in GiB instead of fitting them into the single `TaskRunner` GiB.

- [x] Implement Executable-Module-Libraries.md

- [x] Rename UserAccountList -> AccountList

- [x] Make all autotests pass

- [x] Execute Syscall-Return-ABI.md

- [x] Enrich the module loading smoke test

- [x] Implement stdin/stdout/stderr

- [x] Renaming
  - [x] Rename ExecutableELF-Metadata.* -> Executable-ELF-Metadata.*
  - [x] Rename ExecutableELF-Private.* -> Executable-ELF-Private.*
  - [x] Rename ExecutableELF.* -> Executable-ELF.*
  - [x] Rename ExecutableEXOS.* -> Executable-EXOS.*
  - [x] Rename ExecutableModule-Relocation.* -> Executable-Module-Relocation.*
  - [x] Rename ExecutableModule.* -> Executable-Module.*
  - [x] Rename FileSystem.* -> File-System.*
  - [x] Rename BuddyAllocator.* -> Buddy-Allocator.*
  - [x] Rename EpkParser.* -> Epk-Parser.*
  - [x] Rename Shell-EmbeddedScripts.* -> Shell-Embedded-Scripts.*
  - [x] Rename DeferredWork.* -> Deferred-Work.*
  - [x] Rename DeferredWorkQueue.* -> Deferred-Work-Queue.*

- [x] Clean up runtime folder
  - [x] Move stdlib & posix headers from runtime/include/ to runtime/include/stdlib/
  - [x] Move exos headers from runtime/include/ to runtime/include/exos/
  - [x] Rename exos-runtime.h -> exos-runtime-main.h
  - [x] Rename exos-string.h -> exos-runtime-string.h
  - [x] Rename http.h -> exos-runtime-http.h
  - [x] Rename exos-runtime-c.c -> exos-runtime-main.c
  - [x] Rename exos-string.c -> exos-runtime-string.c
  - [x] Rename http.c -> exos-runtime-http.c
  - [x] Create exos-window.c and move windowing functions from exos.c to exos-window.c

- [x] Remove UI components from kernel and cleanup
  - Remove kernel/source/ui/* and kernel/include/ui/* , and use the files to replace the contents of portal.

- [x] Clean up kernel (no kernel module should include "exos.h") :
  - [x] Rename KernelPeekMessage -> PeekMessage
  - [x] Rename KernelGetMessage -> GetMessage
  - [x] Rename KernelDispatchMessage -> DispatchMessage
  - [x] Add HeapAlloc and HeapFree in exos runtime using SYSCALL_HeapAlloc and SYSCALL_HeapFree
  - [x] There must be no conflict between functions in kernel and functions in runtime

- [x] Execute iGPU.md : Step 11
- [ ] Execute Universal-Serial-Bus.md : all remaining steps
- [ ] Execute Non-Volatile-Memory-Express.md : all remaining steps
- [ ] Execute Packaging-System-Plan.md : all remaining steps
- [ ] Execute Network.md : all remaining steps

- [ ] Implement full UTF and Unicode.md
- [ ] Handle languages

- [x] Fix input-info #PF on exit
  - Not reproducible under Predator -> pop_os -> QEMU. Might be happening elsewhere.

- [x] Keyboard : Handle '<' key in french keyboard mapping.

- [x] Scripting memory : AST cache with execution-stack-based eviction

- [x] Intermittent `[ReservedHeapInit] Failed to commit initial region for ShellHeap`
  - Symptom: during boot, `ReservedHeapInit()` fails to commit the initial ShellHeap region, so `InitShellContext()` falls back to the process heap (`Reserved shell heap unavailable, using process heap`). The smoke test then reports a kernel fatal error and fails.
  - Fix: `IsRegionFree()` now treats any non-zero page-table entry as occupying the region, on x86-64 (`ReadPageTableEntryValue() != 0`) and x86-32 (raw PTE != 0). `FreeRegionForProcess()` now clears reserved (non-present non-zero) entries on both architectures so freed regions become reusable.

- [x] Rewrite the `memoryMap` shell command to report on the whole memory carving, for ALL processes:
  - Extend it to cover all memory: process address space arenas, page directories, memory regions, stacks, heaps, for every process.
  - Verify the memory is carved out healthily from the largest structures (process address space arenas, memory region descriptors) down to the stacks.
  - Detect overlapping zones and layout inconsistencies and list every overlap found.
  - Provide an option to assess heap fragmentation (must not be enabled by default; heaps are used at too fine a granularity by the different components).

- [x] TOML parsing allocations too many small objects and fragments heap.
  - `TomlParse()` uses a single-allocation two-pass parse: one block covers
    the `TOML` structure, the `TOML_ITEM` array and one contiguous string area,
    so 3 allocations per item become 1 allocation total, and `TomlFree()` is a
    single free.

- [x] Prevent `edit` editor flicker: every keypress triggers a full UI redraw, which visibly flickers. Redraw only the affected parts of the screen (the edited line, cursor, status bar) instead of the whole interface.
  - Fixed by selective redraw in 9915dac (`kernel/source/system/Edit-Main.c`): only the edited line, cursor and status bar are redrawn, and the title bar is kept stable on scroll.

- [x] Keyboard repeat delay and frequency are configurable via `Keyboard.RepeatDelayMS` (default 400 ms) and `Keyboard.RepeatIntervalMS` (default 50 ms) in the `[Keyboard]` config section; values are cached once in `Keyboard-Common.c` and drive `KeyboardRepeatPoll`.
  - Note: the previous item stated "200 ms" for the initial delay, but the code hardcodes 400 ms (`Keyboard-Common.c`).
  - Note: infinite repeat from a lost key-up (e.g. QEMU `usb-kbd` HID queue overflow on rapid scripted input) is an emulator limitation, not a kernel defect; a duration cap is intentionally NOT added because it would break intentional long key holds. The non-disruptive recovery path is the periodic GET_REPORT state-reconciliation item under Medium-priority Keyboard.

## Medium priority

### Shell

- [x] Fix IP address octet display reversed in dns, ping, SystemDataView and Expose-Network.
  The `Ntohl()` + MSB-first extraction pattern byte-swapped on little-endian x86,
  reversing the octets. Added `FormatIPv4()` to CoreString; all call sites now
  extract directly from the big-endian value.
  - `dns google.com` was showing "174.22.217.172" instead of "172.217.22.174".
- [x] Add two alternatives to commands in shell for the following (rest stays blank) :
  - changeFolder, cf, cd
  - listFolder, lf, dir, ls
  - makeFolder, mf, md, mkdir
  - `SHELL_COMMAND_ENTRY.AltName` now holds a space-separated alias list; the command dispatcher matches the primary name or any alias, and `commands` renders the whole alias list.
- [x] Add options to lf : sort by name, extension, modified date. limit output to n items.
  - `listFolder --sort=name|extension|modified` sorts entries; `--limit=n` caps output per folder to n items. Short forms: `-sn` (sort by name), `-se` (sort by extension), `-sm` (sort by modified), `-l<n>` (limit); `-s` is now the sort prefix, stress is `--stress` only.
  - Sorting snapshots each folder's entries then orders them with the reusable `utils/Sort` module; the default listing keeps the file system order.
- [ ] Make the shell syscall-clean: the shell will become a userland program, so no shell command may call a kernel function directly; every service access must go through a syscall (`DoSystemCall`/`exoscall`). Existing direct kernel calls in shell commands (for example `NetworkManager_*` device checks, socket layer calls) are known and will be migrated to their syscall equivalents progressively.

### Naming

- [x] Make all script exposed function camelCase instead of snake_case.
- [x] Add a comment for every member of every structure in Process.h.
- [x] Rename all script exposed object/member using camelCase.
- [x] Rename the following structures :
  - ATADISK -> ATA_DISK
  - ATADRIVEID -> ATA_DRIVE_ID
  - COFFHEADER -> COFF_HEADER
  - COFFRELOCATION -> COFF_RELOCATION
  - COFFSECTION -> COFF_SECTION
  - COFFSYMBOL -> COFF_SYMBOL
  - COMMANDLINEEDITOR -> COMMAND_LINE_EDITOR
  - CPUIDREGISTERS -> CPU_ID_REGISTERS
  - DISKACCESS -> DISK_ACCESS
  - DISKGEOMETRY -> DISK_GEOMETRY
  - DISKINFO -> DISK_INFO
  - DRIVERCAPS -> DRIVER_CAPS
  - E1000DEVICE -> E1000_DEVICE
  - EDITCONTEXT -> EDIT_CONTEXT
  - EDITFILE -> EDIT_FILE
  - EDITLINE -> EDIT_LINE
  - EDITMENUITEM -> EDIT_MENU_ITEM
  - EXFSFILE -> EXFS_FILE
  - EXFSFILEREC -> EXFS_FILE_RECORD
  - EXFSFILESYSTEM -> EXFS_FILE_SYSTEM
  - EXFSMBR -> EXFS_MBR
  - EXFSSUPER -> EXFS_SUPER
  - EXFSTIME -> EXFS_TIME
  - EXOSCHUNK -> EXOS_CHUNK
  - EXOSCHUNK_FIXUP -> EXOS_CHUNK_FIXUP (skipped: name collides with the EXOS_CHUNK_FIXUP chunk-ID macro in Executable-EXOS.h)
  - EXOSCHUNK_INIT -> EXOS_CHUNK_INIT (skipped: name collides with the EXOS_CHUNK_INIT chunk-ID macro in Executable-EXOS.h)
  - EXOSHEADER -> EXOS_HEADER
  - EXT2BLOCKGROUP -> EXT2_BLOCK_GROUP
  - EXT2DIRECTORYENTRY -> EXT2_DIRECTORY_ENTRY
  - EXT2FILE -> EXT2_FILE
  - EXT2FILESYSTEM -> EXT2_FILE_SYSTEM
  - EXT2INODE -> EXT2_INODE
  - EXT2SUPER -> EXT2_SUPER
  - FAT16FILESYSTEM -> FAT16_FILE_SYSTEM
  - FAT16MBR -> FAT16_MBR
  - FAT32FILESYSTEM -> FAT32_FILE_SYSTEM
  - FAT32MBR -> FAT32_MBR
  - FATDIRENTRY -> FAT_DIR_ENTRY
  - FATDIRENTRY_EXT -> FAT_DIR_ENTRY_EXT
  - FATDIRENTRY_LFN -> FAT_DIR_ENTRY_LFN
  - FATFILE -> FAT_FILE
  - FATFILELOC -> FAT_FILE_LOCATION
  - FILESYSTEM_GLOBAL_INFO -> FILE_SYSTEM_GLOBAL_INFO
  - FILESYSTEM_MOUNT_CONTROL -> FILE_SYSTEM_MOUNT_CONTROL
  - FILESYSTEM_PATHCHECK -> FILE_SYSTEM_PATH_CHECK
  - GCSELECT -> GC_SELECT
  - GRAPHICSCONTEXT -> GRAPHICS_CONTEXT
  - IOAPIC_CONFIG -> IO_APIC_CONFIG
  - IOAPIC_CONTROLLER -> IO_APIC_CONTROLLER
  - IOAPIC_REDIRECTION_ENTRY -> IO_APIC_REDIRECTION_ENTRY
  - KEYBOARDSTRUCT -> KEYBOARD_STRUCT
  - KEYCODE -> KEY_CODE
  - KEYNAME -> KEY_NAME
  - KEYTRANS -> KEY_TRANS
  - MEMEDITCONTEXT -> MEM_EDIT_CONTEXT
  - MESSAGEQUEUE -> MESSAGE_QUEUE
  - MODEINFOBLOCK -> MODE_INFO_BLOCK
  - NTFS_FILERECORD -> NTFS_FILE_RECORD
  - NTFS_FILEREF -> NTFS_FILE_REF
  - NTFS_STDINFO -> NTFS_STANDARD_INFO
  - NTFSFILE -> NTFS_FILE
  - NTFSFILESYSTEM -> NTFS_FILE_SYSTEM
  - PACKAGEFS_NODE -> PACKAGE_FS_NODE
  - PACKAGEFSFILE -> PACKAGE_FS_FILE
  - PACKAGEFSFILESYSTEM -> PACKAGE_FS_FILESYSTEM
  - PACKAGENAMESPACE_PATHS -> PACKAGE_NAMESPACE_PATHS
  - PATHCOMPLETION -> PATH_COMPLETION
  - RAMDISK -> RAM_DISK
  - SECTORBUFFER -> SECTOR_BUFFER
  - SHELLCONTEXT -> SHELL_CONTEXT
  - SHELLINPUTSTATE -> SHELL_INPUT_STATE
  - STRINGARRAY -> STRING_ARRAY
  - STRINGBUILDER -> STRING_BUILDER
  - SYSTEMFSFILE -> SYSTEMFS_FILE
  - SYSTEMFSFILESYSTEM -> SYSTEMFS_FILE_SYSTEM
  - TASKLIST ->  TASK_LIST
  - TOMLITEM -> TOML_ITEM
  - VESAINFOBLOCK -> VESA_INFO_BLOCK
  - VGAMODEINFO -> VGA_MODE_INFO
  - VGAMODEREGS -> VGA_MODE_REGS
  - VIDEOMODESPECS -> VIDEO_MODE_SPECS
  - XFSFILELOC -> XFS_FILE_LOC

### Clock

- [x] Record boot date-time and expose time values to script in shell : boot datetime, current datetime
- [x] Make GetSystemTime return an incremented SystemUpTime value before the clock interrupt really ticks

### Logs

- [x] Use __func__ to automatically include function name

### Building

- [x] Implement Native-C-Compiler.md

### Smoke test

- [x] tcc-global-hello must actually print something: `system/tcc/samples/hello.c` only returns 0, so the smoke command `tcc ... -o /temp/tcc-global-hello` compiles and runs a binary that produces no output. Give the sample at least one print (for example via `printf`) so the compiled binary is exercised meaningfully.

### Multicore

- [ ] Implement Symmetric-Multiprocessing.md

### Keyboard

- [ ] Add more keyboard layouts : ja-JP, zh-CN, ko-KR, pl-PL, tr-TR, cs-CZ, hi-IN
  - [x] nl-NL (deploy/keyboard/nl-NL.ekm1)
  - [x] sv-SE (deploy/keyboard/sv-SE.ekm1)
  - [x] fi-FI (deploy/keyboard/fi-FI.ekm1)
  - [x] ru-RU (deploy/keyboard/ru-RU.ekm1)
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

### Tools

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
- [x] UEFI portal crash on teardown: the deterministic crash was a GCC stack-probe underflow in `LogViewerWindowFunc` (32KB local buffer, probe `sub $0x8000,%rsp` at user RIP `0x40618C`, CR2=`FFFFFFFFE0B4BD40` = SystemStack base `E0B4C000` minus `0x2C0`). The reactive grow (`GrowFaultingSystemStack`) could not allocate the extension because the ShellHeap reserved regions (#1 `E094C000..E0B4C000`, #2 `E0B4C000..E0D4C000`) butt directly against the SystemStack with zero free page below (`[GrowFaultingSystemStack] AllocRegion failed base=E0B47000 size=20480`), and the subsequent x86-64 kernel page-table walk faulted fatally. Fix: allocate a committed `STACK_GROW_MIN_INCREMENT` (16KB) margin below each SystemStack at creation (`Stack.AllocationBase`, region size `TASK_MINIMUM_SYSTEM_STACK_SIZE + STACK_GROW_MIN_INCREMENT`), so compiler probes land in mapped memory; `StackRelocateAndGrow`, `GrowCurrentStack` and `GrowFaultingSystemStack` now resize/free the full region through `AllocationBase`, and stack teardown uses `StackRelease`. `ResolveKernelPageFault` was hardened to use temporary paging slots instead of recursive windowing. Validated on x86-64 MBR, x86-64 UEFI and x86-32 MBR global smoke tests: portal runs with no fault and no grow trigger.

### System data view

- [x] Add following infos in PCI page (VendorID/DeviceID and Class/Subclass/ProgIF are already displayed):
  - Command / Status
  - BAR0..BAR5 (detect 32 vs 64-bit)
  - Capabilities Pointer + scan capabilities (MSI/MSI-X, PCIe)
  - Interrupt Line/Pin
  - Implemented in `SystemDataViewDrawPagePciList` (PCI page): Command/Status, BAR0..BAR5 with IO/M32/M64 detection, capability pointer plus full scanned capability list (named PM/AGP/VPD/MSI/PCI-X/HT/VendorSpecific/DebugPort/HotPlug/PCIe/MSI-X/SATA/AdvancedFeatures/EnhancedAllocation/FlatteningPortalBridge), and IRQ Line/Pin. Added reusable `PCI_ScanCapabilities` to the PCI module, refactored `PCI_FindCapability` on top of it, and extended `PCI_INFO`-like snapshot struct with the new registers. Clean debug builds pass on x86-32 and x86-64.

### Drivers

- [ ] Implement PCIe : Peripheral-Component-Interconnect-Express.md
- [ ] Implement VMD : Volume-Management-Device.md
- [ ] Implement NVidia
- [ ] Implement Radeon

### Session

- [ ] Lock session on inactivity in graphics display

### Shell

- [ ] Add an interactive confirmation before `kill(handle)` terminates a sensitive kernel task or process

### Scripting

### Console

- [ ] Implement Text-Terminal.md

### Graphics

- [ ] Implement Cursor-Bitmap-Architecture.md

### Filesystem cache

- [ ] Add a cluster-chain navigation cache to FAT16 and FAT32 (FAT sectors are already cached by the generic DiskTransferLayer sector cache)

### Network
- [ ] Create a NetworkHeapAlloc/Free and dedicated memory region for the network heap (AllocRegion).
- [ ] Optimize/evolve the network stack
- [ ] Verify downloaded file integrity: netget / HTTP_DownloadToFile only checks that the byte count matches Content-Length; no content checksum/hash is computed. The network smoke test compares only the file size (`file-size-compare`), not the bytes. Add a hash (for example CRC) exposed by the HTTP server and verified at the end of the download.

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
