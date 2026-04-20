# Executable Module Libraries Implementation Plan

## Goal

Add load-on-demand executable modules for user processes without breaking the existing process, memory, and scheduler architecture.

The target model is:
- a process loads one module on demand from a filesystem path;
- the kernel validates and maps the module into the caller process;
- symbol binding is resolved against the caller process and already loaded modules;
- executable pages are backed by shared physical pages when possible;
- writable module state is split between process-global data and per-thread data;
- thread-local access uses one explicit architectural policy for x86-32 and x86-64 instead of ad-hoc caller conventions.

## Architectural Constraints

- Keep dependencies unidirectional:
  - executable parsing layer
  - module image/cache layer
  - process module binding layer
  - task TLS/context layer
  - syscall/runtime user API layer
- Do not overload `CreateProcess()` with module-specific policy. The process loader and the module loader must share reusable executable helpers, but remain separate entry points.
- Do not duplicate ELF mechanics inside process code. Extend the existing `Executable` and `ExecutableELF` layers first.
- Keep code sharing independent from relocation policy. Writable relocations must not force private copies of pure code pages.
- Keep TLS ownership on the thread side. Module code must not directly manipulate another task's architectural register state.

## Baseline Observations

- The kernel already has a generic executable front-end:
  - `kernel/include/Executable.h`
  - `kernel/source/Executable.c`
- ELF support already exists for executable images loaded into one process image:
  - `kernel/include/ExecutableELF.h`
  - `kernel/source/ExecutableELF.c`
- User address spaces already have arena partitioning:
  - `kernel/include/process/Process-Arena.h`
  - `kernel/source/process/Process-Arena.c`
- Process creation still assumes one contiguous main image loaded at `VMA_USER`:
  - `kernel/source/process/Process.c`
- Task switch code already saves/restores `FS` and `GS`, but treats them as ordinary selectors/state, not as a managed TLS base policy:
  - `kernel/source/arch/x86-64/x86-64.c`
  - corresponding x86-32 task/context code must be extended the same way.

## Recommended Model

Use a module model close to shared libraries, but aligned to the existing EXOS kernel layering:

- `ET_DYN` ELF modules for loadable libraries.
- One kernel-owned module cache object per unique file identity and build identity.
- One process-owned module binding per loaded module.
- One process-wide writable data mapping for each module instance in that process.
- One per-thread TLS block for each module that declares `PT_TLS`.
- One central process symbol resolver used by both initial process load and runtime module load.

This yields:
- shared physical code pages across processes;
- process-private writable relocations and global data;
- thread-private TLS state;
- one resolver path for future extensions such as imports between user executables and modules.

## Non-Goals For The First Milestone

- No lazy page-fault-driven symbol relocation.
- No unload while code may still be executing inside the module.
- No hot replacement of an already loaded module image.
- No kernel-space loadable driver format reuse in the first pass.
- No support for copy relocation, symbol versioning, IFUNC, audit hooks, or GNU loader compatibility details unless they are explicitly required by the chosen toolchain output.

## [x] Step 1 - Define ABI, Binary Scope, and Acceptance Rules

Implemented in:
- `doc/guides/binary-formats/executable-module-elf.md`
- `doc/guides/Kernel.md`

- Define the supported ELF subset for executable modules:
  - `ET_DYN` only for modules;
  - supported machine values for x86-32 and x86-64;
  - required program headers;
  - allowed relocation types;
  - symbol visibility/binding rules;
  - optional `PT_TLS` support requirements.
- Decide how modules declare their exported API:
  - plain ELF exported symbols first;
  - optional EXOS metadata section later if versioning is needed.
- Define the kernel rejection rules clearly:
  - unsupported relocation kind;
  - text relocation requirement;
  - writable and executable overlap;
  - malformed TLS template;
  - unresolved mandatory symbol.
- Define a stable naming and search policy:
  - absolute path only for first milestone, or
  - path resolution through `KernelPath.SystemAppsRoot` / package roots if package-based loading is required.

Acceptance criteria:
- One documentable module ABI exists and is narrow enough to validate deterministically in kernel code.

## [x] Step 2 - Generalize Executable Metadata Beyond Main Process Images

Implemented in:
- `kernel/include/exec/Executable.h`
- `kernel/include/exec/ExecutableELF.h`
- `kernel/source/exec/Executable.c`
- `kernel/source/exec/ExecutableELF.c`

- Split existing executable metadata into:
  - image layout description;
  - relocation/symbol description;
  - TLS template description;
  - mapping intent per segment.
- Extend the executable layer with reusable structures for:
  - loadable segment descriptors;
  - dynamic table summary;
  - exported/imported symbol tables;
  - relocation tables grouped by target section;
  - TLS template base/alignment/initial size/total size.
- Keep ELF parsing in `ExecutableELF*`; do not let process code parse ELF records directly.
- Introduce explicit helpers for:
  - `GetExecutableImageInfo(...)`
  - `GetExecutableModuleInfo(...)`
  - later shared lower-level parsing helpers underneath both.

Acceptance criteria:
- The loader can inspect one ELF module and obtain all information required for mapping, relocation, exports, and TLS without process-specific logic.

## [x] Step 3 - Introduce Kernel Module Image Objects

Implemented in:
- `kernel/include/exec/ExecutableModule.h`
- `kernel/source/exec/ExecutableModule.c`
- `kernel/include/core/KernelData.h`
- `kernel/source/core/KernelData.c`

- Add a kernel object representing one validated module image in memory.
- Store in that object:
  - source path or file identity;
  - architecture;
  - segment layout;
  - export table;
  - import/relocation metadata;
  - TLS template metadata;
  - reference count;
  - backing physical pages for file-backed read-only executable content.
- Keep this object independent from any single process.
- Cache module images so repeated loads reuse the validated image object instead of reparsing the file every time.
- Make code-sharing decisions here, not in process code.

Acceptance criteria:
- Loading the same module in two processes reuses one kernel module image object.

## [x] Step 4 - Extend Process Address Space Layout For Modules

Implemented in:
- `kernel/include/process/Process-Arena.h`
- `kernel/source/process/Process-Arena.c`
- `kernel/source/process/Process.c`
- `doc/guides/Kernel.md`

- Add a dedicated user arena for dynamically loaded module mappings instead of mixing them into the main image lane.
- Keep separate sub-ranges or allocation tags for:
  - shared executable/read-only segments;
  - process-private writable module data;
  - process-private relocation tables if needed;
  - process-private TLS metadata blocks;
  - optional import/export bookkeeping pages.
- Preserve the existing meaning of:
  - `Image` for the main executable;
  - `Heap` for general allocations;
  - `Stack` for task stacks;
  - `System` for process-owned service mappings;
  - `Mmio` for device mappings.
- Prefer one new arena or one well-defined reserved lane inside the current user layout. Do not grow module mappings from arbitrary heap allocations.

Acceptance criteria:
- Modules can be placed deterministically without colliding with heap growth, stacks, or the main image.

## [x] Step 5 - Add Process Module Binding Objects

Implemented in:
- `kernel/include/process/Process.h`
- `kernel/include/process/Process-Module.h`
- `kernel/source/process/Process.c`
- `kernel/source/process/Process-Module.c`
- `doc/guides/Kernel.md`

- Add one process-owned binding object per loaded module.
- Store in the binding:
  - pointer/reference to the shared kernel module image;
  - module base addresses inside the process;
  - writable data mapping base;
  - GOT/PLT or equivalent relocation targets if used;
  - module state flags;
  - per-process reference count;
  - dependency edges to other loaded modules.
- Attach bindings to the process object, not to individual tasks.
- Add owner-side helpers on `PROCESS` to query and mutate module bindings. Do not let unrelated code walk process internals while locking process mutexes directly.

Acceptance criteria:
- One process can load the same module once and share that binding across all its tasks.

## [x] Step 6 - Implement Shared Segment Mapping Policy

- Split module segments by mapping semantics:
  - executable read-only segments: shared physical pages, mapped read/execute;
  - read-only constant data: shared physical pages when relocation-free;
  - writable data and BSS: per-process private pages.
- If relocation would modify a nominally read-only page, treat that page as process-private for the first milestone instead of introducing fragile patch-in-place sharing.
- Prefer relocation models that keep code pages pure:
  - GOT/PLT or data relocations;
  - reject text relocations in first milestone.
- Ensure page flags stay architecture-correct:
  - never writable+executable;
  - user privilege for user mappings;
  - flush mapping state on install/uninstall paths.

Acceptance criteria:
- Two processes loading the same module share executable physical pages while keeping writable state isolated.

## [x] Step 7 - Implement Symbol Resolution and Relocation Binding


- Introduce one reusable process symbol resolver with this search order:
  - main executable exports;
  - already loaded process modules;
  - explicit dependency modules loaded for the target module;
  - optional kernel-provided synthetic runtime exports, if the ABI requires them.
- Resolve relocations only after all required dependencies are present.
- Perform relocation against process-private writable targets only.
- Reject unresolved mandatory symbols with short actionable errors.
- Record dependency graphs so future unload policy can prevent removing in-use modules.
- Keep relocation logic in executable/module code, not in generic process creation paths.

Acceptance criteria:
- A module can import symbols from the main executable and from another loaded module inside the same process.

## [x] Step 8 - Define Process-Global Module Data Policy

- Treat module `.data` and `.bss` as process-global state:
  - one instance per process;
  - shared by all tasks of that process.
- Initialize writable state from the module image template when the binding is first created.
- Store per-process pointers needed by relocations inside the binding object.
- Decide whether module constructors are supported in first milestone:
  - if yes, run them once per process after successful relocation;
  - if no, reject modules that require them.

Acceptance criteria:
- Two tasks in the same process observe the same module global state; two different processes do not.

## [x] Step 9 - Define TLS Model and Per-Thread Lifetime

- Treat `PT_TLS` as the source of one TLS template per module.
- On thread creation:
  - allocate a thread TLS area large enough for all loaded-module TLS blocks already bound to the process;
  - initialize each block from its module template;
  - record offsets in task-owned TLS metadata.
- On runtime module load into a process that already has threads:
  - allocate and initialize the new module TLS block for every existing task in that process;
  - update task-local TLS metadata atomically with scheduler-safe process/thread coordination.
- On future thread exit:
  - release task-owned TLS allocations through task owner paths only.
- Keep TLS bookkeeping under task/process subsystems, not inside architecture switch code.

Acceptance criteria:
- Each task gets its own instance of module thread-local data, including for modules loaded after the task already existed.

## [x] Step 10 - Define `FS` / `GS` Register Policy

- Choose one explicit policy and document it as ABI:
  - x86-64: dedicate `FS` base to user TLS pointer and reserve `GS` for kernel or future per-thread auxiliary usage;
  - x86-32: dedicate one user segment register to the TLS anchor, preferably `FS`, using a per-task descriptor/TLS base policy compatible with the current GDT/TSS model.
- Do not expose both `FS` and `GS` as free-for-all user ABI in the first milestone.
- Define one user-visible thread control block layout at the base register target:
  - self pointer;
  - process pointer or handle if desired;
  - thread identifier;
  - dynamic thread vector or module TLS offset table;
  - reserved fields for runtime expansion.
- Update context-switch code to save/restore the real TLS base state, not only selector values, where architecture requires it.
- Expose owner-side helpers such as:
  - `TaskSetUserTlsAnchor(...)`
  - `TaskGetUserTlsAnchor(...)`
  - `TaskRefreshModuleTls(...)`

Recommended first policy:
- x86-32: `FS` points to the user thread control block.
- x86-64: `FS` base points to the user thread control block.
- reserve `GS` for kernel-internal evolution and avoid making it part of the initial user ABI.

Acceptance criteria:
- The runtime has one stable way to resolve thread-local module data on both supported architectures.

## TLS Support Requirements

- Support static TLS for loadable modules as part of the initial module ABI.
- Accept only TLS layouts the kernel can materialize deterministically for every task:
  - one `PT_TLS` template per module;
  - explicit alignment;
  - explicit initialized size and total size;
  - no toolchain-emitted TLS form that requires a user-space dynamic loader.
- Define one supported access model for the first milestone:
  - local-exec and initial-exec style access only when they can be lowered to the EXOS thread control block policy;
  - reject general-dynamic and local-dynamic forms until the runtime exposes a compatible dynamic thread vector contract.
- Keep TLS relocation support narrow and explicit:
  - accept only relocation kinds required by the chosen x86-32 and x86-64 TLS code generation model;
  - reject unsupported TLS relocations during module validation before any mapping occurs.
- Require the runtime and toolchain to agree on:
  - thread control block layout;
  - module TLS offset encoding;
  - `__tls_get_addr` requirements, or the deliberate absence of that entry point in the first milestone.
- Keep late-loaded module TLS expansion mandatory:
  - existing tasks must receive initialized TLS blocks for the new module;
  - new tasks must inherit the full process module TLS set at creation time.
- Make TLS failure handling strict:
  - if one task cannot receive its TLS expansion, abort the module load and unwind every partial TLS installation in that process.

Acceptance criteria:
- One documented TLS contract exists across kernel validation, task state, runtime ABI, and module build output.

## [x] Step 11 - Add Runtime and Syscall Surface

- Add a syscall family for module operations:
  - load module by path;
  - query exported symbol address;
  - optionally pin/unpin references;
  - optionally query module metadata for diagnostics.
- Add runtime wrappers in `runtime/` so user code does not need raw syscalls.
- Decide whether symbol lookup stays kernel-side only or is exposed to userland.
- Keep module load policy centralized:
  - path validation;
  - privilege checks;
  - package/system root resolution.

Acceptance criteria:
- One user process can request module load on demand with a supported public API.

## [x] Step 12 - Constructors, Error Paths, and Unload Policy

- Define constructor/destructor support explicitly:
  - process-global constructors;
  - thread-local constructors if TLS requires them;
  - teardown ordering at process exit.
- First milestone recommendation:
  - support process teardown cleanup only;
  - defer general runtime unload until dependency tracking and in-module execution quiescence are solved.
- If unload is deferred, keep the public API honest:
  - allow load/query/use;
  - reject unload with `not implemented` until safe.

Acceptance criteria:
- Failure during dependency load, relocation, or TLS expansion unwinds without leaking half-bound process module state.

## [x] Step 13 - Toolchain and Build Output

- Add one module link mode in the userland build system:
  - produce `ET_DYN`;
  - emit supported relocation model only;
  - emit `PT_TLS` when thread-local storage is used;
  - avoid unsupported interpreter or dynamic loader dependencies.
- Define runtime headers/macros needed by applications:
  - import/export annotations if required;
  - TLS model assumptions;
  - module entry point or constructor conventions if supported.
- Keep the generated binaries aligned with the kernel validation rules from Step 1.

Acceptance criteria:
- The repository can build one sample executable plus one loadable module without post-link patching.

## [x] Step 14 - Userland Integration Validation

- Add userland integration tests for:
  - load one module and call exported functions;
  - one module importing a symbol from another loaded module through the process resolver;
  - module global data shared across tasks in one process;
  - module global data isolated across separate processes;
  - TLS isolation across two threads in one process;
  - late module load after an additional thread already exists.
- Add the integration module binaries to the build and boot images.
- Validate the integration workflow on both x86-32 and x86-64.

Acceptance criteria:
- The smoke matrix proves process-global module state, per-thread TLS, late TLS expansion, and supported module-to-module imports.

## [ ] Step 15 - Kernel-Side Validation Matrix

- Add focused kernel-side tests for:
  - ELF module metadata parsing;
  - relocation validation and rejection cases;
  - TLS template parsing;
  - process module binding lifetime;
  - failure unwind during dependency load, relocation, and TLS expansion.
- Add remaining integration coverage where useful for:
  - main executable symbol imported by module;
  - shared executable code pages across two processes;
  - unsupported module import orders rejected deterministically.
- Validate both x86-32 and x86-64 before considering the feature complete.

Acceptance criteria:
- Kernel-side tests prove rejection and cleanup paths that the smoke workflow cannot cover deterministically.

## Suggested Implementation Order

1. Define the ABI and rejected ELF subset.
2. Refactor executable metadata parsing so modules are first-class objects.
3. Add kernel module image cache objects.
4. Add process module binding structures and address-space placement.
5. Implement shared executable/read-only segment mapping.
6. Implement symbol resolution and relocation.
7. Implement process-global writable data handling.
8. Implement TLS metadata and thread expansion paths.
9. Implement the `FS` policy end to end on x86-32 and x86-64.
10. Add syscall/runtime API.
11. Add one sample module workflow and cross-architecture tests.

## Recommended First Deliverable

Keep the first deliverable narrow:

- x86-32 first;
- `ET_DYN` module load by absolute path;
- shared executable pages;
- per-process global `.data/.bss`;
- per-thread `PT_TLS`;
- `FS` as the user TLS anchor;
- no unload;
- no constructors unless the chosen toolchain output requires them immediately.

Once that path is stable, port the same ABI and loader structure to x86-64 with the same user-visible TLS model and `FS`-based anchor policy.
