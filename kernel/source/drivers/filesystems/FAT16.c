
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


    FAT16

\************************************************************************/

#include "core/Kernel.h"
#include "drivers/filesystems/FAT.h"
#include "fs/File-System.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT FAT16Commands(UINT, UINT);

DRIVER DATA_SECTION FAT16Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Microsoft Corporation",
    .Product = "Fat 16 File System",
    .Alias = "fat16",
    .Command = FAT16Commands};

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_FAT16FILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    FAT16MBR Master;
    SECTOR PartitionStart;
    U32 PartitionSize;
    SECTOR FATStart;
    SECTOR FATStart2;
    SECTOR DataStart;
    U32 SectorsInRoot;
    U32 BytesPerCluster;
    U8* IOBuffer;
    U32 IOBufferGeneration;
} FAT16FILESYSTEM, *LPFAT16FILESYSTEM;

/***************************************************************************/

typedef struct tag_FATFILE {
    FILE Header;
    FATFILELOC Location;
    U32 DirectoryBufferCluster;
    U32 DirectoryBufferGeneration;
    BOOL DirectoryBufferValid;
} FATFILE, *LPFATFILE;

/***************************************************************************/

/**
 * @brief Allocate and initialize a FAT16 filesystem object.
 *
 * @param Disk Physical disk to bind.
 * @return Allocated filesystem or NULL on failure.
 */
static LPFAT16FILESYSTEM NewFAT16FileSystem(LPSTORAGE_UNIT Disk) {
    LPFAT16FILESYSTEM This;

    This = (LPFAT16FILESYSTEM)KernelHeapAlloc(sizeof(FAT16FILESYSTEM));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FAT16FILESYSTEM));

    This->Header.TypeID = KOID_FILESYSTEM;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &FAT16Driver;
    This->Header.StorageUnit = Disk;
    This->Disk = Disk;
    This->FATStart = 0;
    This->FATStart2 = 0;
    This->DataStart = 0;
    This->BytesPerCluster = 0;
    This->IOBuffer = NULL;
    This->IOBufferGeneration = 0;

    InitMutex(&(This->Header.Mutex));

    return This;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a FAT file object.
 *
 * @param FileSystem Owning FAT16 filesystem.
 * @param FileLoc Location metadata of the file.
 * @return Allocated file object or NULL on failure.
 */
static LPFATFILE NewFATFile(LPFAT16FILESYSTEM FileSystem, LPFATFILELOC FileLoc) {
    LPFATFILE This;

    This = (LPFATFILE)KernelHeapAlloc(sizeof(FATFILE));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FATFILE));

    This->Header.TypeID = KOID_FILE;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.FileSystem = (LPFILESYSTEM)FileSystem;
    This->Location.PreviousCluster = FileLoc->PreviousCluster;
    This->Location.FolderCluster = FileLoc->FolderCluster;
    This->Location.FileCluster = FileLoc->FileCluster;
    This->Location.DataCluster = FileLoc->DataCluster;
    This->Location.Offset = FileLoc->Offset;
    This->DirectoryBufferCluster = 0;
    This->DirectoryBufferGeneration = 0;
    This->DirectoryBufferValid = FALSE;

    InitMutex(&(This->Header.Mutex));
    InitSecurity(&(This->Header.Security));

    return This;
}

/***************************************************************************/

BOOL MountPartition_FAT16(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE];
    LPFAT16MBR Master;
    LPFAT16FILESYSTEM FileSystem;
    BOOL Success;

    Success = FATReadBootSector(Disk, Partition, Base, (LPVOID)Buffer);
    if (Success == FALSE) return FALSE;

    //-------------------------------------
    // Assign a pointer to the sector

    Master = (LPFAT16MBR)Buffer;

    //-------------------------------------
    // Check if this is really a FAT16 partition

    if (Master->FATName[0] != 'F') return FALSE;
    if (Master->FATName[1] != 'A') return FALSE;
    if (Master->FATName[2] != 'T') return FALSE;
    if (Master->FATName[3] != '1') return FALSE;
    if (Master->FATName[4] != '6') return FALSE;

    //-------------------------------------
    // Create the file system object

    FileSystem = NewFAT16FileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    //-------------------------------------
    // Copy the Master Sector

    MemoryCopy(&(FileSystem->Master), Buffer, SECTOR_SIZE);

    FileSystem->PartitionStart = Base + Partition->LBA;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BytesPerCluster = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    FileSystem->IOBuffer = (U8*)KernelHeapAlloc(FileSystem->Master.SectorsPerCluster * SECTOR_SIZE);

    //-------------------------------------
    // Compute the start of the FAT

    FileSystem->FATStart = FileSystem->PartitionStart + FileSystem->Master.ReservedSectors;

    if (FileSystem->Master.NumFATs > 1) {
        FileSystem->FATStart2 = FileSystem->FATStart + FileSystem->Master.SectorsPerFAT;
    }

    //-------------------------------------
    // Compute the start of the data

    FileSystem->DataStart = FileSystem->FATStart + (FileSystem->Master.NumFATs * FileSystem->Master.SectorsPerFAT);

    FileSystem->SectorsInRoot =
        (FileSystem->Master.NumRootEntries * sizeof(FATDIRENTRY)) / (U32)FileSystem->Master.BytesPerSector;

    //-------------------------------------
    // Update global information and register the file system

    ListAddItem(GetFileSystemList(), FileSystem);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read a cluster from disk into buffer.
 *
 * @param FileSystem FAT16 filesystem context.
 * @param Cluster Cluster number to read.
 * @param Buffer Destination buffer (BytesPerCluster bytes).
 * @return TRUE on success, FALSE on failure.
 */
static BOOL ReadCluster(LPFAT16FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    IOCONTROL Control;
    SECTOR Sector;
    U32 NumSectors;
    U32 Result;

    // Cluster 1 does not exist in FAT16 but here it
    // is assumed to be the root directory

    if (Cluster == 1) {
        Sector = FileSystem->DataStart;
        NumSectors = FileSystem->SectorsInRoot;
        if (NumSectors > FileSystem->Master.SectorsPerCluster) {
            NumSectors = FileSystem->Master.SectorsPerCluster;
        }
    } else {
        Sector =
            FileSystem->DataStart + FileSystem->SectorsInRoot + ((Cluster - 2) * FileSystem->Master.SectorsPerCluster);
        NumSectors = FileSystem->Master.SectorsPerCluster;
    }

    if (Sector < FileSystem->PartitionStart || Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize) {
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = NumSectors;
    Control.Buffer = Buffer;
    Control.BufferSize = NumSectors * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    FileSystem->IOBufferGeneration++;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Write a cluster to disk from buffer.
 *
 * @param FileSystem FAT16 filesystem context.
 * @param Cluster Cluster number to write.
 * @param Buffer Source buffer (BytesPerCluster bytes).
 * @return TRUE on success, FALSE on failure.
 */
static BOOL WriteCluster(LPFAT16FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    IOCONTROL Control;
    SECTOR Sector;
    U32 NumSectors;
    U32 Result;

    // Cluster 1 does not exist in FAT16 but here it
    // is assumed to be the root directory

    if (Cluster == 1) {
        Sector = FileSystem->DataStart;
        NumSectors = FileSystem->SectorsInRoot;
        if (NumSectors > FileSystem->Master.SectorsPerCluster) {
            NumSectors = FileSystem->Master.SectorsPerCluster;
        }
    } else {
        Sector =
            FileSystem->DataStart + FileSystem->SectorsInRoot + ((Cluster - 2) * FileSystem->Master.SectorsPerCluster);
        NumSectors = FileSystem->Master.SectorsPerCluster;
    }

    if (Sector < FileSystem->PartitionStart || Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize) {
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = NumSectors;
    Control.Buffer = Buffer;
    Control.BufferSize = NumSectors * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    FileSystem->IOBufferGeneration++;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve next cluster in a FAT chain.
 *
 * @param FileSystem FAT16 filesystem context.
 * @param Cluster Current cluster.
 * @return Next cluster value or FAT16_CLUSTER_LAST.
 */
static CLUSTER GetNextClusterInChain(LPFAT16FILESYSTEM FileSystem, CLUSTER Cluster) {
    U16 Buffer[SECTOR_SIZE / sizeof(U16)];
    IOCONTROL Control;
    CLUSTER NextCluster;
    U32 NumEntriesPerSector;
    U32 Sector;
    U32 Offset;
    U32 Result;

    NextCluster = FAT16_CLUSTER_LAST;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U16);
    Sector = Cluster / NumEntriesPerSector;
    Offset = Cluster % NumEntriesPerSector;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->FATStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result == DF_RETURN_SUCCESS) {
        NextCluster = Buffer[Offset];
    }

    return NextCluster;
}

/***************************************************************************/

/**
 * @brief Decode a FAT directory entry into a null-terminated name.
 *
 * @param DirEntry FAT directory entry.
 * @param Name Output buffer for file name.
 */
static void DecodeFileName(LPFATDIRENTRY DirEntry, LPSTR Name) {
    U32 Index = 0;

    //-------------------------------------
    // 8.3 names

    for (Index = 0; Index < 8; Index++) {
        if (DirEntry->Name[Index] == STR_SPACE) break;
        *Name++ = DirEntry->Name[Index];
    }

    if (DirEntry->Ext[0] != STR_SPACE) {
        *Name++ = STR_DOT;
        for (Index = 0; Index < 3; Index++) {
            if (DirEntry->Ext[Index] == STR_SPACE) break;
            *Name++ = DirEntry->Ext[Index];
        }
    }

    *Name++ = STR_NULL;
}

/***************************************************************************/

/**
 * @brief Translate a FAT directory entry into FILE fields.
 *
 * @param DirEntry FAT directory entry.
 * @param File Target file object to populate.
 */
static void TranslateFileInfo(LPFATDIRENTRY DirEntry, LPFATFILE File) {
    //-------------------------------------
    // Translate the attributes

    File->Header.Attributes = 0;

    if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if (DirEntry->Attributes & FAT_ATTR_READONLY) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if (DirEntry->Attributes & FAT_ATTR_HIDDEN) {
        File->Header.Attributes |= FS_ATTR_HIDDEN;
    }

    if (DirEntry->Attributes & FAT_ATTR_SYSTEM) {
        File->Header.Attributes |= FS_ATTR_SYSTEM;
    }

    File->Header.Attributes |= FS_ATTR_EXECUTABLE;

    //-------------------------------------
    // Translate the size

    File->Header.SizeLow = DirEntry->Size;
    File->Header.SizeHigh = 0;

    //-------------------------------------
    // Translate the time

    File->Header.Modified.Year = ((DirEntry->Date & FAT_DATE_YEAR_MASK) >> FAT_DATE_YEAR_SHFT) + 1980;
    File->Header.Modified.Month = (DirEntry->Date & FAT_DATE_MONTH_MASK) >> FAT_DATE_MONTH_SHFT;
    File->Header.Modified.Day = (DirEntry->Date & FAT_DATE_DAY_MASK) >> FAT_DATE_DAY_SHFT;
    File->Header.Modified.Hour = (DirEntry->Time & FAT_TIME_HOUR_MASK) >> FAT_TIME_HOUR_SHFT;
    File->Header.Modified.Minute = (DirEntry->Time & FAT_TIME_MINUTE_MASK) >> FAT_TIME_MINUTE_SHFT;
    File->Header.Modified.Second = ((DirEntry->Time & FAT_TIME_SECOND_MASK) >> FAT_TIME_SECOND_SHFT) * 2;
    File->Header.Modified.Milli = 0;
}

/***************************************************************************/

/**
 * @brief Locate a file/directory by path within a FAT16 filesystem.
 *
 * @param FileSystem FAT16 filesystem context.
 * @param Path Absolute path to search.
 * @param FileLoc Output location metadata when found.
 * @return TRUE if found, FALSE otherwise.
 */
static BOOL LocateFile(LPFAT16FILESYSTEM FileSystem, LPCSTR Path, LPFATFILELOC FileLoc) {
    STR Component[MAX_FILE_NAME];
    STR Name[MAX_FILE_NAME];
    LPFATDIRENTRY DirEntry;
    U32 PathIndex = 0;
    U32 CompIndex = 0;

    FileLoc->PreviousCluster = 0;
    FileLoc->FolderCluster = 1;
    FileLoc->FileCluster = FileLoc->FolderCluster;
    FileLoc->Offset = 0;
    FileLoc->DataCluster = 0;

    //-------------------------------------
    // Read the root cluster

    if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer)) {
        return FALSE;
    }

    FOREVER {
        //-------------------------------------
        // Parse the next component to look for

    NextComponent:

        CompIndex = 0;

        FOREVER {
            if (Path[PathIndex] == STR_SLASH) {
                Component[CompIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (Path[PathIndex] == STR_NULL) {
                Component[CompIndex] = STR_NULL;
                break;
            } else {
                Component[CompIndex++] = Path[PathIndex++];
            }
        }

        //-------------------------------------
        // Loop through all directory entries

        FOREVER {
            DirEntry = (LPFATDIRENTRY)(FileSystem->IOBuffer + FileLoc->Offset);

            if ((DirEntry->Cluster) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 && (DirEntry->Name[0] != 0xE5)) {
                DecodeFileName(DirEntry, Name);

                if (StringCompare(Component, TEXT("*")) == 0 || STRINGS_EQUAL(Component, Name)) {
                    if (Path[PathIndex] == STR_NULL) {
                        FileLoc->DataCluster = DirEntry->Cluster;

                        return TRUE;
                    } else {
                        if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
                            FileLoc->FolderCluster = DirEntry->Cluster;
                            FileLoc->FileCluster = FileLoc->FolderCluster;
                            FileLoc->Offset = 0;

                            if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE)
                                return FALSE;

                            goto NextComponent;
                        } else {
                            return FALSE;
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc->Offset += sizeof(FATDIRENTRY);

            if (FileLoc->Offset >= FileSystem->BytesPerCluster) {
                FileLoc->Offset = 0;
                FileLoc->FileCluster = GetNextClusterInChain(FileSystem, FileLoc->FileCluster);

                if (FileLoc->FileCluster == 0 || FileLoc->FileCluster >= FAT16_CLUSTER_RESERVED) return FALSE;

                if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Initialize FAT16 driver (stub).
 *
 * @return DF_RETURN_SUCCESS
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/***************************************************************************/

/**
 * @brief Open a file given file info from VFS.
 *
 * @param Find File info containing path.
 * @return Allocated FAT file or NULL on failure.
 */
static LPFATFILE OpenFile(LPFILE_INFO Find) {
    LPFAT16FILESYSTEM FileSystem = NULL;
    LPFATFILE File = NULL;
    LPFATDIRENTRY DirEntry = NULL;
    FATFILELOC FileLoc;

    //-------------------------------------
    // Check validity of parameters

    if (Find == NULL) return NULL;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT16FILESYSTEM)Find->FileSystem;

    if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE) {
        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;

        DirEntry = (LPFATDIRENTRY)(FileSystem->IOBuffer + FileLoc.Offset);

        File = NewFATFile(FileSystem, &FileLoc);
        if (File == NULL) return NULL;

        File->DirectoryBufferCluster = FileLoc.FileCluster;
        File->DirectoryBufferGeneration = FileSystem->IOBufferGeneration;
        File->DirectoryBufferValid = TRUE;

        DecodeFileName(DirEntry, File->Header.Name);
        TranslateFileInfo(DirEntry, File);
    }

    return File;
}

/***************************************************************************/

/**
 * @brief Open the next file in a directory using stored location.
 *
 * @param File FAT file whose Location indicates folder and offset.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 OpenNext(LPFATFILE File) {
    LPFAT16FILESYSTEM FileSystem = NULL;
    LPFATDIRENTRY DirEntry = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT16FILESYSTEM)File->Header.FileSystem;

    if (!File->DirectoryBufferValid || File->DirectoryBufferCluster != File->Location.FileCluster ||
        File->DirectoryBufferGeneration != FileSystem->IOBufferGeneration) {
        if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE)
            return DF_RETURN_INPUT_OUTPUT;
    }

    FOREVER {
        File->Location.Offset += sizeof(FATDIRENTRY);

        if (File->Location.Offset >= FileSystem->BytesPerCluster) {
            File->Location.Offset = 0;

            File->Location.FileCluster = GetNextClusterInChain(FileSystem, File->Location.FileCluster);

            if (File->Location.FileCluster == 0 || File->Location.FileCluster >= FAT16_CLUSTER_RESERVED)
                return DF_RETURN_GENERIC;

            if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE)
                return DF_RETURN_INPUT_OUTPUT;
        }

        DirEntry = (LPFATDIRENTRY)(FileSystem->IOBuffer + File->Location.Offset);

        if ((DirEntry->Cluster) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 && (DirEntry->Name[0] != 0xE5)) {
            File->Location.DataCluster = DirEntry->Cluster;
            DecodeFileName(DirEntry, File->Header.Name);
            TranslateFileInfo(DirEntry, File);
            File->DirectoryBufferCluster = File->Location.FileCluster;
            File->DirectoryBufferGeneration = FileSystem->IOBufferGeneration;
            File->DirectoryBufferValid = TRUE;
            break;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Close a FAT16 file handle.
 *
 * @param File Target file.
 * @return DF_RETURN_SUCCESS.
 */
static U32 CloseFile(LPFATFILE File) {
    // LPFAT16FILESYSTEM FileSystem;
    // LPFATDIRENTRY DirEntry;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    // FileSystem = (LPFAT16FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Update file information in directory entry

    /*
      if (ReadCluster(FileSystem, File->Location.FileCluster,
      FileSystem->IOBuffer) == FALSE)
      {
    return DF_RETURN_INPUT_OUTPUT;
      }

      DirEntry = (LPFATDIRENTRY) (FileSystem->IOBuffer + File->Location.Offset);

      if (File->Header.SizeLow > DirEntry->Size)
      {
    DirEntry->Size = File->Header.SizeLow;

    if (WriteCluster(FileSystem, File->Location.FileCluster,
      FileSystem->IOBuffer) == FALSE)
    {
      return DF_RETURN_INPUT_OUTPUT;
    }
      }
    */

    ReleaseKernelObject(File);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read bytes from a FAT16 file.
 *
 * @param File Target file with buffer/positions populated.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 ReadFile(LPFATFILE File) {
    LPFAT16FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT16FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    Cluster = File->Location.DataCluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);
        if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
            return DF_RETURN_INPUT_OUTPUT;
        }
    }

    FOREVER {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
        if (ByteCount > BytesRemaining) ByteCount = BytesRemaining;

        //-------------------------------------
        // Copy the data to the user buffer

        MemoryCopy(
            ((U8*)File->Header.Buffer) + File->Header.BytesTransferred, FileSystem->IOBuffer + OffsetInCluster,
            ByteCount);

        //-------------------------------------
        // Update counters

        OffsetInCluster = 0;
        BytesRemaining -= ByteCount;
        File->Header.BytesTransferred += ByteCount;
        File->Header.Position += ByteCount;

        //-------------------------------------
        // Check if we read all data

        if (BytesRemaining == 0) break;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
            break;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Chain a new cluster to extend a file.
 * @param FileSystem File system handle.
 * @param Cluster Last cluster to chain to.
 * @return New cluster number or 0 on failure.
 */
static CLUSTER ChainNewCluster(LPFAT16FILESYSTEM FileSystem, CLUSTER Cluster) {
    U16 Buffer[SECTOR_SIZE / sizeof(U16)];
    IOCONTROL Control;
    U32 Result;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 CurrentOffset;
    U32 CurrentFAT;
    U32 FATStart;
    CLUSTER NewCluster;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U16);
    CurrentSector = 0;
    NewCluster = 0;
    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    FOREVER {
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        for (CurrentOffset = 0; CurrentOffset < NumEntriesPerSector; CurrentOffset++) {
            if (Buffer[CurrentOffset] == 0) goto Next;
        }

        CurrentSector++;

        if (CurrentSector >= FileSystem->Master.SectorsPerFAT) break;
    }

    return NewCluster;

Next:

    NewCluster = (CurrentSector * NumEntriesPerSector) + CurrentOffset;
    CurrentSector = Cluster / NumEntriesPerSector;
    CurrentOffset = Cluster % NumEntriesPerSector;

    FATStart = FileSystem->FATStart;

    for (CurrentFAT = 0; CurrentFAT < FileSystem->Master.NumFATs; CurrentFAT++) {
        Control.SectorLow = FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        Buffer[CurrentOffset] = NewCluster;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (UINT)&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        FATStart += FileSystem->Master.SectorsPerFAT;
    }

    return NewCluster;
}

/***************************************************************************/

/**
 * @brief Write data to a file.
 * @param File File handle with write parameters.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 WriteFile(LPFATFILE File) {
    LPFAT16FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    CLUSTER LastValidCluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT16FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    if (ByteCount > BytesRemaining) {
        ByteCount = BytesRemaining;
    }

    Cluster = File->Location.DataCluster;
    LastValidCluster = Cluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
                return DF_RETURN_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    while (BytesRemaining > 0) {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        //-------------------------------------
        // Copy the user buffer

        U32 BytesToTransfer = ByteCount;

        if (BytesToTransfer > BytesRemaining) {
            BytesToTransfer = BytesRemaining;
        }

        MemoryCopy(
            FileSystem->IOBuffer + OffsetInCluster, ((U8*)File->Header.Buffer) + File->Header.BytesTransferred,
            BytesToTransfer);

        //-------------------------------------
        // Write the current data cluster

        if (WriteCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        //-------------------------------------
        // Update counters

        File->Header.BytesTransferred += BytesToTransfer;
        File->Header.Position += BytesToTransfer;
        BytesRemaining -= BytesToTransfer;

        if (BytesRemaining == 0) {
            break;
        }

        OffsetInCluster = 0;
        ByteCount = FileSystem->BytesPerCluster;

        if (ByteCount > BytesRemaining) {
            ByteCount = BytesRemaining;
        }

        LastValidCluster = Cluster;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT16_CLUSTER_RESERVED) {
                return DF_RETURN_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    if (File->Header.Position > File->Header.SizeLow) {
        File->Header.SizeLow = File->Header.Position;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief FAT16 driver command dispatcher.
 *
 * Routes filesystem operations to FAT16 handlers.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver-specific status/value.
 */
UINT FAT16Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_SETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_CREATEFOLDER:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_DELETEFOLDER:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_RENAMEFOLDER:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_OPENFILE:
            return (UINT)OpenFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENNEXT:
            return (UINT)OpenNext((LPFATFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (UINT)CloseFile((LPFATFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_RENAMEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_READ:
            return (UINT)ReadFile((LPFATFILE)Parameter);
        case DF_FS_WRITE:
            return (UINT)WriteFile((LPFATFILE)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
