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


    PackageFS internal declarations

\************************************************************************/

#ifndef PACKAGEFS_INTERNAL_H_INCLUDED
#define PACKAGEFS_INTERNAL_H_INCLUDED

#include "sync/Mutex.h"
#include "package/PackageFS.h"
#include "utils/ChunkCache.h"

/************************************************************************/

#define PACKAGEFS_ALIAS_MAX_DEPTH 32
#define PACKAGEFS_NODE_TYPE_ROOT 0
#define PACKAGEFS_CHUNK_CACHE_CAPACITY 96
#define PACKAGEFS_CHUNK_CACHE_TTL_MS 10000

/************************************************************************/

typedef struct tag_PACKAGEFS_NODE PACKAGE_FS_NODE, *LPPACKAGEFS_NODE;

typedef struct tag_PACKAGEFSFILESYSTEM {
    FILESYSTEM Header;
    MUTEX FilesMutex;
    CHUNK_CACHE ChunkCache;
    U8* PackageBytes;
    U32 PackageSize;
    EPK_VALIDATED_PACKAGE Package;
    LPPACKAGEFS_NODE Root;
} PACKAGE_FS_FILESYSTEM, *LPPACKAGEFSFILESYSTEM;

typedef struct tag_PACKAGEFS_NODE {
    LPPACKAGEFS_NODE ParentNode;
    LPPACKAGEFS_NODE FirstChild;
    LPPACKAGEFS_NODE NextSibling;
    U32 NodeType;
    U32 Attributes;
    U32 TocIndex;
    BOOL Defined;
    DATETIME Modified;
    STR Name[MAX_FILE_NAME];
    STR AliasTarget[MAX_PATH_NAME];
} PACKAGE_FS_NODE;

typedef struct tag_PACKAGEFSFILE {
    FILE Header;
    LPPACKAGEFS_NODE Node;
    LPPACKAGEFS_NODE EnumerationCursor;
    BOOL Enumerate;
    STR Pattern[MAX_FILE_NAME];
} PACKAGE_FS_FILE, *LPPACKAGEFSFILE;

/************************************************************************/

U32 PackageFSBuildTree(LPPACKAGEFSFILESYSTEM FileSystem);

void PackageFSReleaseNodeTree(LPPACKAGEFS_NODE Node);

LPPACKAGEFS_NODE PackageFSResolvePath(LPPACKAGEFSFILESYSTEM FileSystem,
                                      LPCSTR Path,
                                      BOOL FollowFinalAlias);

LPPACKAGEFSFILE PackageFSOpenFile(LPFILE_INFO Info);

U32 PackageFSOpenNext(LPPACKAGEFSFILE File);

U32 PackageFSCloseFile(LPPACKAGEFSFILE File);

U32 PackageFSReadFile(LPPACKAGEFSFILE File);

U32 PackageFSWriteFile(LPPACKAGEFSFILE File);

BOOL PackageFSPathExists(LPFILESYSTEM_PATHCHECK Check);

BOOL PackageFSFileExists(LPFILE_INFO Info);

U32 PackageFSGetVolumeInfo(LPVOLUME_INFO Info);

/************************************************************************/

#endif
