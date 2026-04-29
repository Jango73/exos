
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


    File System

\************************************************************************/

#ifndef FILESYS_H_INCLUDED
#define FILESYS_H_INCLUDED

/***************************************************************************/

#include "core/Driver.h"
#include "core/ID.h"
#include "Disk.h"
#include "../process/Process.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

// Functions supplied by a file system driver

#define DF_FS_GETVOLUME_INFO (DF_FIRST_FUNCTION + 0)
#define DF_FS_SETVOLUME_INFO (DF_FIRST_FUNCTION + 1)
#define DF_FS_FLUSH (DF_FIRST_FUNCTION + 2)
#define DF_FS_CREATEFOLDER (DF_FIRST_FUNCTION + 3)
#define DF_FS_DELETEFOLDER (DF_FIRST_FUNCTION + 4)
#define DF_FS_RENAMEFOLDER (DF_FIRST_FUNCTION + 5)
#define DF_FS_OPENFILE (DF_FIRST_FUNCTION + 6)
#define DF_FS_OPENNEXT (DF_FIRST_FUNCTION + 7)
#define DF_FS_CLOSEFILE (DF_FIRST_FUNCTION + 8)
#define DF_FS_DELETEFILE (DF_FIRST_FUNCTION + 9)
#define DF_FS_RENAMEFILE (DF_FIRST_FUNCTION + 10)
#define DF_FS_READ (DF_FIRST_FUNCTION + 11)
#define DF_FS_WRITE (DF_FIRST_FUNCTION + 12)
#define DF_FS_GETPOSITION (DF_FIRST_FUNCTION + 13)
#define DF_FS_SETPOSITION (DF_FIRST_FUNCTION + 14)
#define DF_FS_GETATTRIBUTES (DF_FIRST_FUNCTION + 15)
#define DF_FS_SETATTRIBUTES (DF_FIRST_FUNCTION + 16)
#define DF_FS_CREATEPARTITION (DF_FIRST_FUNCTION + 17)
#define DF_FS_MOUNTOBJECT (DF_FIRST_FUNCTION + 18)
#define DF_FS_UNMOUNTOBJECT (DF_FIRST_FUNCTION + 19)
#define DF_FS_PATHEXISTS (DF_FIRST_FUNCTION + 20)
#define DF_FS_FILEEXISTS (DF_FIRST_FUNCTION + 21)

/***************************************************************************/

#define DF_RETURN_FS_BADSECTOR (DF_RETURN_FIRST + 0)
#define DF_RETURN_FS_NOSPACE (DF_RETURN_FIRST + 1)
#define DF_RETURN_FS_CANT_READ_SECTOR (DF_RETURN_FIRST + 2)
#define DF_RETURN_FS_CANT_WRITE_SECTOR (DF_RETURN_FIRST + 3)

/***************************************************************************/

// Generic file attributes

#define FS_ATTR_FOLDER 0x0001
#define FS_ATTR_READONLY 0x0002
#define FS_ATTR_HIDDEN 0x0004
#define FS_ATTR_SYSTEM 0x0008
#define FS_ATTR_EXECUTABLE 0x0010

/***************************************************************************/

#define MBR_PARTITION_START 0x01BE
#define MBR_PARTITION_SIZE 0x0010
#define MBR_PARTITION_COUNT 0x0004

/***************************************************************************/

// The BIOS "Cylinder-Head-Sector" format

typedef struct tag_PCHS {
    U8 Head;
    U8 Cylinder;
    U8 Sector;  // Bits 6 & 7 are high bits of cylinder
} PCHS, *LPPCHS;

/***************************************************************************/

// The logical "Cylinder-Head-Sector" format

typedef struct tag_LCHS {
    U32 Cylinder;
    U32 Head;
    U32 Sector;
} LCHS, *LPLCHS;

/***************************************************************************/

typedef struct tag_BOOT_PARTITION {
    U8 Disk;        // 0x80 for active partition
    PCHS StartCHS;  // CHS of disk start
    U8 Type;        // Type of partition
    PCHS EndCHS;    // CHS of disk end
    SECTOR LBA;     // Logical Block Addressing start
    U32 Size;       // Size in sectors
} BOOT_PARTITION, *LPBOOT_PARTITION;

/***************************************************************************/

// Partition metadata associated with a mounted filesystem

#define PARTITION_SCHEME_NONE 0x00000000
#define PARTITION_SCHEME_MBR 0x00000001
#define PARTITION_SCHEME_GPT 0x00000002
#define PARTITION_SCHEME_VIRTUAL 0x00000003

#define PARTITION_FLAG_ACTIVE 0x00000001

#define PARTITION_FORMAT_UNKNOWN 0x00000000
#define PARTITION_FORMAT_FAT16 0x00000001
#define PARTITION_FORMAT_FAT32 0x00000002
#define PARTITION_FORMAT_NTFS 0x00000003
#define PARTITION_FORMAT_EXFS 0x00000004
#define PARTITION_FORMAT_EXT2 0x00000005
#define PARTITION_FORMAT_EXT3 0x00000006
#define PARTITION_FORMAT_EXT4 0x00000007

typedef struct tag_PARTITION {
    U32 Scheme;
    U32 Type;
    U32 Format;
    U32 Index;
    U32 Flags;
    SECTOR StartSector;
    U32 NumSectors;
    U8 TypeGuid[GPT_GUID_LENGTH];
} PARTITION, *LPPARTITION;

/***************************************************************************/

typedef struct tag_FILESYSTEM {
    LISTNODE_FIELDS
    MUTEX Mutex;
    BOOL Mounted;
    LPDRIVER Driver;
    LPSTORAGE_UNIT StorageUnit;
    PARTITION Partition;
    STR Name[MAX_FS_LOGICAL_NAME];
} FILESYSTEM, *LPFILESYSTEM;

/***************************************************************************/
// Global file system state shared across the kernel

typedef struct tag_FILESYSTEM_GLOBAL_INFO {
    STR ActivePartitionName[MAX_FS_LOGICAL_NAME];
} FILESYSTEM_GLOBAL_INFO, *LPFILESYSTEM_GLOBAL_INFO;

/***************************************************************************/
// The structure used by the folder commands and the file open command

typedef struct tag_FILE_INFO {
    UINT Size;
    LPFILESYSTEM FileSystem;
    U32 Attributes;
    U32 Flags;
    STR Name[MAX_PATH_NAME];
} FILE_INFO, *LPFILE_INFO;

/***************************************************************************/
// Structure that discribes an open file

typedef struct tag_FILE {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPFILESYSTEM FileSystem;
    SECURITY Security;
    LPTASK OwnerTask;
    U32 OpenFlags;
    U32 Attributes;
    U32 SizeLow;
    U32 SizeHigh;
    DATETIME Creation;
    DATETIME Accessed;
    DATETIME Modified;
    UINT Position;
    UINT ByteCount;
    UINT BytesTransferred;
    LPVOID Buffer;
    STR Name[MAX_FILE_NAME];
} FILE, *LPFILE;

/***************************************************************************/
// Structure used by the partition commands

#define FLAG_PART_CREATE_QUICK_FORMAT 0x0001

typedef struct tag_PARTITION_CREATION {
    UINT Size;
    LPSTORAGE_UNIT Disk;
    UINT PartitionStartSector;
    UINT PartitionNumSectors;
    UINT SectorsPerCluster;
    U32 Flags;
    STR VolumeName[MAX_PATH_NAME];
} PARTITION_CREATION, *LPPARTITION_CREATION;

/***************************************************************************/

typedef struct tag_PATH_NODE {
    LISTNODE_FIELDS
    STR Name[MAX_FILE_NAME];
} PATH_NODE, *LPPATH_NODE;

/***************************************************************************/

typedef struct tag_FILESYSTEM_MOUNT_CONTROL {
    STR Path[MAX_PATH_NAME];
    LPLISTNODE Node;
    STR SourcePath[MAX_PATH_NAME];
} FILESYSTEM_MOUNT_CONTROL, *LPFILESYSTEM_MOUNT_CONTROL;

typedef FILESYSTEM_MOUNT_CONTROL FILESYSTEM_UNMOUNT_CONTROL, *LPFILESYSTEM_UNMOUNT_CONTROL;

/***************************************************************************/

typedef struct tag_FILESYSTEM_PATHCHECK {
    STR CurrentFolder[MAX_PATH_NAME];
    STR SubFolder[MAX_PATH_NAME];
} FILESYSTEM_PATHCHECK, *LPFILESYSTEM_PATHCHECK;

/***************************************************************************/

BOOL MountDiskPartitions(LPSTORAGE_UNIT, LPBOOT_PARTITION, U32);
U32 GetNumFileSystems(void);
LPSTORAGE_UNIT FileSystemGetStorageUnit(LPFILESYSTEM FileSystem);
BOOL FileSystemHasStorageUnit(LPFILESYSTEM FileSystem);
BOOL FileSystemReady(void);
LPCSTR FileSystemGetPartitionSchemeName(U32 Scheme);
LPCSTR FileSystemGetPartitionTypeName(LPPARTITION Partition);
LPCSTR FileSystemGetPartitionFormatName(U32 Format);
BOOL GetDefaultFileSystemName(LPSTR, LPSTORAGE_UNIT, U32);
BOOL MountSystemFS(void);
BOOL MountUserNodes(void);
void InitializeFileSystems(void);
void FileSystemSetActivePartition(LPFILESYSTEM FileSystem);

/***************************************************************************/

#pragma pack(pop)

#endif
