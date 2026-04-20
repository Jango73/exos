
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


    Executable module image cache

\************************************************************************/

#ifndef EXECUTABLE_MODULE_H_INCLUDED
#define EXECUTABLE_MODULE_H_INCLUDED

/***************************************************************************/

#include "../Base.h"
#include "../exec/Executable.h"
#include "../fs/FileSystem.h"
#include "../sync/Mutex.h"
#include "../User.h"

/***************************************************************************/

typedef struct tag_EXECUTABLE_MODULE_FILE_IDENTITY {
    LPFILESYSTEM FileSystem;
    UINT FileSize;
    DATETIME Modified;
    STR Name[MAX_FILE_NAME];
} EXECUTABLE_MODULE_FILE_IDENTITY, *LPEXECUTABLE_MODULE_FILE_IDENTITY;

/***************************************************************************/

typedef struct tag_EXECUTABLE_MODULE_SHARED_SEGMENT {
    BOOL Present;
    UINT SegmentIndex;
    UINT AlignedVirtualAddress;
    UINT VirtualAddressOffset;
    UINT FileBackedSize;
    UINT MemorySize;
    UINT PageCount;
    PHYSICAL* PhysicalPages;
} EXECUTABLE_MODULE_SHARED_SEGMENT, *LPEXECUTABLE_MODULE_SHARED_SEGMENT;

/***************************************************************************/

typedef struct tag_EXECUTABLE_MODULE_IMAGE {
    LISTNODE_FIELDS
    MUTEX Mutex;
    EXECUTABLE_MODULE_FILE_IDENTITY Identity;
    EXECUTABLE_METADATA Metadata;
    UINT SharedSegmentCount;
    UINT PrivateSegmentCount;
    EXECUTABLE_MODULE_SHARED_SEGMENT SharedSegments[EXECUTABLE_MAX_SEGMENTS];
    EXECUTABLE_MODULE_SHARED_SEGMENT PrivateSegments[EXECUTABLE_MAX_SEGMENTS];
} EXECUTABLE_MODULE_IMAGE, *LPEXECUTABLE_MODULE_IMAGE;

/***************************************************************************/

LPEXECUTABLE_MODULE_IMAGE AcquireExecutableModuleImage(LPFILE File);
void RetainExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image);
void ReleaseExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image);
BOOL RelocateExecutableModuleBinding(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    EXECUTABLE_SYMBOL_RESOLVER Resolver,
    LPVOID ResolverContext);
void DeleteExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image);

/***************************************************************************/

#endif
