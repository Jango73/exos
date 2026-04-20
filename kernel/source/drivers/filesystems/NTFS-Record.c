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


    NTFS file-record and data stream logic

\************************************************************************/

#include "NTFS-Private.h"
#include "system/Clock.h"
#include "utils/RateLimiter.h"

/***************************************************************************/

#define NTFS_INVALID_RECORD_MAGIC_LOG_LIMIT 8
#define NTFS_INVALID_RECORD_MAGIC_LOG_COOLDOWN_MS 1000

/***************************************************************************/

static BOOL NtfsParseFileRecordAttributes(
    const U8* RecordBuffer,
    U32 RecordSize,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    U32* DataAttributeOffsetOut,
    U32* DataAttributeLengthOut);

/***************************************************************************/

/**
 * @brief Read one MFT record as a raw linear window from MFT start sector.
 *
 * This helper assumes the target record is linearly addressable from
 * FileSystem->MftStartSector and copies exactly FileRecordSize bytes.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT record index.
 * @param RecordBufferOut Output allocated record buffer.
 * @return TRUE on success, FALSE on read/allocation/geometry failure.
 */
static BOOL NtfsReadLinearFileRecordWindow(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8** RecordBufferOut) {
    U64 RecordOffset;
    U64 SectorOffset64;
    U32 SectorShift;
    U32 SectorOffset;
    U32 OffsetInSector;
    U32 TotalBytes;
    U32 NumSectors;
    U32 ReadSize;
    U32 RecordSector;
    U8* ReadBuffer;
    U8* RecordBuffer;

    if (RecordBufferOut != NULL) *RecordBufferOut = NULL;
    if (FileSystem == NULL || RecordBufferOut == NULL) return FALSE;

    if (FileSystem->FileRecordSize == 0 || FileSystem->BytesPerSector == 0 ||
        !NtfsIsPowerOfTwo(FileSystem->BytesPerSector)) {
        return FALSE;
    }

    RecordOffset = NtfsMultiplyU32ToU64(Index, FileSystem->FileRecordSize);
    SectorShift = NtfsLog2(FileSystem->BytesPerSector);
    SectorOffset64 = NtfsU64ShiftRight(RecordOffset, SectorShift);
    OffsetInSector = U64_Low32(RecordOffset) & (FileSystem->BytesPerSector - 1);

    if (U64_High32(SectorOffset64) != 0) return FALSE;
    SectorOffset = U64_Low32(SectorOffset64);

    if (FileSystem->MftStartSector > 0xFFFFFFFF - SectorOffset) return FALSE;
    RecordSector = FileSystem->MftStartSector + SectorOffset;

    if (OffsetInSector > 0xFFFFFFFF - FileSystem->FileRecordSize) return FALSE;
    TotalBytes = OffsetInSector + FileSystem->FileRecordSize;
    NumSectors = TotalBytes / FileSystem->BytesPerSector;
    if ((TotalBytes % FileSystem->BytesPerSector) != 0) NumSectors++;

    if (NumSectors == 0 || NumSectors > 0xFFFFFFFF / FileSystem->BytesPerSector) return FALSE;
    ReadSize = NumSectors * FileSystem->BytesPerSector;

    ReadBuffer = (U8*)KernelHeapAlloc(ReadSize);
    if (ReadBuffer == NULL) return FALSE;
    if (!NtfsReadSectors(FileSystem, RecordSector, NumSectors, ReadBuffer, ReadSize)) {
        KernelHeapFree(ReadBuffer);
        return FALSE;
    }

    RecordBuffer = (U8*)KernelHeapAlloc(FileSystem->FileRecordSize);
    if (RecordBuffer == NULL) {
        KernelHeapFree(ReadBuffer);
        return FALSE;
    }

    MemoryCopy(RecordBuffer, ReadBuffer + OffsetInSector, FileSystem->FileRecordSize);
    KernelHeapFree(ReadBuffer);

    *RecordBufferOut = RecordBuffer;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate one raw file record buffer and expose header.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File-record index used for diagnostics.
 * @param RecordBuffer File record buffer to validate in-place.
 * @param HeaderOut Output validated header.
 * @return TRUE on success, FALSE when header/fixup/size checks fail.
 */
static BOOL NtfsValidateFileRecordBuffer(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8* RecordBuffer, NTFS_FILE_RECORD_HEADER* HeaderOut) {
    NTFS_FILE_RECORD_HEADER Header;

    if (HeaderOut != NULL) MemorySet(HeaderOut, 0, sizeof(NTFS_FILE_RECORD_HEADER));
    if (FileSystem == NULL || RecordBuffer == NULL || HeaderOut == NULL) return FALSE;
    if (FileSystem->FileRecordSize < sizeof(NTFS_FILE_RECORD_HEADER)) return FALSE;

    MemoryCopy(&Header, RecordBuffer, sizeof(NTFS_FILE_RECORD_HEADER));
    if (Header.Magic != NTFS_FILE_RECORD_MAGIC) return FALSE;

    if (!NtfsApplyFileRecordFixup(
            RecordBuffer,
            FileSystem->FileRecordSize,
            FileSystem->BytesPerSector,
            Header.UpdateSequenceOffset,
            Header.UpdateSequenceSize)) {
        WARNING(TEXT("Fixup failed index=%u"), Index);
        return FALSE;
    }

    MemoryCopy(&Header, RecordBuffer, sizeof(NTFS_FILE_RECORD_HEADER));
    if (Header.RealSize > FileSystem->FileRecordSize) {
        WARNING(TEXT("Invalid real size=%u index=%u"), Header.RealSize, Index);
        return FALSE;
    }
    MemoryCopy(HeaderOut, &Header, sizeof(NTFS_FILE_RECORD_HEADER));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read one MFT record through the $MFT DATA runlist mapping.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT file-record index to read.
 * @param RecordBufferOut Output allocated record buffer.
 * @param HeaderOut Output validated header.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL NtfsLoadFileRecordBufferViaMftData(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8** RecordBufferOut, NTFS_FILE_RECORD_HEADER* HeaderOut) {
    U8* MftRecordBuffer;
    NTFS_FILE_RECORD_HEADER MftHeader;
    NTFS_FILE_RECORD_INFO MftRecordInfo;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;
    U8* RecordBuffer;
    const U8* DataAttribute;
    U64 RecordOffset;
    U32 BytesRead;

    if (RecordBufferOut != NULL) *RecordBufferOut = NULL;
    if (HeaderOut != NULL) MemorySet(HeaderOut, 0, sizeof(NTFS_FILE_RECORD_HEADER));
    if (FileSystem == NULL || RecordBufferOut == NULL || HeaderOut == NULL) return FALSE;

    if (!NtfsReadLinearFileRecordWindow(FileSystem, 0, &MftRecordBuffer)) return FALSE;
    if (!NtfsValidateFileRecordBuffer(FileSystem, 0, MftRecordBuffer, &MftHeader)) {
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }

    MemorySet(&MftRecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    MftRecordInfo.Index = 0;
    MftRecordInfo.RecordSize = FileSystem->FileRecordSize;
    MftRecordInfo.UsedSize = MftHeader.RealSize;
    MftRecordInfo.Flags = MftHeader.Flags;
    MftRecordInfo.SequenceNumber = MftHeader.SequenceNumber;
    MftRecordInfo.ReferenceCount = MftHeader.ReferenceCount;
    MftRecordInfo.SequenceOfAttributesOffset = MftHeader.SequenceOfAttributesOffset;
    MftRecordInfo.UpdateSequenceOffset = MftHeader.UpdateSequenceOffset;
    MftRecordInfo.UpdateSequenceSize = MftHeader.UpdateSequenceSize;

    DataAttributeOffset = 0;
    DataAttributeLength = 0;
    if (!NtfsParseFileRecordAttributes(
            MftRecordBuffer,
            FileSystem->FileRecordSize,
            &MftRecordInfo,
            &DataAttributeOffset,
            &DataAttributeLength)) {
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }

    if (!MftRecordInfo.HasDataAttribute || MftRecordInfo.DataIsResident) {
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }
    if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) {
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }

    DataAttribute = MftRecordBuffer + DataAttributeOffset;
    RecordBuffer = (U8*)KernelHeapAlloc(FileSystem->FileRecordSize);
    if (RecordBuffer == NULL) {
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }

    RecordOffset = NtfsMultiplyU32ToU64(Index, FileSystem->FileRecordSize);
    BytesRead = 0;
    if (!NtfsReadNonResidentDataAttributeRange(
            FileSystem,
            DataAttribute,
            DataAttributeLength,
            RecordOffset,
            RecordBuffer,
            FileSystem->FileRecordSize,
            MftRecordInfo.DataSize,
            &BytesRead) ||
        BytesRead < FileSystem->FileRecordSize) {
        KernelHeapFree(RecordBuffer);
        KernelHeapFree(MftRecordBuffer);
        return FALSE;
    }

    KernelHeapFree(MftRecordBuffer);
    if (!NtfsValidateFileRecordBuffer(FileSystem, Index, RecordBuffer, HeaderOut)) {
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    *RecordBufferOut = RecordBuffer;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Load one MFT file record into a dedicated contiguous buffer.
 *
 * The returned buffer has exactly FileRecordSize bytes and must be released
 * with KernelHeapFree by the caller.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index MFT file record index.
 * @param RecordBufferOut Output pointer to allocated file record buffer.
 * @param HeaderOut Parsed and validated file record header.
 * @return TRUE on success, FALSE on read or validation failure.
 */
BOOL NtfsLoadFileRecordBuffer(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8** RecordBufferOut, NTFS_FILE_RECORD_HEADER* HeaderOut) {
    static RATE_LIMITER DATA_SECTION InvalidRecordMagicWarningLimiter = {0};
    static BOOL DATA_SECTION InvalidRecordMagicWarningLimiterInitAttempted = FALSE;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;
    U32 SuppressedWarnings;

    if (RecordBufferOut != NULL) *RecordBufferOut = NULL;
    if (HeaderOut != NULL) MemorySet(HeaderOut, 0, sizeof(NTFS_FILE_RECORD_HEADER));
    if (FileSystem == NULL || RecordBufferOut == NULL || HeaderOut == NULL) return FALSE;

    if (FileSystem->FileRecordSize == 0 || FileSystem->BytesPerSector == 0 ||
        !NtfsIsPowerOfTwo(FileSystem->BytesPerSector)) {
        WARNING(TEXT("Invalid NTFS geometry"));
        return FALSE;
    }
    if (!NtfsIsValidFileRecordIndex(FileSystem, Index)) {
        return FALSE;
    }

    if (!NtfsReadLinearFileRecordWindow(FileSystem, Index, &RecordBuffer)) {
        return FALSE;
    }
    if (!NtfsValidateFileRecordBuffer(FileSystem, Index, RecordBuffer, &Header)) {
        if (Index != 0 && NtfsLoadFileRecordBufferViaMftData(FileSystem, Index, &RecordBuffer, &Header)) {
            *RecordBufferOut = RecordBuffer;
            MemoryCopy(HeaderOut, &Header, sizeof(NTFS_FILE_RECORD_HEADER));
            return TRUE;
        }

        SuppressedWarnings = 0;
        if (InvalidRecordMagicWarningLimiter.Initialized == FALSE && InvalidRecordMagicWarningLimiterInitAttempted == FALSE) {
            InvalidRecordMagicWarningLimiterInitAttempted = TRUE;
            if (RateLimiterInit(
                    &InvalidRecordMagicWarningLimiter,
                    NTFS_INVALID_RECORD_MAGIC_LOG_LIMIT,
                    NTFS_INVALID_RECORD_MAGIC_LOG_COOLDOWN_MS) == FALSE) {
                WARNING(TEXT("Unable to initialize warning limiter"));
            }
        }
        if (RateLimiterShouldTrigger(&InvalidRecordMagicWarningLimiter, GetSystemTime(), &SuppressedWarnings)) {
            WARNING(
                TEXT("Invalid file record magic=%x index=%u suppressed=%u"),
                Header.Magic,
                Index,
                SuppressedWarnings);
        }
        KernelHeapFree(RecordBuffer);
        return FALSE;
    }

    *RecordBufferOut = RecordBuffer;
    MemoryCopy(HeaderOut, &Header, sizeof(NTFS_FILE_RECORD_HEADER));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parse a FILE_NAME attribute payload and update primary name metadata.
 *
 * @param FileNameValue FILE_NAME attribute value buffer.
 * @param FileNameLength FILE_NAME attribute value length.
 * @param RecordInfo Target record information to update.
 */
static void NtfsParseFileNameValue(
    const U8* FileNameValue, U32 FileNameLength, LPNTFS_FILE_RECORD_INFO RecordInfo) {
    U8 NameLength;
    U8 NameSpace;
    U32 CandidateRank;
    U32 CurrentRank;
    U32 Utf16Bytes;
    UINT Utf8Length;

    if (FileNameValue == NULL || RecordInfo == NULL) return;
    if (FileNameLength < 66) return;

    NameLength = FileNameValue[64];
    NameSpace = FileNameValue[65];
    Utf16Bytes = ((U32)NameLength) * sizeof(U16);

    if (66 > FileNameLength || Utf16Bytes > (FileNameLength - 66)) return;

    CandidateRank = NtfsGetFileNameNamespaceRank(NameSpace);
    CurrentRank = RecordInfo->HasPrimaryFileName ? NtfsGetFileNameNamespaceRank((U8)RecordInfo->PrimaryFileNameNamespace) : 0;
    if (RecordInfo->HasPrimaryFileName && CandidateRank < CurrentRank) return;

    StringClear(RecordInfo->PrimaryFileName);
    Utf8Length = 0;
    if (!Utf16LeToUtf8(
            (LPCUSTR)(FileNameValue + 66),
            NameLength,
            RecordInfo->PrimaryFileName,
            sizeof(RecordInfo->PrimaryFileName),
            &Utf8Length)) {
        return;
    }

    RecordInfo->HasPrimaryFileName = TRUE;
    RecordInfo->PrimaryFileNameNamespace = NameSpace;
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 8), &(RecordInfo->CreationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 16), &(RecordInfo->LastModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 24), &(RecordInfo->FileRecordModificationTime));
    NtfsTimestampToDateTime(NtfsLoadU64(FileNameValue + 32), &(RecordInfo->LastAccessTime));
}

/***************************************************************************/

/**
 * @brief View describing one raw NTFS attribute in one file record.
 */
typedef struct tag_NTFS_ATTRIBUTE_VIEW {
    const U8* RecordBuffer;
    U32 RecordSize;
    LPNTFS_FILE_RECORD_INFO RecordInfo;
    U32 AttributeType;
    U32 AttributeOffset;
    U32 AttributeLength;
    BOOL IsNonResident;
    U8 NameLength;
} NTFS_ATTRIBUTE_VIEW, *LPNTFS_ATTRIBUTE_VIEW;

/***************************************************************************/

/**
 * @brief Parse state shared between NTFS attribute handlers.
 */
typedef struct tag_NTFS_ATTRIBUTE_PARSE_STATE {
    BOOL DataFound;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;
} NTFS_ATTRIBUTE_PARSE_STATE, *LPNTFS_ATTRIBUTE_PARSE_STATE;

/***************************************************************************/

typedef BOOL (*NTFS_ATTRIBUTE_HANDLER_FN)(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, LPNTFS_ATTRIBUTE_PARSE_STATE ParseState);

/***************************************************************************/

typedef struct tag_NTFS_ATTRIBUTE_HANDLER_ENTRY {
    U32 AttributeType;
    NTFS_ATTRIBUTE_HANDLER_FN Handler;
} NTFS_ATTRIBUTE_HANDLER_ENTRY, *LPNTFS_ATTRIBUTE_HANDLER_ENTRY;

/***************************************************************************/

/**
 * @brief Validate and expose resident attribute value span.
 *
 * @param AttributeView Parsed attribute view.
 * @param ValueOut Output pointer to value bytes.
 * @param ValueLengthOut Output value length in bytes.
 * @return TRUE on success, FALSE on malformed resident layout.
 */
static BOOL NtfsGetResidentValue(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, const U8** ValueOut, U32* ValueLengthOut) {
    U32 ValueLength;
    U16 ValueOffset;

    if (ValueOut != NULL) *ValueOut = NULL;
    if (ValueLengthOut != NULL) *ValueLengthOut = 0;
    if (AttributeView == NULL || AttributeView->RecordBuffer == NULL) return FALSE;
    if (AttributeView->IsNonResident) return FALSE;
    if (AttributeView->AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
        WARNING(TEXT("Invalid resident length=%u"), AttributeView->AttributeLength);
        return FALSE;
    }

    ValueLength = NtfsLoadU32(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 16);
    ValueOffset = NtfsLoadU16(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 20);
    if ((U32)ValueOffset > AttributeView->AttributeLength ||
        ValueLength > (AttributeView->AttributeLength - (U32)ValueOffset)) {
        WARNING(TEXT("Invalid resident value offset=%u length=%u"), ValueOffset, ValueLength);
        return FALSE;
    }

    if (ValueOut != NULL) {
        *ValueOut = AttributeView->RecordBuffer + AttributeView->AttributeOffset + (U32)ValueOffset;
    }
    if (ValueLengthOut != NULL) *ValueLengthOut = ValueLength;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Handle FILE_NAME attribute during file-record parsing.
 */
static BOOL NtfsHandleFileNameAttribute(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, LPNTFS_ATTRIBUTE_PARSE_STATE ParseState) {
    const U8* Value;
    U32 ValueLength;

    UNUSED(ParseState);

    if (AttributeView == NULL) return FALSE;
    if (AttributeView->IsNonResident) return TRUE;
    if (!NtfsGetResidentValue(AttributeView, &Value, &ValueLength)) return FALSE;
    NtfsParseFileNameValue(Value, ValueLength, AttributeView->RecordInfo);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Handle DATA attribute during file-record parsing.
 */
static BOOL NtfsHandleDataAttribute(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, LPNTFS_ATTRIBUTE_PARSE_STATE ParseState) {
    const U8* Value;
    U32 ValueLength;

    if (AttributeView == NULL || ParseState == NULL) return FALSE;
    if (ParseState->DataFound || AttributeView->NameLength != 0) return TRUE;

    if (AttributeView->IsNonResident) {
        U16 RunListOffset;

        if (AttributeView->AttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) {
            WARNING(TEXT("Invalid non-resident length=%u"), AttributeView->AttributeLength);
            return FALSE;
        }

        RunListOffset = NtfsLoadU16(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 32);
        if ((U32)RunListOffset >= AttributeView->AttributeLength) {
            WARNING(TEXT("Invalid runlist offset=%u"), RunListOffset);
            return FALSE;
        }

        AttributeView->RecordInfo->HasDataAttribute = TRUE;
        AttributeView->RecordInfo->DataIsResident = FALSE;
        AttributeView->RecordInfo->AllocatedDataSize = NtfsLoadU64(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 40);
        AttributeView->RecordInfo->DataSize = NtfsLoadU64(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 48);
        AttributeView->RecordInfo->InitializedDataSize = NtfsLoadU64(AttributeView->RecordBuffer + AttributeView->AttributeOffset + 56);
        ParseState->DataFound = TRUE;
        ParseState->DataAttributeOffset = AttributeView->AttributeOffset;
        ParseState->DataAttributeLength = AttributeView->AttributeLength;
        return TRUE;
    }

    if (!NtfsGetResidentValue(AttributeView, &Value, &ValueLength)) return FALSE;
    UNUSED(Value);
    AttributeView->RecordInfo->HasDataAttribute = TRUE;
    AttributeView->RecordInfo->DataIsResident = TRUE;
    AttributeView->RecordInfo->DataSize = U64_FromU32(ValueLength);
    AttributeView->RecordInfo->AllocatedDataSize = U64_FromU32(ValueLength);
    AttributeView->RecordInfo->InitializedDataSize = U64_FromU32(ValueLength);
    ParseState->DataFound = TRUE;
    ParseState->DataAttributeOffset = AttributeView->AttributeOffset;
    ParseState->DataAttributeLength = AttributeView->AttributeLength;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Handle OBJECT_IDENTIFIER attribute during file-record parsing.
 */
static BOOL NtfsHandleObjectIdentifierAttribute(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, LPNTFS_ATTRIBUTE_PARSE_STATE ParseState) {
    const U8* Value;
    U32 ValueLength;

    UNUSED(ParseState);

    if (AttributeView == NULL) return FALSE;

    AttributeView->RecordInfo->ObjectIdentifier.IsPresent = TRUE;
    if (AttributeView->IsNonResident) return TRUE;
    if (!NtfsGetResidentValue(AttributeView, &Value, &ValueLength)) return FALSE;

    if (ValueLength >= sizeof(AttributeView->RecordInfo->ObjectIdentifier.Value)) {
        MemoryCopy(
            AttributeView->RecordInfo->ObjectIdentifier.Value,
            Value,
            sizeof(AttributeView->RecordInfo->ObjectIdentifier.Value));
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Handle SECURITY_DESCRIPTOR attribute during file-record parsing.
 */
static BOOL NtfsHandleSecurityDescriptorAttribute(
    LPNTFS_ATTRIBUTE_VIEW AttributeView, LPNTFS_ATTRIBUTE_PARSE_STATE ParseState) {
    const U8* Value;
    U32 ValueLength;

    UNUSED(ParseState);

    if (AttributeView == NULL) return FALSE;

    AttributeView->RecordInfo->SecurityDescriptor.IsPresent = TRUE;
    AttributeView->RecordInfo->SecurityDescriptor.IsResident = !AttributeView->IsNonResident;

    if (AttributeView->IsNonResident) {
        if (AttributeView->AttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) {
            WARNING(TEXT("Invalid non-resident length=%u"), AttributeView->AttributeLength);
            return FALSE;
        }
        AttributeView->RecordInfo->SecurityDescriptor.Size = NtfsLoadU64(
            AttributeView->RecordBuffer + AttributeView->AttributeOffset + 48);
        return TRUE;
    }

    if (!NtfsGetResidentValue(AttributeView, &Value, &ValueLength)) return FALSE;
    UNUSED(Value);
    AttributeView->RecordInfo->SecurityDescriptor.Size = U64_FromU32(ValueLength);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return the handler function for one NTFS attribute type.
 */
static NTFS_ATTRIBUTE_HANDLER_FN NtfsFindAttributeHandler(U32 AttributeType) {
    static const NTFS_ATTRIBUTE_HANDLER_ENTRY Handlers[] = {
        {.AttributeType = NTFS_ATTRIBUTE_FILE_NAME, .Handler = NtfsHandleFileNameAttribute},
        {.AttributeType = NTFS_ATTRIBUTE_OBJECT_IDENTIFIER, .Handler = NtfsHandleObjectIdentifierAttribute},
        {.AttributeType = NTFS_ATTRIBUTE_SECURITY_DESCRIPTOR, .Handler = NtfsHandleSecurityDescriptorAttribute},
        {.AttributeType = NTFS_ATTRIBUTE_DATA, .Handler = NtfsHandleDataAttribute},
    };
    U32 Index;

    for (Index = 0; Index < ARRAY_COUNT(Handlers); Index++) {
        if (Handlers[Index].AttributeType == AttributeType) {
            return Handlers[Index].Handler;
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Parse selected attributes from one file record using a handler table.
 *
 * @param RecordBuffer File record buffer after fixup.
 * @param RecordSize File record size in bytes.
 * @param RecordInfo Parsed record metadata.
 * @param DataAttributeOffsetOut Output byte offset of selected DATA attribute.
 * @param DataAttributeLengthOut Output length of selected DATA attribute.
 * @return TRUE on success, FALSE on malformed attribute stream.
 */
static BOOL NtfsParseFileRecordAttributes(
    const U8* RecordBuffer,
    U32 RecordSize,
    LPNTFS_FILE_RECORD_INFO RecordInfo,
    U32* DataAttributeOffsetOut,
    U32* DataAttributeLengthOut) {
    U32 AttributeOffset;
    U32 AttributeLength;
    NTFS_ATTRIBUTE_PARSE_STATE ParseState;

    if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = 0;
    if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = 0;
    if (RecordBuffer == NULL || RecordInfo == NULL) return FALSE;

    AttributeOffset = RecordInfo->SequenceOfAttributesOffset;
    if (AttributeOffset >= RecordInfo->UsedSize || AttributeOffset >= RecordSize) {
        WARNING(TEXT("Invalid attribute offset=%u"), AttributeOffset);
        return FALSE;
    }

    MemorySet(&ParseState, 0, sizeof(NTFS_ATTRIBUTE_PARSE_STATE));
    while (AttributeOffset + 8 <= RecordInfo->UsedSize && AttributeOffset + 8 <= RecordSize) {
        U32 AttributeType;
        BOOL IsNonResident;
        U8 NameLength;
        NTFS_ATTRIBUTE_VIEW AttributeView;
        NTFS_ATTRIBUTE_HANDLER_FN AttributeHandler;

        AttributeType = NtfsLoadU32(RecordBuffer + AttributeOffset);
        if (AttributeType == NTFS_ATTRIBUTE_END_MARKER) {
            if (ParseState.DataFound) {
                if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = ParseState.DataAttributeOffset;
                if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = ParseState.DataAttributeLength;
            }
            return TRUE;
        }

        AttributeLength = NtfsLoadU32(RecordBuffer + AttributeOffset + 4);
        if (AttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
            WARNING(TEXT("Invalid attribute length=%u"), AttributeLength);
            return FALSE;
        }

        if (AttributeOffset > RecordInfo->UsedSize - AttributeLength ||
            AttributeOffset > RecordSize - AttributeLength) {
            WARNING(TEXT("Attribute out of bounds offset=%u length=%u"),
                AttributeOffset, AttributeLength);
            return FALSE;
        }

        IsNonResident = RecordBuffer[AttributeOffset + 8] != 0;
        NameLength = RecordBuffer[AttributeOffset + 9];
        AttributeView.RecordBuffer = RecordBuffer;
        AttributeView.RecordSize = RecordSize;
        AttributeView.RecordInfo = RecordInfo;
        AttributeView.AttributeType = AttributeType;
        AttributeView.AttributeOffset = AttributeOffset;
        AttributeView.AttributeLength = AttributeLength;
        AttributeView.IsNonResident = IsNonResident;
        AttributeView.NameLength = NameLength;

        AttributeHandler = NtfsFindAttributeHandler(AttributeType);
        if (AttributeHandler != NULL && !AttributeHandler(&AttributeView, &ParseState)) {
            return FALSE;
        }

        AttributeOffset += AttributeLength;
    }

    if (ParseState.DataFound) {
        if (DataAttributeOffsetOut != NULL) *DataAttributeOffsetOut = ParseState.DataAttributeOffset;
        if (DataAttributeLengthOut != NULL) *DataAttributeLengthOut = ParseState.DataAttributeLength;
    }

    WARNING(TEXT("Missing attribute end marker"));
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read one non-resident DATA stream using runlist mapping.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param DataAttribute Pointer to DATA attribute header.
 * @param DataAttributeLength DATA attribute length.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param DataSize Logical data size in bytes.
 * @param BytesReadOut Output number of bytes copied in Buffer.
 * @return TRUE on success, FALSE on malformed runlist or read failure.
 */
BOOL NtfsReadNonResidentDataAttribute(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut) {
    return NtfsReadNonResidentDataAttributeRange(
        FileSystem,
        DataAttribute,
        DataAttributeLength,
        U64_Make(0, 0),
        Buffer,
        BufferSize,
        DataSize,
        BytesReadOut);
}

/***************************************************************************/

/**
 * @brief Read one non-resident DATA stream range using runlist mapping.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param DataAttribute Pointer to DATA attribute header.
 * @param DataAttributeLength DATA attribute length.
 * @param DataOffset Stream start offset in bytes.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param DataSize Logical data size in bytes.
 * @param BytesReadOut Output number of bytes copied in Buffer.
 * @return TRUE on success, FALSE on malformed runlist or read failure.
 */
BOOL NtfsReadNonResidentDataAttributeRange(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    U64 DataOffset,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut) {
    U16 RunListOffset;
    const U8* RunPointer;
    const U8* RunEnd;
    U32 TargetBytes;
    U32 BytesWritten;
    U64 RemainingOffset;
    U32 BytesPerSector;
    I32 CurrentLcn;
    U8* SectorBuffer;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || DataAttribute == NULL || Buffer == NULL) return FALSE;
    if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE) return FALSE;
    if (U64_Cmp(DataOffset, DataSize) >= 0) return TRUE;
    if (FileSystem->BytesPerSector == 0) return FALSE;

    BytesPerSector = FileSystem->BytesPerSector;
    RemainingOffset = DataOffset;

    TargetBytes = BufferSize;
    {
        U64 RemainingData = U64_Sub(DataSize, DataOffset);
        if (U64_High32(RemainingData) == 0 && U64_Low32(RemainingData) < TargetBytes) {
            TargetBytes = U64_Low32(RemainingData);
        }
    }

    if (TargetBytes == 0) {
        if (BytesReadOut != NULL) *BytesReadOut = 0;
        return TRUE;
    }

    RunListOffset = NtfsLoadU16(DataAttribute + 32);
    if (RunListOffset >= DataAttributeLength) {
        WARNING(TEXT("Invalid runlist offset=%u"), RunListOffset);
        return FALSE;
    }

    RunPointer = DataAttribute + RunListOffset;
    RunEnd = DataAttribute + DataAttributeLength;
    BytesWritten = 0;
    CurrentLcn = 0;
    SectorBuffer = NULL;

    while (RunPointer < RunEnd && BytesWritten < TargetBytes) {
        U8 Header;
        U32 LengthSize;
        U32 OffsetSize;
        U64 ClusterCount64;
        U32 ClusterCount;
        I32 LcnDelta;
        BOOL IsSparse;
        U32 RunBytes;
        U64 RunBytes64;
        U32 SkipInRun;
        U32 CopyBytes;

        Header = *RunPointer++;
        if (Header == 0) break;

        LengthSize = Header & 0x0F;
        OffsetSize = (Header >> 4) & 0x0F;
        if (LengthSize == 0) {
            WARNING(TEXT("Invalid run length size=0"));
            return FALSE;
        }

        if (RunPointer > RunEnd || LengthSize > (U32)(RunEnd - RunPointer) ||
            OffsetSize > (U32)(RunEnd - (RunPointer + LengthSize))) {
            WARNING(TEXT("Truncated runlist"));
            return FALSE;
        }

        if (!NtfsLoadUnsignedLittleEndian(RunPointer, LengthSize, &ClusterCount64)) return FALSE;
        RunPointer += LengthSize;

        if (U64_High32(ClusterCount64) != 0) {
            WARNING(TEXT("Cluster count too large"));
            return FALSE;
        }
        ClusterCount = U64_Low32(ClusterCount64);
        if (ClusterCount == 0) continue;

        RunBytes = 0;
        IsSparse = OffsetSize == 0;
        LcnDelta = 0;
        if (!IsSparse) {
            if (!NtfsLoadSignedLittleEndian(RunPointer, OffsetSize, &LcnDelta)) return FALSE;
            CurrentLcn += LcnDelta;
        }
        RunPointer += OffsetSize;

        if (ClusterCount > 0xFFFFFFFF / FileSystem->BytesPerCluster) {
            WARNING(TEXT("Run byte size overflow"));
            return FALSE;
        }
        RunBytes = ClusterCount * FileSystem->BytesPerCluster;
        RunBytes64 = U64_FromU32(RunBytes);
        if (U64_Cmp(RemainingOffset, RunBytes64) >= 0) {
            RemainingOffset = U64_Sub(RemainingOffset, RunBytes64);
            continue;
        }

        SkipInRun = U64_Low32(RemainingOffset);
        RemainingOffset = U64_Make(0, 0);

        CopyBytes = TargetBytes - BytesWritten;
        if (CopyBytes > (RunBytes - SkipInRun)) {
            CopyBytes = RunBytes - SkipInRun;
        }
        if (CopyBytes == 0) continue;

        if (IsSparse) {
            MemorySet((U8*)Buffer + BytesWritten, 0, CopyBytes);
        } else {
            U32 ClusterLcn;
            U32 SectorOffset;
            U32 StartSector;
            U32 RelativeSector;
            U32 OffsetInSector;
            U32 RemainingCopy;
            U32 DestinationOffset;

            if (CurrentLcn < 0) {
                WARNING(TEXT("Invalid LCN"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }

            ClusterLcn = (U32)CurrentLcn;
            if (ClusterLcn > 0xFFFFFFFF / FileSystem->SectorsPerCluster) {
                WARNING(TEXT("LCN sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }

            SectorOffset = ClusterLcn * FileSystem->SectorsPerCluster;
            RelativeSector = SkipInRun / BytesPerSector;
            OffsetInSector = SkipInRun % BytesPerSector;
            if (SectorOffset > 0xFFFFFFFF - RelativeSector) {
                WARNING(TEXT("Partition sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }
            SectorOffset += RelativeSector;
            if (FileSystem->PartitionStart > 0xFFFFFFFF - SectorOffset) {
                WARNING(TEXT("Partition sector overflow"));
                if (SectorBuffer != NULL) KernelHeapFree(SectorBuffer);
                return FALSE;
            }
            StartSector = FileSystem->PartitionStart + SectorOffset;

            if (SectorBuffer == NULL) {
                SectorBuffer = (U8*)KernelHeapAlloc(BytesPerSector);
                if (SectorBuffer == NULL) {
                    ERROR(TEXT("Unable to allocate sector buffer"));
                    return FALSE;
                }
            }

            RemainingCopy = CopyBytes;
            DestinationOffset = BytesWritten;
            while (RemainingCopy > 0) {
                U32 Chunk = BytesPerSector - OffsetInSector;
                if (Chunk > RemainingCopy) Chunk = RemainingCopy;

                if (!NtfsReadSectors(FileSystem, StartSector, 1, SectorBuffer, BytesPerSector)) {
                    KernelHeapFree(SectorBuffer);
                    return FALSE;
                }

                MemoryCopy(
                    ((U8*)Buffer) + DestinationOffset,
                    SectorBuffer + OffsetInSector,
                    Chunk);

                DestinationOffset += Chunk;
                RemainingCopy -= Chunk;
                StartSector++;
                OffsetInSector = 0;
            }
        }

        BytesWritten += CopyBytes;
    }

    if (SectorBuffer != NULL) {
        KernelHeapFree(SectorBuffer);
    }

    if (BytesReadOut != NULL) *BytesReadOut = BytesWritten;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read one NTFS default DATA stream range by file-record index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param Offset Start offset within stream.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for bytes copied to Buffer.
 * @return TRUE on success, FALSE on malformed attributes or read failure.
 */
BOOL NtfsReadFileDataRangeByIndex(
    LPFILESYSTEM FileSystem,
    U32 Index,
    U64 Offset,
    LPVOID Buffer,
    U32 BufferSize,
    U32* BytesReadOut) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;
    NTFS_FILE_RECORD_INFO RecordInfo;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || Buffer == NULL) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        U8* DataAttribute;
        U32 ValueLength;
        U32 ValueOffset;
        U32 BytesToCopy;

        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        if (!NtfsLoadFileRecordBuffer(NtfsFileSystem, Index, &RecordBuffer, &Header)) {
            return FALSE;
        }

        MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
        RecordInfo.Index = Index;
        RecordInfo.RecordSize = NtfsFileSystem->FileRecordSize;
        RecordInfo.UsedSize = Header.RealSize;
        RecordInfo.Flags = Header.Flags;
        RecordInfo.SequenceNumber = Header.SequenceNumber;
        RecordInfo.ReferenceCount = Header.ReferenceCount;
        RecordInfo.SequenceOfAttributesOffset = Header.SequenceOfAttributesOffset;
        RecordInfo.UpdateSequenceOffset = Header.UpdateSequenceOffset;
        RecordInfo.UpdateSequenceSize = Header.UpdateSequenceSize;

        DataAttributeOffset = 0;
        DataAttributeLength = 0;
        if (!NtfsParseFileRecordAttributes(
                RecordBuffer,
                NtfsFileSystem->FileRecordSize,
                &RecordInfo,
                &DataAttributeOffset,
                &DataAttributeLength)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        if (!RecordInfo.HasDataAttribute || DataAttributeLength == 0) {
            KernelHeapFree(RecordBuffer);
            if (BytesReadOut != NULL) *BytesReadOut = 0;
            return TRUE;
        }

        if (U64_Cmp(Offset, RecordInfo.DataSize) >= 0) {
            KernelHeapFree(RecordBuffer);
            if (BytesReadOut != NULL) *BytesReadOut = 0;
            return TRUE;
        }

        DataAttribute = RecordBuffer + DataAttributeOffset;
        if (RecordInfo.DataIsResident) {
            U32 StartOffset;

            if (DataAttributeLength < NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }

            if (U64_High32(Offset) != 0) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }
            StartOffset = U64_Low32(Offset);

            ValueLength = NtfsLoadU32(DataAttribute + 16);
            ValueOffset = NtfsLoadU16(DataAttribute + 20);
            if (ValueOffset > DataAttributeLength || ValueLength > (DataAttributeLength - ValueOffset)) {
                KernelHeapFree(RecordBuffer);
                return FALSE;
            }
            if (StartOffset >= ValueLength) {
                KernelHeapFree(RecordBuffer);
                return TRUE;
            }

            BytesToCopy = ValueLength - StartOffset;
            if (BytesToCopy > BufferSize) BytesToCopy = BufferSize;
            if (BytesToCopy > 0) {
                MemoryCopy(Buffer, DataAttribute + ValueOffset + StartOffset, BytesToCopy);
            }
            if (BytesReadOut != NULL) *BytesReadOut = BytesToCopy;
            KernelHeapFree(RecordBuffer);
            return TRUE;
        }

        if (!NtfsReadNonResidentDataAttributeRange(
                NtfsFileSystem,
                DataAttribute,
                DataAttributeLength,
                Offset,
                Buffer,
                BufferSize,
                RecordInfo.DataSize,
                BytesReadOut)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        KernelHeapFree(RecordBuffer);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read one MFT file record and parse the base record header.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param RecordInfo Destination metadata structure.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsReadFileRecord(LPFILESYSTEM FileSystem, U32 Index, LPNTFS_FILE_RECORD_INFO RecordInfo) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U8* RecordBuffer;
    NTFS_FILE_RECORD_HEADER Header;
    U32 DataAttributeOffset;
    U32 DataAttributeLength;

    if (RecordInfo != NULL) {
        MemorySet(RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    }

    if (FileSystem == NULL || RecordInfo == NULL) return FALSE;

    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        if (!NtfsLoadFileRecordBuffer(NtfsFileSystem, Index, &RecordBuffer, &Header)) {
            return FALSE;
        }

        RecordInfo->Index = Index;
        RecordInfo->RecordSize = NtfsFileSystem->FileRecordSize;
        RecordInfo->UsedSize = Header.RealSize;
        RecordInfo->Flags = Header.Flags;
        RecordInfo->SequenceNumber = Header.SequenceNumber;
        RecordInfo->ReferenceCount = Header.ReferenceCount;
        RecordInfo->SequenceOfAttributesOffset = Header.SequenceOfAttributesOffset;
        RecordInfo->UpdateSequenceOffset = Header.UpdateSequenceOffset;
        RecordInfo->UpdateSequenceSize = Header.UpdateSequenceSize;
        DataAttributeOffset = 0;
        DataAttributeLength = 0;

        if (!NtfsParseFileRecordAttributes(
                RecordBuffer,
                NtfsFileSystem->FileRecordSize,
                RecordInfo,
                &DataAttributeOffset,
                &DataAttributeLength)) {
            KernelHeapFree(RecordBuffer);
            return FALSE;
        }

        KernelHeapFree(RecordBuffer);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Read default DATA stream for one file record by MFT index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index File record index in $MFT.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for bytes copied to Buffer.
 * @return TRUE on success, FALSE on malformed attributes or read failure.
 */
BOOL NtfsReadFileDataByIndex(
    LPFILESYSTEM FileSystem, U32 Index, LPVOID Buffer, U32 BufferSize, U32* BytesReadOut) {
    return NtfsReadFileDataRangeByIndex(
        FileSystem,
        Index,
        U64_Make(0, 0),
        Buffer,
        BufferSize,
        BytesReadOut);
}
