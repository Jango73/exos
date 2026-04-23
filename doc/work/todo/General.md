# General TODO list

## High priority

- [x] Fix build :
  - when building UEFI, MBR Makefile MUST NOT be called
  - when building MBR, UEFI Makefile MUST NOT be called
  - make boot-mbr and boot-uefi makefiles as close as possible regarding structure (functions, names, ...), they diverge too much
  - make boot-mbr and boot-uefi makefiles create the same auxilliary images (usb-3, floppy, test, ...)
  - images must be placed in build/image/x86-.../ instead of build/image/x86-.../boot-mbr or build/image/x86-.../boot-uefi (the "x86-..." path component already contains the boot type)
  - add ability NOT TO build disk images (--no-images)

- [x] User.h MUST NOT contain Kernel function definitions : it is a file visible by userland. Userland CANNOT call kernel functions directly.
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

- [ ] Implement stdin/stdout/stderr

- [ ] Execute Universal-Serial-Bus.md : all remaining steps
- [ ] Execute Packaging-System-Plan.md : all remaining steps
- [ ] Execute Network.md : all remaining steps
- [ ] Execute iGPU.md : Step 11

- [ ] Implement full UTF and Unicode.md
- [ ] Handle languages

- [ ] Fix input-info #PF on exit
- [ ] Keyboard : Handle '<' key in french keyboard mapping.

## Medium priority

### Naming

- [x] Make all script exposed function camelCase instead of snake_case.
- [x] Add a comment for every member of every structure in Process.h.
- [x] Rename all script exposed object/member using camelCase.

### Clock

- [x] Record boot date-time and expose time values to script in shell : boot datetime, current datetime
- [x] Make GetSystemTime return an incremented SystemUpTime value before the clock interrupt really ticks

### Logs

- [x] Use __func__ to automatically include function name

### Building

- [x] Implement Native-C-Compiler.md

### Scheduling

- [ ] Improve the scheduler (task priorities)
- [ ] A CPU-bound task that never blocks can starve lower-priority deferred work and input handling (e.g., System Data View loop). Preference: force yield when a task is too CPU-hungry.

### Multicore

- [ ] Implement Symmetric-Multiprocessing.md

### Keyboard

- [ ] Add more keyboard layouts : ja-JP, zh-CN, ko-KR, nl-NL, sv-SE, fi-FI, pl-PL, tr-TR, cs-CZ, ru-RU, hi-IN

### Shell

- [ ] Add options to the command dir : sort by name, extension, modified date. limit output to n items.

## Low priority

### Scripting

- [ ] Make E0 scripting independant and embed it as an external library in third/

### Core

- [ ] Split Kernel.c : put kernel object magangement functions in dedicated file

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
- [ ] Implement a memory sanity checker that scans memory to check how fragmented memory is.
- [ ] TOML parsing allocations too many small objects and fragments heap.
- [ ] Region descriptor tracking is tied to `GetCurrentProcess()` instead of the actual region owner, which yields `[UpdateDescriptorsForFree] Missing descriptor` warnings during task/process teardown : rework alloc/free tracking so descriptors are registered and removed against the owning process/kernel address space, not the current execution context.

### System data view

- [ ] Add following infos in PCI page:
  - VendorID/DeviceID
  - Command / Status
  - Class/Subclass/ProgIF
  - BAR0..BAR5 (detect 32 vs 64-bit)
  - Capabilities Pointer + scan capabilities (MSI/MSI-X, PCIe)
  - Interrupt Line/Pin

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

- [ ] Create a generic fixed-size cache
- [ ] Add a cluster cache to FAT16 and FAT32

### Network
- [ ] Create a NetworkHeapAlloc/Free and dedicated memory region for the network heap (AllocRegion).
- [ ] Optimize/evolve the network stack

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
