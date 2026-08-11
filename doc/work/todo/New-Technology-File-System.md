# NTFS Implementation Roadmap

## Prerequisites (one-time)

- [X] **Block device**: read sectors with LBA, 512 bytes or 4K. (Storage info now exposes BytesPerSector and partition probing reads 512/4K sectors.)
- [X] **Partition**: identify NTFS partition in GPT or MBR, expose start LBA.
- [X] **Cache**: cache for clusters.
- [ ] **Unicode**: UTF-16LE decode for file and folder names.
  - [x] **Phase 1 (minimum viable)**: UTF-16LE -> code point, UTF-16LE -> UTF-8, ASCII case-insensitive compare (`kernel/source/utils/Unicode.c`).
  - [ ] **Phase 2 (full quality)**: full Unicode case-insensitive compare and normalization strategy for path lookup.
- [X] **DATETIME**: convert NTFS timestamps to DATETIME structure. (`NtfsTimestampToDateTime` helper added in `kernel/source/drivers/NTFS.c`.)

## Step 1 --- Volume detection + Boot Sector

**Goal**: validate NTFS volume and extract core geometry.

- [X] Read boot sector, check OEM and signature.
- [X] Parse bytes per sector, sectors per cluster, MFT start cluster.
- [X] Support only standard sector sizes (reject exotic values).

**Success**: `fs --long` prints NTFS geometry and the current volume label state.

## Step 2 --- MFT read (minimal)

**Goal**: read file records by index.

- [X] Read MFT file record 0 and parse basic header.
- [X] Support fixup array, record size, flags.
- [X] Expose a minimal `NtfsReadFileRecord(Index)`.

## Step 3 --- Attributes: FILE_NAME + DATA (read-only)

**Goal**: retrieve file metadata and file contents.

- [X] Parse resident and non-resident attributes.
- [X] Implement runlist parsing for non-resident DATA.
- [X] Read DATA stream (default stream only, no alternate streams).
- [X] Extract FILE_NAME (primary) and basic timestamps.

**Success**: a single file can be read by MFT index and its name is visible.

## Step 4 --- Folder listing (INDEX_ROOT + INDEX_ALLOCATION)

**Goal**: list folder entries.

- [X] Parse INDEX_ROOT for small folders.
- [X] Parse INDEX_ALLOCATION and BITMAP for large folders.
- [X] Implement index entry traversal (B+ tree walk).
- [X] Ignore reparse points and hard links for now.

**Success**: `ls` on a mounted NTFS volume lists folder contents.

## Step 5 --- Path lookup (\Folder\File)

**Goal**: resolve paths to file records.

- [X] Root folder lookup using index entries.
- [X] Implement case-insensitive compare (ASCII first, then Unicode).
- [X] Cache recent folder lookups to reduce MFT reads.

**Success**: `type` on a path prints file contents.

## Step 6 --- Integration with VFS (read-only)

**Goal**: mount NTFS read-only as a filesystem.

- [X] Implement NtfsMount, NtfsOpen, NtfsRead, NtfsListFolder.
- [X] Translate NTFS metadata to VFS attributes.
- [X] Enforce read-only and return proper errors on write attempts.

**Success**: NTFS volume is browseable and files are readable.

## Step 7 --- Future full implementation hooks

**Goal**: keep design ready for full NTFS later.

- [X] Separate stored parsing from VFS layer. (`kernel/source/drivers/NTFS-Record.c`, `kernel/source/drivers/NTFS-VFS.c`)
- [X] Reserve structures for security descriptors and object identifiers. (`kernel/include/drivers/NTFS.h`)
- [X] Add placeholder interfaces for write path (create, write, delete). (`kernel/source/drivers/NTFS-Write.c`)
- [X] Define attribute handlers table to extend support cleanly. (`kernel/source/drivers/NTFS-Record.c`)

**Success**: adding full NTFS features later does not require large refactors.

## Step 8 --- Full NTFS features (later)

- [ ] Alternate data streams.
- [ ] Compression.
- [ ] Encryption (EFS).
- [ ] USN journal.
- [ ] Security descriptors and ACLs.
- [ ] Hard links and reparse points.
- [ ] Sparse files.
- [ ] Object identifiers and quotas.
- [ ] Journaled write support.
