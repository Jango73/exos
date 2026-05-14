# AGENTS.md

This file provides guidance to agents when working with code in this repository.

## Project Overview
This is a multi-architecture operating system. Currently supporting x86-32 and x86-64.

## Main rule
**If the guidelines below are not followed, all modifications will be rejected.**

## Communication Guidelines
- NEVER use emojis in responses.
- DON'T act like a human being with emotions, just be a machine.
- DON'T says "Great!", "Perfect!", "You're right" all the time.
- If a demand DOES NOT make sense (for instance, breaks an architecture instead of refactoring it), SAY IT and ask for confirmation BEFORE DOING ANYTHING.
- NEVER create a commit unless the user explicitly asks for it in the current conversation.

## Architecture and Reuse Rules
- Bidirectional coupling is **STRICTLY FORBIDDEN**, both when writing code from scratch and when delivering a fix. Keep dependencies unidirectional and break cycles instead of introducing or preserving them.
- Any behavior likely to appear in multiple places (rate limit, retry, timeout policy, backoff, filtering, counters) MUST be implemented as a reusable module in `kernel/include/utils` + `kernel/source/utils`.
- Before adding local logic in a driver/subsystem, check `kernel/include/utils` and `kernel/source/utils` first. If no suitable module exists, create a generic one and use it from the caller.
- Driver code should only express policy/usage, not duplicate generic mechanics.
- For log-flood control, use the shared `RateLimiter` helper; do not hardcode ad-hoc counters/cooldowns inside modules.
- **Mutex ownership**: this rule applies to the whole kernel. Never lock another object's mutex directly to inspect or mutate its internal state. Expose owner-side getters/setters/snapshot helpers, keep critical sections short, and never recurse or call callbacks/messages while holding structural object locks.

## Coding Conventions
- **Types**: Use **LINEAR** for virtual addresses (when not using direct pointers), **PHYSICAL** for physical addresses, **UINT** for indexes, sizes and error values. In the kernel, it is **STRICTLY FORBIDDEN** to use a direct c type (int, unsigned long, long long, etc...) : **only types in Base.h are allowed.**
- **Freestanding**: The kernel **MUST NOT** rely on **ANY** external library/module (unless specified otherwise). **NO** stdlib, stdio, whatever. Everything the kernel needs is built in the compiler and in the codebase.
- **Debugging**: Debug output is logged with DEBUG(). Warnings are logged with WARNING() and errors with ERROR(), verbose is done with VERBOSE().
- **Userland debugging**: In runtime/system apps, use the existing `debug()` helper for instrumentation before adding ad-hoc logging paths. It sends the message to the kernel through a syscall, so userland debug traces show up in the standard kernel logs.
- **Userland failure visibility**: Any userland failure/error message shown on screen (console/UI) MUST also be emitted through `debug()` with equivalent content. No failure must appear only on screen without a corresponding kernel log entry.
- **Logging**: A log string **ALWAYS** begins with "[FunctionName]" where FunctionName is the name of the function where the logging is done. Use "%p" for pointers and addresses, "%x" for values except for sizes which use "%u". Do not hide errors by removing warnings. Reduce flood with rate limiting while preserving diagnostic signal (`suppressed` count or equivalent).
- **WARNING/ERROR semantics**: `WARNING()` and `ERROR()` are human-facing alerts. They MUST stay short, actionable, and understandable without deep protocol knowledge.
- **Diagnostic dumps**: Detailed protocol diagnostics (raw register dumps, TRB dumps, retry traces, queue internals, step-by-step instrumentation) MUST use `DEBUG()` only, never `VERBOSE()`, `WARNING()`, or `ERROR()`.
- **TEXT literals**: In kernel C code, every string literal passed to APIs/macros expecting `LPCSTR` (for example `DEBUG`, `WARNING`, `ERROR`, `VERBOSE`, `KernelLogText`, `ConsolePrint`, and ternary literal fallbacks) **MUST** be wrapped with `TEXT("...")`. Never pass raw `"..."` to those paths.
- **Declaration order**: Group declarations by type. 1: macros / 2: type definitions / 3: inline functions / 4: external functions / 5: other
- **Function order**: DO NOT OVERUSE forward declarations. Define functions before they are used.
- **I18n**: Write comments, console output and technical doc in english.
- **Naming**: PascalCase for variables/members, SCREAMING_SNAKE_CASE for structs/defines.
- **Naming clarity**: In addition to using full words, every name must express its intent clearly and without ambiguity.
- **Comments**: For single-line comments, use `//`, not `/*`.
- **Style**: 4-space indentation, follow `.clang-format` rules.
- **Numbers**: Hexadecimal for constant numbers, except for sizes, vectors, points and time.
- **Number suffixes**: Do not add numeric suffixes like `u` to constants; they are not wanted here.
- **Documentation**: Update `doc/guides/Kernel.md` when adding/modifying kernel components.
- **Scripting documentation**: Any change to the E0 scripting engine, syntax, semantics, operators, control flow, host exposure, or observable script behavior MUST update `doc/guides/E0-Scripting.md` in the same work.
- **Documentation wording**: Use timeless technical wording. Do not use temporal terms like "now", "currently", "at this time" in documentation/comments.
- **Kernel logical paths**: For kernel file/folder logical paths, use `utils/KernelPath` (`KernelPathResolve` / `KernelPathBuildFile`) and `KernelPath.*` config keys instead of hardcoded absolute paths.
- **Languages**: C for kernel, avoid Python (use Node.js/JS if needed).
- **Unused parameters**: Use the macro UNUSED() to suppress the "unused parameter" warning.
- **SAFE_USE macros**: These macros validate pointers in kernel space. In userland code (runtime/system apps), NEVER use SAFE_USE_VALID/SAFE_USE_VALID_ID variants, as they will reject userland addresses.
- **Pointers**: In the kernel, before using a kernel object pointer, use the appropriate macro for its validation
  - SAFE_USE if you got a pointer to any kind of object
  - SAFE_USE_VALID_ID if you got a pointer to a kernel object **which inherits LISTNODE_FIELDS**
  - SAFE_USE_2 does the same as SAFE_USE but for two pointers
  - SAFE_USE_VALID_ID_2 does the same as SAFE_USE_VALID_ID but for two pointers (SAFE_USE_VALID_ID_3 for 3 pointers, etc...).
- **Kernel objects**: Any kernel object that contains OBJECT_FIELDS (thus inherits LISTNODE_FIELDS) and is meant to exist in a global kernel list must be created with CreateKernelObject and destroyed with ReleaseKernelObject.
- **No direct access to physical memory**: Use the MapTemporaryPhysicalPage1 (MapTemporaryPhysicalPage2, etc...) and MapIOMemory/UnMapIOMemory functions to access physical memory pages.
- **Drivers**: In driver command dispatchers, any non-implemented function MUST return `DF_RETURN_NOT_IMPLEMENTED`.
- **Clean code**: No duplicate code. Create intermediate functions to avoid it. This also applies to data: create intermediate structures to avoid duplicating data.
- **No globals**: Before adding a global variable, **ALWAYS ASK** if permitted.
- **Functions**: Add a doxygen header to functions and separate all functions with a 75 character line such as : /************************************************************************/
- **File size**: Keep source files under 1000 lines; split by responsibility before crossing this limit.
- **Early boot timing**: `GetSystemTime` does not work in early boot until `EnableInterrupts` has been executed (timer ticks do not advance). For polling timeouts in early boot paths, always use `HasOperationTimedOut()` (Clock) so code keeps a loop-limit fallback and does not rely on time progression alone.
- **Kernel log tag filter**: `KernelLogTagFilter` is defined in `kernel/source/Log.c` and controlled through `KernelLogSetTagFilter()` / `KernelLogGetTagFilter()` (`kernel/include/Log.h`). Use it first when narrowing boot diagnostics instead of editing/removing log calls.
- **EXOS != Unix/Linux/Windows/Whatever** :
  - NEVER use abbreviations; ALWAYS use full words (acronyms are OK).
  - No "directory" : use "folder".
  - No "symlink" : use "folder alias".
  - No unix seconds / timestamp : use DATETIME structure.
  - No INT/UINT in persistent data, those are register-sized. use U8/I8, U16/I16, U32/I32, U64/I64, F32, F64, ...

## Common Build Commands

## Tool Execution Policy
- When running repository scripts that may require elevated permissions, always invoke them with the `bash scripts/...` form (example: `bash scripts/linux/test/smoke-test-global.sh`).
- Keep this invocation form consistent so persistent elevation approval can be reused on the same command prefix.
- NEVER run two `./scripts/linux/build/build` commands in parallel: this repository enforces a build lock and the second build will fail with \"A build is already running\". Always run build commands sequentially.
- If any header file (`*.h`) is modified, validation MUST use a `--clean` build.
- If the modified header is shared across architectures, or is a central kernel header, validation MUST run with `--clean` on both `x86-32` and `x86-64`.
- Incremental builds are forbidden as final validation after header changes.

**Build (ext2):**
```bash
./scripts/linux/build/build --arch x86-32 --fs ext2 --release
./scripts/linux/build/build --arch x86-32 --fs ext2 --debug
./scripts/linux/build/build --arch x86-32 --fs ext2 --debug --clean
```

**Build (fat32):**
```bash
./scripts/linux/build/build --arch x86-32 --fs fat32 --release
./scripts/linux/build/build --arch x86-32 --fs fat32 --debug
```

**Run in QEMU:**
```bash
./scripts/linux/run/run --arch x86-32
./scripts/linux/run/run --arch x86-32 --gdb
```

Replace `x86-32` with `x86-64` when targeting the x86-64 architecture.

**Automated build + smoke tests (dashboard-driven):**
```bash
./scripts/linux/test/smoke-test-global.sh
./scripts/linux/test/smoke-test-global.sh --only x86-32
./scripts/linux/test/smoke-test-global.sh --only x86-64
./scripts/linux/test/smoke-test-global.sh --only x86-64-uefi
```
This script runs build + boot + a list of commands and supports selecting a single target with `--only`.

**Build output layout:**
- Core artifacts are written to `build/core/<BUILD_CORE_NAME>/`.
- Image artifacts are written to `build/image/<BUILD_IMAGE_NAME>/`.
- `BUILD_CORE_NAME` format: `<arch>-<boot>-<config>[-split]`
  - example: `x86-32-mbr-debug`
  - example: `x86-64-uefi-release-split`
- `BUILD_IMAGE_NAME` format: `<BUILD_CORE_NAME>-<filesystem>`
  - example: `x86-32-mbr-debug-ext2`
  - example: `x86-64-uefi-release-fat32`
- Typical files:
  - kernel ELF: `build/core/<BUILD_CORE_NAME>/kernel/exos.elf`
  - MBR image: `build/image/<BUILD_IMAGE_NAME>/exos.img`
  - UEFI image: `build/image/<BUILD_IMAGE_NAME>/exos-uefi.img`

**Remote build on Windows (SSH to a Linux build host):**
```bat
scripts\windows\remote\i386\build-debug-ext2-ssh.bat
scripts\windows\remote\x86-64\build-debug-ext2-ssh.bat
```
Configure SSH and the remote repo root once in `scripts/windows/remote/ssh-config.bat`. The remote build runs in the same repository (same path, same branch/commit) as the Windows workspace (shared folder).

**Don't wait more than 15 seconds when testing interactively; this limit does not apply to repository scripts, where timeouts may be adjusted as needed. The system boots in less than 2 seconds and auto-run executable should finish under 15 seconds.**

## Debug output

Kernel debug output goes to `log/kernel-x86-32.log` and `kernel-x86-64.log`.
QEMU traces go to `qemu.log`.
Bochs output goes to `bochs.log`.
**Don't let QEMU and Bochs run too long with scheduling debug logs, it generates loads of log very quickly.**

## Documentation

Kernel design : `doc/guides/Kernel.md`
Doxygen documentation is in `doc/generated/kernel/*`

**Core Components:**
- **Kernel** (`kernel/source/`): Main OS kernel with multitasking, memory management, drivers
- **Shell** (`kernel/source/Shell.c`): Command-line interface
- **Boot** (`boot-mbr/` and `boot-uefi/`): Bootloader and disk image creation
- **Runtime** (`runtime/`): User-space runtime library, but included in the kernel to interface with 3rd party code
- **System** (`system/`): User-space system library, samples

## Debug Workflow
1. Use scheduling debug build when needing per-tick information, for scheduler or interrupt issues: `./scripts/linux/build/build --arch x86-32 --fs ext2 --scheduling-debug` (or add `--clean` for a clean make) and the `x86-64` equivalent: `./scripts/linux/build/build --arch x86-64 --fs ext2 --scheduling-debug`. GENERATES TONS OF LOG, USE WITH CARE.
2. Monitor `log/kernel-x86-32.log` and `kernel-x86-64.log` for exceptions and page faults
3. **To assert that the systems runs, the emulator must be running and there must be no fault in the logs, in all architectures**

### Reusable x86-64 debug launcher
Use `bash scripts/linux/x86-64/debug-vesa-int10.sh` as the default one-shot launcher for interactive x86-64 debug sessions requiring:
- QEMU start with gdb stub (`-s -S`) and monitor telnet
- deterministic keyboard layout patch in the image for monitor `sendkey`
- optional automatic shell command injection
- automatic gdb attach with configurable breakpoints

Default usage:
```bash
bash scripts/linux/x86-64/debug-vesa-int10.sh
```

Send a command automatically:
```bash
bash scripts/linux/x86-64/debug-vesa-int10.sh "gfx backend vesa 1024x768x16"
```

Send a command and validate it through a kernel log pattern:
```bash
bash scripts/linux/x86-64/debug-vesa-int10.sh "gfx backend vesa 1024x768x16" "[GraphicsSelectorForceBackendByName] Forced backend"
```

Key environment overrides:
- `GDB_BREAKPOINTS` (semicolon-separated, example: `SetVideoMode;RealModeCall`)
- `GDB_DISABLE_INDEXES` (space-separated gdb breakpoint indexes to disable after creation)
- `KEYBOARD_LAYOUT` (default: `en-US`)
- `MONITOR_PORT` and `GDB_PORT`

**Disassembly Analysis:**
- `./scripts/linux/utils/show-x86-32.sh <address> [context_lines]` (x86-32 build)
- `./scripts/linux/utils/show-x86-64.sh <address> [context_lines]` (x86-64 build)
  - Default context: 20 lines before/after target address
  - Target line marked with `>>> ... <<<`
  - Example: `./scripts/linux/utils/show-x86-64.sh 0xc0123456 5` (5 lines context)
