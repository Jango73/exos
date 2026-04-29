
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


    Executable EXOS

\************************************************************************/

#ifndef EXECUTABLEEXOS_H_INCLUDED
#define EXECUTABLEEXOS_H_INCLUDED

/************************************************************************/

#include "../Base.h"
#include "exec/Executable.h"
#include "../fs/File.h"

/************************************************************************/

#define EXOS_SIGNATURE (*((const U32*)"EXOS"))

#define EXOS_CHUNK_NONE (*((const U32*)"xxxx"))
#define EXOS_CHUNK_INIT (*((const U32*)"INIT"))
#define EXOS_CHUNK_FIXUP (*((const U32*)"FXUP"))
#define EXOS_CHUNK_CODE (*((const U32*)"CODE"))
#define EXOS_CHUNK_DATA (*((const U32*)"DATA"))
#define EXOS_CHUNK_STACK (*((const U32*)"STAK"))
#define EXOS_CHUNK_EXPORT (*((const U32*)"EXPT"))
#define EXOS_CHUNK_IMPORT (*((const U32*)"IMPT"))
#define EXOS_CHUNK_TIMESTAMP (*((const U32*)"TIME"))
#define EXOS_CHUNK_SECURITY (*((const U32*)"SECU"))
#define EXOS_CHUNK_COMMENT (*((const U32*)"NOTE"))
#define EXOS_CHUNK_RESOURCE (*((const U32*)"RSRC"))
#define EXOS_CHUNK_VERSION (*((const U32*)"VERS"))
#define EXOS_CHUNK_MENU (*((const U32*)"MENU"))
#define EXOS_CHUNK_DIALOG (*((const U32*)"DLOG"))
#define EXOS_CHUNK_ICON (*((const U32*)"ICON"))
#define EXOS_CHUNK_BITMAP (*((const U32*)"BTMP"))
#define EXOS_CHUNK_WAVE (*((const U32*)"WAVE"))
#define EXOS_CHUNK_DEBUG (*((const U32*)"DBUG"))
#define EXOS_CHUNK_USER (*((const U32*)"USER"))

/************************************************************************/

#define EXOS_TYPE_NONE 0x00000000
#define EXOS_TYPE_EXECUTABLE 0x00000001
#define EXOS_TYPE_LIBRARY 0x00000002

#define EXOS_BYTEORDER_LITTLE_ENDIAN 0x00000000
#define EXOS_BYTEORDER_BIG_ENDIAN 0xFFFFFFFF

#define EXOS_FIXUP_SOURCE_CODE 0x00000001
#define EXOS_FIXUP_SOURCE_DATA 0x00000002
#define EXOS_FIXUP_SOURCE_STACK 0x00000004

#define EXOS_FIXUP_DEST_CODE 0x00000010
#define EXOS_FIXUP_DEST_DATA 0x00000020
#define EXOS_FIXUP_DEST_STACK 0x00000040

/************************************************************************/

typedef struct tag_EXOSHEADER {
    U32 Signature;
    U32 Type;
    U32 VersionMajor;
    U32 VersionMinor;
    U32 ByteOrder;
    U32 Machine;
    U32 Reserved1;
    U32 Reserved2;
    U32 Reserved3;
    U32 Reserved4;
} EXOSHEADER, *LPEXOSHEADER;

/***************************************************************************/

typedef struct tag_EXOSCHUNK {
    U32 ID;
    UINT Size;
} EXOSCHUNK, *LPEXOSCHUNK;

/***************************************************************************/

typedef struct tag_EXOSCHUNK_INIT {
    U32 EntryPoint;
    U32 CodeBase;
    UINT CodeSize;
    U32 DataBase;
    UINT DataSize;
    UINT StackMinimum;
    UINT StackRequested;
    UINT HeapMinimum;
    UINT HeapRequested;
    U32 Reserved1;
    U32 Reserved2;
    U32 Reserved3;
    U32 Reserved4;
    U32 Reserved5;
} EXOSCHUNK_INIT, *LPEXOSCHUNK_INIT;

/***************************************************************************/

typedef struct tag_EXOSCHUNK_FIXUP {
    U32 Section;
    U32 Address;
} EXOSCHUNK_FIXUP, *LPEXOSCHUNK_FIXUP;

/***************************************************************************/

BOOL GetExecutableInfo_EXOS(LPFILE, LPEXECUTABLE_INFO);
BOOL LoadExecutable_EXOS(LPFILE, LPEXECUTABLE_INFO, LINEAR, LINEAR);

/***************************************************************************/

#endif
