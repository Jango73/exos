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


    EXT2 (format)

\************************************************************************/

/************************************************************************/

#include "drivers/filesystems/EXT2-Private.h"

#include "fs/Storage-Transfer-Layer.h"

/************************************************************************/

static BOOL WriteSectorsRaw(LPSTORAGE_UNIT Storage, U32 StartSector, U32 Count, LPCVOID Buffer) {
    IOCONTROL Control;
    U32 Result;

    if (Storage == NULL) return FALSE;
    if (Buffer == NULL) return FALSE;
    if (Count == 0) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = Storage;
    Control.SectorLow = StartSector;
    Control.SectorHigh = 0;
    Control.NumSectors = Count;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = Count * SECTOR_SIZE;

    Result = StorageTransferLayerWrite(&Control);

    return Result == DF_RETURN_SUCCESS;
}

/************************************************************************/

static BOOL WriteBlockRaw(
    LPSTORAGE_UNIT Storage, U32 PartitionStartSector, U32 SectorsPerBlock, U32 Block, LPCVOID Buffer) {
    return WriteSectorsRaw(Storage, PartitionStartSector + (Block * SectorsPerBlock), SectorsPerBlock, Buffer);
}

/************************************************************************/

static void SetBitmapBit(U8* Bitmap, U32 BitIndex) {
    U32 ByteIndex = BitIndex / 8;
    U32 Bit = BitIndex % 8;
    Bitmap[ByteIndex] = (U8)(Bitmap[ByteIndex] | (1 << Bit));
}

/************************************************************************/

static U16 AlignDirEntryLength(U16 Length) {
    U16 Rem = Length % EXT2_DIR_ENTRY_ALIGN;
    if (Rem == 0) return Length;
    return (U16)(Length + (EXT2_DIR_ENTRY_ALIGN - Rem));
}

/************************************************************************/

static void BuildRootDirectoryBlock(U8* Buffer, U32 BlockSize) {
    EXT2_DIRECTORY_ENTRY* Entry;
    U16 EntrySize;

    MemorySet(Buffer, 0, BlockSize);

    Entry = (EXT2_DIRECTORY_ENTRY*)Buffer;
    Entry->Inode = EXT2_ROOT_INODE;
    Entry->NameLength = 1;
    Entry->FileType = EXT2_FT_DIR;
    EntrySize = AlignDirEntryLength((U16)(EXT2_DIR_ENTRY_HEADER_SIZE + Entry->NameLength));
    Entry->RecordLength = EntrySize;
    Entry->Name[0] = '.';

    Entry = (EXT2_DIRECTORY_ENTRY*)(Buffer + EntrySize);
    Entry->Inode = EXT2_ROOT_INODE;
    Entry->NameLength = 2;
    Entry->FileType = EXT2_FT_DIR;
    Entry->RecordLength = (U16)(BlockSize - EntrySize);
    Entry->Name[0] = '.';
    Entry->Name[1] = '.';
}

/************************************************************************/

/**
 * @brief Creates a minimal EXT2 partition layout.
 * @param Create Parameters for the partition.
 * @return Driver-specific error code.
 */
U32 Ext2CreatePartition(LPPARTITION_CREATION Create) {
    U32 BlockSize;
    U32 SectorsPerBlock;
    U32 TotalBlocks;
    U32 InodesPerGroup;
    U32 InodeSize;
    U32 InodeTableBlocks;
    U32 FirstDataBlock;
    U32 GroupDescBlock;
    U32 BlockBitmapBlock;
    U32 InodeBitmapBlock;
    U32 InodeTableBlock;
    U32 DataBlockStart;
    U32 RootDataBlock;
    U32 UsedBlocks;
    U32 FreeBlocks;
    U32 InodesCount;
    U32 FreeInodes;
    const U32 ReservedInodes = 11;
    EXT2_SUPER Super;
    EXT2_BLOCK_GROUP Group;
    U8 BlockBuffer[EXT2_DEFAULT_BLOCK_SIZE];
    U8 BitmapBuffer[EXT2_DEFAULT_BLOCK_SIZE];
    U32 Index;
    U32 RootIndexInGroup;
    U32 RootBlockOffset;
    U32 RootOffsetInBlock;
    EXT2_INODE RootInode;

    if (Create == NULL) return DF_RETURN_BAD_PARAMETER;
    if (Create->Size != sizeof(PARTITION_CREATION)) return DF_RETURN_BAD_PARAMETER;
    if (Create->Storage == NULL) return DF_RETURN_BAD_PARAMETER;
    if (Create->PartitionNumSectors == 0) return DF_RETURN_BAD_PARAMETER;

    BlockSize = EXT2_DEFAULT_BLOCK_SIZE;
    SectorsPerBlock = BlockSize / SECTOR_SIZE;
    if (SectorsPerBlock == 0) return DF_RETURN_BAD_PARAMETER;

    if (Create->SectorsPerCluster == 0) {
        Create->SectorsPerCluster = SectorsPerBlock;
    }

    TotalBlocks = Create->PartitionNumSectors / SectorsPerBlock;
    if (TotalBlocks < 16) return DF_RETURN_BAD_PARAMETER;

    InodeSize = sizeof(EXT2_INODE);
    InodesPerGroup = 128;
    InodeTableBlocks = (InodesPerGroup * InodeSize + BlockSize - 1) / BlockSize;

    FirstDataBlock = 1;
    GroupDescBlock = FirstDataBlock + 1;
    BlockBitmapBlock = GroupDescBlock + 1;
    InodeBitmapBlock = BlockBitmapBlock + 1;
    InodeTableBlock = InodeBitmapBlock + 1;
    DataBlockStart = InodeTableBlock + InodeTableBlocks;
    if (DataBlockStart >= TotalBlocks) return DF_RETURN_BAD_PARAMETER;

    RootDataBlock = DataBlockStart;
    UsedBlocks = RootDataBlock + 1;
    if (UsedBlocks > TotalBlocks) return DF_RETURN_BAD_PARAMETER;

    FreeBlocks = TotalBlocks - UsedBlocks;
    InodesCount = InodesPerGroup;
    if (InodesCount < ReservedInodes) return DF_RETURN_BAD_PARAMETER;
    FreeInodes = InodesCount - ReservedInodes;

    MemorySet(&Super, 0, sizeof(EXT2_SUPER));
    Super.InodesCount = InodesCount;
    Super.BlocksCount = TotalBlocks;
    Super.ReservedBlocksCount = 0;
    Super.FreeBlocksCount = FreeBlocks;
    Super.FreeInodesCount = FreeInodes;
    Super.FirstDataBlock = FirstDataBlock;
    Super.LogBlockSize = 0;
    Super.LogFragmentSize = 0;
    Super.BlocksPerGroup = TotalBlocks;
    Super.FragmentsPerGroup = TotalBlocks;
    Super.InodesPerGroup = InodesPerGroup;
    Super.MountCount = 0;
    Super.MaxMountCount = 0;
    Super.Magic = EXT2_SUPER_MAGIC;
    Super.State = 1;
    Super.Errors = 0;
    Super.RevisionLevel = 0;
    Super.FirstInode = ReservedInodes;
    Super.InodeSize = (U16)InodeSize;
    Super.BlockGroupNumber = 0;

    MemorySet(Super.VolumeName, 0, sizeof(Super.VolumeName));
    StringCopy(Super.VolumeName, Create->VolumeName);

    MemorySet(BlockBuffer, 0, BlockSize);
    MemoryCopy(BlockBuffer, &Super, sizeof(EXT2_SUPER));
    if (WriteSectorsRaw(Create->Storage, Create->PartitionStartSector + 2, 2, BlockBuffer) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    MemorySet(&Group, 0, sizeof(EXT2_BLOCK_GROUP));
    Group.BlockBitmap = BlockBitmapBlock;
    Group.InodeBitmap = InodeBitmapBlock;
    Group.InodeTable = InodeTableBlock;
    Group.FreeBlocksCount = (U16)FreeBlocks;
    Group.FreeInodesCount = (U16)FreeInodes;
    Group.UsedDirsCount = 1;

    MemorySet(BlockBuffer, 0, BlockSize);
    MemoryCopy(BlockBuffer, &Group, sizeof(EXT2_BLOCK_GROUP));
    if (WriteBlockRaw(Create->Storage, Create->PartitionStartSector, SectorsPerBlock, GroupDescBlock, BlockBuffer) ==
        FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    MemorySet(BitmapBuffer, 0, BlockSize);
    for (Index = 0; Index < UsedBlocks; Index++) {
        SetBitmapBit(BitmapBuffer, Index);
    }
    if (WriteBlockRaw(Create->Storage, Create->PartitionStartSector, SectorsPerBlock, BlockBitmapBlock, BitmapBuffer) ==
        FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    MemorySet(BitmapBuffer, 0, BlockSize);
    for (Index = 0; Index < ReservedInodes; Index++) {
        SetBitmapBit(BitmapBuffer, Index);
    }
    if (WriteBlockRaw(Create->Storage, Create->PartitionStartSector, SectorsPerBlock, InodeBitmapBlock, BitmapBuffer) ==
        FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    MemorySet(BlockBuffer, 0, BlockSize);
    RootIndexInGroup = EXT2_ROOT_INODE - 1;
    RootBlockOffset = RootIndexInGroup / (BlockSize / InodeSize);
    RootOffsetInBlock = (RootIndexInGroup % (BlockSize / InodeSize)) * InodeSize;

    MemorySet(&RootInode, 0, sizeof(EXT2_INODE));
    RootInode.Mode = (U16)(EXT2_MODE_DIRECTORY | 0x01ED);
    RootInode.LinksCount = 2;
    RootInode.Size = BlockSize;
    RootInode.Blocks = BlockSize / SECTOR_SIZE;
    RootInode.Block[0] = RootDataBlock;

    MemoryCopy(BlockBuffer + RootOffsetInBlock, &RootInode, sizeof(EXT2_INODE));
    if (WriteBlockRaw(
            Create->Storage,
            Create->PartitionStartSector,
            SectorsPerBlock,
            InodeTableBlock + RootBlockOffset,
            BlockBuffer) == FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    MemorySet(BlockBuffer, 0, BlockSize);
    for (Index = 1; Index < InodeTableBlocks; Index++) {
        if (WriteBlockRaw(
                Create->Storage, Create->PartitionStartSector, SectorsPerBlock, InodeTableBlock + Index, BlockBuffer) ==
            FALSE) {
            return DF_RETURN_FS_CANT_WRITE_SECTOR;
        }
    }

    BuildRootDirectoryBlock(BlockBuffer, BlockSize);
    if (WriteBlockRaw(Create->Storage, Create->PartitionStartSector, SectorsPerBlock, RootDataBlock, BlockBuffer) ==
        FALSE) {
        return DF_RETURN_FS_CANT_WRITE_SECTOR;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
