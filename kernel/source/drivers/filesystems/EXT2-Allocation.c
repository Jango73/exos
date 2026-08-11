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

#include "drivers/filesystems/EXT2-Private.h"

/************************************************************************/

BOOL AllocateBlock(LPEXT2FILESYSTEM FileSystem, U32* BlockNumber) {
    U8* Bitmap;
    U32 GroupIndex;
    U32 BitsPerBlock;

    if (FileSystem == NULL || BlockNumber == NULL) return FALSE;
    if (FileSystem->BlockSize == 0) return FALSE;
    if (FileSystem->Groups == NULL) return FALSE;

    Bitmap = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (Bitmap == NULL) return FALSE;

    BitsPerBlock = FileSystem->BlockSize * 8;

    for (GroupIndex = 0; GroupIndex < FileSystem->GroupCount; GroupIndex++) {
        LPEXT2BLOCKGROUP Group = &(FileSystem->Groups[GroupIndex]);
        U32 BitIndex;

        if (Group->FreeBlocksCount == 0) continue;
        if (Group->BlockBitmap == 0) continue;

        if (ReadBlock(FileSystem, Group->BlockBitmap, Bitmap) == FALSE) continue;

        for (BitIndex = 0; BitIndex < BitsPerBlock && BitIndex < FileSystem->Super.BlocksPerGroup; BitIndex++) {
            U32 ByteIndex = BitIndex / 8;
            U8 Mask = 1 << (BitIndex % 8);

            if (Bitmap[ByteIndex] & Mask) continue;

            Bitmap[ByteIndex] |= Mask;

            if (WriteBlock(FileSystem, Group->BlockBitmap, Bitmap) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            Group->FreeBlocksCount--;
            FileSystem->Super.FreeBlocksCount--;

            if (FlushGroupDescriptor(FileSystem, GroupIndex) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            if (FlushSuperBlock(FileSystem) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            {
                U8* Zero;
                U32 AbsoluteBlock;

                AbsoluteBlock = FileSystem->Super.FirstDataBlock +
                    (GroupIndex * FileSystem->Super.BlocksPerGroup) + BitIndex;

                Zero = (U8*)Ext2AcquireBlockBuffer(FileSystem);
                if (Zero == NULL) {
                    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                    return FALSE;
                }

                MemorySet(Zero, 0, FileSystem->BlockSize);

                if (WriteBlock(FileSystem, AbsoluteBlock, Zero) == FALSE) {
                    Ext2ReleaseBlockBuffer(FileSystem, Zero);
                    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                    FreeBlock(FileSystem, AbsoluteBlock);
                    return FALSE;
                }

                Ext2ReleaseBlockBuffer(FileSystem, Zero);

                *BlockNumber = AbsoluteBlock;
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return TRUE;
            }
        }
    }

    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);

    return FALSE;
}

/************************************************************************/

/**
 * @brief Releases a data block back to the free list.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param BlockNumber Block number to free.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL FreeBlock(LPEXT2FILESYSTEM FileSystem, U32 BlockNumber) {
    U8* Bitmap;
    U32 GroupIndex;
    U32 BitIndex;
    U32 RelativeBlock;

    if (FileSystem == NULL) return FALSE;
    if (BlockNumber == 0) return FALSE;
    if (FileSystem->Groups == NULL) return FALSE;

    if (BlockNumber < FileSystem->Super.FirstDataBlock) return FALSE;

    RelativeBlock = BlockNumber - FileSystem->Super.FirstDataBlock;
    GroupIndex = RelativeBlock / FileSystem->Super.BlocksPerGroup;
    if (GroupIndex >= FileSystem->GroupCount) return FALSE;

    BitIndex = RelativeBlock % FileSystem->Super.BlocksPerGroup;

    Bitmap = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (Bitmap == NULL) return FALSE;

    if (ReadBlock(FileSystem, FileSystem->Groups[GroupIndex].BlockBitmap, Bitmap) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
        return FALSE;
    }

    {
        U32 ByteIndex = BitIndex / 8;
        U8 Mask = 1 << (BitIndex % 8);

        if ((Bitmap[ByteIndex] & Mask) == 0) {
            Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
            return TRUE;
        }

        Bitmap[ByteIndex] &= (U8)~Mask;
    }

    if (WriteBlock(FileSystem, FileSystem->Groups[GroupIndex].BlockBitmap, Bitmap) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
        return FALSE;
    }

    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);

    FileSystem->Groups[GroupIndex].FreeBlocksCount++;
    FileSystem->Super.FreeBlocksCount++;

    if (FlushGroupDescriptor(FileSystem, GroupIndex) == FALSE) return FALSE;
    if (FlushSuperBlock(FileSystem) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocates a free inode and initializes its metadata.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Directory TRUE when allocating a directory inode.
 * @param InodeIndex Receives the index of the allocated inode.
 * @param Inode Receives the initialized inode contents.
 * @param GroupIndexOut Optionally receives the block group index.
 * @return TRUE when an inode is allocated, FALSE otherwise.
 */
BOOL AllocateInode(
    LPEXT2FILESYSTEM FileSystem, BOOL Directory, U32* InodeIndex, LPEXT2INODE Inode, U32* GroupIndexOut) {
    U8* Bitmap;
    U32 GroupIndex;
    U32 BitsPerBitmap;

    if (FileSystem == NULL || InodeIndex == NULL || Inode == NULL) return FALSE;
    if (FileSystem->BlockSize == 0) return FALSE;
    if (FileSystem->Groups == NULL) return FALSE;

    Bitmap = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (Bitmap == NULL) return FALSE;

    BitsPerBitmap = FileSystem->BlockSize * 8;

    for (GroupIndex = 0; GroupIndex < FileSystem->GroupCount; GroupIndex++) {
        LPEXT2BLOCKGROUP Group = &(FileSystem->Groups[GroupIndex]);
        U32 BitIndex;

        if (Group->FreeInodesCount == 0) continue;
        if (Group->InodeBitmap == 0) continue;

        if (ReadBlock(FileSystem, Group->InodeBitmap, Bitmap) == FALSE) continue;

        for (BitIndex = 0; BitIndex < BitsPerBitmap && BitIndex < FileSystem->Super.InodesPerGroup; BitIndex++) {
            U32 ByteIndex = BitIndex / 8;
            U8 Mask = 1 << (BitIndex % 8);

            if (Bitmap[ByteIndex] & Mask) continue;

            Bitmap[ByteIndex] |= Mask;

            if (WriteBlock(FileSystem, Group->InodeBitmap, Bitmap) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            Group->FreeInodesCount--;
            if (Directory) {
                Group->UsedDirsCount++;
            }

            FileSystem->Super.FreeInodesCount--;

            if (FlushGroupDescriptor(FileSystem, GroupIndex) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            if (FlushSuperBlock(FileSystem) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
                return FALSE;
            }

            *InodeIndex = (GroupIndex * FileSystem->Super.InodesPerGroup) + BitIndex + 1;
            if (GroupIndexOut != NULL) {
                *GroupIndexOut = GroupIndex;
            }

            MemorySet(Inode, 0, sizeof(EXT2_INODE));
            Inode->Mode = Directory ? (EXT2_MODE_DIRECTORY | 0x01ED) : (EXT2_MODE_REGULAR | 0x01A4);
            Inode->LinksCount = Directory ? 2 : 1;

            Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
            return TRUE;
        }
    }

    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);

    return FALSE;
}

/************************************************************************/

/**
 * @brief Releases an inode and updates allocation metadata.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param InodeIndex Index of the inode to release.
 * @param Directory TRUE if the inode represents a directory.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL FreeInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, BOOL Directory) {
    U8* Bitmap;
    U32 GroupIndex;
    U32 BitIndex;

    if (FileSystem == NULL) return FALSE;
    if (InodeIndex == 0) return FALSE;
    if (FileSystem->Groups == NULL) return FALSE;

    Bitmap = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (Bitmap == NULL) return FALSE;

    GroupIndex = (InodeIndex - 1) / FileSystem->Super.InodesPerGroup;
    if (GroupIndex >= FileSystem->GroupCount) {
        Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
        return FALSE;
    }

    BitIndex = (InodeIndex - 1) % FileSystem->Super.InodesPerGroup;

    if (ReadBlock(FileSystem, FileSystem->Groups[GroupIndex].InodeBitmap, Bitmap) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
        return FALSE;
    }

    {
        U32 ByteIndex = BitIndex / 8;
        U8 Mask = 1 << (BitIndex % 8);

        if ((Bitmap[ByteIndex] & Mask) == 0) {
            Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
            return TRUE;
        }

        Bitmap[ByteIndex] &= (U8)~Mask;
    }

    if (WriteBlock(FileSystem, FileSystem->Groups[GroupIndex].InodeBitmap, Bitmap) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Bitmap);
        return FALSE;
    }

    Ext2ReleaseBlockBuffer(FileSystem, Bitmap);

    FileSystem->Groups[GroupIndex].FreeInodesCount++;
    if (Directory && FileSystem->Groups[GroupIndex].UsedDirsCount > 0) {
        FileSystem->Groups[GroupIndex].UsedDirsCount--;
    }

    FileSystem->Super.FreeInodesCount++;

    if (FlushGroupDescriptor(FileSystem, GroupIndex) == FALSE) return FALSE;
    if (FlushSuperBlock(FileSystem) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Releases all blocks referenced by an inode and resets its size.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Inode Inode to truncate.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL TruncateInode(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode) {
    U32 Index;

    if (FileSystem == NULL || Inode == NULL) return FALSE;

    for (Index = 0; Index < EXT2_DIRECT_BLOCKS; Index++) {
        if (Inode->Block[Index] != 0) {
            FreeBlock(FileSystem, Inode->Block[Index]);
            Inode->Block[Index] = 0;
        }
    }

    // Free single, double and triple indirect trees if present
    if (Inode->Block[EXT2_DIRECT_BLOCKS] != 0) {
        FreeIndirectTree(FileSystem, Inode->Block[EXT2_DIRECT_BLOCKS], 1);
        Inode->Block[EXT2_DIRECT_BLOCKS] = 0;
    }

    if (Inode->Block[EXT2_DIRECT_BLOCKS + 1] != 0) {
        FreeIndirectTree(FileSystem, Inode->Block[EXT2_DIRECT_BLOCKS + 1], 2);
        Inode->Block[EXT2_DIRECT_BLOCKS + 1] = 0;
    }

    if (Inode->Block[EXT2_DIRECT_BLOCKS + 2] != 0) {
        FreeIndirectTree(FileSystem, Inode->Block[EXT2_DIRECT_BLOCKS + 2], 3);
        Inode->Block[EXT2_DIRECT_BLOCKS + 2] = 0;
    }

    Inode->Size = 0;
    Inode->Blocks = 0;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Recursively frees an indirect block tree.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param BlockNumber Block number of the indirect root to free.
 * @param Depth Indirection depth (1=single, 2=double, 3=triple).
 * @return TRUE on success, FALSE otherwise.
 */
BOOL FreeIndirectTree(LPEXT2FILESYSTEM FileSystem, U32 BlockNumber, U32 Depth) {
    U32 EntriesPerBlock;
    U32 Index;
    U32* Buffer;

    if (FileSystem == NULL) return FALSE;
    if (BlockNumber == 0) return TRUE;
    if (FileSystem->BlockSize == 0) return FALSE;

    EntriesPerBlock = FileSystem->BlockSize / sizeof(U32);
    if (EntriesPerBlock == 0) return FALSE;

    Buffer = (U32*)Ext2AcquireBlockBuffer(FileSystem);
    if (Buffer == NULL) return FALSE;

    if (ReadBlock(FileSystem, BlockNumber, Buffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Buffer);
        return FALSE;
    }

    if (Depth > 1) {
        for (Index = 0; Index < EntriesPerBlock; Index++) {
            if (Buffer[Index] != 0) {
                FreeIndirectTree(FileSystem, Buffer[Index], Depth - 1);
            }
        }
    } else {
        for (Index = 0; Index < EntriesPerBlock; Index++) {
            if (Buffer[Index] != 0) {
                FreeBlock(FileSystem, Buffer[Index]);
            }
        }
    }

    Ext2ReleaseBlockBuffer(FileSystem, Buffer);
    FreeBlock(FileSystem, BlockNumber);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Inserts a directory entry into a directory inode.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Directory Directory inode that will receive the entry.
 * @param DirectoryIndex Index of the directory inode in storage.
 * @param ChildInodeIndex Index of the child inode to reference.
 * @param Name Name of the entry to create.
 * @param FileType EXT2 file type identifier for the entry.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL AddDirectoryEntry(
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 DirectoryIndex,
    U32 ChildInodeIndex,
    LPCSTR Name,
    U8 FileType) {
    U32 NameLength;
    U32 EntrySize;
    U32 BlockIndex;

    if (FileSystem == NULL || Directory == NULL || Name == NULL) return FALSE;
    if (ChildInodeIndex == 0) return FALSE;

    NameLength = StringLength(Name);
    if (NameLength == 0) return FALSE;
    if (NameLength >= EXT2_NAME_MAX) {
        NameLength = EXT2_NAME_MAX - 1;
    }

    EntrySize = EXT2_DIR_ENTRY_HEADER_SIZE + AlignDirectoryNameLength(NameLength);
    if (EntrySize > FileSystem->BlockSize) return FALSE;

    for (BlockIndex = 0; BlockIndex < EXT2_DIRECT_BLOCKS; BlockIndex++) {
        U32 BlockNumber;
        U8* BlockBuffer;
        U32 Offset;

        BlockNumber = Directory->Block[BlockIndex];

        if (BlockNumber == 0) {
            if (AllocateBlock(FileSystem, &BlockNumber) == FALSE) return FALSE;

            Directory->Block[BlockIndex] = BlockNumber;
            Directory->Size = (BlockIndex + 1) * FileSystem->BlockSize;
            Directory->Blocks += FileSystem->BlockSize / 512;

            BlockBuffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
            if (BlockBuffer == NULL) return FALSE;

            MemorySet(BlockBuffer, 0, FileSystem->BlockSize);

            {
                LPEXT2DIRECTORYENTRY Entry = (LPEXT2DIRECTORYENTRY)BlockBuffer;
                Entry->Inode = ChildInodeIndex;
                Entry->RecordLength = FileSystem->BlockSize;
                Entry->NameLength = (U8)NameLength;
                Entry->FileType = FileType;
                MemoryCopy(Entry->Name, Name, NameLength);
            }

            if (WriteBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
                Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
                return FALSE;
            }

            Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

            return WriteInode(FileSystem, DirectoryIndex, Directory);
        }

        BlockBuffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
        if (BlockBuffer == NULL) return FALSE;

        if (ReadBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
            Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
            return FALSE;
        }

        Offset = 0;

        while (Offset + EXT2_DIR_ENTRY_HEADER_SIZE <= FileSystem->BlockSize) {
            LPEXT2DIRECTORYENTRY Entry;
            U32 ActualSize;
            U32 Remaining;

            Entry = (LPEXT2DIRECTORYENTRY)(BlockBuffer + Offset);

            if (Entry->RecordLength < EXT2_DIR_ENTRY_HEADER_SIZE) break;
            if (Offset + Entry->RecordLength > FileSystem->BlockSize) break;

            if (Entry->Inode == 0) {
                if (Entry->RecordLength >= EntrySize) {
                    Entry->Inode = ChildInodeIndex;
                    Entry->NameLength = (U8)NameLength;
                    Entry->FileType = FileType;
                    MemorySet(Entry->Name, 0, Entry->RecordLength - EXT2_DIR_ENTRY_HEADER_SIZE);
                    MemoryCopy(Entry->Name, Name, NameLength);

                    if (WriteBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
                        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
                        return FALSE;
                    }

                    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

                    return WriteInode(FileSystem, DirectoryIndex, Directory);
                }
            }

            ActualSize = EXT2_DIR_ENTRY_HEADER_SIZE + AlignDirectoryNameLength(Entry->NameLength);
            if (ActualSize < Entry->RecordLength) {
                Remaining = Entry->RecordLength - ActualSize;

                if (Remaining >= EntrySize) {
                    LPEXT2DIRECTORYENTRY NewEntry;

                    Entry->RecordLength = (U16)ActualSize;
                    NewEntry = (LPEXT2DIRECTORYENTRY)(BlockBuffer + Offset + ActualSize);

                    NewEntry->Inode = ChildInodeIndex;
                    NewEntry->RecordLength = (U16)Remaining;
                    NewEntry->NameLength = (U8)NameLength;
                    NewEntry->FileType = FileType;
                    MemorySet(NewEntry->Name, 0, Remaining - EXT2_DIR_ENTRY_HEADER_SIZE);
                    MemoryCopy(NewEntry->Name, Name, NameLength);

                    if (WriteBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
                        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
                        return FALSE;
                    }

                    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

                    return WriteInode(FileSystem, DirectoryIndex, Directory);
                }
            }

            Offset += Entry->RecordLength;
        }

        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Creates a new directory under a parent inode.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Parent Parent directory inode in memory.
 * @param ParentIndex Index of the parent inode in storage.
 * @param Name Name of the directory to create.
 * @param NewInodeIndex Optionally receives the created inode index.
 * @param NewInode Optionally receives the created inode contents.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL CreateDirectoryInternal(
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Parent,
    U32 ParentIndex,
    LPCSTR Name,
    U32* NewInodeIndex,
    LPEXT2INODE NewInode) {
    U32 InodeIndex;
    EXT2_INODE DirectoryInode;
    U32 BlockNumber;
    U8* BlockBuffer;

    if (FileSystem == NULL || Parent == NULL || Name == NULL) return FALSE;

    if (AllocateInode(FileSystem, TRUE, &InodeIndex, &DirectoryInode, NULL) == FALSE) return FALSE;

    if (AllocateBlock(FileSystem, &BlockNumber) == FALSE) {
        FreeInode(FileSystem, InodeIndex, TRUE);
        return FALSE;
    }

    BlockBuffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (BlockBuffer == NULL) {
        FreeBlock(FileSystem, BlockNumber);
        FreeInode(FileSystem, InodeIndex, TRUE);
        return FALSE;
    }

    MemorySet(BlockBuffer, 0, FileSystem->BlockSize);

    {
        LPEXT2DIRECTORYENTRY Dot = (LPEXT2DIRECTORYENTRY)BlockBuffer;
        LPEXT2DIRECTORYENTRY DotDot;
        U32 DotSize = EXT2_DIR_ENTRY_HEADER_SIZE + AlignDirectoryNameLength(1);
        U32 DotDotSize = FileSystem->BlockSize - DotSize;

        Dot->Inode = InodeIndex;
        Dot->RecordLength = (U16)DotSize;
        Dot->NameLength = 1;
        Dot->FileType = EXT2_FT_DIR;
        Dot->Name[0] = '.';

        DotDot = (LPEXT2DIRECTORYENTRY)(BlockBuffer + DotSize);
        DotDot->Inode = ParentIndex;
        DotDot->RecordLength = (U16)DotDotSize;
        DotDot->NameLength = 2;
        DotDot->FileType = EXT2_FT_DIR;
        DotDot->Name[0] = '.';
        DotDot->Name[1] = '.';
    }

    if (WriteBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
        FreeBlock(FileSystem, BlockNumber);
        FreeInode(FileSystem, InodeIndex, TRUE);
        return FALSE;
    }

    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

    DirectoryInode.Block[0] = BlockNumber;
    DirectoryInode.Size = FileSystem->BlockSize;
    DirectoryInode.Blocks = FileSystem->BlockSize / 512;
    DirectoryInode.LinksCount = 2;

    if (WriteInode(FileSystem, InodeIndex, &DirectoryInode) == FALSE) {
        FreeBlock(FileSystem, BlockNumber);
        FreeInode(FileSystem, InodeIndex, TRUE);
        return FALSE;
    }

    if (AddDirectoryEntry(FileSystem, Parent, ParentIndex, InodeIndex, Name, EXT2_FT_DIR) == FALSE) {
        FreeBlock(FileSystem, BlockNumber);
        FreeInode(FileSystem, InodeIndex, TRUE);
        return FALSE;
    }

    Parent->LinksCount++;
    if (WriteInode(FileSystem, ParentIndex, Parent) == FALSE) {
        return FALSE;
    }

    if (NewInode != NULL) {
        MemoryCopy(NewInode, &DirectoryInode, sizeof(EXT2_INODE));
    }

    if (NewInodeIndex != NULL) {
        *NewInodeIndex = InodeIndex;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Ensures that the parent directory of a path exists.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Path Full path whose parent should be located or created.
 * @param Parent Receives the parent directory inode.
 * @param ParentIndex Receives the parent inode index.
 * @param FinalComponent Receives the last component of the path.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL EnsureParentDirectory(
    LPEXT2FILESYSTEM FileSystem,
    LPCSTR Path,
    LPEXT2INODE Parent,
    U32* ParentIndex,
    LPSTR FinalComponent) {
    STR Temp[MAX_PATH_NAME];
    STR Component[MAX_FILE_NAME];
    LPSTR Slash;
    U32 CurrentIndex;
    EXT2_INODE CurrentInode;
    U32 Offset;
    U32 Length;

    if (FileSystem == NULL || Path == NULL || Parent == NULL || ParentIndex == NULL || FinalComponent == NULL) {
        return FALSE;
    }

    if (ReadInode(FileSystem, EXT2_ROOT_INODE, &CurrentInode) == FALSE) return FALSE;
    CurrentIndex = EXT2_ROOT_INODE;

    StringCopy(Temp, Path);

    Length = StringLength(Temp);
    while (Length > 1 && Temp[Length - 1] == PATH_SEP) {
        Temp[Length - 1] = STR_NULL;
        Length--;
    }

    Slash = StringFindCharR(Temp, PATH_SEP);
    if (Slash != NULL) {
        StringCopy(FinalComponent, Slash + 1);
        *Slash = STR_NULL;
    } else {
        StringCopy(FinalComponent, Temp);
        Temp[0] = STR_NULL;
    }

    if (FinalComponent[0] == STR_NULL) return FALSE;
    if (StringLength(FinalComponent) >= MAX_FILE_NAME) return FALSE;

    Offset = 0;
    Length = StringLength(Temp);

    while (Offset < Length) {
        U32 ComponentLength = 0;

        while (Offset < Length && Temp[Offset] == PATH_SEP) {
            Offset++;
        }

        if (Offset >= Length) break;

        while ((Offset + ComponentLength) < Length && Temp[Offset + ComponentLength] != PATH_SEP) {
            ComponentLength++;
        }

        if (ComponentLength == 0 || ComponentLength >= MAX_FILE_NAME) {
            return FALSE;
        }

        MemorySet(Component, 0, sizeof(Component));
        MemoryCopy(Component, Temp + Offset, ComponentLength);

        {
            U32 NextIndex;
            EXT2_INODE NextInode;

            if (FindInodeInDirectory(FileSystem, &CurrentInode, Component, &NextIndex) == FALSE) {
                if (CreateDirectoryInternal(FileSystem, &CurrentInode, CurrentIndex, Component, &NextIndex, &NextInode) == FALSE) {
                    return FALSE;
                }

                MemoryCopy(&CurrentInode, &NextInode, sizeof(EXT2_INODE));
                CurrentIndex = NextIndex;
            } else {
                if (ReadInode(FileSystem, NextIndex, &NextInode) == FALSE) {
                    return FALSE;
                }

                if ((NextInode.Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_DIRECTORY) {
                    return FALSE;
                }

                MemoryCopy(&CurrentInode, &NextInode, sizeof(EXT2_INODE));
                CurrentIndex = NextIndex;
            }
        }

        Offset += ComponentLength;
    }

    MemoryCopy(Parent, &CurrentInode, sizeof(EXT2_INODE));
    *ParentIndex = CurrentIndex;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Creates a file or directory node represented by FILE_INFO.
 * @param Info Kernel-provided file information structure.
 * @param Directory TRUE to create a directory, FALSE for a file.
 * @return Driver error code indicating the operation result.
 */
U32 CreateNode(LPFILE_INFO Info, BOOL Directory) {
    LPEXT2FILESYSTEM FileSystem;
    EXT2_INODE ParentInode;
    U32 ParentIndex;
    STR FinalComponent[MAX_FILE_NAME];
    U32 ExistingIndex;
    EXT2_INODE ExistingInode;

    if (Info == NULL) return DF_RETURN_BAD_PARAMETER;

    FileSystem = (LPEXT2FILESYSTEM)Info->FileSystem;
    if (FileSystem == NULL) return DF_RETURN_BAD_PARAMETER;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    if (EnsureParentDirectory(FileSystem, Info->Name, &ParentInode, &ParentIndex, FinalComponent) == FALSE) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_GENERIC;
    }

    if (FindInodeInDirectory(FileSystem, &ParentInode, FinalComponent, &ExistingIndex)) {
        if (ReadInode(FileSystem, ExistingIndex, &ExistingInode) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_INPUT_OUTPUT;
        }

        if (Directory && (ExistingInode.Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_SUCCESS;
        }

        if (!Directory && (ExistingInode.Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_REGULAR) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_SUCCESS;
        }

        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_GENERIC;
    }

    if (Directory) {
        if (CreateDirectoryInternal(FileSystem, &ParentInode, ParentIndex, FinalComponent, NULL, NULL) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_GENERIC;
        }
    } else {
        U32 NewInodeIndex;
        EXT2_INODE NewInode;

        if (AllocateInode(FileSystem, FALSE, &NewInodeIndex, &NewInode, NULL) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_GENERIC;
        }

        if (AddDirectoryEntry(FileSystem, &ParentInode, ParentIndex, NewInodeIndex, FinalComponent, EXT2_FT_REG_FILE) == FALSE) {
            FreeInode(FileSystem, NewInodeIndex, FALSE);
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_GENERIC;
        }

        if (WriteInode(FileSystem, NewInodeIndex, &NewInode) == FALSE) {
            FreeInode(FileSystem, NewInodeIndex, FALSE);
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_GENERIC;
        }
    }

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

DRIVER DATA_SECTION EXT2Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Jango73",
    .Product = "EXT2 File System",
    .Alias = "ext2",
    .Command = EXT2Commands};

/************************************************************************/

/**
 * @brief Retrieves the EXT2 driver descriptor.
 * @return Pointer to the EXT2 driver.
 */
LPDRIVER EXT2GetDriver(void) {
    return &EXT2Driver;
}

/************************************************************************/

/**
 * @brief Reads raw sectors relative to the partition start.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Sector Sector index relative to the partition start.
 * @param Count Number of sectors to read.
 * @param Buffer Destination buffer for the read data.
 * @return TRUE on success, FALSE otherwise.
 */
