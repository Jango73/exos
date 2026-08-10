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

/**
 * @brief Checks whether a path contains wildcard characters.
 * @param Path Null-terminated path string to inspect.
 * @return TRUE if '*' or '?' is found in the path, FALSE otherwise.
 */
BOOL HasWildcard(LPCSTR Path) {
    if (STRING_EMPTY(Path)) return FALSE;

    if (StringFindChar(Path, '*') != NULL) return TRUE;
    if (StringFindChar(Path, '?') != NULL) return TRUE;

    return FALSE;
}

/************************************************************************/

/**
 * @brief Extracts the last component of a path.
 * @param Path Input path string that may contain separators.
 * @param Name Destination buffer receiving the extracted component.
 */
void ExtractBaseName(LPCSTR Path, LPSTR Name) {
    STR Buffer[MAX_PATH_NAME];
    LPSTR Slash;
    U32 Length;

    if (Name == NULL) return;

    Name[0] = STR_NULL;

    if (STRING_EMPTY(Path)) {
        StringCopy(Name, TEXT("/"));
        return;
    }

    StringCopy(Buffer, Path);

    Length = StringLength(Buffer);
    while (Length > 0 && Buffer[Length - 1] == PATH_SEP) {
        Buffer[Length - 1] = STR_NULL;
        Length--;
    }

    if (Length == 0) {
        StringCopy(Name, TEXT("/"));
        return;
    }

    Slash = StringFindCharR(Buffer, PATH_SEP);
    if (Slash != NULL) {
        StringCopy(Name, Slash + 1);
    } else {
        StringCopy(Name, Buffer);
    }
}

/************************************************************************/

/**
 * @brief Releases directory-specific buffers owned by a file handle.
 * @param File Directory handle whose cached resources must be freed.
 */
void ReleaseDirectoryResources(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;

    if (File == NULL) return;

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;

    if (File->DirectoryBlock != NULL) {
        if (FileSystem != NULL) {
            Ext2ReleaseBlockBuffer(FileSystem, File->DirectoryBlock);
        } else {
            KernelHeapFree(File->DirectoryBlock);
        }
        File->DirectoryBlock = NULL;
    }

    File->DirectoryBlockValid = FALSE;
}

/************************************************************************/

/**
 * @brief Compares a name against a wildcard pattern.
 * @param Name File name to evaluate.
 * @param Pattern Wildcard expression supporting '*' and '?'.
 * @return TRUE when the name matches the pattern, FALSE otherwise.
 */
BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern) {
    if (Pattern == NULL) return FALSE;
    if (Name == NULL) return FALSE;

    if (Pattern[0] == STR_NULL) return Name[0] == STR_NULL;

    if (Pattern[0] == '*') {
        while (*Pattern == '*') Pattern++;
        if (*Pattern == STR_NULL) return TRUE;

        while (*Name != STR_NULL) {
            if (MatchPattern(Name, Pattern)) return TRUE;
            Name++;
        }

        return MatchPattern(Name, Pattern);
    }

    if (Pattern[0] == '?') {
        if (*Name == STR_NULL) return FALSE;
        return MatchPattern(Name + 1, Pattern + 1);
    }

    if (*Name != *Pattern) return FALSE;

    if (*Name == STR_NULL) return TRUE;

    return MatchPattern(Name + 1, Pattern + 1);
}

/************************************************************************/

/**
 * @brief Locates and validates the inode for a directory path.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Path Directory path that should be resolved.
 * @param Inode Receives the inode data for the directory.
 * @param InodeIndex Receives the inode index when provided.
 * @return TRUE if the directory inode was found, FALSE otherwise.
 */
BOOL LoadDirectoryInode(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex) {
    STR Normalized[MAX_PATH_NAME];
    U32 Length;

    if (FileSystem == NULL || Inode == NULL) return FALSE;

    if (STRING_EMPTY(Path)) {
        if (ReadInode(FileSystem, EXT2_ROOT_INODE, Inode) == FALSE) return FALSE;
        if (InodeIndex != NULL) *InodeIndex = EXT2_ROOT_INODE;
        return (Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY;
    }

    StringCopy(Normalized, Path);

    Length = StringLength(Normalized);
    while (Length > 0 && Normalized[Length - 1] == PATH_SEP) {
        Normalized[Length - 1] = STR_NULL;
        Length--;
    }

    if (Length == 0) {
        if (ReadInode(FileSystem, EXT2_ROOT_INODE, Inode) == FALSE) return FALSE;
        if (InodeIndex != NULL) *InodeIndex = EXT2_ROOT_INODE;
    } else {
        if (ResolvePath(FileSystem, Normalized, Inode, InodeIndex) == FALSE) return FALSE;
    }

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_DIRECTORY) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Populates a FILE header from an EXT2 inode description.
 * @param File Target file handle to update.
 * @param Name Optional file name to assign.
 * @param Inode Source inode containing metadata.
 */
void FillFileHeaderFromInode(LPEXT2FILE File, LPCSTR Name, LPEXT2INODE Inode) {
    if (File == NULL || Inode == NULL) return;

    if (Name != NULL && Name[0] != STR_NULL) {
        StringCopy(File->Header.Name, Name);
    } else {
        File->Header.Name[0] = STR_NULL;
    }

    File->Header.Attributes = 0;

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if ((Inode->Mode & (EXT2_MODE_USER_WRITE | EXT2_MODE_GROUP_WRITE | EXT2_MODE_OTHER_WRITE)) == 0) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_REGULAR) {
        if (Inode->Mode & (EXT2_MODE_USER_EXECUTE | EXT2_MODE_GROUP_EXECUTE | EXT2_MODE_OTHER_EXECUTE)) {
            File->Header.Attributes |= FS_ATTR_EXECUTABLE;
        }
    }

    File->Header.SizeLow = Inode->Size;
    File->Header.SizeHigh = 0;

    MemorySet(&(File->Header.Creation), 0, sizeof(DATETIME));
    MemorySet(&(File->Header.Accessed), 0, sizeof(DATETIME));
    MemorySet(&(File->Header.Modified), 0, sizeof(DATETIME));
}

/************************************************************************/

/**
 * @brief Configures an EXT2 directory handle for enumeration or access.
 * @param File File object being configured.
 * @param FileSystem Owning file system instance.
 * @param Directory Inode describing the directory.
 * @param InodeIndex Index of the directory inode.
 * @param Enumerate TRUE to prepare for enumeration, FALSE otherwise.
 * @param Pattern Wildcard pattern used when enumerating entries.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupDirectoryHandle(
    LPEXT2FILE File,
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 InodeIndex,
    BOOL Enumerate,
    LPCSTR Pattern) {
    if (File == NULL || FileSystem == NULL || Directory == NULL) return FALSE;

    File->IsDirectory = TRUE;
    File->Enumerate = Enumerate;
    MemoryCopy(&(File->Inode), Directory, sizeof(EXT2_INODE));
    File->InodeIndex = InodeIndex;
    File->DirectoryBlockIndex = 0;
    File->DirectoryBlockOffset = 0;
    File->DirectoryBlockValid = FALSE;
    File->DirectoryBlock = NULL;
    File->Pattern[0] = STR_NULL;

    if (Pattern != NULL && Pattern[0] != STR_NULL) {
        StringCopy(File->Pattern, Pattern);
    } else {
        StringCopy(File->Pattern, TEXT("*"));
    }

    if (Enumerate) {
        if (FileSystem->BlockSize == 0) return FALSE;

        File->DirectoryBlock = (U8*)Ext2AcquireBlockBuffer(FileSystem);
        if (File->DirectoryBlock == NULL) return FALSE;

        if (LoadNextDirectoryEntry(File) == FALSE) {
            ReleaseDirectoryResources(File);
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Loads the next directory entry matching the current pattern.
 * @param File Directory handle being enumerated.
 * @return TRUE when an entry is loaded, FALSE when no more entries exist.
 */
BOOL LoadNextDirectoryEntry(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 BlockCount;

    if (File == NULL) return FALSE;

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return FALSE;

    if (FileSystem->BlockSize == 0) return FALSE;

    BlockCount = (File->Inode.Size + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    if (BlockCount == 0) {
        BlockCount = 1;
    }

    while (File->DirectoryBlockIndex < BlockCount) {
        if (File->DirectoryBlockValid == FALSE) {
            U32 BlockNumber;

            if (GetInodeBlockNumber(FileSystem, &(File->Inode), File->DirectoryBlockIndex, &BlockNumber) == FALSE) {
                return FALSE;
            }

            if (BlockNumber == 0) {
                File->DirectoryBlockIndex++;
                File->DirectoryBlockOffset = 0;
                File->DirectoryBlockValid = FALSE;
                continue;
            }

            if (ReadBlock(FileSystem, BlockNumber, File->DirectoryBlock) == FALSE) {
                return FALSE;
            }

            File->DirectoryBlockValid = TRUE;
            File->DirectoryBlockOffset = 0;
        }

        while (File->DirectoryBlockOffset + EXT2_DIR_ENTRY_HEADER_SIZE <= FileSystem->BlockSize) {
            U32 Offset;
            LPEXT2DIRECTORYENTRY Entry;
            U16 EntryLength;
            U8 NameLength;
            U32 NameLength32;
            STR EntryName[MAX_FILE_NAME];
            EXT2_INODE EntryInode;

            Offset = File->DirectoryBlockOffset;
            Entry = (LPEXT2DIRECTORYENTRY)(File->DirectoryBlock + Offset);
            EntryLength = Entry->RecordLength;
            NameLength = Entry->NameLength;
            NameLength32 = (U32)NameLength;

            if (EntryLength < EXT2_DIR_ENTRY_HEADER_SIZE) {
                File->DirectoryBlockOffset = FileSystem->BlockSize;
                break;
            }

            if (Offset + EntryLength > FileSystem->BlockSize) {
                File->DirectoryBlockOffset = FileSystem->BlockSize;
                break;
            }

            File->DirectoryBlockOffset += EntryLength;

            if (Entry->Inode == 0 || NameLength32 == 0) continue;

            if (NameLength32 >= MAX_FILE_NAME) {
                NameLength32 = MAX_FILE_NAME - 1;
            }
            NameLength = (U8)NameLength32;

            MemorySet(EntryName, 0, sizeof(EntryName));
            MemoryCopy(EntryName, Entry->Name, NameLength);

            if (MatchPattern(EntryName, File->Pattern) == FALSE) continue;

            if (ReadInode(FileSystem, Entry->Inode, &EntryInode) == FALSE) continue;

            FillFileHeaderFromInode(File, EntryName, &EntryInode);

            return TRUE;
        }

        File->DirectoryBlockIndex++;
        File->DirectoryBlockOffset = 0;
        File->DirectoryBlockValid = FALSE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Aligns a directory entry name length to the EXT2 record boundary.
 * @param Length Raw name length in bytes.
 * @return Length rounded up to the nearest valid directory record size.
 */
U32 AlignDirectoryNameLength(U32 Length) {
    return (Length + (EXT2_DIR_ENTRY_ALIGN - 1)) & ~(EXT2_DIR_ENTRY_ALIGN - 1);
}

/************************************************************************/

/**
 * @brief Writes the in-memory superblock back to disk.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL FlushSuperBlock(LPEXT2FILESYSTEM FileSystem) {
    U8 Buffer[SECTOR_SIZE * 2];

    if (FileSystem == NULL) return FALSE;

    MemorySet(Buffer, 0, sizeof(Buffer));
    MemoryCopy(Buffer, &(FileSystem->Super), sizeof(EXT2_SUPER));

    return WriteSectors(FileSystem, 2, 2, Buffer);
}

/************************************************************************/

/**
 * @brief Persists a block group descriptor to disk.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param GroupIndex Index of the block group descriptor to flush.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL FlushGroupDescriptor(LPEXT2FILESYSTEM FileSystem, U32 GroupIndex) {
    U32 DescriptorsPerBlock;
    U32 TargetBlock;
    U32 OffsetInBlock;
    U8* Buffer;

    if (FileSystem == NULL) return FALSE;
    if (FileSystem->Groups == NULL) return FALSE;
    if (GroupIndex >= FileSystem->GroupCount) return FALSE;

    if (FileSystem->BlockSize == 0) return FALSE;

    DescriptorsPerBlock = FileSystem->BlockSize / sizeof(EXT2_BLOCK_GROUP);
    if (DescriptorsPerBlock == 0) return FALSE;

    TargetBlock = FileSystem->Super.FirstDataBlock + 1 + (GroupIndex / DescriptorsPerBlock);
    OffsetInBlock = (GroupIndex % DescriptorsPerBlock) * sizeof(EXT2_BLOCK_GROUP);

    Buffer = (U8*)Ext2AcquireBlockBuffer(FileSystem);
    if (Buffer == NULL) return FALSE;

    if (ReadBlock(FileSystem, TargetBlock, Buffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Buffer);
        return FALSE;
    }

    MemoryCopy(Buffer + OffsetInBlock, &(FileSystem->Groups[GroupIndex]), sizeof(EXT2_BLOCK_GROUP));

    if (WriteBlock(FileSystem, TargetBlock, Buffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, Buffer);
        return FALSE;
    }

    Ext2ReleaseBlockBuffer(FileSystem, Buffer);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Writes an inode structure back to disk.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param InodeIndex Index of the inode to update.
 * @param Inode Source inode data to persist.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL WriteInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode) {
    U32 OffsetInBlock;
    U8* BlockBuffer;
    U32 CopySize;

    if (FileSystem == NULL || Inode == NULL) return FALSE;
    if (PrepareInodeBlockAccess(FileSystem, InodeIndex, &BlockBuffer, &OffsetInBlock, &CopySize) == FALSE) return FALSE;

    MemoryCopy(BlockBuffer + OffsetInBlock, Inode, CopySize);

    if (WriteBlock(FileSystem,
                   FileSystem->Groups[(InodeIndex - 1) / FileSystem->Super.InodesPerGroup].InodeTable +
                       (((InodeIndex - 1) % FileSystem->Super.InodesPerGroup) / FileSystem->InodesPerBlock),
                   BlockBuffer) == FALSE) {
        Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);
        return FALSE;
    }

    Ext2ReleaseBlockBuffer(FileSystem, BlockBuffer);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocates a free data block and marks it as used.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param BlockNumber Receives the allocated block number on success.
 * @return TRUE when a block is allocated, FALSE otherwise.
 */
