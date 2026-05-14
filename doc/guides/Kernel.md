# EXOS Kernel documentation

## Table of contents
- [Table of contents](#table-of-contents)
- [Conventions](#conventions)
  - [Notations used in this document](#notations-used-in-this-document)
  - [Naming](#naming)
- [Platform and Memory Foundations](#platform-and-memory-foundations)
  - [Startup sequence on HD (real HD on x86-32 or qemu-system-i386)](#startup-sequence-on-hd-real-hd-on-x86-32-or-qemu-system-i386)
  - [Startup sequence on UEFI](#startup-sequence-on-uefi)
  - [Physical Memory map (may change)](#physical-memory-map-may-change)
  - [Paging abstractions](#paging-abstractions)
- [Isolation and Kernel Core](#isolation-and-kernel-core)
  - [Security Architecture](#security-architecture)
  - [Kernel objects](#kernel-objects)
  - [Handle reuse](#handle-reuse)
- [Execution Model and Kernel Interface](#execution-model-and-kernel-interface)
  - [Tasks](#tasks)
  - [Process and Task Lifecycle Management](#process-and-task-lifecycle-management)
    - [Reserved module heaps](#reserved-module-heaps)
  - [System calls](#system-calls)
  - [Task and window message delivery](#task-and-window-message-delivery)
  - [Command line editing](#command-line-editing)
  - [Exposed objects in shell](#exposed-objects-in-shell)
- [Hardware and Driver Stack](#hardware-and-driver-stack)
  - [Driver architecture](#driver-architecture)
  - [Input device stack](#input-device-stack)
  - [USB host and class stack](#usb-host-and-class-stack)
  - [Console](#console)
  - [Graphics](#graphics)
  - [Early boot console](#early-boot-console)
  - [ACPI services](#acpi-services)
  - [Disk interfaces](#disk-interfaces)
- [Storage and Filesystems](#storage-and-filesystems)
  - [File systems](#file-systems)
    - [Mounted volume naming](#mounted-volume-naming)
    - [EPK package format](#epk-package-format)
    - [Package namespace integration](#package-namespace-integration)
  - [EXOS File System - EXFS](#exos-file-system---exfs)
  - [Filesystem Cluster cache](#filesystem-cluster-cache)
  - [Foreign File systems](#foreign-file-systems)
- [Interaction and Networking](#interaction-and-networking)
  - [Shell scripting](#shell-scripting)
  - [Network Stack](#network-stack)
- [Windowing](#windowing)
  - [Desktop activation](#desktop-activation)
  - [Window model](#window-model)
  - [Rendering pipeline](#rendering-pipeline)
  - [Decoration modes](#decoration-modes)
  - [Input, capture, and timers](#input-capture-and-timers)
  - [Theme architecture](#theme-architecture)
  - [Theme levels](#theme-levels)
  - [Theme runtime API and fallback](#theme-runtime-api-and-fallback)
  - [Theme file example](#theme-file-example)
  - [Legacy compatibility](#legacy-compatibility)
  - [Synchronization rules](#synchronization-rules)
- [Tooling and References](#tooling-and-references)
  - [System Data View](#system-data-view)
  - [Logging](#logging)
  - [Automated debug validation script](#automated-debug-validation-script)
  - [Build output layout](#build-output-layout)
  - [Package tooling](#package-tooling)
  - [Keyboard Layout Format (EKM1)](#keyboard-layout-format-ekm1)
  - [QEMU network graph](#qemu-network-graph)
  - [Links](#links)
## Conventions

### Notations used in this document

| Abbrev | Meaning                         |
|--------|---------------------------------|
| U8     | unsigned byte                   |
| I8     | signed byte                     |
| U16    | unsigned word                   |
| I16    | signed word                     |
| U32    | unsigned long                   |
| I32    | signed long                     |
| U64    | unsigned qword                  |
| I64    | signed qword                    |
| UINT   | unsigned register-sized integer |
| INT    | signed register-sized integer   |

| Abbrev | Meaning                           |
|--------|-----------------------------------|
| EXOS   | Extensible Operating System       |
| BIOS   | Basic Input/Output System         |
| CHS    | Cylinder-Head-Sector              |
| MBR    | Master Boot Record                |
| OS     | Operating System                  |

---


### Naming

The following naming conventions have been adopted throughout the EXOS code base and interface.

- Structure : SCREAMING_SNAKE_CASE
- Macro : SCREAMING_SNAKE_CASE
- Function : PascalCase
- Variable : PascalCase
- Shell command : lower_snake_case
- Shell object/property : lowerCamelCase


## Platform and Memory Foundations

### Startup sequence on HD (real HD on x86-32 or qemu-system-i386)

Everything in this sequence runs in 16-bit real mode on x86-32 processors. However, the code uses 32 bit registers when appropriate.

1. BIOS loads disk MBR at 0x7C00.
2. Code in mbr.asm is executed.
3. MBR code looks for the active partition and loads its VBR at 0x7E00.
4. Code in vbr.asm is executed.
5. VBR code loads the reserved FAT32/EXT2 sectors (which contain VBR payload) at 0x8000.
6. Code in vbr-payload-a.asm is executed.
7. VBR payload asm sets up a stack and calls BootMain in vbr-payload-c.c.
8. BootMain finds the FAT32/EXT2 entry for the specified binary.
9. BootMain reads all the clusters of the binary at 0x20000.
10. EnterProtectedPagingAndJump sets up minimal GDT and paging structures for the loaded binary to execute in higher half memory (0xC0000000).
11. It finally jumps to the loaded binary.
12. That's all folks. But it was a real pain to code :D


### Startup sequence on UEFI

1. Firmware loads `EFI/BOOT/BOOTX64.EFI` (x86-64) or `EFI/BOOT/BOOTIA32.EFI` (x86-32) from the EFI System Partition (FAT32).
2. The UEFI loader reads `exos.bin` from the root folder of the EFI System Partition into physical address 0x200000.
3. The loader gathers the UEFI memory map, converts it to an E820 map, and builds the Multiboot information block. The first module descriptor carries the loader-reserved kernel span size in `module.reserved`; this value is mandatory for EXOS and consumed directly by the kernel memory initialization.
4. The loader switches to EXOS paging and GDT layout, then jumps to the kernel entry with the Multiboot registers set.


### Physical Memory map on x86-32 (may change)

```
┌──────────────────────────────────────────────────────────────────────────┐
│ 00000000 -> 000003FF  IVT                                                │
├──────────────────────────────────────────────────────────────────────────┤
│ 00000400 -> 000004FF  BIOS Data                                          │
├──────────────────────────────────────────────────────────────────────────┤
│ 00000500 -> 00000FFF  ??                                                 │
├──────────────────────────────────────────────────────────────────────────┤
│ 00002000 -> 00004FFF  VBR memory                                         │
├──────────────────────────────────────────────────────────────────────────┤
│ 00005000 -> 0001FFFF  Unused                                             │
├──────────────────────────────────────────────────────────────────────────┤
│ 00020000 -> 0009FBFF  EXOS Kernel (523263 bytes) mapped at C0000000      │
├──────────────────────────────────────────────────────────────────────────┤
│ 0009FC00 -> 0009FFFF  Extended BIOS data area                            │
├──────────────────────────────────────────────────────────────────────────┤
│ 000A0000 -> 000B7FFF  ROM Reserved                                       │
├──────────────────────────────────────────────────────────────────────────┤
│ 000B8000 -> 000BFFFF  Console buffer                                     │
├──────────────────────────────────────────────────────────────────────────┤
│ 000C0000 -> 000CFFFF  VESA                                               │
├──────────────────────────────────────────────────────────────────────────┤
│ 000F0000 -> 000FFFFF  BIOS                                               │
├──────────────────────────────────────────────────────────────────────────┤
│ 00100000 -> 003FFFFF  ??                                                 │
├──────────────────────────────────────────────────────────────────────────┤
│ 00400000 -> EFFFFFFF  Flat free RAM                                      │
├──────────────────────────────────────────────────────────────────────────┤
```


### Paging abstractions

The memory subsystem is split into architecture-agnostic services and architecture backends.

#### Layering and architecture backend

Common memory code consumes helpers exposed by `arch/Memory.h` and does not manipulate paging bitfields directly. Backend headers expose entry builders and accessors such as `MakePageDirectoryEntryValue`, `MakePageTableEntryValue`, and `WritePage*EntryValue`, which keeps common allocation logic independent from paging depth.

On x86-32, `arch/x86-32/x86-32-Memory.h` also exposes self-map helpers and an `ARCH_PAGE_ITERATOR`. Region routines (`IsRegionFree`, `AllocRegion`, `FreeRegion`, `ResizeRegion`) walk mappings through this iterator and reclaim empty page tables through `PageTableIsEmpty`. Physical range clipping is delegated to `ClipPhysicalRange`.

#### Physical memory allocation (buddy allocator)

Each architecture owns the low-level `InitializeMemoryManager` sequence. Both x86-32 and x86-64 bootstrap buddy metadata in low mapped memory, register loader-reserved and allocator-metadata physical spans (`SetLoaderReservedPhysicalRange`, `SetPhysicalAllocatorMetadataRange`), then call `MarkUsedPhysicalMemory` to reserve low memory, loader pages, allocator metadata pages, and non-available multiboot ranges before normal allocations start.

This separates physical page management (buddy allocator) from virtual mapping bookkeeping.

#### Virtual address space construction

Virtual address space setup follows a dependency order.

1. Page-directory construction:
- On x86-32 bootstrap, the page directory maps the `TaskRunner` page with write access so the kernel main stack can run during early bring-up.
- On x86-64, `AllocPageDirectory` creates the paging root and foundational mappings (low memory, kernel span, recursive slot, task-runner window).
- `AllocUserPageDirectory` reuses the same construction helpers and installs a user seed table so user mappings can be populated immediately.
- Default kernel virtual bases (`VMA_KERNEL`) are:
  - x86-32: `0xC0000000`
  - x86-64: `0xFFFFFFFFC0000000`

2. Arena layout (`PROCESS_ADDRESS_SPACE` in `kernel/include/process/Process-Arena.h`, `kernel/source/process/Process-Arena.c`):
- `Image`: executable image span loaded at `VMA_USER`.
- `Heap`: process heap growth lane.
- `Stack`: downward-growing user task stacks.
- `Module`: runtime-loaded executable module mappings and module bookkeeping.
- `System`: process-owned fixed mappings (for example message queue backing storage).
- `Mmio`: process MMIO/DMA-oriented mappings.

`VMA_USER_LIMIT` names the upper anchor of the process-owned user arena. The `TaskRunner` user alias sits one page below that anchor, and process arenas are laid out below `VMA_TASK_RUNNER`. x86-32 reserves 64 MiB for `System`, 64 MiB for `Mmio`, 256 MiB for `Stack`, and 1 GiB for `Module`. x86-64 reserves 1 GiB for `System`, 1 GiB for `Mmio`, 4 GiB for `Stack`, and 64 GiB for `Module`; the high user PML4 entry is installed up front and missing lower paging structures are allocated on demand.

3. Region operations inside arena policy:
- `AllocRegion`, `FreeRegion`, and `ResizeRegion` perform mapping updates, populate page tables, and flush translation state.
- Arena-aware allocation keeps fixed mappings out of the heap lane and avoids heap growth failures caused by unrelated `AllocRegion(0, ...)` placement.

#### Region descriptor tracking

Both x86-32 and x86-64 track successful virtual region operations with `MEMORY_REGION_DESCRIPTOR` records linked from `PROCESS.MemoryRegionList`. Allocation uses `RegionTrackAlloc`, release uses `RegionTrackFree`, and growth/shrink uses `RegionTrackResize`.

Descriptors are allocated from dedicated descriptor slabs mapped with `AllocKernelRegion`, so descriptor metadata does not consume the process heap. Each descriptor stores canonical base, size/page count, optional physical origin, flags/attributes, tag, and paging granularity.

Descriptor slab bootstrap is protected by `G_RegionDescriptorBootstrap`. While descriptor slabs are being allocated and mapped, tracking callbacks are temporarily bypassed to prevent recursive descriptor allocation.


## Isolation and Kernel Core

### Security Architecture

Security in EXOS is implemented as a layered architecture. The effective access decision is the result of CPU privilege isolation, virtual memory boundaries, session-backed user identity, syscall privilege checks, and object-level policy inside subsystems.

#### Layer 1: CPU privilege domains and execution context

- EXOS uses kernel/user CPU privilege separation (ring 0 and ring 3) in both x86-32 and x86-64 task setup paths.
- Task setup selects kernel or user code/data selectors based on `Process->Privilege` and seeds the initial interrupt frame accordingly (`kernel/source/arch/x86-32/x86-32.c`, `kernel/source/arch/x86-64/x86-64.c`).
- Kernel tasks start at `TaskRunner` with kernel selectors; user tasks start through the user-mapped task runner trampoline with user selectors.
- On x86-64, task setup also allocates a dedicated IST1 stack for fault handling, reducing the risk of stack-corruption escalation during exceptions.

#### Layer 2: Virtual memory isolation

- Per-process address spaces are built with separate kernel and user privilege page mappings.
- Kernel mappings are created with kernel page privilege; user seed tables and task runner mappings are created with user page privilege (`kernel/source/arch/x86-32/x86-32-Memory.c`, `kernel/source/arch/x86-64/x86-64-Memory.c`).
- User pointers received from syscalls are validated through `SAFE_USE_VALID`, `SAFE_USE_INPUT_POINTER`, and `IsValidMemory` checks before dereference (`kernel/source/SYSCall.c`).

#### Layer 3: Identity and session model

- Identity is session-centric: `GetCurrentUser()` resolves the current process session to a user account (`kernel/source/utils/Helpers.c`).
- `USER_ACCOUNT` stores `UserID`, privilege (`EXOS_PRIVILEGE_USER` or `EXOS_PRIVILEGE_ADMIN`), status, and password hash; `USER_SESSION` stores `SessionID`, `UserID`, login/activity timestamps, lock state, and shell task binding (`kernel/include/user/Account.h`).
- Session lifecycle is managed by `CreateUserSession`, `SetCurrentSession`, `GetCurrentSession`, timeout validation, and lock/unlock helpers in `kernel/source/user/UserSession.c`.
- Session inactivity timeout is configurable with `Session.TimeoutSeconds` in kernel configuration, with a compile fallback to `SESSION_TIMEOUT_MS`. Key `Session.TimeoutMinutes` is also accepted when `Session.TimeoutSeconds` is absent.
- Periodic session timeout enforcement is triggered from the scheduler path through a lightweight scheduler tick callback that signals deferred work; the lock/unlock user interface remains handled by shell code (`kernel/source/process/Schedule.c`, `kernel/source/user/UserSession.c`, `kernel/source/shell/Shell-Main.c`).
- Authentication throttling uses the shared `utils/AuthPolicy` helper. User login and session unlock flows apply a short cooldown after failures and a temporary lockout after repeated consecutive failures, while successful authentication resets the in-memory throttle state (`kernel/source/utils/AuthPolicy.c`, `kernel/source/user/Account.c`, `kernel/source/user/UserSession.c`).
- Child process creation inherits the parent session (`Process->Session`) and stable owner identifier (`Process->UserID`), preserving identity continuity across spawned processes even when the live session pointer is absent (`kernel/source/process/Process.c`, `kernel/source/user/UserSession.c`).
- Same-user process targeting policy and caller privilege resolution are centralized in `utils/ProcessAccess`: a process may target itself, processes owned by the same effective user, or any process when the caller resolves to administrator or kernel privilege. For kernel objects that carry `OBJECT_FIELDS`, the canonical security owner is `OBJECT.OwnerProcess`; generic object access checks resolve ownership from that field so tasks, windows, desktops, graphics contexts backed by one desktop, and other process-owned objects share one source of truth. `PROCESS` objects remain the deliberate exception because their `OwnerProcess` link models parentage, while the process object itself is its own security target. The module also exposes a current-process helper and global validation macros so syscall and subsystem code can share the same object-validation plus authorization pattern without local wrappers. Task access wrappers mediate shell task-control commands so the shell does not carry direct policy checks. These helpers are reused by exposure checks, process/task syscalls, generic handle-based syscalls, window/desktop/window-class syscalls, and shell-facing task control (`kernel/source/utils/ProcessAccess.c`, `kernel/source/expose/Expose-Security.c`, `kernel/source/SYSCall.c`, `kernel/source/process/Task-Access.c`).

#### Layer 4: Syscall privilege gate

- Every syscall is dispatched through `SystemCallHandler`, which checks the required privilege stored in `SysCallTable[]` before calling the handler (`kernel/source/SYSCall.c`, `kernel/source/SYSCallTable.c`).
- The privilege model is ordinal (`kernel < admin < user`) and compares current user privilege against the required level.
- Authentication/user-management syscalls (`Login`, `Logout`, `CreateUser`, `DeleteUser`, `ListUsers`, `ChangePassword`) apply explicit account checks in their handlers (`kernel/source/SYSCall.c`).

#### Layer 5: Handle boundary and kernel object exposure

- User space passes opaque handles; the kernel translates with `HandleToPointer`/`PointerToHandle` and validates target object type before operation (`kernel/source/SYSCall.c`).
- The shell script exposure layer enforces read policy per object and per field using `EXPOSE_REQUIRE_ACCESS(...)` with flags:
  - `EXPOSE_ACCESS_PUBLIC`
  - `EXPOSE_ACCESS_SAME_USER`
  - `EXPOSE_ACCESS_ADMIN`
  - `EXPOSE_ACCESS_KERNEL`
  - `EXPOSE_ACCESS_OWNER_PROCESS`
  (defined in `kernel/include/Exposed.h`, implemented by `kernel/source/expose/Expose-Security.c`)
- Process/task exposure protects sensitive fields (`pageDirectory`, heap metadata, architecture context, stack internals) behind kernel/admin or owner-process checks (`kernel/source/expose/Expose-Process.c`, `kernel/source/expose/Expose-Task.c`).

#### Layer 6: Security data model for kernel objects

- `SECURITY` objects provide owner and per-user permission fields (`READ`, `WRITE`, `EXECUTE`) with default permissions (`kernel/include/Security.h`).
- Process structures embed a `SECURITY` instance initialized by `InitSecurity()` during process creation (`kernel/source/process/Process.c`).
- This data model is present and initialized, while policy enforcement is concentrated in syscall handlers and expose-layer checks.

#### Architectural properties and current boundaries

- The architecture provides defense-in-depth through independent barriers (CPU ring separation, page privilege separation, session identity, syscall gate, and field-level exposure checks).
- Access control for script-visible kernel state is fine-grained and centralized through reusable expose security helpers.
- Security policy is not represented as a single global ACL engine. Some controls are explicit per-subsystem (for example, admin checks in user-management syscalls), which keeps behavior clear but requires discipline when adding new kernel entry points.


### Kernel objects

#### Object identifiers

Every kernel object stores a 64-bit instance identifier in `OBJECT_FIELDS.InstanceID`. The identifier is assigned by `CreateKernelObject` using a random UUID source and is kept for the object lifetime, including in the termination cache through `OBJECT_TERMINATION_STATE.InstanceID`. This provides stable identity when memory is reused and keeps scheduler lookups independent from raw pointers. Any kernel object that contains `OBJECT_FIELDS` (and therefore `LISTNODE_FIELDS`) and is meant to live in a global kernel list must be created with `CreateKernelObject` and destroyed with `ReleaseKernelObject`.

`OBJECT_FIELDS` also carries one optional per-object destructor pointer. `CreateKernelObject` initializes that slot to `NULL`, `SetKernelObjectDestructor(...)` binds one type-specific teardown routine when an object owns extra resources, and the global unreferenced-object sweep destroys objects through `DestroyKernelObject(...)`. This keeps garbage collection generic: the sweep does not branch on object types, and object-specific teardown remains attached to the object lifetime contract itself.

#### Event objects

Kernel events (`KOID_KERNELEVENT`) are lightweight objects for ISR-to-task signaling, implemented in `kernel/source/KernelEvent.c`. They embed `LISTNODE_FIELDS` plus a `Signaled` flag and `SignalCount` counter, and are tracked in `Kernel.Event` alongside other kernel-managed lists so reference tracking and garbage collection treat them uniformly.

Event lifecycle and state helpers:

- `CreateKernelEvent()` and `DeleteKernelEvent()` manage allocation and reference counting.
- `SignalKernelEvent()` and `ResetKernelEvent()` update the signaled state with local interrupts masked (`SaveFlags`/`DisableInterrupts`) so they are safe in interrupt context.
- `KernelEventIsSignaled()` and `KernelEventGetSignalCount()` expose state for scheduler and consumer code.

`Wait()` recognizes event handles in `WAIT_INFO.Objects` and returns when `SignalKernelEvent` marks the event signaled, propagating `SignalCount` through the wait exit codes. The termination cache is checked first for process and task exits.

#### List nodes

Kernel objects that embed `LISTNODE_FIELDS` participate in intrusive lists. Each list node carries a `Parent` pointer so objects can represent hierarchy when required, but insertion helpers keep the `Parent` pointer NULL unless it is explicitly set by the caller. This avoids accidental parent chains while still enabling structured ownership models.


### Handle reuse

#### Global mapping architecture

Handle translation is centralized in one global `HANDLE_MAP` stored in `Kernel.HandleMap` and initialized during `InitializeKernel()` with `HandleMapInit()`.

`HANDLE_MAP` combines:
- a radix tree (`Map->Tree`) keyed by handle value,
- a slab-style block allocator (`Map->EntryAllocator`) for `HANDLE_MAP_ENTRY`,
- a mutex (`Map->Mutex`) guarding every map operation,
- a monotonic allocator cursor (`Map->NextHandle`), starting at `HANDLE_MINIMUM` (`0x10`).

Each map entry stores `{Handle, Pointer, Attached}`. A handle is considered valid for resolution only when `Attached` is true and `Pointer` is non-null.

#### Conversion flow

`PointerToHandle()` enforces pointer-to-handle reuse before allocation:
1. Reject null pointers.
2. Search for an existing mapping with `HandleMapFindHandleByPointer()`.
3. Reuse the existing handle when found.
4. Otherwise allocate a new handle (`HandleMapAllocateHandle`) and attach the pointer (`HandleMapAttachPointer`).

This guarantees one active exported handle per pointer in the global map and avoids duplicate exports during repeated conversions.

`HandleToPointer()` performs the reverse operation through `HandleMapResolveHandle()`. `ReleaseHandle()` detaches the pointer (`HandleMapDetachPointer`) then removes the handle entry (`HandleMapReleaseHandle`).

`EnsureKernelPointer()` and `EnsureHandle()` normalize mixed values in call paths that may receive either raw kernel pointers or user-visible handles.

#### Message path behavior

`SysCall_GetMessage()` and `SysCall_PeekMessage()` convert `MESSAGE_INFO.Target` from handle to pointer before calling the internal queue logic, then convert the returned pointer back through `PointerToHandle()`. `SysCall_DispatchMessage()` resolves the incoming handle to a pointer for dispatch, then restores the original handle in the user buffer.

Because `PointerToHandle()` reuses existing mappings, repeated message retrieval and dispatch cycles keep target handles stable instead of generating transient replacements.

#### Complexity boundary

Forward resolution (handle -> pointer) is radix-tree lookup. Reverse lookup (pointer -> handle reuse check) is implemented by iterating the radix tree (`HandleMapFindHandleByPointer`), so it is linear in the number of active handles. The design favors simple global consistency and stable handle identity over constant-time reverse indexing.


## Execution Model and Kernel Interface

### Tasks

#### Architecture-specific task data

Each task embeds an `ARCH_TASK_DATA` structure (declared in the architecture-specific header under `kernel/include/arch/`) that contains the saved interrupt frame along with the user, system, and any auxiliary stack descriptors that the target CPU requires. The generic `tag_TASK` definition in `kernel/include/process/Task.h` exposes this structure as the `Arch` member so that all stack and context manipulations remain scoped to the active architecture.

The x86-32 implementation of `SetupTask` (`kernel/source/arch/x86-32/x86-32.c`) allocates and clears per-task stacks, initializes selectors in the interrupt frame, and performs the bootstrap stack switch for the main kernel task.

The x86-64 implementation performs the same baseline duties and also provisions a dedicated Interrupt Stack Table (`IST1`) stack for faults that need a reliable kernel stack when the regular system stack is unusable. During IDT initialization, the kernel assigns `IST1` to fault vectors that are likely to run with a corrupted task stack (double fault, invalid TSS, segment-not-present, stack, general protection, and page faults). This keeps handlers on the emergency per-task stack and prevents double-fault escalation into triple fault when the active stack pointer is invalid.

`KernelCreateTask` calls the architecture-specific helper after generic task bookkeeping. This keeps scheduler and task-manager logic architecture-agnostic while allowing each architecture to specialize `SetupTask`.

Both the x86-32 and x86-64 context-switch helpers (`SetupStackForKernelMode` and `SetupStackForUserMode` in their respective architecture headers) must reserve space on the stack in bytes rather than entries before writing the return frame. Subtracting the correct byte count avoids writing past the top of the allocated stack when seeding the initial `iret` frame for a task. On x86-64 the helpers also arrange the bootstrap frame so that the stack pointer becomes 16-byte aligned after `iretq` pops its arguments, preserving the ABI-mandated alignment once execution resumes in the scheduled task.

#### Stack sizing

The minimum sizes for task and system stacks are driven by the configuration keys `Task.MinimumTaskStackSize` and `Task.MinimumSystemStackSize` in `kernel/configuration/exos.ref.toml`. At boot the task manager reads those values, but it clamps them to the architecture defaults (`64 KiB`/`16 KiB` on x86-32 and `128 KiB`/`32 KiB` on x86-64) to prevent under-provisioned stacks. Increasing the values in the configuration grows every newly created task and keeps the auto stack growing logic operating on the larger baseline.

Stack growth also enforces compile-time caps defined in `kernel/include/Stack.h` (`STACK_MAXIMUM_TASK_STACK_SIZE`, `STACK_MAXIMUM_SYSTEM_STACK_SIZE`). The kernel does not rely on runtime configuration for these hard limits.

When an in-place resize fails (for example because the next virtual range is occupied), `GrowCurrentStack` relocates the active stack to a new region, switches the live stack pointer to the new top, updates the task stack descriptor, then releases the old region. This keeps kernel stack growth functional even when neighboring mappings block contiguous expansion.

On x86-64, interrupt and syscall stubs must treat the saved general-register `RSP` slot as diagnostic state only. The unwind path discards that slot instead of restoring `RSP` from it, so a relocated live kernel stack continues to return through the copied interrupt frame rather than jumping back to the previous stack mapping.

On x86-64, task setup allocates IST1 with an explicit guard gap above the system stack, so emergency fault stack placement does not sit immediately adjacent to the regular system stack.

The stack autotest module (`TestCopyStack`) is registered for on-demand execution only. It is excluded from the boot-time `RunAllTests` path and can be triggered manually from the shell with `autotest stack`.

#### IRQ scheduling

##### IRQ 0 path

IRQ 0 └── trap lands in interrupt-a.asm : Interrupt_Clock
    └── calls ClockHandler to increment system time
    └── calls Scheduler to check if it's time to switch to another task
        └── Scheduler switches page directory if needed and returns the next task's context

`GetSystemTime()` normally returns the millisecond counter maintained by `ClockHandler`. After interrupts are enabled, but before IRQ 0 increments that counter, the clock returns synthetic 10 ms steps so boot log entries in that gap do not all carry the same timestamp. The first `ClockHandler` invocation folds that pre-interrupt value into `SystemUpTime` so timestamps do not move backward when the real IRQ-driven counter starts; scheduler time and local date-time remain driven by real clock interrupts.

`Sleep` relies on scheduler wake-up timestamps only after the first `EnableInterrupts` call is executed. Before this point, `Sleep` uses a calibrated busy-wait loop (derived from CPU base frequency) so early-boot delays do not depend on `GetSystemTime` progression.

Fields that are read or written by the scheduler from ISR context are isolated in dedicated structures instead of sharing the general task/process state:
- `TASK.SchedulerState` stores scheduler-owned task fields such as `Status` and `WakeUpTime`.
- `PROCESS.SchedulerState` stores scheduler-owned process fields such as the paused state.
- `GetTaskSchedulerStateSnapshot()`, `SetTaskSchedulerStatus()`, and `GetProcessSchedulerStateSnapshot()` expose this data to the scheduling code without taking task-local or process-local mutexes.

##### ISR 0 call graph

```
Interrupt_Clock └── BuildInterruptFrame
    └── KernelLogText
        └── StringEmpty
        └── StringPrintFormatArgs
            └── IsNumeric : endpoint
            └── IsNumeric : endpoint
            └── SkipAToI : endpoint
            └── VarArg : endpoint
            └── StringLength : endpoint
            └── NumberToString : endpoint
        └── KernelPrintString
            └── LockMutex
                └── SaveFlags : endpoint
                └── DisableInterrupts : endpoint
                └── GetCurrentTask : endpoint
                └── RestoreFlags : endpoint
                └── GetSystemTime : endpoint
                └── IdleCPU : endpoint
            └── UnlockMutex
                └── SaveFlags : endpoint
                └── DisableInterrupts : endpoint
                └── GetCurrentTask : endpoint
                └── RestoreFlags : endpoint
        └── KernelPrintChar : endpoint
└── ClockHandler
    └── KernelLogText
        └── ...
    └── KernelPrintString
        └── ...
    └── IsLeapYear : endpoint
    └── Scheduler
        └── KernelLogText
            └── ...
        └── CheckStack
            └── GetCurrentTask : endpoint
        └── KernelKillTask
            └── KernelLogText
                └── ...
            └── RemoveTaskFromQueue
                └── FreezeScheduler
                    └── LockMutex
                        └── ...
                    └── UnlockMutex
                        └── ...
                └── FindNextRunnableTask
                    └── GetSystemTime : endpoint
                └── UnfreezeScheduler
                    └── LockMutex
                        └── ...
                    └── UnlockMutex
                        └── ...
                └── KernelLogText
                    └── ...
            └── LockMutex
                └── ...
            └── ListRemove : endpoint
            └── DeleteTask
                └── KernelLogText
                    └── ...
                    └── HeapFree_HBHS : endpoint
                    └── HeapFree
                        └── GetCurrentProcess
                            └── GetCurrentTask : endpoint
                        └── HeapFree_P
                            └── LockMutex
                                └── ...
                            └── HeapFree_HBHS : endpoint
                            └── UnlockMutex
                                └── ...
            └── UnlockMutex
                └── ...
```


### Process and Task Lifecycle Management

EXOS implements a lifecycle management system for both processes and tasks that ensures consistent cleanup and prevents resource leaks.

#### Process Heap Management

- Every `PROCESS` keeps track of its `MaximumAllocatedMemory`, which is initialized to `N_HalfMemory` for both the kernel and user processes.
- When a heap allocation exhausts the committed region, the kernel automatically attempts to double the heap size without exceeding the process limit by calling `ResizeRegion`.
- If the resize operation cannot be completed, the allocator logs an error and the allocation fails gracefully.
- Kernel heap allocations that still fail dump the current task interrupt frame through the same logging path used by the #GP/#PF handlers, giving register and backtrace context when diagnosing out-of-heap issues.
- `SysCall_GetProcessMemoryInfo` exposes a dedicated `PROCESS_MEMORY_INFO` snapshot for heap diagnostics. The structure reports the current process heap base, reserved span, first-unallocated offset, used payload bytes, and free payload bytes without overloading process-creation structures.

#### Reserved module heaps

- `HeapAlloc_HBHS`, `HeapRealloc_HBHS`, and `HeapFree_HBHS` operate on an explicit heap base and size and form the common backend for both the process heap and module-owned heaps.
- `HEAP_CONTROL_BLOCK` carries heap-local growth policy (`ResizeCallback`, `ResizeContext`, `MaximumSize`, `RegionFlags`) so heap expansion is no longer limited to `PROCESS.HeapBase`.
- `utils/ReservedHeap` wraps one dedicated virtual region, one mutex, and one allocator view around that backend. The owner initializes the region, exposes `ReservedHeapAlloc`/`ReservedHeapRealloc`/`ReservedHeapFree`, and tears down the whole region at module shutdown.
- The shell uses this mechanism for scripting state, command history, completion data, and shell-managed temporary buffers. This isolates shell pressure from the main kernel heap while keeping the allocation algorithm shared.
- Reusable helpers that need to allocate inside one module heap consume a context-aware `ALLOCATOR` instead of calling `KernelHeapAlloc` or `HeapAlloc` directly.

#### Status States

**Task Status (Task.Status):**
- `TASK_STATUS_FREE` (0x00): Unused task slot
- `TASK_STATUS_READY` (0x01): Ready to run
- `TASK_STATUS_RUNNING` (0x02): Currently executing
- `TASK_STATUS_WAITING` (0x03): Waiting for an event
- `TASK_STATUS_SLEEPING` (0x04): Sleeping for a specific time
- `TASK_STATUS_WAITMESSAGE` (0x05): Waiting for a message
- `TASK_STATUS_DEAD` (0xFF): Marked for deletion

- Sleep durations are specified in `UINT`. A value of `INFINITY` is treated as a sentinel meaning "sleep indefinitely". `SetTaskWakeUpTime()` stores `INFINITY` without adding the current time and the scheduler ignores such tasks until another subsystem explicitly changes their status.
- Task suspension is tracked separately from `Task.Status` in scheduler-owned task state. `SuspendTaskExecution()` and `ResumeTaskExecution()` toggle a dedicated suspension flag, allowing the scheduler to stop selecting a task without destroying whether it was running, sleeping, or waiting for messages.

**Process Status (Process.Status):**
- `PROCESS_STATUS_ALIVE` (0x00): Normal operating state
- `PROCESS_STATUS_DEAD` (0xFF): Marked for deletion

#### Process Creation Flags

**Process Creation Flags (Process.Flags):**
- `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` (0x00000001): When the process terminates, all child processes are also killed. If this flag is not set, child processes are orphaned (their Parent field is set to NULL).

#### Session Inheritance

- New processes inherit the user session pointer from their `OwnerProcess` during `NewProcess`.
- Session ownership is therefore tied to the process tree: children share the same session by default unless explicitly reassigned.
- This keeps user identity and security context consistent across a spawned process hierarchy.

#### Standard Stream Inheritance and Pipes

- `PROCESS_INFO` carries `StdIn`, `StdOut`, and `StdErr` handle inputs for process creation.
- `CreateProcess()` resolves those handles, duplicates them for child ownership, and stores effective values in the `PROCESS` structure.
- If one standard stream handle is omitted at creation, the child inherits the corresponding handle from its parent process.
- Process teardown closes child-owned standard stream handles so stream object lifetime follows process lifetime.
- `GetProcessInfo()` returns the effective standard stream handles for runtime initialization.
- Anonymous pipes are provided by `SYSCALL_CreatePipe` (`PIPE_INFO`) and exported as two endpoint handles:
  - read endpoint,
  - write endpoint.
- `ReadFile` and `WriteFile` syscalls accept both file handles and pipe endpoint handles, so redirection and pipelines use one shared data path.

#### Lifecycle Flow

**1. Task Termination:**
- When a task terminates, `KernelKillTask()` releases every mutex held by the task before marking it as `TASK_STATUS_DEAD`
- During `DeleteTask()`, the task is removed from the scheduler queue before task resources are released
- `DeleteDeadTasksAndProcesses()` (called periodically) removes dead tasks and processes from lists

**2. Process Termination via Task Count:**
- When `DeleteTask()` processes a dead task:
  - Decrements `Process.TaskCount`
  - If `TaskCount` reaches 0:
    - Applies child process policy based on `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
    - Marks the process as `PROCESS_STATUS_DEAD`
  - The process remains in the process list for later cleanup

**3. Process Termination via KillProcess:**
- `KillProcess()` can be called to terminate a process and handle its children:
  - Checks the `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
  - If flag is set: Finds all child processes recursively and kills them
  - If flag is not set: Orphans children by setting their `Parent` field to NULL
  - Calls `KernelKillTask()` on all tasks of the target process
  - Marks the target process as `PROCESS_STATUS_DEAD`

**4. Final Cleanup:**
- `DeleteDeadTasksAndProcesses()` is called periodically by the kernel monitor
- First phase: Processes all `TASK_STATUS_DEAD` tasks
  - Calls `DeleteTask()` which frees stacks, message queues, etc.
  - Updates process task counts and marks processes dead if needed
- Second phase: Processes all `PROCESS_STATUS_DEAD` processes
  - Calls `ReleaseProcessKernelObjects()` to drop references held by the process on every kernel-managed list
  - Calls `DeleteProcessCommit()` which frees page directories, heaps, etc.
  - Removes process from global process list

#### Key Design Principles

**Deferred Deletion:**
- Neither tasks nor processes are immediately freed when killed
- They are marked as DEAD and cleaned up later by `DeleteDeadTasksAndProcesses()`
- This prevents race conditions and ensures consistent state

**Hierarchical Process Management:**
- Child process handling depends on parent's `PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH` flag
- If flag is set: Child processes are automatically killed when parent dies
- If flag is not set: Child processes are orphaned (Parent set to NULL)
- The `Parent` field creates a process tree structure
- `KillProcess()` implements policy-based child handling

**Mutex Protection:**
- Process list operations are protected by `MUTEX_PROCESS`
- Task list operations are protected by `MUTEX_KERNEL`
- `ReleaseProcessKernelObjects()` requires `MUTEX_KERNEL` to be locked while iterating kernel lists
- Task count updates are atomic to prevent race conditions

**Resource Cleanup Order:**
1. Tasks are killed first (marked as DEAD)
2. Task resources are freed (stacks, message queues)
3. Process task count reaches zero
4. Process is marked as DEAD
5. Process resources are freed (page directory, heap)
6. Process is removed from global list

This approach ensures that:
- No resources are leaked during process/task termination
- Parent-child relationships are properly maintained
- The system remains stable during complex termination scenarios
- Both voluntary (task exit) and involuntary (kill) termination work consistently


### System calls

#### System call full path - x86-32

`USE_SYSCALL` is a project-level build flag (`./scripts/linux/build/build --arch x86-64 --fs ext2 --debug --use-syscall`) that selects between the interrupt gate path and the SYSCALL/SYSRET pair on x86-64. The flag has no effect on x86-32 builds.

```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'int EXOS_USER_CALL' instruction
        └── trap lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

#### System call full path - x86-64

When `USE_SYSCALL = 0` (default build setting)
```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'int EXOS_USER_CALL' instruction
        └── trap lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

When `USE_SYSCALL = 1`
```
exos-runtime-c.c : malloc() (or any other function)
└── calls exos-runtime-a.asm : exoscall()
    └── 'syscall' instruction
        └── syscall lands in interrupt-a.asm : Interrupt_SystemCall
            └── calls SYSCall.c : SystemCallHandler()
                └── calls SysCall_xxx via SysCallTable[]
                    └── whew... finally job is done
```

#### Syscall return ABI contract

Syscalls that use user-provided `*_INFO` payloads follow one return contract:
- the syscall return register carries only `DF_RETURN_*` status codes;
- payload values are written back through explicit output fields in the same structure;
- callers must check `Result == DF_RETURN_SUCCESS` before reading output fields.

`CreateProcess` follows this pattern by returning `DF_RETURN_*` and writing created handles in `PROCESS_INFO.Process` and `PROCESS_INFO.Task`. `CreateTask` returns `DF_RETURN_*` and writes the created task handle in `TASK_INFO.Task`. Runtime wrappers expose `ExosIsSuccess(...)` so userland checks are consistent.

### Task and window message delivery

Tasks and processes own fixed-size message queues (`MESSAGEQUEUE` in `kernel/source/process/Task-Messaging.c`) backed by dedicated virtual memory regions and operated through `utils/MessageQueue`. Task queues are allocated at task creation (`TaskInitializeMessageBuffer` in `kernel/source/process/Task.c`), while process queues are allocated on demand (`EnsureProcessMessageQueue`). Both use the process `System` arena (`ProcessArenaAllocateSystem`) so queue storage does not consume heap expansion space. Queue operations do not allocate or free entries during message posting/retrieval. If a target queue does not exist, posted messages are dropped and keyboard input continues down the classic buffered path for `getkey()` (used by the shell). When a process message queue exists, the keyboard helpers (`PeekChar`, `GetChar`, `GetKeyCode`) consume key events from that queue by discarding `EWM_KEYUP` messages and returning the first `EWM_KEYDOWN`, then fall back to the classic buffer when no queue exists. Each queue is capped to 100 pending messages and guarded by a per-queue mutex plus a waiting flag. `WaitForMessage` marks the task queue as waiting and sleeps the task; `AddTaskMessage` wakes the task when a new message arrives and clears the waiting flag.

Message posting:
- `PostMessage` accepts NULL targets (current task), task handles, and window handles; window targets enqueue into the owning task queue. Keyboard drivers and the mouse dispatcher push input events into the global input queue using `EnqueueInputMessage` so only the focused process sees them.
- Queue pressure is reduced by coalescing duplicate window messages in `PostMessage`: one pending `EWM_DRAW` per target window, and one pending `EWM_NOTIFY` per target window and notification id (`Param1`).
- Generic control activation uses `EWM_CLICKED`, while structural window notifications such as `EWN_WINDOW_RECT_CHANGED` remain on `EWM_NOTIFY`.
- Mouse input is throttled by a tiny dispatcher that filters `EWM_MOUSEMOVE` with a 10ms cooldown between enqueues, while button changes still dispatch immediately through the shared input queue.
- `SendMessage` is synchronous and window-only.

Message retrieval:
- Public runtime calls `GetMessage`/`PeekMessage` first check the global input queue when the caller’s process has focus (desktop focus + per-desktop `FocusedProcess`), then fall back to the task’s own queue. `GetMessage` blocks if neither queue holds messages; `PeekMessage` is non-blocking. Userland syscalls translate handles in `MESSAGE_INFO` before dispatching to the kernel implementations `KernelGetMessage` and `KernelPeekMessage`.
- Focus tracking lives in `Kernel.ActiveDesktop` and `Kernel.FocusedProcess`. A process may exist without any desktop. When a focused process is associated with a desktop, focusing that process also makes its desktop active. When no active desktop exists, input falls back to the focused process, then to `KernelProcess`.


### Command line editing

Interactive editing of shell command lines is implemented in `kernel/source/utils/CommandLineEditor.c`. The module processes keyboard input via the classic buffered path (`PeekChar`/`GetKeyCode`), maintains an in-memory history, refreshes the console display, and relies on callbacks to retrieve completion suggestions. The shell owns an input state structure that embeds the editor instance and provides shell-specific callbacks for completion and idle processing so the component remains agnostic of higher level shell logic. While reading input, the editor adjusts for console scrolling so the display does not re-trigger scrolling on each key press, console paging prompts are suspended until the line is submitted, and successful key interactions update session activity timestamps.

All reusable helpers -such as the command line editor, adaptive delay, string containers, byte-size formatting helpers (`utils/SizeFormat`), CRC/SHA-256 utilities, compression utilities, chunk cache utilities, detached signature utilities, notifications, path helpers, TOML parsing, UUID support, regex, hysteresis control, cooldown timing, rate limiting, DMA buffer allocation (`utils/DMABuffer`), and network checksum helpers— live under `kernel/source/utils` with their public headers in `kernel/include/utils`. Architecture-compat 64-bit helpers shared by the whole kernel (`U64_MUL_U32`, `U64_DIV_U32`) are exposed from `kernel/include/Base.h` and keep arithmetic behavior identical on x86-32 and x86-64. SHA-256 is exposed through `utils/Crypt` and bridged to the vendored BearSSL hash implementation under `third/bearssl`. Compression is exposed through `utils/Compression` and bridged to the vendored miniz backend under `third/miniz`. Detached signature verification is exposed through `utils/Signature` with a backend-swappable API surface, and Ed25519 verification is wired to vendored Monocypher sources under `third/monocypher`. This keeps generic infrastructure separated from core subsystems and makes it easier to share common code across the kernel.


### Exposed objects in shell

- `process`: Kernel process list root. provides indexed access to process views. Permissions: exposed to scripts; access is enforced per item and per field.
  - `process.count`: total number of processes. Permissions: anyone.
  - `process[n]`: process view at index `n`. Permissions: see fields below.
    - `handle`: handle for the process. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `status`: current process status. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `flags`: process flags bitfield. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `exitCode`: termination status. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `fileName`: executable name. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `commandLine`: full command line string. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `workFolder`: current working folder. Permissions: anyone (except `process[0]`, kernel and administrator only).
    - `pageDirectory`: page directory pointer. Permissions: kernel and administrator only.
    - `heapBase`: heap base address. Permissions: kernel and administrator only.
    - `heapSize`: heap size. Permissions: kernel and administrator only.
    - `task`: task list for the owning process. Permissions: kernel and administrator only.
      - `task.count`: number of tasks in the process. Permissions: kernel and administrator only.
      - `task[n]`: task view at index `n`. Permissions: see fields below.
        - `handle`: user-visible handle for the task. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `name`: task name. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `type`: task type. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `status`: current task status. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `priority`: scheduling priority. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `flags`: task flags bitfield. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `exitCode`: termination status. Permissions: anyone (except tasks that belong to the kernel process, kernel and administrator only).
        - `function`: task entry function pointer. Permissions: kernel, administrator, and owner process.
        - `parameter`: task entry parameter pointer. Permissions: kernel, administrator, and owner process.
        - `architecture`: architecture-specific context view. Permissions: kernel and administrator only.
          - `context`: raw context pointer. Permissions: kernel and administrator only.
          - `stack`: architecture stack view. Permissions: kernel and administrator only.
          - `systemStack`: architecture system stack view. Permissions: kernel and administrator only.
        - `stack`: task stack view. Permissions: kernel and administrator only.
          - `base`: stack base address. Permissions: kernel and administrator only.
          - `size`: stack size. Permissions: kernel and administrator only.
        - `systemStack`: system stack pointer. Permissions: kernel and administrator only.
        - `wakeUpTime`: scheduler wake-up time. Permissions: kernel and administrator only.
        - `mutex`: mutex pointer. Permissions: kernel and administrator only.
        - `messageQueue`: message queue pointer. Permissions: kernel and administrator only.
        - `process`: owning process pointer. Permissions: kernel and administrator only.
- `task`: Global task list root filtered through process access policy.
  - `task.count`: number of tasks the caller may target.
  - `task[n]`: task view at visible index `n`. Permissions and fields match the task view exposed through `process[n].task[n]`.
- `driver`: Kernel driver list root. provides indexed access to driver views. Permissions: kernel and administrator only.
  - `driver.count`: number of drivers. Permissions: kernel and administrator only.
  - `driver[n]`: driver view at index `n`. Permissions: kernel and administrator only.
    - `type`: driver type. Permissions: kernel and administrator only.
    - `typeName`: driver type text. Permissions: anyone.
    - `alias`: driver alias. Permissions: anyone.
    - `versionMajor`: major version. Permissions: kernel and administrator only.
    - `versionMinor`: minor version. Permissions: kernel and administrator only.
    - `designer`: driver designer. Permissions: kernel and administrator only.
    - `manufacturer`: driver manufacturer. Permissions: kernel and administrator only.
    - `product`: driver product name. Permissions: kernel and administrator only.
    - `ready`: ready flag. Permissions: anyone.
    - `flags`: driver flags. Permissions: kernel and administrator only.
    - `mode`: graphics mode array exposed by concrete graphics backends.
      - `mode.count`: number of reported graphics modes for this driver.
      - `mode[n]`: graphics mode view at index `n`.
        - `width`: mode width.
        - `height`: mode height.
        - `bpp`: mode bit depth.
    - `enumDomainCount`: number of enum domains. Permissions: kernel and administrator only.
    - `enumDomain`: enum domain array. Permissions: kernel and administrator only.
      - `enumDomain.count`: enum domain count. Permissions: kernel and administrator only.
      - `enumDomain[n]`: enum domain value at index `n`. Permissions: kernel and administrator only.
- `graphics`: Active graphics session state. Permissions: anyone.
  - `graphics.frontend`: active display frontend text (`console` or `desktop`).
  - `graphics.currentDriverIndex`: index of the active concrete graphics backend inside `driver[n]`.
  - `graphics.currentDriverAlias`: alias of the active concrete graphics backend.
  - `graphics.mode`: active graphics mode view.
    - `graphics.mode.width`: active mode width.
    - `graphics.mode.height`: active mode height.
    - `graphics.mode.bpp`: active mode bit depth.
- `clock`: Clock exposure root. Permissions: anyone.
  - `clock.uptimeMs`: milliseconds since clock initialization.
  - `clock.bootDatetime`: local date-time captured during clock initialization.
  - `clock.currentDatetime`: local date-time maintained by the clock tick.
  - Date-time objects expose `year`, `month`, `day`, `hour`, `minute`, `second`, and `milli`.
- `memoryMap`: Kernel address-space exposure root. Permissions: anyone.
  - `memoryMap.kernelRegion`: kernel memory region list root.
    - `memoryMap.kernelRegion.count`: number of kernel memory regions.
    - `memoryMap.kernelRegion[n]`: kernel memory region view at index `n`.
      - `tag`: region tag.
      - `baseLow`: lower 32 bits of the canonical base.
      - `baseHigh`: upper 32 bits of the canonical base.
      - `physicalLow`: lower 32 bits of the physical base.
      - `physicalHigh`: upper 32 bits of the physical base.
      - `physicalKnown`: indicates whether a physical base is known.
      - `size`: region size.
      - `pageCount`: region page count.
      - `flags`: region flags.
      - `attributes`: region attributes.
      - `granularity`: region granularity.
- `network`: Network exposure root. Permissions: anyone.
  - `network.device`: network device list root.
    - `network.device.count`: number of network devices.
    - `network.device[n]`: network device view at index `n`.
      - `name`: device name.
      - `manufacturer`: device manufacturer text.
      - `product`: device product text.
      - `mac0`..`mac5`: MAC address bytes.
      - `ip0`..`ip3`: IPv4 address bytes.
      - `linkUp`: link state.
      - `speedMbps`: link speed in Mbps.
      - `duplexFull`: duplex mode flag.
      - `mtu`: configured MTU.
      - `initialized`: initialization state.
- `storage`: Storage list root. provides indexed access to storage views. Permissions: anyone.
  - `storage.count`: number of storage objects. Permissions: anyone.
  - `storage[n]`: storage view at index `n`. Permissions: anyone.
    - `type`: storage type. Permissions: anyone.
    - `removable`: removable flag. Permissions: anyone.
    - `bytesPerSector`: sector size in bytes. Permissions: anyone.
    - `numSectorsLow`: lower 32-bit sector count. Permissions: anyone.
    - `numSectorsHigh`: upper 32-bit sector count. Permissions: anyone.
    - `access`: access mode flags. Permissions: anyone.
    - `driverManufacturer`: backing driver manufacturer. Permissions: anyone.
    - `driverProduct`: backing driver product name. Permissions: anyone.
- `pciBus`: PCI bus list root. provides indexed access to PCI bus views. Permissions: anyone.
  - `pciBus.count`: number of PCI buses. Permissions: anyone.
  - `pciBus[n]`: PCI bus view at index `n`. Permissions: anyone.
    - `number`: PCI bus number. Permissions: anyone.
    - `deviceCount`: number of PCI devices on the bus. Permissions: anyone.
- `pciDevice`: PCI device list root. provides indexed access to PCI device views. Permissions: anyone.
  - `pciDevice.count`: number of PCI devices. Permissions: anyone.
  - `pciDevice[n]`: PCI device view at index `n`. Permissions: anyone.
    - `bus`: PCI bus number. Permissions: anyone.
    - `device`: PCI device number. Permissions: anyone.
    - `function`: PCI function number. Permissions: anyone.
    - `vendorId`: PCI vendor identifier. Permissions: anyone.
    - `deviceId`: PCI device identifier. Permissions: anyone.
    - `baseClass`: PCI base class code. Permissions: anyone.
    - `subClass`: PCI subclass code. Permissions: anyone.
    - `progIf`: PCI programming interface code. Permissions: anyone.
    - `revision`: PCI revision code. Permissions: anyone.
- `keyboard`: Keyboard exposure root. provides access to keyboard state. Permissions: anyone.
  - `keyboard.layout`: active keyboard layout code. Permissions: anyone.
  - `keyboard.driver`: active keyboard driver. Permissions: kernel and administrator only.
- `mouse`: Mouse exposure root. provides access to mouse state. Permissions: anyone.
  - `mouse.x`: cursor X coordinate. Permissions: anyone.
  - `mouse.y`: cursor Y coordinate. Permissions: anyone.
  - `mouse.driver`: active mouse driver. Permissions: kernel and administrator only.
- `usb.port`: xHCI port list root. provides indexed access to USB ports. Permissions: kernel and administrator only.
  - `usb.port.count`: number of USB ports. Permissions: kernel and administrator only.
  - `usb.port[n]`: port view at index `n`. Permissions: kernel and administrator only.
    - `bus`: PCI bus number. Permissions: kernel and administrator only.
    - `device`: PCI device number. Permissions: kernel and administrator only.
    - `function`: PCI function number. Permissions: kernel and administrator only.
    - `portNumber`: xHCI port index. Permissions: kernel and administrator only.
    - `portStatus`: raw port status. Permissions: kernel and administrator only.
    - `speedId`: link speed identifier. Permissions: kernel and administrator only.
    - `connected`: connection status. Permissions: kernel and administrator only.
    - `enabled`: port enable state. Permissions: kernel and administrator only.
- `usb.device`: USB device list root. provides indexed access to USB devices. Permissions: anyone.
  - `usb.device.count`: number of USB devices. Permissions: anyone.
  - `usb.device[n]`: device view at index `n`. Permissions: anyone.
    - `bus`: PCI bus number. Permissions: anyone.
    - `device`: PCI device number. Permissions: anyone.
    - `function`: PCI function number. Permissions: anyone.
    - `portNumber`: xHCI port index. Permissions: anyone.
    - `address`: USB device address. Permissions: anyone.
    - `speedId`: link speed identifier. Permissions: anyone.
    - `vendorId`: USB vendor ID. Permissions: anyone.
    - `productId`: USB product ID. Permissions: anyone.
- `usb.drive`: USB mass-storage list root. provides indexed access to USB storage devices. Permissions: anyone.
  - `usb.drive.count`: number of USB mass-storage devices. Permissions: anyone.
  - `usb.drive[n]`: storage device view at index `n`. Permissions: anyone.
    - `address`: USB device address. Permissions: anyone.
    - `vendorId`: USB vendor ID. Permissions: anyone.
    - `productId`: USB product ID. Permissions: anyone.
    - `blockCount`: block count. Permissions: anyone.
    - `blockSize`: block size in bytes. Permissions: anyone.
    - `present`: online state. Permissions: anyone.
- `usb.node`: USB descriptor-tree list root. provides indexed access to discovered USB tree nodes. Permissions: anyone.
  - `usb.node.count`: number of USB tree nodes. Permissions: anyone.
  - `usb.node[n]`: tree node view at index `n`. Permissions: anyone.
    - `nodeType`: node kind.
    - `bus`: PCI bus number.
    - `device`: PCI device number.
    - `function`: PCI function number.
    - `portNumber`: xHCI port index.
    - `address`: USB device address.
    - `speedId`: link speed identifier.
    - `deviceClass`: USB device class.
    - `deviceSubClass`: USB device subclass.
    - `deviceProtocol`: USB device protocol.
    - `configValue`: configuration value.
    - `configAttributes`: configuration attributes.
    - `configMaxPower`: configuration max power.
    - `interfaceNumber`: interface number.
    - `alternateSetting`: interface alternate setting.
    - `interfaceClass`: interface class.
    - `interfaceSubClass`: interface subclass.
    - `interfaceProtocol`: interface protocol.
    - `endpointAddress`: endpoint address.
    - `endpointAttributes`: endpoint attributes.
    - `endpointMaxPacketSize`: endpoint max packet size.
    - `endpointInterval`: endpoint interval.
    - `vendorId`: USB vendor ID.
    - `productId`: USB product ID.


## Hardware and Driver Stack

### Driver architecture

Hardware-facing components are grouped under `kernel/source/drivers` with public headers in `kernel/include/drivers`. This area contains keyboard, serial mouse, interrupt controller (I/O APIC), PCI bus, network (`E1000`, `RTL8139`, `RTL8139CPlus`, `RTL8169` family), storage (`ATA`, `SATA`, `NVMe`), graphics (`iGPU`, `VGA`, `VESA`, mode tables), and file system backends (`FAT16`, `FAT32`, `ext2`).

Kernel-side registration follows a deterministic list-driven flow in `KernelData.c`: `InitializeDriverList()` populates `StartupDrivers` (load order) and `Drivers` (all known descriptors), then `LoadAllDrivers()` walks `StartupDrivers` in order.
PCI-backed class drivers such as `e1000`, `rtl8139`, `rtl8169`, `ahci`, `nvme`, and `xhci` are also inserted in the global known-driver list (`Drivers`) for shell and diagnostics visibility, while their effective load/attach lifecycle remains driven by PCI enumeration.
The `driver <alias>` shell command prints one detailed driver report (identity fields, type, flags, command pointers, command-reported version/capabilities, and enum domains).
The Realtek Ethernet families (`rtl8139`, `rtl8139cplus`, `rtl8169`) share one common kernel layer for register-window setup, reset helpers, MAC plumbing, polling integration, and legacy INTx management through `DeviceInterrupt`, while each family keeps its own datapath and hardware-specific programming model.
`rtl8139` keeps the legacy receive-buffer ring used by pre-CPlus revisions, while `rtl8139cplus` switches to the descriptor-based C+ DMA path (separate RX/TX rings) even though both revisions expose the same PCI device identifier and PHY register set.
That shared layer also centralizes DWORD multicast-filter initialization and optional deferred interrupt acknowledgment for controller families whose receive-overflow handling must update device state before clearing selected ISR bits.

The NVMe driver initializes admin queues first, then I/O queues, configures completion interrupts through MSI-X when available, enumerates namespaces, and registers each namespace as a disk so `MountDiskPartitions` can attach file systems.


### Input device stack

Mouse input is centralized in `kernel/source/MouseCommon.c`, which buffers deltas/buttons before forwarding them to `kernel/source/input/MouseDispatcher.c` for cursor movement, throttled mouse messages, and desktop cursor updates. `MouseDispatcher` also exposes a diagnostic serpentine sweep mode controlled through a dedicated syscall, so desktop-pipeline tests can drive pointer motion without depending on emulator-side mouse injection. USB HID mouse support (`kernel/source/drivers/Mouse-USB.c`) takes priority over the serial mouse when a compatible USB device is present. The USB mouse driver mirrors the USB keyboard execution model: device discovery stays in deferred polling, while report processing follows xHCI interrupts in normal mode and falls back to deferred polling only when the global polling configuration is enabled.

Keyboard selection is handled by the keyboard selector driver, keeping one active keyboard path at a time while sharing the same higher-level input/message routing model.

### USB host and class stack

#### Overview

USB foundations are defined in `kernel/include/drivers/USB.h`. This header provides shared type definitions for speed tiers, endpoint kinds, addressing, and standard descriptor layouts used by host-controller and class drivers.

Driver timeout and retry-delay constants shared across storage, USB, and graphics drivers are centralized in `kernel/include/Driver.h`, with units encoded in the constant names (`_MS`, `_LOOPS`, `_ITER`, `_POLLS`). Driver code therefore keeps policy and flow control locally, while shared numerical values stay in one declaration point.

#### xHCI host stack

The xHCI host stack is split across:

- `kernel/source/drivers/usb/XHCI-Core.c`
- `kernel/source/drivers/usb/XHCI-Controller.c`
- `kernel/source/drivers/usb/XHCI-Device-Lifecycle.c`
- `kernel/source/drivers/usb/XHCI-Device-Transfer.c`
- `kernel/source/drivers/usb/XHCI-Device-Enum.c`
- `kernel/source/drivers/usb/XHCI-Hub.c`
- `kernel/source/drivers/usb/XHCI-Enum.c`

It is attached by the PCI subsystem and performs:

- controller halt, reset, and run sequencing,
- MMIO mapping and ring allocation for the DCBAA, command ring, and event ring,
- interrupter programming,
- EP0 control transfers for enumeration,
- topology construction for devices, configurations, interfaces, and endpoints,
- operational reporting through `usbctl ports`, `usbctl probe`, and `usbctl devices`.

USB interfaces and endpoints are kernel objects stored in global lists. Class drivers hold references to those objects so teardown is deferred until hotplug release is safe.

#### Transfer path and lifecycle

Class drivers reuse the same xHCI high-level transfer helpers for normal-transfer submission and transfer-event completion matching. Before a fresh single-TRB transfer is armed on one endpoint, cached transfer events for the same slot/DCI route are purged so late completions from a previous timeout or recovery cycle cannot be misassociated with the new transfer.

Doorbell writes publish ring and context memory updates through an explicit barrier before the MMIO write so the controller observes fully visible producer state.

Disconnect handling is staged:

- stop and reset endpoints,
- flush transfer rings,
- disable slot context,
- release resources after object references drain.

Root-port probe failures are rate-limited in the xHCI diagnostics path so one unstable port does not flood the kernel log and hide unrelated USB activity. Repeated failures on one root port while its filtered `PORTSC` state stays unchanged are counted through the reusable `utils/FailureGate` helper. After the threshold is reached, the port is blacklisted and re-enumeration is deferred until the port state changes or the device disconnects.

#### Hub-class devices

Hub-class devices are supported through descriptor parsing, port power management, downstream tracking, and interrupt-endpoint polling for port-change driven reset and re-enumeration.

Interrupt endpoint contexts derive interval, Max Burst, and Max ESIT payload from the descriptor encoding required by the endpoint speed tier. Bulk endpoint contexts use a non-zero Average TRB Length aligned with the xHCI recommended initial value for bulk transfers.

#### USB mass storage

USB mass storage support is implemented in `kernel/source/drivers/storage/USBStorage.c`, `kernel/source/drivers/storage/USBStorage-Transport.c`, and `kernel/source/drivers/storage/USBStorage-SCSI.c`. Device discovery runs from a deferred poll callback that scans xHCI-managed USB devices, selects interfaces matching Mass Storage class, SCSI subclass, and BOT protocol, rejects UAS, resolves one bulk IN endpoint and one bulk OUT endpoint, then starts a per-device `USB_MASS_STORAGE_DEVICE` context.

Startup configures the bulk endpoint pair in xHCI, allocates one page-sized shared I/O buffer, issues SCSI `INQUIRY`, then issues `READ CAPACITY(10)`. Capacity parsing accepts 512-byte and 4096-byte logical blocks and rejects devices beyond the `READ CAPACITY(10)` range. A successful device is registered both in the global disk list and in `Kernel.USBDevice`, with mount deferred until `FileSystemReady()` when needed.

The BOT transport builds one CBW, optional data stage, and one CSW in the shared buffer. Transport code validates CSW signature, tag, residue bounds, and status values. Phase errors, invalid CSWs, and transport failures trigger BOT reset recovery through the class-specific reset request followed by endpoint halt clear on both bulk pipes. A stalled data stage or CSW stage is handled through endpoint halt clear and bounded retry flow. Completion waits use xHCI transfer completion matching with timeout handling, endpoint reset on timeout or stall, and rate-limited debug traces for repetitive `READ(10)` and `WRITE(10)` traffic.

SCSI command helpers cover `INQUIRY`, `TEST UNIT READY`, `REQUEST SENSE`, `READ CAPACITY(10)`, `READ(10)`, `WRITE(10)`, and `SYNCHRONIZE CACHE(10)`. Each disk write request flushes the medium cache before completion. When `REQUEST SENSE` reports `UNIT ATTENTION` or `NOT READY`, the driver runs BOT reset recovery, waits for `TEST UNIT READY`, refreshes inquiry/capacity data, updates the exported disk geometry, and retries the failed read/write or cache-flush request once.

Disk I/O goes through one shared validation and chunking path. `DF_DISK_READ` and `DF_DISK_WRITE` validate geometry, readiness, access flags, buffer size, and present-state, then split transfers into page-sized `READ(10)` or `WRITE(10)` requests. On removal, the driver detaches mounted and unused filesystems associated with the storage unit, unregisters the list entry, releases USB references and I/O buffers, and broadcasts `ETM_USB_MASS_STORAGE_MOUNTED` or `ETM_USB_MASS_STORAGE_UNMOUNTED` to process message queues.

### Console

#### Overview

Console rendering is a kernel-owned display frontend. Text drawing is dispatched through the active graphics backend whenever framebuffer console output is available.

Display ownership is tracked in `kernel/source/desktop/DisplaySession.c` through `DISPLAY_SESSION` stored in `KERNEL_DATA`. This state records the active frontend (`console` or `desktop`), the active desktop pointer, the selected graphics driver, and the active mode. Frontend transitions are performed by `DisplaySwitchToConsole()` and `DisplaySwitchToDesktop()`, with backend ownership preserved across frontend changes.

#### Console rendering path

Console text output uses backend-dispatched text commands implemented in `kernel/source/Console-TextOps.c`. Glyph, region, and cursor operations are forwarded through `DF_GFX_TEXT_*` commands on the active graphics backend:

- `TEXT_PUTCELL`
- `TEXT_CLEAR_REGION`
- `TEXT_SCROLL_REGION`
- `TEXT_SET_CURSOR`
- `TEXT_SET_CURSOR_VISIBLE`

When `setGraphicsDriver(driverAlias, width, height, bpp)` applies a graphics mode while the shell is in the console frontend, display session routing uses the selected backend for console rendering and recomputes console cell geometry from the active pixel mode. Shell output stays visible across backend and mode transitions.

The console text dispatch path caches the active `GRAPHICSCONTEXT` while the console frontend is bound to the same graphics driver. That cache is invalidated when console mode or framebuffer mapping changes so boot log rendering does not repeatedly call `DF_GFX_GETCONTEXT`.

The dispatch path also exposes `ConsoleIsFramebufferMappingInProgress()`. Split-debug log mirroring uses that state to suppress recursive console writes during framebuffer context acquisition.

Console state is tracked independently from the active backend through one console-owned shadow text buffer. This canonical cell buffer stores characters and text attributes for the full console grid, allowing region repaint and lock-screen state restoration even when the active backend only exposes glyph drawing and scrolling commands.

#### Synchronization and fallback

Console synchronization uses dedicated lock domains in addition to the legacy compatibility lock:

- `MUTEX_CONSOLE_STATE` protects mutable console state such as cursor position, regions, and paging state.
- `MUTEX_CONSOLE_RENDER` protects backend rendering critical sections.

When both locks are needed, acquisition order is always state first, render second. Console paging input wait paths must run without any console mutex held so split and non-split console flows do not amplify lock hold times or stall on input.

Emergency text fallback is isolated in `kernel/source/Console-VGATextFallback.c`. This path is used when console frontend activation cannot complete through the active graphics backend. It requests VGA text mode through the VGA driver command interface, then keeps all text cells, region operations, and cursor updates on the delegated backend path.

Console metadata differs by firmware path:

- BIOS/MBR uses VGA text memory metadata for text mode operation.
- UEFI uses GOP-provided framebuffer metadata through the selected graphics backend.
- Emergency fallback is isolated to `kernel/source/Console-VGATextFallback.c`.

#### Fonts and userland text

The default font is the in-tree ASCII 8x16 EXOS font and can be replaced through the font API. The shared font layer separates font-face metrics and raster access (`FONT_FACE`) from the bitmap glyph-set path (`FONT_GLYPH_SET`). Console rendering uses that abstraction boundary for font access.

Userland text rendering uses the same higher-level text path. `SYSCALL_DrawText` / `SYSCALL_MeasureText` and the runtime wrappers `DrawText` / `MeasureText` expose text drawing and measurement to userland. `Font = 0` selects the default kernel font.

### Graphics

#### Backend contract

`kernel/include/GFX.h` defines the backend-facing graphics command contract used by selectors, desktop code, shell tools, and console text dispatch.

The same contract also defines optional backend operations for modern display handling:

- capabilities and outputs: `GETCAPABILITIES`, `ENUMOUTPUTS`, `GETOUTPUTINFO`
- presentation and synchronization: `PRESENT`, `WAITVBLANK`
- surfaces and scanout: `ALLOCSURFACE`, `FREESURFACE`, `SETSCANOUT`
- text and measurement: `TEXT_DRAW`, `TEXT_MEASURE`
- mouse cursor: `CURSOR_SET_SHAPE`, `CURSOR_SET_POSITION`, `CURSOR_SET_VISIBLE`

Backends that do not support optional commands return `DF_RETURN_NOT_IMPLEMENTED`.

Graphics drivers expose mode enumeration through `DF_GFX_GETMODECOUNT` and `DF_GFX_GETMODEINFO`. `GRAPHICS_MODE_INFO.ModeIndex` selects a specific mode, while `INFINITY` targets the active mode.

#### Backend selection and diagnostics

Graphics backend selection is implemented in `kernel/source/drivers/graphics/common/Graphics-Selector.c`. The selector loads available backends, filters inactive or unusable ones, scores remaining candidates, and forwards `DF_GFX_*` commands to the selected backend. This provides deterministic backend selection for desktop code.

Boot-path capability gating is centralized in `utils/BootPath` (`kernel/include/utils/BootPath.h`, `kernel/source/utils/BootPath.c`). VESA probing is disabled on x86-64 boot paths and enabled on x86-32 boot paths. On x86-32, backend availability is decided by the VESA initialization and probe path.

Explicit backend forcing through `setGraphicsDriver(driverAlias, width, height, bpp)` uses a strict selector path: only the requested backend is loaded into selector state, and command forwarding targets that backend while forced mode is active.

Shell graphics switching crosses the user/kernel boundary through `SYSCALL_SetGraphicsDriver`. The syscall payload is `GRAPHICS_DRIVER_SELECTION_INFO`, which carries one inline `DriverAlias[MAX_NAME]` buffer and the requested width, height, and bits per pixel. The shell `setGraphicsDriver(...)` host function only marshals arguments into that ABI payload and invokes the syscall.

Generic driver diagnostics use `DF_DEBUG_INFO` with `DRIVER_DEBUG_INFO.Text`, a multi-line buffer sized with `MAX_STRING_BUFFER`. Graphics backends use that interface to expose backend alias and current resolution, and the graphics selector forwards the query to the active backend. Mouse drivers expose the selected manufacturer and product through the same pattern, and the mouse selector forwards that data as well.

#### VESA and VGA

The VESA driver requests VBE modes in linear frame buffer mode (`INT 10h 4F02h`, bit 14), validates linear frame buffer capability, and maps `PhysBasePtr` through `MapIOMemory`. Console rendering writes directly to mapped VRAM. Desktop composition can instead draw into a desktop-owned shadow buffer and use `DF_GFX_PRESENT` to copy one dirty rectangle to the mapped scanout.

VESA drawing primitives include line, rectangle, arc, and triangle command paths (`DF_GFX_LINE`, `DF_GFX_RECTANGLE`, `DF_GFX_ARC`, `DF_GFX_TRIANGLE`) and are forwarded through `Graphics-Selector`.

Rectangle, triangle, and arc rasterization share the generic scanline helpers in `kernel/source/utils/Graphics-Utils.c`. Solid fills, vertical gradients, horizontal gradients, filled arcs, and rounded-corner rectangles all converge on the same scanline entry so shape composition stays backend-agnostic. Rounded rectangles accept `RECT_CORNER_RADIUS_AUTO` in `RECT_INFO.CornerRadius`, which resolves to half of the rectangle's smallest dimension. `RECT_CORNER_RADIUS_AUTO_LIMIT(MaximumRadius)` keeps auto sizing but clamps the resolved radius to one maximum. Themes may apply the same behavior with `corner_radius = token:metric.corner_radius.auto` plus `corner_radius_limit = <value>`.  Desktop themes may also set `corner_style`, using literals or tokens that resolve to `square`, `rounded`, or `bevel`. The low-level contiguous pixel write path is provided by the architecture `GraphicsDrawScanlineAsm` helper in `kernel/source/arch/x86-32/asm/System.asm` and `kernel/source/arch/x86-64/asm/System.asm`. Solid `ROP_SET` scanlines and desktop present row blits use dedicated SSE2 fast paths when the architecture setup enables XMM instructions.

`PEN_INFO` and `PEN` carry `Width` in addition to color and pattern. `LINE` applies the selected pen width through the shared line rasterizer. Closed shapes apply the selected pen width inward from the outer contour, so rectangle, arc, and triangle outlines stay inside the requested geometry.

`kernel/source/drivers/graphics/vga/VGA-Main.c` exposes a dedicated VGA text driver (`alias: vga`) that implements mode enumeration, context retrieval, text cell output, region clear and scroll, and hardware cursor updates through the same `DF_GFX_*` contract. Console code no longer accesses VGA text memory or VGA cursor ports directly.
When the boot path exposes a multiboot text framebuffer, console text dispatch resolves directly to the VGA driver instead of the graphics selector so early boot logging cannot recurse through backend probing.

Display-class PCI attach logic is implemented in `kernel/source/drivers/graphics/common/Graphics-PCI.c`. The PCI bus layer registers this graphics-provided attach driver during PCI initialization so generic display controllers appear in the PCI device list.

#### Intel native backend

The Intel native backend is split by responsibility:

- `kernel/source/drivers/graphics/igpu/iGPU-Base.c`: load, dispatch, and PCI attach
- `kernel/source/drivers/graphics/igpu/iGPU-Mode.c`: takeover and native modeset flow
- `kernel/source/drivers/graphics/igpu/iGPU-Present.c`: CPU drawing and surfaces
- `kernel/source/drivers/graphics/igpu/iGPU-Text.c`: text operations
- `kernel/source/drivers/graphics/igpu/iGPU-Cursor.c`: hardware cursor plane operations
- `kernel/source/drivers/graphics/igpu/iGPU-Interrupt.c`: vblank synchronization and frame pacing

Capability discovery is centralized in an internal `INTEL_GFX_CAPS` object built from a PCI device-id family table and refined with bounded MMIO register probes such as display version, pipe presence, and port mask. Public `GFX_CAPABILITIES` values returned by `DF_GFX_GETCAPABILITIES` are projected from that single capability object.

The takeover path reads active pipe and plane state from display registers, maps the active scanout buffer through the aperture BAR, builds a `GRAPHICSCONTEXT` from the discovered mode, and then serves window-manager drawing either directly to scanout or through a desktop-owned shadow context followed by `DF_GFX_PRESENT`.

The native `DF_GFX_SETMODE` path in `kernel/source/drivers/graphics/igpu/iGPU-Mode.c` follows an ordered sequence: disable, route, clock, link, enable, verify. It applies explicit pipe, output, and transcoder routing policy, uses conservative generation-aware clock handling, includes eDP panel and backlight stabilization hooks, and rolls back to a captured hardware snapshot when a partial modeset stage fails.

On hybrid platforms without active scanout takeover, the Intel backend can be loaded through explicit backend forcing. In that case `DF_GFX_SETMODE` performs a conservative cold modeset bootstrap using requested timings, pipe and output programming, link setup, and context rebuild from programmed state. `DF_GFX_GETMODECOUNT` and `DF_GFX_GETMODEINFO` expose a deterministic 32-bpp catalog assembled from the active mode, the firmware boot framebuffer mode, and a conservative list filtered by Intel capability bounds.

The modeset core resolves explicit `INTEL_DISPLAY_FAMILY_OPS` descriptors from display version so stride encoding and decoding, plane tiling policy, and cold-modeset support remain family-specific and extension-ready without hardwired device-id control flow. Diagnostics record explicit failure state in `INTEL_GFX_STATE` through `LastModesetFailureStage` and `LastModesetFailureCode`.

VBlank synchronization is implemented in `kernel/source/drivers/graphics/igpu/iGPU-Interrupt.c`. `DF_GFX_WAITVBLANK` performs bounded waits with `HasOperationTimedOut()` and rate-limited timeout diagnostics. Presentation serialization uses `PresentMutex`, and frame pacing tracks `PresentFrameSequence` and `VBlankFrameSequence` with optional `PIPESTAT` vblank handling and scanline polling fallback.

Hardware pointer support uses the generic cursor contract in `kernel/include/GFX.h`. `DF_GFX_GETCAPABILITIES` exposes `HasCursorPlane`, while `DF_GFX_CURSOR_SET_SHAPE`, `DF_GFX_CURSOR_SET_POSITION`, and `DF_GFX_CURSOR_SET_VISIBLE` drive one conservative Intel 64x64 ARGB cursor plane on the active pipe. The backend stores cursor shape, hotspot, position, and visibility in `INTEL_GFX_STATE`, reapplies that state after mode activation, and exposes the last cursor failure reason through `DF_DEBUG_INFO` so desktop code can fall back deterministically to software overlay rendering.

#### Desktop, cursor, and overlay

Mouse pointer operations are part of the same graphics backend contract, and cursor ownership is managed by kernel desktop code. The desktop selects either a hardware cursor path or a software overlay path depending on cursor-plane support and available cursor commands.

Desktop cursor runtime state is tracked per desktop and managed by `kernel/source/desktop/Desktop-Cursor.c`. That state includes position, pending target position, software-dirty state, visibility, clipping rectangle, active path, and fallback reason.

Desktop graphics composition uses one per-desktop shadow buffer in graphics mode. `BeginWindowDraw()` resolves the desktop shadow context for window content, while the structured desktop draw dispatcher submits each completed dirty clip through `DF_GFX_PRESENT` after non-client and client rendering finish for that clip. This keeps the text console on direct framebuffer rendering while desktop windows gain one stable destination buffer suitable for stable software composition.

Shared geometry and damage tracking are centralized:

- rectangle intersection and screen or window coordinate conversion live in `kernel/source/utils/Graphics-Utils.c` and `kernel/include/utils/Graphics-Utils.h`
- visible-region construction and subtree subtraction live in `kernel/source/desktop/Desktop-VisibleRegion.c`
- overlay invalidation helpers live in `kernel/source/desktop/Desktop-OverlayInvalidation.c` and `kernel/include/desktop/Desktop-OverlayInvalidation.h`

Occlusion, clipping, bounded screen damage, and window composition use one shared implementation path across desktop subsystems. Software cursor overlay rendering stays outside the desktop shadow buffer and is emitted on the final scanout context after window present, so the cursor path remains compatible with hardware-cursor backends and does not become part of the desktop composition buffer.

### Early boot console

`kernel/source/console/Console-EarlyBoot.c` provides a minimal framebuffer text path independent from normal console initialization. It writes glyphs through physical framebuffer mappings and is used for early boot and memory-initialization checkpoints.

Bootloader text mode handoff preserves one logical cursor position through the multiboot `config_table` field using one EXOS-owned configuration block. When the first regular console backend activates, it imports that bootloader cell position once, clamps it to the active console geometry, and then continues with the standard console-owned cursor state for later frontend and backend transitions.

### ACPI services

#### Overview

Advanced power-management and reset paths live in `kernel/source/ACPI.c`. The module discovers ACPI tables, exposes the parsed configuration, and provides helpers for platform control.

#### Power-off

`ACPIShutdown()` releases ACPI mappings and state without powering off.

`ACPIPowerOff()` enters the S5 soft-off state using the `_S5` sleep type extracted from the DSDT when available, with default value `7` when that information is absent. The routine also includes alternate power-off sequences for systems where the ACPI power-off path does not complete.

#### Reboot

`ACPIReboot()` performs a warm reboot by using the ACPI reset register when present and then chaining to legacy reset controllers. This path covers systems that expose the ACPI reset register as well as older chipsets that require a non-ACPI reset sequence.

#### Kernel wrappers

Kernel-level wrappers `ShutdownKernel()` and `RebootKernel()` drive shell commands, clear userland processes, then kernel tasks, and perform reverse-order driver unload before handing control to the ACPI routines. This shutdown ordering reduces the amount of subsystem state left pending when the machine powers off or reboots.

### Disk interfaces

```
+------------------------------------+
|          Operating System          |
+------------------------------------+
                |
                | (Software Drivers)
                v
+------------------------------------+
| Controllers/Protocols              |
| +--------+  +--------+  +--------+ |
| |  AHCI  |  |  NVMe  |  |  SCSI  | |
| +--------+  +--------+  +--------+ |
|      |           |           |     |
|      v           v           v     |
| +--------+  +--------+  +--------+ |
| |  SATA  |  |  PCIe  |  |  SAS/  | |
| |Interface| |Interface| |SATA Int| |
| +--------+  +--------+  +--------+ |
|      |           |           |     |
|      v           v           v     |
| +--------+  +--------+  +--------+ |
| | HDD,   |  | SSD    |  | HDD,   | |
| | SSD    |  | NVMe   |  | SSD    | |
| | (SATA) |  |        |  | (SAS/  | |
| |        |  |        |  | SATA)  | |
| +--------+  +--------+  +--------+ |
+------------------------------------+
```

**AHCI interrupt policy**: the SATA driver registers the controller with the shared `DeviceInterruptRegister` infrastructure and installs dedicated top and bottom halves so IRQ 11 traffic can be routed through a private slot when the hardware gets its own vector (MSI/MSI-X or a non-shared INTx line). Commands complete synchronously, therefore all AHCI per-port interrupt masks (`PORT.ie`) and the global `GHC.IE` bit are cleared in shipping builds so the shared IRQ 11 line stays quiet for the `E1000` NIC.
Disk drivers expose `BytesPerSector` through `DF_DISK_GETINFO` (`DISKINFO.BytesPerSector`). Partition probing in `FileSystem.c` consumes this value and accepts 512-byte and 4096-byte sectors when reading MBR/GPT and signature data.

## Storage and Filesystems

### File systems

#### Global state and object model

File system state is split between:
- `Kernel.FileSystem`: mounted `FILESYSTEM` objects,
- `Kernel.UnusedFileSystem`: discovered but non-mounted `FILESYSTEM` objects,
- `Kernel.FileSystemInfo`: global metadata (`ActivePartitionName`),
- `Kernel.SystemFS`: virtual filesystem wrapper (`SYSTEMFSFILESYSTEM`) used as the global path entry point.

Each `FILESYSTEM` object carries runtime fields (`Driver`, `StorageUnit`, `Mounted`, `Mutex`, `Name`) plus partition metadata in `PARTITION` (`Scheme`, `Type`, `Format`, `Index`, `Flags`, `StartSector`, `NumSectors`, `TypeGuid`).

`FileSystemGetStorageUnit()` and `FileSystemHasStorageUnit()` expose backing storage uniformly for disk-backed and virtual filesystems. Display helpers (`FileSystemGetPartitionSchemeName`, `FileSystemGetPartitionTypeName`, `FileSystemGetPartitionFormatName`) centralize partition labeling.

#### Discovery and mount pipeline

`InitializeFileSystems()` is the main orchestration path:
1. Clear `ActivePartitionName`.
2. Release stale entries from `Kernel.UnusedFileSystem`.
3. Scan `Kernel.Disk` and call `MountDiskPartitions()` for each storage unit.
4. Select an active partition by searching for `exos.toml`/`EXOS.TOML` (`FileSystemSelectActivePartitionFromConfig()`).
5. Build and mount SystemFS (`MountSystemFS()`).
6. Load kernel configuration (`ReadKernelConfiguration()`).
7. Apply configured user mounts (`MountUserNodes()` via `SystemFS.Mount.<index>.*` keys).
8. Resolve logical kernel paths through `KernelPath.<name>` keys when subsystems request configured file or folder locations.

Logical kernel path keys are consumed through `utils/KernelPath`:
- `KernelPath.UsersDatabase`: absolute VFS file path used by user account persistence.
- `KernelPath.KeyboardLayouts`: absolute VFS folder path used to load keyboard layout files (`<layout>.ekm1`).
- `KernelPath.SystemAppsRoot`: absolute VFS folder path used by shell package-name resolution (`package run <name>`).
- `KernelPath.Binaries.<index>.Path`: repeatable absolute VFS folder path searched by the shell for executable command names after the current-folder resolution fails; resolved through the generic `KernelPathResolveListEntry(KERNEL_PATH_LIST_BINARY, ...)` helper.

`MountDiskPartitions()` handles MBR and switches to GPT parsing when a protective MBR entry (`0xEE`) is detected. Supported formats are mounted through dedicated drivers (FAT16/FAT32/NTFS/EXFS/EXT2 path); partition metadata is written with `SetFileSystemPartitionInfo()`. Non-mounted partitions are materialized through `RegisterUnusedFileSystem()` so diagnostics and shell tooling can inspect them.

When SystemFS is ready (`FileSystemReady()`), newly mounted filesystems are attached into SystemFS under `/fs/<volume>` through `SystemFSMountFileSystem()`.

The RAM disk driver initializes a small in-memory disk and formats it with EXT2 through the filesystem `DF_FS_CREATEPARTITION` command. EXT2 formatting populates a minimal superblock, group descriptor, bitmaps, inode table, and root directory.

#### Mounted volume naming

Mounted partition names are generated by `GetDefaultFileSystemName()` (`kernel/source/fs/FileSystem.c`) and exposed under `/fs/<volume>`.

Format:
- `<prefix><disk_index>p<partition_index>`

Prefix by storage driver type:
- `r` for RAM disks (`DRIVER_TYPE_RAMDISK`)
- `f` for floppy disks (`DRIVER_TYPE_FLOPPYDISK`)
- `u` for USB mass storage (`DRIVER_TYPE_USB_STORAGE`)
- `n` for NVMe storage (`DRIVER_TYPE_NVME_STORAGE`)
- `s` for SATA/AHCI storage (`DRIVER_TYPE_SATA_STORAGE`)
- `a` for ATA storage (`DRIVER_TYPE_ATA_STORAGE`)
- `d` for all other disk drivers (fallback)

Index rules:
- `disk_index` is zero-based and counted among disks of the same driver type.
- `partition_index` is zero-based and comes from the partition enumeration path (MBR slot index or GPT entry index).

Examples:
- `/fs/s0p0`
- `/fs/n1p0`
- `/fs/u0p0`
- `/fs/f0p0`
- `/fs/r0p0`

#### EPK package format

The EPK package binary layout is frozen for parser/tooling integration in:
- `doc/guides/binary-formats/epk.md`
- `kernel/include/package/EpkFormat.h`
- `kernel/include/package/EpkParser.h`
- `kernel/source/package/EpkParser.c`

The format is a strict on-disk contract:
- fixed 128-byte header (`EPK_HEADER`) with explicit section offsets/sizes and package hash,
- TOC section (`EPK_TOC_HEADER` + `EPK_TOC_ENTRY` records + variable UTF-8 path blobs),
- block table section (`EPK_BLOCK_ENTRY` records for compressed chunks),
- dedicated manifest blob (`manifest.toml`) and optional detached signature blob.

Compatibility is fail-closed by contract:
- unknown flags or unsupported version are rejected,
- reserved fields must stay zero,
- malformed section bounds/order are rejected with deterministic validation status codes.

Step-3 parser/validator behavior:
- validates header layout and section bounds/order before deeper parsing,
- parses TOC entries and block table into kernel-side parsed descriptors,
- validates package hash (`SHA-256`) over package bytes excluding signature region,
- optionally validates detached signature blob through `utils/Signature`,
- returns stable validation status codes and logs explicit parse failures with function-tagged error messages.

#### ELF executable module ABI

The first EXOS userland loadable module ABI is defined in:
- `doc/guides/binary-formats/executable-module-elf.md`

The ABI freezes:
- the accepted `ET_DYN` ELF subset for modules;
- required and rejected program header combinations;
- the narrow relocation and TLS contract for the first milestone;
- deterministic kernel rejection categories for unsupported module binaries.

#### Process user address space arenas

User processes use `Process-Arena` (`kernel/include/process/Process-Arena.h`, `kernel/source/process/Process-Arena.c`) to keep virtual address space responsibilities separated:
- `Image` covers the main executable image lane;
- `Heap` remains the process general allocation lane and its growth is capped to the heap arena limit;
- `Stack` remains the task stack lane and allocates from the top of its reserved range;
- `Module` is a dedicated lane for runtime-loaded executable modules;
- `Mmio` remains reserved for explicit device mappings;
- `System` remains reserved for process-owned service mappings such as message queues.

The module lane exists independently from the main executable and heap:
- module mappings do not consume heap growth space;
- module allocations stay below the `Mmio` and `System` reservations;
- the initial process heap is reconfigured through `ProcessArenaConfigureMainHeap(...)` so `HeapInit(...)` growth stops before the stack/module lanes;
- `ProcessArenaAllocateModule(...)` provides one entry point for module-related mappings and accepts explicit purposes for shared segments, private writable data, TLS storage, and bookkeeping pages.

Executable modules are exposed to userland through `LoadModule(...)`, `GetModuleSymbol(...)`, and `ReleaseModule(...)` runtime wrappers backed by dedicated syscalls. The syscall layer owns path-based file opening and current-process handle export; `exec/ExecutableModule` owns the shared image cache; `process/Process-Module` owns process-local bindings, mapping, relocation, TLS registration, symbol lookup, and process teardown cleanup. Symbol lookup returns installed user addresses from a binding owned by the caller process only. General runtime unload is rejected with `DF_RETURN_NOT_IMPLEMENTED` until dependency tracking, constructor/destructor ordering, and in-module execution quiescence have a complete contract.

Module TLS blocks are installed per task. The hardware user TLS selector/base exposes the compiler ABI thread pointer for the first module TLS block, while the user-visible module TLS control block remains mapped for kernel bookkeeping and future runtime enumeration. Initial-exec `TPOFF` relocations are resolved relative to the module TLS block size.

#### PackageFS readonly mount

Step-4 introduces a dedicated PackageFS module:
- `kernel/include/package/PackageFS.h`
- `kernel/source/package/PackageFS.c`
- `kernel/source/package/PackageFS-Tree.c`
- `kernel/source/package/PackageFS-File.c`
- `kernel/source/package/PackageFS-Mount.c`

PackageFS mounts one validated `.epk` archive as a virtual read-only filesystem:
- mount entry point: `PackageFSMountFromBuffer(...)`,
- unmount entry point: `PackageFSUnmount(...)`,
- TOC tree materialization for files, folders, and folder aliases,
- wildcard folder enumeration through `DF_FS_OPENFILE` + `DF_FS_OPENNEXT`,
- write-class operations (`create`, `delete`, `rename`, `write`, `set volume info`) rejected with `DF_RETURN_NO_PERMISSION`,
- unmount refused when open handles still reference the mounted package,
- block-backed file reads mapped to table ranges with on-demand per-chunk decompression,
- per-chunk SHA-256 validation against block table hashes before serving data,
- bounded decompressed chunk caching through `utils/ChunkCache`, with cleanup-based eviction and full invalidation during unmount.

#### Package namespace integration

Step-6 namespace integration is implemented by:
- `kernel/include/package/PackageNamespace.h`
- `kernel/source/package/PackageNamespace.c`

Package integration targets per-process launch behavior:
- packaged application receives a private `/package` mount,
- packaged application receives `/user-data` alias to `/users/<current-user>/<package-name>/data`,
- no global application package mount graph is required for launch.

Process-view hooks:
- `PackageNamespaceBindCurrentProcessPackageView(...)` mounts one package view at `/package`.
- the same helper maps `/user-data` to `/users/<current-user>/<package-name>/data` on the active filesystem.
- package namespace roots and aliases are resolved through `utils/KernelPath` keys:
  - `KernelPath.UsersRoot`
  - `KernelPath.CurrentUserAlias`
  - `KernelPath.PrivatePackageAlias`
  - `KernelPath.PrivateUserDataAlias`

#### Package manifest compatibility checks

Step-7 manifest resolution is implemented by:
- `kernel/include/package/PackageManifest.h`
- `kernel/source/package/PackageManifest.c`

Launch validation flow includes:
- parse and validate embedded `manifest.toml` (identity + compatibility fields),
- enforce architecture and kernel compatibility policy before mount activation,
- reject incompatible packages with deterministic diagnostics.

Manifest model is strict and dependency-free:
- required keys: `name`, `version`, `arch`, `kernel_api`, `entry`,
- optional table: `[commands]` (`command-name -> package-relative executable path`),
- accepted architecture values: `x86-32`, `x86-64`,
- `kernel_api` compatibility policy: `required.major == kernel.major` and `required.minor <= kernel.minor`,
- `provides` and `requires` keys are rejected.

No dependency solver behavior is part of this model:
- no provider graph,
- no transitive dependency resolution,
- no global package activation transaction state for application launches.

Launch target rules:
- `entry` is the default launch target for the package.
- `commands.<name>` exposes additional named launch targets for multi-binary packages.
- command-name collisions do not use implicit priority; ambiguous matches fail with explicit diagnostics.

Command resolution without package name is deterministic:
1. path token (contains `/`) runs as direct path,
2. user alias namespace (`/users/<user>/commands/<name>`),
3. system alias namespace (`/system/commands/<name>`),
4. package command index (`commands.<name>` across known packages),
5. on multiple package matches, launch is rejected as ambiguous.

#### Package launch flow

Step-8 launch activation is wired in shell launch path (`SpawnExecutable`):
- when target extension is `.epk`, shell reads package bytes from disk,
- package manifest is parsed and compatibility-checked before activation,
- package is mounted through `PackageFSMountFromBuffer(...)`,
- package aliases are bound through `PackageNamespaceBindCurrentProcessPackageView(...)`,
- manifest `entry` is executed from `/package/...`,
- launch failures trigger explicit unbind/unmount cleanup with no partial mounted leftovers,
- background launches keep mounted package filesystem attached to process state and release it during process teardown.

Shell package command:
- `package run <package-name> [command-name] [args...]` resolves package file from `KernelPath.SystemAppsRoot`,
- if `command-name` matches `manifest.commands.<name>`, that target is launched,
- otherwise launch falls back to `manifest.entry` and keeps the token as the first application argument.
- `package list <package-name|path.epk>` validates/mounts one package and lists manifest metadata plus package tree content.
- `package add <package-name|path.epk>` validates source package and copies it to `KernelPath.SystemAppsRoot` under `<manifest.name>.epk`.

#### Runtime access paths

`OpenFile()` takes two routing paths:
- absolute path (`/...`): delegated to SystemFS (`DF_FS_OPENFILE`), which can traverse mounted nodes and forward to backing filesystems;
- non-absolute path: probes mounted filesystems in `Kernel.FileSystem` until one resolves the file.

The file layer synchronizes filesystem list access with `MUTEX_FILESYSTEM`, but `DF_FS_OPENFILE` probes run outside that global lock using retained filesystem references from a short-lived snapshot. Open handles are tracked in `Kernel.File` with per-file ownership and reference management (`OwnerTask`, `OpenFlags`, refcount).

#### Removable storage behavior

USB mass storage hot-plug integrates with the same pipeline:
- on attach, `USBMassStorageStartDevice()` calls `MountDiskPartitions()` only when `FileSystemReady()` is true;
- on detach, `USBMassStorageDetachFileSystems()` unmounts from SystemFS (`SystemFSUnmountFileSystem()`), releases mounted and unused filesystem objects for that disk, and clears `ActivePartitionName` when the removed volume was active.

Mount and unmount notifications are broadcast to processes (`ETM_USB_MASS_STORAGE_MOUNTED` / `ETM_USB_MASS_STORAGE_UNMOUNTED`).


### EXOS File System - EXFS

#### Structure of the Master Boot Record

| Offset   | Type | Description                                  |
|----------|------|----------------------------------------------|
| 0..445   | U8x? | The boot sequence                            |
| 446..461 | ?    | CHS location of partition No 1               |
| 462..477 | ?    | CHS location of partition No 2               |
| 478..493 | ?    | CHS location of partition No 3               |
| 494..509 | ?    | CHS location of partition No 4               |
| 510      | U16  | BIOS signature : 0x55AA (`_*_*_*_**_*_*_*_`) |

---

#### Structure of SuperBlock

The SuperBlock is always **1024 bytes** in size.

| Offset | Type   | Description                                   |
|--------|--------|-----------------------------------------------|
| 0      | U32    | Magic number, must be `"EXOS"`                |
| 4      | U32    | Version (high word = major, low word = minor) |
| 8      | U32    | Size of a cluster in bytes                    |
| 12     | U32    | Number of clusters                            |
| 16     | U32    | Number of free clusters                       |
| 20     | U32    | Cluster index of cluster bitmap               |
| 24     | U32    | Cluster index of bad cluster page             |
| 28     | U32    | Cluster index of root FileRecord ("/")        |
| 32     | U32    | Cluster index of security info                |
| 36     | U32    | Index in root for OS kernel main file         |
| 40     | U32    | Number of folders (excluding "." and "..")    |
| 44     | U32    | Number of files                               |
| 48     | U32    | Max mount count before check is forced        |
| 52     | U32    | Current mount count                           |
| 56     | U32    | Format of the volume name                     |
| 60–63  | U8x4   | Reserved                                      |
| 64     | U8x32  | Password (optional)                           |
| 96     | U8x32  | Name of this file system's creator            |
| 128    | U8x128 | Name of the volume                            |

---

#### Structure of FileRecord

| Offset | Type   | Description                        |
|--------|--------|------------------------------------|
| 0      | U32    | SizeLow                            |
| 4      | U32    | SizeHigh                           |
| 8      | U64    | Creation time                      |
| 16     | U64    | Last access time                   |
| 24     | U64    | Last modification time             |
| 32     | U32    | Cluster index for ClusterTable     |
| 36     | U32    | Standard attributes                |
| 40     | U32    | Security attributes                |
| 44     | U32    | Group owner of this file           |
| 48     | U32    | User owner of this file            |
| 52     | U32    | Format of name                     |
| 56–127 | U8x?   | Reserved, should be zero           |
| 128    | U8x128 | Name of the file (NULL terminated) |

---

#### FileRecord fields

**Time fields (bit layout):**

- Bits 0..21  : Year (max: 4,194,303)
- Bits 22..25 : Month in the year (max: 15)
- Bits 26..31 : Day in the month (max: 63)
- Bits 32..37 : Hour in the day (max: 63)
- Bits 38..43 : Minute in the hour (max: 63)
- Bits 44..49 : Second in the minute (max: 63)
- Bits 50..59 : Millisecond in the second (max: 1023)

**Standard attributes field:**

- Bit 0 : 1 = folder, 0 = file
- Bit 1 : 1 = read-only, 0 = read/write
- Bit 2 : 1 = system
- Bit 3 : 1 = archive
- Bit 4 : 1 = hidden

**Security attributes field:**

- Bit 0 : 1 = only kernel has access to the file
- Bit 1 : 1 = fill the file's clusters with zeroes on delete

**Name format:**

- 0 : ASCII (8 bits per character)
- 1 : Unicode (16 bits per character)

---

#### Structure of folders and files

- A cluster that contains 32-bit indices to other clusters is called a **page**.
- FileRecord contains a cluster index for its first page.
- A page is filled with cluster indices pointing to file/folder data.
- For folders: data = series of FileRecords.
- For files: data = arbitrary user data.
- The **last entry** of a page is `0xFFFFFFFF`.
- If more than one page is needed, the last index points to the **next page**.

---

#### Clusters

- All cluster pointers are 32-bit.
- Cluster 0 = boot sector (1024 bytes).
- Cluster 1 = SuperBlock (1024 bytes).
- First usable cluster starts at byte 2048.

**Max addressable bytes by cluster size:**

| Cluster size | Max addressable bytes  |
|--------------|-------------------------|
| 1024         | 4,398,046,510,080       |
| 2048         | 8,796,093,020,160       |
| 4096         | 17,592,186,040,320      |
| 8192         | 35,184,372,080,640      |

**Number of clusters formula:**

```
(Disc size in bytes - 2048) / Cluster size
```

Fractional part = unusable space.

**Examples:**

| Disc size               | Cluster size | Total clusters |
|-------------------------|--------------|----------------|
| 536,870,912 (500 MB)    | 1,024 (1 KB) | 524,286        |
| 536,870,912 (500 MB)    | 2,048 (2 KB) | 262,143        |
| 536,870,912 (500 MB)    | 4,096 (4 KB) | 131,071        |
| 536,870,912 (500 MB)    | 8,192 (8 KB) | 65,535         |
| 4,294,967,296 (4 GB)    | 1,024 (1 KB) | 4,194,302      |
| 4,294,967,296 (4 GB)    | 2,048 (2 KB) | 2,097,151      |
| 4,294,967,296 (4 GB)    | 4,096 (4 KB) | 1,048,575      |
| 4,294,967,296 (4 GB)    | 8,192 (8 KB) | 524,287        |

---

#### Cluster bitmap

- A bit array showing free/used clusters.
- `0 = free`, `1 = used`.
- Size of bitmap =

```
(Total disc size / Cluster size) / 8
```

**Examples:**

| Disc size              | Cluster size | Bitmap size | Num. clusters |
|------------------------|--------------|-------------|---------------|
| 536,870,912 (500 MB)   | 1,024 (1 KB) | 65,536      | 64            |
| 536,870,912 (500 MB)   | 2,048 (2 KB) | 32,768      | 16            |
| 536,870,912 (500 MB)   | 4,096 (4 KB) | 16,384      | 4             |
| 536,870,912 (500 MB)   | 8,192 (8 KB) | 8,192       | 1             |
| 4,294,967,296 (4 GB)   | 1,024 (1 KB) | 524,288     | 512           |
| 4,294,967,296 (4 GB)   | 2,048 (2 KB) | 262,144     | 128           |
| 4,294,967,296 (4 GB)   | 4,096 (4 KB) | 131,072     | 32            |
| 4,294,967,296 (4 GB)   | 8,192 (8 KB) | 65,536      | 8             |
| 17,179,869,184 (16 GB) | 1,024 (1 KB) | 4,194,304   | 8,192         |
| 17,179,869,184 (16 GB) | 2,048 (2 KB) | 1,048,576   | 512           |
| 17,179,869,184 (16 GB) | 4,096 (4 KB) | 524,288     | 128           |
| 17,179,869,184 (16 GB) | 8,192 (4 KB) | 262,144     | 32            |


### Filesystem Cluster cache

The shared cluster cache helper is implemented in `kernel/source/drivers/filesystems/ClusterCache.c` with its public interface in `kernel/include/drivers/filesystems/ClusterCache.h`. It reuses the generic `utils/Cache` engine (TTL, cleanup, eviction) and adds cluster-oriented keys (`owner + cluster index + size`) so multiple filesystem drivers can share one non-duplicated cache pattern. The generic cache supports `CACHE_WRITE_POLICY_READ_ONLY`, `CACHE_WRITE_POLICY_WRITE_THROUGH`, and `CACHE_WRITE_POLICY_WRITE_BACK`, with optional flush callbacks for dirty entry persistence.


### Foreign File systems

| FS | Key Concepts | RO Difficulty | Full RW Difficulty | Notes |
|---|---|---:|---:|---|
| **FAT12/16** | Boot-friendly, allocation tables, 8.3 names | 2 | 3 | Very simple; some edge cases with cluster chains. |
| **ISO9660/Joliet/Rock Ridge** | CD-ROM FS, fixed tables | 2 | 2 | Read-only only; trivial for mounting images. |
| **MINIX (v1/v2)** | Bitmaps, inodes, direct/indirect | 3 | 4 | Educational, limited size, very clean spec. |
| **FAT32** | FAT + FSInfo + VFAT long names | 3 | 4 | Long File Names, timestamp quirks, no journal. |
| **squashfs** | Read-only, compressed, indexed tables | 3 | 3 | Dead simple in RO; great for system images. |
| **exFAT** | Bitmap + FAT, chained dir entries | 4 | 6 | Official specs exist, but many entry types. |
| **UDF** | Successor to ISO9660, incremental writes | 4 | 6–7 | Many versions/profiles; optical and USB use. |
| **ext2** | Superblock, group desc, bitmaps, inodes | 5 | 6 | Very documented; no journal; fsck required. |
| **ext3** | ext2 + JBD journal | 6 | 7 | Journaling metadata/data, proper recovery required. |
| **ReiserFS (v3)** | Balanced trees, small entry packing | 6 | 8 | Non-standard layout; legacy. |
| **HFS+** | B-trees (catalog, extents), forks | 6 | 8 | Unicode normalization, legacy quirks. |
| **ext4** | Extents, htree, 64-bit, JBD2 | 6 | 9 | Extents + journal + optional features. |
| **XFS** | Btrees everywhere, delayed alloc, journaling | 6 | 9 | High-performance, recovery heavy. |
| **F2FS** | Log-structured, flash segments | 6 | 8 | GC/segment cleaning, wear-level tuning. |
| **APFS** | Copy-on-write, containers, snapshots | 7 | 9–10 | Encryption, clones, variable blocks; partial docs. |
| **Btrfs** | COW, extent trees, checksums, RAID, snapshots | 7 | 9–10 | Complex balance between many trees; fragile. |
| **ZFS** | COW, pools, checksums, RAID-Z, snapshots | 7 | 10 | Includes volume mgmt; very large scope. |
| **NTFS** | MFT, resident/non-resident attrs, bitmap, journal | 7 | 9 | Compression, sparse, ACLs, USN; very rich design. |

#### EXT2

The EXT2 driver implementation is split into focused units under
`kernel/source/drivers/filesystems/`:
`EXT2-Base.c`, `EXT2-Allocation.c`, `EXT2-Storage.c`, and
`EXT2-FileOps.c`.

EXT2 block I/O uses a per-filesystem block buffer pool via
`utils/BufferPool` (backed by `BlockList`) to reuse block-sized buffers
and reduce heap churn in metadata and data block paths.

```
                ┌──────────────────────────────────────┐
                │               INODE                  │
                ├──────────────────────────────────────┤
                │ Block[0] → [DATA BLOCK 0]            │
                │ Block[1] → [DATA BLOCK 1]            │
                │   ...                                │
                │ Block[11] → [DATA BLOCK 11]          │
                │ Block[12] → [SINGLE INDIRECT]        │
                │ Block[13] → [DOUBLE INDIRECT]        │
                │ Block[14] → [TRIPLE INDIRECT]        │
                └──────────────────────────────────────┘
                               │
                               │
                               ▼
─────────────────────────────────────────────────────────────────────
(1) SINGLE INDIRECT (Block[12])
─────────────────────────────────────────────────────────────────────
[SINGLE INDIRECT BLOCK]
 ├── ptr[0] → [DATA BLOCK 12]
 ├── ptr[1] → [DATA BLOCK 13]
 ├── ptr[2] → [DATA BLOCK 14]
 ...
 └── ptr[1023] → [DATA BLOCK N]

─────────────────────────────────────────────────────────────────────
(2) DOUBLE INDIRECT (Block[13])
─────────────────────────────────────────────────────────────────────
[DOUBLE INDIRECT BLOCK]
 ├── ptr[0] → [SINGLE INDIRECT BLOCK A]
 │              ├── ptr[0] → [DATA BLOCK A0]
 │              ├── ptr[1] → [DATA BLOCK A1]
 │              └── ...
 ├── ptr[1] → [SINGLE INDIRECT BLOCK B]
 │              ├── ptr[0] → [DATA BLOCK B0]
 │              ├── ptr[1] → [DATA BLOCK B1]
 │              └── ...
 └── ptr[1023] → [SINGLE INDIRECT BLOCK Z]
                 ├── ...
                 └── [DATA BLOCK Zx]

─────────────────────────────────────────────────────────────────────
(3) TRIPLE INDIRECT (Block[14])
─────────────────────────────────────────────────────────────────────
[TRIPLE INDIRECT BLOCK]
 ├── ptr[0] → [DOUBLE INDIRECT BLOCK A]
 │              ├── ptr[0] → [SINGLE INDIRECT BLOCK A1]
 │              │              ├── ptr[0] → [DATA BLOCK A1-0]
 │              │              └── ...
 │              ├── ptr[1] → [SINGLE INDIRECT BLOCK A2]
 │              │              ├── ptr[0] → [DATA BLOCK A2-0]
 │              │              └── ...
 │              └── ...
 ├── ptr[1] → [DOUBLE INDIRECT BLOCK B]
 │              └── ...
 └── ...
─────────────────────────────────────────────────────────────────────
```

#### NTFS

```
                  ┌─────────────────────────────────────────┐
                  │           NTFS BOOT SECTOR              │
                  ├─────────────────────────────────────────┤
                  │ BPB/EBPB                                │
                  │ BytesPerSector, SectorsPerCluster       │
                  │ MFT LCN, MFTMirr LCN, Record size       │
                  └─────────────────────────────────────────┘
                                   │
               ┌───────────────────┴───────────────────┐
               ▼                                       ▼
┌──────────────────────────────────┐      ┌───────────────────────────┐
│ $MFT (Master File Table)         │      │ $MFTMirr                  │
│ record 0..N                      │      │ mirror of first records   │
└──────────────────────────────────┘      └───────────────────────────┘
               │
               ▼
┌─────────────────────────────────────────────────────────────────────┐
│ FILE RECORD (typically 1024 bytes)                                  │
├─────────────────────────────────────────────────────────────────────┤
│ Header: "FILE", USA/Fixup array, sequence, flags, attr offsets      │
│ Attributes (typed TLV chain):                                       │
│ - STANDARD_INFORMATION                                              │
│ - FILE_NAME (parent ref + UTF-16 name)                              │
│ - DATA (resident or non-resident)                                   │
│ - INDEX_ROOT / INDEX_ALLOCATION / BITMAP (folders)                  │
│ - other metadata attributes                                         │
└─────────────────────────────────────────────────────────────────────┘
               │
   ┌───────────┴───────────┐
   ▼                       ▼
┌───────────────────┐   ┌────────────────────────────────────────────┐
│ Resident DATA     │   │ Non-resident DATA                          │
│ bytes in record   │   │ Runlist: (VCN range -> LCN range)          │
└───────────────────┘   │ sparse/compressed flags possible           │
                        └────────────────────────────────────────────┘

───────────────────────────────────────────────────────────────────────
Folder indexing (B+tree-like)
───────────────────────────────────────────────────────────────────────
INDEX_ROOT (small entries in record)
    └─ if overflow -> INDEX_ALLOCATION blocks + BITMAP allocation map
         each index entry: filename key + file reference (MFT index)

───────────────────────────────────────────────────────────────────────
Core metadata files in MFT
───────────────────────────────────────────────────────────────────────
$MFT, $MFTMirr, $Bitmap, $LogFile, $Volume, $AttrDef, $UpCase, ...
```

The NTFS driver is split across dedicated modules under `kernel/source/drivers/`:
`NTFS-Base.c`, `NTFS-Record.c`, `NTFS-Index.c`, `NTFS-Path.c`, `NTFS-VFS.c`, `NTFS-Time.c`, and `NTFS-Write.c`.

`MountPartition_NTFS` (`NTFS-Base.c`) validates the boot sector, checks sector-size compatibility (512/4096), computes geometry (`BytesPerSector`, `SectorsPerCluster`, `BytesPerCluster`, `MftStartCluster`, `MftStartSector`), initializes the filesystem object, and registers the mounted volume. `NtfsGetVolumeGeometry` exposes the cached geometry to diagnostics.

Low-level file-record loading is implemented by `NtfsLoadFileRecordBuffer` (`NTFS-Record.c`), which reads raw MFT records, applies update-sequence fixups, and returns a validated in-memory record image. Parsed record metadata is exposed through `NtfsReadFileRecord`.

Record attribute parsing is table-driven in `NtfsParseFileRecordAttributes` (`NTFS-Record.c`). Dedicated handlers process `FILE_NAME`, default `DATA`, `OBJECT_IDENTIFIER`, and `SECURITY_DESCRIPTOR` attributes. Stream reads are provided by `NtfsReadFileDataByIndex` and `NtfsReadFileDataRangeByIndex`; non-resident runlist reads are handled by `NtfsReadNonResidentDataAttributeRange`.

Folder traversal is implemented by `NtfsEnumerateFolderByIndex` (`NTFS-Index.c`) using `INDEX_ROOT`, `INDEX_ALLOCATION`, and `BITMAP` metadata to walk NTFS index entries. Path resolution is implemented by `NtfsResolvePathToIndex` (`NTFS-Path.c`) with case-insensitive matching and a lookup cache (`NTFSFILESYSTEM.PathLookupCache`) to reduce repeated lookups.

VFS integration is implemented in `NTFS-VFS.c` and dispatched from `NTFS-Base.c` through `DF_FS_OPENFILE`, `DF_FS_OPENNEXT`, `DF_FS_CLOSEFILE`, `DF_FS_READ`, and `DF_FS_WRITE`. NTFS metadata is translated into generic `FILE` fields and attributes (folder flag, sizes, timestamps). The current mode is read-only: write and mutating operations are routed to explicit placeholders in `NTFS-Write.c` and return `DF_RETURN_NO_PERMISSION`.

Timestamp conversion from NTFS 100ns units to kernel `DATETIME` is implemented by `NtfsTimestampToDateTime` (`NTFS-Time.c`). UTF-16LE filename decoding and comparison support is provided by `kernel/source/utils/Unicode.c` (`Utf16LeNextCodePoint`, `Utf16LeToUtf8`, `Utf16LeCompareCaseInsensitiveAscii`).

## Interaction and Networking

### Shell scripting

#### Persistent interpreter context

`InitShellContext()` creates one `SCRIPT_CONTEXT` per shell context with callbacks for output, command execution, variable resolution, and function calls. The same context is reused for command-line execution, startup commands, and `.e0` file execution until `DeinitShellContext()` destroys it.

This provides a stable interpreter state across commands and centralizes callback wiring in `kernel/source/shell/Shell-Commands.c`.

The script engine implementation is split into dedicated modules under `kernel/source/script/` (`Script-Core.c`, `Script-Parser-Expression.c`, `Script-Parser-Statements.c`, `Script-Eval.c`, `Script-Collections.c`, `Script-Scope.c`) with public and internal headers under `kernel/include/script/`.

Control-flow statements include `if`, `for`, `return`, and `continue`. `continue;` is accepted only inside loop bodies and skips directly to the increment phase of the current `for` iteration.

#### Execution paths

Shell command lines are executed through `ExecuteCommandLine()`, which calls `ScriptExecute()` directly on the entered text. Errors are reported through `ScriptGetErrorMessage()`.

Startup automation (`ExecuteStartupCommands()`) loads `Run.<index>.Command` entries from `exos.toml` and executes each entry through the same `ExecuteCommandLine()` path.

The `run` command delegates launch to `SpawnExecutable()`:

- command names without folder separators are first resolved against the current shell folder, then against configured `KernelPath.Binaries.<index>.Path` folders when the current-folder candidate cannot be opened;
- if the resolved target ends with `.e0` (`ScriptIsE0FileName()`), `RunScriptFile()` opens the file, reads it to memory, and executes its content with `ScriptExecute()`;
- otherwise, it follows the process spawn path.

Background mode is blocked for `.e0` scripts.

#### String operators

E0 expressions support string-specific operator behavior in the interpreter:

- `+` concatenates as text when either operand is a string. In a left-associative `+` chain, once one sub-expression yields a string, later `+` operations continue as text concatenation.
- `string - string` removes every occurrence of the right operand from the left operand.

Examples: `"foo" + "bar"` gives `"foobar"`, `1 + "x"` gives `"1x"`, `1 + 2 + "x" + 3` gives `"3x3"`, and `"foobarfoo" - "foo"` gives `"bar"`.

#### Shell command bridge inside E0

The parser supports shell-style command statements inside E0 source. When a statement is recognized as a shell command, the AST expression node is marked `IsShellCommand = TRUE` and stores the full command line.

At evaluation time, this node calls the `ExecuteCommand` callback (`ShellScriptExecuteCommand()` in shell integration). That callback routes to:

- built-in shell commands from `COMMANDS[]`;
- executable launch via `SpawnExecutable()` when no built-in matches.

Each `SHELL_COMMAND_ENTRY` stores the primary name, alternate name, usage text, and a short description so the `commands` command can print a single-line summary per entry.

This places command execution policy in shell code while the script engine is generic.

Shell inspection commands that only read exposed kernel objects can delegate formatting to embedded E0 scripts. `driver list`, `network devices`, `usb ports|devices|device-tree|drives|probe`, `mem_map`, and `task list` follow that pattern so the shell stays on the public exposure API instead of walking kernel lists directly.

Function-call expressions support zero or more arguments. The parser stores each argument as an AST expression, the evaluator resolves each one to text, and the host `CallFunction` callback receives `(name, argc, argv, user)` instead of a single serialized argument.

The shell exposes host functions through that bridge:

- `print(...)`: prints the stringified arguments joined with spaces.
- `exec(...)`: rebuilds one command line from the stringified arguments and executes it through the normal shell command path.
- `kill(handle)`: resolves one user-visible handle and routes to `SysCall_KillProcess()` or `SysCall_KillTask()` depending on the object type behind the handle.
- `smokeTestMultiArgs(a, b, c, d)`: reserved smoke helper that validates four serialized arguments and returns one deterministic marker value for automated regression checks.
- `setGraphicsDriver(driverAlias, width, height, bpp)`: forces one graphics backend alias and applies the requested mode through the selector path.
- `createAccount(userName, password, privilege)`: creates one account through `SYSCALL_CreateUser`.
- `deleteAccount(userName)`: deletes one account through `SYSCALL_DeleteUser`.
- `changePassword(oldPassword, newPassword)`: changes the current user password through `SYSCALL_ChangePassword`.

Known host functions return `SCRIPT_FUNCTION_STATUS_UNKNOWN` when the symbol does not exist, and `SCRIPT_FUNCTION_STATUS_ERROR` when the function exists but rejects the call after setting an explicit script error.

#### Return value behavior

`AST_RETURN` stores a return value in the script context (`ScriptStoreReturnValue()`). The shell path (`RunScriptFile()`) prints the raw return value on its own line after successful execution.

Supported stored return categories are string, integer, float, and native E0 object values. Host handles and arrays are rejected by the interpreter storage path.

#### Host object exposure model

The shell registers host symbols with `ScriptRegisterHostSymbol()` during context initialization.
The authoritative root list and exposed field matrix are documented in [Exposed objects in shell](#exposed-objects-in-shell).

Each symbol is associated with a `SCRIPT_HOST_DESCRIPTOR` implemented under `kernel/source/expose/*`. Descriptor callbacks (`GetProperty`, `GetElement`) provide typed access to fields and arrays.

Access control is enforced in exposure helpers through shared macros and checks (`EXPOSE_REQUIRE_ACCESS(...)`, `ExposeCanReadProcess(...)`) so scripts can inspect kernel state through controlled interfaces instead of raw object access. `account.count` is public to support first-user bootstrap, while `account[n]` details remain restricted to administrator or kernel callers.

### Network Stack

EXOS implements a modern layered network stack with per-device context isolation and support for Ethernet, ARP, IPv4, UDP, and TCP protocols. The implementation follows standard networking principles with clear separation between layers and full support for multiple network devices.

#### Architecture Overview

The network stack is organized in five main layers with per-device context management:

```
┌─────────────────────────────────────┐
│            Applications             │
├─────────────────────────────────────┤
│     Socket Layer (TCP and UDP)      │
│   (Connection and datagram APIs,    │
│    state machine, send/recv paths)  │
├─────────────────────────────────────┤
│          IPv4 Protocol Layer        │
│    (ICMP, UDP, TCP protocols)       │
│    [Per-device IPv4 contexts]       │
├─────────────────────────────────────┤
│             ARP Layer               │
│    (Address Resolution Protocol)    │
│     [Per-device ARP contexts]       │
├─────────────────────────────────────┤
│         Network Manager Layer       │
│  (Device discovery, initialization, │
│   callback routing, maintenance)    │
├─────────────────────────────────────┤
│           Ethernet Layer            │
│         (E1000 Driver)              │
└─────────────────────────────────────┘
```

#### Device Infrastructure

**Location:** `kernel/include/Device.h`, `kernel/source/Device.c`

The network stack uses a device-based architecture where all network devices inherit from a common `DEVICE` structure that supports context storage and management. Every device embeds a mutex used to serialize access to shared state; drivers must call `InitMutex()` on the device instance before exposing it to other subsystems.

**Device Structure:**
```c
#define DEVICE_FIELDS       \
    LISTNODE_FIELDS         \
    MUTEX Mutex;            \
    LPDRIVER Driver;        \
    LIST Contexts;

typedef struct DeviceTag {
    DEVICE_FIELDS
} DEVICE, *LPDEVICE;
```

**Context Management API:**
- `GetDeviceContext(Device, ID)`: Retrieve context by type ID
- `SetDeviceContext(Device, ID, Context)`: Store context for device
- `RemoveDeviceContext(Device, ID)`: Remove and free context

#### Device Interrupt Infrastructure

**Location:** `kernel/source/drivers/DeviceInterrupt.c`, `kernel/include/drivers/DeviceInterrupt.h`, `kernel/source/sync/DeferredWork.c`, `kernel/source/sync/DeferredWorkQueue.c`

The device interrupt layer centralizes vector assignment, interrupt routing, and deferred work dispatching for hardware devices.

**Key Features:**
- Configurable interrupt vector slots shared across PCI/PIC paths (`General.DeviceInterruptSlots`, 1–32, default 32).
- Slot bookkeeping is allocated dynamically from kernel memory so the table matches the configured slot count.
- `DeviceInterruptRegister()` binds ISR top halves, deferred callbacks, and optional poll routines to a slot.
- `DeferredWorkQueue` owns the reusable queue mechanics: slot storage, callback registration, event signaling, callback dispatch, polling callbacks, and unregister quiescence.
- `DeferredWork` instantiates the standard queue and a fast queue. The fast queue uses a 5 ms wait and polling cadence and is selected by mouse dispatch.
- `DEFERRED_WORK_TOKEN` identifies a queue and slot explicitly. It is used instead of packing queue metadata into a scalar handle.
- `DeferredWorkUnregister()` first blocks new dispatches on the token slot, waits for in-flight callbacks to finish, then clears the slot so driver teardown cannot race one copied callback frame.
- Automatic spurious-interrupt suppression masks a slot after repeated suppressed top halves and relies on its poll routine until the driver re-arms the IRQ.
- Graceful fallback to polling when hardware interrupts are unavailable.
- The IOAPIC driver is optional; when ACPI is unavailable the kernel continues in PIC mode and boots without IOAPIC.
- Local APIC initialization enables the APIC and programs the spurious vector (SVR bit 8) early for consistent delivery.
- When PIC mode is active, the IMCR is forced to route legacy IRQs to the PIC.

**API Functions:**
- `InitializeDeviceInterrupts()`: Reset slot bookkeeping at boot.
- `DeviceInterruptRegister()/DeviceInterruptUnregister()`: Manage slot lifetime.
- `DeviceInterruptHandler(slot)`: ASM entry point fan-out for interrupt vectors 0x30–0x37.
- `InitializeDeferredWork()`: Start the standard and fast dispatcher kernel tasks and supporting events.
- PIC mode remaps IRQs to vectors 0x20–0x2F before interrupts are enabled.
- PIC routing consults the IMCR presence flag set at initialization; if the register is not writable, the Local APIC LINT0 ExtINT path is enabled to keep legacy IRQs flowing.

#### Network Manager

**Location:** `kernel/source/network/NetworkManager.c`, `kernel/include/network/NetworkManager.h`

The Network Manager provides centralized network device discovery, initialization, and maintenance.

**Key Features:**
- Automatic PCI network device discovery (up to 8 devices)
- Per-device network stack initialization (ARP, IPv4, UDP, TCP)
- Unified frame reception callback routing
- Integration with the deferred work dispatcher for interrupt-driven receive paths with polling fallback
- Primary device selection for global protocols

**Initialization Flow:**
```c
void InitializeNetworkManager(void) {
    // 1. Scan PCI devices for DRIVER_TYPE_NETWORK
    // 2. For each network device:
    //    a. Reset device hardware
    //    b. Initialize ARP context
    //    c. Initialize IPv4 context
    //    d. Install device-specific RX callback
    //    e. Initialize UDP for the device
    //    f. Initialize TCP (once globally)
}
```

**API Functions:**
- `InitializeNetworkManager()`: Discover and initialize all network devices
- `NetworkManager_InitializeDevice()`: Initialize specific network device
- `NetworkManager_MaintenanceTick()`: Deferred maintenance routine invoked by `DeferredWorkDispatcher`
- `NetworkManager_GetPrimaryDevice()`: Get primary device for TCP

**DHCP Integration**
- DHCP ACK applies assigned IP, subnet mask, gateway, and DNS server to the IPv4 layer and network device context.
- ARP cache and pending IPv4 routes are flushed on lease changes before marking the device ready, ensuring stale mappings are dropped when a lease is renewed or replaced.
- DHCP retry backoff is capped; on exhaustion, the stack optionally falls back to the configured static IP/mask/gateway before declaring the device ready.

#### E1000 Ethernet Driver

**Location:** `kernel/source/network/E1000.c`

The E1000 driver provides the hardware abstraction layer for Intel 82540EM network cards. It implements the standard EXOS driver interface with network-specific function IDs.

**Key Features:**
- TX/RX descriptor ring management
- Hardware interrupt handling (IRQ 11)
- Frame transmission and reception
- EthType recognition (IPv4: 0x0800, ARP: 0x0806)
- MAC address retrieval
- Link status monitoring

**Driver Interface:**
- `DF_NT_RESET`: Reset network adapter
- `DF_NT_GETINFO`: Get MAC address and link status
- `DF_NT_SEND`: Send Ethernet frame
- `DF_NT_POLL`: Poll receive ring for new frames
- `DF_NT_SETRXCB`: Register frame receive callback
- `DF_DEV_ENABLE_INTERRUPT`: Configure interrupt routing and unmask device interrupts
- `DF_DEV_DISABLE_INTERRUPT`: Mask device interrupts and release routing

#### ARP (Address Resolution Protocol)

**Location:** `kernel/source/network/ARP.c`, `kernel/include/network/ARP.h`, `kernel/include/ARPContext.h`

ARP handles IPv4-to-MAC address resolution with per-device cache management and automatic request generation.

**Per-Device Context:**
```c
typedef struct ArpContextTag {
    LPDEVICE Device;
    U8 LocalMacAddress[6];
    U32 LocalIPv4_Be;
    ArpCacheEntry Cache[ARP_CACHE_SIZE];
} ArpContext, *LPArpContext;
```

**Key Features:**
- 32-entry LRU cache per device with TTL (10 minutes default)
- Automatic ARP request generation for unknown addresses
- ARP reply processing and cache updates
- Response to incoming ARP requests for local IP
- Paced request retransmission (3-second intervals)

**Cache Entry Structure:**
```c
typedef struct ArpCacheEntryTag {
    U32 IPv4_Be;        // IPv4 address (big-endian)
    U8 MacAddress[6];   // Corresponding MAC address
    U32 TimeToLive;     // Entry expiration timer
    U8 IsValid;         // Entry validity flag
    U8 IsProbing;       // Request already sent flag
} ArpCacheEntry;
```

**API Functions:**
- `ARP_Initialize(Device, LocalIPv4_Be, DeviceInfo)`: Initialize ARP context for device, optionally using cached link information
- `ARP_Destroy(Device)`: Cleanup ARP context
- `ARP_Resolve(Device, TargetIPv4_Be, OutMacAddress[])`: Resolve IPv4 to MAC
- `ARP_Tick(Device)`: Age cache entries (call every 1 second)
- `ARP_OnEthernetFrame(Device, Frame, Length)`: Process incoming ARP packets
- `ARP_DumpCache(Device)`: Debug helper to display cache contents

#### IPv4 Internet Protocol

**Location:** `kernel/source/network/IPv4.c`, `kernel/include/network/IPv4.h`

IPv4 layer provides packet parsing, routing, and protocol multiplexing with per-device protocol handler registration.

**Per-Device Context:**
```c
typedef struct IPv4ContextTag {
    LPDEVICE Device;
    U32 LocalIPv4_Be;
    IPv4_ProtocolHandler ProtocolHandlers[IPV4_MAX_PROTOCOLS];
} IPv4Context, *LPIPv4Context;
```

**Key Features:**
- Complete IPv4 header validation (version, IHL, checksum, TTL)
- Simple routing: local delivery vs. drop (no forwarding)
- Per-device protocol handler registration (ICMP=1, TCP=6, UDP=17)
- Automatic packet encapsulation to Ethernet
- Fragmentation detection (non-fragmented packets only)
- Checksum calculation and verification

**IPv4 Header Structure:**
```c
typedef struct IPv4HeaderTag {
    U8 VersionIHL;          // Version (4 bits) + IHL (4 bits)
    U8 TypeOfService;       // DSCP/ToS field
    U16 TotalLength;        // Total packet length (big-endian)
    U16 Identification;     // Fragment identification
    U16 FlagsFragmentOffset; // Flags + Fragment offset
    U8 TimeToLive;          // TTL hop count
    U8 Protocol;            // Next protocol number
    U16 HeaderChecksum;     // Header checksum
    U32 SourceAddress;      // Source IPv4 (big-endian)
    U32 DestinationAddress; // Destination IPv4 (big-endian)
} IPv4Header;
```

**Routing Logic:**
1. Validate packet structure and checksum
2. Check if destination matches device's local IP address
3. If local: dispatch to device's registered protocol handler
4. If remote: drop packet (no forwarding implemented)

**API Functions:**
- `IPv4_Initialize(Device, LocalIPv4_Be)`: Initialize IPv4 context for device
- `IPv4_Destroy(Device)`: Cleanup IPv4 context
- `IPv4_SetLocalAddress(Device, LocalIPv4_Be)`: Update device's local IP
- `IPv4_RegisterProtocolHandler(Device, Protocol, Handler)`: Register protocol handler
- `IPv4_Send(Device, DestinationIP, Protocol, Payload, Length)`: Send IPv4 packet
- `IPv4_OnEthernetFrame(Device, Frame, Length)`: Process incoming IPv4 packets

#### UDP (User Datagram Protocol)

**Location:** `kernel/source/network/UDP.c`, `kernel/include/network/UDP.h`

UDP provides connectionless datagram delivery with IPv4 integration and per-port handlers.

**Key Features:**
- UDP header build/parse with source port, destination port, length, and checksum
- Pseudo-header checksum generation and validation
- Per-device port handler registration (`UDP_RegisterPortHandler`)
- Socket datagram operations through `SocketSendTo` and `SocketReceiveFrom`

**API Functions:**
- `UDP_Initialize(Device)`: Initialize UDP context for a device
- `UDP_Destroy(Device)`: Cleanup UDP context
- `UDP_Send(Device, DestinationIP, SourcePort, DestinationPort, Payload, Length)`: Send UDP datagram
- `UDP_OnIPv4Packet()`: Process incoming UDP datagrams from IPv4

**Known Limits:**
- Socket receive dispatch is local-port based and does not yet enforce additional per-socket remote endpoint filtering.
- Datagram truncation reports payload truncation through logs, but no dedicated API-level truncation flag is exposed yet.
- Shared-local-port behavior for multiple UDP sockets is not fully policy-driven (`SO_REUSEADDR` style fan-out is pending).

#### TCP (Transmission Control Protocol)

**Location:** `kernel/source/network/TCP.c`, `kernel/include/network/TCP.h`

TCP provides reliable connection-oriented communication using a state machine-based implementation.

**Key Features:**
- RFC 793 compliant state machine (CLOSED, LISTEN, SYN_SENT, ESTABLISHED, etc.)
- Connection management with unique 4-tuple identification
- Send/receive buffers with flow control
- Configurable buffer sizes through `TCP.SendBufferSize` and `TCP.ReceiveBufferSize`
- Sequence number management
- Timer-based retransmission and TIME_WAIT handling
- Bounded exponential backoff for retransmission timeout
- Duplicate ACK detection with fast retransmit and fast recovery
- Reno-style congestion baseline (slow start and congestion avoidance)
- Checksum validation with IPv4 pseudo-header

The buffer capacities default to 32768 bytes each when the configuration entries are absent.
The retransmission tracker keeps one outstanding MSS-sized segment for fast retransmit.

#### Layer Interactions

**Frame Reception Flow:**
1. **E1000 Hardware** receives Ethernet frame and generates interrupt
2. **E1000 Driver** copies frame to memory, calls device-specific RX callback
3. **Network Manager** callback examines EthType and dispatches:
   - `0x0806` → `ARP_OnEthernetFrame(Device, Frame, Length)`
   - `0x0800` → `IPv4_OnEthernetFrame(Device, Frame, Length)`
4. **ARP Layer** updates device cache and responds to requests
5. **IPv4 Layer** validates packet and calls device's registered protocol handler
6. **Protocol Handler** (ICMP/UDP/TCP) processes payload with source/destination IPs

**Frame Transmission Flow:**
1. **Application** calls `IPv4_Send(Device, DestinationIP, Protocol, Payload, Length)`
2. **IPv4 Layer** calls `ARP_Resolve(Device, DestinationIP, OutMacAddress[])`
3. **ARP Layer** returns cached MAC or triggers ARP request
4. **IPv4 Layer** builds Ethernet + IPv4 headers with source MAC from device context
5. **E1000 Driver** transmits frame via `DF_NT_SEND`

#### Network Configuration

**Default Configuration:**
- Primary device IP: `192.168.56.16` (big-endian: `0xC0A83810`)
- Network: `192.168.56.0/24`
- Gateway: `192.168.56.1`
- QEMU TAP interface: `tap0`

**Initialization Sequence:**
```c
// 1. Initialize Network Manager (discovers all devices)
InitializeNetworkManager();

// 2. For each device, Network Manager automatically:
//    a. Calls ARP_Initialize(Device, DEFAULT_LOCAL_IP_BE, CachedInfo)
//    b. Calls IPv4_Initialize(Device, DEFAULT_LOCAL_IP_BE)
    //    c. Calls UDP_Initialize(Device)
    //    d. Calls TCP_Initialize() (once globally)

// 3. Application registers protocol handlers per device:
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_ICMP, ICMPHandler);
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_UDP, UDPHandler);
IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_TCP, TCP_OnIPv4Packet);

// 4. Deferred work dispatcher drives maintenance once initialized during boot
```

#### Key Benefits of Per-Device Architecture

1. **Scalability**: Supports multiple network interfaces simultaneously
2. **Isolation**: Each device maintains independent protocol state
3. **Flexibility**: Different devices can have different IP addresses and protocol configurations
4. **Reliability**: Failure of one network device doesn't affect others
5. **Maintainability**: Clear separation of concerns and context management

The network stack successfully handles real network traffic across multiple devices and provides a robust foundation for implementing network applications and services.

## Windowing

The windowing system is implemented in the kernel desktop layer. It owns desktop lifetime, window objects, z-order, invalidation, draw dispatch, non-client rendering, and theme activation. Graphics drivers provide scanout and drawing contexts, but window composition policy stays in desktop code.

### Desktop activation

The shell entry point is the `desktop` command.

- `desktop show` creates or reuses the shell desktop, starts the dispatcher task, selects one graphics backend and mode, switches the session to graphics mode, and launches `/system/apps/portal`.
- When one userland-owned desktop becomes active, desktop activation also transfers focused-process ownership to the desktop task owner so mouse and keyboard input continue to route through that desktop window tree.
- `desktop status` reports desktop state and theme runtime state.
- `desktop theme <path-or-name>` loads and/or activates one theme.

After shell startup completes, the shell invokes the same desktop activation path automatically.

`Desktop.ThemePath` in `exos.*.toml` is optional. If configured, `desktop show` tries to load and activate that theme after desktop activation succeeds. Theme errors never block desktop startup.

### Window model

Each window is a kernel object attached to one desktop tree. Parent/child relations, sibling order, visibility, and geometry are managed by the desktop core. Userland interacts through the public window API and syscalls, but the authoritative state lives in the kernel.

Geometry uses three coordinate spaces:

- `ScreenRect` / `ScreenPoint`: absolute desktop coordinates.
- `WindowRect` / `WindowPoint`: coordinates relative to the full owned window rectangle.
- `ClientRect` / `ClientPoint`: coordinates relative to the client area origin.

Creation, move, resize, and drag-driven geometry changes all resolve through the same placement path before one new rectangle is committed. Style bits also express generic placement and z-order policy such as `EWS_ALWAYS_IN_FRONT`, `EWS_ALWAYS_AT_BOTTOM`, and `EWS_EXCLUDE_SIBLING_PLACEMENT`.

### Rendering pipeline

Rendering is asynchronous and dirty-rectangle driven.

When a window is invalidated, the desktop core records screen-space damage in the window dirty region and posts one coalesced draw request. The structured draw path then rebuilds the clip region, iterates dirty rectangles, draws kernel-owned non-client visuals when required, dispatches `EWM_DRAW` to the window procedure with an active draw context, and presents the completed clip once after both passes finish.

This split is important:

- system chrome is rendered by the kernel before client paint,
- client paint is clipped to the effective dirty region,
- draw dispatch happens outside structural desktop locks.

`EWM_CLEAR` is part of the same pipeline. It resolves the themed background for the current draw surface, updates resolved transparency state, and lets overlay invalidation re-expose what became visible behind transparent content.

### Decoration modes

Decoration mode is selected through window style bits:

- `EWS_SYSTEM_DECORATED`
- `EWS_CLIENT_DECORATED`
- `EWS_BARE_SURFACE`
- `EWS_CLOSE_BUTTON_VISIBLE`
- `EWS_MINIMIZE_BUTTON_VISIBLE`
- `EWS_MAXIMIZE_BUTTON_VISIBLE`

Style `0` is treated as `SystemDecorated` for compatibility.

If none of the title bar button visibility bits are set, system-decorated windows keep the default three-button chrome for compatibility. If at least one visibility bit is set, only the requested buttons are shown and hit-tested.

For `SystemDecorated` windows, the kernel owns the border, title bar, caption rendering, non-client layout, and the standard title bar buttons that post `EWM_CLOSE`, `EWM_MAXIMIZE`, and `EWM_MINIMIZE`. `EWM_DRAW` is delivered in client coordinates only.

For `ClientDecorated` windows, the kernel renders no non-client chrome. The client owns the full window surface, including any custom frame or title bar. `EWM_DRAW` is delivered on the full owned surface.

For `BareSurface` windows, the kernel also skips non-client rendering and keeps policy assumptions minimal.

### Input, capture, and timers

The desktop layer routes mouse and keyboard events to the focused window and tracks capture at desktop scope. Focus changes, mouse capture, move/resize operations, and timer delivery all use generic windowing paths rather than component-specific logic.

Client invalidation and full-window invalidation are separate APIs. `InvalidateClientRect(Window, Rect)` interprets `Rect` in client coordinates and maps `NULL` to the full client area. `InvalidateWindowRect(Window, Rect)` remains the lower-level full-window API in window coordinates and maps `NULL` to the full window surface, including non-client chrome. When one parent window moves, the desktop refreshes descendant screen rectangles and invalidates descendant full surfaces so child windows follow the parent visually.

Window visibility distinguishes requested visibility from effective visibility. `EWS_VISIBLE` stores the window local visibility request. `WINDOW_STATUS_VISIBLE` stores effective visibility after combining that request with ancestor effective visibility. Hiding one parent clears effective visibility for the whole subtree without destroying descendant requested visibility, and showing the parent restores effective visibility only for descendants that still request visibility.

Per-window timers are asynchronous. `SetWindowTimer`, `KillWindowTimer`, and `EWM_TIMER` let one window request periodic redraw or state updates without blocking the desktop pipeline.

### Theme architecture

The theme system has one built-in default runtime and one optional loaded runtime. Theme files are strict TOML documents parsed directly by the kernel.

The top-level theme sections are:

- `[theme]`
- `[tokens]`
- `[elements.<element-id>]`
- `[recipes.<recipe-id>]`
- `[bindings]`

Canonical element identifiers include:

- `desktop.root`
- `window.client`
- `window.border`
- `window.titlebar`
- `window.title.text`
- `window.button.close`
- `window.button.maximize`
- `window.button.minimize`
- `window.resize.left`
- `window.resize.right`
- `window.resize.top`
- `window.resize.bottom`
- `window.resize.top_left`
- `window.resize.top_right`
- `window.resize.bottom_left`
- `window.resize.bottom_right`
- `button.body`
- `button.text`
- `textbox.body`
- `textbox.text`
- `textbox.caret`
- `menu.background`
- `menu.item`
- `menu.item.text`

State identifiers are:

- `normal`
- `hover`
- `pressed`
- `focused`
- `active`
- `disabled`
- `checked`
- `selected`

State lookup uses the fallback order `exact -> partial -> normal`.

### Theme levels

Level 1 is the fast path. It resolves tokens and typed element properties such as colors, metrics, booleans, and text values.

Level 2 is the recipe path. One binding maps `(element, state)` to one recipe identifier. The recipe interpreter executes a bounded list of primitives such as `fill_rect`, `stroke_rect`, `line`, `gradient_h`, `gradient_v`, `glyph`, and `inset_rect`.

This keeps most themes simple while still allowing richer non-client rendering without hardcoding theme-specific drawing logic into the desktop.

### Theme runtime API and fallback

Theme runtime lifecycle is exposed through:

- `LoadTheme(Path)`
- `ActivateTheme(NameOrHandle)`
- `GetActiveThemeInfo(Info)`
- `ResetThemeToDefault()`
- `ApplyDesktopTheme(Target)`

`LoadTheme` parses one file and stages the candidate runtime. `ActivateTheme` swaps the active runtime atomically and invalidates desktop windows for full redraw. `ResetThemeToDefault` switches back to the built-in runtime.

The runtime tracks:

- whether the built-in or loaded theme is active,
- whether one staged theme exists,
- active and staged theme paths,
- last parser/activation status,
- last fallback reason,
- active token/property/recipe/binding counts.

Failure is explicit. Invalid path, file read failure, strict parse failure, invalid references, and activation failure all leave the existing active theme usable. Fallback reason and status stay queryable through `GetActiveThemeInfo`.

### Theme file example

Minimal reference theme:

```toml
[theme]
name = "Default"

[tokens]
color.desktop.background = "#008080"
color.window.border = "#000000"
color.client.background = "#c0c0c0"
color.window.title.active.start = "#000080"
color.window.title.active.end = "#1084d0"
color.window.title.focused.start = "#808080"
color.window.title.focused.end = "#666666"
color.window.title.inactive.start = "#808080"
color.window.title.inactive.end = "#a0a0a0"
color.window.title.text = "#ffffff"
metric.window.border = 2
metric.window.title_height = 22

[elements.desktop.root]
background = "token:color.desktop.background"

[elements.window.client]
background = "token:color.client.background"

[elements.window.border]
border_color = "token:color.window.border"
border_thickness = "token:metric.window.border"

[elements.window.titlebar]
background = "token:color.window.title.active.start"
background2 = "token:color.window.title.active.end"
title_height = "token:metric.window.title_height"
corner_radius = 6

[elements.window.titlebar.states.normal]
background = "token:color.window.title.active.start"
background2 = "token:color.window.title.active.end"
corner_radius = 6

[elements.window.titlebar.states.focused]
background = "token:color.window.title.focused.start"
background2 = "token:color.window.title.focused.end"
corner_radius = 6

[elements.window.titlebar.states.active]
background = "token:color.window.title.active.start"
background2 = "token:color.window.title.active.end"
corner_radius = 6
```

### Legacy compatibility

`SM_COLOR_*` consumers use `GetSystemBrush` and `GetSystemPen`, which resolve system colors through the active theme token set. The desktop renderer uses the same theme contract.

### Synchronization rules

Desktop-owned `EWM_*` traffic is pumped by a dedicated dispatcher task. Lock ordering is:

`Process -> ProcessHeap -> ProcessMessageQueue -> Task -> TaskMessageQueue -> Desktop -> DesktopTimer -> Window -> GraphicsContext`

Structural desktop locks are not held across message dispatch, callbacks, or recursive tree traversal. Windowing follows the kernel-wide mutex ownership rule: one object mutex is manipulated by the code that owns that object state, and foreign callers access that state through owner-side helpers or snapshots.

Desktop and windowing code follow this same kernel-wide rule. Typical examples:

- desktop code reads one window rectangle through
  `GetWindowScreenRectSnapshot(...)` instead of taking `Window->Mutex`
  directly,
- draw and overlay paths snapshot child lists first, then recurse without
  holding the parent window lock,
- graphics-context clip/origin mutation stays on graphics-context helpers
  instead of direct caller-side `GC->Mutex` access.

Mutex diagnostics are assigned at initialization time through `InitMutexWithDebugInfo(...)` or `SetMutexDebugInfo(...)`. Each tracked mutex carries a stable class, a diagnostic name, and the owning acquisition return address. The deadlock monitor keeps a per-task held-mutex stack and aborts immediately in debug builds when one class inversion is detected, so lock-order regressions fail at the first illegal acquisition instead of requiring ad-hoc session instrumentation.

## Tooling and References

### System Data View

`SYSTEM_DATA_VIEW` is a project-level build flag (`./scripts/linux/build/build --arch x86-32 --fs ext2 --system-data-view`) that starts a diagnostic pager before task creation. The view renders in the kernel console, uses left/right to switch pages, uses up/down to scroll, and exits on `Esc` to continue the boot sequence.

The implementation lives in `kernel/source/SystemDataView.c`. It provides a compact inspection surface for platform state that is already available during early kernel execution, before the shell or desktop environment starts.

#### Pages

- `ACPI MADT`: reports whether ACPI, the local APIC, and the IO APIC are enabled, then lists MADT-derived LAPIC entries, IO APIC entries, and interrupt source overrides.
- `PIC / PIT / IO APIC`: shows PIC masks, IRR and ISR state, IMCR value, PIT counter and status, then prints the discovered IO APIC base, identifier, version, and redirection entries.
- `Local APIC`: shows the LAPIC base and version together with `SVR`, `TPR`, `LDR`, `DFR`, timer registers, and the major local interrupt vectors used by the kernel.
- `Interrupt Routing`: summarizes the interrupt routing view used by the kernel for timer, keyboard, mouse, AHCI, EHCI, xHCI, and E1000-related paths, including the PCI controller selected for each block when one exists.
- `AHCI`: identifies the SATA AHCI controller found through PCI enumeration and prints its PCI location, vendor and device identifiers, IRQ line, and BAR values.
- `EHCI`: identifies the USB EHCI controller found through PCI enumeration and prints its PCI location, vendor and device identifiers, IRQ line, and BAR values.
- `xHCI`: reports PCI identity, decoded PCI status error flags, scratchpad capability and state (`HCSPARAMS2`, `MaxScratchpadBuffers`, `DCBAA[0]`), controller runtime registers (`USBCMD`, `USBSTS`, `CRCR`, `DCBAAP`, interrupter state), slot usage, and per-port enumeration diagnostics such as raw `PORTSC`, speed/link state, device presence, slot assignment, and the last enumeration completion or error.
- `Graphics Devices (PCI)`: enumerates all PCI display controllers and shows bus/device/function, vendor and device identifiers, display subclass, IRQ wiring, BAR values, and whether the device is attached in the kernel PCI device list.
- `Network Devices`: enumerates PCI network controllers present on the bus and shows the PCI location, identifiers, controller type, IRQ wiring, and BAR values. When a controller is attached by the kernel network stack, the page also shows the device name, driver manufacturer and product, MAC address, active IPv4 address, link state, MTU, and initialization or readiness state.
- `PCI Devices`: walks the PCI space discovered by the kernel and prints a compact list of every enumerated function.
- `VMD (Intel Bridge)`: highlights Intel bridge devices matching the VMD-oriented detection criteria and prints the PCI location, identifiers, header type, interrupt pin, and BAR values.
- `Storage Controllers`: enumerates PCI mass-storage controllers and reports the PCI location, class/subclass/interface triplet, vendor and device identifiers, IRQ line, and BAR values.
- `IDT`: prints the IDT base and limit, then shows the installed handler offsets and selectors for a subset of active interrupt vectors.
- `GDT`: prints the GDT base and limit, then shows the decoded base and limit for the first descriptors used by the kernel execution environment.

### Logging

#### Log pipeline and format

Kernel logging funnels through `KernelLogText` and uses typed log classes.
The `DEBUG`, `WARNING`, `ERROR`, `VERBOSE`, and `TEST` macros inject `__func__` into the log path.
The log formatter emits the function tag centrally and can emit structured results such as `TEST > [CMD_sysinfo] sys_info : OK`.
Serial output is sanitized to printable ASCII (plus tab/newline) before being written to the log.
When `DEBUG_SPLIT` is `1`, kernel logs are mirrored to a dedicated right-side console region while standard console output stays on the left.
`LOG_ERROR` entries stay in the kernel log path and are not mirrored to the main console, so diagnostics do not interfere with interactive console output.

#### Log classes

Available log classes are `DEBUG`, `WARNING`, `ERROR`, `VERBOSE`, and `TEST`.
`DEBUG`, `WARNING`, `ERROR`, and `VERBOSE` are always available.
`TEST` is debug-only and is compiled out when `DEBUG_OUTPUT` is disabled.

#### Tag filtering

`KernelLogSetTagFilter()` provides optional tag-based filtering.
The filter value accepts separators (comma, semicolon, pipe, or space), and each token matches the function tag supplied by the log macro (for example `MountDiskPartitionsGpt` or `[MountDiskPartitionsGpt]`).
When filtering is active, only matching tagged lines are emitted.
The default startup filter is initialized for NVMe/GPT diagnostics.
Builds can override this value with `--kernel-log-tag-filter <value>` in `scripts/linux/build/build.sh`; passing an empty value compiles an empty default filter.

#### Recent retained log view

The log module retains one bounded in-memory view of recent formatted lines for desktop diagnostics.
Retention is owned by `Log.c` and exposed through:
- `KernelLogGetRecentSequence()`
- `KernelLogCaptureRecentLines()`

The retained view is intentionally small and bounded:
- latest `500` lines maximum,
- about `96 KiB` of recent formatted text.

This retained view is consumed by the floating desktop log viewer component in `system/portal/source/ui/LogViewer.c`.
The UI component does not access desktop internals or log storage internals directly; it only polls the public log snapshot syscall and redraws when the retained sequence changes.

#### Threshold-based one-shot logging

`ThresholdLatch` provides one-shot logging when an elapsed-time threshold is crossed during long operations.

### Automated debug validation script

The repository provides `scripts/linux/test/smoke-test-global.sh` to run an automated debug validation flow:

- clean build + image generation,
- QEMU boot,
- shell command injection (`sys_info`, `dir`, `/system/apps/hello`),
- cross-filesystem storage checks including RAM disk folder creation and copy (`/fs/n0p0` to `/fs/r0p0`),
- kernel log pattern checks.

The script supports selecting one target with `--only x86-32`, `--only x86-64`, or `--only x86-64-uefi`.  
Kernel logs are consumed from per-target files (`log/kernel-x86-32-mbr-debug.log`, `log/kernel-x86-64-mbr-debug.log`, `log/kernel-x86-64-uefi-debug.log` and release equivalents with `-release`).

### Build output layout

Build artifacts are split between a core folder and an image folder:

- core outputs: `build/core/<BUILD_CORE_NAME>/...`
- image outputs: `build/image/<BUILD_IMAGE_NAME>/...`
- `BUILD_CORE_NAME`: `<arch>-<boot>-<config>[-split]`
- `BUILD_IMAGE_NAME`: `<BUILD_CORE_NAME>-<filesystem>`

Examples:

- x86-32 MBR debug ext2:
  - core: `build/core/x86-32-mbr-debug`
  - image: `build/image/x86-32-mbr-debug-ext2`
- x86-64 UEFI debug ext2:
  - core: `build/core/x86-64-uefi-debug`
  - image: `build/image/x86-64-uefi-debug-ext2`

Path mapping (migration reference):

| Old path | New path |
|---|---|
| `build/x86-32/kernel/exos.elf` | `build/core/x86-32-mbr-debug/kernel/exos.elf` |
| `build/x86-64/kernel/exos.elf` | `build/core/x86-64-mbr-debug/kernel/exos.elf` |
| `build/x86-32/boot-mbr/exos.img` | `build/image/x86-32-mbr-debug-ext2/exos.img` |
| `build/x86-64/boot-mbr/exos.img` | `build/image/x86-64-mbr-debug-ext2/exos.img` |
| `build/x86-64/boot-uefi/exos-uefi.img` | `build/image/x86-64-uefi-debug-ext2/exos-uefi.img` |
| `build/x86-32/tools/cycle` | `build/core/x86-32-mbr-debug/tools/cycle` |
| `build/x86-64/tools/cycle` | `build/core/x86-64-mbr-debug/tools/cycle` |


### Package tooling

Host-side EPK package generation is implemented in `tools/source/package/epk_pack.c`.
The binary is built by `tools/Makefile` and produced as:
- `build/core/<build-core-name>/tools/epk-pack`

Command form:
- `build/core/<build-core-name>/tools/epk-pack pack --input <folder> --output <file.epk>`


### Keyboard Layout Format (EKM1)

The EKM1 keyboard layout format specification is maintained in a dedicated document:

- `doc/guides/binary-formats/keyboard-layout-ekm1.md`

Use that file as the single source of truth for directives, constraints, and examples.

### Process control hotkeys

Hotkeys are configured from `exos.toml` with indexed TOML array entries:
- `Hotkey.<index>.Key`: key expression (`control+c`, `shift+z`, `f5`, ...).
- `Hotkey.<index>.Action`: action name.

The key expression parser supports:
- Modifiers: `control`/`ctrl`, `shift`, `alt`.
- One non-modifier key token: `a`..`z`, `0`..`9`, `f1`..`f12`, arrows, insert/delete/home/end/pageup/pagedown, `space`, `enter`, `esc`, `tab`, `backspace`.

Keyboard handling evaluates hotkeys on key-down only, before normal input routing to the focused process queue. Repeated key-down events are ignored by hotkey matching.
When configuration does not define any `Hotkey` entries, the kernel synthesizes one implicit entry equivalent to:
- `control+c` -> `kill_process`

Supported actions:
- `kill_process`: targets the focused process.
  - If focused process is kernel process, it does not kill it and requests cooperative command interruption.
  - Otherwise it triggers process kill through process control messaging.
- `pause_process`: toggles pause for the focused process.
  - Kernel process is ignored.
  - Non-kernel process tasks are skipped by scheduler while paused.
- `switch_to_console`: switches the active display front-end back to the console.
  - This is intended for global recovery shortcuts such as `control+shift+f1`.
  - It uses the existing display session console activation path, including graphics text routing and VGA text fallback.
- `toggle_window_pipeline_trace`: toggles a desktop debug mode that visualizes computed regions and draw dispatches on screen.
  - `control+shift+f12` is handled as one built-in shortcut for this action before configuration-defined hotkeys are evaluated.
  - The flag is stored in `KERNEL_DATA` and consumed by desktop pipeline trace helpers.
  - For compatibility, the configuration action name `toggle_slow_redraw` still maps to the same handler.
- `Debug.UseDeadlockMonitor`: enables mutex deadlock diagnostics hooks in `Mutex.c` (`DeadlockMonitorOnWaitStart`, `DeadlockMonitorOnWaitCancel`, `DeadlockMonitorOnAcquire`, `DeadlockMonitorOnRelease`). Keep this disabled for normal runtime because the hooks run on the mutex hot path.

Process control messages are intercepted in task messaging before queue insertion:
- `ETM_INTERRUPT`
- `ETM_PROCESS_KILL`
- `ETM_PROCESS_TOGGLE_PAUSE`

Cooperative interruption API:
- `ProcessControlRequestInterrupt(Process)`
- `ProcessControlIsInterruptRequested(Process)`
- `ProcessControlConsumeInterrupt(Process)`
- `ProcessControlCheckpoint(Process)`

Console paging (`-- Press a key --`) integrates with cooperative interruption:
- When paging wait detects `Control+C` (virtual combination or ASCII `0x03`), it requests interruption for the current process instead of consuming the key as paging continuation.
- Fault dump paths (`LogCPUState` in x86-32/x86-64 fault handlers) force paging inactive while diagnostics are emitted, then restore the previous state.

Long command loops can place interruption checkpoints; `dir` and `dir --stress` integrate this behavior and abort listing when an interruption request is pending.

Configuration example:
```toml
[[Hotkey]]
Key = "control+c"
Action = "kill_process"

[[Hotkey]]
Key = "shift+z"
Action = "pause_process"

[[Hotkey]]
Key = "control+shift+f1"
Action = "switch_to_console"
```

### QEMU network graph

```
[ VM (Guest OS) ]                [ QEMU Process ]                  [ Host OS / PC ]
+----------------+               +-------------------+             +----------------+
|                |               |                   |             |                |
| +------------+ |               | +---------------+ |             | +------------+ |
| | E1000 NIC  | | (Ethernet     | | SLIRP Backend | | (NAT &      | | Host NIC   | |
| | (Virtual)  | |  Packets)     | | (User Net)    | |  Routing)   | | (Physical) | |
| +------------+ | ------------> | | Mini Network  | | ----------> | | e.g. eth0  | |
|                |               | | (10.0.2.0/24) | |             | +------------+ |
|                |               | +---------------+ |             |                |
|                |               |                   |             |                |
| App/TCP/IP     |               | NAT Layer         |             | Kernel TCP/IP  |
| (L3/L4)        |               | (Translate IP)    |             | (L3/L4)        |
+----------------+               +-------------------+             +----------------+
```

### Links

| Name | Description | Link |
|------|-------------|------|
| RFCs | RFC Root | https://datatracker.ietf.org/ |
| RFC 791 | Internet protocol | https://datatracker.ietf.org/doc/html/rfc791/ |
| RFC 793 | Transmission Control Protocol | https://datatracker.ietf.org/doc/html/rfc793/ |
| Intel x86-64 | x86-64 Technical Documentation | https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html |
