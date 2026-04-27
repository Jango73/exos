
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


    EXFS

\************************************************************************/
#include "drivers/filesystems/EXFS.h"

#include "core/Kernel.h"
#include "drivers/filesystems/FileSystem-Common.h"
#include "fs/File-System.h"
#include "log/Log.h"
#include "utils/Path.h"

/************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT EXFSCommands(UINT, UINT);

DRIVER DATA_SECTION EXFSDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Jango73",
    .Product = "EXOS File System",
    .Alias = "exfs",
    .Command = EXFSCommands};

/************************************************************************/

/**
 * @brief Retrieves the EXFS driver descriptor.
 * @return Pointer to the EXFS driver.
 */
LPDRIVER EXFSGetDriver(void) { return &EXFSDriver; }

U8 Dummy[128] = {1, 1};

/************************************************************************/
// The file system object allocated when mounting

typedef struct tag_EXFSFILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    EXFSMBR Master;
    EXFSSUPER Super;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BytesPerCluster;
    SECTOR DataStart;
    U8* PageBuffer;
    U8* IOBuffer;
} EXFSFILESYSTEM, *LPEXFSFILESYSTEM;

/************************************************************************/

typedef struct tag_EXFSFILE {
    FILE Header;
    EXFSFILELOC Location;
} EXFSFILE, *LPEXFSFILE;

/************************************************************************/

/**
 * @brief Allocate and initialize a new EXFS file system object.
 * @param Disk Physical disk associated with the file system.
 * @return Pointer to the created file system or NULL.
 */
static LPEXFSFILESYSTEM NewEXFSFileSystem(LPSTORAGE_UNIT Disk) {
    LPEXFSFILESYSTEM This;

    This = (LPEXFSFILESYSTEM)KernelHeapAlloc(sizeof(EXFSFILESYSTEM));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(EXFSFILESYSTEM));

    This->Header.TypeID = KOID_FILESYSTEM;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &EXFSDriver;
    This->Header.StorageUnit = Disk;
    This->Disk = Disk;
    This->PageBuffer = NULL;
    This->IOBuffer = NULL;

    InitMutex(&(This->Header.Mutex));

    return This;
}

/************************************************************************/

/**
 * @brief Create a new EXFS file object for a given location.
 * @param FileSystem File system handle.
 * @param FileLoc Location information for the file.
 * @return Pointer to the created file or NULL.
 */
static LPEXFSFILE NewEXFSFile(LPEXFSFILESYSTEM FileSystem, LPEXFSFILELOC FileLoc) {
    LPEXFSFILE This;

    This = (LPEXFSFILE)KernelHeapAlloc(sizeof(EXFSFILE));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(EXFSFILE));

    This->Header.TypeID = KOID_FILE;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.FileSystem = (LPFILESYSTEM)FileSystem;
    This->Location.PageCluster = FileLoc->PageCluster;
    This->Location.PageOffset = FileLoc->PageOffset;
    This->Location.FileCluster = FileLoc->FileCluster;
    This->Location.FileOffset = FileLoc->FileOffset;
    This->Location.DataCluster = FileLoc->DataCluster;

    InitMutex(&(This->Header.Mutex));
    InitSecurity(&(This->Header.Security));

    return This;
}

/************************************************************************/

/**
 * @brief Mount an EXFS partition found on a physical disk.
 * @param Disk Physical disk.
 * @param Partition Partition descriptor.
 * @param Base Base LBA offset.
 * @param PartIndex Partition index.
 * @return TRUE on success.
 */
BOOL MountPartition_EXFS(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer1[SECTOR_SIZE * 2];
    U8 Buffer2[SECTOR_SIZE * 2];
    IOCONTROL Control;
    LPEXFSMBR Master = NULL;
    LPEXFSSUPER Super = NULL;
    LPEXFSFILESYSTEM FileSystem = NULL;
    U32 Result = 0;

    //-------------------------------------
    // Read the Master Boot Record

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Base + Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)Buffer1;
    Control.BufferSize = SECTOR_SIZE * 2;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    //-------------------------------------
    // Read the Superblock

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = (Base + Partition->LBA) + 2;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)Buffer2;
    Control.BufferSize = SECTOR_SIZE * 2;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    //-------------------------------------
    // Assign pointers

    Master = (LPEXFSMBR)Buffer1;
    Super = (LPEXFSSUPER)Buffer2;

    //-------------------------------------
    // Check for presence of BIOS mark

    if (Master->BIOSMark != 0xAA55) return FALSE;

    //-------------------------------------
    // Check if this is really an EXOS partition

    if (Master->OEMName[0] != 'E') return FALSE;
    if (Master->OEMName[1] != 'X') return FALSE;
    if (Master->OEMName[2] != 'O') return FALSE;
    if (Master->OEMName[3] != 'S') return FALSE;

    if (Super->Magic[0] != 'E') return FALSE;
    if (Super->Magic[1] != 'X') return FALSE;
    if (Super->Magic[2] != 'O') return FALSE;
    if (Super->Magic[3] != 'S') return FALSE;

    //-------------------------------------
    // Create the file system object

    FileSystem = NewEXFSFileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    //-------------------------------------
    // Copy the Master Boot Sector and the Superblock

    MemoryCopy(&(FileSystem->Master), Master, sizeof(EXFSMBR));
    MemoryCopy(&(FileSystem->Super), Super, sizeof(EXFSSUPER));

    FileSystem->PartitionStart = Base + Partition->LBA;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BytesPerCluster = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    FileSystem->PageBuffer = (U8*)KernelHeapAlloc(FileSystem->Master.SectorsPerCluster * SECTOR_SIZE);

    FileSystem->IOBuffer = (U8*)KernelHeapAlloc(FileSystem->Master.SectorsPerCluster * SECTOR_SIZE);

    //-------------------------------------
    // Compute the start of the data

    FileSystem->DataStart = FileSystem->PartitionStart + (SECTOR_SIZE * 4);

    //-------------------------------------
    // Update global information and register the file system

    ListAddItem(GetFileSystemList(), FileSystem);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read a cluster from disk into a buffer.
 * @param FileSystem Target file system.
 * @param Cluster Cluster index to read.
 * @param Buffer Destination buffer.
 * @return TRUE on success.
 */
static BOOL ReadCluster(LPEXFSFILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    SECTOR Sector;

    Sector = FileSystem->DataStart + (Cluster * FileSystem->Master.SectorsPerCluster);

    return PartitionTransferSectors(
        FileSystem->Disk, FileSystem->PartitionStart, FileSystem->PartitionSize, Sector,
        FileSystem->Master.SectorsPerCluster, Buffer, FileSystem->Master.SectorsPerCluster * SECTOR_SIZE, DF_DISK_READ);
}

/***************************************************************************/

/*
static BOOL WriteCluster(LPEXFSFILESYSTEM FileSystem, CLUSTER Cluster,
                         LPVOID Buffer) {
    IOCONTROL Control;
    SECTOR Sector;
    U32 Result;

    Sector = FileSystem->DataStart +
             (Cluster * FileSystem->Master.SectorsPerCluster);

    if (Sector < FileSystem->PartitionStart ||
        Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize) {
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = FileSystem->Master.SectorsPerCluster;
    Control.Buffer = Buffer;
    Control.BufferSize = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    return TRUE;
}
*/

/***************************************************************************/

#define GET_PAGE_ENTRY() (*((U32*)(FileSystem->PageBuffer + FileLoc->PageOffset)))

/**
 * @brief Locate a file by path on the EXFS file system.
 * @param FileSystem File system to search.
 * @param Path Path to the file.
 * @param FileLoc Output location information.
 * @return TRUE if file found.
 */
static BOOL LocateFile(LPEXFSFILESYSTEM FileSystem, LPCSTR Path, LPEXFSFILELOC FileLoc) {
    LPLIST List = NULL;
    LPPATH_NODE Component = NULL;
    LPEXFSFILEREC FileRec;

    FileLoc->PageCluster = FileSystem->Super.RootCluster;
    FileLoc->PageOffset = 0;
    FileLoc->FileCluster = 0;
    FileLoc->FileOffset = 0;
    FileLoc->DataCluster = 0;

    //-------------------------------------
    // Read the root page

    if (!ReadCluster(FileSystem, FileLoc->PageCluster, FileSystem->PageBuffer)) {
        return FALSE;
    }

    FileLoc->FileCluster = GET_PAGE_ENTRY();

    if (FileLoc->FileCluster == EXFS_CLUSTER_END) return FALSE;

    if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer)) {
        return FALSE;
    }

    //-------------------------------------
    // Decompose the path

    List = DecomposePath(Path);

    if (List == NULL) return FALSE;

    //-------------------------------------
    // Loop through all components

    for (Component = (LPPATH_NODE)List->First; Component != NULL; Component = (LPPATH_NODE)Component->Next) {
        //-------------------------------------
        // Loop through all directory entries

        FOREVER {
            FileRec = (LPEXFSFILEREC)(FileSystem->IOBuffer + FileLoc->FileOffset);

            if (FileRec->ClusterTable == EXFS_CLUSTER_END) {
                goto Out_Error;
            }

            if (FileRec->ClusterTable > 0 && FileRec->ClusterTable != EXFS_CLUSTER_END) {
                if (StringCompare(Component->Name, TEXT("*")) == 0 || STRINGS_EQUAL(Component->Name, FileRec->Name)) {
                    if (Component->Next == NULL) {
                        FileLoc->DataCluster = FileRec->ClusterTable;
                        goto Out_Success;
                    } else {
                        if (FileRec->Attributes & EXFS_ATTR_FOLDER) {
                            FileLoc->PageCluster = FileRec->ClusterTable;
                            FileLoc->PageOffset = 0;
                            FileLoc->FileCluster = 0;
                            FileLoc->FileOffset = 0;

                            if (ReadCluster(FileSystem, FileLoc->PageCluster, FileSystem->PageBuffer) == FALSE)
                                goto Out_Error;

                            FileLoc->FileCluster = GET_PAGE_ENTRY();

                            if (FileLoc->FileCluster == EXFS_CLUSTER_END) goto Out_Error;

                            if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE)
                                goto Out_Error;

                            goto NextComponent;
                        } else {
                            goto Out_Error;
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc->FileOffset += sizeof(EXFSFILEREC);

            if (FileLoc->FileOffset >= FileSystem->BytesPerCluster) {
                FileLoc->PageOffset += sizeof(U32);

                //-------------------------------------
                // If we are at the last page entry, check if there is
                // another page

                if (FileLoc->PageOffset == (FileSystem->BytesPerCluster - sizeof(U32))) {
                    FileLoc->PageCluster = GET_PAGE_ENTRY();
                    FileLoc->PageOffset = 0;

                    if (FileLoc->PageCluster == EXFS_CLUSTER_END) goto Out_Error;

                    if (!ReadCluster(FileSystem, FileLoc->PageCluster, FileSystem->PageBuffer)) {
                        return FALSE;
                    }
                }

                FileLoc->FileCluster = GET_PAGE_ENTRY();

                if (FileLoc->FileCluster == EXFS_CLUSTER_END) goto Out_Error;

                if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer)) {
                    return FALSE;
                }
            }
        }

    NextComponent:;
    }

Out_Success:

    DeleteList(List);
    return TRUE;

Out_Error:

    DeleteList(List);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Write sectors to a physical disk.
 * @param Disk Target disk.
 * @param Sector Starting sector.
 * @param NumSectors Number of sectors to write.
 * @param Buffer Source buffer.
 * @return TRUE on success.
 */
static BOOL WriteSectors(LPSTORAGE_UNIT Disk, SECTOR Sector, U32 NumSectors, LPVOID Buffer) {
    IOCONTROL Control;
    U32 Result;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = NumSectors;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = Disk->Driver->Command(DF_DISK_WRITE, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Create a new EXFS partition on a disk.
 * @param Create Parameters for the partition.
 * @return Driver-specific error code.
 */
static U32 CreatePartition(LPPARTITION_CREATION Create) {
    U8 Buffer1[SECTOR_SIZE * 2];
    U8 Buffer2[SECTOR_SIZE * 2];
    U8 Buffer3[SECTOR_SIZE * 2];
    LPEXFSMBR Master = (LPEXFSMBR)Buffer1;
    LPEXFSSUPER Super = (LPEXFSSUPER)Buffer2;
    LPEXFSFILEREC FileRec = (LPEXFSFILEREC)Buffer3;
    U32* Buffer3Long = (U32*)Buffer3;
    U32 PartitionNumClusters = 0;
    U32 BytesPerCluster = 0;
    U32 BitmapEntriesPerCluster = 0;
    U32 BitmapNumClusters = 0;
    U32 BitmapCluster = 0;
    U32 RootCluster = 0;
    U32 CurrentSector = Create->PartitionStartSector;

    //-------------------------------------
    // Check validity of parameters

    if (Create == NULL) return DF_RETURN_BAD_PARAMETER;
    if (Create->Size != sizeof(PARTITION_CREATION)) return DF_RETURN_BAD_PARAMETER;
    if (Create->Disk == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------

    MemorySet(Buffer1, 0, SECTOR_SIZE * 2);
    MemorySet(Buffer2, 0, SECTOR_SIZE * 2);
    MemorySet(Buffer3, 0, SECTOR_SIZE * 2);

    //-------------------------------------
    // Compute size in clusters of bitmap

    if (Create->SectorsPerCluster == 0) {
        Create->SectorsPerCluster = 4096 / SECTOR_SIZE;
    }

    BytesPerCluster = Create->SectorsPerCluster * SECTOR_SIZE;
    PartitionNumClusters = Create->PartitionNumSectors / Create->SectorsPerCluster;
    BitmapEntriesPerCluster = BytesPerCluster * 8;
    BitmapNumClusters = (PartitionNumClusters / BitmapEntriesPerCluster) + 1;
    BitmapCluster = 1;
    RootCluster = BitmapCluster + BitmapNumClusters;

    //-------------------------------------
    // Fill the master boot record

    ExosMbrFill(Master, (U16)Create->SectorsPerCluster);

    if (WriteSectors(Create->Disk, CurrentSector, 2, Master) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    CurrentSector += 2;

    //-------------------------------------
    // Fill the superblock

    Super->Magic[0] = 'E';
    Super->Magic[1] = 'X';
    Super->Magic[2] = 'O';
    Super->Magic[3] = 'S';
    Super->Version = 0x00010000;
    Super->BytesPerCluster = BytesPerCluster;
    Super->NumClusters = PartitionNumClusters;
    Super->NumFreeClusters = PartitionNumClusters;
    Super->BitmapCluster = BitmapCluster;
    Super->BadCluster = 0;
    Super->RootCluster = RootCluster;
    Super->KernelFileIndex = 0;
    Super->NumFolders = 0;
    Super->NumFiles = 0;
    Super->MaxMountCount = 128;
    Super->CurrentMountCount = 0;
    Super->VolumeNameFormat = 0;

    StringCopy(Super->VolumeName, Create->VolumeName);

    if (WriteSectors(Create->Disk, CurrentSector, 2, Super) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    CurrentSector += 2;

    //-------------------------------------
    // Cluster 0 is empty because 0 is not a valid
    // cluster index (like NULL)

    CurrentSector += Create->SectorsPerCluster;

    //-------------------------------------
    // Skip the bitmap

    CurrentSector += (BitmapNumClusters * Create->SectorsPerCluster);

    //-------------------------------------
    // Write the root cluster page

    Buffer3Long[0] = RootCluster + 1;
    Buffer3Long[1] = EXFS_CLUSTER_END;

    if (WriteSectors(Create->Disk, CurrentSector, 1, Buffer3) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    CurrentSector += Create->SectorsPerCluster;

    //-------------------------------------
    // Write the first file record

    MemorySet(FileRec, 0, sizeof(EXFSFILEREC));

    FileRec->ClusterTable = EXFS_CLUSTER_END;

    if (WriteSectors(Create->Disk, CurrentSector, 1, Buffer3) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    //-------------------------------------

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Translate an EXFS file record to a file structure.
 * @param FileRec Source file record.
 * @param File Destination file.
 */
static void TranslateFileInfo(LPEXFSFILEREC FileRec, LPEXFSFILE File) {
    //-------------------------------------
    // Translate the attributes

    File->Header.Attributes = 0;

    if (FileRec->Attributes & EXFS_ATTR_FOLDER) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if (FileRec->Attributes & EXFS_ATTR_READONLY) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if (FileRec->Attributes & EXFS_ATTR_HIDDEN) {
        File->Header.Attributes |= FS_ATTR_HIDDEN;
    }

    if (FileRec->Attributes & EXFS_ATTR_SYSTEM) {
        File->Header.Attributes |= FS_ATTR_SYSTEM;
    }

    if (FileRec->Attributes & EXFS_ATTR_EXECUTABLE) {
        File->Header.Attributes |= FS_ATTR_EXECUTABLE;
    }

    //-------------------------------------
    // Translate the size

    File->Header.SizeLow = FileRec->SizeLo;
    File->Header.SizeHigh = FileRec->SizeHi;

    //-------------------------------------
    // Translate the time

    File->Header.Creation.Year = FileRec->CreationTime.Year;
    File->Header.Creation.Month = FileRec->CreationTime.Month;
    File->Header.Creation.Day = FileRec->CreationTime.Day;
    File->Header.Creation.Hour = FileRec->CreationTime.Hour;
    File->Header.Creation.Minute = FileRec->CreationTime.Minute;
    File->Header.Creation.Second = FileRec->CreationTime.Second;
    File->Header.Creation.Milli = FileRec->CreationTime.Milli;
}

/***************************************************************************/

/**
 * @brief Initialize the EXFS driver.
 * @return Driver-specific result code.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/***************************************************************************/

/**
 * @brief Open a file based on search information.
 * @param Find File information from a directory search.
 * @return Pointer to opened file or NULL.
 */
static LPEXFSFILE OpenFile(LPFILE_INFO Find) {
    LPEXFSFILESYSTEM FileSystem = NULL;
    LPEXFSFILE File = NULL;
    LPEXFSFILEREC FileRec = NULL;
    EXFSFILELOC FileLoc;

    //-------------------------------------
    // Check validity of parameters

    if (Find == NULL) return NULL;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPEXFSFILESYSTEM)Find->FileSystem;

    if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE) {
        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;

        FileRec = (LPEXFSFILEREC)(FileSystem->IOBuffer + FileLoc.FileOffset);

        File = NewEXFSFile(FileSystem, &FileLoc);
        if (File == NULL) return NULL;

        StringCopy(File->Header.Name, FileRec->Name);
        TranslateFileInfo(FileRec, File);
    } else if (Find->Flags & FILE_OPEN_CREATE_ALWAYS) {
        // TODO: Implement file creation in EXFS
        // For now, this is a placeholder to handle FILE_OPEN_CREATE_ALWAYS
        // The actual file creation functionality needs to be implemented
        return NULL;
    }

    return File;
}

/***************************************************************************/

#undef GET_PAGE_ENTRY

#define GET_PAGE_ENTRY() (*((U32*)(FileSystem->PageBuffer + File->Location.PageOffset)))

/**
 * @brief Open the next file in a directory listing.
 * @param File Current file handle.
 * @return Driver-specific status code.
 */
static U32 OpenNext(LPEXFSFILE File) {
    LPEXFSFILESYSTEM FileSystem = NULL;
    LPEXFSFILEREC FileRec = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPEXFSFILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Read the cluster containing the file

    if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;

    FOREVER {
        File->Location.FileOffset += sizeof(EXFSFILEREC);

        if (File->Location.FileOffset >= FileSystem->BytesPerCluster) {
            File->Location.PageOffset += sizeof(U32);

            //-------------------------------------
            // If we are at the last page entry, check if there is
            // another page

            if (File->Location.PageOffset == (FileSystem->BytesPerCluster - sizeof(U32))) {
                File->Location.PageCluster = GET_PAGE_ENTRY();
                File->Location.PageOffset = 0;

                if (File->Location.PageCluster == EXFS_CLUSTER_END) return DF_RETURN_GENERIC;

                if (!ReadCluster(FileSystem, File->Location.PageCluster, FileSystem->PageBuffer)) {
                    return DF_RETURN_GENERIC;
                }
            }

            File->Location.FileCluster = GET_PAGE_ENTRY();

            if (File->Location.FileCluster == EXFS_CLUSTER_END) return DF_RETURN_GENERIC;

            if (!ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer)) {
                return DF_RETURN_GENERIC;
            }
        }

        FileRec = (LPEXFSFILEREC)(FileSystem->IOBuffer + File->Location.FileOffset);

        if (FileRec->ClusterTable == EXFS_CLUSTER_END) return DF_RETURN_GENERIC;

        if (FileRec->ClusterTable) {
            File->Location.DataCluster = FileRec->ClusterTable;
            StringCopy(File->Header.Name, FileRec->Name);
            TranslateFileInfo(FileRec, File);
            break;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Close an open EXFS file.
 * @param File File handle.
 * @return Driver-specific status code.
 */
static U32 CloseFile(LPEXFSFILE File) {
    // LPEXFSFILESYSTEM FileSystem = NULL;
    // LPEXFSFILEREC FileRec = NULL;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    // FileSystem = (LPEXFSFILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Update file information in directory entry

    /*
      if (ReadCluster(FileSystem, File->Location.FileCluster,
      FileSystem->IOBuffer) == FALSE)
      {
    return DF_RETURN_INPUT_OUTPUT;
      }

      DirEntry = (LPFATDIRENTRY_EXT) (FileSystem->IOBuffer +
      File->Location.Offset);

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
 * @brief Dispatch EXFS driver commands.
 * @param Function Command identifier.
 * @param Parameter Command parameter.
 * @return Driver-specific result code.
 */
UINT EXFSCommands(UINT Function, UINT Parameter) {
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
            return (UINT)OpenNext((LPEXFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (UINT)CloseFile((LPEXFSFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_RENAMEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_READ:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_WRITE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_CREATEPARTITION:
            return CreatePartition((LPPARTITION_CREATION)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
