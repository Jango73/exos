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

static LPEXT2FILESYSTEM NewEXT2FileSystem(LPSTORAGE_UNIT Disk) {
    LPEXT2FILESYSTEM FileSystem;

    FileSystem = (LPEXT2FILESYSTEM)KernelHeapAlloc(sizeof(EXT2FILESYSTEM));
    if (FileSystem == NULL) return NULL;

    MemorySet(FileSystem, 0, sizeof(EXT2FILESYSTEM));

    FileSystem->Header.TypeID = KOID_FILESYSTEM;
    FileSystem->Header.References = 1;
    FileSystem->Header.Next = NULL;
    FileSystem->Header.Prev = NULL;
    FileSystem->Header.Driver = &EXT2Driver;
    FileSystem->Header.StorageUnit = Disk;
    FileSystem->Disk = Disk;
    FileSystem->Groups = NULL;
    FileSystem->GroupCount = 0;
    FileSystem->PartitionStart = 0;
    FileSystem->PartitionSize = 0;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;
    FileSystem->SectorsPerBlock = 0;
    FileSystem->InodeSize = 0;
    FileSystem->InodesPerBlock = 0;
    FileSystem->IOBuffer = NULL;

    InitMutex(&(FileSystem->Header.Mutex));
    InitMutex(&(FileSystem->FilesMutex));

    return FileSystem;
}

/************************************************************************/

/**
 * @brief Allocates a new EXT2 file handle.
 * @param FileSystem Owning EXT2 filesystem instance.
 * @return Newly allocated file handle, or NULL on failure.
 */
static LPEXT2FILE NewEXT2File(LPEXT2FILESYSTEM FileSystem) {
    LPEXT2FILE File;

    File = (LPEXT2FILE)KernelHeapAlloc(sizeof(EXT2FILE));
    if (File == NULL) return NULL;

    MemorySet(File, 0, sizeof(EXT2FILE));

    File->Header.TypeID = KOID_FILE;
    File->Header.References = 1;
    File->Header.Next = NULL;
    File->Header.Prev = NULL;
    File->Header.FileSystem = (LPFILESYSTEM)FileSystem;

    InitMutex(&(File->Header.Mutex));
    InitSecurity(&(File->Header.Security));

    File->InodeIndex = 0;
    File->DirectoryBlockIndex = 0;
    File->DirectoryBlockOffset = 0;
    File->DirectoryBlock = NULL;
    File->DirectoryBlockValid = FALSE;

    return File;
}

/**
 * @brief Initializes the EXT2 driver when it is loaded by the kernel.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Opens a file from the EXT2 filesystem.
 * @param Info File open parameters provided by the kernel.
 * @return Pointer to the opened file object, or NULL on failure.
 */
static LPEXT2FILE OpenFile(LPFILE_INFO Info) {
    LPEXT2FILESYSTEM FileSystem;
    LPEXT2FILE File;
    BOOL Wildcard;

    if (Info == NULL || STRING_EMPTY(Info->Name)) return NULL;

    FileSystem = (LPEXT2FILESYSTEM)Info->FileSystem;
    if (FileSystem == NULL) {
        ERROR(TEXT("EXT2 missing filesystem"));
        return NULL;
    }

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    Wildcard = HasWildcard(Info->Name);

    if (Wildcard) {
        STR DirectoryPath[MAX_PATH_NAME];
        STR Pattern[MAX_FILE_NAME];
        LPSTR Slash;
        EXT2INODE DirectoryInode;
        U32 DirectoryIndex;

        StringCopy(DirectoryPath, Info->Name);
        Slash = StringFindCharR(DirectoryPath, PATH_SEP);

        if (Slash != NULL) {
            StringCopy(Pattern, Slash + 1);
            *Slash = STR_NULL;
        } else {
            DirectoryPath[0] = STR_NULL;
            StringCopy(Pattern, Info->Name);
        }

        if (LoadDirectoryInode(FileSystem, DirectoryPath, &DirectoryInode, &DirectoryIndex) == FALSE) {
            WARNING(TEXT("EXT2 load folder inode failed path=%s"), DirectoryPath);
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        File = NewEXT2File(FileSystem);
        if (File == NULL) {
            ERROR(TEXT("EXT2 allocation failed for file handle"));
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        if (SetupDirectoryHandle(File, FileSystem, &DirectoryInode, DirectoryIndex, TRUE, Pattern) == FALSE) {
            ReleaseDirectoryResources(File);
            KernelHeapFree(File);
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        File->Header.OpenFlags = Info->Flags;

        UnlockMutex(&(FileSystem->FilesMutex));

        return File;
    }

    {
        EXT2INODE Inode;
        U32 InodeIndex;

        if (ResolvePath(FileSystem, Info->Name, &Inode, &InodeIndex) == FALSE) {
            if (Info->Flags & FILE_OPEN_CREATE_ALWAYS) {
                UnlockMutex(&(FileSystem->FilesMutex));

                if (CreateNode(Info, FALSE) != DF_RETURN_SUCCESS) {
                    WARNING(TEXT("EXT2 create failed name=%s"), Info->Name);
                    return NULL;
                }

                LockMutex(&(FileSystem->FilesMutex), INFINITY);

                if (ResolvePath(FileSystem, Info->Name, &Inode, &InodeIndex) == FALSE) {
                    WARNING(TEXT("EXT2 resolve after create failed name=%s"), Info->Name);
                    UnlockMutex(&(FileSystem->FilesMutex));
                    return NULL;
                }
            } else {
                WARNING(TEXT("EXT2 resolve failed name=%s"), Info->Name);
                UnlockMutex(&(FileSystem->FilesMutex));
                return NULL;
            }
        }

        File = NewEXT2File(FileSystem);
        if (File == NULL) {
            ERROR(TEXT("EXT2 allocation failed for file handle"));
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        MemoryCopy(&(File->Inode), &Inode, sizeof(EXT2INODE));
        File->InodeIndex = InodeIndex;

        if ((Inode.Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY) {
            if (SetupDirectoryHandle(File, FileSystem, &Inode, InodeIndex, FALSE, NULL) == FALSE) {
                ReleaseDirectoryResources(File);
                KernelHeapFree(File);
                UnlockMutex(&(FileSystem->FilesMutex));
                return NULL;
            }

            {
                STR BaseName[MAX_FILE_NAME];
                ExtractBaseName(Info->Name, BaseName);
                FillFileHeaderFromInode(File, BaseName, &Inode);
            }

            File->Header.OpenFlags = Info->Flags;

            UnlockMutex(&(FileSystem->FilesMutex));

            return File;
        }

        if ((Inode.Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_REGULAR) {
            KernelHeapFree(File);
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        {
            STR BaseName[MAX_FILE_NAME];
            ExtractBaseName(Info->Name, BaseName);
            FillFileHeaderFromInode(File, BaseName, &Inode);
        }

        File->IsDirectory = FALSE;
        File->Enumerate = FALSE;
        File->Header.OpenFlags = Info->Flags;
        File->Header.SizeLow = Inode.Size;
        File->Header.SizeHigh = 0;
        File->Header.Position = (Info->Flags & FILE_OPEN_APPEND) ? Inode.Size : 0;
        File->Header.BytesTransferred = 0;

        if ((Info->Flags & FILE_OPEN_TRUNCATE) && (Info->Flags & FILE_OPEN_WRITE)) {
            if (TruncateInode(FileSystem, &(File->Inode)) == FALSE) {
                KernelHeapFree(File);
                UnlockMutex(&(FileSystem->FilesMutex));
                return NULL;
            }

            if (WriteInode(FileSystem, File->InodeIndex, &(File->Inode)) == FALSE) {
                KernelHeapFree(File);
                UnlockMutex(&(FileSystem->FilesMutex));
                return NULL;
            }

            File->Header.SizeLow = 0;
            File->Header.Position = 0;
        }

        UnlockMutex(&(FileSystem->FilesMutex));

        return File;
    }
}

/************************************************************************/

/**
 * @brief Advances to the next entry when enumerating a directory.
 * @param File Directory enumeration handle.
 * @return DF_RETURN_SUCCESS on success or an error code otherwise.
 */
static U32 OpenNext(LPEXT2FILE File) {
    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    if (File->IsDirectory == FALSE) return DF_RETURN_GENERIC;
    if (File->Enumerate == FALSE) return DF_RETURN_GENERIC;

    if (LoadNextDirectoryEntry(File) == FALSE) return DF_RETURN_GENERIC;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Closes an EXT2 file handle and releases its memory.
 * @param File File handle to close.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER if the handle is invalid.
 */
static U32 CloseFile(LPEXT2FILE File) {
    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    if (File->IsDirectory) {
        ReleaseDirectoryResources(File);
    }

    ReleaseKernelObject(File);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reads data from an EXT2 file into the provided buffer.
 * @param File File handle describing the read request.
 * @return DF_RETURN_SUCCESS on success or an error code on failure.
 */
static U32 ReadFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 Remaining;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    if (File->IsDirectory) {
        return DF_RETURN_GENERIC;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_RETURN_BAD_PARAMETER;
    if (FileSystem->BlockSize == 0 || FileSystem->IOBuffer == NULL) return DF_RETURN_INPUT_OUTPUT;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    File->Header.BytesTransferred = 0;

    if (File->Header.Position >= File->Inode.Size) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_SUCCESS;
    }

    if (File->Header.ByteCount == 0) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_SUCCESS;
    }

    Remaining = File->Inode.Size - File->Header.Position;
    if (Remaining > File->Header.ByteCount) {
        Remaining = File->Header.ByteCount;
    }

    while (Remaining > 0) {
        U32 BlockIndex;
        U32 OffsetInBlock;
        U32 BlockNumber;
        U32 Chunk;

        BlockIndex = File->Header.Position / FileSystem->BlockSize;
        OffsetInBlock = File->Header.Position % FileSystem->BlockSize;

        if (GetInodeBlockNumber(FileSystem, &(File->Inode), BlockIndex, &BlockNumber) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_INPUT_OUTPUT;
        }

        if (BlockNumber == 0) {
            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
        } else if (ReadBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_INPUT_OUTPUT;
        }

        Chunk = FileSystem->BlockSize - OffsetInBlock;
        if (Chunk > Remaining) {
            Chunk = Remaining;
        }

        MemoryCopy(((U8*)File->Header.Buffer) + File->Header.BytesTransferred,
            FileSystem->IOBuffer + OffsetInBlock,
            Chunk);

        File->Header.Position += Chunk;
        File->Header.BytesTransferred += Chunk;
        Remaining -= Chunk;
    }

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Writes buffered data to an EXT2 file block by block.
 * @param File File handle describing the write request.
 * @return DF_RETURN_SUCCESS on success or an error code on failure.
 */
static U32 WriteFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 Remaining;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    if ((File->Header.OpenFlags & FILE_OPEN_WRITE) == 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    if (File->IsDirectory) {
        return DF_RETURN_GENERIC;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_RETURN_BAD_PARAMETER;
    if (FileSystem->BlockSize == 0 || FileSystem->IOBuffer == NULL) return DF_RETURN_INPUT_OUTPUT;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    if (File->Header.OpenFlags & FILE_OPEN_APPEND) {
        File->Header.Position = File->Inode.Size;
    }

    File->Header.BytesTransferred = 0;

    if (File->Header.ByteCount == 0) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_SUCCESS;
    }

    Remaining = File->Header.ByteCount;

    while (Remaining > 0) {
        U32 BlockIndex;
        U32 OffsetInBlock;
        U32 BlockNumber;
        U32 Chunk;
        U8* Source;

        BlockIndex = File->Header.Position / FileSystem->BlockSize;
        OffsetInBlock = File->Header.Position % FileSystem->BlockSize;

        if (ResolveInodeBlock(FileSystem, &(File->Inode), BlockIndex, TRUE, &BlockNumber) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_INPUT_OUTPUT;
        }

        if (BlockNumber == 0) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_RETURN_INPUT_OUTPUT;
        }

        Chunk = FileSystem->BlockSize - OffsetInBlock;
        if (Chunk > Remaining) {
            Chunk = Remaining;
        }

        Source = ((U8*)File->Header.Buffer) + File->Header.BytesTransferred;

        if (Chunk != FileSystem->BlockSize || OffsetInBlock != 0) {
            if (ReadBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_RETURN_INPUT_OUTPUT;
            }

            MemoryCopy(FileSystem->IOBuffer + OffsetInBlock, Source, Chunk);

            if (WriteBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_RETURN_INPUT_OUTPUT;
            }
        } else {
            if (WriteBlock(FileSystem, BlockNumber, Source) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_RETURN_INPUT_OUTPUT;
            }
        }

        File->Header.Position += Chunk;
        File->Header.BytesTransferred += Chunk;
        Remaining -= Chunk;
    }

    if (File->Header.Position > File->Inode.Size) {
        File->Inode.Size = File->Header.Position;
    }

    File->Header.SizeLow = File->Inode.Size;

    if (WriteInode(FileSystem, File->InodeIndex, &(File->Inode)) == FALSE) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_RETURN_INPUT_OUTPUT;
    }

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Mounts an EXT2 partition and registers it with the kernel.
 * @param Disk Physical disk hosting the partition.
 * @param Partition Partition descriptor provided by the kernel.
 * @param Base Base LBA of the containing disk extent.
 * @param PartIndex Index of the partition on the disk.
 * @return TRUE on success, FALSE if the partition could not be mounted.
 */
BOOL MountPartition_EXT2(LPSTORAGE_UNIT Disk, LPBOOT_PARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE * 2];
    IOCONTROL Control;
    LPEXT2SUPER Super;
    LPEXT2FILESYSTEM FileSystem;
    U32 Result;
    SECTOR PartitionStart;

    if (Disk == NULL || Partition == NULL) return FALSE;

    PartitionStart = Base + Partition->LBA;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = PartitionStart + 2;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = sizeof(Buffer);

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    Super = (LPEXT2SUPER)Buffer;

    if (Super->Magic != EXT2_SUPER_MAGIC) {
        return FALSE;
    }

    FileSystem = NewEXT2FileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    MemoryCopy(&(FileSystem->Super), Super, sizeof(EXT2SUPER));

    FileSystem->PartitionStart = PartitionStart;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;

    if (Super->LogBlockSize <= 4) {
        FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE << Super->LogBlockSize;
    }

    FileSystem->SectorsPerBlock = FileSystem->BlockSize / SECTOR_SIZE;
    if (FileSystem->SectorsPerBlock == 0) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    FileSystem->InodeSize = Super->InodeSize;
    if (FileSystem->InodeSize == 0) {
        FileSystem->InodeSize = sizeof(EXT2INODE);
    }

    FileSystem->InodesPerBlock = FileSystem->BlockSize / FileSystem->InodeSize;
    if (FileSystem->InodesPerBlock == 0) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    if (Ext2BufferPoolInit(FileSystem) == FALSE) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    if (LoadGroupDescriptors(FileSystem) == FALSE) {
        Ext2BufferPoolDeinit(FileSystem);
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    FileSystem->IOBuffer = (U8*)KernelHeapAlloc(FileSystem->BlockSize);
    if (FileSystem->IOBuffer == NULL) {
        KernelHeapFree(FileSystem->Groups);
        Ext2BufferPoolDeinit(FileSystem);
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    ListAddItem(GetFileSystemList(), FileSystem);


    return TRUE;
}

/************************************************************************/

/**
 * @brief Dispatches EXT2 driver commands requested by the kernel.
 * @param Function Identifier of the requested operation.
 * @param Parameter Optional parameter pointer or value.
 * @return Driver-specific result or error code.
 */
UINT EXT2Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_CREATEFOLDER:
            return CreateNode((LPFILE_INFO)Parameter, TRUE);
        case DF_FS_OPENFILE:
            return (UINT)OpenFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENNEXT:
            return OpenNext((LPEXT2FILE)Parameter);
        case DF_FS_CLOSEFILE:
            return CloseFile((LPEXT2FILE)Parameter);
        case DF_FS_READ:
            return ReadFile((LPEXT2FILE)Parameter);
        case DF_FS_WRITE:
            return WriteFile((LPEXT2FILE)Parameter);
        case DF_FS_CREATEPARTITION:
            return Ext2CreatePartition((LPPARTITION_CREATION)Parameter);
        default:
            break;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
