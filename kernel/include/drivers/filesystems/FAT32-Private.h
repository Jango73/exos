
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


    Executable

\************************************************************************/

#ifndef FAT32_PRIVATE_H_INCLUDED
#define FAT32_PRIVATE_H_INCLUDED

#include "core/Kernel.h"
#include "drivers/filesystems/FAT.h"
#include "fs/File-System.h"
#include "log/Log.h"
#include "text/CoreString.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/

// The file system object allocated when mounting

typedef struct tag_FAT32FILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    FAT32MBR Master;
    SECTOR PartitionStart;
    U32 PartitionSize;
    SECTOR FATStart;
    SECTOR FATStart2;
    SECTOR DataStart;
    U32 BytesPerCluster;
    U8* IOBuffer;
    U32 IOBufferGeneration;
} FAT32FILESYSTEM, *LPFAT32FILESYSTEM;

/***************************************************************************/

typedef struct tag_FATFILE {
    FILE Header;
    FATFILELOC Location;
    U32 DirectoryBufferCluster;
    U32 DirectoryBufferGeneration;
    BOOL DirectoryBufferValid;
} FATFILE, *LPFATFILE;

/***************************************************************************/

extern DRIVER FAT32Driver;

UINT FAT32Commands(UINT Function, UINT Parameter);

U32 GetNameChecksum(LPSTR Name);
LPFATFILE NewFATFile(LPFAT32FILESYSTEM FileSystem, LPFATFILELOC FileLoc);
BOOL ReadCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer);
BOOL WriteCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer);
CLUSTER GetNextClusterInChain(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster);
BOOL CreateDirEntry(LPFAT32FILESYSTEM FileSystem, CLUSTER FolderCluster, LPSTR Name, U32 Attributes);
CLUSTER ChainNewCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster);

#endif
