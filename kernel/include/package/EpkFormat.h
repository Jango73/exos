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


    EPK package stored format constants and structures

\************************************************************************/

#ifndef EPK_FORMAT_H_INCLUDED
#define EPK_FORMAT_H_INCLUDED

#include "Base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

#define EPK_MAGIC 0x314B5045

#define EPK_VERSION_MAKE(Major, Minor) (((U32)(Major) << 16) | ((U32)(Minor)))
#define EPK_VERSION_MAJOR(Version) ((U16)(((Version) >> 16) & 0xFFFF))
#define EPK_VERSION_MINOR(Version) ((U16)((Version) & 0xFFFF))
#define EPK_VERSION_1_0 EPK_VERSION_MAKE(1, 0)

#define EPK_HASH_SIZE 32

#define EPK_HEADER_SIZE 128
#define EPK_HEADER_PACKAGE_HASH_OFFSET 80
#define EPK_TOC_HEADER_SIZE 8
#define EPK_TOC_ENTRY_SIZE 96
#define EPK_BLOCK_ENTRY_SIZE 52

#define EPK_HEADER_FLAG_COMPRESSED_BLOCKS 0x00000001
#define EPK_HEADER_FLAG_HAS_SIGNATURE 0x00000002
#define EPK_HEADER_FLAG_ENCRYPTED_CONTENT 0x00000004
#define EPK_HEADER_FLAG_MASK_KNOWN                                                \
    (EPK_HEADER_FLAG_COMPRESSED_BLOCKS | EPK_HEADER_FLAG_HAS_SIGNATURE |          \
     EPK_HEADER_FLAG_ENCRYPTED_CONTENT)

#define EPK_NODE_TYPE_FILE 1
#define EPK_NODE_TYPE_FOLDER 2
#define EPK_NODE_TYPE_FOLDER_ALIAS 3

#define EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA 0x00000001
#define EPK_TOC_ENTRY_FLAG_HAS_BLOCKS 0x00000002
#define EPK_TOC_ENTRY_FLAG_HAS_ALIAS_TARGET 0x00000004
#define EPK_TOC_ENTRY_FLAG_MASK_KNOWN                                             \
    (EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA | EPK_TOC_ENTRY_FLAG_HAS_BLOCKS |         \
     EPK_TOC_ENTRY_FLAG_HAS_ALIAS_TARGET)

#define EPK_COMPRESSION_METHOD_NONE 0
#define EPK_COMPRESSION_METHOD_ZLIB 1

#define EPK_VALIDATION_OK 0
#define EPK_VALIDATION_INVALID_ARGUMENT 1
#define EPK_VALIDATION_INVALID_MAGIC 2
#define EPK_VALIDATION_UNSUPPORTED_VERSION 3
#define EPK_VALIDATION_UNSUPPORTED_FLAGS 4
#define EPK_VALIDATION_INVALID_HEADER_SIZE 5
#define EPK_VALIDATION_INVALID_BOUNDS 6
#define EPK_VALIDATION_INVALID_ALIGNMENT 7
#define EPK_VALIDATION_INVALID_SECTION_ORDER 8
#define EPK_VALIDATION_INVALID_TABLE_FORMAT 9
#define EPK_VALIDATION_INVALID_ENTRY_FORMAT 10
#define EPK_VALIDATION_HASH_MISMATCH 11
#define EPK_VALIDATION_SIGNATURE_FAILED 12
#define EPK_VALIDATION_OUT_OF_MEMORY 13

/***************************************************************************/

typedef struct PACKED tag_EPK_HEADER {
    U32 Magic;
    U32 Version;
    U32 Flags;
    U32 HeaderSize;
    U64 TocOffset;
    U64 TocSize;
    U64 BlockTableOffset;
    U64 BlockTableSize;
    U64 ManifestOffset;
    U64 ManifestSize;
    U64 SignatureOffset;
    U64 SignatureSize;
    U8 PackageHash[EPK_HASH_SIZE];
    U8 Reserved[16];
} EPK_HEADER;

typedef struct PACKED tag_EPK_TOC_HEADER {
    U32 EntryCount;
    U32 Reserved;
} EPK_TOC_HEADER;

typedef struct PACKED tag_EPK_TOC_ENTRY {
    U32 EntrySize;
    U32 NodeType;
    U32 EntryFlags;
    U32 PathLength;
    U32 AliasTargetLength;
    U32 Permissions;
    U64 ModifiedTime;
    U64 FileSize;
    U64 InlineDataOffset;
    U32 InlineDataSize;
    U32 BlockIndexStart;
    U32 BlockCount;
    U8 FileHash[EPK_HASH_SIZE];
    U32 Reserved;
} EPK_TOC_ENTRY;

typedef struct PACKED tag_EPK_BLOCK_ENTRY {
    U64 CompressedOffset;
    U32 CompressedSize;
    U32 UncompressedSize;
    U8 CompressionMethod;
    U8 Reserved0;
    U16 Reserved1;
    U8 ChunkHash[EPK_HASH_SIZE];
} EPK_BLOCK_ENTRY;

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
