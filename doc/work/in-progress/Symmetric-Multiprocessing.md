# Symmetric Multiprocessing (SMP) Implementation Roadmap

Goal: enable n-core SMP on x86-32 and x86-64 using Local APIC + IOAPIC, without regressing the existing single-core flow. The system must always be able to boot and run in legacy monoprocessor mode.

## Hard Requirements
- Legacy monoprocessor mode must remain fully functional (boot and run "a l'ancienne") even when SMP code is present.
- Which CPUs are usable is chosen entirely through the TOML configuration.
- Per-process CPU assignment is done at launch time from the shell via a launch option.
- The `nosmp` boot flag provides a runtime fallback to monoprocessor mode.

## Configuration Surface (new keys)
Added to `[General]` in `kernel/configuration/exos.ref.toml` / `exos.ext2.toml`:
- `SMP=0|1` — master switch. `0` keeps the current single-CPU code path untouched.
- `EnabledCPUs=<bitmask>` — bitmask selecting which detected APIC IDs are usable (bit N = APIC ID N). Empty/unset means "all detected". The boot log lists detected APIC IDs to build the mask.
- `LAPICTimer=1` (default) — use per-CPU LAPIC timers for the scheduler tick; `0` keeps PIT on all CPUs.
Effective CPU set = detected CPUs AND `EnabledCPUs`, forced to 1 when `SMP=0` or `nosmp`.

## Shell Launch Option (new)
- `run` gains `--cpu=N` (with `-cN` short form via `GetListOptionShortValue`) pinning the launched process to CPU N.
- Threaded through `CMD_run` -> `SpawnExecutable` -> `ShellLaunchCommandLine` -> `Spawn`/`CreateProcess` -> `KernelCreateTask` -> `TASK_INFO.CpuId`.
- Default affinity = ANY (any CPU), preserving current behavior.

---

## Step 1 — CPU Discovery and Boot Policy
Goal: enumerate usable CPUs from ACPI and apply the configuration.
- Reuse MADT parsing (`ACPI.c` `ParseMADT`, `G_LocalApicInfo[32]`, `G_AcpiConfig.LocalApicCount`); validate APIC base from `IA32_APIC_BASE_MSR` vs ACPI override; refuse SMP if APIC disabled or no MADT.
- Read `SMP`, `EnabledCPUs`, `nosmp`; compute the effective CPU set and BSP selection.
- Store per-CPU records (APIC ID, ACPI ProcessorId, flags, status) for both architectures.
- Success: boot log lists all detected CPUs with APIC IDs and the selected BSP; monoprocessor config boots exactly as before.

## Step 2 — BSP Early Init with APIC Mode
Goal: BSP switches to APIC interrupt delivery without breaking single-CPU boot.
- Enable Local APIC on BSP (`IA32_APIC_BASE_MSR` ENABLE, spurious vector set, LVT masks default), keeping the existing LAPIC enable path.
- Transition interrupt mode: PIC masked, IOAPIC initialized, spurious vector configured; route legacy IRQs through IOAPIC redirections to the BSP.
- Prepare LAPIC timer calibration scaffolding (PIT as reference clock).
- Success: BSP runs with LAPIC enabled and interrupts delivered via IOAPIC; `SMP=0` still uses the current PIC path.

## Step 3 — AP Bootstrap Path
Goal: APs start through an existing-style trampoline and reach a C entry.
- Copy a parameterized trampoline stub to low memory (`LOW_MEMORY_PAGE_4`, 0x4000 — currently unused); parameters: per-AP stack, CR3, GDT/IDT pointers, target C entry.
- BSP sends INIT + SIPI to each usable APIC ID (skipping BSP), reusing `RealModeCalls` self-copy style.
- In trampoline: set per-AP temporary stack, load GDT, enable paging with BSP CR3, enable LAPIC, jump to AP C entry.
- AP C entry builds per-CPU stack/TSS, loads a per-CPU IDT, signals "online", parks in the idle loop.
- Success: every AP reaches its C entry without new bootstrap infrastructure.

## Step 4 — Per-CPU Structures and Accessors
Goal: each CPU can read its own state without global locks.
- Introduce `CPU` struct: APIC ID, ACPI ProcessorId, Status (offline/booting/online), per-CPU stack/TSS pointers, CurrentTask, LocalAPIC base, LAPIC timer state, per-CPU flags, statistics.
- Per-CPU accessors `CurrentCPU()` / `PerCPUGet` / `PerCPUSet`: GS-based on x86-64 (coordinate with existing user TLS GS use), per-CPU array indexed by APIC ID on x86-32.
- Global CPU table + online bitmap; BSP entry initialized before AP start.
- Success: any CPU obtains its CPU struct without locking.

## Step 5 — Scheduler/Task Affinity
Goal: independent scheduling per CPU with task affinity.
- Replace the single global `TaskList` with per-CPU run queues plus a global queue for orphan tasks; keep a monoprocessor fast path.
- Add `CpuId` affinity field to `TASK_INFO` (default ANY); pin kernel threads (e.g., idle) to their CPU.
- Reschedule IPIs to wake remote CPUs on enqueue; protect run queues with spinlocks; make `FreezeScheduler`/`UnfreezeScheduler` CPU-aware.
- Success: round-robin scheduling runs independently on each CPU; tasks migrate when affinity allows.

## Step 6 — Interrupts and IPIs
Goal: IRQs and IPIs delivered and acknowledged on all CPUs.
- Allocate vectors in the free 80–255 range: Reschedule IPI, TLB Shootdown, Generic Call, LAPIC Timer, Error/Spurious; document the vector map.
- IOAPIC redirections deliver device IRQs to a chosen CPU; per-CPU LAPIC EOI paths and interrupt statistics.
- Success: IRQs acknowledged via LAPIC on all CPUs; IPIs received and handled.

## Step 7 — Timing and Tick Source
Goal: stable per-CPU scheduler tick with a single timekeeper.
- Calibrate LAPIC timer once (PIT reference); switch scheduler tick to per-CPU LAPIC timer; PIT disabled afterward unless `LAPICTimer=0`.
- Single timekeeper CPU increments wallclock; jiffies/system time shared atomically.
- Success: stable 10 ms tick on each CPU, consistent uptime/time across CPUs.

## Step 8 — Synchronization Primitives
Goal: multi-CPU correctness for all shared state.
- Add atomic ops (Base.h types) and spinlocks; audit Mutex for SMP safety (currently interrupt-disable + cooperative only).
- Protect scheduler, process lists, handle map, memory allocators (buddy allocator is `MUTEX_MEMORY`-only) with locks or per-CPU structures; per-CPU temp mapping slots.
- Interrupt-disable + spinlock pairs in all interrupt-reachable paths.
- Success: no data races under stress (task create/kill, IPC, file ops).

## Step 9 — Memory and TLB Coherency
Goal: coherent address space view across CPUs.
- TLB shootdown: track CPUs running an address space, send shootdown IPIs with barrier.
- Per-CPU CR3, kernel stacks, IST/ESP0 in per-CPU TSS at every task switch.
- SMP-safe page allocator (locks or per-CPU caches).
- Success: mapping changes visible on all CPUs; no stale TLB.

## Step 10 — Boot/Shutdown Flow and Debug
Goal: orderly multi-CPU lifecycle.
- Reorder boot: ACPI -> APIC init -> CPU bring-up -> scheduler start; log lines prefixed with CPU ID.
- CPU offline/shutdown hook (halt APs) for clean reboot.
- Update `doc/guides/Kernel.md`.
- Success: clean multi-CPU boot logs and orderly halt/reboot.

## Step 11 — Testing Matrix
- QEMU `-smp 4` on both x86-32 and x86-64 (`scripts/linux/run/run.sh` `-smp` at line 472; add a CLI flag): all CPUs online, tasks scheduled, no faults in `log/kernel*.log`.
- Legacy: `SMP=0` and `nosmp` boots identical to current single-CPU behavior.
- Config: `EnabledCPUs` subsets verified against `taskStat`/`cpu` output.
- Affinity: `run --cpu N` pins the process; ANY migrates.
- Stress: spawn many tasks, IPC, file ops; no deadlocks/races.
- Timer: tick accuracy before/after LAPIC switch; device IRQs (keyboard/net) and IPIs verified.

---

## Risk Mitigation / Compatibility
- `SMP=0` / `nosmp` fallback path stays fully functional until SMP is stable.
- If PIT compatibility is costly, retire PIT after LAPIC timer is enabled and calibrated.
- Guard new vectors against the existing IRQ layout; document the vector map.
- New SMP module files placed under `kernel/source/smp/` + `kernel/include/smp/`; add a `$(wildcard source/smp/*.c)` line to `kernel/Makefile`.

## State Summary
- Step 1 done: CPU discovery and boot policy implemented in `kernel/source/smp/SMP.c` + `kernel/include/smp/SMP.h`. Effective CPU set driven by `General.SMP`, `General.EnabledCPUs`, `nosmp`; per-CPU records with BSP selection; monoprocessor config boots exactly as before (verified on QEMU x86-32/x86-64, `detected=1 usable=1`).
- Step 2 done: BSP APIC mode was already active in the default path (IOAPIC AUTO mode enables LAPIC, masks PIC, routes legacy IRQs through IOAPIC to the BSP). Added LAPIC timer calibration scaffolding in `kernel/source/smp/LAPICTimer.c` + `kernel/include/smp/LAPICTimer.h`: 10 ms PIT one-shot reference window, divide-by-16, calibrated frequency stored and logged (`CalibrateLAPICTimer`, `GetLAPICTimerFrequency`). Not yet wired into the scheduler tick.
- Still pending: Steps 3-11 (AP trampoline bring-up, per-CPU structures/accessors, scheduler affinity + `run --cpu N`, IPIs, per-CPU LAPIC timer tick + single timekeeper, atomics/spinlocks, TLB shootdown, boot/shutdown flow + Kernel.md update, QEMU `-smp` testing matrix).
- This roadmap tracks missing behavior, not merely uncommitted code.
