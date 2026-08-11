# Generic Storage Transfer Layer (block layer)

## Goal
Add a generic, driver-independent storage I/O layer (block-layer equivalent) that owns
the sector cache, the request merging and the chunking policy for ALL storage drivers
(SATA, USB storage, NVMe, RAM storage). Drivers keep only the raw transfer plus their
device-specific limits (maximum sectors per command). This replaces the SATA-local
cache + chunking logic with a reusable, architecture-neutral mechanism placed above
the drivers, following the Linux block-layer model.

## Rationale / architecture decision (confirmed with user)
- Current stack: `File.c -> DF_FS_READ -> EXT2 -> IOCONTROL -> Storage->Driver->Command(DF_STORAGE_READ) -> driver Read()`.
- Every storage driver implements its own `Read`/`Write` today:
  - SATA: sector cache (`CACHE` + `SECTOR_BUFFER` pool) + no batching (per-sector commands).
  - USB storage: no sector cache, chunking bounded by `PAGE_SIZE / BlockSize` per command.
  - RAM storage: plain `MemoryCopy`.
  - NVMe: no cache, no batching.
- The cache + chunking belong to a GENERIC layer, not to a driver. This matches
  AGENTS.md ("any reusable behavior must be a module, not driver-local logic").
- Linux model adopted: drivers do not know about cache/merge; the block layer does
  the merging (contiguous requests coalesced) bounded by a per-driver fixed maximum
  transfer size. No dynamic throughput measurement (user dropped that idea);
  adaptation is a per-driver constant (`MaxSectorsPerTransfer`), not a load metric.

## Scope
- New generic layer `StorageTransferLayer` in `kernel/source/fs/` + `kernel/include/fs/Storage-Transfer-Layer.h`
  (next to `Storage.c`), operating per `STORAGE_UNIT`.
- Filesystems call the generic layer instead of `Storage->Driver->Command(DF_STORAGE_READ/WRITE)`
  directly. Callers to update: `File-System.c` (mount/probe paths), `EXT2-Storage.c`
  (`ReadSectors`/`WriteSectors`), `SystemFS.c` (if it touches storage), `Shell-Commands-Storage.c`,
  `Expose-Storage.c`.
- Drivers declare `MaxSectorsPerTransfer` (per driver/hardware capability) and
  implement only raw transfer. Remove SATA-local `SectorCache`/`SectorBufferPool`
  management from SATA `Read`/`Write` and move it into the generic layer.
- Sector cache reused by all drivers (today only SATA caches).

## Design sketch
```
File.c -> EXT2 -> StorageTransferLayer generic (cache + merge + chunk <= MaxSectorsPerTransfer)
                                 -> Storage->Driver->Command(DF_STORAGE_READ)   (raw, already chunked)
```

### `StorageTransferLayer` module responsibilities
- Per `STORAGE_UNIT` state: sector `CACHE` + `SECTOR_BUFFER` buffer pool (moved up
  from SATA), plus the unit's `MaxSectorsPerTransfer`.
- `StorageTransferLayerRead(IOCONTROL)` / `StorageTransferLayerWrite(IOCONTROL)`:
  1. Validate control + unit.
  2. Cache lookup per sector; copy hits to the destination.
  3. Group the contiguous uncached run into one chunk and issue a single
     `DF_STORAGE_READ` to the driver when the chunk fits `MaxSectorsPerTransfer`,
     otherwise split.
  4. Populate the cache from the data just transferred.
- Cache eviction/cleanup timing preserved (5 min TTL, `CacheCleanup` before use).

### Per-driver capabilities
- New field, e.g. `U32 MaxSectorsPerTransfer` on the storage unit or a driver
  capability query (`DF_STORAGE_GETINFO` extension or dedicated function).
- Suggested values:
  - SATA/AHCI: bounded by bounce buffer size currently `(SATA_BOUNCE_BUFFER_BYTES - N_4KB) / SECTOR_SIZE`
    (8 with the current 8 KiB bounce buffer; raise the bounce buffer to grow this).
  - USB storage: `PAGE_SIZE / BlockSize` (current USBStorageTransfer cap).
  - RAM storage: configurable limit, large default (see below), no hardware bound
    (memory copy); must be changeable through the config file.
  - NVMe: hardware/queue limit (to be defined during implementation).

### RAM storage configurable limit (confirmed with user)
- `MaxSectorsPerTransfer` for the RAM storage is NOT a fixed constant: it is a
  config key with a large default, overridable via `exos.toml`.
- Follow the existing config pattern:
  - Key define in `kernel/include/utils/Helpers.h` (e.g.
    `CONFIG_RAMSTORAGE_MAX_SECTORS_PER_TRANSFER "RAMStorage.MaxSectorsPerTransfer"`).
  - Read through `GetConfigurationUInt(TEXT(CONFIG_...), Default, Min, Max)`
    (same pattern as `Task.MinimumTaskStackSize`, Task.c:85).
  - Documented in `kernel/configuration/exos.ref.toml` under a `[RAMStorage]` section
    (currently absent; add it, commented with the default value).
  - Default large (e.g. the RAM storage capacity or a generous cap such as
    `NUM_BUFFERS`/1 MiB worth of sectors); bounded by `Max` to keep chunking
    sane (e.g. `MAX_U32` or a practical transfer cap).
- The generic layer uses the per-unit `MaxSectorsPerTransfer`; for the RAM storage
  the attach-time value is the default, and the config key is read lazily on
  first use through `GetConfigurationUIntLazy` (the config file is loaded after
  the drivers, so it cannot be read at attach time).

## Work items

### 1. Generic `StorageTransferLayer` module
- `kernel/include/fs/Storage-Transfer-Layer.h` + `kernel/source/fs/Storage-Transfer-Layer.c`.
- State struct per storage unit (cache + buffer pool + max sectors).
- Read/Write entry points with merge + chunk + cache population.
- Init/deinit called from storage attach/detach paths (SATA `AHCIAttach`, USB storage
  attach, NVMe attach, RAM storage init).

### 2. Driver simplification
- SATA: remove `SectorCache`/`SectorBufferPool` from `AHCI_PORT` and from
  `Read`/`Write`; keep `AHCICommand` (bounce buffer + DMA) and `GetInfo`.
- USB storage: keep `USBStorageTransfer` chunk loop as the raw transfer; remove any
  cache-related logic (none today beyond the device write-cache flush).
- RAM storage / NVMe: expose `MaxSectorsPerTransfer`; keep raw transfer.

### 3. Capability plumbing
- Declare `MaxSectorsPerTransfer` per driver; thread it into the generic layer
  state at attach time.
- RAM storage: attach-time default large, overridden lazily by the config key
  `RAMStorage.MaxSectorsPerTransfer` on first use (changeable via `exos.toml`).

### 4. Call-site migration
- `EXT2-Storage.c`: `ReadSectors`/`WriteSectors` -> `StorageTransferLayerRead`/`StorageTransferLayerWrite`.
- `File-System.c`: `FileSystemReadStorageSector`, `MountStoragePartitions`,
  `MountPartition_Extended` -> generic layer.
- Verify no remaining direct `DF_STORAGE_READ`/`DF_STORAGE_WRITE` calls outside drivers.

### 5. Validation
- Build both architectures, `--release` and `--debug --profiling`.
- Boot + smoke test global suite (all targets) must stay green.
- Re-measure SATA AHCICommand call count / latency with `prof` on the netget
  2 MiB download: expect the same call-count reduction the SATA-local batching
  gave (37 070 -> ~5 000) WITHOUT SATA-specific code.
- Regression: USB storage and RAM storage paths still work (smoke test includes them).

## Notes
- This supersedes the reverted commit that implemented batching inside SATA only
  (the commit was revoked on request; only the AGENTS.md smoke-test rule was kept).
- The Linux comparison (merge by contiguity + fixed per-driver max) was discussed
  with the user and validated as the model to follow.
- Cache pool sizing and `STORAGE_CACHE_TTL_MS` semantics move unchanged into the
  generic layer.
- `log/Profile` scopes (`SataRead`/`SataWrite`/`AHCICommand`/`CacheCleanupSATA`)
  added during the SATA batching experiment were reverted. The generic-layer
  equivalents are now in the tree: `StorageTransferLayerRead`/`StorageTransferLayerWrite`
  scopes plus an `AHCICommand` call counter in SATA.c, used for the item 5 measurement.

## Tracking
- [x] 1. Generic `StorageTransferLayer` module created (cache + merge + chunk).
- [x] 2. Drivers simplified (SATA, USB, RAM storage, NVMe) to raw transfer only.
- [x] 3. `MaxSectorsPerTransfer` capability wired per driver.
- [x] 4. Filesystem call sites migrated to the generic layer.
- [x] 5. Build + smoke tests green on both architectures; SATA batching benefit reproduced.
      Measured with a `--profiling` build on the netget 2 MiB download (smoke-very-large.txt):
      `AHCICommand` calls = 5033 (baseline without batching: 37 070), matching the ~5 000
      target. Instrumentation kept in tree: `ProfileCountCall("AHCICommand")` in SATA.c and
      `PROFILE_SCOPED("StorageTransferLayerRead"/"StorageTransferLayerWrite")` in the layer.
- [x] 6. `doc/guides/Kernel.md` updated for the new component.
- [x] 7. `RAMStorage.MaxSectorsPerTransfer` config key added (Helpers.h + exos.ref.toml
      + lazy read on first use) and validated.

## Constraints (AGENTS.md reminders)
- No direct C types in kernel code: only `Base.h` types.
- Log strings begin with `[FunctionName]`; `TEXT("...")` on all string literals.
- Reusable mechanics go in `kernel/include/utils` + `kernel/source/utils` (the cache
  is already a `utils/Cache` module; the generic layer belongs in `fs/` next to `Storage.c`).
- Mutex ownership: the generic layer must not lock driver/unit internals; use owner-side
  accessors. Critical sections short, no callbacks/messages under structural locks.
- Source files stay under 1000 lines; split modules by responsibility.
- Modified files formatted with clang-format; header changes validated with `--clean`
  builds on both architectures.
- No direct access to physical memory: keep using `MapLinearToPhysical`/bounce
  buffers through the drivers.
