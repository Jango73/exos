# Floppy Drive Support Implementation Roadmap

## Goal
- Detect the floppy controller exposed by QEMU at boot.
- Register each floppy drive as a normal `STORAGE_UNIT` in `Kernel.Disk`.
- Read and write sectors through the shared disk interface.
- Mount floppy media even when the medium is a raw FAT12 superfloppy without MBR.
- Expose mounted volumes through the existing filesystem pipeline.

## Current State
- QEMU exposes a floppy controller and a floppy drive during boot.
- The kernel has a logical driver type for floppy disks (`DRIVER_TYPE_FLOPPYDISK`) and naming support for `/fs/f...`.
- There is no floppy controller driver, no floppy disk driver registration, no sector I/O path, and no FAT12 mount path.
- The generated floppy image is a 1.44 MiB FAT12 image written directly to the medium, not an MBR-partitioned disk.

## Non-Goals
- No USB floppy support in the first phase.
- No advanced formatting utility in the first phase.
- No attempt to emulate hard disk semantics on floppy media.

## Step 0 - Define the Driver Split
Goal: keep controller logic and disk logic separated.
- [ ] Add a dedicated floppy controller module under `kernel/source/drivers/storage`.
- [ ] Add a dedicated floppy disk access module under `kernel/source/drivers/storage`.
- [ ] Add public headers under `kernel/include/drivers/storage`.
- [ ] Register the floppy disk driver through the normal startup path in `InitializeDriverList()`.
- [ ] Add a getter in `kernel/include/DriverGetters.h`.
Success: the codebase has a clear controller/disk structure matching ATA/SATA/NVMe style.

## Step 1 - Detect the Controller and Drives
Goal: discover whether a floppy controller and one or more drives are present.
- [ ] Read CMOS floppy drive type fields and determine installed drives.
- [ ] Define supported drive geometries for the first phase:
- 3.5" 1.44 MiB is mandatory.
- 5.25" and other formats are optional and can be rejected explicitly at first.
- [ ] Create one kernel object per detected drive and attach the floppy disk driver to it.
- [ ] Add each detected drive to `Kernel.Disk`.
- [ ] Keep logs concise and actionable.
Success: the kernel log reports detected floppy drives and `GetDiskList()` contains floppy entries.

## Step 2 - Implement Low-Level FDC Access
Goal: talk to the ISA floppy disk controller safely.
- [ ] Define controller register constants and status bits.
- [ ] Implement command submission and reply parsing.
- [ ] Implement controller reset sequence.
- [ ] Implement IRQ 6 completion handling.
- [ ] Implement DMA setup for sector transfers.
- [ ] Add timeout handling using `HasOperationTimedOut()` for early boot safety.
- [ ] Implement motor control with bounded wait logic.
- [ ] Implement drive select, recalibrate, seek, and sense interrupt status.
Success: a diagnostic path can reset the controller, seek, and complete one sector transfer without fault.

## Step 3 - Implement Block I/O Through `DF_DISK_*`
Goal: expose floppy media through the shared storage contract.
- [ ] Implement `DF_DISK_RESET`.
- [ ] Implement `DF_DISK_READ`.
- [ ] Implement `DF_DISK_WRITE`.
- [ ] Implement `DF_DISK_GETINFO`.
- [ ] Implement `DF_DISK_SETACCESS`.
- [ ] Fill `DISKINFO` with removable media semantics and correct geometry-derived sector count.
- [ ] Reuse `DISKGEOMETRY` and `SectorToBlockParams()` for CHS addressing.
- [ ] Handle read-only mode and absent-media cases cleanly.
Success: shell and filesystem code can read sector 0 through the generic disk API.

## Step 4 - Media Presence and Change Handling
Goal: make removable-media behavior correct enough for boot/runtime use.
- [ ] Track whether media is present.
- [ ] Detect media change when possible and invalidate any floppy-side cached state.
- [ ] Return a clear failure when media disappears during access.
- [ ] Re-read geometry/media state after reset or change.
- [ ] Keep the first phase simple: polling-based presence checks are acceptable.
Success: remove/insert cycles do not leave stale mounted state silently in use.

## Step 5 - Add Superfloppy Mount Support
Goal: mount floppy media that does not contain an MBR.
- [ ] Extend the filesystem discovery path so removable floppy media can fall back to direct volume probing when sector 0 is not an MBR/GPT partition table.
- [ ] Keep the existing MBR/GPT path unchanged for normal disks.
- [ ] Add a dedicated helper instead of special-casing floppy logic inline in multiple places.
- [ ] Record partition metadata consistently for superfloppy media:
- scheme should distinguish "raw volume" from MBR/GPT, or reuse an agreed fallback with explicit documentation.
- partition index should remain deterministic.
- [ ] Ensure SystemFS volume naming still uses the floppy prefix `f`.
Success: a raw floppy image can be mounted from sector 0 without fake partition entries.

## Step 6 - Add FAT12 Support
Goal: mount the floppy filesystem format actually produced by the build system.
- [ ] Introduce a FAT12 mount path rather than forcing FAT16 logic onto FAT12 media.
- [ ] Reuse shared FAT helpers where possible.
- [ ] Keep FAT12-specific cluster semantics in dedicated code paths.
- [ ] Detect FAT12 from the boot sector, not from MBR partition type alone.
- [ ] Support read access first if write support slows down bring-up too much.
- [ ] Add write support only after read-only mounting is stable.
Success: the QEMU floppy image created with `mkfs.fat -F 12` mounts successfully.

## Step 7 - Integrate With Existing Filesystem Discovery
Goal: make floppy volumes appear naturally in the system.
- [ ] Ensure `InitializeFileSystems()` scans floppy disks from `Kernel.Disk` exactly like other disks.
- [ ] Ensure mounted floppy filesystems appear under `/fs/f0p0` or the chosen raw-volume naming rule.
- [ ] Ensure unmounted/unsupported floppy media appears in diagnostics as unused storage where appropriate.
- [ ] Ensure the active system partition selection logic ignores floppy media unless explicitly intended.
Success: floppy media is visible through normal shell and VFS tooling without a custom code path.

## Step 8 - Shell and Diagnostics
Goal: make bring-up and regression diagnosis practical.
- [ ] Extend storage shell commands to identify floppy disks clearly as removable floppy media.
- [ ] Add a small floppy diagnostic command if existing storage commands are insufficient.
- [ ] Add `DEBUG()` traces for reset, seek, DMA, IRQ completion, and media detection.
- [ ] Keep `WARNING()` and `ERROR()` short and human-facing.
Success: failures can be diagnosed without editing code each time.

## Step 9 - Testing Strategy
Goal: validate the implementation in controlled stages.

### Stage 1 - Enumeration
- [ ] Boot under QEMU with the default floppy arguments.
- [ ] Confirm the controller initializes and a floppy disk object is created.
- [ ] Confirm `storage list` or equivalent shell tooling shows the floppy disk.

### Stage 2 - Raw Sector I/O
- [ ] Read sector 0 successfully.
- [ ] Verify the FAT boot sector signature and expected BPB fields.
- [ ] Read a few known sectors repeatedly to check stability.

### Stage 3 - Filesystem Mount
- [ ] Mount the FAT12 floppy volume automatically.
- [ ] List the root folder through the shell.
- [ ] Read a known test file from the floppy image.

### Stage 4 - Write Path
- [ ] Write a small file or overwrite a test sector.
- [ ] Reboot and verify persistence.
- [ ] Validate read-only mode behavior.

### Stage 5 - Error Paths
- [ ] Boot without floppy media.
- [ ] Boot with an unreadable or malformed floppy image.
- [ ] Trigger controller reset during operation and verify recovery.

## Step 10 - Documentation
Goal: keep the kernel documentation aligned with the implementation.
- [ ] Update `doc/guides/Kernel.md` when the floppy controller, disk path, superfloppy mount behavior, and FAT12 support are added.
- [ ] Document supported floppy media types and current limitations.
- [ ] Document whether write support is enabled and under which conditions.
Success: the documented storage stack matches real kernel behavior.

## Recommended Implementation Order
1. Driver split and registration.
2. CMOS detection and drive object creation.
3. FDC reset/seek/read one sector.
4. Full `DF_DISK_*` support.
5. Superfloppy probing helper.
6. FAT12 mount support.
7. Write support.
8. Diagnostics and documentation cleanup.

## Risks
- DMA programming and IRQ sequencing are easy to get subtly wrong.
- Media-change behavior can invalidate mounted state unexpectedly.
- Treating a floppy like an MBR disk will fail on raw FAT12 media.
- Reusing FAT16 code blindly for FAT12 can introduce cluster-chain bugs.

## Minimal Viable Result
- QEMU floppy is detected at boot.
- Sector reads work reliably.
- Raw FAT12 floppy mounts read-only.
- The volume is visible in SystemFS with the floppy prefix.
