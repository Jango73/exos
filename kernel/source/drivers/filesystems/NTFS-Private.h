/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    NTFS private declarations

\************************************************************************/

#ifndef NTFS_PRIVATE_H_INCLUDED
#define NTFS_PRIVATE_H_INCLUDED

/***************************************************************************/

#include "drivers/filesystems/NTFS.h"
#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "utils/Unicode.h"

/***************************************************************************/

#define NTFS_VER_MAJOR 1
#define NTFS_VER_MINOR 0
#define NTFS_MAX_SECTOR_SIZE 4096
#define NTFS_MIN_FILE_RECORD_SIZE 512
#define NTFS_MAX_FILE_RECORD_SIZE 4096
#define NTFS_ATTRIBUTE_STANDARD_INFORMATION 0x10
#define NTFS_ATTRIBUTE_ATTRIBUTE_LIST 0x20
#define NTFS_ATTRIBUTE_FILE_NAME 0x30
#define NTFS_ATTRIBUTE_OBJECT_IDENTIFIER 0x40
#define NTFS_ATTRIBUTE_SECURITY_DESCRIPTOR 0x50
#define NTFS_ATTRIBUTE_DATA 0x80
#define NTFS_ATTRIBUTE_INDEX_ROOT 0x90
#define NTFS_ATTRIBUTE_INDEX_ALLOCATION 0xA0
#define NTFS_ATTRIBUTE_BITMAP 0xB0
#define NTFS_ATTRIBUTE_END_MARKER 0xFFFFFFFF
#define NTFS_ATTRIBUTE_HEADER_RESIDENT_SIZE 0x18
#define NTFS_ATTRIBUTE_HEADER_NON_RESIDENT_SIZE 0x40
#define NTFS_INDEX_ENTRY_FLAG_HAS_SUBNODE 0x0001
#define NTFS_INDEX_ENTRY_FLAG_LAST_ENTRY 0x0002
#define NTFS_FILE_NAME_ATTRIBUTE_MIN_SIZE 66
#define NTFS_MAX_INDEX_ALLOCATION_BYTES (16 * N_1MB)
#define NTFS_FILE_NAME_NAMESPACE_POSIX 0
#define NTFS_FILE_NAME_NAMESPACE_WIN32 1
#define NTFS_FILE_NAME_NAMESPACE_DOS 2
#define NTFS_FILE_NAME_NAMESPACE_WIN32_DOS 3
#define NTFS_ROOT_FILE_RECORD_INDEX 5
#define NTFS_PATH_LOOKUP_CACHE_SIZE 32
#define NTFS_ENUMERATION_WINDOW_SIZE 256

/***************************************************************************/

typedef struct tag_NTFS_PATH_LOOKUP_CACHE_ENTRY {
    BOOL IsValid;
    U32 ParentFolderIndex;
    U32 ChildFileRecordIndex;
    BOOL ChildIsFolder;
    STR Name[MAX_FILE_NAME];
} NTFS_PATH_LOOKUP_CACHE_ENTRY, *LPNTFS_PATH_LOOKUP_CACHE_ENTRY;

/***************************************************************************/

typedef struct tag_NTFSFILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Storage;
    NTFS_MBR BootSector;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BytesPerSector;
    U32 SectorsPerCluster;
    U32 BytesPerCluster;
    U32 FileRecordSize;
    U32 MftStartSector;
    U64 MftStartCluster;
    STR VolumeLabel[MAX_FS_LOGICAL_NAME];
    U32 PathLookupCacheNextSlot;
    NTFS_PATH_LOOKUP_CACHE_ENTRY PathLookupCache[NTFS_PATH_LOOKUP_CACHE_SIZE];
} NTFS_FILE_SYSTEM, *LPNTFSFILESYSTEM;

/***************************************************************************/

typedef struct tag_NTFSFILE {
    FILE Header;
    U32 FileRecordIndex;
    BOOL IsFolder;
    BOOL Enumerate;
    U32 EnumerationFolderIndex;
    U32 EnumerationNextStartIndex;
    BOOL EnumerationReachedEnd;
    STR EnumerationPattern[MAX_FILE_NAME];
    U32 EnumerationIndex;
    U32 EnumerationCount;
    LPNTFS_FOLDER_ENTRY_INFO EnumerationEntries;
} NTFS_FILE, *LPNTFSFILE;

/***************************************************************************/

typedef struct tag_NTFS_FILE_RECORD_HEADER {
    U32 Magic;
    U16 UpdateSequenceOffset;
    U16 UpdateSequenceSize;
    U64 LogFileSequenceNumber;
    U16 SequenceNumber;
    U16 ReferenceCount;
    U16 SequenceOfAttributesOffset;
    U16 Flags;
    U32 RealSize;
    U32 AllocatedSize;
    U64 BaseRecord;
    U16 MaximumAttributeID;
    U16 Alignment;
    U32 RecordNumber;
} NTFS_FILE_RECORD_HEADER, *LPNTFS_FILE_RECORD_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_ROOT_HEADER {
    U32 AttributeType;
    U32 CollationRule;
    U32 IndexBlockSize;
    U8 ClustersPerIndexBlock;
    U8 Reserved[3];
} NTFS_INDEX_ROOT_HEADER, *LPNTFS_INDEX_ROOT_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_HEADER {
    U32 EntryOffset;
    U32 EntrySize;
    U32 AllocatedEntrySize;
    U8 Flags;
    U8 Reserved[3];
} NTFS_INDEX_HEADER, *LPNTFS_INDEX_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_INDEX_RECORD_HEADER {
    U32 Magic;
    U16 UpdateSequenceOffset;
    U16 UpdateSequenceSize;
    U64 LogFileSequenceNumber;
    U64 Vcn;
    NTFS_INDEX_HEADER IndexHeader;
} NTFS_INDEX_RECORD_HEADER, *LPNTFS_INDEX_RECORD_HEADER;

/***************************************************************************/

typedef struct tag_NTFS_FOLDER_ENUM_CONTEXT {
    LPNTFSFILESYSTEM FileSystem;
    LPNTFS_FOLDER_ENTRY_INFO Entries;
    U32 MaxEntries;
    U32 StartEntryIndex;
    BOOL StopWhenWindowFilled;
    BOOL StopRequested;
    U32 EntryCount;
    U32 TotalEntries;
    const U8* IndexAllocation;
    U32 IndexAllocationSize;
    U32 IndexBlockSize;
    const U8* Bitmap;
    U32 BitmapSize;
    U8* VisitedVcnMap;
    U32 VisitedVcnMapSize;
    U32 DiagInvalidFileReferenceCount;
    U32 DiagInvalidRecordIndexCount;
    U32 DiagReadRecordFailureCount;
    U32 DiagSequenceMismatchCount;
    U32 DiagTraverseErrorCode;
    U32 DiagTraverseStage;
    U32 DiagTraverseVcn;
    U32 DiagHeaderRegionSize;
    U32 DiagEntryOffset;
    U32 DiagEntrySize;
    U32 DiagCursor;
    U32 DiagEntryLength;
    U32 DiagEntryFlags;
} NTFS_FOLDER_ENUM_CONTEXT, *LPNTFS_FOLDER_ENUM_CONTEXT;

/***************************************************************************/

extern DRIVER DATA_SECTION NTFSDriver;

/***************************************************************************/

BOOL NtfsIsSupportedSectorSize(U32 BytesPerSector);
BOOL NtfsIsPowerOfTwo(U32 Value);
U32 NtfsGetStorageBytesPerSector(LPSTORAGE_UNIT Storage);
U16 NtfsLoadU16(LPCVOID Address);
U32 NtfsLoadU32(LPCVOID Address);
U64 NtfsLoadU64(LPCVOID Address);
void NtfsStoreU16(LPVOID Address, U16 Value);
BOOL NtfsLoadUnsignedLittleEndian(const U8* Buffer, U32 Size, U64* ValueOut);
BOOL NtfsLoadSignedLittleEndian(const U8* Buffer, U32 Size, I32* ValueOut);
U32 NtfsGetFileNameNamespaceRank(U8 NameSpace);
U64 NtfsU64ShiftLeft1(U64 Value);
U64 NtfsU64ShiftRight1(U64 Value);
U64 NtfsMultiplyU32ToU64(U32 Left, U32 Right);
U64 NtfsU64ShiftRight(U64 Value, U32 Shift);
U32 NtfsLog2(U32 Value);
BOOL NtfsIsValidFileRecordIndex(LPNTFSFILESYSTEM FileSystem, U32 Index);
BOOL NtfsReadBootSector(
    LPSTORAGE_UNIT Storage, SECTOR BootSectorLba, LPVOID Buffer, U32 BufferSize, U32* BytesPerSectorOut);
BOOL NtfsReadSectors(
    LPNTFSFILESYSTEM FileSystem, SECTOR Sector, U32 NumSectors, LPVOID Buffer, U32 BufferSize);
BOOL NtfsComputeFileRecordSize(
    LPNTFS_MBR BootSector, U32 BytesPerCluster, U32* RecordSizeOut);
BOOL NtfsComputeMftStartSector(
    SECTOR PartitionStart, U32 SectorsPerCluster, U64 MftStartCluster, U32* SectorOut);
BOOL NtfsApplyFileRecordFixup(
    U8* RecordBuffer, U32 RecordSize, U32 SectorSize, U16 UpdateSequenceOffset, U16 UpdateSequenceSize);

/***************************************************************************/

BOOL NtfsLoadFileRecordBuffer(
    LPNTFSFILESYSTEM FileSystem, U32 Index, U8** RecordBufferOut, NTFS_FILE_RECORD_HEADER* HeaderOut);
BOOL NtfsReadNonResidentDataAttribute(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut);
BOOL NtfsReadNonResidentDataAttributeRange(
    LPNTFSFILESYSTEM FileSystem,
    const U8* DataAttribute,
    U32 DataAttributeLength,
    U64 DataOffset,
    LPVOID Buffer,
    U32 BufferSize,
    U64 DataSize,
    U32* BytesReadOut);
BOOL NtfsReadFileDataRangeByIndex(
    LPFILESYSTEM FileSystem,
    U32 Index,
    U64 Offset,
    LPVOID Buffer,
    U32 BufferSize,
    U32* BytesReadOut);

/***************************************************************************/

LPFILE NtfsOpenFile(LPFILE_INFO Info);
U32 NtfsOpenNext(LPNTFSFILE File);
U32 NtfsCloseFile(LPNTFSFILE File);
U32 NtfsReadFile(LPNTFSFILE File);
U32 NtfsWriteFile(LPNTFSFILE File);
U32 NtfsCreateFolder(LPFILE_INFO Info);
U32 NtfsDeleteFolder(LPFILE_INFO Info);
U32 NtfsRenameFolder(LPFILE_INFO Info);
U32 NtfsDeleteFile(LPFILE_INFO Info);
U32 NtfsRenameFile(LPFILE_INFO Info);

/***************************************************************************/

#endif  // NTFS_PRIVATE_H_INCLUDED
