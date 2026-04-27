/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    EPK package parser and validator

\************************************************************************/

#include "package/Epk-Parser.h"

#include "log/Log.h"
#include "memory/Heap.h"
#include "text/CoreString.h"
#include "utils/Crypt.h"
#include "utils/Signature.h"

/************************************************************************/

typedef struct tag_EPK_RUNTIME_SECTIONS {
    U32 TocOffset;
    U32 TocSize;
    U32 BlockTableOffset;
    U32 BlockTableSize;
    U32 ManifestOffset;
    U32 ManifestSize;
    U32 SignatureOffset;
    U32 SignatureSize;
} EPK_RUNTIME_SECTIONS;

/************************************************************************/

/**
 * @brief Convert a U64 package field to U32 when representable.
 * @param Value Source value.
 * @param Out Receives converted value.
 * @return TRUE when conversion is exact.
 */
static BOOL EpkU64ToU32(U64 Value, U32* Out) {
    if (Out == NULL) {
        return FALSE;
    }

    if (U64_High32(Value) != 0) {
        return FALSE;
    }

    *Out = U64_Low32(Value);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate an offset/size range inside the package blob.
 * @param Offset64 Range offset (U64 on-disk field).
 * @param Size64 Range size (U64 on-disk field).
 * @param BlobSize Total package size.
 * @param OffsetOut Receives converted offset.
 * @param SizeOut Receives converted size.
 * @return TRUE when range is valid.
 */
static BOOL EpkExtractRange(U64 Offset64, U64 Size64, U32 BlobSize, U32* OffsetOut, U32* SizeOut) {
    U32 Offset;
    U32 Size;
    U32 End;

    if (!EpkU64ToU32(Offset64, &Offset)) {
        return FALSE;
    }

    if (!EpkU64ToU32(Size64, &Size)) {
        return FALSE;
    }

    if (Offset > BlobSize) {
        return FALSE;
    }

    End = Offset + Size;
    if (End < Offset) {
        return FALSE;
    }

    if (End > BlobSize) {
        return FALSE;
    }

    if (OffsetOut != NULL) {
        *OffsetOut = Offset;
    }

    if (SizeOut != NULL) {
        *SizeOut = Size;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate section ordering and non-overlap constraints.
 * @param Header Parsed header.
 * @param Sections Receives validated section offsets/sizes.
 * @param PackageSize Total package size.
 * @return Validation status.
 */
static U32 EpkValidateSections(const EPK_HEADER* Header, EPK_RUNTIME_SECTIONS* Sections, U32 PackageSize) {
    U32 TocEnd;
    U32 BlockEnd;
    U32 ManifestEnd;
    U32 SignatureEnd;

    if (Header == NULL || Sections == NULL) {
        return EPK_VALIDATION_INVALID_ARGUMENT;
    }

    if (!EpkExtractRange(Header->TocOffset, Header->TocSize, PackageSize, &Sections->TocOffset, &Sections->TocSize)) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    if (!EpkExtractRange(
            Header->BlockTableOffset, Header->BlockTableSize, PackageSize, &Sections->BlockTableOffset,
            &Sections->BlockTableSize)) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    if (!EpkExtractRange(
            Header->ManifestOffset, Header->ManifestSize, PackageSize, &Sections->ManifestOffset,
            &Sections->ManifestSize)) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    if (!EpkExtractRange(
            Header->SignatureOffset, Header->SignatureSize, PackageSize, &Sections->SignatureOffset,
            &Sections->SignatureSize)) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    TocEnd = Sections->TocOffset + Sections->TocSize;
    BlockEnd = Sections->BlockTableOffset + Sections->BlockTableSize;
    ManifestEnd = Sections->ManifestOffset + Sections->ManifestSize;
    SignatureEnd = Sections->SignatureOffset + Sections->SignatureSize;

    if (Sections->TocOffset < EPK_HEADER_SIZE) {
        return EPK_VALIDATION_INVALID_SECTION_ORDER;
    }

    if (Sections->BlockTableOffset < TocEnd) {
        return EPK_VALIDATION_INVALID_SECTION_ORDER;
    }

    if (Sections->ManifestOffset < BlockEnd) {
        return EPK_VALIDATION_INVALID_SECTION_ORDER;
    }

    if (Sections->SignatureSize != 0 && Sections->SignatureOffset < ManifestEnd) {
        return EPK_VALIDATION_INVALID_SECTION_ORDER;
    }

    if ((Header->Flags & EPK_HEADER_FLAG_HAS_SIGNATURE) == 0 && Sections->SignatureSize != 0) {
        return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
    }

    if ((Header->Flags & EPK_HEADER_FLAG_HAS_SIGNATURE) != 0 && Sections->SignatureSize == 0) {
        return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
    }

    if (SignatureEnd > PackageSize) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Parse and validate TOC entries.
 * @param PackageBytes Package byte buffer.
 * @param Sections Validated section information.
 * @param OutPackage Target validated package descriptor.
 * @return Validation status.
 */
static U32 EpkParseToc(
    const U8* PackageBytes, const EPK_RUNTIME_SECTIONS* Sections, LPEPK_VALIDATED_PACKAGE OutPackage) {
    EPK_TOC_HEADER TocHeader;
    U32 Cursor;
    U32 TocEnd;
    U32 EntryIndex;
    LPEPK_PARSED_TOC_ENTRY Parsed;

    if (Sections->TocSize < sizeof(EPK_TOC_HEADER)) {
        return EPK_VALIDATION_INVALID_TABLE_FORMAT;
    }

    MemoryCopy(&TocHeader, PackageBytes + Sections->TocOffset, sizeof(EPK_TOC_HEADER));
    if (TocHeader.Reserved != 0) {
        return EPK_VALIDATION_INVALID_TABLE_FORMAT;
    }

    if (TocHeader.EntryCount != 0) {
        U32 BytesNeeded = TocHeader.EntryCount * (U32)sizeof(EPK_PARSED_TOC_ENTRY);
        if (BytesNeeded / (U32)sizeof(EPK_PARSED_TOC_ENTRY) != TocHeader.EntryCount) {
            return EPK_VALIDATION_INVALID_TABLE_FORMAT;
        }

        Parsed = (LPEPK_PARSED_TOC_ENTRY)KernelHeapAlloc(BytesNeeded);
        if (Parsed == NULL) {
            return EPK_VALIDATION_OUT_OF_MEMORY;
        }

        MemorySet(Parsed, 0, BytesNeeded);
        OutPackage->TocEntries = Parsed;
    } else {
        OutPackage->TocEntries = NULL;
    }

    OutPackage->TocEntryCount = TocHeader.EntryCount;

    Cursor = Sections->TocOffset + (U32)sizeof(EPK_TOC_HEADER);
    TocEnd = Sections->TocOffset + Sections->TocSize;

    for (EntryIndex = 0; EntryIndex < TocHeader.EntryCount; EntryIndex++) {
        EPK_TOC_ENTRY Entry;
        U32 PathStart;
        U32 AliasStart;
        U32 EntryEnd;
        BOOL HasInlineData;
        BOOL HasBlocks;
        BOOL HasAlias;

        if (Cursor > TocEnd || TocEnd - Cursor < EPK_TOC_ENTRY_SIZE) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        MemoryCopy(&Entry, PackageBytes + Cursor, sizeof(EPK_TOC_ENTRY));

        if (Entry.EntrySize < EPK_TOC_ENTRY_SIZE) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        EntryEnd = Cursor + Entry.EntrySize;
        if (EntryEnd < Cursor || EntryEnd > TocEnd) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        PathStart = Cursor + EPK_TOC_ENTRY_SIZE;
        if (PathStart < Cursor || PathStart > EntryEnd) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        AliasStart = PathStart + Entry.PathLength;
        if (AliasStart < PathStart || AliasStart > EntryEnd) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (AliasStart + Entry.AliasTargetLength != EntryEnd) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (Entry.PathLength == 0) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if ((Entry.EntryFlags & ~EPK_TOC_ENTRY_FLAG_MASK_KNOWN) != 0) {
            return EPK_VALIDATION_UNSUPPORTED_FLAGS;
        }

        if (Entry.Reserved != 0) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        HasInlineData = (Entry.EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA) != 0;
        HasBlocks = (Entry.EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_BLOCKS) != 0;
        HasAlias = (Entry.EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_ALIAS_TARGET) != 0;

        if (Entry.NodeType == EPK_NODE_TYPE_FOLDER) {
            if (HasInlineData || HasBlocks || HasAlias) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (U64_Cmp(Entry.FileSize, U64_FromU32(0)) != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (Entry.InlineDataSize != 0 || Entry.BlockCount != 0 || Entry.AliasTargetLength != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
        } else if (Entry.NodeType == EPK_NODE_TYPE_FILE) {
            if (HasAlias || Entry.AliasTargetLength != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (HasInlineData == HasBlocks) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (HasInlineData && Entry.BlockCount != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (HasBlocks && Entry.InlineDataSize != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
        } else if (Entry.NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
            if (!HasAlias || Entry.AliasTargetLength == 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (HasInlineData || HasBlocks) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
            if (U64_Cmp(Entry.FileSize, U64_FromU32(0)) != 0 || Entry.BlockCount != 0 || Entry.InlineDataSize != 0) {
                return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
            }
        } else {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (OutPackage->TocEntries != NULL) {
            LPEPK_PARSED_TOC_ENTRY ParsedEntry = &OutPackage->TocEntries[EntryIndex];
            ParsedEntry->NodeType = Entry.NodeType;
            ParsedEntry->EntryFlags = Entry.EntryFlags;
            ParsedEntry->Permissions = Entry.Permissions;
            ParsedEntry->ModifiedTime = Entry.ModifiedTime;
            ParsedEntry->FileSize = Entry.FileSize;
            ParsedEntry->InlineDataOffset = Entry.InlineDataOffset;
            ParsedEntry->InlineDataSize = Entry.InlineDataSize;
            ParsedEntry->BlockIndexStart = Entry.BlockIndexStart;
            ParsedEntry->BlockCount = Entry.BlockCount;
            ParsedEntry->PathOffset = PathStart;
            ParsedEntry->PathLength = Entry.PathLength;
            ParsedEntry->AliasTargetOffset = AliasStart;
            ParsedEntry->AliasTargetLength = Entry.AliasTargetLength;
            MemoryCopy(ParsedEntry->FileHash, Entry.FileHash, EPK_HASH_SIZE);
        }

        Cursor = EntryEnd;
    }

    if (Cursor != TocEnd) {
        return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Parse and validate block table entries.
 * @param PackageBytes Package byte buffer.
 * @param PackageSize Total package size.
 * @param Sections Validated section information.
 * @param OutPackage Target validated package descriptor.
 * @return Validation status.
 */
static U32 EpkParseBlockTable(
    const U8* PackageBytes, U32 PackageSize, const EPK_RUNTIME_SECTIONS* Sections, LPEPK_VALIDATED_PACKAGE OutPackage) {
    U32 BlockCount;
    U32 BlockIndex;

    if (Sections->BlockTableSize % EPK_BLOCK_ENTRY_SIZE != 0) {
        return EPK_VALIDATION_INVALID_TABLE_FORMAT;
    }

    BlockCount = Sections->BlockTableSize / EPK_BLOCK_ENTRY_SIZE;
    OutPackage->BlockCount = BlockCount;

    if (BlockCount != 0) {
        U32 BytesNeeded = BlockCount * (U32)sizeof(EPK_PARSED_BLOCK_ENTRY);
        if (BytesNeeded / (U32)sizeof(EPK_PARSED_BLOCK_ENTRY) != BlockCount) {
            return EPK_VALIDATION_INVALID_TABLE_FORMAT;
        }

        OutPackage->BlockEntries = (LPEPK_PARSED_BLOCK_ENTRY)KernelHeapAlloc(BytesNeeded);
        if (OutPackage->BlockEntries == NULL) {
            return EPK_VALIDATION_OUT_OF_MEMORY;
        }

        MemorySet(OutPackage->BlockEntries, 0, BytesNeeded);
    } else {
        OutPackage->BlockEntries = NULL;
    }

    for (BlockIndex = 0; BlockIndex < BlockCount; BlockIndex++) {
        EPK_BLOCK_ENTRY Entry;
        U32 EntryOffset = Sections->BlockTableOffset + BlockIndex * EPK_BLOCK_ENTRY_SIZE;
        U32 CompressedOffset;
        U32 BlockEnd;
        LPEPK_PARSED_BLOCK_ENTRY ParsedEntry = &OutPackage->BlockEntries[BlockIndex];

        MemoryCopy(&Entry, PackageBytes + EntryOffset, sizeof(EPK_BLOCK_ENTRY));

        if (Entry.Reserved0 != 0 || Entry.Reserved1 != 0) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (Entry.CompressionMethod != EPK_COMPRESSION_METHOD_NONE &&
            Entry.CompressionMethod != EPK_COMPRESSION_METHOD_ZLIB) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (Entry.UncompressedSize == 0 || Entry.CompressedSize == 0) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        if (!EpkU64ToU32(Entry.CompressedOffset, &CompressedOffset)) {
            return EPK_VALIDATION_INVALID_BOUNDS;
        }

        BlockEnd = CompressedOffset + Entry.CompressedSize;
        if (BlockEnd < CompressedOffset || BlockEnd > PackageSize) {
            return EPK_VALIDATION_INVALID_BOUNDS;
        }

        ParsedEntry->CompressedOffset = Entry.CompressedOffset;
        ParsedEntry->CompressedSize = Entry.CompressedSize;
        ParsedEntry->UncompressedSize = Entry.UncompressedSize;
        ParsedEntry->CompressionMethod = Entry.CompressionMethod;
        MemoryCopy(ParsedEntry->ChunkHash, Entry.ChunkHash, EPK_HASH_SIZE);
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Validate TOC entries that reference block table ranges.
 * @param Package Parsed package descriptor.
 * @return Validation status.
 */
static U32 EpkValidateTocBlockRanges(const EPK_VALIDATED_PACKAGE* Package) {
    U32 EntryIndex;

    for (EntryIndex = 0; EntryIndex < Package->TocEntryCount; EntryIndex++) {
        const EPK_PARSED_TOC_ENTRY* Entry = &Package->TocEntries[EntryIndex];
        U32 BlockEnd;

        if ((Entry->EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_BLOCKS) == 0) {
            continue;
        }

        if (Entry->BlockCount == 0) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }

        BlockEnd = Entry->BlockIndexStart + Entry->BlockCount;
        if (BlockEnd < Entry->BlockIndexStart || BlockEnd > Package->BlockCount) {
            return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
        }
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Validate inline data ranges for TOC entries.
 * @param Package Parsed package descriptor.
 * @return Validation status.
 */
static U32 EpkValidateTocInlineRanges(const EPK_VALIDATED_PACKAGE* Package) {
    U32 EntryIndex;

    for (EntryIndex = 0; EntryIndex < Package->TocEntryCount; EntryIndex++) {
        const EPK_PARSED_TOC_ENTRY* Entry = &Package->TocEntries[EntryIndex];
        U32 InlineOffset;
        U32 InlineEnd;

        if ((Entry->EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA) == 0) {
            continue;
        }

        if (Entry->InlineDataSize == 0) {
            continue;
        }

        if (!EpkU64ToU32(Entry->InlineDataOffset, &InlineOffset)) {
            return EPK_VALIDATION_INVALID_BOUNDS;
        }

        InlineEnd = InlineOffset + Entry->InlineDataSize;
        if (InlineEnd < InlineOffset || InlineEnd > Package->PackageSize) {
            return EPK_VALIDATION_INVALID_BOUNDS;
        }
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Compute package hash according to EPK rules.
 * @param Package Package descriptor.
 * @param OutHash Receives SHA-256 hash.
 * @return Validation status.
 */
static U32 EpkComputePackageHash(const EPK_VALIDATED_PACKAGE* Package, U8 OutHash[SHA256_SIZE]) {
    U32 HashInputSize;
    U8* HashInput;
    U32 SignatureStart;
    U32 SignatureEnd;

    if (Package == NULL || OutHash == NULL) {
        return EPK_VALIDATION_INVALID_ARGUMENT;
    }

    if (Package->SignatureSize > Package->PackageSize) {
        return EPK_VALIDATION_INVALID_BOUNDS;
    }

    HashInputSize = Package->PackageSize - Package->SignatureSize;
    HashInput = (U8*)KernelHeapAlloc(HashInputSize == 0 ? 1 : HashInputSize);
    if (HashInput == NULL) {
        return EPK_VALIDATION_OUT_OF_MEMORY;
    }

    SignatureStart = Package->SignatureOffset;
    SignatureEnd = SignatureStart + Package->SignatureSize;

    if (Package->SignatureSize == 0) {
        MemoryCopy(HashInput, Package->PackageBytes, HashInputSize);
    } else {
        if (SignatureEnd < SignatureStart || SignatureEnd > Package->PackageSize) {
            KernelHeapFree(HashInput);
            return EPK_VALIDATION_INVALID_BOUNDS;
        }

        if (SignatureStart > 0) {
            MemoryCopy(HashInput, Package->PackageBytes, SignatureStart);
        }

        if (SignatureEnd < Package->PackageSize) {
            MemoryCopy(
                HashInput + SignatureStart, Package->PackageBytes + SignatureEnd, Package->PackageSize - SignatureEnd);
        }
    }

    if (HashInputSize >= EPK_HEADER_PACKAGE_HASH_OFFSET + EPK_HASH_SIZE) {
        MemorySet(HashInput + EPK_HEADER_PACKAGE_HASH_OFFSET, 0, EPK_HASH_SIZE);
    }

    SHA256(HashInput, HashInputSize, OutHash);
    KernelHeapFree(HashInput);
    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Validate package hash and optional detached signature.
 * @param Package Parsed package descriptor.
 * @param Options Parser options.
 * @return Validation status.
 */
static U32 EpkValidateSecurity(const EPK_VALIDATED_PACKAGE* Package, const EPK_PARSER_OPTIONS* Options) {
    U8 ComputedHash[SHA256_SIZE];
    U32 Status;

    if (Options->VerifyPackageHash) {
        Status = EpkComputePackageHash(Package, ComputedHash);
        if (Status != EPK_VALIDATION_OK) {
            return Status;
        }

        if (MemoryCompare(ComputedHash, Package->Header.PackageHash, EPK_HASH_SIZE) != 0) {
            return EPK_VALIDATION_HASH_MISMATCH;
        }
    }

    if (Options->RequireSignature && Package->SignatureSize == 0) {
        return EPK_VALIDATION_SIGNATURE_FAILED;
    }

    if (Options->VerifySignature && Package->SignatureSize != 0) {
        U32 SignatureStatus = SignatureVerifyDetachedBlob(
            Package->PackageBytes + Package->SignatureOffset, Package->SignatureSize, Package->Header.PackageHash,
            EPK_HASH_SIZE);
        if (SignatureStatus != SIGNATURE_STATUS_OK) {
            return EPK_VALIDATION_SIGNATURE_FAILED;
        }
    }

    return EPK_VALIDATION_OK;
}

/************************************************************************/

/**
 * @brief Release parser-owned structures from a validated package descriptor.
 * @param Package Package descriptor.
 */
void EpkReleaseValidatedPackage(LPEPK_VALIDATED_PACKAGE Package) {
    if (Package == NULL) {
        return;
    }

    if (Package->TocEntries != NULL) {
        KernelHeapFree(Package->TocEntries);
    }

    if (Package->BlockEntries != NULL) {
        KernelHeapFree(Package->BlockEntries);
    }

    MemorySet(Package, 0, sizeof(EPK_VALIDATED_PACKAGE));
}

/************************************************************************/

/**
 * @brief Validate and parse an in-memory EPK package.
 * @param PackageBytes Package byte buffer.
 * @param PackageSize Package byte size.
 * @param Options Parser options.
 * @param OutPackage Receives parsed descriptor on success.
 * @return Validation status.
 */
U32 EpkValidatePackageBuffer(
    const void* PackageBytes, U32 PackageSize, const EPK_PARSER_OPTIONS* Options, LPEPK_VALIDATED_PACKAGE OutPackage) {
    EPK_PARSER_OPTIONS EffectiveOptions;
    EPK_RUNTIME_SECTIONS Sections;
    EPK_HEADER Header;
    U32 Status;
    const U8* Bytes = (const U8*)PackageBytes;

    if (PackageBytes == NULL || OutPackage == NULL) {
        ERROR(TEXT("Invalid argument"));
        return EPK_VALIDATION_INVALID_ARGUMENT;
    }

    if (PackageSize < EPK_HEADER_SIZE) {
        ERROR(TEXT("Package too small size=%u"), PackageSize);
        return EPK_VALIDATION_INVALID_HEADER_SIZE;
    }

    if (Options == NULL) {
        EffectiveOptions.VerifyPackageHash = TRUE;
        EffectiveOptions.VerifySignature = TRUE;
        EffectiveOptions.RequireSignature = FALSE;
    } else {
        EffectiveOptions = *Options;
    }

    MemorySet(OutPackage, 0, sizeof(EPK_VALIDATED_PACKAGE));
    MemoryCopy(&Header, Bytes, sizeof(EPK_HEADER));

    if (Header.Magic != EPK_MAGIC) {
        ERROR(TEXT("Invalid magic=%x"), Header.Magic);
        return EPK_VALIDATION_INVALID_MAGIC;
    }

    if (Header.Version != EPK_VERSION_1_0) {
        ERROR(TEXT("Unsupported version=%x"), Header.Version);
        return EPK_VALIDATION_UNSUPPORTED_VERSION;
    }

    if ((Header.Flags & ~EPK_HEADER_FLAG_MASK_KNOWN) != 0) {
        ERROR(TEXT("Unsupported header flags=%x"), Header.Flags);
        return EPK_VALIDATION_UNSUPPORTED_FLAGS;
    }

    if (Header.HeaderSize != EPK_HEADER_SIZE) {
        ERROR(TEXT("Invalid header size=%u"), Header.HeaderSize);
        return EPK_VALIDATION_INVALID_HEADER_SIZE;
    }

    static const U8 ZeroReserved[16] = {0};

    if (MemoryCompare(Header.Reserved, ZeroReserved, 16) != 0) {
        ERROR(TEXT("Reserved header bytes are not zero"));
        return EPK_VALIDATION_INVALID_ENTRY_FORMAT;
    }

    Status = EpkValidateSections(&Header, &Sections, PackageSize);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("Section validation failed status=%u"), Status);
        return Status;
    }

    OutPackage->PackageBytes = Bytes;
    OutPackage->PackageSize = PackageSize;
    OutPackage->Header = Header;
    OutPackage->TocOffset = Sections.TocOffset;
    OutPackage->TocSize = Sections.TocSize;
    OutPackage->BlockTableOffset = Sections.BlockTableOffset;
    OutPackage->BlockTableSize = Sections.BlockTableSize;
    OutPackage->ManifestOffset = Sections.ManifestOffset;
    OutPackage->ManifestSize = Sections.ManifestSize;
    OutPackage->SignatureOffset = Sections.SignatureOffset;
    OutPackage->SignatureSize = Sections.SignatureSize;

    Status = EpkParseToc(Bytes, &Sections, OutPackage);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("TOC parse failed status=%u"), Status);
        EpkReleaseValidatedPackage(OutPackage);
        return Status;
    }

    Status = EpkParseBlockTable(Bytes, PackageSize, &Sections, OutPackage);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("Block table parse failed status=%u"), Status);
        EpkReleaseValidatedPackage(OutPackage);
        return Status;
    }

    Status = EpkValidateTocBlockRanges(OutPackage);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("TOC block range validation failed status=%u"), Status);
        EpkReleaseValidatedPackage(OutPackage);
        return Status;
    }

    Status = EpkValidateTocInlineRanges(OutPackage);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("TOC inline range validation failed status=%u"), Status);
        EpkReleaseValidatedPackage(OutPackage);
        return Status;
    }

    Status = EpkValidateSecurity(OutPackage, &EffectiveOptions);
    if (Status != EPK_VALIDATION_OK) {
        ERROR(TEXT("Security validation failed status=%u"), Status);
        EpkReleaseValidatedPackage(OutPackage);
        return Status;
    }

    return EPK_VALIDATION_OK;
}
