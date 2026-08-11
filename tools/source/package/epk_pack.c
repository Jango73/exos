
/************************************************************************\

    EXOS Tools
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


    EPK Pack - A tool for EPK management

\************************************************************************/

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "miniz.h"
#include "bearssl_hash.h"

#define EPK_MAGIC 0x314B5045U
#define EPK_VERSION_1_0 ((1U << 16) | 0U)

#define EPK_HASH_SIZE 32U
#define EPK_HEADER_SIZE 128U
#define EPK_TOC_ENTRY_SIZE 96U
#define EPK_BLOCK_ENTRY_SIZE 52U

#define EPK_HEADER_FLAG_COMPRESSED_BLOCKS 0x00000001U
#define EPK_HEADER_FLAG_HAS_SIGNATURE 0x00000002U

#define EPK_NODE_TYPE_FILE 1U
#define EPK_NODE_TYPE_FOLDER 2U

#define EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA 0x00000001U
#define EPK_TOC_ENTRY_FLAG_HAS_BLOCKS 0x00000002U

#define EPK_COMPRESSION_METHOD_NONE 0U
#define EPK_COMPRESSION_METHOD_ZLIB 1U

#define HEADER_HASH_OFFSET 80U

typedef enum tag_COMPRESSION_MODE {
    COMPRESSION_MODE_ZLIB,
    COMPRESSION_MODE_NONE,
    COMPRESSION_MODE_AUTO,
} COMPRESSION_MODE;

typedef enum tag_MTIME_POLICY {
    MTIME_POLICY_ZERO,
    MTIME_POLICY_SOURCE,
} MTIME_POLICY;

typedef enum tag_SIGNATURE_OUTPUT {
    SIGNATURE_OUTPUT_RAW,
    SIGNATURE_OUTPUT_HEX,
} SIGNATURE_OUTPUT;

typedef struct tag_OPTIONS {
    const char *InputPath;
    const char *OutputPath;
    const char *ManifestPath;
    uint32_t ChunkSize;
    int CompressionLevel;
    COMPRESSION_MODE CompressionMode;
    MTIME_POLICY MTimePolicy;
    const char *SignatureCommand;
    SIGNATURE_OUTPUT SignatureOutput;
} OPTIONS;

typedef struct tag_BYTE_BUFFER {
    uint8_t *Data;
    size_t Size;
    size_t Capacity;
} BYTE_BUFFER;

typedef struct tag_TEXT_LIST {
    char **Items;
    size_t Count;
    size_t Capacity;
} TEXT_LIST;

typedef struct tag_FILE_ENTRY {
    char *RelativePath;
    char *AbsolutePath;
    struct stat Stat;
} FILE_ENTRY;

typedef struct tag_FILE_LIST {
    FILE_ENTRY *Items;
    size_t Count;
    size_t Capacity;
} FILE_LIST;

typedef struct tag_BLOCK_ENTRY {
    uint64_t CompressedOffset;
    uint32_t CompressedSize;
    uint32_t UncompressedSize;
    uint8_t CompressionMethod;
    uint8_t ChunkHash[32];
} BLOCK_ENTRY;

typedef struct tag_BLOCK_LIST {
    BLOCK_ENTRY *Items;
    size_t Count;
    size_t Capacity;
} BLOCK_LIST;

typedef struct tag_TOC_ENTRY {
    uint32_t NodeType;
    uint32_t EntryFlags;
    uint32_t Permissions;
    uint64_t ModifiedTime;
    uint64_t FileSize;
    uint64_t InlineDataOffset;
    uint32_t InlineDataSize;
    uint32_t BlockIndexStart;
    uint32_t BlockCount;
    uint8_t FileHash[32];
    char *Path;
} TOC_ENTRY;

typedef struct tag_TOC_LIST {
    TOC_ENTRY *Items;
    size_t Count;
    size_t Capacity;
} TOC_LIST;

/**
 * @brief Prints the command-line usage and supported options to stdout.
 *
 * This is invoked for --help / -h and when arguments are missing.
 */
static void Usage(void) {
    printf("Usage:\n");
    printf("  epk-pack pack --input <folder> --output <file.epk> [options]\n\n");
    printf("Options:\n");
    printf("  --manifest <path>\n");
    printf("  --chunk-size <bytes>\n");
    printf("  --compression <zlib|none|auto>\n");
    printf("  --compression-level <0..9>\n");
    printf("  --mtime-policy <zero|source>\n");
    printf("  --signature-command <cmd>\n");
    printf("  --signature-output <raw|hex>\n");
}

/**
 * @brief Prints a fatal error message and terminates the process.
 *
 * This function does not return.
 *
 * @param Message Human-readable error message (no formatting).
 */
static void Fail(const char *Message) {
    fprintf(stderr, "error: %s\n", Message);
    exit(1);
}

/**
 * @brief Prints a fatal error message including strerror(errno) and terminates the process.
 *
 * This function does not return.
 *
 * @param Prefix Context prefix describing the failing operation (e.g. "cannot open file").
 * @param Path Path associated with the failing operation.
 */
static void FailErrno(const char *Prefix, const char *Path) {
    fprintf(stderr, "error: %s '%s': %s\n", Prefix, Path, strerror(errno));
    exit(1);
}

/**
 * @brief Duplicates a NUL-terminated string using malloc().
 *
 * On allocation failure the process terminates via Fail().
 *
 * @param Value Source NUL-terminated string.
 *
 * @return Newly allocated string copy. The caller must free().
 */
static char *DupString(const char *Value) {
    size_t Length = strlen(Value);
    char *Copy = (char *)malloc(Length + 1);
    if (Copy == NULL) {
        Fail("out of memory");
    }
    memcpy(Copy, Value, Length + 1);
    return Copy;
}

/**
 * @brief Initializes a BYTE_BUFFER to the empty state.
 *
 * Sets Data/Size/Capacity to zero.
 *
 * @param Buffer Buffer to initialize.
 */
static void ByteBufferInit(BYTE_BUFFER *Buffer) {
    memset(Buffer, 0, sizeof(*Buffer));
}

/**
 * @brief Ensures a BYTE_BUFFER has at least the requested capacity.
 *
 * Capacity grows by doubling (starting at 4096 bytes). Terminates on overflow or OOM.
 *
 * @param Buffer Target buffer to grow.
 * @param Needed Minimum capacity in bytes (not size).
 */
static void ByteBufferReserve(BYTE_BUFFER *Buffer, size_t Needed) {
    if (Needed <= Buffer->Capacity) {
        return;
    }
    size_t NewCapacity = Buffer->Capacity == 0 ? 4096 : Buffer->Capacity;
    while (NewCapacity < Needed) {
        if (NewCapacity > ((size_t)-1) / 2) {
            Fail("buffer overflow");
        }
        NewCapacity *= 2;
    }
    uint8_t *NewData = (uint8_t *)realloc(Buffer->Data, NewCapacity);
    if (NewData == NULL) {
        Fail("out of memory");
    }
    Buffer->Data = NewData;
    Buffer->Capacity = NewCapacity;
}

/**
 * @brief Appends bytes to a BYTE_BUFFER.
 *
 * Grows the buffer if needed and copies Size bytes from Data.
 *
 * @param Buffer Destination buffer.
 * @param Data Source bytes to append.
 * @param Size Number of bytes to append.
 */
static void ByteBufferAppend(BYTE_BUFFER *Buffer, const void *Data, size_t Size) {
    size_t NewSize = Buffer->Size + Size;
    if (NewSize < Buffer->Size) {
        Fail("buffer size overflow");
    }
    ByteBufferReserve(Buffer, NewSize);
    memcpy(Buffer->Data + Buffer->Size, Data, Size);
    Buffer->Size = NewSize;
}

/**
 * @brief Appends zero-initialized bytes to a BYTE_BUFFER.
 *
 * Grows the buffer if needed and writes Size bytes of 0x00.
 *
 * @param Buffer Destination buffer.
 * @param Size Number of zero bytes to append.
 */
static void ByteBufferAppendZeros(BYTE_BUFFER *Buffer, size_t Size) {
    size_t NewSize = Buffer->Size + Size;
    if (NewSize < Buffer->Size) {
        Fail("buffer size overflow");
    }
    ByteBufferReserve(Buffer, NewSize);
    memset(Buffer->Data + Buffer->Size, 0, Size);
    Buffer->Size = NewSize;
}

/**
 * @brief Releases the storage owned by a BYTE_BUFFER and resets it.
 *
 * Safe to call on an already-empty buffer.
 *
 * @param Buffer Buffer to free/reset.
 */
static void ByteBufferFree(BYTE_BUFFER *Buffer) {
    free(Buffer->Data);
    memset(Buffer, 0, sizeof(*Buffer));
}

/**
 * @brief Appends a string pointer to a TEXT_LIST (growing the list if needed).
 *
 * The list stores the pointer as-is (no duplication).
 *
 * @param List Target list.
 * @param Item String pointer to store in the list.
 */
static void TextListPush(TEXT_LIST *List, char *Item) {
    if (List->Count == List->Capacity) {
        size_t NewCapacity = List->Capacity == 0 ? 16 : List->Capacity * 2;
        char **NewItems = (char **)realloc(List->Items, NewCapacity * sizeof(char *));
        if (NewItems == NULL) {
            Fail("out of memory");
        }
        List->Items = NewItems;
        List->Capacity = NewCapacity;
    }
    List->Items[List->Count++] = Item;
}

/**
 * @brief Appends a FILE_ENTRY to a FILE_LIST (growing the list if needed).
 *
 * The entry is copied by value; ownership of any heap pointers inside remains with the caller's convention.
 *
 * @param List Target list.
 * @param Entry File entry to append.
 */
static void FileListPush(FILE_LIST *List, FILE_ENTRY Entry) {
    if (List->Count == List->Capacity) {
        size_t NewCapacity = List->Capacity == 0 ? 16 : List->Capacity * 2;
        FILE_ENTRY *NewItems = (FILE_ENTRY *)realloc(List->Items, NewCapacity * sizeof(FILE_ENTRY));
        if (NewItems == NULL) {
            Fail("out of memory");
        }
        List->Items = NewItems;
        List->Capacity = NewCapacity;
    }
    List->Items[List->Count++] = Entry;
}

/**
 * @brief Appends a BLOCK_ENTRY to a BLOCK_LIST (growing the list if needed).
 *
 * @param List Target list.
 * @param Entry Block metadata to append.
 */
static void BlockListPush(BLOCK_LIST *List, BLOCK_ENTRY Entry) {
    if (List->Count == List->Capacity) {
        size_t NewCapacity = List->Capacity == 0 ? 64 : List->Capacity * 2;
        BLOCK_ENTRY *NewItems = (BLOCK_ENTRY *)realloc(List->Items, NewCapacity * sizeof(BLOCK_ENTRY));
        if (NewItems == NULL) {
            Fail("out of memory");
        }
        List->Items = NewItems;
        List->Capacity = NewCapacity;
    }
    List->Items[List->Count++] = Entry;
}

/**
 * @brief Appends a TOC_ENTRY to a TOC_LIST (growing the list if needed).
 *
 * @param List Target list.
 * @param Entry TOC entry to append.
 */
static void TocListPush(TOC_LIST *List, TOC_ENTRY Entry) {
    if (List->Count == List->Capacity) {
        size_t NewCapacity = List->Capacity == 0 ? 64 : List->Capacity * 2;
        TOC_ENTRY *NewItems = (TOC_ENTRY *)realloc(List->Items, NewCapacity * sizeof(TOC_ENTRY));
        if (NewItems == NULL) {
            Fail("out of memory");
        }
        List->Items = NewItems;
        List->Capacity = NewCapacity;
    }
    List->Items[List->Count++] = Entry;
}

/**
 * @brief qsort() comparator for an array of (char*).
 *
 * Compares the pointed-to strings with strcmp().
 *
 * @param A Pointer to a (char*) element.
 * @param B Pointer to a (char*) element.
 *
 * @return Negative/zero/positive as in strcmp().
 */
static int CompareCStrings(const void *A, const void *B) {
    const char *Left = *(const char *const *)A;
    const char *Right = *(const char *const *)B;
    return strcmp(Left, Right);
}

/**
 * @brief qsort() comparator for FILE_ENTRY items by RelativePath.
 *
 * @param A Pointer to a FILE_ENTRY.
 * @param B Pointer to a FILE_ENTRY.
 *
 * @return Negative/zero/positive as in strcmp().
 */
static int CompareFileEntries(const void *A, const void *B) {
    const FILE_ENTRY *Left = (const FILE_ENTRY *)A;
    const FILE_ENTRY *Right = (const FILE_ENTRY *)B;
    return strcmp(Left->RelativePath, Right->RelativePath);
}

/**
 * @brief qsort() comparator for TOC_ENTRY items by Path.
 *
 * @param A Pointer to a TOC_ENTRY.
 * @param B Pointer to a TOC_ENTRY.
 *
 * @return Negative/zero/positive as in strcmp().
 */
static int CompareTocEntries(const void *A, const void *B) {
    const TOC_ENTRY *Left = (const TOC_ENTRY *)A;
    const TOC_ENTRY *Right = (const TOC_ENTRY *)B;
    return strcmp(Left->Path, Right->Path);
}

/**
 * @brief Normalizes path separators in-place.
 *
 * Replaces '\\' characters with '/'.
 *
 * @param PathText Path string to normalize (modified in-place).
 */
static void NormalizeSlashes(char *PathText) {
    for (char *Cursor = PathText; *Cursor != '\0'; Cursor++) {
        if (*Cursor == '\\') {
            *Cursor = '/';
        }
    }
}

/**
 * @brief Joins two path components with a '/' separator when needed.
 *
 * Allocates a new string via malloc(). The caller must free().
 *
 * @param Left Left path component.
 * @param Right Right path component.
 *
 * @return Newly allocated joined path.
 */
static char *JoinPath(const char *Left, const char *Right) {
    size_t LeftLength = strlen(Left);
    size_t RightLength = strlen(Right);
    int NeedSlash = (LeftLength > 0 && Left[LeftLength - 1] != '/');
    size_t Total = LeftLength + (NeedSlash ? 1 : 0) + RightLength;
    char *Result = (char *)malloc(Total + 1);
    if (Result == NULL) {
        Fail("out of memory");
    }
    memcpy(Result, Left, LeftLength);
    size_t Offset = LeftLength;
    if (NeedSlash) {
        Result[Offset++] = '/';
    }
    memcpy(Result + Offset, Right, RightLength);
    Result[Total] = '\0';
    return Result;
}

/**
 * @brief Builds a normalized relative path from an absolute path inside a root folder.
 *
 * Fails if AbsolutePath does not start with RootPath (prevents escaping the package root).
 *
 * @param RootPath Absolute package root path.
 * @param AbsolutePath Absolute path under RootPath.
 *
 * @return Newly allocated relative path (normalized to forward slashes).
 */
static char *RelativePathFromRoot(const char *RootPath, const char *AbsolutePath) {
    size_t RootLength = strlen(RootPath);
    if (strncmp(RootPath, AbsolutePath, RootLength) != 0) {
        Fail("path escaped package root");
    }
    const char *Cursor = AbsolutePath + RootLength;
    if (*Cursor == '/') {
        Cursor++;
    }
    char *Relative = DupString(Cursor);
    NormalizeSlashes(Relative);
    return Relative;
}

/**
 * @brief Checks whether a directory entry name is "." or "..".
 *
 * @param Name Directory entry name.
 *
 * @return Non-zero if Name is "." or "..", otherwise 0.
 */
static int IsDotOrDotDot(const char *Name) {
    return (strcmp(Name, ".") == 0 || strcmp(Name, "..") == 0);
}

/**
 * @brief Recursively walks the input folder and collects folders and files to be packaged.
 *
 * Traversal is made deterministic by sorting directory entries. Symbolic links are rejected. The manifest file itself is excluded from the file list.
 *
 * @param RootPath Resolved absolute root input folder.
 * @param CurrentPath Current folder being visited (absolute).
 * @param ManifestPath Resolved absolute manifest path to exclude from Files.
 * @param Folders Output list of relative folder paths (heap strings).
 * @param Files Output list of file entries with absolute/relative paths and stat() data.
 */
static void WalkInputTree(const char *RootPath,
                          const char *CurrentPath,
                          const char *ManifestPath,
                          TEXT_LIST *Folders,
                          FILE_LIST *Files) {
    DIR *Dir = opendir(CurrentPath);
    if (Dir == NULL) {
        FailErrno("cannot open folder", CurrentPath);
    }

    TEXT_LIST Names = {0};
    struct dirent *Entry;
    while ((Entry = readdir(Dir)) != NULL) {
        if (IsDotOrDotDot(Entry->d_name)) {
            continue;
        }
        TextListPush(&Names, DupString(Entry->d_name));
    }
    closedir(Dir);

    qsort(Names.Items, Names.Count, sizeof(char *), CompareCStrings);

    for (size_t Index = 0; Index < Names.Count; Index++) {
        char *ChildPath = JoinPath(CurrentPath, Names.Items[Index]);

        struct stat St;
        if (lstat(ChildPath, &St) != 0) {
            FailErrno("cannot stat", ChildPath);
        }

        if (S_ISLNK(St.st_mode)) {
            fprintf(stderr, "error: symbolic links are not supported: %s\n", ChildPath);
            exit(1);
        }

        if (S_ISDIR(St.st_mode)) {
            char *Relative = RelativePathFromRoot(RootPath, ChildPath);
            if (strlen(Relative) > 0) {
                TextListPush(Folders, Relative);
            } else {
                free(Relative);
            }
            WalkInputTree(RootPath, ChildPath, ManifestPath, Folders, Files);
            free(ChildPath);
            continue;
        }

        if (S_ISREG(St.st_mode)) {
            if (strcmp(ChildPath, ManifestPath) == 0) {
                free(ChildPath);
                continue;
            }
            FILE_ENTRY FileEntry = {0};
            FileEntry.AbsolutePath = ChildPath;
            FileEntry.RelativePath = RelativePathFromRoot(RootPath, ChildPath);
            FileEntry.Stat = St;
            FileListPush(Files, FileEntry);
            continue;
        }

        fprintf(stderr, "error: unsupported node type: %s\n", ChildPath);
        exit(1);
    }

    for (size_t Index = 0; Index < Names.Count; Index++) {
        free(Names.Items[Index]);
    }
    free(Names.Items);
}

/**
 * @brief Computes SHA-256 over an in-memory byte buffer.
 *
 * Uses BearSSL's SHA-256 implementation.
 *
 * @param Data Input bytes.
 * @param Size Input size in bytes.
 * @param Out Output 32-byte hash.
 */
static void Sha256Bytes(const uint8_t *Data, size_t Size, uint8_t Out[32]) {
    br_sha256_context Context;
    br_sha256_init(&Context);
    br_sha256_update(&Context, Data, Size);
    br_sha256_out(&Context, Out);
}

/**
 * @brief Encodes a file modification time into a 64-bit packed timestamp.
 *
 * If Policy is MTIME_POLICY_ZERO, returns 0. Otherwise converts st_mtime to UTC with gmtime_r() and packs fields into a custom bit layout.
 *
 * @param St stat() structure for the file.
 * @param Policy Timestamp policy (zero or source).
 *
 * @return Packed timestamp value (or 0 on policy/failed conversion).
 */
static uint64_t PackDateTime(const struct stat *St, MTIME_POLICY Policy) {
    if (Policy == MTIME_POLICY_ZERO) {
        return 0;
    }

    struct tm TmValue;
    if (gmtime_r(&St->st_mtime, &TmValue) == NULL) {
        return 0;
    }

    uint64_t Year = (uint64_t)(TmValue.tm_year + 1900);
    uint64_t Month = (uint64_t)(TmValue.tm_mon + 1);
    uint64_t Day = (uint64_t)TmValue.tm_mday;
    uint64_t Hour = (uint64_t)TmValue.tm_hour;
    uint64_t Minute = (uint64_t)TmValue.tm_min;
    uint64_t Second = (uint64_t)TmValue.tm_sec;
    uint64_t Milli = 0;

    return (Year) | (Month << 26) | (Day << 30) | (Hour << 36) | (Minute << 42) | (Second << 48) | (Milli << 54);
}

/**
 * @brief Writes a 32-bit value to a byte buffer in little-endian order.
 *
 * @param Target Destination byte buffer.
 * @param Offset Byte offset to write at.
 * @param Value Value to encode.
 */
static void WriteU32LE(uint8_t *Target, size_t Offset, uint32_t Value) {
    Target[Offset + 0] = (uint8_t)(Value & 0xFF);
    Target[Offset + 1] = (uint8_t)((Value >> 8) & 0xFF);
    Target[Offset + 2] = (uint8_t)((Value >> 16) & 0xFF);
    Target[Offset + 3] = (uint8_t)((Value >> 24) & 0xFF);
}

/**
 * @brief Writes a 64-bit value to a byte buffer in little-endian order.
 *
 * @param Target Destination byte buffer.
 * @param Offset Byte offset to write at.
 * @param Value Value to encode.
 */
static void WriteU64LE(uint8_t *Target, size_t Offset, uint64_t Value) {
    for (size_t Index = 0; Index < 8; Index++) {
        Target[Offset + Index] = (uint8_t)((Value >> (Index * 8)) & 0xFF);
    }
}

/**
 * @brief Reads an entire file into a newly allocated memory buffer.
 *
 * Returns a malloc()'d buffer; for empty files, a 1-byte allocation may be used internally but OutSize will be 0.
 *
 * @param Path Path to read.
 * @param OutSize Receives the file size in bytes.
 *
 * @return Allocated file data buffer. The caller must free().
 */
static uint8_t *ReadWholeFile(const char *Path, size_t *OutSize) {
    FILE *File = fopen(Path, "rb");
    if (File == NULL) {
        FailErrno("cannot open file", Path);
    }

    if (fseek(File, 0, SEEK_END) != 0) {
        fclose(File);
        Fail("fseek failed");
    }

    long End = ftell(File);
    if (End < 0) {
        fclose(File);
        Fail("ftell failed");
    }
    if (fseek(File, 0, SEEK_SET) != 0) {
        fclose(File);
        Fail("fseek failed");
    }

    size_t Size = (size_t)End;
    uint8_t *Data = (uint8_t *)malloc(Size > 0 ? Size : 1);
    if (Data == NULL) {
        fclose(File);
        Fail("out of memory");
    }

    if (Size > 0) {
        size_t Read = fread(Data, 1, Size, File);
        if (Read != Size) {
            fclose(File);
            free(Data);
            Fail("file read failed");
        }
    }

    fclose(File);
    *OutSize = Size;
    return Data;
}

/**
 * @brief Compresses a file chunk according to the selected compression mode.
 *
 * In COMPRESSION_MODE_NONE, returns an allocated copy and marks method as NONE. In COMPRESSION_MODE_ZLIB, always returns zlib-compressed data. In COMPRESSION_MODE_AUTO, uses zlib but falls back to storing uncompressed when compression is not beneficial.
 *
 * @param Chunk Input chunk bytes.
 * @param ChunkSize Input chunk size in bytes.
 * @param CompressionMode Compression policy (zlib/none/auto).
 * @param CompressionLevel zlib compression level (0..9).
 * @param OutData Receives a malloc()'d buffer holding the output bytes (caller must free()).
 * @param OutSize Receives the output size in bytes.
 * @param OutMethod Receives the method id written in the package (NONE or ZLIB).
 */
static void CompressChunk(const uint8_t *Chunk,
                          size_t ChunkSize,
                          COMPRESSION_MODE CompressionMode,
                          int CompressionLevel,
                          uint8_t **OutData,
                          size_t *OutSize,
                          uint8_t *OutMethod) {
    if (CompressionMode == COMPRESSION_MODE_NONE) {
        uint8_t *Copy = (uint8_t *)malloc(ChunkSize > 0 ? ChunkSize : 1);
        if (Copy == NULL) {
            Fail("out of memory");
        }
        if (ChunkSize > 0) {
            memcpy(Copy, Chunk, ChunkSize);
        }
        *OutData = Copy;
        *OutSize = ChunkSize;
        *OutMethod = EPK_COMPRESSION_METHOD_NONE;
        return;
    }

    mz_ulong Bound = mz_compressBound((mz_ulong)ChunkSize);
    uint8_t *Compressed = (uint8_t *)malloc((size_t)Bound > 0 ? (size_t)Bound : 1);
    if (Compressed == NULL) {
        Fail("out of memory");
    }

    mz_ulong CompressedSize = Bound;
    int Result = mz_compress2(Compressed, &CompressedSize, Chunk, (mz_ulong)ChunkSize, CompressionLevel);
    if (Result != MZ_OK) {
        free(Compressed);
        Fail("compression failed");
    }

    if (CompressionMode == COMPRESSION_MODE_AUTO && (size_t)CompressedSize >= ChunkSize) {
        free(Compressed);
        uint8_t *Copy = (uint8_t *)malloc(ChunkSize > 0 ? ChunkSize : 1);
        if (Copy == NULL) {
            Fail("out of memory");
        }
        if (ChunkSize > 0) {
            memcpy(Copy, Chunk, ChunkSize);
        }
        *OutData = Copy;
        *OutSize = ChunkSize;
        *OutMethod = EPK_COMPRESSION_METHOD_NONE;
        return;
    }

    *OutData = Compressed;
    *OutSize = (size_t)CompressedSize;
    *OutMethod = EPK_COMPRESSION_METHOD_ZLIB;
}

/**
 * @brief Splits a file into chunks, optionally compresses each chunk, and appends block data to the data region.
 *
 * Computes the file SHA-256 into OutFileHash. For each chunk, computes a per-chunk hash, fills a BLOCK_ENTRY, appends it to Blocks, and appends the (compressed or raw) bytes to DataRegion.
 *
 * @param FileBytes Whole file contents.
 * @param FileSize File size in bytes.
 * @param Options Packaging options (chunk size, compression settings).
 * @param Blocks Output block metadata list (appended).
 * @param DataRegion Output data region buffer receiving block payloads.
 * @param OutBlockIndexStart Receives the starting block index for this file in Blocks.
 * @param OutBlockCount Receives the number of blocks generated for this file.
 * @param OutFileHash Receives the file SHA-256 (32 bytes).
 */
static void AppendFileBlocks(const uint8_t *FileBytes,
                             size_t FileSize,
                             const OPTIONS *Options,
                             BLOCK_LIST *Blocks,
                             BYTE_BUFFER *DataRegion,
                             uint32_t *OutBlockIndexStart,
                             uint32_t *OutBlockCount,
                             uint8_t *OutFileHash) {
    Sha256Bytes(FileBytes, FileSize, OutFileHash);

    if (FileSize == 0) {
        *OutBlockIndexStart = 0;
        *OutBlockCount = 0;
        return;
    }

    uint32_t BlockStart = (uint32_t)Blocks->Count;
    uint32_t BlockCount = 0;
    size_t Cursor = 0;

    while (Cursor < FileSize) {
        size_t Remaining = FileSize - Cursor;
        size_t ChunkSize = Remaining < Options->ChunkSize ? Remaining : Options->ChunkSize;

        uint8_t *Compressed = NULL;
        size_t CompressedSize = 0;
        uint8_t CompressionMethod = EPK_COMPRESSION_METHOD_NONE;

        CompressChunk(FileBytes + Cursor,
                      ChunkSize,
                      Options->CompressionMode,
                      Options->CompressionLevel,
                      &Compressed,
                      &CompressedSize,
                      &CompressionMethod);

        BLOCK_ENTRY Block = {0};
        Block.CompressedOffset = (uint64_t)DataRegion->Size;
        Block.CompressedSize = (uint32_t)CompressedSize;
        Block.UncompressedSize = (uint32_t)ChunkSize;
        Block.CompressionMethod = CompressionMethod;
        Sha256Bytes(FileBytes + Cursor, ChunkSize, Block.ChunkHash);

        BlockListPush(Blocks, Block);
        ByteBufferAppend(DataRegion, Compressed, CompressedSize);

        free(Compressed);

        Cursor += ChunkSize;
        BlockCount++;
    }

    *OutBlockIndexStart = BlockStart;
    *OutBlockCount = BlockCount;
}

/**
 * @brief Serializes the TOC (table of contents) into the stored binary format.
 *
 * Writes a TOC header containing entry count, then writes each TOC entry header followed by the path bytes.
 *
 * @param TocEntries Sorted list of TOC entries to serialize.
 *
 * @return Serialized TOC buffer. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER BuildTocBuffer(const TOC_LIST *TocEntries) {
    BYTE_BUFFER Buffer;
    ByteBufferInit(&Buffer);

    uint8_t TocHeader[8] = {0};
    WriteU32LE(TocHeader, 0, (uint32_t)TocEntries->Count);
    ByteBufferAppend(&Buffer, TocHeader, sizeof(TocHeader));

    for (size_t Index = 0; Index < TocEntries->Count; Index++) {
        const TOC_ENTRY *Entry = &TocEntries->Items[Index];
        size_t PathLength = strlen(Entry->Path);
        uint32_t EntrySize = (uint32_t)(EPK_TOC_ENTRY_SIZE + PathLength);

        uint8_t Header[EPK_TOC_ENTRY_SIZE];
        memset(Header, 0, sizeof(Header));

        WriteU32LE(Header, 0, EntrySize);
        WriteU32LE(Header, 4, Entry->NodeType);
        WriteU32LE(Header, 8, Entry->EntryFlags);
        WriteU32LE(Header, 12, (uint32_t)PathLength);
        WriteU32LE(Header, 16, 0);
        WriteU32LE(Header, 20, Entry->Permissions);
        WriteU64LE(Header, 24, Entry->ModifiedTime);
        WriteU64LE(Header, 32, Entry->FileSize);
        WriteU64LE(Header, 40, Entry->InlineDataOffset);
        WriteU32LE(Header, 48, Entry->InlineDataSize);
        WriteU32LE(Header, 52, Entry->BlockIndexStart);
        WriteU32LE(Header, 56, Entry->BlockCount);
        memcpy(Header + 60, Entry->FileHash, EPK_HASH_SIZE);

        ByteBufferAppend(&Buffer, Header, sizeof(Header));
        ByteBufferAppend(&Buffer, Entry->Path, PathLength);
    }

    return Buffer;
}

/**
 * @brief Serializes the block table into the stored binary format.
 *
 * Each entry stores absolute compressed offset (DataOffset + CompressedOffset), sizes, method and chunk hash.
 *
 * @param Blocks Block list to serialize.
 * @param DataOffset Absolute file offset where the data region begins.
 *
 * @return Serialized block table buffer. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER BuildBlockTableBuffer(const BLOCK_LIST *Blocks, uint64_t DataOffset) {
    BYTE_BUFFER Buffer;
    ByteBufferInit(&Buffer);

    for (size_t Index = 0; Index < Blocks->Count; Index++) {
        const BLOCK_ENTRY *Block = &Blocks->Items[Index];
        uint8_t Bytes[EPK_BLOCK_ENTRY_SIZE];
        memset(Bytes, 0, sizeof(Bytes));

        WriteU64LE(Bytes, 0, DataOffset + Block->CompressedOffset);
        WriteU32LE(Bytes, 8, Block->CompressedSize);
        WriteU32LE(Bytes, 12, Block->UncompressedSize);
        Bytes[16] = Block->CompressionMethod;
        memcpy(Bytes + 20, Block->ChunkHash, EPK_HASH_SIZE);

        ByteBufferAppend(&Buffer, Bytes, sizeof(Bytes));
    }

    return Buffer;
}

/**
 * @brief Builds the complete .epk image in memory (header + TOC + block table + data + manifest + optional signature).
 *
 * The header is initially zero-filled, then populated with magic/version/flags and region offsets. OutSignatureOffset receives the absolute signature offset (or 0 when absent).
 *
 * @param TocEntries TOC entries to include (typically sorted).
 * @param Blocks Block metadata list.
 * @param DataRegion Concatenated block payload bytes.
 * @param ManifestData Manifest contents bytes (appended after data region).
 * @param ManifestSize Manifest size in bytes.
 * @param SignatureData Signature bytes to append (may be NULL when SignatureSize is 0).
 * @param SignatureSize Signature size in bytes (0 to omit).
 * @param OutSignatureOffset Receives the absolute signature file offset (0 if omitted).
 *
 * @return In-memory package buffer. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER BuildPackageBuffer(const TOC_LIST *TocEntries,
                                      const BLOCK_LIST *Blocks,
                                      const BYTE_BUFFER *DataRegion,
                                      const uint8_t *ManifestData,
                                      size_t ManifestSize,
                                      const uint8_t *SignatureData,
                                      size_t SignatureSize,
                                      uint64_t *OutSignatureOffset) {
    BYTE_BUFFER Toc = BuildTocBuffer(TocEntries);

    uint64_t TocOffset = EPK_HEADER_SIZE;
    uint64_t TocSize = Toc.Size;
    uint64_t BlockTableOffset = TocOffset + TocSize;
    uint64_t BlockTableSize = (uint64_t)Blocks->Count * EPK_BLOCK_ENTRY_SIZE;
    uint64_t DataOffset = BlockTableOffset + BlockTableSize;
    uint64_t DataSize = DataRegion->Size;
    uint64_t ManifestOffset = DataOffset + DataSize;
    uint64_t SignatureOffset = SignatureSize > 0 ? (ManifestOffset + ManifestSize) : 0;
    BYTE_BUFFER BlockTable = BuildBlockTableBuffer(Blocks, DataOffset);

    BYTE_BUFFER Package;
    ByteBufferInit(&Package);
    ByteBufferAppendZeros(&Package, EPK_HEADER_SIZE);
    ByteBufferAppend(&Package, Toc.Data, Toc.Size);
    ByteBufferAppend(&Package, BlockTable.Data, BlockTable.Size);
    ByteBufferAppend(&Package, DataRegion->Data, DataRegion->Size);
    ByteBufferAppend(&Package, ManifestData, ManifestSize);
    if (SignatureSize > 0) {
        ByteBufferAppend(&Package, SignatureData, SignatureSize);
    }

    uint32_t Flags = 0;
    for (size_t Index = 0; Index < Blocks->Count; Index++) {
        if (Blocks->Items[Index].CompressionMethod == EPK_COMPRESSION_METHOD_ZLIB) {
            Flags |= EPK_HEADER_FLAG_COMPRESSED_BLOCKS;
            break;
        }
    }
    if (SignatureSize > 0) {
        Flags |= EPK_HEADER_FLAG_HAS_SIGNATURE;
    }

    WriteU32LE(Package.Data, 0, EPK_MAGIC);
    WriteU32LE(Package.Data, 4, EPK_VERSION_1_0);
    WriteU32LE(Package.Data, 8, Flags);
    WriteU32LE(Package.Data, 12, EPK_HEADER_SIZE);
    WriteU64LE(Package.Data, 16, TocOffset);
    WriteU64LE(Package.Data, 24, TocSize);
    WriteU64LE(Package.Data, 32, BlockTableOffset);
    WriteU64LE(Package.Data, 40, BlockTableSize);
    WriteU64LE(Package.Data, 48, ManifestOffset);
    WriteU64LE(Package.Data, 56, ManifestSize);
    WriteU64LE(Package.Data, 64, SignatureOffset);
    WriteU64LE(Package.Data, 72, SignatureSize);

    ByteBufferFree(&Toc);
    ByteBufferFree(&BlockTable);

    *OutSignatureOffset = SignatureOffset;
    return Package;
}

/**
 * @brief Computes the package SHA-256 hash, optionally excluding the signature region.
 *
 * The header hash field is temporarily zeroed before hashing. When a signature is present, the signature byte range is excluded from the hash.
 *
 * @param Package Full in-memory package image.
 * @param SignatureOffset Absolute offset of the signature region within Package (0 if none).
 * @param SignatureSize Size of the signature region in bytes (0 if none).
 * @param OutHash Receives the computed SHA-256 hash (32 bytes).
 */
static void ComputePackageHash(const BYTE_BUFFER *Package,
                               uint64_t SignatureOffset,
                               uint64_t SignatureSize,
                               uint8_t OutHash[32]) {
    if (SignatureSize > 0 && (SignatureOffset + SignatureSize > Package->Size)) {
        Fail("invalid signature bounds");
    }

    uint8_t Backup[32];
    memcpy(Backup, Package->Data + HEADER_HASH_OFFSET, 32);
    memset(Package->Data + HEADER_HASH_OFFSET, 0, 32);

    br_sha256_context Context;
    br_sha256_init(&Context);

    if (SignatureSize > 0) {
        if (SignatureOffset > 0) {
            br_sha256_update(&Context, Package->Data, (size_t)SignatureOffset);
        }
        size_t TailOffset = (size_t)(SignatureOffset + SignatureSize);
        if (TailOffset < Package->Size) {
            br_sha256_update(&Context, Package->Data + TailOffset, Package->Size - TailOffset);
        }
    } else {
        br_sha256_update(&Context, Package->Data, Package->Size);
    }

    br_sha256_out(&Context, OutHash);

    memcpy(Package->Data + HEADER_HASH_OFFSET, Backup, 32);
}

/**
 * @brief Encodes bytes as lowercase hexadecimal.
 *
 * Out must provide at least (Size * 2 + 1) bytes for the NUL terminator.
 *
 * @param Data Input bytes.
 * @param Size Input size in bytes.
 * @param Out Output NUL-terminated hex string.
 */
static void BytesToHex(const uint8_t *Data, size_t Size, char *Out) {
    static const char *Digits = "0123456789abcdef";
    for (size_t Index = 0; Index < Size; Index++) {
        Out[Index * 2] = Digits[(Data[Index] >> 4) & 0xF];
        Out[Index * 2 + 1] = Digits[Data[Index] & 0xF];
    }
    Out[Size * 2] = '\0';
}

/**
 * @brief Builds the signature command line from a template and the package hash.
 *
 * If the template contains the placeholder "{hash}", it is replaced in-place. Otherwise the hash is appended as an extra argument.
 *
 * @param Template Command template.
 * @param HashHex Lowercase hex SHA-256 hash string.
 *
 * @return Newly allocated command line string. Caller must free().
 */
static char *BuildSignatureCommandLine(const char *Template, const char *HashHex) {
    const char *Placeholder = "{hash}";
    const char *At = strstr(Template, Placeholder);
    if (At == NULL) {
        size_t Size = strlen(Template) + 1 + strlen(HashHex) + 1;
        char *Line = (char *)malloc(Size);
        if (Line == NULL) {
            Fail("out of memory");
        }
        snprintf(Line, Size, "%s %s", Template, HashHex);
        return Line;
    }

    size_t Prefix = (size_t)(At - Template);
    size_t Suffix = strlen(At + strlen(Placeholder));
    size_t Total = Prefix + strlen(HashHex) + Suffix;

    char *Line = (char *)malloc(Total + 1);
    if (Line == NULL) {
        Fail("out of memory");
    }

    memcpy(Line, Template, Prefix);
    memcpy(Line + Prefix, HashHex, strlen(HashHex));
    memcpy(Line + Prefix + strlen(HashHex), At + strlen(Placeholder), Suffix);
    Line[Total] = '\0';
    return Line;
}

/**
 * @brief Runs a shell command and captures its stdout into a BYTE_BUFFER.
 *
 * Uses popen()/pclose(). Fails if the command cannot start, read fails, or returns non-zero status.
 *
 * @param CommandLine Command line passed to popen().
 *
 * @return Captured stdout bytes. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER RunCommandCapture(const char *CommandLine) {
    FILE *Pipe = popen(CommandLine, "r");
    if (Pipe == NULL) {
        Fail("signature command start failed");
    }

    BYTE_BUFFER Output;
    ByteBufferInit(&Output);

    uint8_t Temp[4096];
    while (!feof(Pipe)) {
        size_t Read = fread(Temp, 1, sizeof(Temp), Pipe);
        if (Read > 0) {
            ByteBufferAppend(&Output, Temp, Read);
        }
        if (ferror(Pipe)) {
            pclose(Pipe);
            ByteBufferFree(&Output);
            Fail("signature command read failed");
        }
    }

    int Status = pclose(Pipe);
    if (Status != 0) {
        ByteBufferFree(&Output);
        Fail("signature command returned non-zero status");
    }

    return Output;
}

/**
 * @brief Converts a single hexadecimal character to its numeric value.
 *
 * @param Value Input character.
 *
 * @return Nibble value 0..15, or 0xFF if the character is not valid hex.
 */
static uint8_t HexNibble(char Value) {
    if (Value >= '0' && Value <= '9') return (uint8_t)(Value - '0');
    if (Value >= 'a' && Value <= 'f') return (uint8_t)(Value - 'a' + 10);
    if (Value >= 'A' && Value <= 'F') return (uint8_t)(Value - 'A' + 10);
    return 0xFF;
}

/**
 * @brief Decodes a whitespace-tolerant hex buffer into raw bytes.
 *
 * Whitespace is removed before decoding; fails if the remaining length is zero or odd or contains invalid hex.
 *
 * @param Input Input buffer containing ASCII hex (may include whitespace).
 *
 * @return Decoded bytes. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER DecodeHexBuffer(const BYTE_BUFFER *Input) {
    BYTE_BUFFER Filtered;
    ByteBufferInit(&Filtered);

    for (size_t Index = 0; Index < Input->Size; Index++) {
        if (!isspace(Input->Data[Index])) {
            ByteBufferAppend(&Filtered, Input->Data + Index, 1);
        }
    }

    if (Filtered.Size == 0 || (Filtered.Size % 2) != 0) {
        ByteBufferFree(&Filtered);
        Fail("signature command did not output valid hex data");
    }

    BYTE_BUFFER Decoded;
    ByteBufferInit(&Decoded);
    ByteBufferReserve(&Decoded, Filtered.Size / 2);

    for (size_t Index = 0; Index < Filtered.Size; Index += 2) {
        uint8_t High = HexNibble((char)Filtered.Data[Index]);
        uint8_t Low = HexNibble((char)Filtered.Data[Index + 1]);
        if (High == 0xFF || Low == 0xFF) {
            ByteBufferFree(&Filtered);
            ByteBufferFree(&Decoded);
            Fail("signature command did not output valid hex data");
        }
        uint8_t Value = (uint8_t)((High << 4) | Low);
        ByteBufferAppend(&Decoded, &Value, 1);
    }

    ByteBufferFree(&Filtered);
    return Decoded;
}

/**
 * @brief Executes the external signature command and returns the produced signature bytes.
 *
 * The command receives the package hash. Output can be treated as raw bytes or as ASCII hex (decoded) depending on OutputMode.
 *
 * @param CommandTemplate Signature command template (may contain "{hash}" placeholder).
 * @param OutputMode How to interpret the command stdout (raw or hex).
 * @param Hash Package hash (32 bytes) to pass to the signer.
 *
 * @return Signature bytes. Caller must ByteBufferFree() it.
 */
static BYTE_BUFFER ExecuteSignatureHook(const char *CommandTemplate,
                                        SIGNATURE_OUTPUT OutputMode,
                                        const uint8_t Hash[32]) {
    char HashHex[65];
    BytesToHex(Hash, 32, HashHex);

    char *CommandLine = BuildSignatureCommandLine(CommandTemplate, HashHex);
    BYTE_BUFFER Raw = RunCommandCapture(CommandLine);
    free(CommandLine);

    if (OutputMode == SIGNATURE_OUTPUT_RAW) {
        if (Raw.Size == 0) {
            ByteBufferFree(&Raw);
            Fail("signature command output is empty");
        }
        return Raw;
    }

    BYTE_BUFFER Decoded = DecodeHexBuffer(&Raw);
    ByteBufferFree(&Raw);
    if (Decoded.Size == 0) {
        ByteBufferFree(&Decoded);
        Fail("signature command output is empty");
    }
    return Decoded;
}

/**
 * @brief Ensures the parent folder of an output file path exists.
 *
 * Uses `mkdir -p` via system() on the path's parent directory (based on the last '/').
 *
 * @param OutputPath Destination file path whose parent directory must exist.
 */
static void EnsureFolderExistsForFile(const char *OutputPath) {
    char *Copy = DupString(OutputPath);
    char *Slash = strrchr(Copy, '/');
    if (Slash != NULL) {
        *Slash = '\0';
        char Command[4096];
        snprintf(Command, sizeof(Command), "mkdir -p '%s'", Copy);
        int Result = system(Command);
        free(Copy);
        if (Result != 0) {
            Fail("cannot create output folder");
        }
        return;
    }
    free(Copy);
}

/**
 * @brief Writes an in-memory buffer to storage.
 *
 * Creates the output folder when needed and writes Buffer->Data to Path.
 *
 * @param Path Destination path.
 * @param Buffer Buffer to write.
 */
static void SaveBufferToFile(const char *Path, const BYTE_BUFFER *Buffer) {
    EnsureFolderExistsForFile(Path);
    FILE *File = fopen(Path, "wb");
    if (File == NULL) {
        FailErrno("cannot open output", Path);
    }

    if (Buffer->Size > 0) {
        size_t Written = fwrite(Buffer->Data, 1, Buffer->Size, File);
        if (Written != Buffer->Size) {
            fclose(File);
            Fail("file write failed");
        }
    }

    fclose(File);
}

/**
 * @brief Parses command-line arguments into an OPTIONS structure.
 *
 * Supports the `pack` command. Applies defaults and validates required options.
 *
 * @param Argc Argument count.
 * @param Argv Argument vector.
 * @param Options Output options structure.
 */
static void ParseOptions(int Argc, char **Argv, OPTIONS *Options) {
    memset(Options, 0, sizeof(*Options));
    Options->ChunkSize = 65536;
    Options->CompressionLevel = 9;
    Options->CompressionMode = COMPRESSION_MODE_ZLIB;
    Options->MTimePolicy = MTIME_POLICY_ZERO;
    Options->SignatureOutput = SIGNATURE_OUTPUT_RAW;

    if (Argc < 2 || strcmp(Argv[1], "--help") == 0 || strcmp(Argv[1], "-h") == 0) {
        Usage();
        exit(0);
    }

    if (strcmp(Argv[1], "pack") != 0) {
        Fail("unsupported command");
    }

    for (int Index = 2; Index < Argc; Index++) {
        const char *Option = Argv[Index];
        if (strcmp(Option, "--input") == 0 && Index + 1 < Argc) {
            Options->InputPath = Argv[++Index];
        } else if (strcmp(Option, "--output") == 0 && Index + 1 < Argc) {
            Options->OutputPath = Argv[++Index];
        } else if (strcmp(Option, "--manifest") == 0 && Index + 1 < Argc) {
            Options->ManifestPath = Argv[++Index];
        } else if (strcmp(Option, "--chunk-size") == 0 && Index + 1 < Argc) {
            long Value = strtol(Argv[++Index], NULL, 10);
            if (Value <= 0) Fail("invalid chunk-size");
            Options->ChunkSize = (uint32_t)Value;
        } else if (strcmp(Option, "--compression") == 0 && Index + 1 < Argc) {
            const char *Value = Argv[++Index];
            if (strcmp(Value, "zlib") == 0) Options->CompressionMode = COMPRESSION_MODE_ZLIB;
            else if (strcmp(Value, "none") == 0) Options->CompressionMode = COMPRESSION_MODE_NONE;
            else if (strcmp(Value, "auto") == 0) Options->CompressionMode = COMPRESSION_MODE_AUTO;
            else Fail("invalid compression");
        } else if (strcmp(Option, "--compression-level") == 0 && Index + 1 < Argc) {
            long Value = strtol(Argv[++Index], NULL, 10);
            if (Value < 0 || Value > 9) Fail("invalid compression-level");
            Options->CompressionLevel = (int)Value;
        } else if (strcmp(Option, "--mtime-policy") == 0 && Index + 1 < Argc) {
            const char *Value = Argv[++Index];
            if (strcmp(Value, "zero") == 0) Options->MTimePolicy = MTIME_POLICY_ZERO;
            else if (strcmp(Value, "source") == 0) Options->MTimePolicy = MTIME_POLICY_SOURCE;
            else Fail("invalid mtime-policy");
        } else if (strcmp(Option, "--signature-command") == 0 && Index + 1 < Argc) {
            Options->SignatureCommand = Argv[++Index];
        } else if (strcmp(Option, "--signature-output") == 0 && Index + 1 < Argc) {
            const char *Value = Argv[++Index];
            if (strcmp(Value, "raw") == 0) Options->SignatureOutput = SIGNATURE_OUTPUT_RAW;
            else if (strcmp(Value, "hex") == 0) Options->SignatureOutput = SIGNATURE_OUTPUT_HEX;
            else Fail("invalid signature-output");
        } else {
            Fail("invalid option");
        }
    }

    if (Options->InputPath == NULL) Fail("--input is required");
    if (Options->OutputPath == NULL) Fail("--output is required");
}

/**
 * @brief Program entry point: builds an .epk package from an input folder.
 *
 * Resolves input/manifest paths, walks the input tree, builds TOC and block data, optionally runs a signature hook, computes the final package hash and writes the output file.
 *
 * @param Argc Argument count.
 * @param Argv Argument vector.
 *
 * @return Process exit code (0 on success).
 */
int main(int Argc, char **Argv) {
    OPTIONS Options;
    ParseOptions(Argc, Argv, &Options);

    char InputResolved[4096];
    char ManifestResolved[4096];

    if (realpath(Options.InputPath, InputResolved) == NULL) {
        FailErrno("cannot resolve input path", Options.InputPath);
    }

    struct stat InputStat;
    if (stat(InputResolved, &InputStat) != 0 || !S_ISDIR(InputStat.st_mode)) {
        Fail("input must be a folder");
    }

    char ManifestCandidate[4096];
    if (Options.ManifestPath != NULL) {
        strncpy(ManifestCandidate, Options.ManifestPath, sizeof(ManifestCandidate) - 1);
        ManifestCandidate[sizeof(ManifestCandidate) - 1] = '\0';
    } else {
        int Written = snprintf(ManifestCandidate, sizeof(ManifestCandidate), "%s/manifest.toml", InputResolved);
        if (Written < 0 || (size_t)Written >= sizeof(ManifestCandidate)) {
            Fail("manifest path too long");
        }
    }

    if (realpath(ManifestCandidate, ManifestResolved) == NULL) {
        FailErrno("cannot resolve manifest path", ManifestCandidate);
    }

    size_t ManifestSize = 0;
    uint8_t *ManifestData = ReadWholeFile(ManifestResolved, &ManifestSize);

    TEXT_LIST FolderPaths = {0};
    FILE_LIST Files = {0};
    WalkInputTree(InputResolved, InputResolved, ManifestResolved, &FolderPaths, &Files);

    qsort(FolderPaths.Items, FolderPaths.Count, sizeof(char *), CompareCStrings);
    qsort(Files.Items, Files.Count, sizeof(FILE_ENTRY), CompareFileEntries);

    TOC_LIST TocEntries = {0};
    BLOCK_LIST Blocks = {0};
    BYTE_BUFFER DataRegion;
    ByteBufferInit(&DataRegion);

    for (size_t Index = 0; Index < FolderPaths.Count; Index++) {
        TOC_ENTRY Entry;
        memset(&Entry, 0, sizeof(Entry));
        Entry.NodeType = EPK_NODE_TYPE_FOLDER;
        Entry.EntryFlags = 0;
        Entry.Path = DupString(FolderPaths.Items[Index]);
        Entry.Permissions = 0x1FF;
        Entry.ModifiedTime = 0;
        TocListPush(&TocEntries, Entry);
    }

    for (size_t Index = 0; Index < Files.Count; Index++) {
        size_t FileSize = 0;
        uint8_t *FileBytes = ReadWholeFile(Files.Items[Index].AbsolutePath, &FileSize);

        TOC_ENTRY Entry;
        memset(&Entry, 0, sizeof(Entry));
        Entry.NodeType = EPK_NODE_TYPE_FILE;
        Entry.Path = DupString(Files.Items[Index].RelativePath);
        Entry.Permissions = (uint32_t)(Files.Items[Index].Stat.st_mode & 0x1FF);
        Entry.ModifiedTime = PackDateTime(&Files.Items[Index].Stat, Options.MTimePolicy);
        Entry.FileSize = (uint64_t)FileSize;

        if (FileSize == 0) {
            Entry.EntryFlags = EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA;
            memset(Entry.FileHash, 0, sizeof(Entry.FileHash));
        } else {
            Entry.EntryFlags = EPK_TOC_ENTRY_FLAG_HAS_BLOCKS;
            AppendFileBlocks(FileBytes,
                             FileSize,
                             &Options,
                             &Blocks,
                             &DataRegion,
                             &Entry.BlockIndexStart,
                             &Entry.BlockCount,
                             Entry.FileHash);
        }

        TocListPush(&TocEntries, Entry);
        free(FileBytes);
    }

    qsort(TocEntries.Items, TocEntries.Count, sizeof(TOC_ENTRY), CompareTocEntries);

    BYTE_BUFFER Signature = {0};
    BYTE_BUFFER FinalPackage = {0};
    uint8_t PackageHash[32];

    if (Options.SignatureCommand == NULL) {
        uint64_t SignatureOffset = 0;
        FinalPackage = BuildPackageBuffer(&TocEntries,
                                          &Blocks,
                                          &DataRegion,
                                          ManifestData,
                                          ManifestSize,
                                          NULL,
                                          0,
                                          &SignatureOffset);
        ComputePackageHash(&FinalPackage, SignatureOffset, 0, PackageHash);
    } else {
        size_t SignatureSizeGuess = 64;
        for (int Attempt = 0; Attempt < 4; Attempt++) {
            BYTE_BUFFER Placeholder;
            ByteBufferInit(&Placeholder);
            ByteBufferAppendZeros(&Placeholder, SignatureSizeGuess);

            uint64_t SignatureOffset = 0;
            BYTE_BUFFER Draft = BuildPackageBuffer(&TocEntries,
                                                   &Blocks,
                                                   &DataRegion,
                                                   ManifestData,
                                                   ManifestSize,
                                                   Placeholder.Data,
                                                   Placeholder.Size,
                                                   &SignatureOffset);

            uint8_t DraftHash[32];
            ComputePackageHash(&Draft, SignatureOffset, Placeholder.Size, DraftHash);
            ByteBufferFree(&Draft);
            ByteBufferFree(&Placeholder);

            Signature = ExecuteSignatureHook(Options.SignatureCommand, Options.SignatureOutput, DraftHash);
            if (Signature.Size != SignatureSizeGuess) {
                SignatureSizeGuess = Signature.Size;
                ByteBufferFree(&Signature);
                continue;
            }

            FinalPackage = BuildPackageBuffer(&TocEntries,
                                              &Blocks,
                                              &DataRegion,
                                              ManifestData,
                                              ManifestSize,
                                              Signature.Data,
                                              Signature.Size,
                                              &SignatureOffset);
            ComputePackageHash(&FinalPackage, SignatureOffset, Signature.Size, PackageHash);
            break;
        }

        if (FinalPackage.Data == NULL) {
            Fail("signature size did not stabilize");
        }
    }

    memcpy(FinalPackage.Data + HEADER_HASH_OFFSET, PackageHash, 32);
    SaveBufferToFile(Options.OutputPath, &FinalPackage);

    char HashHex[65];
    BytesToHex(PackageHash, 32, HashHex);

    printf("Wrote package: %s\n", Options.OutputPath);
    printf("Package size: %zu bytes\n", FinalPackage.Size);
    printf("Package hash (sha256): %s\n", HashHex);
    printf("TOC entries: %zu\n", TocEntries.Count);
    printf("Blocks: %zu\n", Blocks.Count);
    printf("Signature bytes: %zu\n", Signature.Size);

    free(ManifestData);
    ByteBufferFree(&DataRegion);
    ByteBufferFree(&FinalPackage);
    ByteBufferFree(&Signature);

    for (size_t Index = 0; Index < FolderPaths.Count; Index++) {
        free(FolderPaths.Items[Index]);
    }
    free(FolderPaths.Items);

    for (size_t Index = 0; Index < Files.Count; Index++) {
        free(Files.Items[Index].RelativePath);
        free(Files.Items[Index].AbsolutePath);
    }
    free(Files.Items);

    for (size_t Index = 0; Index < TocEntries.Count; Index++) {
        free(TocEntries.Items[Index].Path);
    }
    free(TocEntries.Items);
    free(Blocks.Items);

    return 0;
}
