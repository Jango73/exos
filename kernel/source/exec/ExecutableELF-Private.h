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


    Executable ELF Private

\************************************************************************/

#ifndef EXECUTABLEELF_PRIVATE_H_INCLUDED
#define EXECUTABLEELF_PRIVATE_H_INCLUDED

/************************************************************************/

#include "exec/ExecutableELF.h"

/************************************************************************/

typedef struct tag_ELF_FILE_HEADER {
    U32 Type;
    U32 Machine;
    U32 Class;
    UINT EntryPoint;
    UINT ProgramHeaderOffset;
    UINT ProgramHeaderEntrySize;
    U16 ProgramHeaderCount;
} ELF_FILE_HEADER, *LPELF_FILE_HEADER;

typedef struct tag_ELF_PROGRAM_HEADER {
    U32 Type;
    U32 Flags;
    UINT Offset;
    UINT VirtualAddress;
    UINT FileSize;
    UINT MemorySize;
    UINT Alignment;
} ELF_PROGRAM_HEADER, *LPELF_PROGRAM_HEADER;

typedef struct tag_ELF_LAYOUT_INFO {
    UINT CodeMin;
    UINT CodeMax;
    UINT DataMin;
    UINT DataMax;
    UINT BssMin;
    UINT BssMax;
    BOOL HasLoadable;
    BOOL HasCode;
    BOOL HasInterp;
} ELF_LAYOUT_INFO, *LPELF_LAYOUT_INFO;

typedef struct __attribute__((packed)) tag_EXOS_ELF32_DYN {
    U32 d_tag;
    U32 d_val;
} EXOS_ELF32_DYN;

typedef struct __attribute__((packed)) tag_EXOS_ELF64_DYN {
    U64 d_tag;
    U64 d_val;
} EXOS_ELF64_DYN;

typedef struct tag_ELF_DYNAMIC_ENTRY {
    UINT Tag;
    UINT Value;
} ELF_DYNAMIC_ENTRY, *LPELF_DYNAMIC_ENTRY;

/************************************************************************/

BOOL AddUIntOverflow(UINT Left, UINT Right, UINT* Out);
void ELFInitializeFileOperation(LPFILE File, LPFILE_OPERATION FileOperation);
BOOL ELFReadBytes(LPFILE_OPERATION FileOperation, UINT Offset, LPVOID Buffer, UINT Size);
BOOL ELFReadIdent(LPFILE_OPERATION FileOperation, U32 FileSize, U8 Ident[EI_NIDENT]);
BOOL ELFReadHeader(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    U8 Class,
    const U8 Ident[EI_NIDENT],
    LPELF_FILE_HEADER Header
);
BOOL ELFValidateProgramHeaderTable(U32 FileSize, const ELF_FILE_HEADER* Header);
BOOL ELFReadProgramHeader(
    LPFILE_OPERATION FileOperation,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    U32 Index,
    LPELF_PROGRAM_HEADER ProgramHeader
);
BOOL ELFAnalyzeLayout(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    LPELF_LAYOUT_INFO Layout
);
void ELFStoreExecutableInfo(
    const ELF_FILE_HEADER* Header,
    const ELF_LAYOUT_INFO* Layout,
    LPEXECUTABLE_INFO Info
);

/************************************************************************/

#endif
