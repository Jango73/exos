
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
#ifndef EXFS_H_INCLUDED
#define EXFS_H_INCLUDED

/***************************************************************************/

#include "core/ID.h"
#include "fs/File-System.h"
#include "utils/ExosMbr.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// EXFS Super Block

typedef struct tag_EXFSSUPER {
    U8 Magic[4];  // "EXOS"
    U32 Version;
    U32 BytesPerCluster;
    U32 NumClusters;
    U32 NumFreeClusters;
    U32 BitmapCluster;    // First cluster of bitmap
    U32 BadCluster;       // Page for bad clusters
    U32 RootCluster;      // Page for root directory
    U32 SecurityCluster;  // Security info
    U32 KernelFileIndex;
    U32 NumFolders;
    U32 NumFiles;
    U32 MaxMountCount;
    U32 CurrentMountCount;
    U32 VolumeNameFormat;
    U8 Reserved[4];
    U8 Password[32];
    U8 Creator[32];
    U8 VolumeName[128];
} EXFSSUPER, *LPEXFSSUPER;

/***************************************************************************/
// File time, 64 bytes

typedef struct tag_EXFSTIME {
    U32 Year : 22;
    U32 Month : 4;
    U32 Day : 6;
    U32 Hour : 6;
    U32 Minute : 6;
    U32 Second : 6;
    U32 Milli : 10;
    U32 Reserved : 4;
} EXFSTIME, *LPEXFSTIME;

/***************************************************************************/
// EXFS File Record, 256 bytes

typedef struct tag_EXFSFILEREC {
    U32 SizeLo;
    U32 SizeHi;
    EXFSTIME CreationTime;
    EXFSTIME LastAccessTime;
    EXFSTIME LastModificationTime;
    U32 ClusterTable;  // 0xFFFFFFFF = End of list
    U32 Attributes;
    U32 Security;
    U32 Group;
    U32 User;
    U32 NameFormat;
    U8 Reserved[72];  // Zeroes
    U8 Name[128];
} EXFSFILEREC, *LPEXFSFILEREC;

#define EXFS_ATTR_FOLDER BIT_0
#define EXFS_ATTR_READONLY BIT_1
#define EXFS_ATTR_SYSTEM BIT_2
#define EXFS_ATTR_ARCHIVE BIT_3
#define EXFS_ATTR_HIDDEN BIT_4
#define EXFS_ATTR_EXECUTABLE BIT_5

/***************************************************************************/

#define EXFS_CLUSTER_RESERVED ((U32)0xFFFFFFF0)
#define EXFS_CLUSTER_END ((U32)0xFFFFFFFF)

/***************************************************************************/
// EXFS File location

typedef struct tag_XFSFILELOC {
    U32 PageCluster;
    U32 PageOffset;
    U32 FileCluster;  // Actual cluster of this file
    U32 FileOffset;   // Offset in actual cluster of this file
    U32 DataCluster;  // Data cluster of this file
} EXFSFILELOC, *LPEXFSFILELOC;

/***************************************************************************/

#pragma pack(pop)

#endif
