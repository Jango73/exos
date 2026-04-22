# TinyCC Import Metadata For EXOS

## Upstream Source

- Source: https://repo.or.cz/tinycc.git
- Branch: `mob`
- Imported commit: `9b8765d`

## Import Scope

The upstream source tree is imported so the EXOS bootstrap application can
select the TinyCC target that matches the EXOS build architecture.

The upstream license text is kept in `COPYING`.

The EXOS TinyCC configuration header is provided by
`system/tcc/include/config.h` through the wrapper build include path, so the
imported TinyCC source folder does not carry EXOS-specific configuration edits.

The upstream `tests`, `win32`, `examples`, `lib`, and `include` folders are
intentionally excluded because they are not used by the EXOS build and would
unnecessarily increase the codebase size. Upstream build, documentation, and
helper scripts not used by EXOS are excluded for the same reason. Backends for
architectures not targeted by EXOS may be excluded when they are not required
by the x86-32 or x86-64 build.

## License Scope For The EXOS TinyCC Build

EXOS builds TinyCC through `system/tcc/source/tcc-main.c` with `ONE_SOURCE=1`
and `TCC_TARGET_I386` or `TCC_TARGET_X86_64` (see `system/tcc/Makefile`).
That selection pulls the following TinyCC compilation units:

- `tcc.c`
- `libtcc.c`
- `tcctools.c`
- `tccpp.c`
- `tccgen.c`
- `tccdbg.c`
- `tccasm.c`
- `tccelf.c`
- `tccrun.c`
- `i386-gen.c`
- `i386-link.c`
- `i386-asm.c`
- `x86_64-gen.c` (x86-64 target)
- `x86_64-link.c` (x86-64 target)
- `tcc.h`, `elf.h`, `dwarf.h` (headers included by the units above)

For these units, the effective license notices are LGPL "version 2 (or 2.1)
or any later version", or dual GPL/LGPL with an "or any later version" clause
for imported headers (for example `dwarf.h`). This is compatible with GPLv3
distribution because the "or later" grant allows applying GPLv3 terms.

Non-selected TinyCC code paths that carry different terms are not built by the
EXOS TinyCC targets:

- `il-opcodes.h` (GPL, used by `il-gen.c` only; not referenced by EXOS targets)
- `tcctools.c` `tiny_impdef` section (GPL, guarded by `TCC_TARGET_PE`; EXOS
  does not define `TCC_TARGET_PE` for x86-32 or x86-64)

## Compliance Note For Redistribution

- Keep `third/tinycc/COPYING` in distributed source packages.
- Keep original copyright/license notices in imported files.
- Distribute EXOS under GPLv3 terms for combined works.
