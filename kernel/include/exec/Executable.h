
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


    Executable

\************************************************************************/

#ifndef EXECUTABLE_H_INCLUDED
#define EXECUTABLE_H_INCLUDED

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../Base.h"

/***************************************************************************/

typedef struct tag_FILE FILE, *LPFILE;

/***************************************************************************/

#define EXECUTABLE_FORMAT_UNKNOWN 0
#define EXECUTABLE_FORMAT_EXOS 1
#define EXECUTABLE_FORMAT_ELF 2

#define EXECUTABLE_TARGET_IMAGE 1
#define EXECUTABLE_TARGET_MODULE 2

#define EXECUTABLE_ARCHITECTURE_UNKNOWN 0
#define EXECUTABLE_ARCHITECTURE_X86_32 1
#define EXECUTABLE_ARCHITECTURE_X86_64 2

#define EXECUTABLE_SEGMENT_ACCESS_READ 0x00000001
#define EXECUTABLE_SEGMENT_ACCESS_WRITE 0x00000002
#define EXECUTABLE_SEGMENT_ACCESS_EXECUTE 0x00000004

#define EXECUTABLE_SEGMENT_MAPPING_NONE 0
#define EXECUTABLE_SEGMENT_MAPPING_CODE 1
#define EXECUTABLE_SEGMENT_MAPPING_DATA 2
#define EXECUTABLE_SEGMENT_MAPPING_TLS 3

#define EXECUTABLE_RELOCATION_TABLE_NONE 0
#define EXECUTABLE_RELOCATION_TABLE_REL 1
#define EXECUTABLE_RELOCATION_TABLE_RELA 2
#define EXECUTABLE_RELOCATION_TABLE_PLT_REL 3
#define EXECUTABLE_RELOCATION_TABLE_PLT_RELA 4

#define EXECUTABLE_MAX_SEGMENTS 16
#define EXECUTABLE_MAX_RELOCATION_TABLES 8

#define EXECUTABLE_SYMBOL_BIND_LOCAL 0
#define EXECUTABLE_SYMBOL_BIND_GLOBAL 1
#define EXECUTABLE_SYMBOL_BIND_WEAK 2

#define EXECUTABLE_SYMBOL_TYPE_NONE 0
#define EXECUTABLE_SYMBOL_TYPE_OBJECT 1
#define EXECUTABLE_SYMBOL_TYPE_FUNCTION 2
#define EXECUTABLE_SYMBOL_TYPE_SECTION 3
#define EXECUTABLE_SYMBOL_TYPE_FILE 4

/***************************************************************************/

typedef struct tag_EXECUTABLE_INFO {
    UINT EntryPoint;
    UINT CodeBase;
    UINT CodeSize;
    UINT DataBase;
    UINT DataSize;
    UINT BssBase;
    UINT BssSize;
    UINT StackMinimum;
    UINT StackRequested;
    UINT HeapMinimum;
    UINT HeapRequested;
} EXECUTABLE_INFO, *LPEXECUTABLE_INFO;

/***************************************************************************/

typedef struct tag_EXECUTABLE_SEGMENT_DESCRIPTOR {
    U32 SourceType;
    U32 Access;
    U32 Mapping;
    UINT FileOffset;
    UINT VirtualAddress;
    UINT FileSize;
    UINT MemorySize;
    UINT Alignment;
} EXECUTABLE_SEGMENT_DESCRIPTOR, *LPEXECUTABLE_SEGMENT_DESCRIPTOR;

/***************************************************************************/

typedef struct tag_EXECUTABLE_SYMBOL_TABLE_INFO {
    BOOL Present;
    UINT StringTableAddress;
    UINT StringTableSize;
    UINT SymbolTableAddress;
    UINT SymbolTableSize;
    UINT SymbolEntrySize;
    UINT HashTableAddress;
    UINT GnuHashTableAddress;
} EXECUTABLE_SYMBOL_TABLE_INFO, *LPEXECUTABLE_SYMBOL_TABLE_INFO;

/***************************************************************************/

typedef struct tag_EXECUTABLE_RELOCATION_TABLE_INFO {
    U32 Type;
    UINT VirtualAddress;
    UINT Size;
    UINT EntrySize;
} EXECUTABLE_RELOCATION_TABLE_INFO, *LPEXECUTABLE_RELOCATION_TABLE_INFO;

/***************************************************************************/

typedef struct tag_EXECUTABLE_DYNAMIC_INFO {
    BOOL Present;
    BOOL RequiresInterpreter;
    BOOL RequiresTextRelocation;
    BOOL HasConstructors;
    UINT DynamicTableAddress;
    UINT DynamicTableSize;
    UINT NeededLibraryCount;
    EXECUTABLE_SYMBOL_TABLE_INFO SymbolTable;
    UINT RelocationTableCount;
    EXECUTABLE_RELOCATION_TABLE_INFO RelocationTables[EXECUTABLE_MAX_RELOCATION_TABLES];
} EXECUTABLE_DYNAMIC_INFO, *LPEXECUTABLE_DYNAMIC_INFO;

/***************************************************************************/

typedef struct tag_EXECUTABLE_TLS_INFO {
    BOOL Present;
    UINT TemplateAddress;
    UINT TemplateFileOffset;
    UINT TemplateSize;
    UINT TotalSize;
    UINT Alignment;
} EXECUTABLE_TLS_INFO, *LPEXECUTABLE_TLS_INFO;

/***************************************************************************/

typedef struct tag_EXECUTABLE_SYMBOL_RESOLUTION {
    LPCSTR Name;
    UINT SourceSymbolIndex;
    BOOL Required;
    LINEAR Address;
} EXECUTABLE_SYMBOL_RESOLUTION, *LPEXECUTABLE_SYMBOL_RESOLUTION;

typedef BOOL (*EXECUTABLE_SYMBOL_RESOLVER)(LPVOID Context, LPEXECUTABLE_SYMBOL_RESOLUTION Resolution);
typedef LINEAR (*EXECUTABLE_VIRTUAL_ADDRESS_MAPPER)(LPVOID Context, UINT VirtualAddress);

/***************************************************************************/

typedef struct tag_EXECUTABLE_METADATA {
    U32 Format;
    U32 Target;
    U32 Architecture;
    UINT EntryPoint;
    EXECUTABLE_INFO Layout;
    UINT SegmentCount;
    EXECUTABLE_SEGMENT_DESCRIPTOR Segments[EXECUTABLE_MAX_SEGMENTS];
    EXECUTABLE_DYNAMIC_INFO Dynamic;
    EXECUTABLE_TLS_INFO Tls;
} EXECUTABLE_METADATA, *LPEXECUTABLE_METADATA;

/***************************************************************************/
// Load request: caller provides actual target bases where segments will land.

typedef struct tag_EXECUTABLE_LOAD {
    LPFILE File;
    LPEXECUTABLE_INFO Info;
    LINEAR CodeBase;
    LINEAR DataBase;
    LINEAR BssBase;
} EXECUTABLE_LOAD, *LPEXECUTABLE_LOAD;

/***************************************************************************/

BOOL GetExecutableImageInfo(LPFILE, LPEXECUTABLE_METADATA);
BOOL GetExecutableModuleInfo(LPFILE, LPEXECUTABLE_METADATA);
BOOL GetExecutableInfo(LPFILE, LPEXECUTABLE_INFO);
BOOL LoadExecutable(LPEXECUTABLE_LOAD);
BOOL ResolveExecutableMappedSymbol(
    LPEXECUTABLE_METADATA Metadata,
    EXECUTABLE_VIRTUAL_ADDRESS_MAPPER Mapper,
    LPVOID MapperContext,
    LPCSTR Name,
    LINEAR* Address);

/***************************************************************************/

#endif
