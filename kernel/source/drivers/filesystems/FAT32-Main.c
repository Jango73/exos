
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


    FAT32

\************************************************************************/

#include "drivers/filesystems/FAT32-Private.h"
#include "drivers/filesystems/FileSystem-Common.h"
#include "fs/Storage-Transfer-Layer.h"

DRIVER DATA_SECTION FAT32Driver = { .TypeID = KOID_DRIVER,
                                    .References = 1,
                                    .OwnerProcess = &KernelProcess,
                                    .Next = NULL,
                                    .Prev = NULL,
                                    .Type = DRIVER_TYPE_FILESYSTEM,
                                    .VersionMajor = VER_MAJOR,
                                    .VersionMinor = VER_MINOR,
                                    .Designer = "Jango73",
                                    .Manufacturer = "Microsoft Corporation",
                                    .Product = "Fat 32 File System",
                                    .Alias = "fat32",
                                    .Command = FAT32Commands };

/**
 * @brief Allocate and initialize a FAT32 file system object.
 * @param Storage Physical storage hosting the partition.
 * @return Pointer to a new FAT32 file system or NULL on failure.
 */
static LPFAT32FILESYSTEM NewFATFileSystem(LPSTORAGE_UNIT Storage) {
    LPFAT32FILESYSTEM This;

    This = (LPFAT32FILESYSTEM)KernelHeapAlloc(sizeof(FAT32_FILE_SYSTEM));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FAT32_FILE_SYSTEM));

    This->Header.TypeID = KOID_FILESYSTEM;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &FAT32Driver;
    This->Header.StorageUnit = Storage;
    This->Storage = Storage;
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
 * @brief Allocate and initialize a FAT32 file handle.
 * @param FileSystem Owning file system.
 * @param FileLoc Initial file location information.
 * @return Pointer to a new FAT_FILE or NULL on failure.
 */
LPFATFILE NewFATFile(LPFAT32FILESYSTEM FileSystem, LPFATFILELOC FileLoc) {
    LPFATFILE This;

    This = (LPFATFILE)KernelHeapAlloc(sizeof(FAT_FILE));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FAT_FILE));

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

/**
 * @brief Mount a FAT32 partition and register the file system.
 * @param Storage Physical storage containing the partition.
 * @param Partition Partition descriptor.
 * @param Base Base sector offset.
 * @param PartIndex Partition index for naming.
 * @return TRUE on success, FALSE on failure.
 */
BOOL MountPartition_FAT32(LPSTORAGE_UNIT Storage, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE];
    LPFAT32MBR Master;
    LPFAT32FILESYSTEM FileSystem;
    BOOL Success;


    Success = FATReadBootSector(Storage, Partition, Base, (LPVOID)Buffer);
    if (Success == FALSE) return FALSE;

    //-------------------------------------
    // Assign a pointer to the sector

    Master = (LPFAT32MBR)Buffer;

    //-------------------------------------
    // Check if this is really a FAT32 partition

    if (Master->FATName[0] != 'F') return FALSE;
    if (Master->FATName[1] != 'A') return FALSE;
    if (Master->FATName[2] != 'T') return FALSE;
    if (Master->FATName[3] != '3') return FALSE;
    if (Master->FATName[4] != '2') return FALSE;

    //-------------------------------------
    // Create the file system object

    FileSystem = NewFATFileSystem(Storage);
    if (FileSystem == NULL) return FALSE;

    GetDefaultFileSystemName(FileSystem->Header.Name, Storage, PartIndex);

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
        FileSystem->FATStart2 = FileSystem->FATStart + FileSystem->Master.NumSectorsPerFAT;
    }

    //-------------------------------------
    // Compute the start of the data

    FileSystem->DataStart = FileSystem->FATStart + (FileSystem->Master.NumFATs * FileSystem->Master.NumSectorsPerFAT);

    //-------------------------------------
    // Update global information and register the file system

    ListAddItem(GetFileSystemList(), FileSystem);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute FAT32 short name checksum.
 * @param Name 11-character short name.
 * @return Calculated checksum value.
 */
U32 GetNameChecksum(LPSTR Name) {
    U32 Checksum = 0;
    U32 Index = 0;

    for (Index = 0; Index < 11; Index++) {
        Checksum = (((Checksum & 0x01) << 7) | ((Checksum & 0xFE) >> 1)) + Name[Index];
    }

    Checksum &= 0xFF;

    return Checksum;
}

/***************************************************************************/

/**
 * @brief Submit one FAT32 cluster transfer.
 * @param FileSystem Target file system.
 * @param Cluster Cluster number to transfer.
 * @param Buffer Transfer buffer.
 * @param Command Storage command to execute.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL FAT32TransferCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer, UINT Command) {
    SECTOR Sector;

    Sector =
        FileSystem->DataStart + ((Cluster - FileSystem->Master.RootCluster) * FileSystem->Master.SectorsPerCluster);

    if (PartitionTransferSectors(
            FileSystem->Storage,
            FileSystem->PartitionStart,
            FileSystem->PartitionSize,
            Sector,
            FileSystem->Master.SectorsPerCluster,
            Buffer,
            FileSystem->Master.SectorsPerCluster * SECTOR_SIZE,
            Command) == FALSE) {
        return FALSE;
    }

    FileSystem->IOBufferGeneration++;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read a cluster from storage into memory.
 * @param FileSystem Target file system.
 * @param Cluster Cluster number to read.
 * @param Buffer Destination buffer.
 * @return TRUE on success, FALSE on failure.
 */
BOOL ReadCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    return FAT32TransferCluster(FileSystem, Cluster, Buffer, DF_STORAGE_READ);
}

/***************************************************************************/

/**
 * @brief Write a memory buffer to a specific cluster.
 * @param FileSystem Target file system.
 * @param Cluster Cluster number to write.
 * @param Buffer Source buffer.
 * @return TRUE on success, FALSE on failure.
 */
BOOL WriteCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    return FAT32TransferCluster(FileSystem, Cluster, Buffer, DF_STORAGE_WRITE);
}

/***************************************************************************/

/**
 * @brief Retrieve the next cluster in a FAT chain.
 * @param FileSystem Target file system.
 * @param Cluster Current cluster in chain.
 * @return Next cluster number or 0 on failure.
 */
CLUSTER GetNextClusterInChain(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    CLUSTER NextCluster;
    U32 NumEntriesPerSector;
    U32 Sector;
    U32 Offset;
    U32 Result;

    NextCluster = FAT32_CLUSTER_LAST;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    Sector = Cluster / NumEntriesPerSector;
    Offset = Cluster % NumEntriesPerSector;

    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = FileSystem->Storage;
    Control.SectorLow = FileSystem->FATStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = StorageTransferLayerRead(&Control);

    if (Result == DF_RETURN_SUCCESS) {
        NextCluster = Buffer[Offset];
    }

    return NextCluster;
}

/***************************************************************************/

/**
 * @brief Search the FAT for a free cluster.
 * @param FileSystem Target file system.
 * @return Cluster number or 0 if none available.
 */
static CLUSTER FindFreeCluster(LPFAT32FILESYSTEM FileSystem) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    CLUSTER NewCluster;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 Index;
    U32 Result;


    //-------------------------------------
    // Setup variables

    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = FileSystem->Storage;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;
    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;


    FOREVER {
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = StorageTransferLayerRead(&Control);
        if (Result != DF_RETURN_SUCCESS) {
            goto Out;
        }

        for (Index = 0; Index < NumEntriesPerSector; Index++) {
            if (Buffer[Index] == FAT32_CLUSTER_AVAIL) {
                //-------------------------------------
                // Mark the cluster as used

                Buffer[Index] = FAT32_CLUSTER_LAST;

                //-------------------------------------
                // Write the FAT sector

                Control.SectorLow = FileSystem->FATStart + CurrentSector;
                Control.SectorHigh = 0;
                Control.NumSectors = 1;
                Result = StorageTransferLayerWrite(&Control);
                if (Result != DF_RETURN_SUCCESS) {
                    goto Out;
                }

                NewCluster = (CurrentSector * NumEntriesPerSector) + Index;

                if (FileSystem->Master.NumFATs > 1) {
                    //-------------------------------------
                    // Read the FAT sector

                    Control.SectorLow = FileSystem->FATStart2 + CurrentSector;
                    Control.SectorHigh = 0;
                    Control.NumSectors = 1;
                    Result = StorageTransferLayerRead(&Control);
                    if (Result != DF_RETURN_SUCCESS) {
                        goto Out;
                    }

                    //-------------------------------------
                    // Mark the cluster as used

                    Buffer[Index] = FAT32_CLUSTER_LAST;

                    //-------------------------------------
                    // Write the FAT sector

                    Control.SectorLow = FileSystem->FATStart2 + CurrentSector;
                    Control.SectorHigh = 0;
                    Control.NumSectors = 1;
                    Result = StorageTransferLayerWrite(&Control);
                    if (Result != DF_RETURN_SUCCESS) {
                        goto Out;
                    }
                }

                return NewCluster;
            }
        }

        CurrentSector++;
        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) {
            break;
        }
    }

Out:

    return 0;
}

/***************************************************************************/

/*
static BOOL FindFreeFATEntry(LPFAT32FILESYSTEM FileSystem, U32* Sector,
                             U32* Offset) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 CurrentOffset;
    U32 Result;

    *Sector = 0;
    *Offset = 0;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;

    FOREVER {
        Control.TypeID = KOID_IOCONTROL;
        Control.Storage = FileSystem->Storage;
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;
        Control.Buffer = Buffer;
        Control.BufferSize = SECTOR_SIZE;

        Result = StorageTransferLayerRead(&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return FALSE;
        }

        for (CurrentOffset = 0; CurrentOffset < NumEntriesPerSector;
             CurrentOffset++) {
            if (Buffer[CurrentOffset] == FAT32_CLUSTER_AVAIL) {
                *Sector = CurrentSector;
                *Offset = CurrentOffset * sizeof(U32);
                return TRUE;
            }
        }

        CurrentSector++;
        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) break;
    }

    return FALSE;
}
*/

/***************************************************************************/

/**
 * @brief Populate a directory entry in a buffer.
 * @param Buffer Directory sector buffer.
 * @param Name 8.3 formatted name.
 * @param Cluster Starting cluster of file.
 * @param Attributes Attribute flags.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL SetDirEntry(LPVOID Buffer, LPSTR Name, CLUSTER Cluster, U32 Attributes) {
    LPFATDIRENTRY_EXT DirEntry = NULL;
    LPFATDIRENTRY_LFN LFNEntry = NULL;
    STR ShortName[16];
    U32 Checksum = 0;
    U32 Ordinal = 0;
    U32 Index = 0;
    U32 Length = 0;
    U32 NumEntries = 0;

    Length = StringLength(Name);
    if (Length > 255) return FALSE;

    //-------------------------------------
    // Compute the number of entries required

    NumEntries = ((Length + 1) / 13) + 1;
    NumEntries++;

    //-------------------------------------
    // Create the short name

    for (Index = 0; Index < 6; Index++) {
        if (Name[Index] == '\0') break;
        ShortName[Index] = Name[Index];
    }

    ShortName[Index++] = '~';
    ShortName[Index++] = '1';

    for (; Index < 11; Index++) {
        ShortName[Index] = STR_SPACE;
    }

    //-------------------------------------
    // Compute checksum

    Checksum = GetNameChecksum(ShortName);

    //-------------------------------------
    // Fill the directory entry

    DirEntry = (LPFATDIRENTRY_EXT)(Buffer + ((NumEntries - 1) * sizeof(FAT_DIR_ENTRY_EXT)));

    DirEntry->Name[0] = ShortName[0];
    DirEntry->Name[1] = ShortName[1];
    DirEntry->Name[2] = ShortName[2];
    DirEntry->Name[3] = ShortName[3];
    DirEntry->Name[4] = ShortName[4];
    DirEntry->Name[5] = ShortName[5];
    DirEntry->Name[6] = ShortName[6];
    DirEntry->Name[7] = ShortName[7];

    DirEntry->Ext[0] = ShortName[8];
    DirEntry->Ext[1] = ShortName[9];
    DirEntry->Ext[2] = ShortName[10];

    DirEntry->Attributes = Attributes;
    DirEntry->NT = 0;
    DirEntry->CreationMS = 0;
    DirEntry->CreationHM = 0;
    DirEntry->CreationYM = 0;
    DirEntry->LastAccessDate = 0;
    DirEntry->ClusterHigh = (Cluster & 0xFFFF0000) >> 16;
    DirEntry->Time = 0;
    DirEntry->Date = 0;
    DirEntry->ClusterLow = (Cluster & 0xFFFF);
    DirEntry->Size = 0;

    //-------------------------------------
    // Store the long name

    LFNEntry = (LPFATDIRENTRY_LFN)DirEntry;

    Index = 0;
    Ordinal = 1;

    FOREVER {
        LFNEntry--;

        LFNEntry->Ordinal = Ordinal++;
        LFNEntry->Checksum = Checksum;
        LFNEntry->Attributes = FAT_ATTR_LFN;

        if (Name[Index])
            LFNEntry->Char01 = (USTR)Name[Index++];
        else {
            LFNEntry->Char01 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char02 = (USTR)Name[Index++];
        else {
            LFNEntry->Char02 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char03 = (USTR)Name[Index++];
        else {
            LFNEntry->Char03 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char04 = (USTR)Name[Index++];
        else {
            LFNEntry->Char04 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char05 = (USTR)Name[Index++];
        else {
            LFNEntry->Char05 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char06 = (USTR)Name[Index++];
        else {
            LFNEntry->Char06 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char07 = (USTR)Name[Index++];
        else {
            LFNEntry->Char07 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char08 = (USTR)Name[Index++];
        else {
            LFNEntry->Char08 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char09 = (USTR)Name[Index++];
        else {
            LFNEntry->Char09 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char10 = (USTR)Name[Index++];
        else {
            LFNEntry->Char10 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char11 = (USTR)Name[Index++];
        else {
            LFNEntry->Char11 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char12 = (USTR)Name[Index++];
        else {
            LFNEntry->Char12 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char13 = (USTR)Name[Index++];
        else {
            LFNEntry->Char13 = (USTR)'\0';
            break;
        }
    }

    LFNEntry->Ordinal |= BIT_6;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Create a directory entry for a file or folder.
 * @param FileSystem Target file system.
 * @param FolderCluster Cluster of parent folder.
 * @param Name 8.3 formatted name.
 * @param Attributes Attribute flags.
 * @return TRUE on success, FALSE on failure.
 */
BOOL CreateDirEntry(LPFAT32FILESYSTEM FileSystem, CLUSTER FolderCluster, LPSTR Name, U32 Attributes) {
    LPFATDIRENTRY_EXT DirEntry;
    LPFATDIRENTRY_EXT BaseEntry;
    CLUSTER CurrentCluster;
    CLUSTER NewCluster;
    U32 CurrentOffset;
    U32 Length;
    U32 RequiredEntries;
    U32 FreeEntries;


    Length = StringLength(Name);
    if (Length > 255) {
        return FALSE;
    }

    //-------------------------------------
    // Compute the number of entries required

    RequiredEntries = ((Length + 1) / 13) + 1;
    RequiredEntries++;

    //-------------------------------------
    // First we try to find some space in the
    // existing directory entries

    CurrentCluster = FolderCluster;

    FOREVER {
        if (!ReadCluster(FileSystem, CurrentCluster, FileSystem->IOBuffer)) {
            return FALSE;
        }

        BaseEntry = NULL;
        FreeEntries = 0;
        CurrentOffset = 0;

        FOREVER {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + CurrentOffset);

            if (DirEntry->Name[0] == 0 && DirEntry->Name[1] == 0) {
                if (BaseEntry == NULL) {
                    BaseEntry = DirEntry;
                }
                FreeEntries++;
            } else {
                if (FreeEntries > 0) {
                }
                BaseEntry = NULL;
                FreeEntries = 0;
            }

            if (FreeEntries == RequiredEntries) {
                NewCluster = FindFreeCluster(FileSystem);

                if (NewCluster == 0) {
                    return FALSE;
                }

                SetDirEntry(BaseEntry, Name, NewCluster, Attributes);

                if (!WriteCluster(FileSystem, CurrentCluster, FileSystem->IOBuffer)) {
                    return FALSE;
                }

                return TRUE;
            }

            CurrentOffset += sizeof(FAT_DIR_ENTRY_EXT);

            if (CurrentOffset >= FileSystem->BytesPerCluster) {
                BaseEntry = NULL;
                FreeEntries = 0;
                CurrentOffset = 0;
                CurrentCluster = GetNextClusterInChain(FileSystem, CurrentCluster);
                break;
            }
        }

        if (CurrentCluster == 0 || CurrentCluster >= FAT32_CLUSTER_RESERVED) {
            break;
        }
    }

    // FileLoc.FileCluster = ChainNewCluster(FileSystem, LastValidCluster);

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Append a new cluster to an existing chain.
 * @param FileSystem Target file system.
 * @param Cluster Current last cluster in chain.
 * @return Number of new cluster or 0 on failure.
 */
CLUSTER ChainNewCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    U32 Result;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 CurrentOffset;
    U32 CurrentFAT;
    U32 FATStart;
    CLUSTER NewCluster;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;
    NewCluster = 0;
    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = FileSystem->Storage;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    FOREVER {
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = StorageTransferLayerRead(&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        for (CurrentOffset = 0; CurrentOffset < NumEntriesPerSector; CurrentOffset++) {
            if (Buffer[CurrentOffset] == 0) goto Next;
        }

        CurrentSector++;

        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) break;
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

        Result = StorageTransferLayerRead(&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        Buffer[CurrentOffset] = NewCluster;

        Result = StorageTransferLayerWrite(&Control);

        if (Result != DF_RETURN_SUCCESS) {
            return NewCluster;
        }

        FATStart += FileSystem->Master.NumSectorsPerFAT;
    }

    return NewCluster;
}

/***************************************************************************/
