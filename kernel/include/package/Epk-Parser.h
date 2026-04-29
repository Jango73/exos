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


    EPK package parser and validator

\************************************************************************/

#ifndef EPK_PARSER_H_INCLUDED
#define EPK_PARSER_H_INCLUDED

#include "Base.h"
#include "package/EpkFormat.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

typedef struct tag_EPK_PARSER_OPTIONS {
    BOOL VerifyPackageHash;
    BOOL VerifySignature;
    BOOL RequireSignature;
} EPK_PARSER_OPTIONS, *LPEPK_PARSER_OPTIONS;

typedef struct tag_EPK_PARSED_TOC_ENTRY {
    U32 NodeType;
    U32 EntryFlags;
    U32 Permissions;
    U64 ModifiedTime;
    U64 FileSize;
    U64 InlineDataOffset;
    U32 InlineDataSize;
    U32 BlockIndexStart;
    U32 BlockCount;
    U8 FileHash[EPK_HASH_SIZE];
    U32 PathOffset;
    U32 PathLength;
    U32 AliasTargetOffset;
    U32 AliasTargetLength;
} EPK_PARSED_TOC_ENTRY, *LPEPK_PARSED_TOC_ENTRY;

typedef struct tag_EPK_PARSED_BLOCK_ENTRY {
    U64 CompressedOffset;
    U32 CompressedSize;
    U32 UncompressedSize;
    U8 CompressionMethod;
    U8 ChunkHash[EPK_HASH_SIZE];
} EPK_PARSED_BLOCK_ENTRY, *LPEPK_PARSED_BLOCK_ENTRY;

typedef struct tag_EPK_VALIDATED_PACKAGE {
    const U8* PackageBytes;
    U32 PackageSize;
    EPK_HEADER Header;

    U32 TocOffset;
    U32 TocSize;
    U32 TocEntryCount;
    LPEPK_PARSED_TOC_ENTRY TocEntries;

    U32 BlockTableOffset;
    U32 BlockTableSize;
    U32 BlockCount;
    LPEPK_PARSED_BLOCK_ENTRY BlockEntries;

    U32 ManifestOffset;
    U32 ManifestSize;

    U32 SignatureOffset;
    U32 SignatureSize;
} EPK_VALIDATED_PACKAGE, *LPEPK_VALIDATED_PACKAGE;

/***************************************************************************/

U32 EpkValidatePackageBuffer(const void* PackageBytes,
                             U32 PackageSize,
                             const EPK_PARSER_OPTIONS* Options,
                             LPEPK_VALIDATED_PACKAGE OutPackage);

void EpkReleaseValidatedPackage(LPEPK_VALIDATED_PACKAGE Package);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
