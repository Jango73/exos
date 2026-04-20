
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


    File System

\************************************************************************/

#include "fs/FileSystem.h"

#include "console/Console.h"
#include "drivers/filesystems/NTFS.h"
#include "fs/File.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "package/PackageNamespace.h"
#include "text/CoreString.h"
#include "fs/SystemFS.h"
#include "utils/Helpers.h"
#include "User.h"
#include "text/Text.h"
#include "utils/TOML.h"

extern BOOL MountPartition_FAT16(LPSTORAGE_UNIT, LPBOOT_PARTITION, U32, U32);
extern BOOL MountPartition_FAT32(LPSTORAGE_UNIT, LPBOOT_PARTITION, U32, U32);
extern BOOL MountPartition_EXFS(LPSTORAGE_UNIT, LPBOOT_PARTITION, U32, U32);
extern BOOL MountPartition_EXT2(LPSTORAGE_UNIT, LPBOOT_PARTITION, U32, U32);

/***************************************************************************/

#define FILESYSTEM_VER_MAJOR 1
#define FILESYSTEM_VER_MINOR 0
#define FILESYSTEM_MAX_SECTOR_SIZE 4096

/***************************************************************************/

typedef struct PACKED tag_GPT_HEADER {
    U8 Signature[8];
    U32 Revision;
    U32 HeaderSize;
    U32 HeaderCrc32;
    U32 Reserved;
    U64 CurrentLba;
    U64 BackupLba;
    U64 FirstUsableLba;
    U64 LastUsableLba;
    U8 DiskGuid[GPT_GUID_LENGTH];
    U64 PartitionEntryLba;
    U32 NumPartitionEntries;
    U32 SizeOfPartitionEntry;
    U32 PartitionArrayCrc32;
} GPT_HEADER, *LPGPT_HEADER;

typedef struct PACKED tag_GPT_ENTRY {
    U8 TypeGuid[GPT_GUID_LENGTH];
    U8 UniqueGuid[GPT_GUID_LENGTH];
    U64 FirstLba;
    U64 LastLba;
    U64 Attributes;
    U16 Name[36];
} GPT_ENTRY, *LPGPT_ENTRY;

/***************************************************************************/

static UINT FileSystemDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION FileSystemDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = FILESYSTEM_VER_MAJOR,
    .VersionMinor = FILESYSTEM_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "FileSystems",
    .Alias = "filesystems",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = FileSystemDriverCommands};

/***************************************************************************/

/**
 * @brief Retrieves the file system driver descriptor.
 * @return Pointer to the file system driver.
 */
LPDRIVER FileSystemGetDriver(void) {
    return &FileSystemDriver;
}

/***************************************************************************/

/**
 * @brief Retrieve disk sector size in bytes.
 * @param Disk Target disk.
 * @return Sector size in bytes (512 by default).
 */
static U32 FileSystemGetDiskBytesPerSector(LPSTORAGE_UNIT Disk) {
    DISKINFO DiskInfo;

    if (Disk == NULL || Disk->Driver == NULL) return SECTOR_SIZE;

    MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
    DiskInfo.Disk = Disk;
    if (Disk->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo) != DF_RETURN_SUCCESS) {
        return SECTOR_SIZE;
    }

    if (DiskInfo.BytesPerSector == 0) {
        return SECTOR_SIZE;
    }

    return DiskInfo.BytesPerSector;
}

/***************************************************************************/

/**
 * @brief Read one full on-disk sector from a disk.
 * @param Disk Target disk.
 * @param Sector LBA sector index.
 * @param Buffer Destination buffer.
 * @param BufferSize Buffer size in bytes.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL FileSystemReadDiskSector(LPSTORAGE_UNIT Disk, U32 Sector, LPVOID Buffer, U32 BufferSize) {
    IOCONTROL Control;
    U32 BytesPerSector = 0;

    if (Disk == NULL || Buffer == NULL) return FALSE;
    BytesPerSector = FileSystemGetDiskBytesPerSector(Disk);
    if (BytesPerSector > FILESYSTEM_MAX_SECTOR_SIZE) return FALSE;
    if (BufferSize < BytesPerSector) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = BytesPerSector;

    return (Disk->Driver->Command(DF_DISK_READ, (UINT)&Control) == DF_RETURN_SUCCESS);
}

/***************************************************************************/

/**
 * @brief Compare a fixed signature inside a sector buffer.
 * @param Buffer Sector data.
 * @param Offset Byte offset in sector.
 * @param Signature Expected signature bytes.
 * @param Length Signature length.
 * @return TRUE when signature matches, FALSE otherwise.
 */
static BOOL FileSystemSectorHasSignature(const U8* Buffer, U32 Offset, const U8* Signature, U32 Length) {
    if (Buffer == NULL || Signature == NULL) return FALSE;
    if (Length == 0) return FALSE;
    if ((Offset + Length) > SECTOR_SIZE) return FALSE;

    for (U32 Index = 0; Index < Length; Index++) {
        if (Buffer[Offset + Index] != Signature[Index]) {
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Probe filesystem format from partition on-disk signatures.
 * @param Disk Target storage unit.
 * @param StartSector Partition first sector.
 * @return PARTITION_FORMAT_* value, or UNKNOWN when not detected.
 */
static U32 FileSystemDetectPartitionFormat(LPSTORAGE_UNIT Disk, SECTOR StartSector) {
    U8 SectorBuffer[FILESYSTEM_MAX_SECTOR_SIZE];
    const U8 SignatureNtfs[8] = {'N', 'T', 'F', 'S', ' ', ' ', ' ', ' '};
    const U8 SignatureFat32[8] = {'F', 'A', 'T', '3', '2', ' ', ' ', ' '};
    const U8 SignatureFat16[8] = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '};
    U32 BytesPerSector = 0;
    U32 ExtMagicOffset = 1024 + 56;
    U32 ExtMagicSectorOffset = 0;
    U32 ExtMagicByteOffset = 0;

    if (Disk == NULL) return PARTITION_FORMAT_UNKNOWN;
    BytesPerSector = FileSystemGetDiskBytesPerSector(Disk);
    if (BytesPerSector > FILESYSTEM_MAX_SECTOR_SIZE) return PARTITION_FORMAT_UNKNOWN;

    if (!FileSystemReadDiskSector(Disk, StartSector, SectorBuffer, sizeof(SectorBuffer))) {
        return PARTITION_FORMAT_UNKNOWN;
    }

    if (FileSystemSectorHasSignature(SectorBuffer, 3, SignatureNtfs, 8)) {
        return PARTITION_FORMAT_NTFS;
    }

    if (FileSystemSectorHasSignature(SectorBuffer, 82, SignatureFat32, 8)) {
        return PARTITION_FORMAT_FAT32;
    }

    if (FileSystemSectorHasSignature(SectorBuffer, 54, SignatureFat16, 8)) {
        return PARTITION_FORMAT_FAT16;
    }

    // EXT superblock starts at byte 1024, magic (0xEF53) at offset 0x38.
    ExtMagicSectorOffset = ExtMagicOffset / BytesPerSector;
    ExtMagicByteOffset = ExtMagicOffset % BytesPerSector;
    if ((ExtMagicByteOffset + 1) < BytesPerSector &&
        FileSystemReadDiskSector(Disk, StartSector + ExtMagicSectorOffset, SectorBuffer, sizeof(SectorBuffer)) &&
        SectorBuffer[ExtMagicByteOffset + 0] == 0x53 &&
        SectorBuffer[ExtMagicByteOffset + 1] == 0xEF) {
        return PARTITION_FORMAT_EXT2;
    }

    return PARTITION_FORMAT_UNKNOWN;
}

/***************************************************************************/

/**
 * @brief Compare two GPT GUIDs.
 * @param Left First GUID.
 * @param Right Second GUID.
 * @return TRUE when identical, FALSE otherwise.
 */
static BOOL GptGuidEquals(const U8* Left, const U8* Right) {
    if (Left == NULL || Right == NULL) return FALSE;
    for (U32 Index = 0; Index < GPT_GUID_LENGTH; Index++) {
        if (Left[Index] != Right[Index]) return FALSE;
    }
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether a GPT GUID is zero-filled.
 * @param Guid GUID bytes.
 * @return TRUE when the GUID is empty, FALSE otherwise.
 */
static BOOL GptGuidIsZero(const U8* Guid) {
    if (Guid == NULL) return TRUE;
    for (U32 Index = 0; Index < GPT_GUID_LENGTH; Index++) {
        if (Guid[Index] != 0u) return FALSE;
    }
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Returns the newly mounted filesystem since the provided tail snapshot.
 * @param PreviousLast Last item from the filesystem list before mount attempt.
 * @return Mounted filesystem pointer, or NULL when nothing new was mounted.
 */
static LPFILESYSTEM ResolveMountedFileSystem(LPFILESYSTEM PreviousLast) {
    LPLIST FileSystemList = GetFileSystemList();
    LPFILESYSTEM MountedFileSystem =
        (LPFILESYSTEM)(FileSystemList != NULL ? FileSystemList->Last : NULL);

    if (MountedFileSystem == NULL || MountedFileSystem == PreviousLast) {
        return NULL;
    }

    return MountedFileSystem;
}

/***************************************************************************/

/**
 * @brief Stores partition metadata in a filesystem descriptor.
 * @param FileSystem Filesystem object.
 * @param Scheme Partition scheme constant.
 * @param Type Partition type identifier (MBR type value).
 * @param TypeGuid GPT type GUID, or NULL when not applicable.
 * @param Index Partition index.
 * @param Flags Partition flags.
 * @param StartSector First sector of the partition.
 * @param NumSectors Number of sectors in the partition.
 * @param Format Mounted filesystem format.
 * @param Mounted TRUE when the filesystem is mounted, FALSE otherwise.
 */
static void SetFileSystemPartitionInfo(
    LPFILESYSTEM FileSystem,
    U32 Scheme,
    U32 Type,
    const U8* TypeGuid,
    U32 Index,
    U32 Flags,
    SECTOR StartSector,
    U32 NumSectors,
    U32 Format,
    BOOL Mounted) {
    if (FileSystem == NULL) return;

    FileSystem->Mounted = Mounted;
    FileSystem->Partition.Scheme = Scheme;
    FileSystem->Partition.Type = Type;
    FileSystem->Partition.Format = Format;
    FileSystem->Partition.Index = Index;
    FileSystem->Partition.Flags = Flags;
    FileSystem->Partition.StartSector = StartSector;
    FileSystem->Partition.NumSectors = NumSectors;
    MemorySet(FileSystem->Partition.TypeGuid, 0, GPT_GUID_LENGTH);

    if (TypeGuid != NULL) {
        MemoryCopy(FileSystem->Partition.TypeGuid, TypeGuid, GPT_GUID_LENGTH);
    }
}

/***************************************************************************/

/**
 * @brief Registers a discovered but non-mounted partition descriptor.
 * @param Disk Target storage unit.
 * @param Scheme Partition scheme constant.
 * @param Type Partition type identifier (MBR type value).
 * @param TypeGuid GPT type GUID, or NULL when not applicable.
 * @param Index Partition index.
 * @param Flags Partition flags.
 * @param StartSector First sector of the partition.
 * @param NumSectors Number of sectors in the partition.
 * @param Format Detected partition format.
 */
static void RegisterUnusedFileSystem(
    LPSTORAGE_UNIT Disk,
    U32 Scheme,
    U32 Type,
    const U8* TypeGuid,
    U32 Index,
    U32 Flags,
    SECTOR StartSector,
    U32 NumSectors,
    U32 Format) {
    LPLIST UnusedFileSystemList = GetUnusedFileSystemList();
    LPFILESYSTEM FileSystem = NULL;

    if (Disk == NULL || UnusedFileSystemList == NULL) return;

    FileSystem = (LPFILESYSTEM)CreateKernelObject(sizeof(FILESYSTEM), KOID_FILESYSTEM);
    if (FileSystem == NULL) return;

    InitMutex(&(FileSystem->Mutex));
    FileSystem->Driver = NULL;
    FileSystem->StorageUnit = Disk;
    GetDefaultFileSystemName(FileSystem->Name, Disk, Index);
    if (Format == PARTITION_FORMAT_UNKNOWN) {
        Format = FileSystemDetectPartitionFormat(Disk, StartSector);
    }
    SetFileSystemPartitionInfo(FileSystem,
        Scheme,
        Type,
        TypeGuid,
        Index,
        Flags,
        StartSector,
        NumSectors,
        Format,
        FALSE);

    ListAddItem(UnusedFileSystemList, FileSystem);
}

/***************************************************************************/

/**
 * @brief Mount a GPT FAT partition (ESP or data).
 * @param Disk Target disk.
 * @param Partition Partition descriptor.
 * @param PartIndex GPT entry index.
 * @return TRUE when a FAT file system is mounted, FALSE otherwise.
 */
static BOOL MountGptFatPartition(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 PartIndex, U32* FormatOut) {
    BOOL Mounted = FALSE;

    if (Disk == NULL || Partition == NULL) return FALSE;
    if (FormatOut != NULL) *FormatOut = PARTITION_FORMAT_UNKNOWN;

    Mounted = MountPartition_FAT32(Disk, Partition, 0u, PartIndex);
    if (Mounted) {
        if (FormatOut != NULL) *FormatOut = PARTITION_FORMAT_FAT32;
        DEBUG(TEXT("FAT32 mounted entry %u"), PartIndex);
        return TRUE;
    }

    Mounted = MountPartition_FAT16(Disk, Partition, 0u, PartIndex);
    if (Mounted) {
        if (FormatOut != NULL) *FormatOut = PARTITION_FORMAT_FAT16;
        DEBUG(TEXT("FAT16 mounted entry %u"), PartIndex);
        return TRUE;
    }

    WARNING(TEXT("FAT mount failed for entry %u"), PartIndex);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Mount GPT partitions from a disk.
 * @param Disk Target disk.
 * @return TRUE when the GPT is parsed, FALSE otherwise.
 */
static BOOL MountDiskPartitionsGpt(LPSTORAGE_UNIT Disk) {
    U8 SectorBuffer[FILESYSTEM_MAX_SECTOR_SIZE];
    GPT_HEADER Header;
    const U8 GptGuidLinuxExtx[GPT_GUID_LENGTH] = GPT_GUID_LINUX_EXTX;
    const U8 GptGuidEfiSystem[GPT_GUID_LENGTH] = GPT_GUID_EFI_SYSTEM;
    const U8 GptGuidMicrosoftBasicData[GPT_GUID_LENGTH] = GPT_GUID_MICROSOFT_BASIC_DATA;
    U32 DiskSectorBytes = 0;

    if (Disk == NULL) return FALSE;
    DiskSectorBytes = FileSystemGetDiskBytesPerSector(Disk);
    if (DiskSectorBytes > FILESYSTEM_MAX_SECTOR_SIZE) {
        WARNING(TEXT("Unsupported sector size %u"), DiskSectorBytes);
        return FALSE;
    }

    if (!FileSystemReadDiskSector(Disk, 1u, SectorBuffer, sizeof(SectorBuffer))) {
        WARNING(TEXT("GPT header read failed"));
        return FALSE;
    }

    MemoryCopy(&Header, SectorBuffer, sizeof(GPT_HEADER));
    if (Header.Signature[0] != 'E' || Header.Signature[1] != 'F' ||
        Header.Signature[2] != 'I' || Header.Signature[3] != ' ' ||
        Header.Signature[4] != 'P' || Header.Signature[5] != 'A' ||
        Header.Signature[6] != 'R' || Header.Signature[7] != 'T') {
        WARNING(TEXT("Invalid GPT signature"));
        return FALSE;
    }

    if (Header.SizeOfPartitionEntry == 0u || Header.NumPartitionEntries == 0u) {
        WARNING(TEXT("No GPT entries"));
        return FALSE;
    }

    if (Header.SizeOfPartitionEntry > DiskSectorBytes) {
        WARNING(TEXT("GPT entry size too large (%u)"), Header.SizeOfPartitionEntry);
        return FALSE;
    }

    if (U64_High32(Header.PartitionEntryLba) != 0u) {
        WARNING(TEXT("GPT entry LBA above 4GB not supported"));
        return FALSE;
    }

    U32 EntryLbaBase = U64_Low32(Header.PartitionEntryLba);
    U32 EntriesPerSector = DiskSectorBytes / Header.SizeOfPartitionEntry;
    if (EntriesPerSector == 0u) {
        WARNING(TEXT("GPT entry size invalid (%u)"), Header.SizeOfPartitionEntry);
        return FALSE;
    }

    DEBUG(TEXT("GPT entries=%u entry_size=%u"),
          Header.NumPartitionEntries, Header.SizeOfPartitionEntry);

    for (U32 EntryIndex = 0; EntryIndex < Header.NumPartitionEntries; EntryIndex++) {
        LPFILESYSTEM PreviousLast =
            (LPFILESYSTEM)((GetFileSystemList() != NULL) ? GetFileSystemList()->Last : NULL);
        U32 SectorIndex = EntryIndex / EntriesPerSector;
        U32 EntryInSector = EntryIndex % EntriesPerSector;
        U32 SectorLba = EntryLbaBase + SectorIndex;

        if (!FileSystemReadDiskSector(Disk, SectorLba, SectorBuffer, sizeof(SectorBuffer))) {
            WARNING(TEXT("GPT entry read failed at LBA %u"), SectorLba);
            return FALSE;
        }

        U32 EntryOffset = EntryInSector * Header.SizeOfPartitionEntry;
        if ((EntryOffset + sizeof(GPT_ENTRY)) > DiskSectorBytes) {
            continue;
        }

        GPT_ENTRY Entry;
        MemoryCopy(&Entry, SectorBuffer + EntryOffset, sizeof(GPT_ENTRY));

        if (GptGuidIsZero(Entry.TypeGuid)) {
            continue;
        }

        if (U64_High32(Entry.FirstLba) != 0u || U64_High32(Entry.LastLba) != 0u) {
            WARNING(TEXT("GPT entry %u above 4GB not supported"), EntryIndex);
            continue;
        }

        U32 FirstLba = U64_Low32(Entry.FirstLba);
        U32 LastLba = U64_Low32(Entry.LastLba);
        if (LastLba < FirstLba) {
            WARNING(TEXT("GPT entry %u has invalid range"), EntryIndex);
            continue;
        }

        BOOT_PARTITION Partition;
        MemorySet(&Partition, 0, sizeof(BOOT_PARTITION));
        Partition.LBA = (SECTOR)FirstLba;
        Partition.Size = (LastLba - FirstLba) + 1u;

        if (GptGuidEquals(Entry.TypeGuid, GptGuidLinuxExtx)) {
            Partition.Type = FSID_LINUX_EXT2;
            DEBUG(TEXT("Mounting EXT2 partition %u"), EntryIndex);
            if (!MountPartition_EXT2(Disk, &Partition, 0u, EntryIndex)) {
                WARNING(TEXT("EXT2 mount failed for entry %u"), EntryIndex);
                RegisterUnusedFileSystem(Disk,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    PARTITION_FORMAT_EXT2);
            } else {
                LPFILESYSTEM MountedFileSystem = ResolveMountedFileSystem(PreviousLast);
                SetFileSystemPartitionInfo(MountedFileSystem,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    PARTITION_FORMAT_EXT2,
                    TRUE);
            }
            continue;
        }

        if (GptGuidEquals(Entry.TypeGuid, GptGuidEfiSystem)) {
            U32 MountedFormat = PARTITION_FORMAT_UNKNOWN;
            DEBUG(TEXT("EFI FAT partition detected at entry %u"), EntryIndex);
            if (MountGptFatPartition(Disk, &Partition, EntryIndex, &MountedFormat)) {
                LPFILESYSTEM MountedFileSystem = ResolveMountedFileSystem(PreviousLast);
                SetFileSystemPartitionInfo(MountedFileSystem,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    MountedFormat,
                    TRUE);
            } else {
                RegisterUnusedFileSystem(Disk,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    PARTITION_FORMAT_UNKNOWN);
            }
            continue;
        }

        if (GptGuidEquals(Entry.TypeGuid, GptGuidMicrosoftBasicData)) {
            U32 MountedFormat = PARTITION_FORMAT_UNKNOWN;
            DEBUG(TEXT("Microsoft basic data detected at entry %u"), EntryIndex);
            if (MountGptFatPartition(Disk, &Partition, EntryIndex, &MountedFormat)) {
                LPFILESYSTEM MountedFileSystem = ResolveMountedFileSystem(PreviousLast);
                SetFileSystemPartitionInfo(MountedFileSystem,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    MountedFormat,
                    TRUE);
            } else if (MountPartition_NTFS(Disk, &Partition, 0u, EntryIndex)) {
                LPFILESYSTEM MountedFileSystem = ResolveMountedFileSystem(PreviousLast);
                SetFileSystemPartitionInfo(MountedFileSystem,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    PARTITION_FORMAT_NTFS,
                    TRUE);
            } else {
                RegisterUnusedFileSystem(Disk,
                    PARTITION_SCHEME_GPT,
                    FSID_NONE,
                    Entry.TypeGuid,
                    EntryIndex,
                    0,
                    Partition.LBA,
                    Partition.Size,
                    PARTITION_FORMAT_NTFS);
            }
            continue;
        }

        RegisterUnusedFileSystem(Disk,
            PARTITION_SCHEME_GPT,
            FSID_NONE,
            Entry.TypeGuid,
            EntryIndex,
            0,
            Partition.LBA,
            Partition.Size,
            PARTITION_FORMAT_UNKNOWN);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Loads and parses the kernel configuration file.
 *
 * Attempts to read the kernel configuration file (case insensitive) and stores
 * the resulting TOML data in the kernel configuration state.
 */
static void ReadKernelConfiguration(void) {
    DEBUG(TEXT("Enter"));

    UINT Size = 0;
    LPVOID Buffer = FileReadAll(TEXT(KERNEL_CONFIG_NAME), &Size);

    if (Buffer == NULL) {
        Buffer = FileReadAll(TEXT(KERNEL_CONFIG_NAME_UPPER), &Size);

        SAFE_USE(Buffer) {
            DEBUG(TEXT("Config read from %s"),
                  TEXT(KERNEL_CONFIG_NAME_UPPER));
        }
    } else {
        DEBUG(TEXT("Config read from %s"),
              TEXT(KERNEL_CONFIG_NAME));
    }

    SAFE_USE(Buffer) {
        SetConfiguration(TomlParse((LPCSTR)Buffer));
        KernelHeapFree(Buffer);
    }

    DEBUG(TEXT("Exit"));
}

/***************************************************************************/

/**
 * @brief Test whether a filesystem contains the kernel configuration file.
 * @param FileSystem Target filesystem.
 * @param Name Configuration file name.
 * @return TRUE when found, FALSE otherwise.
 */
static BOOL FileSystemHasConfigFile(LPFILESYSTEM FileSystem, LPCSTR Name) {
    FILE_INFO Info;
    LPFILE File;
    UINT Result;

    if (FileSystem == NULL || Name == NULL) return FALSE;

    Info.Size = sizeof(FILE_INFO);
    Info.FileSystem = FileSystem;
    Info.Attributes = MAX_U32;
    Info.Flags = FILE_OPEN_READ;
    StringCopy(Info.Name, Name);

    Result = FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Info);
    File = (LPFILE)Result;
    SAFE_USE_VALID_ID(File, KOID_FILE) {
        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Select the active filesystem by locating the kernel config file.
 */
static void FileSystemSelectActivePartitionFromConfig(void) {
    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    LPLIST FileSystemList = GetFileSystemList();
    LPLISTNODE Node;
    LPFILESYSTEM FileSystem;

    if (GlobalInfo == NULL || FileSystemList == NULL) return;
    if (StringEmpty(GlobalInfo->ActivePartitionName) == FALSE) return;

    for (Node = FileSystemList->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;
        if (FileSystem == GetSystemFS()) continue;

        if (FileSystemHasConfigFile(FileSystem, TEXT(KERNEL_CONFIG_NAME)) ||
            FileSystemHasConfigFile(FileSystem, TEXT(KERNEL_CONFIG_NAME_UPPER))) {
            DEBUG(TEXT("Active partition set to %s"),
                  FileSystem->Name);
            FileSystemSetActivePartition(FileSystem);
            return;
        }
    }

    WARNING(TEXT("Config not found in any filesystem"));
}

/***************************************************************************/

/**
 * @brief Gets the number of mounted file systems.
 *
 * @return Number of file systems currently mounted in the system
 */
U32 GetNumFileSystems(void) {
    LPLIST FileSystemList = GetFileSystemList();
    return FileSystemList != NULL ? FileSystemList->NumItems : 0;
}

/***************************************************************************/

/**
 * @brief Returns the storage unit associated with a mounted filesystem.
 *
 * @param FileSystem Mounted filesystem instance.
 * @return Associated storage unit pointer, or NULL for virtual filesystems.
 */
LPSTORAGE_UNIT FileSystemGetStorageUnit(LPFILESYSTEM FileSystem) {
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        return FileSystem->StorageUnit;
    }
    return NULL;
}

/***************************************************************************/

/**
 * @brief Indicates whether a mounted filesystem is backed by a storage unit.
 *
 * @param FileSystem Mounted filesystem instance.
 * @return TRUE when the filesystem has a backing disk, FALSE otherwise.
 */
BOOL FileSystemHasStorageUnit(LPFILESYSTEM FileSystem) {
    return FileSystemGetStorageUnit(FileSystem) != NULL;
}

/***************************************************************************/

/**
 * @brief Indicates whether runtime mounts can be attached to SystemFS.
 * @return TRUE when SystemFS root is initialized, FALSE otherwise.
 */
BOOL FileSystemReady(void) {
    LPSYSTEMFSFILESYSTEM SystemFS = GetSystemFSData();
    return (SystemFS != NULL && SystemFS->Root != NULL);
}

/***************************************************************************/

/**
 * @brief Returns a readable partition scheme name.
 * @param Scheme Partition scheme constant.
 * @return Constant string describing the scheme.
 */
LPCSTR FileSystemGetPartitionSchemeName(U32 Scheme) {
    switch (Scheme) {
        case PARTITION_SCHEME_MBR:
            return TEXT("MBR");
        case PARTITION_SCHEME_GPT:
            return TEXT("GPT");
        case PARTITION_SCHEME_VIRTUAL:
            return TEXT("VIRTUAL");
        default:
            return TEXT("NONE");
    }
}

/***************************************************************************/

/**
 * @brief Returns a readable mounted partition format name.
 * @param Format Partition format constant.
 * @return Constant string for the mounted format.
 */
LPCSTR FileSystemGetPartitionFormatName(U32 Format) {
    switch (Format) {
        case PARTITION_FORMAT_FAT16:
            return TEXT("FAT16");
        case PARTITION_FORMAT_FAT32:
            return TEXT("FAT32");
        case PARTITION_FORMAT_NTFS:
            return TEXT("NTFS");
        case PARTITION_FORMAT_EXFS:
            return TEXT("EXFS");
        case PARTITION_FORMAT_EXT2:
            return TEXT("EXT2");
        case PARTITION_FORMAT_EXT3:
            return TEXT("EXT3");
        case PARTITION_FORMAT_EXT4:
            return TEXT("EXT4");
        default:
            return TEXT("UNKNOWN");
    }
}

/***************************************************************************/

/**
 * @brief Returns a readable partition type description.
 * @param Partition Partition metadata.
 * @return Constant string for the partition type.
 */
LPCSTR FileSystemGetPartitionTypeName(LPPARTITION Partition) {
    if (Partition == NULL) return TEXT("UNKNOWN");

    if (Partition->Scheme == PARTITION_SCHEME_GPT) {
        const U8 GptGuidLinuxExtx[GPT_GUID_LENGTH] = GPT_GUID_LINUX_EXTX;
        const U8 GptGuidEfiSystem[GPT_GUID_LENGTH] = GPT_GUID_EFI_SYSTEM;
        const U8 GptGuidMicrosoftBasicData[GPT_GUID_LENGTH] = GPT_GUID_MICROSOFT_BASIC_DATA;

        if (GptGuidEquals(Partition->TypeGuid, GptGuidLinuxExtx)) {
            return TEXT("Linux filesystem");
        }
        if (GptGuidEquals(Partition->TypeGuid, GptGuidEfiSystem)) {
            return TEXT("EFI System");
        }
        if (GptGuidEquals(Partition->TypeGuid, GptGuidMicrosoftBasicData)) {
            return TEXT("Microsoft Basic Data");
        }
        return TEXT("Unknown GPT type");
    }

    if (Partition->Scheme != PARTITION_SCHEME_MBR) {
        return TEXT("N/A");
    }

    switch (Partition->Type) {
        case FSID_NONE:
            return TEXT("Unused");
        case FSID_EXTENDED:
            return TEXT("Extended");
        case FSID_LINUX_EXTENDED:
            return TEXT("Linux Extended");
        case FSID_DOS_FAT16S:
            return TEXT("FAT16 (< 32MB)");
        case FSID_DOS_FAT16L:
            return TEXT("FAT16");
        case FSID_DOS_FAT32:
            return TEXT("FAT32");
        case FSID_DOS_FAT32_LBA1:
            return TEXT("FAT32 (LBA)");
        case FSID_OS2_HPFS:
            return TEXT("NTFS/HPFS");
        case FSID_EXOS:
            return TEXT("EXOS");
        case FSID_LINUX_EXT2:
            return TEXT("Linux native");
#if FSID_LINUX_EXT3 != FSID_LINUX_EXT2
        case FSID_LINUX_EXT3:
            return TEXT("Linux EXT3");
#endif
#if FSID_LINUX_EXT4 != FSID_LINUX_EXT2
        case FSID_LINUX_EXT4:
            return TEXT("Linux EXT4");
#endif
#if FSID_LINUXNATIVE != FSID_LINUX_EXT2
        case FSID_LINUXNATIVE:
            return TEXT("Linux native");
#endif
        case FSID_GPT_PROTECTIVE:
            return TEXT("GPT Protective MBR");
        default:
            return TEXT("Unknown MBR type");
    }
}

/***************************************************************************/

/**
 * @brief Generates a default file system name for a disk partition.
 *
 * Creates a volume name using the disk type and zero-based partition index.
 * The naming convention helps identify partitions systematically.
 *
 * @param Name Buffer to store the generated name (must be large enough)
 * @param Disk Pointer to physical disk structure
 * @param PartIndex Zero-based partition index on the disk
 * @return TRUE if name was generated successfully, FALSE otherwise
 */
BOOL GetDefaultFileSystemName(LPSTR Name, LPSTORAGE_UNIT Disk, U32 PartIndex) {
    STR Temp[12];
    LPLISTNODE Node;
    LPSTORAGE_UNIT CurrentDisk;
    U32 DiskIndex = 0;

    // Find the index of this disk among disks of the same type
    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
        CurrentDisk = (LPSTORAGE_UNIT)Node;
        if (CurrentDisk == Disk) break;
        if (CurrentDisk->Driver->Type == Disk->Driver->Type) DiskIndex++;
    }

    switch (Disk->Driver->Type) {
        case DRIVER_TYPE_RAMDISK:
            StringCopy(Name, Text_Prefix_RAMDrive);
            break;
        case DRIVER_TYPE_FLOPPYDISK:
            StringCopy(Name, Text_Prefix_FloppyDrive);
            break;
        case DRIVER_TYPE_USB_STORAGE:
            StringCopy(Name, Text_Prefix_USBDrive);
            break;
        case DRIVER_TYPE_NVME_STORAGE:
            StringCopy(Name, Text_Prefix_NVMe);
            break;
        case DRIVER_TYPE_SATA_STORAGE:
            StringCopy(Name, Text_Prefix_SATADrive);
            break;
        case DRIVER_TYPE_ATA_STORAGE:
            StringCopy(Name, Text_Prefix_ATADrive);
            break;
        default:
            StringCopy(Name, Text_Prefix_Drive);
            break;
    }

    // Append the zero-based disk index
    U32ToString(DiskIndex, Temp);
    StringConcat(Name, Temp);
    StringConcat(Name, TEXT("p"));

    // Append the zero-based partition index
    U32ToString(PartIndex, Temp);
    StringConcat(Name, Temp);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Stores the logical name of the active partition.
 *
 * Updates the kernel-wide file system information so that higher level
 * components can retrieve the currently active partition name.
 *
 * @param FileSystem Mounted file system flagged as active in the MBR.
 */
void FileSystemSetActivePartition(LPFILESYSTEM FileSystem) {
    SAFE_USE(FileSystem) {
        FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
        StringCopy(GlobalInfo->ActivePartitionName, FileSystem->Name);
        DEBUG(TEXT("Active partition name set to %s"), FileSystem->Name);
    }
}

/***************************************************************************/

/**
 * @brief Mounts extended partitions from a disk.
 *
 * Reads and processes extended partition table entries to discover
 * and mount logical drives within extended partitions.
 *
 * @param Disk Pointer to physical disk structure
 * @param Partition Pointer to boot partition information
 * @param Base Base sector address for partition calculations
 * @return TRUE if extended partitions were processed successfully, FALSE otherwise
 */
BOOL MountPartition_Extended(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base) {
    U8 Buffer[FILESYSTEM_MAX_SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;
    U32 BytesPerSector;

    BytesPerSector = FileSystemGetDiskBytesPerSector(Disk);
    if (BytesPerSector > FILESYSTEM_MAX_SECTOR_SIZE) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = BytesPerSector;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    Base += Partition->LBA;

    Partition = (LPBOOT_PARTITION)(Buffer + MBR_PARTITION_START);

    return MountDiskPartitions(Disk, Partition, Base);
}

/***************************************************************************/

/**
 * @brief Mounts all partitions found on a physical disk.
 *
 * Reads the Master Boot Record (MBR) and processes each partition entry,
 * attempting to mount supported file system types (FAT16, FAT32, NTFS, EXFS).
 * Handles both primary and extended partitions recursively.
 *
 * @param Disk Pointer to physical disk structure
 * @param Partition Pointer to boot partition array, or NULL to read from disk
 * @param Base Base sector address for partition offset calculations
 * @return TRUE if partitions were processed successfully, FALSE otherwise
 */
BOOL MountDiskPartitions(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base) {
    U8 Buffer[FILESYSTEM_MAX_SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;
    U32 Index;
    U32 BytesPerSector;

    DEBUG(TEXT("Disk = %x, Partition = %x, Base = %x"), Disk, Partition, Base);

    BytesPerSector = FileSystemGetDiskBytesPerSector(Disk);
    if (BytesPerSector > FILESYSTEM_MAX_SECTOR_SIZE) {
        WARNING(TEXT("Unsupported sector size %u"), BytesPerSector);
        return FALSE;
    }

    if (Partition == NULL) {
        Control.TypeID = KOID_IOCONTROL;
        Control.Disk = Disk;
        Control.SectorLow = 0;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;
        Control.Buffer = (LPVOID)Buffer;
        Control.BufferSize = BytesPerSector;

        Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
        if (Result != DF_RETURN_SUCCESS) {
            WARNING(TEXT("MBR read failed result=%x"), Result);
            return FALSE;
        }

        Partition = (LPBOOT_PARTITION)(Buffer + MBR_PARTITION_START);
    }

    //-------------------------------------
    // Read the list of partitions

    for (Index = 0; Index < MBR_PARTITION_COUNT; Index++) {
        if (Partition[Index].Type == FSID_GPT_PROTECTIVE) {
            DEBUG(TEXT("GPT protective MBR detected"));
            return MountDiskPartitionsGpt(Disk);
        }
    }

    for (Index = 0; Index < MBR_PARTITION_COUNT; Index++) {
        if (Partition[Index].LBA != 0) {
            BOOL PartitionMounted = FALSE;
            BOOL PartitionIsActive = ((Partition[Index].Disk & 0x80) != 0);
            U32 PartitionFormat = PARTITION_FORMAT_UNKNOWN;
            LPLIST FileSystemList = GetFileSystemList();
            LPFILESYSTEM PreviousLast =
                (LPFILESYSTEM)(FileSystemList != NULL ? FileSystemList->Last : NULL);

            switch (Partition[Index].Type) {
                case FSID_NONE:
                    break;

                case FSID_EXTENDED:
                case FSID_LINUX_EXTENDED: {
                    MountPartition_Extended(Disk, Partition + Index, Base);
                } break;

                case FSID_DOS_FAT16S:
                case FSID_DOS_FAT16L: {
                    PartitionFormat = PARTITION_FORMAT_FAT16;
                    DEBUG(TEXT("Mounting FAT16 partition"));
                    PartitionMounted =
                        MountPartition_FAT16(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_DOS_FAT32:
                case FSID_DOS_FAT32_LBA1: {
                    PartitionFormat = PARTITION_FORMAT_FAT32;
                    DEBUG(TEXT("Mounting FAT32 partition"));
                    PartitionMounted =
                        MountPartition_FAT32(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_OS2_HPFS: {
                    PartitionFormat = PARTITION_FORMAT_NTFS;
                    DEBUG(TEXT("Mounting NTFS partition"));
                    PartitionMounted =
                        MountPartition_NTFS(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_EXOS: {
                    PartitionFormat = PARTITION_FORMAT_EXFS;
                    DEBUG(TEXT("Mounting EXFS partition"));
                    PartitionMounted =
                        MountPartition_EXFS(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_LINUX_EXT2:
                    PartitionFormat = PARTITION_FORMAT_EXT2;
#if FSID_LINUX_EXT3 != FSID_LINUX_EXT2
                case FSID_LINUX_EXT3:
                    PartitionFormat = PARTITION_FORMAT_EXT3;
#endif
#if FSID_LINUX_EXT4 != FSID_LINUX_EXT2
                case FSID_LINUX_EXT4:
                    PartitionFormat = PARTITION_FORMAT_EXT4;
#endif
#if FSID_LINUXNATIVE != FSID_LINUX_EXT2
                case FSID_LINUXNATIVE:
                    PartitionFormat = PARTITION_FORMAT_EXT2;
#endif
                {
                    DEBUG(TEXT("Mounting EXT2 partition"));
                    PartitionMounted =
                        MountPartition_EXT2(Disk, Partition + Index, Base, Index);
                } break;

                default: {
                    WARNING(TEXT("Partition type %X not implemented\n"),
                        (U32)Partition[Index].Type);
                } break;
            }

            if (PartitionMounted) {
                LPLIST FileSystemList = GetFileSystemList();
                LPFILESYSTEM MountedFileSystem =
                    (LPFILESYSTEM)(FileSystemList != NULL ? FileSystemList->Last : NULL);

                if (MountedFileSystem != NULL && MountedFileSystem != PreviousLast) {
                    SetFileSystemPartitionInfo(MountedFileSystem,
                        PARTITION_SCHEME_MBR,
                        Partition[Index].Type,
                        NULL,
                        Index,
                        PartitionIsActive ? PARTITION_FLAG_ACTIVE : 0,
                        Base + Partition[Index].LBA,
                        Partition[Index].Size,
                        PartitionFormat,
                        TRUE);

                    if (GetSystemFSData()->Root != NULL) {
                        if (!SystemFSMountFileSystem(MountedFileSystem)) {
                            WARNING(TEXT("SystemFS mount failed for %s"),
                                MountedFileSystem->Name);
                        }
                    } else {
                        WARNING(TEXT("SystemFS not ready for %s"),
                            MountedFileSystem->Name);
                    }
                    if (PartitionIsActive) {
                        FileSystemSetActivePartition(MountedFileSystem);
                    }
                }
            } else if (Partition[Index].Type != FSID_NONE &&
                       Partition[Index].Type != FSID_EXTENDED &&
                       Partition[Index].Type != FSID_LINUX_EXTENDED) {
                RegisterUnusedFileSystem(Disk,
                    PARTITION_SCHEME_MBR,
                    Partition[Index].Type,
                    NULL,
                    Index,
                    PartitionIsActive ? PARTITION_FLAG_ACTIVE : 0,
                    Base + Partition[Index].LBA,
                    Partition[Index].Size,
                    PartitionFormat);
            }
        }
    }

    return TRUE;
}

/***************************************************************************/
/**
 * @brief Mounts available disk partitions and the system file system.
 */

void InitializeFileSystems(void) {
    LPLISTNODE Node;

    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    LPLIST UnusedFileSystemList = GetUnusedFileSystemList();
    StringClear(GlobalInfo->ActivePartitionName);

    for (Node = UnusedFileSystemList != NULL ? UnusedFileSystemList->First : NULL; Node;) {
        LPLISTNODE Next = Node->Next;
        ReleaseKernelObject((LPFILESYSTEM)Node);
        Node = Next;
    }

    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
        MountDiskPartitions((LPSTORAGE_UNIT)Node, NULL, 0);
    }

    FileSystemSelectActivePartitionFromConfig();

    MountSystemFS();
    ReadKernelConfiguration();
    MountUserNodes();
    PackageNamespaceInitialize();
}

/***************************************************************************/

/**
 * @brief Driver command handler for filesystem initialization.
 *
 * DF_LOAD mounts disk partitions and system FS once; DF_UNLOAD only clears
 * readiness.
 */
static UINT FileSystemDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((FileSystemDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeFileSystems();
            FileSystemDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((FileSystemDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            FileSystemDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(FILESYSTEM_VER_MAJOR, FILESYSTEM_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
