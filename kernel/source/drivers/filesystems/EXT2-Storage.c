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


    EXT2

\************************************************************************/

#include "drivers/filesystems/EXT2-Private.h"

/************************************************************************/

BOOL ReadSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPVOID Buffer) {
    IOCONTROL Control;
    U32 Result;

    if (FileSystem == NULL || FileSystem->Disk == NULL) return FALSE;
    if (Buffer == NULL || Count == 0) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->PartitionStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = Count;
    Control.Buffer = Buffer;
    Control.BufferSize = Count * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    return Result == DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reads a complete EXT2 block into the provided buffer.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Block Block index to read.
 * @param Buffer Destination buffer sized to hold one block.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ReadBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPVOID Buffer) {
    BOOL Result;

    if (FileSystem == NULL) return FALSE;
    if (Buffer == NULL) return FALSE;
    if (FileSystem->SectorsPerBlock == 0) return FALSE;

    Result = ReadSectors(FileSystem, Block * FileSystem->SectorsPerBlock, FileSystem->SectorsPerBlock, Buffer);

    return Result;
}

/************************************************************************/

/**
 * @brief Writes raw sectors relative to the partition start.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Sector Sector index relative to the partition start.
 * @param Count Number of sectors to write.
 * @param Buffer Source buffer containing the data to write.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL WriteSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPCVOID Buffer) {
    IOCONTROL Control;

    if (FileSystem == NULL || FileSystem->Disk == NULL) return FALSE;
    if (Buffer == NULL || Count == 0) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->PartitionStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = Count;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = Count * SECTOR_SIZE;

    return FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (UINT)&Control) == DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Writes a complete EXT2 block from the provided buffer.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Block Block index to write.
 * @param Buffer Source buffer sized to hold one block.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL WriteBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPCVOID Buffer) {
    if (FileSystem == NULL) return FALSE;
    if (Buffer == NULL) return FALSE;
    if (FileSystem->SectorsPerBlock == 0) return FALSE;

    return WriteSectors(FileSystem, Block * FileSystem->SectorsPerBlock, FileSystem->SectorsPerBlock, Buffer);
}

/************************************************************************/

/**
 * @brief Loads block group descriptors from disk into memory.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @return TRUE when descriptors are successfully loaded, FALSE otherwise.
 */
BOOL LoadGroupDescriptors(LPEXT2FILESYSTEM FileSystem) {
    U32 GroupCount;
    U32 TableSize;
    U32 BlocksToRead;
    U32 Index;
    U32 StartBlock;
    U8* Buffer;

    if (FileSystem == NULL) return FALSE;
    if (FileSystem->Super.BlocksPerGroup == 0) return FALSE;

    if (FileSystem->Groups != NULL) {
        KernelHeapFree(FileSystem->Groups);
        FileSystem->Groups = NULL;
        FileSystem->GroupCount = 0;
    }

    GroupCount = (FileSystem->Super.BlocksCount + FileSystem->Super.BlocksPerGroup - 1) /
        FileSystem->Super.BlocksPerGroup;

    if (GroupCount == 0) return FALSE;

    TableSize = GroupCount * sizeof(EXT2BLOCKGROUP);
    FileSystem->Groups = (LPEXT2BLOCKGROUP)KernelHeapAlloc(TableSize);
    if (FileSystem->Groups == NULL) return FALSE;

    MemorySet(FileSystem->Groups, 0, TableSize);

    BlocksToRead = (TableSize + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    if (BlocksToRead == 0) BlocksToRead = 1;

    Buffer = (U8*)KernelHeapAlloc(BlocksToRead * FileSystem->BlockSize);
    if (Buffer == NULL) {
        KernelHeapFree(FileSystem->Groups);
        FileSystem->Groups = NULL;
        return FALSE;
    }

    MemorySet(Buffer, 0, BlocksToRead * FileSystem->BlockSize);

    StartBlock = FileSystem->Super.FirstDataBlock + 1;

    for (Index = 0; Index < BlocksToRead; Index++) {
        if (ReadBlock(FileSystem, StartBlock + Index, Buffer + (Index * FileSystem->BlockSize)) == FALSE) {
            KernelHeapFree(Buffer);
            KernelHeapFree(FileSystem->Groups);
            FileSystem->Groups = NULL;
            return FALSE;
        }
    }

    MemoryCopy(FileSystem->Groups, Buffer, TableSize);

    KernelHeapFree(Buffer);

    FileSystem->GroupCount = GroupCount;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve and read the inode table block containing one inode.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param InodeIndex Index of the inode to access.
 * @param BlockBufferOut Receives an acquired block buffer.
 * @param OffsetInBlockOut Receives the inode byte offset within the block.
 * @param CopySizeOut Receives the inode copy size.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL PrepareInodeBlockAccess(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, U8** BlockBufferOut, U32* OffsetInBlockOut,
                             U32* CopySizeOut) {
    LPEXT2BLOCKGROUP Group;
    U32 GroupIndex;
    U32 IndexInGroup;
    U32 BlockOffset;
    U8* BlockBuffer;

    if (FileSystem == NULL || BlockBufferOut == NULL || OffsetInBlockOut == NULL || CopySizeOut == NULL) return FALSE;
    if (InodeIndex == 0) return FALSE;
    if (FileSystem->InodesPerBlock == 0) return FALSE;
    if (FileSystem->GroupCount == 0 || FileSystem->Groups == NULL) return FALSE;

    GroupIndex = (InodeIndex - 1) / FileSystem->Super.InodesPerGroup;
    if (GroupIndex >= FileSystem->GroupCount) return FALSE;

    Group = &(FileSystem->Groups[GroupIndex]);
    if (Group->InodeTable == 0) return FALSE;

    IndexInGroup = (InodeIndex - 1) % FileSystem->Super.InodesPerGroup;
    BlockOffset = IndexInGroup / FileSystem->InodesPerBlock;
    *OffsetInBlockOut = (IndexInGroup % FileSystem->InodesPerBlock) * FileSystem->InodeSize;

    BlockBuffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (BlockBuffer == NULL) return FALSE;

    if (ReadBlock(FileSystem, Group->InodeTable + BlockOffset, BlockBuffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
        return FALSE;
    }

    *CopySizeOut = FileSystem->InodeSize;
    if (*CopySizeOut > sizeof(EXT2INODE)) {
        *CopySizeOut = sizeof(EXT2INODE);
    }

    *BlockBufferOut = BlockBuffer;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reads an inode from disk.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param InodeIndex Index of the inode to read.
 * @param Inode Destination buffer for the inode data.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ReadInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode) {
    U32 OffsetInBlock;
    U8* BlockBuffer;
    U32 CopySize;

    if (FileSystem == NULL || Inode == NULL) return FALSE;
    if (PrepareInodeBlockAccess(FileSystem, InodeIndex, &BlockBuffer, &OffsetInBlock, &CopySize) == FALSE) return FALSE;

    MemorySet(Inode, 0, sizeof(EXT2INODE));
    MemoryCopy(Inode, BlockBuffer + OffsetInBlock, CopySize);

    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieves the physical block number for a given inode block index.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Inode Pointer to the inode describing the file.
 * @param BlockIndex Zero-based data block index within the file.
 * @param BlockNumber Receives the resolved block number (0 if sparse).
 * @return TRUE if the block number could be determined, FALSE otherwise.
 */
BOOL GetInodeBlockNumber(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, U32* BlockNumber) {
    return ResolveInodeBlock(FileSystem, Inode, BlockIndex, FALSE, BlockNumber);
}

/************************************************************************/

/**
 * @brief Resolves a logical block index to a physical block, allocating
 *        the necessary indirection blocks when requested.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Inode Pointer to the inode describing the file.
 * @param BlockIndex Zero-based data block index within the file.
 * @param Allocate When TRUE, create missing data/indirect blocks.
 * @param BlockNumber Receives the resolved physical block (0 if absent).
 * @return TRUE on success, FALSE on error.
 */
BOOL ResolveInodeBlock(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, BOOL Allocate, U32* BlockNumber) {
    U32 EntriesPerBlock;
    U32 BlocksPerEntry;
    U32 LogicalIndex;
    U32 SingleSpan;
    U32 DoubleSpan;
    U32 TripleSpan;

    if (FileSystem == NULL || Inode == NULL || BlockNumber == NULL) return FALSE;
    if (FileSystem->BlockSize == 0) return FALSE;

    EntriesPerBlock = FileSystem->BlockSize / sizeof(U32);
    if (EntriesPerBlock == 0) return FALSE;

    BlocksPerEntry = FileSystem->BlockSize / 512;
    if (BlocksPerEntry == 0) return FALSE;

    if (BlockIndex < EXT2_DIRECT_BLOCKS) {
        U32 DataBlock = Inode->Block[BlockIndex];

        if (DataBlock == 0 && Allocate) {
            if (AllocateBlock(FileSystem, &DataBlock) == FALSE) return FALSE;
            Inode->Block[BlockIndex] = DataBlock;
            Inode->Blocks += BlocksPerEntry;
        }

        *BlockNumber = DataBlock;
        return TRUE;
    }

    LogicalIndex = BlockIndex - EXT2_DIRECT_BLOCKS;
    SingleSpan = EntriesPerBlock;
    DoubleSpan = SingleSpan * EntriesPerBlock;
    TripleSpan = DoubleSpan * EntriesPerBlock;

    if (LogicalIndex < SingleSpan) {
        U32 IndirectBlock = Inode->Block[EXT2_DIRECT_BLOCKS];
        U32* Buffer;

        if (IndirectBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &IndirectBlock) == FALSE) return FALSE;
            Inode->Block[EXT2_DIRECT_BLOCKS] = IndirectBlock;
            Inode->Blocks += BlocksPerEntry;

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, IndirectBlock, FileSystem->IOBuffer) == FALSE) return FALSE;
        }

        Buffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (Buffer == NULL) return FALSE;

        if (ReadBlock(FileSystem, IndirectBlock, Buffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, Buffer);
            return FALSE;
        }

        if (Buffer[LogicalIndex] == 0 && Allocate) {
            U32 NewBlock = 0;

            if (AllocateBlock(FileSystem, &NewBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Buffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            Buffer[LogicalIndex] = NewBlock;

            if (WriteBlock(FileSystem, IndirectBlock, Buffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Buffer);
                return FALSE;
            }

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, NewBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Buffer);
                return FALSE;
            }
        }

        *BlockNumber = Buffer[LogicalIndex];
        Ext2ReleaseBlockBuffer(FileSystem, Buffer);
        return TRUE;
    }

    LogicalIndex -= SingleSpan;

    if (LogicalIndex < DoubleSpan) {
        U32 DoubleBlock = Inode->Block[EXT2_DIRECT_BLOCKS + 1];
        U32* DoubleBuffer;
        U32* SingleBuffer;
        U32 DoubleIndex;
        U32 SingleIndex;
        U32 SingleBlock;

        if (DoubleBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &DoubleBlock) == FALSE) return FALSE;
            Inode->Block[EXT2_DIRECT_BLOCKS + 1] = DoubleBlock;
            Inode->Blocks += BlocksPerEntry;

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, DoubleBlock, FileSystem->IOBuffer) == FALSE) return FALSE;
        }

        DoubleBuffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (DoubleBuffer == NULL) return FALSE;

        if (ReadBlock(FileSystem, DoubleBlock, DoubleBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            return FALSE;
        }

        DoubleIndex = (U32)(LogicalIndex / SingleSpan);
        SingleIndex = (U32)(LogicalIndex % SingleSpan);

        if (DoubleIndex >= EntriesPerBlock) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            return FALSE;
        }

        SingleBlock = DoubleBuffer[DoubleIndex];

        if (SingleBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &SingleBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, SingleBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }

            DoubleBuffer[DoubleIndex] = SingleBlock;
            if (WriteBlock(FileSystem, DoubleBlock, DoubleBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }
        }

        SingleBuffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (SingleBuffer == NULL) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            return FALSE;
        }

        if (ReadBlock(FileSystem, SingleBlock, SingleBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            return FALSE;
        }

        if (SingleBuffer[SingleIndex] == 0 && Allocate) {
            U32 NewBlock = 0;

            if (AllocateBlock(FileSystem, &NewBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            SingleBuffer[SingleIndex] = NewBlock;

            if (WriteBlock(FileSystem, SingleBlock, SingleBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, NewBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                return FALSE;
            }
        }

        *BlockNumber = SingleBuffer[SingleIndex];
        Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
        Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
        return TRUE;
    }

    LogicalIndex -= DoubleSpan;

    if (LogicalIndex < TripleSpan) {
        U32 TripleBlock = Inode->Block[EXT2_DIRECT_BLOCKS + 2];
        U32* TripleBuffer;
        U32* DoubleBuffer;
        U32* SingleBuffer;
        U32 TripleIndex;
        U32 DoubleIndex;
        U32 SingleIndex;
        U32 DoubleBlock;
        U32 SingleBlock;

        if (TripleBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &TripleBlock) == FALSE) return FALSE;
            Inode->Block[EXT2_DIRECT_BLOCKS + 2] = TripleBlock;
            Inode->Blocks += BlocksPerEntry;

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, TripleBlock, FileSystem->IOBuffer) == FALSE) return FALSE;
        }

        TripleBuffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (TripleBuffer == NULL) return FALSE;

        if (ReadBlock(FileSystem, TripleBlock, TripleBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        TripleIndex = (U32)(LogicalIndex / DoubleSpan);
        LogicalIndex %= DoubleSpan;

        if (TripleIndex >= EntriesPerBlock) {
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        DoubleBlock = TripleBuffer[TripleIndex];

        if (DoubleBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &DoubleBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, DoubleBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            TripleBuffer[TripleIndex] = DoubleBlock;
            if (WriteBlock(FileSystem, TripleBlock, TripleBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }
        }

        DoubleBuffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (DoubleBuffer == NULL) {
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        if (ReadBlock(FileSystem, DoubleBlock, DoubleBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        DoubleIndex = (U32)(LogicalIndex / SingleSpan);
        SingleIndex = (U32)(LogicalIndex % SingleSpan);

        if (DoubleIndex >= EntriesPerBlock) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        SingleBlock = DoubleBuffer[DoubleIndex];

        if (SingleBlock == 0) {
            if (Allocate == FALSE) {
                *BlockNumber = 0;
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return TRUE;
            }

            if (AllocateBlock(FileSystem, &SingleBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, SingleBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            DoubleBuffer[DoubleIndex] = SingleBlock;
            if (WriteBlock(FileSystem, DoubleBlock, DoubleBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }
        }

        SingleBuffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
        if (SingleBuffer == NULL) {
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        if (ReadBlock(FileSystem, SingleBlock, SingleBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
            Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
            return FALSE;
        }

        if (SingleBuffer[SingleIndex] == 0 && Allocate) {
            U32 NewBlock = 0;

            if (AllocateBlock(FileSystem, &NewBlock) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            Inode->Blocks += BlocksPerEntry;
            SingleBuffer[SingleIndex] = NewBlock;

            if (WriteBlock(FileSystem, SingleBlock, SingleBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }

            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
            if (WriteBlock(FileSystem, NewBlock, FileSystem->IOBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
                Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
                return FALSE;
            }
        }

        *BlockNumber = SingleBuffer[SingleIndex];
        Ext2ReleaseBlockBuffer(FileSystem, SingleBuffer);
        Ext2ReleaseBlockBuffer(FileSystem, DoubleBuffer);
        Ext2ReleaseBlockBuffer(FileSystem, TripleBuffer);
        return TRUE;
    }

    *BlockNumber = 0;
    return FALSE;
}

/************************************************************************/

/**
 * @brief Finds a child inode within a directory by name.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Directory Pointer to the directory inode description.
 * @param Name Name of the entry to locate.
 * @param InodeIndex Receives the inode index when found.
 * @return TRUE if the entry exists, FALSE otherwise.
 */
BOOL FindInodeInDirectory(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Directory, LPCSTR Name, U32* InodeIndex) {
    U32 BlockCount;
    U32 BlockIndex;
    U32 NameLength;
    U8* BlockBuffer;
    BOOL Found;

    if (FileSystem == NULL || Directory == NULL || InodeIndex == NULL) return FALSE;
    if (STRING_EMPTY(Name)) return FALSE;

    if ((Directory->Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_DIRECTORY) return FALSE;

    NameLength = StringLength(Name);
    BlockCount = 0;
    Found = FALSE;

    if (FileSystem->BlockSize == 0) return FALSE;

    if (Directory->Size != 0) {
        BlockCount = (Directory->Size + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    }

    BlockBuffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (BlockBuffer == NULL) {
        ERROR(TEXT("Allocation failed size=%u"), FileSystem->BlockSize);
        return FALSE;
    }

    for (BlockIndex = 0; BlockIndex < BlockCount && Found == FALSE; BlockIndex++) {
        U32 BlockNumber;

        if (GetInodeBlockNumber(FileSystem, Directory, BlockIndex, &BlockNumber) == FALSE) {
            WARNING(TEXT("GetInodeBlockNumber failed index=%u"), BlockIndex);
            break;
        }
        if (BlockNumber == 0) continue;

        if (ReadBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
            WARNING(TEXT("ReadBlock failed block=%u"), BlockNumber);
            break;
        }

        U32 Offset = 0;
        while (Offset + sizeof(EXT2DIRECTORYENTRY) <= FileSystem->BlockSize) {
            LPEXT2DIRECTORYENTRY Entry = (LPEXT2DIRECTORYENTRY)(BlockBuffer + Offset);
            U32 EntryLength;

            EntryLength = Entry->RecordLength;
            if (EntryLength == 0) break;
            if (Offset + EntryLength > FileSystem->BlockSize) break;

            U32 EntryNameLength = (U32)Entry->NameLength;
            if (Entry->Inode != 0 && EntryNameLength == NameLength) {
                STR EntryName[MAX_FILE_NAME];
                U32 CopyLength = EntryNameLength;

                if (CopyLength >= MAX_FILE_NAME) {
                    CopyLength = MAX_FILE_NAME - 1;
                }

                MemorySet(EntryName, 0, sizeof(EntryName));
                MemoryCopy(EntryName, Entry->Name, (UINT)CopyLength);

                if (StringCompare(EntryName, Name) == 0) {
                    *InodeIndex = Entry->Inode;
                    Found = TRUE;
                    break;
                }
            }

            Offset += EntryLength;
        }
    }

    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

    return Found;
}

/************************************************************************/

/**
 * @brief Resolves a path to its inode by traversing directories.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Path UTF-8 path using '/' as separator.
 * @param Inode Receives the inode information on success.
 * @param InodeIndex Receives the inode index on success (may be NULL).
 * @return TRUE when the path is resolved, FALSE otherwise.
 */
BOOL ResolvePath(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex) {
    EXT2INODE CurrentInode;
    U32 CurrentIndex;
    U32 Offset;
    U32 Length;

    if (FileSystem == NULL || STRING_EMPTY(Path) || Inode == NULL || InodeIndex == NULL) return FALSE;

    if (ReadInode(FileSystem, EXT2_ROOT_INODE, &CurrentInode) == FALSE) {
        WARNING(TEXT("ReadInode root failed"));
        return FALSE;
    }
    CurrentIndex = EXT2_ROOT_INODE;

    Length = StringLength(Path);
    Offset = 0;

    while (Offset < Length) {
        STR Component[MAX_FILE_NAME];
        U32 ComponentLength;

        while (Offset < Length && Path[Offset] == PATH_SEP) {
            Offset++;
        }

        if (Offset >= Length) break;

        ComponentLength = 0;
        while ((Offset + ComponentLength) < Length && Path[Offset + ComponentLength] != PATH_SEP) {
            ComponentLength++;
        }

        if (ComponentLength == 0 || ComponentLength >= MAX_FILE_NAME) {
            WARNING(TEXT("Invalid component length=%u"), ComponentLength);
            return FALSE;
        }

        MemorySet(Component, 0, sizeof(Component));
        MemoryCopy(Component, Path + Offset, ComponentLength);

        if (FindInodeInDirectory(FileSystem, &CurrentInode, Component, &CurrentIndex) == FALSE) {
            WARNING(TEXT("Component not found component=%s"), Component);
            return FALSE;
        }

        if (ReadInode(FileSystem, CurrentIndex, &CurrentInode) == FALSE) {
            WARNING(TEXT("ReadInode failed index=%u component=%s"), CurrentIndex, Component);
            return FALSE;
        }

        Offset += ComponentLength;
    }

    MemoryCopy(Inode, &CurrentInode, sizeof(EXT2INODE));
    if (InodeIndex != NULL) {
        *InodeIndex = CurrentIndex;
    }

    return TRUE;
}

/**
 * @brief Allocates and initializes a new EXT2 filesystem structure.
 * @param Disk Pointer to the physical disk hosting the filesystem.
 * @return Newly allocated filesystem descriptor, or NULL on failure.
 */
