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


    EXT2 (private)

\************************************************************************/
#ifndef EXT2_PRIVATE_H_INCLUDED
#define EXT2_PRIVATE_H_INCLUDED

/************************************************************************/

#include "drivers/filesystems/EXT2.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"
#include "utils/BufferPool.h"

/************************************************************************/

#define VER_MAJOR 0
#define VER_MINOR 1

#define EXT2_DEFAULT_BLOCK_SIZE 1024
#define EXT2_MODE_TYPE_MASK 0xF000
#define EXT2_MODE_DIRECTORY 0x4000
#define EXT2_MODE_REGULAR 0x8000
#define EXT2_DIRECT_BLOCKS 12
#define EXT2_DIR_ENTRY_HEADER_SIZE (sizeof(U32) + sizeof(U16) + sizeof(U8) + sizeof(U8))
#define EXT2_DIR_ENTRY_ALIGN 4
#define EXT2_FT_UNKNOWN 0
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR 2
#define EXT2_MODE_USER_WRITE 0x0080
#define EXT2_MODE_GROUP_WRITE 0x0010
#define EXT2_MODE_OTHER_WRITE 0x0002
#define EXT2_MODE_USER_EXECUTE 0x0040
#define EXT2_MODE_GROUP_EXECUTE 0x0008
#define EXT2_MODE_OTHER_EXECUTE 0x0001

#define EXT2_BLOCK_BUFFER_OBJECTS_PER_SLAB 8
#define EXT2_BLOCK_BUFFER_INITIAL_SLABS 1
#define EXT2_BLOCK_BUFFER_MIN_FREE 8

/************************************************************************/

typedef struct tag_EXT2FILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    EXT2_SUPER Super;
    LPEXT2BLOCKGROUP Groups;
    U32 GroupCount;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BlockSize;
    U32 SectorsPerBlock;
    U32 InodeSize;
    U32 InodesPerBlock;
    MUTEX FilesMutex;
    BUFFER_POOL BlockBufferPool;
    U8* IOBuffer;
} EXT2_FILE_SYSTEM, *LPEXT2FILESYSTEM;

/************************************************************************/

typedef struct tag_EXT2FILE {
    FILE Header;
    EXT2_INODE Inode;
    U32 InodeIndex;
    BOOL IsDirectory;
    BOOL Enumerate;
    U32 DirectoryBlockIndex;
    U32 DirectoryBlockOffset;
    U8* DirectoryBlock;
    BOOL DirectoryBlockValid;
    STR Pattern[MAX_FILE_NAME];
} EXT2_FILE, *LPEXT2FILE;

/************************************************************************/

extern DRIVER EXT2Driver;

BOOL HasWildcard(LPCSTR Path);
void ExtractBaseName(LPCSTR Path, LPSTR Name);
void ReleaseDirectoryResources(LPEXT2FILE File);
BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern);
BOOL ReadSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPVOID Buffer);
BOOL ReadBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPVOID Buffer);
BOOL WriteSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPCVOID Buffer);
BOOL WriteBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPCVOID Buffer);
BOOL Ext2BufferPoolInit(LPEXT2FILESYSTEM FileSystem);
void Ext2BufferPoolDeinit(LPEXT2FILESYSTEM FileSystem);
LPVOID Ext2AcquireBlockBuffer(LPEXT2FILESYSTEM FileSystem);
void Ext2ReleaseBlockBuffer(LPEXT2FILESYSTEM FileSystem, LPVOID Buffer);
BOOL LoadGroupDescriptors(LPEXT2FILESYSTEM FileSystem);
BOOL PrepareInodeBlockAccess(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, U8** BlockBufferOut, U32* OffsetInBlockOut,
                             U32* CopySizeOut);
BOOL ReadInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode);
BOOL WriteInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode);
BOOL GetInodeBlockNumber(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, U32* BlockNumber);
BOOL ResolveInodeBlock(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, BOOL Allocate, U32* BlockNumber);
BOOL FindInodeInDirectory(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Directory, LPCSTR Name, U32* InodeIndex);
BOOL ResolvePath(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex);
BOOL LoadDirectoryInode(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex);
void FillFileHeaderFromInode(LPEXT2FILE File, LPCSTR Name, LPEXT2INODE Inode);
BOOL SetupDirectoryHandle(
    LPEXT2FILE File,
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 InodeIndex,
    BOOL Enumerate,
    LPCSTR Pattern);
BOOL LoadNextDirectoryEntry(LPEXT2FILE File);
U32 AlignDirectoryNameLength(U32 Length);
BOOL FlushSuperBlock(LPEXT2FILESYSTEM FileSystem);
BOOL FlushGroupDescriptor(LPEXT2FILESYSTEM FileSystem, U32 GroupIndex);
BOOL AllocateBlock(LPEXT2FILESYSTEM FileSystem, U32* BlockNumber);
BOOL FreeBlock(LPEXT2FILESYSTEM FileSystem, U32 BlockNumber);
BOOL AllocateInode(
    LPEXT2FILESYSTEM FileSystem, BOOL Directory, U32* InodeIndex, LPEXT2INODE Inode, U32* GroupIndexOut);
BOOL FreeInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, BOOL Directory);
BOOL TruncateInode(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode);
BOOL FreeIndirectTree(LPEXT2FILESYSTEM FileSystem, U32 BlockNumber, U32 Depth);
BOOL AddDirectoryEntry(
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 DirectoryIndex,
    U32 ChildInodeIndex,
    LPCSTR Name,
    U8 FileType);
BOOL CreateDirectoryInternal(
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Parent,
    U32 ParentIndex,
    LPCSTR Name,
    U32* NewInodeIndex,
    LPEXT2INODE NewInode);
BOOL EnsureParentDirectory(
    LPEXT2FILESYSTEM FileSystem,
    LPCSTR Path,
    LPEXT2INODE Parent,
    U32* ParentIndex,
    LPSTR FinalComponent);
U32 CreateNode(LPFILE_INFO Info, BOOL Directory);

BOOL MountPartition_EXT2(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex);
U32 Ext2CreatePartition(LPPARTITION_CREATION Create);
UINT EXT2Commands(UINT Function, UINT Parameter);

#endif  // EXT2_PRIVATE_H_INCLUDED
