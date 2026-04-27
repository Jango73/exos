
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


    EXT2

\************************************************************************/
#ifndef EXT2_H_INCLUDED
#define EXT2_H_INCLUDED

/***************************************************************************/

#include "core/ID.h"
#include "fs/File-System.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// EXT2 constants

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_ROOT_INODE 2
#define EXT2_NAME_MAX 255
#define EXT2_N_BLOCKS 15

/***************************************************************************/
// EXT2 Super Block (partial representation)

typedef struct tag_EXT2SUPER {
    U32 InodesCount;
    U32 BlocksCount;
    U32 ReservedBlocksCount;
    U32 FreeBlocksCount;
    U32 FreeInodesCount;
    U32 FirstDataBlock;
    U32 LogBlockSize;
    U32 LogFragmentSize;
    U32 BlocksPerGroup;
    U32 FragmentsPerGroup;
    U32 InodesPerGroup;
    U32 MountTime;
    U32 WriteTime;
    U16 MountCount;
    U16 MaxMountCount;
    U16 Magic;
    U16 State;
    U16 Errors;
    U16 MinorRevisionLevel;
    U32 LastCheck;
    U32 CheckInterval;
    U32 CreatorOS;
    U32 RevisionLevel;
    U16 DefaultReservedUserID;
    U16 DefaultReservedGroupID;
    U32 FirstInode;
    U16 InodeSize;
    U16 BlockGroupNumber;
    U32 FeatureCompatible;
    U32 FeatureIncompatible;
    U32 FeatureReadOnlyCompatible;
    U8 UUID[16];
    U8 VolumeName[16];
    U8 LastMounted[64];
} EXT2SUPER, *LPEXT2SUPER;

/***************************************************************************/
// EXT2 Block Group Descriptor (partial)

typedef struct tag_EXT2BLOCKGROUP {
    U32 BlockBitmap;
    U32 InodeBitmap;
    U32 InodeTable;
    U16 FreeBlocksCount;
    U16 FreeInodesCount;
    U16 UsedDirsCount;
    U16 Pad;
    U8 Reserved[12];
} EXT2BLOCKGROUP, *LPEXT2BLOCKGROUP;

/***************************************************************************/
// EXT2 Inode structure (partial)

typedef struct tag_EXT2INODE {
    U16 Mode;
    U16 UserID;
    U32 Size;
    U32 AccessTime;
    U32 ChangeTime;
    U32 ModificationTime;
    U32 DeletionTime;
    U16 GroupID;
    U16 LinksCount;
    U32 Blocks;
    U32 Flags;
    U32 Reserved;
    U32 Block[EXT2_N_BLOCKS];
    U32 Generation;
    U32 FileACL;
    U32 DirectoryACL;
    U32 FragmentAddress;
} EXT2INODE, *LPEXT2INODE;

/***************************************************************************/
// EXT2 Directory entry (variable length)

typedef struct tag_EXT2DIRECTORYENTRY {
    U32 Inode;
    U16 RecordLength;
    U8 NameLength;
    U8 FileType;
    U8 Name[EXT2_NAME_MAX];
} EXT2DIRECTORYENTRY, *LPEXT2DIRECTORYENTRY;

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

LPDRIVER EXT2GetDriver(void);

/***************************************************************************/

#endif  // EXT2_H_INCLUDED
