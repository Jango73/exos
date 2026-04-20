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


    NTFS folder index traversal

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

#define NTFS_FILE_NAME_ATTRIBUTE_DIRECTORY_FLAG 0x10000000
#define NTFS_TRAVERSE_ERROR_NONE 0
#define NTFS_TRAVERSE_ERROR_HEADER_TOO_SMALL 0x1001
#define NTFS_TRAVERSE_ERROR_ENTRY_OFFSET 0x1002
#define NTFS_TRAVERSE_ERROR_ENTRY_SIZE 0x1003
#define NTFS_TRAVERSE_ERROR_ENTRY_SIZE_NORMALIZED 0x1004
#define NTFS_TRAVERSE_ERROR_ENTRY_LENGTH 0x1005
#define NTFS_TRAVERSE_ERROR_SUBNODE_LENGTH 0x1006
#define NTFS_TRAVERSE_ERROR_SUBNODE_VCN 0x1007
#define NTFS_TRAVERSE_ERROR_MISSING_LAST_ENTRY 0x1008
#define NTFS_ATTRIBUTE_LIST_ENTRY_MIN_SIZE 0x1A
#define NTFS_MAX_ATTRIBUTE_LIST_RECORD_REFERENCES 256

/***************************************************************************/

/**
 * @brief Check whether one UTF-16LE name matches "$I30".
 *
 * @param NameUtf16 UTF-16LE name pointer.
 * @param NameLength Name length in UTF-16 code units.
 * @return TRUE when the name is "$I30" (case-insensitive ASCII), FALSE otherwise.
 */
static BOOL NtfsIsI30Utf16Name(const U8* NameUtf16, U32 NameLength) {
    static const USTR NtfsI30Name[4] = {'$', 'I', '3', '0'};

    if (NameUtf16 == NULL) return FALSE;
    if (NameLength != ARRAY_COUNT(NtfsI30Name)) return FALSE;

    return Utf16LeCompareCaseInsensitiveAscii(
        (LPCUSTR)NameUtf16,
        NameLength,
        NtfsI30Name,
        ARRAY_COUNT(NtfsI30Name));
}

/***************************************************************************/

/**
 * @brief Check whether an NTFS attribute name matches "$I30".
 *
 * @param Attribute Attribute header pointer.
 * @param AttributeLength Total attribute length in bytes.
 * @return TRUE when unnamed or "$I30", FALSE otherwise.
 */
static BOOL NtfsIsI30AttributeName(const U8* Attribute, U32 AttributeLength) {
    U8 NameLength;
    U16 NameOffset;

    if (Attribute == NULL || AttributeLength < 16) return FALSE;

    NameLength = Attribute[9];
    if (NameLength == 0) return TRUE;

    NameOffset = NtfsLoadU16(Attribute + 10);
    if (NameOffset > AttributeLength) return FALSE;
    if (((U32)NameLength) > (AttributeLength - NameOffset) / sizeof(U16)) return FALSE;

    return NtfsIsI30Utf16Name(Attribute + NameOffset, NameLength);
}

/***************************************************************************/

/**
 * @brief Read full payload of one NTFS attribute.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Attribute Attribute header pointer.
 * @param AttributeLength Total attribute length in bytes.
 * @param ValueBufferOut Output allocated buffer with attribute payload.
 * @param ValueSizeOut Output payload size in bytes.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
static BOOL NtfsReadAttributeValue(
    LPNTFSFILESYSTEM FileSystem,
    const U8* Attribute,
    U32 AttributeLength,
    U8** ValueBufferOut,
    U32* ValueSizeOut) {
    BOOL IsNonResident;

    if (ValueBufferOut != NULL) *ValueBufferOut = NULL;
    if (ValueSizeOut != NULL) *ValueSizeOut = 0;
    if (FileSystem == NULL || Attribute == NULL || ValueBufferOut == NULL || ValueSizeOut == NULL) return FALSE;
    if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) return FALSE;

    IsNonResident = Attribute[8] != 0;
    if (!IsNonResident) {
        U32 ValueLength;
        U16 ValueOffset;
        U8* ValueBuffer;

        ValueLength = NtfsLoadU32(Attribute + 16);
        ValueOffset = NtfsLoadU16(Attribute + 20);
        if (ValueOffset > AttributeLength || ValueLength > (AttributeLength - ValueOffset)) return FALSE;

        if (ValueLength == 0) {
            *ValueBufferOut = NULL;
            *ValueSizeOut = 0;
            return TRUE;
        }

        ValueBuffer = (U8*)KernelHeapAlloc(ValueLength);
        if (ValueBuffer == NULL) {
            ERROR(TEXT("Unable to allocate %u bytes"), ValueLength);
            return FALSE;
        }

        MemoryCopy(ValueBuffer, Attribute + ValueOffset, ValueLength);
        *ValueBufferOut = ValueBuffer;
        *ValueSizeOut = ValueLength;
        return TRUE;
    }

    if (AttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) return FALSE;

    {
        U64 DataSize64 = NtfsLoadU64(Attribute + 48);
        U32 DataSize;
        U8* ValueBuffer;
        U32 BytesRead;

        if (U64_High32(DataSize64) != 0) {
            WARNING(TEXT("Attribute data size too large"));
            return FALSE;
        }

        DataSize = U64_Low32(DataSize64);
        if (DataSize > NTFS_MAX_INDEX_ALLOCATION_BYTES) {
            WARNING(TEXT("Attribute data size unsupported=%u"), DataSize);
            return FALSE;
        }

        if (DataSize == 0) {
            *ValueBufferOut = NULL;
            *ValueSizeOut = 0;
            return TRUE;
        }

        ValueBuffer = (U8*)KernelHeapAlloc(DataSize);
        if (ValueBuffer == NULL) {
            ERROR(TEXT("Unable to allocate %u bytes"), DataSize);
            return FALSE;
        }

        if (!NtfsReadNonResidentDataAttribute(
                FileSystem,
                Attribute,
                AttributeLength,
                ValueBuffer,
                DataSize,
                DataSize64,
                &BytesRead)) {
            KernelHeapFree(ValueBuffer);
            return FALSE;
        }

        if (BytesRead < DataSize) {
            MemorySet(ValueBuffer + BytesRead, 0, DataSize - BytesRead);
        }

        *ValueBufferOut = ValueBuffer;
        *ValueSizeOut = DataSize;
        return TRUE;
    }
}

/***************************************************************************/

/**
 * @brief Parse folder index-related attributes from a folder file record.
 *
 * @param RecordBuffer File record buffer.
 * @param RecordInfo Parsed file record info.
 * @param IndexRootOut Output pointer to INDEX_ROOT attribute.
 * @param IndexRootLengthOut Output length of INDEX_ROOT attribute.
 * @param IndexAllocationOut Output pointer to INDEX_ALLOCATION attribute.
 * @param IndexAllocationLengthOut Output length of INDEX_ALLOCATION attribute.
 * @param BitmapOut Output pointer to BITMAP attribute.
 * @param BitmapLengthOut Output length of BITMAP attribute.
 * @return TRUE on success, FALSE on malformed attributes.
 */
static BOOL NtfsParseFolderIndexAttributes(
    const U8* RecordBuffer,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    const U8** IndexRootOut,
    U32* IndexRootLengthOut,
    const U8** IndexAllocationOut,
    U32* IndexAllocationLengthOut,
    const U8** BitmapOut,
    U32* BitmapLengthOut) {
    U32 AttributeOffset;

    if (IndexRootOut != NULL) *IndexRootOut = NULL;
    if (IndexRootLengthOut != NULL) *IndexRootLengthOut = 0;
    if (IndexAllocationOut != NULL) *IndexAllocationOut = NULL;
    if (IndexAllocationLengthOut != NULL) *IndexAllocationLengthOut = 0;
    if (BitmapOut != NULL) *BitmapOut = NULL;
    if (BitmapLengthOut != NULL) *BitmapLengthOut = 0;

    if (RecordBuffer == NULL || RecordInfo == NULL) return FALSE;

    AttributeOffset = RecordInfo->SequenceOfAttributesOffset;
    while (AttributeOffset + 8 <= RecordInfo->UsedSize) {
        U32 AttributeType = NtfsLoadU32(RecordBuffer + AttributeOffset);
        U32 AttributeLength;
        const U8* Attribute;

        if (AttributeType == NTFS_ATTRIBUTE_END_MARKER) return TRUE;

        AttributeLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 4);
        if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) return FALSE;
        if (AttributeOffset > RecordInfo->UsedSize - AttributeLength) return FALSE;

        Attribute = RecordBuffer + AttributeOffset;
        if (AttributeType == NTFS_ATTRIBUTE_INDEX_ROOT) {
            if (Attribute[8] != 0 || !NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (IndexRootOut != NULL) *IndexRootOut = Attribute;
            if (IndexRootLengthOut != NULL) *IndexRootLengthOut = AttributeLength;
        } else if (AttributeType == NTFS_ATTRIBUTE_INDEX_ALLOCATION) {
            if (Attribute[8] == 0 || !NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (IndexAllocationOut != NULL) *IndexAllocationOut = Attribute;
            if (IndexAllocationLengthOut != NULL) *IndexAllocationLengthOut = AttributeLength;
        } else if (AttributeType == NTFS_ATTRIBUTE_BITMAP) {
            if (!NtfsIsI30AttributeName(Attribute, AttributeLength)) {
                AttributeOffset += AttributeLength;
                continue;
            }

            if (BitmapOut != NULL) *BitmapOut = Attribute;
            if (BitmapLengthOut != NULL) *BitmapLengthOut = AttributeLength;
        }

        AttributeOffset += AttributeLength;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode one NTFS file reference (record number + sequence number).
 *
 * @param FileReference Pointer to 8-byte file reference field.
 * @param RecordIndexOut Output decoded 32-bit record index.
 * @param SequenceNumberOut Output sequence number from the file reference.
 * @return TRUE on success, FALSE when record number exceeds 32-bit range.
 */
static BOOL NtfsDecodeFileReference(
    const U8* FileReference, U32* RecordIndexOut, U16* SequenceNumberOut) {
    U32 RecordIndex;

    if (RecordIndexOut != NULL) *RecordIndexOut = 0;
    if (SequenceNumberOut != NULL) *SequenceNumberOut = 0;
    if (FileReference == NULL || RecordIndexOut == NULL || SequenceNumberOut == NULL) return FALSE;

    if (FileReference[4] != 0 || FileReference[5] != 0) {
        return FALSE;
    }

    RecordIndex = NtfsLoadU32(FileReference);
    *RecordIndexOut = RecordIndex;
    *SequenceNumberOut = NtfsLoadU16(FileReference + 6);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize parsed file-record metadata from one on-disk header.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param RecordIndex MFT record index.
 * @param Header File-record header.
 * @param RecordInfoOut Output metadata structure.
 */
static void NtfsInitFileRecordInfoFromHeader(
    LPNTFSFILESYSTEM FileSystem,
    U32 RecordIndex,
    const NTFS_FILE_RECORD_HEADER* Header,
    LPNTFS_FILE_RECORD_INFO RecordInfoOut) {
    if (RecordInfoOut == NULL) return;

    MemorySet(RecordInfoOut, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (FileSystem == NULL || Header == NULL) return;

    RecordInfoOut->Index = RecordIndex;
    RecordInfoOut->RecordSize = FileSystem->FileRecordSize;
    RecordInfoOut->UsedSize = Header->RealSize;
    RecordInfoOut->Flags = Header->Flags;
    RecordInfoOut->SequenceNumber = Header->SequenceNumber;
    RecordInfoOut->ReferenceCount = Header->ReferenceCount;
    RecordInfoOut->SequenceOfAttributesOffset = Header->SequenceOfAttributesOffset;
    RecordInfoOut->UpdateSequenceOffset = Header->UpdateSequenceOffset;
    RecordInfoOut->UpdateSequenceSize = Header->UpdateSequenceSize;
}

/***************************************************************************/

/**
 * @brief Find the first attribute of one specific type inside one file record.
 *
 * @param RecordBuffer File record bytes.
 * @param RecordInfo Parsed file-record metadata.
 * @param AttributeType Target attribute type.
 * @param AttributeOut Output pointer to found attribute.
 * @param AttributeLengthOut Output attribute total length.
 * @return TRUE on success, FALSE when malformed record.
 */
static BOOL NtfsFindFirstAttributeByType(
    const U8* RecordBuffer,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    U32 AttributeType,
    const U8** AttributeOut,
    U32* AttributeLengthOut) {
    U32 AttributeOffset;

    if (AttributeOut != NULL) *AttributeOut = NULL;
    if (AttributeLengthOut != NULL) *AttributeLengthOut = 0;
    if (RecordBuffer == NULL || RecordInfo == NULL) return FALSE;

    AttributeOffset = RecordInfo->SequenceOfAttributesOffset;
    while (AttributeOffset + 8 <= RecordInfo->UsedSize) {
        U32 CurrentAttributeType = NtfsLoadU32(RecordBuffer + AttributeOffset);
        U32 CurrentAttributeLength;

        if (CurrentAttributeType == NTFS_ATTRIBUTE_END_MARKER) return TRUE;

        CurrentAttributeLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 4);
        if (CurrentAttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) return FALSE;
        if (AttributeOffset > RecordInfo->UsedSize - CurrentAttributeLength) return FALSE;

        if (CurrentAttributeType == AttributeType) {
            if (AttributeOut != NULL) *AttributeOut = RecordBuffer + AttributeOffset;
            if (AttributeLengthOut != NULL) *AttributeLengthOut = CurrentAttributeLength;
            return TRUE;
        }

        AttributeOffset += CurrentAttributeLength;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether one ATTRIBUTE_LIST entry targets an "$I30" index stream.
 *
 * @param Entry Attribute-list entry pointer.
 * @param EntryLength Entry length in bytes.
 * @return TRUE when unnamed or "$I30", FALSE otherwise.
 */
static BOOL NtfsIsI30AttributeListEntry(const U8* Entry, U32 EntryLength) {
    U8 NameLength;
    U8 NameOffset;

    if (Entry == NULL || EntryLength < NTFS_ATTRIBUTE_LIST_ENTRY_MIN_SIZE) return FALSE;

    NameLength = Entry[6];
    if (NameLength == 0) return TRUE;

    NameOffset = Entry[7];
    if (NameOffset > EntryLength) return FALSE;
    if (((U32)NameLength) > (EntryLength - NameOffset) / sizeof(U16)) return FALSE;

    return NtfsIsI30Utf16Name(Entry + NameOffset, NameLength);
}

/***************************************************************************/

/**
 * @brief Load folder index streams from one file record when present.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param RecordBuffer File record bytes.
 * @param RecordInfo Parsed file-record metadata.
 * @param IndexRootValueOut In/out INDEX_ROOT payload.
 * @param IndexRootValueSizeOut In/out INDEX_ROOT payload size.
 * @param IndexAllocationDataOut In/out INDEX_ALLOCATION payload.
 * @param IndexAllocationDataSizeOut In/out INDEX_ALLOCATION payload size.
 * @param BitmapDataOut In/out BITMAP payload.
 * @param BitmapDataSizeOut In/out BITMAP payload size.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
static BOOL NtfsLoadFolderIndexStreamsFromRecord(
    LPNTFSFILESYSTEM FileSystem,
    const U8* RecordBuffer,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    U8** IndexRootValueOut,
    U32* IndexRootValueSizeOut,
    U8** IndexAllocationDataOut,
    U32* IndexAllocationDataSizeOut,
    U8** BitmapDataOut,
    U32* BitmapDataSizeOut) {
    const U8* IndexRootAttribute;
    U32 IndexRootAttributeLength;
    const U8* IndexAllocationAttribute;
    U32 IndexAllocationAttributeLength;
    const U8* BitmapAttribute;
    U32 BitmapAttributeLength;

    if (FileSystem == NULL || RecordBuffer == NULL || RecordInfo == NULL) return FALSE;
    if (IndexRootValueOut == NULL || IndexRootValueSizeOut == NULL) return FALSE;
    if (IndexAllocationDataOut == NULL || IndexAllocationDataSizeOut == NULL) return FALSE;
    if (BitmapDataOut == NULL || BitmapDataSizeOut == NULL) return FALSE;

    if (!NtfsParseFolderIndexAttributes(
            RecordBuffer,
            RecordInfo,
            &IndexRootAttribute,
            &IndexRootAttributeLength,
            &IndexAllocationAttribute,
            &IndexAllocationAttributeLength,
            &BitmapAttribute,
            &BitmapAttributeLength)) {
        return FALSE;
    }

    if (*IndexRootValueOut == NULL && IndexRootAttribute != NULL) {
        if (!NtfsReadAttributeValue(
                FileSystem,
                IndexRootAttribute,
                IndexRootAttributeLength,
                IndexRootValueOut,
                IndexRootValueSizeOut)) {
            return FALSE;
        }
    }

    if (*IndexAllocationDataOut == NULL && IndexAllocationAttribute != NULL) {
        if (!NtfsReadAttributeValue(
                FileSystem,
                IndexAllocationAttribute,
                IndexAllocationAttributeLength,
                IndexAllocationDataOut,
                IndexAllocationDataSizeOut)) {
            return FALSE;
        }
    }

    if (*BitmapDataOut == NULL && BitmapAttribute != NULL) {
        if (!NtfsReadAttributeValue(
                FileSystem,
                BitmapAttribute,
                BitmapAttributeLength,
                BitmapDataOut,
                BitmapDataSizeOut)) {
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Load complete folder index streams, including ATTRIBUTE_LIST extents.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param FolderIndex Folder record index.
 * @param IndexRootValueOut Output INDEX_ROOT payload.
 * @param IndexRootValueSizeOut Output INDEX_ROOT payload size.
 * @param IndexAllocationDataOut Output INDEX_ALLOCATION payload.
 * @param IndexAllocationDataSizeOut Output INDEX_ALLOCATION payload size.
 * @param BitmapDataOut Output BITMAP payload.
 * @param BitmapDataSizeOut Output BITMAP payload size.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
static BOOL NtfsLoadFolderIndexStreams(
    LPNTFSFILESYSTEM FileSystem,
    U32 FolderIndex,
    U8** IndexRootValueOut,
    U32* IndexRootValueSizeOut,
    U8** IndexAllocationDataOut,
    U32* IndexAllocationDataSizeOut,
    U8** BitmapDataOut,
    U32* BitmapDataSizeOut) {
    U8* BaseRecordBuffer;
    NTFS_FILE_RECORD_HEADER BaseRecordHeader;
    NTFS_FILE_RECORD_INFO BaseRecordInfo;
    const U8* AttributeListAttribute;
    U32 AttributeListAttributeLength;
    U8* AttributeListValue;
    U32 AttributeListValueSize;
    U32 ReferencedRecordIndices[NTFS_MAX_ATTRIBUTE_LIST_RECORD_REFERENCES];
    U32 ReferencedRecordCount;
    U32 EntryOffset;
    U32 Index;
    BOOL HasAttributeListParseFailure;

    if (IndexRootValueOut != NULL) *IndexRootValueOut = NULL;
    if (IndexRootValueSizeOut != NULL) *IndexRootValueSizeOut = 0;
    if (IndexAllocationDataOut != NULL) *IndexAllocationDataOut = NULL;
    if (IndexAllocationDataSizeOut != NULL) *IndexAllocationDataSizeOut = 0;
    if (BitmapDataOut != NULL) *BitmapDataOut = NULL;
    if (BitmapDataSizeOut != NULL) *BitmapDataSizeOut = 0;
    if (FileSystem == NULL) return FALSE;
    if (IndexRootValueOut == NULL || IndexRootValueSizeOut == NULL) return FALSE;
    if (IndexAllocationDataOut == NULL || IndexAllocationDataSizeOut == NULL) return FALSE;
    if (BitmapDataOut == NULL || BitmapDataSizeOut == NULL) return FALSE;

    if (!NtfsLoadFileRecordBuffer(FileSystem, FolderIndex, &BaseRecordBuffer, &BaseRecordHeader)) {
        WARNING(TEXT("Unable to load folder record index=%u"), FolderIndex);
        return FALSE;
    }

    NtfsInitFileRecordInfoFromHeader(FileSystem, FolderIndex, &BaseRecordHeader, &BaseRecordInfo);
    if ((BaseRecordInfo.Flags & NTFS_FR_FLAG_FOLDER) == 0) {
        WARNING(TEXT("Record is not a folder index=%u flags=%x"), FolderIndex, BaseRecordInfo.Flags);
        KernelHeapFree(BaseRecordBuffer);
        return FALSE;
    }

    if (!NtfsLoadFolderIndexStreamsFromRecord(
            FileSystem,
            BaseRecordBuffer,
            &BaseRecordInfo,
            IndexRootValueOut,
            IndexRootValueSizeOut,
            IndexAllocationDataOut,
            IndexAllocationDataSizeOut,
            BitmapDataOut,
            BitmapDataSizeOut)) {
        WARNING(TEXT("Unable to parse folder index attributes index=%u"), FolderIndex);
        KernelHeapFree(BaseRecordBuffer);
        return FALSE;
    }

    if (*IndexRootValueOut != NULL && *IndexAllocationDataOut != NULL && *BitmapDataOut != NULL) {
        KernelHeapFree(BaseRecordBuffer);
        return TRUE;
    }

    AttributeListAttribute = NULL;
    AttributeListAttributeLength = 0;
    if (!NtfsFindFirstAttributeByType(
            BaseRecordBuffer,
            &BaseRecordInfo,
            NTFS_ATTRIBUTE_ATTRIBUTE_LIST,
            &AttributeListAttribute,
            &AttributeListAttributeLength)) {
        WARNING(TEXT("Unable to parse ATTRIBUTE_LIST index=%u"), FolderIndex);
        KernelHeapFree(BaseRecordBuffer);
        return FALSE;
    }

    if (AttributeListAttribute == NULL) {
        KernelHeapFree(BaseRecordBuffer);
        return TRUE;
    }

    AttributeListValue = NULL;
    AttributeListValueSize = 0;
    if (!NtfsReadAttributeValue(
            FileSystem,
            AttributeListAttribute,
            AttributeListAttributeLength,
            &AttributeListValue,
            &AttributeListValueSize)) {
        WARNING(TEXT("Unable to read ATTRIBUTE_LIST index=%u"), FolderIndex);
        KernelHeapFree(BaseRecordBuffer);
        return TRUE;
    }

    ReferencedRecordCount = 0;
    HasAttributeListParseFailure = FALSE;
    EntryOffset = 0;
    while (EntryOffset + NTFS_ATTRIBUTE_LIST_ENTRY_MIN_SIZE <= AttributeListValueSize) {
        const U8* Entry = AttributeListValue + EntryOffset;
        U32 EntryType = NtfsLoadU32(Entry);
        U16 EntryLength = NtfsLoadU16(Entry + 4);

        if (EntryLength < NTFS_ATTRIBUTE_LIST_ENTRY_MIN_SIZE) {
            WARNING(TEXT("Invalid ATTRIBUTE_LIST entry length=%u index=%u"),
                EntryLength,
                FolderIndex);
            HasAttributeListParseFailure = TRUE;
            break;
        }

        if (EntryOffset > AttributeListValueSize - EntryLength) {
            WARNING(TEXT("ATTRIBUTE_LIST entry out of bounds offset=%u length=%u index=%u"),
                EntryOffset,
                EntryLength,
                FolderIndex);
            HasAttributeListParseFailure = TRUE;
            break;
        }

        if (EntryType == NTFS_ATTRIBUTE_INDEX_ROOT ||
            EntryType == NTFS_ATTRIBUTE_INDEX_ALLOCATION ||
            EntryType == NTFS_ATTRIBUTE_BITMAP) {
            U32 ReferencedRecordIndex;
            U16 ReferencedSequence;
            BOOL AlreadyReferenced;

            if (NtfsIsI30AttributeListEntry(Entry, EntryLength) &&
                NtfsDecodeFileReference(Entry + 16, &ReferencedRecordIndex, &ReferencedSequence) &&
                NtfsIsValidFileRecordIndex(FileSystem, ReferencedRecordIndex)) {
                UNUSED(ReferencedSequence);
                AlreadyReferenced = FALSE;
                for (Index = 0; Index < ReferencedRecordCount; Index++) {
                    if (ReferencedRecordIndices[Index] == ReferencedRecordIndex) {
                        AlreadyReferenced = TRUE;
                        break;
                    }
                }

                if (!AlreadyReferenced &&
                    ReferencedRecordCount < NTFS_MAX_ATTRIBUTE_LIST_RECORD_REFERENCES) {
                    ReferencedRecordIndices[ReferencedRecordCount] = ReferencedRecordIndex;
                    ReferencedRecordCount++;
                }
            }
        }

        EntryOffset += EntryLength;

        if (*IndexRootValueOut != NULL && *IndexAllocationDataOut != NULL && *BitmapDataOut != NULL) {
            break;
        }
    }

    for (Index = 0; Index < ReferencedRecordCount; Index++) {
        U32 RecordIndex = ReferencedRecordIndices[Index];
        U8* RecordBuffer;
        NTFS_FILE_RECORD_HEADER RecordHeader;
        NTFS_FILE_RECORD_INFO RecordInfo;

        if (RecordIndex == FolderIndex) continue;
        if (!NtfsLoadFileRecordBuffer(FileSystem, RecordIndex, &RecordBuffer, &RecordHeader)) {
            WARNING(TEXT("Unable to load extension record index=%u base=%u"),
                RecordIndex,
                FolderIndex);
            continue;
        }

        if (U64_Cmp(RecordHeader.BaseRecord, U64_FromU32(0)) != 0) {
            U32 BaseRecordIndex;
            U16 BaseRecordSequence;

            if (!NtfsDecodeFileReference(
                    (const U8*)&RecordHeader.BaseRecord,
                    &BaseRecordIndex,
                    &BaseRecordSequence) ||
                BaseRecordIndex != FolderIndex) {
                WARNING(TEXT("Ignoring foreign extension record index=%u base=%u expected=%u"),
                    RecordIndex,
                    BaseRecordIndex,
                    FolderIndex);
                UNUSED(BaseRecordSequence);
                KernelHeapFree(RecordBuffer);
                continue;
            }
            UNUSED(BaseRecordSequence);
        }

        NtfsInitFileRecordInfoFromHeader(FileSystem, RecordIndex, &RecordHeader, &RecordInfo);
        if (!NtfsLoadFolderIndexStreamsFromRecord(
                FileSystem,
                RecordBuffer,
                &RecordInfo,
                IndexRootValueOut,
                IndexRootValueSizeOut,
                IndexAllocationDataOut,
                IndexAllocationDataSizeOut,
                BitmapDataOut,
                BitmapDataSizeOut)) {
            WARNING(TEXT("Unable to parse extension index attributes index=%u base=%u"),
                RecordIndex,
                FolderIndex);
            KernelHeapFree(RecordBuffer);
            continue;
        }

        KernelHeapFree(RecordBuffer);

        if (*IndexRootValueOut != NULL && *IndexAllocationDataOut != NULL && *BitmapDataOut != NULL) {
            break;
        }
    }

    if (HasAttributeListParseFailure) {
        WARNING(TEXT("ATTRIBUTE_LIST parsing stopped early index=%u"), FolderIndex);
    }

    if (AttributeListValue != NULL) KernelHeapFree(AttributeListValue);
    KernelHeapFree(BaseRecordBuffer);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode FILE_NAME payload into folder entry information.
 *
 * @param FileNameValue FILE_NAME payload pointer.
 * @param FileNameLength FILE_NAME payload length in bytes.
 * @param EntryInfo Output folder entry information.
 * @return TRUE on success, FALSE on malformed value.
 */
static BOOL NtfsDecodeFolderEntryFileName(
    const U8* FileNameValue, U32 FileNameLength, LPNTFS_FOLDER_ENTRY_INFO EntryInfo) {
    U8 NameLength;
    U32 Utf16Bytes;
    U32 FileAttributes;
    UINT Utf8Length;

    if (EntryInfo != NULL) MemorySet(EntryInfo, 0, sizeof(NTFS_FOLDER_ENTRY_INFO));
    if (FileNameValue == NULL || EntryInfo == NULL) return FALSE;
    if (FileNameLength < NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE) return FALSE;

    NameLength = FileNameValue[64];
    EntryInfo->NameSpace = FileNameValue[65];
    Utf16Bytes = ((U32)NameLength) * sizeof(U16);
    if (Utf16Bytes > (FileNameLength - NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE)) return FALSE;

    if (!Utf16LeToUtf8(
            (LPCUSTR)(FileNameValue + NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE),
            NameLength,
            EntryInfo->Name,
            sizeof(EntryInfo->Name),
            &Utf8Length)) {
        return FALSE;
    }

    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 8), &(EntryInfo->CreationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 16), &(EntryInfo->LastModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 24), &(EntryInfo->FileRecordModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 32), &(EntryInfo->LastAccessTime));
    FileAttributes = NtfsLoadU32(FileNameValue + 56);
    EntryInfo->IsFolder = (FileAttributes & NTFS_FILE_NAME_ATTRIBUTE_DIRECTORY_FLAG) != 0;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return TRUE when one folder entry is already present in output list.
 *
 * @param Context Enumeration context.
 * @param Entry Candidate entry.
 * @return TRUE if duplicate entry exists, FALSE otherwise.
 */
static BOOL NtfsFolderEntryAlreadyPresent(
    LPNTFS_FOLDER_ENUM_CONTEXT Context, LPNTFS_FOLDER_ENTRY_INFO Entry) {
    U32 Index;

    if (Context == NULL || Entry == NULL) return FALSE;
    if (Context->Entries == NULL) return FALSE;

    for (Index = 0; Index < Context->EntryCount; Index++) {
        LPNTFS_FOLDER_ENTRY_INFO Current = Context->Entries + Index;
        if (Current->FileRecordIndex == Entry->FileRecordIndex &&
            StringCompare(Current->Name, Entry->Name) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Add one folder entry decoded from index key data.
 *
 * @param Context Enumeration context.
 * @param EntryBuffer Pointer to index entry buffer.
 * @param EntryLength Index entry length.
 * @param KeyLength Index key length.
 * @return TRUE on success, FALSE on malformed key data.
 */
static BOOL NtfsAddFolderEntryFromIndexKey(
    LPNTFS_FOLDER_ENUM_CONTEXT Context, const U8* EntryBuffer, U32 EntryLength, U32 KeyLength) {
    NTFS_FOLDER_ENTRY_INFO EntryInfo;
    NTFS_FILE_RECORD_INFO RecordInfo;
    U32 FileRecordIndex;
    U16 FileReferenceSequence;

    if (Context == NULL || EntryBuffer == NULL) return FALSE;
    if (EntryLength < 16 || KeyLength > (EntryLength - 16)) return FALSE;
    if (KeyLength < NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE) return TRUE;

    if (!NtfsDecodeFileReference(EntryBuffer, &FileRecordIndex, &FileReferenceSequence)) {
        Context->DiagInvalidFileReferenceCount++;
        return TRUE;
    }
    if (!NtfsIsValidFileRecordIndex(Context->FileSystem, FileRecordIndex)) {
        Context->DiagInvalidRecordIndexCount++;
        return TRUE;
    }
    MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (!NtfsReadFileRecord((LPFILESYSTEM)Context->FileSystem, FileRecordIndex, &RecordInfo)) {
        Context->DiagReadRecordFailureCount++;
        return TRUE;
    }
    if (FileReferenceSequence != 0 && RecordInfo.SequenceNumber != FileReferenceSequence) {
        Context->DiagSequenceMismatchCount++;
        return TRUE;
    }
    if ((RecordInfo.Flags & NTFS_FR_FLAG_IN_USE) == 0) {
        Context->DiagSequenceMismatchCount++;
        return TRUE;
    }
    if (!NtfsDecodeFolderEntryFileName(EntryBuffer + 16, KeyLength, &EntryInfo)) return TRUE;

    if (StringCompare(EntryInfo.Name, TEXT(".")) == 0 || StringCompare(EntryInfo.Name, TEXT("..")) == 0) {
        return TRUE;
    }

    EntryInfo.FileRecordIndex = FileRecordIndex;

    if (NtfsFolderEntryAlreadyPresent(Context, &EntryInfo)) return TRUE;

    U32 EntryOrdinal = Context->TotalEntries;
    Context->TotalEntries++;

    if (EntryOrdinal < Context->StartEntryIndex) {
        return TRUE;
    }

    if (Context->Entries != NULL && Context->EntryCount < Context->MaxEntries) {
        MemoryCopy(Context->Entries + Context->EntryCount, &EntryInfo, sizeof(NTFS_FOLDER_ENTRY_INFO));
        Context->EntryCount++;

        if (Context->StopWhenWindowFilled && Context->EntryCount >= Context->MaxEntries) {
            Context->StopRequested = TRUE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether one index-allocation VCN is marked used in bitmap.
 *
 * @param Context Enumeration context.
 * @param Vcn Index-allocation VCN.
 * @return TRUE when the VCN is used or no bitmap is available.
 */
static BOOL NtfsIsIndexAllocationVcnUsed(LPNTFS_FOLDER_ENUM_CONTEXT Context, U32 Vcn) {
    U32 ByteIndex;
    U8 BitMask;

    if (Context == NULL) return FALSE;
    if (Context->Bitmap == NULL || Context->BitmapSize == 0) return TRUE;

    ByteIndex = Vcn / 8;
    if (ByteIndex >= Context->BitmapSize) return FALSE;

    BitMask = (U8)(1 << (Vcn % 8));
    return (Context->Bitmap[ByteIndex] & BitMask) != 0;
}

/***************************************************************************/

/**
 * @brief Mark one index-allocation VCN as visited.
 *
 * @param Context Enumeration context.
 * @param Vcn VCN to mark.
 * @return TRUE when VCN was not visited before and is now marked.
 */
static BOOL NtfsMarkIndexAllocationVcnVisited(LPNTFS_FOLDER_ENUM_CONTEXT Context, U32 Vcn) {
    U32 ByteIndex;
    U8 BitMask;

    if (Context == NULL || Context->VisitedVcnMap == NULL) return FALSE;

    ByteIndex = Vcn / 8;
    if (ByteIndex >= Context->VisitedVcnMapSize) return FALSE;

    BitMask = (U8)(1 << (Vcn % 8));
    if ((Context->VisitedVcnMap[ByteIndex] & BitMask) != 0) return FALSE;

    Context->VisitedVcnMap[ByteIndex] |= BitMask;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Store compact diagnostics for one index-traversal failure.
 *
 * @param Context Enumeration context.
 * @param ErrorCode Failure code.
 * @param Stage Traversal stage (1=root, 2=index-allocation node).
 * @param Vcn VCN for stage 2, 0 for stage 1.
 * @param HeaderRegionSize Traversed region size.
 * @param EntryOffset Normalized entry offset.
 * @param EntrySize Normalized entry span.
 * @param Cursor Cursor inside entry span.
 * @param EntryLength Current entry length.
 * @param EntryFlags Current entry flags.
 * @return Always FALSE.
 */
static BOOL NtfsSetTraverseError(
    LPNTFS_FOLDER_ENUM_CONTEXT Context,
    U32 ErrorCode,
    U32 Stage,
    U32 Vcn,
    U32 HeaderRegionSize,
    U32 EntryOffset,
    U32 EntrySize,
    U32 Cursor,
    U32 EntryLength,
    U32 EntryFlags) {
    if (Context != NULL && Context->DiagTraverseErrorCode == NTFS_TRAVERSE_ERROR_NONE) {
        Context->DiagTraverseErrorCode = ErrorCode;
        Context->DiagTraverseStage = Stage;
        Context->DiagTraverseVcn = Vcn;
        Context->DiagHeaderRegionSize = HeaderRegionSize;
        Context->DiagEntryOffset = EntryOffset;
        Context->DiagEntrySize = EntrySize;
        Context->DiagCursor = Cursor;
        Context->DiagEntryLength = EntryLength;
        Context->DiagEntryFlags = EntryFlags;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Traverse one NTFS index-header entry array.
 *
 * @param Context Enumeration context.
 * @param Header Pointer to index header.
 * @param HeaderRegionSize Available bytes from Header to end of region.
 * @param PendingVcns VCN stack storage.
 * @param PendingCountInOut In/out number of pending VCN entries.
 * @param PendingCapacity Capacity of PendingVcns array.
 * @return TRUE on success, FALSE on malformed entry stream.
 */
static BOOL NtfsTraverseIndexHeader(
    LPNTFS_FOLDER_ENUM_CONTEXT Context,
    const NTFS_INDEX_HEADER* Header,
    U32 HeaderRegionSize,
    U32 Stage,
    U32 Vcn,
    U32* PendingVcns,
    U32* PendingCountInOut,
    U32 PendingCapacity) {
    U32 RawEntryOffset;
    U32 RawEntrySize;
    U32 CandidateOffsets[2];
    U32 CandidateCount;
    U32 CandidateIndex;
    U32 FirstErrorCode;
    U32 FirstErrorStage;
    U32 FirstErrorVcn;
    U32 FirstHeaderRegionSize;
    U32 FirstEntryOffset;
    U32 FirstEntrySize;
    U32 FirstCursor;
    U32 FirstEntryLength;
    U32 FirstEntryFlags;
    BOOL FirstErrorCaptured;

    if (Context == NULL || Header == NULL || PendingCountInOut == NULL) return FALSE;
    if (HeaderRegionSize < sizeof(NTFS_INDEX_HEADER)) {
        return NtfsSetTraverseError(
            Context, NTFS_TRAVERSE_ERROR_HEADER_TOO_SMALL, Stage, Vcn, HeaderRegionSize, 0, 0, 0, 0, 0);
    }

    RawEntryOffset = NtfsLoadU32((const U8*)Header);
    RawEntrySize = NtfsLoadU32(((const U8*)Header) + 4);

    CandidateOffsets[0] = RawEntryOffset;
    CandidateCount = 1;
    if (Stage == 2 && RawEntryOffset >= 24) {
        U32 AlternateOffset = RawEntryOffset - 24;
        if (AlternateOffset != RawEntryOffset) {
            CandidateOffsets[CandidateCount] = AlternateOffset;
            CandidateCount++;
        }
    }

    FirstErrorCaptured = FALSE;
    FirstErrorCode = NTFS_TRAVERSE_ERROR_NONE;
    FirstErrorStage = 0;
    FirstErrorVcn = 0;
    FirstHeaderRegionSize = 0;
    FirstEntryOffset = 0;
    FirstEntrySize = 0;
    FirstCursor = 0;
    FirstEntryLength = 0;
    FirstEntryFlags = 0;

    for (CandidateIndex = 0; CandidateIndex < CandidateCount; CandidateIndex++) {
        U32 EntryOffset = CandidateOffsets[CandidateIndex];
        U32 EntrySize = RawEntrySize;
        U32 Cursor = 0;
        BOOL LastEntryFound = FALSE;
        U32 SavedEntryCount = Context->EntryCount;
        U32 SavedTotalEntries = Context->TotalEntries;
        U32 SavedPendingCount = *PendingCountInOut;
        U32 SavedRefInvalid = Context->DiagInvalidFileReferenceCount;
        U32 SavedIdxInvalid = Context->DiagInvalidRecordIndexCount;
        U32 SavedReadFail = Context->DiagReadRecordFailureCount;
        U32 SavedSeqMismatch = Context->DiagSequenceMismatchCount;

        Context->DiagTraverseErrorCode = NTFS_TRAVERSE_ERROR_NONE;
        Context->DiagTraverseStage = 0;
        Context->DiagTraverseVcn = 0;
        Context->DiagHeaderRegionSize = 0;
        Context->DiagEntryOffset = 0;
        Context->DiagEntrySize = 0;
        Context->DiagCursor = 0;
        Context->DiagEntryLength = 0;
        Context->DiagEntryFlags = 0;

        if (EntryOffset > HeaderRegionSize) {
            NtfsSetTraverseError(
                Context,
                NTFS_TRAVERSE_ERROR_ENTRY_OFFSET,
                Stage,
                Vcn,
                HeaderRegionSize,
                EntryOffset,
                EntrySize,
                0,
                0,
                0);
        } else if (EntrySize > (HeaderRegionSize - EntryOffset)) {
            if (EntrySize >= EntryOffset && EntrySize <= HeaderRegionSize) {
                EntrySize -= EntryOffset;
            } else {
                NtfsSetTraverseError(
                    Context,
                    NTFS_TRAVERSE_ERROR_ENTRY_SIZE,
                    Stage,
                    Vcn,
                    HeaderRegionSize,
                    EntryOffset,
                    EntrySize,
                    0,
                    0,
                    0);
            }
        }

        if (Context->DiagTraverseErrorCode == NTFS_TRAVERSE_ERROR_NONE && EntrySize < 16) {
            NtfsSetTraverseError(
                Context,
                NTFS_TRAVERSE_ERROR_ENTRY_SIZE_NORMALIZED,
                Stage,
                Vcn,
                HeaderRegionSize,
                EntryOffset,
                EntrySize,
                0,
                0,
                0);
        }

        while (!Context->StopRequested &&
               Context->DiagTraverseErrorCode == NTFS_TRAVERSE_ERROR_NONE &&
               Cursor + 16 <= EntrySize) {
            const U8* Entry = ((const U8*)Header) + EntryOffset + Cursor;
            U16 Length = NtfsLoadU16(Entry + 8);
            U16 KeyLength = NtfsLoadU16(Entry + 10);
            U16 Flags = NtfsLoadU16(Entry + 12);

            if (Length < 16 || Length > (EntrySize - Cursor)) {
                NtfsSetTraverseError(
                    Context,
                    NTFS_TRAVERSE_ERROR_ENTRY_LENGTH,
                    Stage,
                    Vcn,
                    HeaderRegionSize,
                    EntryOffset,
                    EntrySize,
                    Cursor,
                    Length,
                    Flags);
                break;
            }

            if ((Flags & NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY) == 0) {
                if (!NtfsAddFolderEntryFromIndexKey(Context, Entry, Length, KeyLength)) {
                    Context->DiagTraverseErrorCode = NTFS_TRAVERSE_ERROR_ENTRY_LENGTH;
                    break;
                }
            }

            if ((Flags & NTFS_INDEX_ENTRY_FLAG_HAS_SUBNODE) != 0) {
                U64 Vcn64;

                if (Length < 24) {
                    NtfsSetTraverseError(
                        Context,
                        NTFS_TRAVERSE_ERROR_SUBNODE_LENGTH,
                        Stage,
                        Vcn,
                        HeaderRegionSize,
                        EntryOffset,
                        EntrySize,
                        Cursor,
                        Length,
                        Flags);
                    break;
                }
                Vcn64 = NtfsLoadU64(Entry + Length - sizeof(U64));
                if (U64_High32(Vcn64) != 0) {
                    NtfsSetTraverseError(
                        Context,
                        NTFS_TRAVERSE_ERROR_SUBNODE_VCN,
                        Stage,
                        Vcn,
                        HeaderRegionSize,
                        EntryOffset,
                        EntrySize,
                        Cursor,
                        Length,
                        Flags);
                    break;
                }

                if (!Context->StopRequested &&
                    PendingVcns != NULL &&
                    *PendingCountInOut < PendingCapacity) {
                    PendingVcns[*PendingCountInOut] = U64_Low32(Vcn64);
                    (*PendingCountInOut)++;
                }
            }

            Cursor += Length;
            if ((Flags & NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY) != 0) {
                LastEntryFound = TRUE;
                break;
            }
        }

        if (Context->DiagTraverseErrorCode == NTFS_TRAVERSE_ERROR_NONE && !LastEntryFound) {
            NtfsSetTraverseError(
                Context,
                NTFS_TRAVERSE_ERROR_MISSING_LAST_ENTRY,
                Stage,
                Vcn,
                HeaderRegionSize,
                EntryOffset,
                EntrySize,
                Cursor,
                0,
                0);
        }

        if (Context->DiagTraverseErrorCode == NTFS_TRAVERSE_ERROR_NONE) {
            return TRUE;
        }

        if (!FirstErrorCaptured) {
            FirstErrorCaptured = TRUE;
            FirstErrorCode = Context->DiagTraverseErrorCode;
            FirstErrorStage = Context->DiagTraverseStage;
            FirstErrorVcn = Context->DiagTraverseVcn;
            FirstHeaderRegionSize = Context->DiagHeaderRegionSize;
            FirstEntryOffset = Context->DiagEntryOffset;
            FirstEntrySize = Context->DiagEntrySize;
            FirstCursor = Context->DiagCursor;
            FirstEntryLength = Context->DiagEntryLength;
            FirstEntryFlags = Context->DiagEntryFlags;
        }

        Context->EntryCount = SavedEntryCount;
        Context->TotalEntries = SavedTotalEntries;
        *PendingCountInOut = SavedPendingCount;
        Context->DiagInvalidFileReferenceCount = SavedRefInvalid;
        Context->DiagInvalidRecordIndexCount = SavedIdxInvalid;
        Context->DiagReadRecordFailureCount = SavedReadFail;
        Context->DiagSequenceMismatchCount = SavedSeqMismatch;
    }

    if (FirstErrorCaptured) {
        Context->DiagTraverseErrorCode = FirstErrorCode;
        Context->DiagTraverseStage = FirstErrorStage;
        Context->DiagTraverseVcn = FirstErrorVcn;
        Context->DiagHeaderRegionSize = FirstHeaderRegionSize;
        Context->DiagEntryOffset = FirstEntryOffset;
        Context->DiagEntrySize = FirstEntrySize;
        Context->DiagCursor = FirstCursor;
        Context->DiagEntryLength = FirstEntryLength;
        Context->DiagEntryFlags = FirstEntryFlags;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Apply update-sequence fixup on all index-allocation records.
 *
 * @param Context Enumeration context.
 * @return TRUE on success, FALSE on malformed index record.
 */
static BOOL NtfsPrepareIndexAllocationRecords(LPNTFS_FOLDER_ENUM_CONTEXT Context) {
    U32 RecordCount;
    U32 Index;

    if (Context == NULL || Context->IndexAllocation == NULL || Context->IndexBlockSize == 0) return TRUE;
    if (Context->IndexAllocationSize == 0) return TRUE;
    if ((Context->IndexAllocationSize % Context->IndexBlockSize) != 0) return FALSE;

    RecordCount = Context->IndexAllocationSize / Context->IndexBlockSize;
    for (Index = 0; Index < RecordCount; Index++) {
        U8* Record;
        NTFS_INDEX_RECORD_HEADER Header;

        if (!NtfsIsIndexAllocationVcnUsed(Context, Index)) continue;

        Record = (U8*)Context->IndexAllocation + (Index * Context->IndexBlockSize);
        MemoryCopy(&Header, Record, sizeof(NTFS_INDEX_RECORD_HEADER));
        if (Header.Magic != 0x58444E49) continue;

        if (!NtfsApplyFileRecordFixup(
                Record,
                Context->IndexBlockSize,
                Context->FileSystem->BytesPerSector,
                Header.UpdateSequenceOffset,
                Header.UpdateSequenceSize)) {
            WARNING(TEXT("Fixup failed vcn=%u"), Index);
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Enumerate one NTFS folder by file-record index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param FolderIndex Folder file-record index.
 * @param Entries Optional output array for folder entries.
 * @param MaxEntries Capacity of Entries.
 * @param EntryCountOut Optional output number of stored entries.
 * @param TotalEntriesOut Optional output total number of enumerated entries.
 * @return TRUE on success, FALSE on malformed metadata or read failure.
 */
static BOOL NtfsEnumerateFolderByIndexInternal(
    LPFILESYSTEM FileSystem,
    U32 FolderIndex,
    U32 StartEntryIndex,
    LPNTFS_FOLDER_ENTRY_INFO Entries,
    U32 MaxEntries,
    U32* EntryCountOut,
    U32* TotalEntriesOut,
    BOOL StopWhenWindowFilled) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* IndexRootValue;
    U32 IndexRootValueSize;
    U8* IndexAllocationData;
    U32 IndexAllocationDataSize;
    U8* BitmapData;
    U32 BitmapDataSize;
    NTFS_INDEX_ROOT_HEADER RootHeader;
    NTFS_FOLDER_ENUM_CONTEXT Context;
    U32 MaxVcnRecords;
    U32* PendingVcns;
    U32 PendingCount;
    BOOL HadNodeTraversalFailure;
    BOOL Result;

    if (EntryCountOut != NULL) *EntryCountOut = 0;
    if (TotalEntriesOut != NULL) *TotalEntriesOut = 0;
    if (FileSystem == NULL) return FALSE;
    if (Entries == NULL && MaxEntries != 0) return FALSE;
    if (StopWhenWindowFilled && (Entries == NULL || EntryCountOut == NULL)) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;
        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;

        IndexRootValue = NULL;
        IndexRootValueSize = 0;
        IndexAllocationData = NULL;
        IndexAllocationDataSize = 0;
        BitmapData = NULL;
        BitmapDataSize = 0;
        if (!NtfsLoadFolderIndexStreams(
                NtfsFileSystem,
                FolderIndex,
                &IndexRootValue,
                &IndexRootValueSize,
                &IndexAllocationData,
                &IndexAllocationDataSize,
                &BitmapData,
                &BitmapDataSize)) {
            if (IndexRootValue != NULL) KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        if (IndexRootValue == NULL || IndexRootValueSize < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
            WARNING(TEXT("Missing INDEX_ROOT index=%u"), FolderIndex);
            if (IndexRootValue != NULL) KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        if (IndexRootValue == NULL || IndexRootValueSize < sizeof(NTFS_INDEX_ROOT_HEADER) + sizeof(NTFS_INDEX_HEADER)) {
            WARNING(TEXT("INDEX_ROOT payload invalid size=%u index=%u"),
                IndexRootValueSize,
                FolderIndex);
            if (IndexRootValue != NULL) KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        MemoryCopy(&RootHeader, IndexRootValue, sizeof(NTFS_INDEX_ROOT_HEADER));
        if (RootHeader.IndexBlockSize == 0 || !NtfsIsPowerOfTwo(RootHeader.IndexBlockSize)) {
            WARNING(TEXT("Invalid index block size=%u index=%u"),
                RootHeader.IndexBlockSize,
                FolderIndex);
            KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        MemorySet(&Context, 0, sizeof(NTFS_FOLDER_ENUM_CONTEXT));
        Context.FileSystem = NtfsFileSystem;
        Context.Entries = Entries;
        Context.MaxEntries = MaxEntries;
        Context.StartEntryIndex = StartEntryIndex;
        Context.StopWhenWindowFilled = StopWhenWindowFilled;
        Context.StopRequested = FALSE;
        Context.EntryCount = 0;
        Context.TotalEntries = 0;
        Context.IndexAllocation = IndexAllocationData;
        Context.IndexAllocationSize = IndexAllocationDataSize;
        Context.IndexBlockSize = RootHeader.IndexBlockSize;
        Context.Bitmap = BitmapData;
        Context.BitmapSize = BitmapDataSize;

        MaxVcnRecords = 0;
        if (Context.IndexAllocation != NULL && Context.IndexBlockSize != 0) {
            if ((Context.IndexAllocationSize % Context.IndexBlockSize) != 0) {
                WARNING(TEXT("INDEX_ALLOCATION size misaligned size=%u block=%u index=%u"),
                    Context.IndexAllocationSize,
                    Context.IndexBlockSize,
                    FolderIndex);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
            MaxVcnRecords = Context.IndexAllocationSize / Context.IndexBlockSize;
        }

        Context.VisitedVcnMap = NULL;
        Context.VisitedVcnMapSize = 0;
        if (MaxVcnRecords > 0) {
            Context.VisitedVcnMapSize = (MaxVcnRecords + 7) / 8;
            Context.VisitedVcnMap = (U8*)KernelHeapAlloc(Context.VisitedVcnMapSize);
            if (Context.VisitedVcnMap == NULL) {
                WARNING(TEXT("Unable to allocate visited map size=%u index=%u"),
                    Context.VisitedVcnMapSize,
                    FolderIndex);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
            MemorySet(Context.VisitedVcnMap, 0, Context.VisitedVcnMapSize);
        }

        if (!NtfsPrepareIndexAllocationRecords(&Context)) {
            WARNING(TEXT("Unable to prepare index allocation records index=%u"), FolderIndex);
            if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
            KernelHeapFree(IndexRootValue);
            if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
            if (BitmapData != NULL) KernelHeapFree(BitmapData);
            return FALSE;
        }

        PendingVcns = NULL;
        PendingCount = 0;
        if (MaxVcnRecords > 0) {
            PendingVcns = (U32*)KernelHeapAlloc(MaxVcnRecords * sizeof(U32));
            if (PendingVcns == NULL) {
                WARNING(TEXT("Unable to allocate pending VCN list count=%u index=%u"),
                    MaxVcnRecords,
                    FolderIndex);
                if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
                KernelHeapFree(IndexRootValue);
                KernelHeapFree(IndexAllocationData);
                if (BitmapData != NULL) KernelHeapFree(BitmapData);
                return FALSE;
            }
        }

        Result = NtfsTraverseIndexHeader(
            &Context,
            (const NTFS_INDEX_HEADER*)(IndexRootValue + sizeof(NTFS_INDEX_ROOT_HEADER)),
            IndexRootValueSize - sizeof(NTFS_INDEX_ROOT_HEADER),
            1,
            0,
            PendingVcns,
            &PendingCount,
            MaxVcnRecords);
        HadNodeTraversalFailure = FALSE;

        while (Result && PendingCount > 0 && !Context.StopRequested) {
            U32 Vcn;
            U32 RecordOffset;
            U8* RecordBufferNode;
            NTFS_INDEX_RECORD_HEADER NodeHeader;
            BOOL NodeResult;

            PendingCount--;
            Vcn = PendingVcns[PendingCount];

            if (!NtfsIsIndexAllocationVcnUsed(&Context, Vcn)) continue;
            if (!NtfsMarkIndexAllocationVcnVisited(&Context, Vcn)) continue;
            if (Vcn >= MaxVcnRecords) continue;

            RecordOffset = Vcn * Context.IndexBlockSize;
            RecordBufferNode = (U8*)Context.IndexAllocation + RecordOffset;

            MemoryCopy(&NodeHeader, RecordBufferNode, sizeof(NTFS_INDEX_RECORD_HEADER));
            if (NodeHeader.Magic != 0x58444E49) continue;

            NodeResult = NtfsTraverseIndexHeader(
                &Context,
                (const NTFS_INDEX_HEADER*)(RecordBufferNode + 24),
                Context.IndexBlockSize - 24,
                2,
                Vcn,
                PendingVcns,
                &PendingCount,
                MaxVcnRecords);
            if (!NodeResult) {
                HadNodeTraversalFailure = TRUE;
                Context.DiagTraverseErrorCode = NTFS_TRAVERSE_ERROR_NONE;
                continue;
            }
        }
        if (HadNodeTraversalFailure) {
            WARNING(TEXT("Ignored one or more invalid index-allocation nodes index=%u"), FolderIndex);
        }
        if (!Result) {
            WARNING(TEXT("Index traversal failed index=%u error=%x stage=%u vcn=%u region=%u offset=%u size=%u cursor=%u len=%u flags=%x ref_invalid=%u idx_invalid=%u record_read_fail=%u seq_mismatch=%u"),
                FolderIndex,
                Context.DiagTraverseErrorCode,
                Context.DiagTraverseStage,
                Context.DiagTraverseVcn,
                Context.DiagHeaderRegionSize,
                Context.DiagEntryOffset,
                Context.DiagEntrySize,
                Context.DiagCursor,
                Context.DiagEntryLength,
                Context.DiagEntryFlags,
                Context.DiagInvalidFileReferenceCount,
                Context.DiagInvalidRecordIndexCount,
                Context.DiagReadRecordFailureCount,
                Context.DiagSequenceMismatchCount);
        }

        if (EntryCountOut != NULL) *EntryCountOut = Context.EntryCount;
        if (TotalEntriesOut != NULL) *TotalEntriesOut = Context.TotalEntries;

        if (PendingVcns != NULL) KernelHeapFree(PendingVcns);
        if (Context.VisitedVcnMap != NULL) KernelHeapFree(Context.VisitedVcnMap);
        KernelHeapFree(IndexRootValue);
        if (IndexAllocationData != NULL) KernelHeapFree(IndexAllocationData);
        if (BitmapData != NULL) KernelHeapFree(BitmapData);

        return Result;
    }

    return FALSE;
}

/***************************************************************************/

BOOL NtfsEnumerateFolderByIndex(
    LPFILESYSTEM FileSystem,
    U32 FolderIndex,
    LPNTFS_FOLDER_ENTRY_INFO Entries,
    U32 MaxEntries,
    U32* EntryCountOut,
    U32* TotalEntriesOut) {
    return NtfsEnumerateFolderByIndexInternal(
        FileSystem,
        FolderIndex,
        0,
        Entries,
        MaxEntries,
        EntryCountOut,
        TotalEntriesOut,
        FALSE);
}

/***************************************************************************/

BOOL NtfsEnumerateFolderByIndexWindow(
    LPFILESYSTEM FileSystem,
    U32 FolderIndex,
    U32 StartEntryIndex,
    LPNTFS_FOLDER_ENTRY_INFO Entries,
    U32 MaxEntries,
    U32* EntryCountOut) {
    return NtfsEnumerateFolderByIndexInternal(
        FileSystem,
        FolderIndex,
        StartEntryIndex,
        Entries,
        MaxEntries,
        EntryCountOut,
        NULL,
        TRUE);
}
