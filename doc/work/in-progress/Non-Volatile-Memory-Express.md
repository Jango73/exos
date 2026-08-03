# NVMe Implementation Roadmap

## Prerequisites
- [x] PCIe enumeration working (class 0x01, subclass 0x08 for NVMe).
- [x] MMIO mapping (typically BAR0).
- [x] DMA buffers: physically contiguous, 4 KiB aligned.
- [ ] Interrupts: MSI/MSI-X preferred (INTx fallback).
- [x] Logging: concise traces (register reads/writes, SQ/CQ entries, status codes).

## Step 1 — Detect NVMe Controller
Goal: confirm NVMe presence and read capabilities.  
- [x] Scan PCI config, find class 0x0108.  
- [x] Map BAR0, read CAP, VS, CC, CSTS, AQA, ASQ, ACQ.  
- [ ] Record version and queue entry size limits in persistent controller state.  
Success: kernel log shows controller version and max queue depth.

## Step 2 — Admin Queue Setup
Goal: enable the controller and exchange admin commands.  
- [x] Allocate ASQ/ACQ in DMA memory and program AQA/ASQ/ACQ.  
- [x] Set CC.EN = 1, wait for CSTS.RDY = 1.  
Success: kernel log confirms the controller is ready.

## Step 3 — Identify Controller & Namespace
Goal: retrieve identity and basic namespace info.  
- [x] Identify Controller (Admin 0x06) → serial, model, firmware.  
- [x] Identify Namespace (Admin 0x06, CNS=0x00, NSID=1).  
- [ ] Parse LBA formats completely (size, metadata, active format constraints).  
Success: kernel log shows NSID=1 with capacity in sectors.

## Step 4 — Create I/O Queues
Goal: operational I/O submission/completion queues.  
- [x] Create I/O CQ (Admin 0x05), Create I/O SQ (Admin 0x01).  
- [x] Route CQ to an MSI/MSI-X vector and arm interrupts.
- [ ] Implement a functional interrupt completion path; current code still relies on synchronous polling for completions.  
Success: dummy no-op commands complete on the I/O CQ.

## Step 5 — Read Sectors
Goal: implement the read path.  
- [x] Build PRP (1–2 pages) for destination buffer.  
- [x] Submit Read (0x02) to I/O SQ and wait for completion.  
Success: kernel log shows the MBR signature read from LBA0.

## Step 6 — Write Sectors
Goal: implement the write path.  
- [x] Build PRP for source buffer and submit Write (0x01).  
- [ ] Optionally issue Flush (0x00).  
Success: write a signature and verify by reading it back.

## Step 7 — Namespace Management
Goal: support multiple namespaces.  
- [x] Enumerate all namespaces (Identify CNS=0x02).  
- [x] Expose each namespace as a DISK object (OBJECT_FIELDS, KOID_DISK).  
- [x] Implement DF_DISK_READ/WRITE/GETINFO/SETACCESS for the NVMe driver.  
- [x] Add each NVMe disk to `GetDiskList()` so filesystem mount can use it.  
- [x] Ensure the partition scan runs on NVMe disks so EXT2 can mount.  
Success: shell command `disk` lists all NVMe namespaces with capacities and the filesystem mount sees the EXT2 partition.

## Step 8 — Error Handling & Reset
Goal: robust recovery.  
- [x] Handle wrap of SQ tail/CQ head.  
- [ ] Handle timeouts with explicit recovery paths.  
- [ ] On CSTS.CFS = 1: disable CC.EN, wait RDY=0, reinitialize.  
- [ ] Decode status codes centrally (phase tag, SC, SCT), retry where appropriate.  
Success: bad commands or device hiccups do not panic the kernel.

## Step 9 — Performance & Multiqueue
Goal: scale with CPUs and load.  
- [ ] Per-CPU I/O SQ/CQ pairs.  
- [ ] Batch doorbells and support multiple outstanding commands.  
- [ ] One MSI-X vector per CQ when available.  
Success: sequential throughput increases with CPU count/queues.

## Step 10 — Features & Maintenance
Goal: useful admin features.  
- [ ] Get/Set Features: number of queues, write cache, arbitration.  
- [ ] SMART/Health Log: temperature, media errors, endurance stats.  
- [ ] Dataset Management/TRIM (0x09).  
Success: kernel log or shell output shows key health metrics.

## Step Decoupling (each step runs without the next)
- 1–3: read-only discovery, safe to ship.  
- 4: live queues, still no data movement required.  
- 5: minimal read path, enough to mount filesystems read-only.  
- 6: adds writes.  
- 7: multi-namespace abstraction.  
- 8–9: robustness and performance.  
- 10: optional enhancements.  

## Integration into EXOS
Goal: integrate NVMe cleanly with existing EXOS driver and disk layers.  

### Reuse and align with existing code
- Driver model: follow the PCI driver pattern used by `kernel/source/drivers/SATA.c` and `kernel/source/drivers/XHCI-*.c`.
- Device lists: reuse `GetDiskList()` and disk info structs already used by AHCI (see `kernel/source/drivers/SATA.c`).
- DMA + mapping: reuse `MapIOMemory`, `MapLinearToPhysical`, `KernelHeapAlloc`, `KernelHeapFree`, and cache helpers in `kernel/include/utils/Cache.h`.
- Interrupts: reuse `DeviceInterruptRegister` and the top-half/bottom-half pattern from AHCI and XHCI.
- Driver enumeration: expose NVMe through `DriverEnum` in the same style as AHCI/USB if needed.

### Proposed modules and file layout
- `kernel/include/drivers/NVMe-Core.h`: register definitions, queue entries, command opcodes, driver structs.  
- `kernel/source/drivers/NVMe-Core.c`: PCI probe, attach, controller init, admin queue, I/O queue, read/write path.  
- Optional shared helpers (only if duplication is real and not trivial):
  - `kernel/include/drivers/PCIe-Helpers.h` and `kernel/source/drivers/PCIe-Helpers.c` for PCIe capabilities/MSI-MSIX parsing.
  - `kernel/include/drivers/DMA-Helpers.h` and `kernel/source/drivers/DMA-Helpers.c` for aligned DMA alloc/free.

### PCI integration
- Register `NVMePCIDriver` in `kernel/source/drivers/PCI.c` alongside AHCI/XHCI.
- Match class 0x01, subclass 0x08, progIF 0x02.
- Store BAR0 MMIO base and size; map with `MapIOMemory`.

### Disk integration (read/write)
- Expose each namespace as a `DISK` object with `OBJECT_FIELDS` and `KOID_DISK`.
- Add each NVMe disk to `GetDiskList()` just like AHCI does in `InitializeAHCIController`.
- Implement `DF_DISK_READ/WRITE/GETINFO/SETACCESS` on the NVMe driver, matching the AHCI disk interface.
- [ ] Reuse the sector cache (`CacheInit`, `CacheFind`, `CacheAdd`, `CacheCleanup`) for read path parity with AHCI.

### Scheduling and polling
- [ ] Provide poll-mode handler for interrupts (see `AHCIInterruptPoll`) to keep early boot functional.
- [ ] Ensure queue submission does not busy-loop; yield or sleep where needed.

### Error handling and reset
- Follow the style in AHCI: concise `WARNING`/`ERROR` logs, no flood.
- On fatal controller errors, disable CC.EN, wait for RDY=0, reinit queues.

### System Data View and shell
- Add a System Data View page for NVMe controllers (optional but useful for bare metal).
- [x] Add a shell command `nvme` with `list` first.
- [ ] Extend the shell command with `info` and `smart` as the driver matures.

### Tests and validation
- On bare metal, validate PCI detection first, then admin queue readiness.

## State Summary
- Admin queue setup, identify commands, one I/O queue pair, read path, write path, namespace enumeration, disk registration, and partition mounting are implemented.
- The main remaining work is robustness: functional interrupt-driven completion, timeout recovery, controller reset on fatal status, centralized status decoding, and broader validation of write persistence.
- The roadmap above tracks missing behavior, not merely uncommitted code.
