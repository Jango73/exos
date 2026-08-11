# EPK Binary Format Specification

## Scope

This document freezes the stored `.epk` layout for parser and tooling work.
All numeric fields are little-endian.

Reference header: `kernel/include/package/EpkFormat.h`

## Versioning

- `Version` uses `U32` with `major` in high 16 bits and `minor` in low 16 bits.
- Initial released format: `1.0` (`EPK_VERSION_1_0`).

Compatibility policy:
- parser accepts only major `1` and minor `0` for step 1 scope.
- unknown header flags are rejected.
- unknown TOC entry flags are rejected.
- unknown compression methods in block table are rejected.
- reserved fields must be zero.

## File Layout

1. Fixed header (`EPK_HEADER`, 128 bytes)
2. TOC section (`EPK_TOC_HEADER` + `EPK_TOC_ENTRY` records + variable strings)
3. Block table section (`EPK_BLOCK_ENTRY` array)
4. Compressed data region (file chunks and optional inline data payload area)
5. Manifest blob (`manifest.toml`, UTF-8)
6. Optional detached signature blob

Section offsets and sizes are declared in header fields.

## Header (`EPK_HEADER`)

- `Magic`: `EPK1` (`EPK_MAGIC`)
- `Version`: packed major/minor
- `Flags`: bitfield
  - `EPK_HEADER_FLAG_COMPRESSED_BLOCKS`
  - `EPK_HEADER_FLAG_HAS_SIGNATURE`
  - `EPK_HEADER_FLAG_ENCRYPTED_CONTENT`
- `HeaderSize`: must be `128`
- `TocOffset`, `TocSize`
- `BlockTableOffset`, `BlockTableSize`
- `ManifestOffset`, `ManifestSize`
- `SignatureOffset`, `SignatureSize`
- `PackageHash[32]`: SHA-256 over all package bytes except signature region, with `PackageHash` bytes zeroed before hashing
- `Reserved[16]`: must be zero

Header contract:
- every section range must be inside file bounds.
- every section range must be non-overlapping.
- section order must be monotonic by offset.
- `SignatureSize` must be zero when `EPK_HEADER_FLAG_HAS_SIGNATURE` is not set.
- `SignatureSize` must be non-zero when `EPK_HEADER_FLAG_HAS_SIGNATURE` is set.

## TOC Section

Section prefix:
- `EPK_TOC_HEADER`:
  - `EntryCount`
  - `Reserved` (must be zero)

Each node uses `EPK_TOC_ENTRY` followed by variable bytes:
- `PathBytes[PathLength]` (UTF-8, relative path, no leading `/`)
- `AliasTargetBytes[AliasTargetLength]` (UTF-8, required for folder alias only)

Node model:
- `NodeType`:
  - `EPK_NODE_TYPE_FILE`
  - `EPK_NODE_TYPE_FOLDER`
  - `EPK_NODE_TYPE_FOLDER_ALIAS`
- `Permissions`: package permissions metadata value
- `ModifiedTime`: packed DATETIME-compatible 64-bit layout
- `FileSize`: uncompressed file size
- `InlineDataOffset`, `InlineDataSize`: used when `EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA`
- `BlockIndexStart`, `BlockCount`: used when `EPK_TOC_ENTRY_FLAG_HAS_BLOCKS`
- `FileHash[32]`: SHA-256 of uncompressed file payload
- `Reserved`: must be zero

Entry rules:
- `EntrySize` includes fixed entry header and variable bytes.
- folder entry:
  - `FileSize`, `InlineDataSize`, `BlockCount`, `AliasTargetLength` must be zero.
- file entry:
  - exactly one content mode: inline data or block-backed data.
- folder alias entry:
  - `EPK_TOC_ENTRY_FLAG_HAS_ALIAS_TARGET` must be set.
  - `AliasTargetLength` must be non-zero.

## Block Table

`EPK_BLOCK_ENTRY` array:
- `CompressedOffset`
- `CompressedSize`
- `UncompressedSize`
- `CompressionMethod` (`none` or `zlib`)
- reserved fields must be zero
- `ChunkHash[32]`: SHA-256 of uncompressed chunk

Block rules:
- block count = `BlockTableSize / EPK_BLOCK_ENTRY_SIZE` (`52` bytes per block entry).
- `CompressedOffset + CompressedSize` must stay inside package bounds.
- for zlib blocks, `UncompressedSize` must be in `(0, chunk_size_limit]`.
- file block ranges (`BlockIndexStart`, `BlockCount`) must be inside block table count.

## Manifest and Signature

- `ManifestOffset` and `ManifestSize` define a dedicated blob, expected UTF-8 TOML.
- `SignatureOffset` and `SignatureSize` define an optional detached signature blob.
- signature blob payload is `PackageHash`.
- detached signature blob structure is handled by `utils/Signature`.

## Parser Contract (Deterministic Reject Codes)

The parser must return stable error classes from `EpkFormat.h`:

- `EPK_VALIDATION_INVALID_MAGIC`
- `EPK_VALIDATION_UNSUPPORTED_VERSION`
- `EPK_VALIDATION_UNSUPPORTED_FLAGS`
- `EPK_VALIDATION_INVALID_HEADER_SIZE`
- `EPK_VALIDATION_INVALID_BOUNDS`
- `EPK_VALIDATION_INVALID_ALIGNMENT`
- `EPK_VALIDATION_INVALID_SECTION_ORDER`
- `EPK_VALIDATION_INVALID_TABLE_FORMAT`
- `EPK_VALIDATION_INVALID_ENTRY_FORMAT`
- `EPK_VALIDATION_HASH_MISMATCH`
- `EPK_VALIDATION_SIGNATURE_FAILED`
- `EPK_VALIDATION_OUT_OF_MEMORY`

## Determinism Constraints for Tooling

Pack tool output must be deterministic for identical inputs:
- stable TOC ordering (lexicographic normalized path order),
- stable timestamp serialization policy,
- stable chunking policy,
- stable manifest byte serialization,
- stable signature presence policy.
