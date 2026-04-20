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


    PackageFS file operations implementation

\************************************************************************/

#include "PackageFS-Internal.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "utils/Compression.h"
#include "utils/Crypt.h"

/************************************************************************/

/**
 * @brief Create a file object bound to a package node.
 * @param FileSystem Owning PackageFS instance.
 * @param Node Target node.
 * @return Allocated file object or NULL.
 */
static LPPACKAGEFSFILE PackageFSCreateFileObject(LPPACKAGEFSFILESYSTEM FileSystem, LPPACKAGEFS_NODE Node) {
    LPPACKAGEFSFILE File;
    const EPK_PARSED_TOC_ENTRY* Entry = NULL;

    if (FileSystem == NULL || Node == NULL) {
        return NULL;
    }

    File = (LPPACKAGEFSFILE)CreateKernelObject(sizeof(PACKAGEFSFILE), KOID_FILE);
    if (File == NULL) {
        return NULL;
    }

    File->Header.FileSystem = &FileSystem->Header;
    File->Node = Node;
    File->EnumerationCursor = NULL;
    File->Enumerate = FALSE;
    File->Pattern[0] = STR_NULL;
    InitMutex(&File->Header.Mutex);
    InitSecurity(&File->Header.Security);

    if (Node->ParentNode == NULL) {
        StringCopy(File->Header.Name, TEXT("/"));
    } else {
        StringCopy(File->Header.Name, Node->Name);
    }

    File->Header.Attributes = Node->Attributes;
    File->Header.Creation = Node->Modified;
    File->Header.Accessed = Node->Modified;
    File->Header.Modified = Node->Modified;
    File->Header.Position = 0;
    File->Header.BytesTransferred = 0;

    if ((Node->Attributes & FS_ATTR_FOLDER) == 0 &&
        Node->TocIndex != MAX_U32 &&
        Node->TocIndex < FileSystem->Package.TocEntryCount) {
        Entry = &FileSystem->Package.TocEntries[Node->TocIndex];
        File->Header.SizeLow = U64_Low32(Entry->FileSize);
        File->Header.SizeHigh = U64_High32(Entry->FileSize);
    } else {
        File->Header.SizeLow = 0;
        File->Header.SizeHigh = 0;
    }

    return File;
}

/************************************************************************/

/**
 * @brief Wildcard matcher for folder enumeration.
 * @param Pattern Pattern containing '*' and '?'.
 * @param Name Candidate name.
 * @return TRUE when pattern matches.
 */
static BOOL PackageFSWildcardMatch(LPCSTR Pattern, LPCSTR Name) {
    if (Pattern == NULL || Name == NULL) {
        return FALSE;
    }

    if (*Pattern == STR_NULL) {
        return *Name == STR_NULL;
    }

    if (*Pattern == '*') {
        LPCSTR NextPattern = Pattern + 1;
        LPCSTR Cursor = Name;

        while (*NextPattern == '*') {
            NextPattern++;
        }

        if (*NextPattern == STR_NULL) {
            return TRUE;
        }

        while (*Cursor != STR_NULL) {
            if (PackageFSWildcardMatch(NextPattern, Cursor)) {
                return TRUE;
            }
            Cursor++;
        }

        return PackageFSWildcardMatch(NextPattern, Cursor);
    }

    if (*Pattern == '?') {
        if (*Name == STR_NULL) {
            return FALSE;
        }
        return PackageFSWildcardMatch(Pattern + 1, Name + 1);
    }

    if (*Pattern != *Name) {
        return FALSE;
    }

    return PackageFSWildcardMatch(Pattern + 1, Name + 1);
}

/************************************************************************/

/**
 * @brief Advance a folder enumeration file handle.
 * @param File Enumeration handle.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSAdvanceEnumeration(LPPACKAGEFSFILE File) {
    LPPACKAGEFS_NODE Cursor;

    if (File == NULL || File->Node == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Cursor = File->EnumerationCursor;
    while (Cursor != NULL) {
        File->EnumerationCursor = Cursor->NextSibling;

        if (!PackageFSWildcardMatch(File->Pattern, Cursor->Name)) {
            Cursor = File->EnumerationCursor;
            continue;
        }

        StringCopy(File->Header.Name, Cursor->Name);
        File->Header.Attributes = Cursor->Attributes;
        File->Header.Creation = Cursor->Modified;
        File->Header.Accessed = Cursor->Modified;
        File->Header.Modified = Cursor->Modified;

        if ((Cursor->Attributes & FS_ATTR_FOLDER) != 0 ||
            Cursor->TocIndex == MAX_U32 ||
            Cursor->TocIndex >= ((LPPACKAGEFSFILESYSTEM)File->Header.FileSystem)->Package.TocEntryCount) {
            File->Header.SizeLow = 0;
            File->Header.SizeHigh = 0;
        } else {
            const EPK_PARSED_TOC_ENTRY* Entry =
                &((LPPACKAGEFSFILESYSTEM)File->Header.FileSystem)->Package.TocEntries[Cursor->TocIndex];
            File->Header.SizeLow = U64_Low32(Entry->FileSize);
            File->Header.SizeHigh = U64_High32(Entry->FileSize);
        }

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

/**
 * @brief Open file or folder in PackageFS.
 * @param Info Open request.
 * @return Opened file handle or NULL.
 */
LPPACKAGEFSFILE PackageFSOpenFile(LPFILE_INFO Info) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    STR PathText[MAX_PATH_NAME];
    LPSTR LastSlash;
    BOOL Wildcard = FALSE;
    LPPACKAGEFS_NODE Node;
    LPPACKAGEFSFILE File;

    if (Info == NULL || Info->FileSystem == NULL) {
        return NULL;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)Info->FileSystem;

    StringCopy(PathText, Info->Name);
    if (PathText[0] == STR_NULL) {
        StringCopy(PathText, TEXT("/"));
    }

    LockMutex(&FileSystem->FilesMutex, INFINITY);

    if ((Info->Flags & FILE_OPEN_WRITE) != 0 ||
        (Info->Flags & FILE_OPEN_APPEND) != 0 ||
        (Info->Flags & FILE_OPEN_TRUNCATE) != 0 ||
        (Info->Flags & FILE_OPEN_CREATE_ALWAYS) != 0) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    if (StringFindChar(PathText, '*') != NULL || StringFindChar(PathText, '?') != NULL) {
        STR PatternPath[MAX_PATH_NAME];
        STR Pattern[MAX_FILE_NAME];

        Wildcard = TRUE;
        StringCopy(PatternPath, PathText);
        LastSlash = StringFindCharR(PatternPath, PATH_SEP);
        if (LastSlash != NULL) {
            StringCopy(Pattern, LastSlash + 1);
            *LastSlash = STR_NULL;
            if (PatternPath[0] == STR_NULL) {
                StringCopy(PatternPath, TEXT("/"));
            }
        } else {
            StringCopy(Pattern, PatternPath);
            StringCopy(PatternPath, TEXT("/"));
        }

        Node = PackageFSResolvePath(FileSystem, PatternPath, TRUE);
        if (Node == NULL || (Node->Attributes & FS_ATTR_FOLDER) == 0) {
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        File = PackageFSCreateFileObject(FileSystem, Node);
        if (File == NULL) {
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        File->Enumerate = TRUE;
        StringCopy(File->Pattern, Pattern);
        File->EnumerationCursor = Node->FirstChild;

        if (PackageFSAdvanceEnumeration(File) != DF_RETURN_SUCCESS) {
            ReleaseKernelObject(File);
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        UnlockMutex(&FileSystem->FilesMutex);
        return File;
    }

    Node = PackageFSResolvePath(FileSystem, PathText, FALSE);
    if (Node == NULL) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    File = PackageFSCreateFileObject(FileSystem, Node);
    if (File == NULL) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    File->Enumerate = Wildcard;

    UnlockMutex(&FileSystem->FilesMutex);
    return File;
}

/************************************************************************/

/**
 * @brief Enumerate next folder entry.
 * @param File Enumeration file handle.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSOpenNext(LPPACKAGEFSFILE File) {
    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (!File->Enumerate) {
        return DF_RETURN_GENERIC;
    }

    return PackageFSAdvanceEnumeration(File);
}

/************************************************************************/

/**
 * @brief Close PackageFS file handle.
 * @param File File handle.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSCloseFile(LPPACKAGEFSFILE File) {
    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    ReleaseKernelObject(File);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read one PackageFS block into an uncompressed destination buffer.
 * @param FileSystem PackageFS instance.
 * @param BlockIndex Absolute block table index.
 * @param Destination Destination buffer.
 * @param DestinationSize Expected uncompressed block size.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSReadUncompressedBlock(LPPACKAGEFSFILESYSTEM FileSystem,
                                          U32 BlockIndex,
                                          LPVOID Destination,
                                          U32 DestinationSize) {
    const EPK_PARSED_BLOCK_ENTRY* Block;
    U32 CompressedOffset;
    U32 CompressedEnd;
    const U8* Source;
    U32 DecodedSize = 0;
    U8 Digest[SHA256_SIZE];

    if (FileSystem == NULL || Destination == NULL || DestinationSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (BlockIndex >= FileSystem->Package.BlockCount) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Block = &FileSystem->Package.BlockEntries[BlockIndex];
    if (Block->UncompressedSize != DestinationSize || Block->CompressedSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    CompressedOffset = U64_Low32(Block->CompressedOffset);
    CompressedEnd = CompressedOffset + Block->CompressedSize;
    if (CompressedEnd < CompressedOffset || CompressedEnd > FileSystem->PackageSize) {
        ERROR(TEXT("Invalid block bounds index=%u"), BlockIndex);
        return DF_RETURN_GENERIC;
    }

    Source = FileSystem->Package.PackageBytes + CompressedOffset;

    if (Block->CompressionMethod == EPK_COMPRESSION_METHOD_NONE) {
        if (Block->CompressedSize != Block->UncompressedSize) {
            ERROR(TEXT("Raw block size mismatch index=%u"), BlockIndex);
            return DF_RETURN_GENERIC;
        }
        MemoryCopy(Destination, Source, DestinationSize);
    } else if (Block->CompressionMethod == EPK_COMPRESSION_METHOD_ZLIB) {
        U32 InflateStatus =
            CompressionInflate(Source,
                              Block->CompressedSize,
                              Destination,
                              DestinationSize,
                              &DecodedSize,
                              COMPRESSION_FORMAT_ZLIB);

        if (InflateStatus != COMPRESSION_STATUS_OK || DecodedSize != DestinationSize) {
            ERROR(TEXT("Inflate failed index=%u status=%u decoded=%u expected=%u"),
                  BlockIndex,
                  InflateStatus,
                  DecodedSize,
                  DestinationSize);
            return DF_RETURN_GENERIC;
        }
    } else {
        ERROR(TEXT("Unsupported compression method=%u index=%u"),
              Block->CompressionMethod,
              BlockIndex);
        return DF_RETURN_GENERIC;
    }

    SHA256(Destination, DestinationSize, Digest);
    if (MemoryCompare(Digest, Block->ChunkHash, EPK_HASH_SIZE) != 0) {
        ERROR(TEXT("Block hash mismatch index=%u"), BlockIndex);
        return DF_RETURN_GENERIC;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read one file backed by block table chunks.
 * @param File PackageFS file handle.
 * @param FileSystem Owning PackageFS instance.
 * @param Entry Parsed TOC entry for the file.
 * @return DF_RETURN_SUCCESS when read succeeds.
 */
static U32 PackageFSReadBlockBackedFile(LPPACKAGEFSFILE File,
                                        LPPACKAGEFSFILESYSTEM FileSystem,
                                        const EPK_PARSED_TOC_ENTRY* Entry) {
    U32 FileSize;
    U32 Position;
    U32 RemainingRequest;
    U32 FileOffset;
    U32 RelativeBlockIndex;
    U8* Output;

    if (File == NULL || FileSystem == NULL || Entry == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Entry->BlockCount == 0) {
        return DF_RETURN_GENERIC;
    }

    FileSize = U64_ToU32_Clip(Entry->FileSize);
    Position = (U32)File->Header.Position;
    if (Position >= FileSize || File->Header.ByteCount == 0) {
        return DF_RETURN_SUCCESS;
    }

    RemainingRequest = File->Header.ByteCount;
    if (RemainingRequest > FileSize - Position) {
        RemainingRequest = FileSize - Position;
    }

    ChunkCacheCleanup(&FileSystem->ChunkCache);

    Output = (U8*)File->Header.Buffer;
    FileOffset = 0;

    for (RelativeBlockIndex = 0;
         RelativeBlockIndex < Entry->BlockCount && RemainingRequest > 0;
         RelativeBlockIndex++) {
        const EPK_PARSED_BLOCK_ENTRY* Block =
            &FileSystem->Package.BlockEntries[Entry->BlockIndexStart + RelativeBlockIndex];
        U32 BlockStart = FileOffset;
        U32 BlockSize = Block->UncompressedSize;
        U32 BlockEnd = BlockStart + BlockSize;
        U32 BlockReadOffset;
        U32 BlockCopySize;
        U32 AbsoluteBlockIndex = Entry->BlockIndexStart + RelativeBlockIndex;
        U8* BlockBuffer;
        U64 CacheKey = U64_FromU32(AbsoluteBlockIndex);
        U32 LoadStatus;

        if (BlockEnd < BlockStart || BlockEnd > FileSize) {
            ERROR(TEXT("Invalid block coverage block=%u start=%u end=%u file=%u"),
                  AbsoluteBlockIndex,
                  BlockStart,
                  BlockEnd,
                  FileSize);
            return DF_RETURN_GENERIC;
        }

        if (Position >= BlockEnd) {
            FileOffset = BlockEnd;
            continue;
        }

        BlockReadOffset = Position > BlockStart ? Position - BlockStart : 0;
        BlockCopySize = BlockSize - BlockReadOffset;
        if (BlockCopySize > RemainingRequest) {
            BlockCopySize = RemainingRequest;
        }

        BlockBuffer = (U8*)KernelHeapAlloc(BlockSize);
        if (BlockBuffer == NULL) {
            return DF_RETURN_NO_MEMORY;
        }

        if (!ChunkCacheRead(&FileSystem->ChunkCache, FileSystem, CacheKey, BlockBuffer, BlockSize)) {
            LoadStatus = PackageFSReadUncompressedBlock(FileSystem, AbsoluteBlockIndex, BlockBuffer, BlockSize);
            if (LoadStatus != DF_RETURN_SUCCESS) {
                KernelHeapFree(BlockBuffer);
                return LoadStatus;
            }

            if (!ChunkCacheStore(&FileSystem->ChunkCache, FileSystem, CacheKey, BlockBuffer, BlockSize)) {
                WARNING(TEXT("Chunk cache store failed block=%u"), AbsoluteBlockIndex);
            }
        }

        MemoryCopy(Output + File->Header.BytesTransferred, BlockBuffer + BlockReadOffset, BlockCopySize);
        KernelHeapFree(BlockBuffer);

        File->Header.BytesTransferred += BlockCopySize;
        File->Header.Position += BlockCopySize;
        Position += BlockCopySize;
        RemainingRequest -= BlockCopySize;
        FileOffset = BlockEnd;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read file bytes from PackageFS.
 * @param File File handle.
 * @return DF_RETURN_SUCCESS when read succeeds.
 */
U32 PackageFSReadFile(LPPACKAGEFSFILE File) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    const EPK_PARSED_TOC_ENTRY* Entry;
    U32 DataSize;
    U32 Remaining;
    U32 ReadBytes;
    U32 Position;

    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (File->Header.Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    if (File->Node == NULL || (File->Node->Attributes & FS_ATTR_FOLDER) != 0) {
        return DF_RETURN_GENERIC;
    }

    if (File->Node->TocIndex == MAX_U32) {
        return DF_RETURN_GENERIC;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL || File->Node->TocIndex >= FileSystem->Package.TocEntryCount) {
        return DF_RETURN_GENERIC;
    }

    Entry = &FileSystem->Package.TocEntries[File->Node->TocIndex];
    File->Header.BytesTransferred = 0;

    if ((Entry->EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_BLOCKS) != 0) {
        return PackageFSReadBlockBackedFile(File, FileSystem, Entry);
    }

    if ((Entry->EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA) == 0) {
        return DF_RETURN_GENERIC;
    }

    DataSize = Entry->InlineDataSize;
    Position = (U32)File->Header.Position;
    if (Position >= DataSize || File->Header.ByteCount == 0) {
        return DF_RETURN_SUCCESS;
    }

    Remaining = DataSize - Position;
    ReadBytes = File->Header.ByteCount;
    if (ReadBytes > Remaining) {
        ReadBytes = Remaining;
    }

    MemoryCopy(File->Header.Buffer,
               FileSystem->Package.PackageBytes + U64_Low32(Entry->InlineDataOffset) + Position,
               ReadBytes);

    File->Header.Position += ReadBytes;
    File->Header.BytesTransferred = ReadBytes;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reject write operations on PackageFS.
 * @param File File handle.
 * @return DF_RETURN_NO_PERMISSION always.
 */
U32 PackageFSWriteFile(LPPACKAGEFSFILE File) {
    UNUSED(File);
    return DF_RETURN_NO_PERMISSION;
}

/************************************************************************/

/**
 * @brief Check whether a path exists in PackageFS.
 * @param Check Path check structure.
 * @return TRUE when path resolves to a folder.
 */
BOOL PackageFSPathExists(LPFILESYSTEM_PATHCHECK Check) {
    UNUSED(Check);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether one file or folder exists in PackageFS.
 * @param Info File info containing target path.
 * @return TRUE when target exists.
 */
BOOL PackageFSFileExists(LPFILE_INFO Info) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    LPPACKAGEFS_NODE Node;
    STR FullPath[MAX_PATH_NAME];

    if (Info == NULL || Info->FileSystem == NULL) {
        return FALSE;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)Info->FileSystem;

    if (Info->Name[0] == PATH_SEP) {
        StringCopy(FullPath, Info->Name);
    } else {
        StringCopy(FullPath, TEXT("/"));
        StringConcat(FullPath, Info->Name);
    }

    Node = PackageFSResolvePath(FileSystem, FullPath, FALSE);
    return Node != NULL;
}
