# ELF Executable Module ABI

## Scope

This document defines the first EXOS loadable executable module ABI.
It freezes the accepted ELF subset for user process modules so kernel validation can reject unsupported binaries deterministically before any mapping occurs.

This ABI applies only to userland loadable modules.
Main executable images keep using the existing executable loader flow.

All numeric ELF fields are little-endian.

## File Identity and Naming Policy

- first milestone load policy: absolute filesystem path only;
- no implicit search through package roots;
- no `PT_INTERP`;
- no user-space dynamic loader contract;
- one path identifies one candidate module image before later cache identity refinement.

Future path indirection through `KernelPath.SystemAppsRoot` or package roots may be added by a later milestone, but it is outside this ABI definition.

## Accepted ELF Header Contract

- `e_ident[EI_MAG0..EI_MAG3]` must be the ELF signature;
- `e_ident[EI_DATA]` must be `ELFDATA2LSB`;
- `e_ident[EI_VERSION]` must be `EV_CURRENT`;
- `e_type` must be `ET_DYN`;
- `e_machine` must be:
  - `EM_386` for x86-32 modules;
  - `EM_X86_64` for x86-64 modules;
- `e_phnum` must be non-zero;
- program header table must fit entirely inside file bounds;
- section headers are optional for loading, but dynamic linking metadata referenced by program headers must remain inside file bounds.

Rejected header conditions:
- wrong architecture for the target kernel build;
- `ET_EXEC`, `ET_REL`, or any non-`ET_DYN` file;
- big-endian or unknown ELF data encoding;
- truncated ELF header or truncated program header table.

## Program Header Contract

Required program header set:
- at least one `PT_LOAD`;
- exactly one `PT_DYNAMIC`;
- zero or one `PT_TLS`.

Allowed program header types for the first milestone:
- `PT_LOAD`
- `PT_DYNAMIC`
- `PT_TLS`
- `PT_GNU_STACK`
- `PT_NOTE`
- `PT_PHDR`

Rejected program header conditions:
- any `PT_INTERP`;
- more than one `PT_DYNAMIC`;
- more than one `PT_TLS`;
- overlapping loadable segments after page alignment;
- any loadable segment with `p_memsz < p_filesz`;
- writable and executable overlap in one loadable segment;
- segment range or file range outside supported bounds.

Mapping policy implied by validation:
- executable segments must be read/execute, never writable;
- writable segments must be read/write, never executable;
- read-only constant data may be shared only when relocation-free;
- text relocations are forbidden.

## Dynamic Table Contract

The module must expose one `PT_DYNAMIC` table describing all relocation and symbol metadata required by the kernel loader.

Required dynamic information:
- symbol table;
- string table;
- relocation tables required by the accepted relocation model.

Optional dynamic information:
- hash table support used only for lookup acceleration;
- init/fini metadata is not part of the first milestone ABI.

Rejected dynamic metadata conditions:
- missing symbol or string table;
- relocation table outside file-backed data bounds;
- dynamic entries requiring a user-space ELF interpreter contract;
- constructor/destructor requirements outside the accepted module ABI.

## Exported API Policy

- exported API is plain ELF global symbol export;
- no EXOS-specific metadata section is required in the first milestone;
- symbol versioning is unsupported;
- IFUNC is unsupported;
- weak exports may exist, but kernel resolution does not depend on GNU loader compatibility rules.

Export eligibility:
- symbol must have a name in the dynamic string table;
- symbol must resolve to one defined symbol inside the main executable or one loaded module when imported;
- exported function and object symbols must land in accepted mapped segments.

## Symbol Resolution Rules

The kernel module resolver uses one deterministic mandatory-symbol policy:
- undefined strong symbol: reject;
- undefined weak symbol: may resolve to zero only if the chosen relocation model permits it;
- hidden or internal symbols are not visible outside the defining image;
- duplicate exported names across already loaded providers are resolved by the process resolver policy defined by the module-loading milestone, not by ad-hoc ELF order.

The first milestone exported/imported symbol scope is:
- main executable exports;
- already loaded process modules;
- explicit dependency modules loaded before relocation resolution completes.

## Accepted Relocation Subset

The accepted relocation subset stays intentionally narrow.
Only relocations that preserve pure executable pages and can be applied by the kernel without a user-space loader are valid.

Accepted non-TLS policy:
- relocations must target writable module state or writable indirection tables;
- relocations that require patching executable code are rejected;
- copy relocations are rejected;
- PLT/GOT-style indirection is acceptable only when relocation writes stay in writable pages.

Accepted architecture-specific relocation families for the first milestone:

### x86-32

- absolute or relative data relocations needed by position-independent `ET_DYN` output;
- GOT-based imports when relocation writes stay in writable targets;
- TLS relocations only from the accepted static TLS model.

Rejected x86-32 relocation families:
- text relocations;
- copy relocations;
- relocations requiring lazy binding trampolines;
- unsupported TLS general-dynamic or local-dynamic forms.

### x86-64

- relative relocations for position-independent data fixups;
- GOT-based imports when relocation writes stay in writable targets;
- TLS relocations only from the accepted static TLS model.

Rejected x86-64 relocation families:
- text relocations;
- copy relocations;
- IFUNC-related relocations;
- lazy binding requirements;
- unsupported TLS general-dynamic or local-dynamic forms.

The precise accepted relocation constants must be encoded in kernel validation alongside this document when module parsing support is added.
Any relocation kind outside the explicit allowlist must be rejected.

## TLS Contract

`PT_TLS` is optional.
When present, the module must obey these rules:

- exactly one `PT_TLS` program header;
- one deterministic TLS template per module;
- explicit initialization size, total size, and alignment;
- template bytes must lie inside file bounds;
- total size must be greater than or equal to initialization size;
- alignment must be non-zero and compatible with kernel allocation rules.

Accepted TLS access model for the first milestone:
- static TLS only;
- local-exec or initial-exec style code generation only when it matches the EXOS thread control block contract;
- no `__tls_get_addr` dependency.

Rejected TLS conditions:
- malformed template bounds;
- multiple TLS templates;
- unsupported TLS relocation kind;
- TLS layout requiring a dynamic thread vector ABI not yet exposed by runtime;
- late-load TLS expansion failure for any task in the target process.

## Toolchain Output

The userland module build mode emits `ET_DYN` ELF images with one dynamic table, SYSV hash metadata, no interpreter segment, and non-executable stack marking.
Module code uses position-independent output with hidden default visibility; public exports must opt in through the runtime export annotation.
Thread-local storage uses the static TLS model exposed by the runtime header so TLS use emits one `PT_TLS` template without a user-space dynamic loader dependency.

## Stack Marking Policy

`PT_GNU_STACK` is optional.
If present:
- executable stack requests are rejected;
- non-executable stack requests are accepted.

If absent:
- the kernel keeps the default non-executable user stack policy.

## Deterministic Kernel Reject Classes

Kernel validation must produce stable rejection categories for module load diagnostics.
The first milestone categories are:

- invalid ELF signature;
- unsupported ELF class or machine;
- unsupported ELF type;
- invalid program header table bounds;
- missing required program header;
- unsupported program header type;
- writable and executable overlap;
- text relocation required;
- unsupported relocation kind;
- malformed dynamic table;
- malformed TLS template;
- unresolved mandatory symbol;
- unsupported constructor or interpreter dependency.

The exact constant names can be introduced with the module parser work, but the categories above are frozen by this ABI.

## Acceptance Summary

One module is accepted by the first ABI only when all of the following are true:

- it is an `ET_DYN` little-endian ELF for the target architecture;
- it contains one valid dynamic table and at least one loadable segment;
- every relocation belongs to the explicit supported subset;
- no executable page requires runtime patching;
- no segment is both writable and executable;
- optional TLS metadata is fully materializable by the kernel for all tasks;
- every mandatory import resolves through the process resolver policy;
- the module is requested by absolute path.
