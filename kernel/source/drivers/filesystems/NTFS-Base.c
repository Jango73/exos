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


    NTFS base helpers and mount

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

/**
 * @brief Returns TRUE for supported disk sector sizes.
 *
 * @param BytesPerSector Logical bytes per sector.
 * @return TRUE for supported sizes, FALSE otherwise.
 */
BOOL NtfsIsSupportedSectorSize(U32 BytesPerSector) {
    return (BytesPerSector == 512) || (BytesPerSector == 4096);
}

/***************************************************************************/

/**
 * @brief Determines whether a value is a power of two.
 *
 * @param Value Input value.
 * @return TRUE when Value is a power of two, FALSE otherwise.
 */
BOOL NtfsIsPowerOfTwo(U32 Value) {
    if (Value == 0) return FALSE;
    return (Value & (Value - 1)) == 0;
}

/***************************************************************************/

/**
 * @brief Query logical bytes per sector from a storage unit.
 *
 * @param Disk Target storage unit.
 * @return Bytes per sector, or 0 when unavailable.
 */
U32 NtfsGetDiskBytesPerSector(LPSTORAGE_UNIT Disk) {
    DISKINFO DiskInfo;
    U32 Result;

    if (Disk == NULL || Disk->Driver == NULL) return 0;

    MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
    DiskInfo.Disk = Disk;
    Result = Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);
    if (Result != DF_RETURN_SUCCESS) return 0;

    return DiskInfo.BytesPerSector;
}

/***************************************************************************/

/**
 * @brief Load a U16 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
U16 NtfsLoadU16(LPCVOID Address) {
    U16 Value;

    MemoryCopy(&Value, Address, sizeof(U16));
    return Value;
}

/***************************************************************************/

/**
 * @brief Load a U32 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
U32 NtfsLoadU32(LPCVOID Address) {
    U32 Value;

    MemoryCopy(&Value, Address, sizeof(U32));
    return Value;
}

/***************************************************************************/

/**
 * @brief Load a U64 from an arbitrary memory address.
 *
 * @param Address Source address.
 * @return Loaded value.
 */
U64 NtfsLoadU64(LPCVOID Address) {
    U64 Value;

    MemoryCopy(&Value, Address, sizeof(U64));
    return Value;
}

/***************************************************************************/

/**
 * @brief Store a U16 to an arbitrary memory address.
 *
 * @param Address Destination address.
 * @param Value Value to store.
 */
void NtfsStoreU16(LPVOID Address, U16 Value) {
    MemoryCopy(Address, &Value, sizeof(U16));
}

/***************************************************************************/

/**
 * @brief Decode an unsigned little-endian integer from byte stream.
 *
 * @param Buffer Byte stream.
 * @param Size Number of bytes to decode.
 * @param ValueOut Decoded value.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL NtfsLoadUnsignedLittleEndian(const U8* Buffer, U32 Size, U64* ValueOut) {
    U32 Low;
    U32 High;
    U32 Index;

    if (ValueOut != NULL) *ValueOut = U64_Make(0, 0);
    if (Buffer == NULL || ValueOut == NULL) return FALSE;
    if (Size == 0 || Size > 8) return FALSE;

    Low = 0;
    High = 0;
    for (Index = 0; Index < Size; Index++) {
        if (Index < 4) {
            Low |= ((U32)Buffer[Index]) << (Index * 8);
        } else {
            High |= ((U32)Buffer[Index]) << ((Index - 4) * 8);
        }
    }

    *ValueOut = U64_Make(High, Low);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode a signed little-endian integer from byte stream.
 *
 * @param Buffer Byte stream.
 * @param Size Number of bytes to decode.
 * @param ValueOut Decoded signed value.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL NtfsLoadSignedLittleEndian(const U8* Buffer, U32 Size, I32* ValueOut) {
    U32 Value;
    U32 Index;

    if (ValueOut != NULL) *ValueOut = 0;
    if (Buffer == NULL || ValueOut == NULL) return FALSE;
    if (Size == 0 || Size > 4) return FALSE;

    Value = 0;
    for (Index = 0; Index < Size; Index++) {
        Value |= ((U32)Buffer[Index]) << (Index * 8);
    }

    if ((Buffer[Size - 1] & 0x80) != 0) {
        for (Index = Size; Index < 4; Index++) {
            Value |= ((U32)0xFF) << (Index * 8);
        }
    }

    *ValueOut = (I32)Value;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Rank FILE_NAME namespace priority.
 *
 * @param NameSpace NTFS FILE_NAME namespace value.
 * @return Rank where higher is preferred.
 */
U32 NtfsGetFileNameNamespaceRank(U8 NameSpace) {
    switch (NameSpace) {
        case NTFS_FILE_NAME_NAMESPACE_WIN32:
        case NTFS_FILE_NAME_NAMESPACE_WIN32_DOS:
            return 4;
        case NTFS_FILE_NAME_NAMESPACE_POSIX:
            return 3;
        case NTFS_FILE_NAME_NAMESPACE_DOS:
            return 1;
        default:
            return 0;
    }
}

/***************************************************************************/

/**
 * @brief Shift a U64 value left by one bit.
 *
 * @param Value Input value.
 * @return Shifted value.
 */
U64 NtfsU64ShiftLeft1(U64 Value) {
    U32 High;
    U32 Low;

    High = U64_High32(Value);
    Low = U64_Low32(Value);

    return U64_Make((High << 1) | (Low >> 31), Low << 1);
}

/***************************************************************************/

/**
 * @brief Shift a U64 value right by one bit.
 *
 * @param Value Input value.
 * @return Shifted value.
 */
U64 NtfsU64ShiftRight1(U64 Value) {
    U32 High;
    U32 Low;

    High = U64_High32(Value);
    Low = U64_Low32(Value);

    return U64_Make(High >> 1, (Low >> 1) | ((High & 1) << 31));
}

/***************************************************************************/

/**
 * @brief Multiply two U32 values and return a U64 product.
 *
 * @param Left Left factor.
 * @param Right Right factor.
 * @return 64-bit product.
 */
U64 NtfsMultiplyU32ToU64(U32 Left, U32 Right) {
    U64 Result = U64_Make(0, 0);
    U64 Addend = U64_FromU32(Left);

    while (Right != 0) {
        if ((Right & 1) != 0) {
            Result = U64_Add(Result, Addend);
        }
        Right >>= 1;
        if (Right != 0) {
            Addend = NtfsU64ShiftLeft1(Addend);
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Shift a U64 value right by N bits.
 *
 * @param Value Input value.
 * @param Shift Number of bits to shift.
 * @return Shifted value.
 */
U64 NtfsU64ShiftRight(U64 Value, U32 Shift) {
    while (Shift > 0) {
        Value = NtfsU64ShiftRight1(Value);
        Shift--;
    }
    return Value;
}

/***************************************************************************/

/**
 * @brief Return base-2 logarithm for power-of-two value.
 *
 * @param Value Input power-of-two.
 * @return Bit index.
 */
U32 NtfsLog2(U32 Value) {
    U32 Shift = 0;

    while (Value > 1) {
        Value >>= 1;
        Shift++;
    }

    return Shift;
}

/***************************************************************************/

/**
 * @brief Validate one MFT file-record index against mounted volume geometry.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Index Candidate file-record index.
 * @return TRUE when the index can fit in the partition MFT address space.
 */
BOOL NtfsIsValidFileRecordIndex(LPNTFSFILESYSTEM FileSystem, U32 Index) {
    U64 PartitionBytes;
    U64 MaxRecordCount;
    U32 RecordShift;

    if (FileSystem == NULL) return FALSE;
    if (FileSystem->BytesPerSector == 0 || FileSystem->FileRecordSize == 0) return FALSE;
    if (!NtfsIsPowerOfTwo(FileSystem->BytesPerSector) || !NtfsIsPowerOfTwo(FileSystem->FileRecordSize)) return FALSE;

    PartitionBytes = NtfsMultiplyU32ToU64(FileSystem->PartitionSize, FileSystem->BytesPerSector);
    RecordShift = NtfsLog2(FileSystem->FileRecordSize);
    MaxRecordCount = NtfsU64ShiftRight(PartitionBytes, RecordShift);

    if (U64_High32(MaxRecordCount) != 0) {
        return TRUE;
    }

    return Index < U64_Low32(MaxRecordCount);
}

/***************************************************************************/

/**
 * @brief Reads a partition boot sector.
 *
 * @param Disk Target disk.
 * @param BootSectorLba Boot sector LBA.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size.
 * @param BytesPerSectorOut Optional output for detected sector size.
 * @return TRUE on success, FALSE on read/validation failure.
 */
BOOL NtfsReadBootSector(
    LPSTORAGE_UNIT Disk, SECTOR BootSectorLba, LPVOID Buffer, U32 BufferSize, U32* BytesPerSectorOut) {
    IOCONTROL Control;
    U32 BytesPerSector;
    U32 Result;

    if (BytesPerSectorOut != NULL) *BytesPerSectorOut = 0;
    if (Disk == NULL || Disk->Driver == NULL || Buffer == NULL) return FALSE;

    BytesPerSector = NtfsGetDiskBytesPerSector(Disk);
    if (!NtfsIsSupportedSectorSize(BytesPerSector)) {
        WARNING(TEXT("Unsupported sector size %u"), BytesPerSector);
        return FALSE;
    }

    if (BytesPerSector > BufferSize || BytesPerSector > NTFS_MAX_SECTOR_SIZE) {
        WARNING(TEXT("Buffer too small for sector size %u"), BytesPerSector);
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = BootSectorLba;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = BytesPerSector;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Boot sector read failed result=%x"), Result);
        return FALSE;
    }

    if (BytesPerSectorOut != NULL) *BytesPerSectorOut = BytesPerSector;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read sectors from a mounted NTFS partition.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Sector Absolute disk sector.
 * @param NumSectors Number of sectors to read.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsReadSectors(
    LPNTFSFILESYSTEM FileSystem, SECTOR Sector, U32 NumSectors, LPVOID Buffer, U32 BufferSize) {
    IOCONTROL Control;
    U32 RelativeSector;
    U32 MaxBytes;
    U32 Result;

    if (FileSystem == NULL || Buffer == NULL) return FALSE;
    if (NumSectors == 0) return FALSE;

    if (Sector < FileSystem->PartitionStart) {
        WARNING(TEXT("Sector underflow %u"), Sector);
        return FALSE;
    }

    RelativeSector = Sector - FileSystem->PartitionStart;
    if (RelativeSector >= FileSystem->PartitionSize) {
        WARNING(TEXT("Sector out of partition %u"), Sector);
        return FALSE;
    }

    if (NumSectors > FileSystem->PartitionSize - RelativeSector) {
        WARNING(TEXT("Read over partition boundary sector=%u count=%u"),
            Sector, NumSectors);
        return FALSE;
    }

    if (NumSectors > 0xFFFFFFFF / FileSystem->BytesPerSector) {
        WARNING(TEXT("Byte size overflow count=%u"), NumSectors);
        return FALSE;
    }

    MaxBytes = NumSectors * FileSystem->BytesPerSector;
    if (BufferSize < MaxBytes) {
        WARNING(TEXT("Buffer too small %u<%u"), BufferSize, MaxBytes);
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = NumSectors;
    Control.Buffer = Buffer;
    Control.BufferSize = MaxBytes;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Read failed result=%x"), Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode file record size from NTFS boot data.
 *
 * @param BootSector Boot sector structure.
 * @param BytesPerCluster Bytes per cluster.
 * @param RecordSizeOut Destination record size in bytes.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsComputeFileRecordSize(
    LPNTFS_MBR BootSector, U32 BytesPerCluster, U32* RecordSizeOut) {
    U8 RawValue;
    U32 RecordSize;

    if (RecordSizeOut != NULL) *RecordSizeOut = 0;
    if (BootSector == NULL || RecordSizeOut == NULL) return FALSE;

    RawValue = (U8)(BootSector->FileRecordSize & 0xFF);
    if (RawValue == 0) {
        WARNING(TEXT("Invalid file record size byte=0"));
        return FALSE;
    }

    if ((RawValue & 0x80) == 0) {
        RecordSize = ((U32)RawValue) * BytesPerCluster;
    } else {
        I8 SignedValue = (I8)RawValue;
        U8 Shift = (U8)(-SignedValue);

        if (Shift > 31) {
            WARNING(TEXT("Invalid file record exponent=%u"), Shift);
            return FALSE;
        }

        RecordSize = (U32)(1 << Shift);
    }

    if (RecordSize < NTFS_MIN_FILE_RECORD_SIZE || RecordSize > NTFS_MAX_FILE_RECORD_SIZE) {
        WARNING(TEXT("Unsupported file record size=%u"), RecordSize);
        return FALSE;
    }

    if (!NtfsIsPowerOfTwo(RecordSize)) {
        WARNING(TEXT("File record size not power-of-two=%u"), RecordSize);
        return FALSE;
    }

    *RecordSizeOut = RecordSize;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute absolute sector of $MFT record 0.
 *
 * @param PartitionStart Partition start sector.
 * @param SectorsPerCluster Cluster size in sectors.
 * @param MftStartCluster MFT start cluster.
 * @param SectorOut Destination absolute sector.
 * @return TRUE on success, FALSE on overflow or unsupported value.
 */
BOOL NtfsComputeMftStartSector(
    SECTOR PartitionStart, U32 SectorsPerCluster, U64 MftStartCluster, U32* SectorOut) {
    U32 ClusterLow;
    U32 ClusterOffsetSectors;
    U32 MftSector;

    if (SectorOut != NULL) *SectorOut = 0;
    if (SectorOut == NULL) return FALSE;

    if (U64_High32(MftStartCluster) != 0) {
        WARNING(TEXT("Unsupported MFT cluster high part=%x"),
            (U32)U64_High32(MftStartCluster));
        return FALSE;
    }

    ClusterLow = (U32)U64_Low32(MftStartCluster);
    if (ClusterLow > 0xFFFFFFFF / SectorsPerCluster) {
        WARNING(TEXT("Cluster multiplication overflow cluster=%u"), ClusterLow);
        return FALSE;
    }

    ClusterOffsetSectors = ClusterLow * SectorsPerCluster;
    if (PartitionStart > 0xFFFFFFFF - ClusterOffsetSectors) {
        WARNING(TEXT("Sector overflow start=%u"), PartitionStart);
        return FALSE;
    }

    MftSector = PartitionStart + ClusterOffsetSectors;
    *SectorOut = MftSector;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply NTFS update sequence fixup on a file record buffer.
 *
 * @param RecordBuffer Record buffer to patch in place.
 * @param RecordSize Record size in bytes.
 * @param SectorSize Sector size in bytes.
 * @param UpdateSequenceOffset Update sequence array offset.
 * @param UpdateSequenceSize Number of U16 values in update sequence array.
 * @return TRUE on success, FALSE on validation mismatch.
 */
BOOL NtfsApplyFileRecordFixup(
    U8* RecordBuffer, U32 RecordSize, U32 SectorSize, U16 UpdateSequenceOffset, U16 UpdateSequenceSize) {
    U16 UpdateSequenceNumber;
    U32 SectorsInRecord;
    U32 FixupWords;
    U32 Index;

    if (RecordBuffer == NULL || SectorSize == 0) return FALSE;
    if (RecordSize == 0 || (RecordSize % SectorSize) != 0) return FALSE;
    if (UpdateSequenceSize < 2) return FALSE;

    SectorsInRecord = RecordSize / SectorSize;
    if ((U32)UpdateSequenceSize != (SectorsInRecord + 1)) {
        WARNING(TEXT("Invalid update sequence size=%u sectors=%u"),
            UpdateSequenceSize, SectorsInRecord);
        return FALSE;
    }

    FixupWords = (U32)UpdateSequenceSize;
    if ((U32)UpdateSequenceOffset > RecordSize) return FALSE;
    if (FixupWords > (RecordSize - (U32)UpdateSequenceOffset) / sizeof(U16)) {
        WARNING(TEXT("Update sequence out of range offset=%u words=%u"),
            UpdateSequenceOffset, FixupWords);
        return FALSE;
    }

    UpdateSequenceNumber = NtfsLoadU16(RecordBuffer + UpdateSequenceOffset);

    for (Index = 0; Index < SectorsInRecord; Index++) {
        U32 TailOffset = ((Index + 1) * SectorSize) - sizeof(U16);
        U16 TailValue = NtfsLoadU16(RecordBuffer + TailOffset);
        U16 Replacement;

        if (TailValue != UpdateSequenceNumber) {
            WARNING(TEXT("Update sequence mismatch index=%u"), Index);
            return FALSE;
        }

        Replacement = NtfsLoadU16(
            RecordBuffer + UpdateSequenceOffset + ((Index + 1) * sizeof(U16)));
        NtfsStoreU16(RecordBuffer + TailOffset, Replacement);
    }

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
static U32 NtfsGetVolumeInfo(LPVOLUME_INFO VolumeInfo) {
    LPFILESYSTEM Header;
    LPNTFSFILESYSTEM FileSystem;

    if (VolumeInfo == NULL || VolumeInfo->Size != sizeof(VOLUME_INFO)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Header = (LPFILESYSTEM)VolumeInfo->Volume;
    SAFE_USE_VALID_ID(Header, KOID_FILESYSTEM) {
        FileSystem = (LPNTFSFILESYSTEM)Header;
        if (!StringEmpty(FileSystem->VolumeLabel)) {
            StringCopy(VolumeInfo->Name, FileSystem->VolumeLabel);
        } else {
            StringCopy(VolumeInfo->Name, FileSystem->Header.Name);
        }
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief Dispatch entry point for the NTFS driver.
 *
 * @param Function Requested function ID.
 * @param Parameter Optional function parameter.
 * @return DF_RETURN_* status code.
 */
static UINT NTFSCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(NTFS_VER_MAJOR, NTFS_VER_MINOR);
        case DF_FS_GETVOLUME_INFO:
            return NtfsGetVolumeInfo((LPVOLUME_INFO)Parameter);
        case DF_FS_SETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_CREATEFOLDER:
            return NtfsCreateFolder((LPFILE_INFO)Parameter);
        case DF_FS_DELETEFOLDER:
            return NtfsDeleteFolder((LPFILE_INFO)Parameter);
        case DF_FS_RENAMEFOLDER:
            return NtfsRenameFolder((LPFILE_INFO)Parameter);
        case DF_FS_DELETEFILE:
            return NtfsDeleteFile((LPFILE_INFO)Parameter);
        case DF_FS_RENAMEFILE:
            return NtfsRenameFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENFILE:
            return (UINT)NtfsOpenFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENNEXT:
            return NtfsOpenNext((LPNTFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return NtfsCloseFile((LPNTFSFILE)Parameter);
        case DF_FS_READ:
            return NtfsReadFile((LPNTFSFILE)Parameter);
        case DF_FS_WRITE:
            return NtfsWriteFile((LPNTFSFILE)Parameter);
        default:
            return DF_RETURN_NOT_IMPLEMENTED;
    }
}

/***************************************************************************/

DRIVER DATA_SECTION NTFSDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = NTFS_VER_MAJOR,
    .VersionMinor = NTFS_VER_MINOR,
    .Designer = "Microsoft Corporation",
    .Manufacturer = "Microsoft Corporation",
    .Product = "NTFS File System",
    .Alias = "ntfs",
    .Command = NTFSCommands};

/***************************************************************************/

/**
 * @brief Mount an NTFS partition and cache boot geometry.
 *
 * @param Disk Physical disk.
 * @param Partition Partition descriptor.
 * @param Base Base LBA offset.
 * @param PartIndex Partition index.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL MountPartition_NTFS(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[NTFS_MAX_SECTOR_SIZE];
    LPNTFS_MBR BootSector;
    LPNTFSFILESYSTEM FileSystem;
    U32 DiskBytesPerSector;
    U32 BootBytesPerSector;
    U32 SectorsPerCluster;
    U32 BytesPerCluster;
    U32 FileRecordSize;
    U32 MftStartSector;
    U64 MftStartCluster;
    SECTOR PartitionStart;
    NTFS_FILE_RECORD_INFO RecordInfo;

    if (Disk == NULL || Partition == NULL) return FALSE;

    PartitionStart = Base + Partition->LBA;
    if (!NtfsReadBootSector(Disk, PartitionStart, Buffer, sizeof(Buffer), &DiskBytesPerSector)) {
        return FALSE;
    }

    if (Buffer[510] != 0x55 || Buffer[511] != 0xAA) {
        WARNING(TEXT("Invalid boot signature (%x, %x)"),
            Buffer[510], Buffer[511]);
        return FALSE;
    }

    BootSector = (LPNTFS_MBR)Buffer;
    if (BootSector->OEMName[0] != 'N' || BootSector->OEMName[1] != 'T' ||
        BootSector->OEMName[2] != 'F' || BootSector->OEMName[3] != 'S') {
        WARNING(TEXT("Invalid OEM name (%x %x %x %x %x %x %x %x)"),
            BootSector->OEMName[0], BootSector->OEMName[1],
            BootSector->OEMName[2], BootSector->OEMName[3],
            BootSector->OEMName[4], BootSector->OEMName[5],
            BootSector->OEMName[6], BootSector->OEMName[7]);
        return FALSE;
    }

    BootBytesPerSector = BootSector->BytesPerSector;
    if (!NtfsIsSupportedSectorSize(BootBytesPerSector)) {
        WARNING(TEXT("Unsupported boot sector size %u"), BootBytesPerSector);
        return FALSE;
    }

    if (BootBytesPerSector != DiskBytesPerSector) {
        WARNING(TEXT("Disk/boot sector mismatch %u/%u"),
            DiskBytesPerSector, BootBytesPerSector);
        return FALSE;
    }

    SectorsPerCluster = BootSector->SectorsPerCluster;
    if (!NtfsIsPowerOfTwo(SectorsPerCluster)) {
        WARNING(TEXT("Invalid sectors per cluster %u"), SectorsPerCluster);
        return FALSE;
    }

    BytesPerCluster = BootBytesPerSector * SectorsPerCluster;
    if (BytesPerCluster == 0) {
        WARNING(TEXT("Invalid bytes per cluster"));
        return FALSE;
    }

    MftStartCluster = BootSector->LCN_VCN0_MFT;
    if (!NtfsComputeFileRecordSize(BootSector, BytesPerCluster, &FileRecordSize)) {
        return FALSE;
    }

    if (!NtfsComputeMftStartSector(PartitionStart, SectorsPerCluster, MftStartCluster, &MftStartSector)) {
        return FALSE;
    }

    FileSystem = (LPNTFSFILESYSTEM)CreateKernelObject(sizeof(NTFSFILESYSTEM), KOID_FILESYSTEM);
    if (FileSystem == NULL) {
        ERROR(TEXT("Unable to allocate NTFS filesystem object"));
        return FALSE;
    }

    InitMutex(&(FileSystem->Header.Mutex));
    FileSystem->Header.Driver = &NTFSDriver;
    FileSystem->Header.StorageUnit = Disk;
    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    FileSystem->Disk = Disk;
    MemoryCopy(&(FileSystem->BootSector), BootSector, sizeof(NTFS_MBR));
    FileSystem->PartitionStart = PartitionStart;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BytesPerSector = BootBytesPerSector;
    FileSystem->SectorsPerCluster = SectorsPerCluster;
    FileSystem->BytesPerCluster = BytesPerCluster;
    FileSystem->FileRecordSize = FileRecordSize;
    FileSystem->MftStartSector = MftStartSector;
    FileSystem->MftStartCluster = MftStartCluster;
    StringClear(FileSystem->VolumeLabel);
    FileSystem->PathLookupCacheNextSlot = 0;
    MemorySet(FileSystem->PathLookupCache, 0, sizeof(FileSystem->PathLookupCache));

    ListAddItem(GetFileSystemList(), FileSystem);


    MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (NtfsReadFileRecord((LPFILESYSTEM)FileSystem, 0, &RecordInfo)) {
    } else {
        WARNING(TEXT("MFT[0] read failed"));
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve geometry cached at NTFS mount time.
 *
 * @param FileSystem Mounted filesystem pointer.
 * @param Geometry Destination geometry.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL NtfsGetVolumeGeometry(LPFILESYSTEM FileSystem, LPNTFS_VOLUME_GEOMETRY Geometry) {
    LPNTFSFILESYSTEM NtfsFileSystem;

    if (FileSystem == NULL || Geometry == NULL) return FALSE;
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;

        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
        Geometry->BytesPerSector = NtfsFileSystem->BytesPerSector;
        Geometry->SectorsPerCluster = NtfsFileSystem->SectorsPerCluster;
        Geometry->BytesPerCluster = NtfsFileSystem->BytesPerCluster;
        Geometry->FileRecordSize = NtfsFileSystem->FileRecordSize;
        Geometry->MftStartCluster = NtfsFileSystem->MftStartCluster;
        StringCopy(Geometry->VolumeLabel, NtfsFileSystem->VolumeLabel);

        return TRUE;
    }

    return FALSE;
}
