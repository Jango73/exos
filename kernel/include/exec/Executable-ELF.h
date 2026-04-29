
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


    Executable ELF

\************************************************************************/

#ifndef EXECUTABLEELF_H_INCLUDED
#define EXECUTABLEELF_H_INCLUDED

/************************************************************************/

#include "../Base.h"
#include "exec/Executable.h"
#include "../fs/File.h"

/************************************************************************/
// ELF signature and basic constants

#define ELF_SIGNATURE 0x464C457F  // 0x7F 'E' 'L' 'F'

/* e_ident indices */
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define EI_ABIVERSION 8
#define EI_PAD 9
#define EI_NIDENT 16

/* EI_CLASS */
#define ELFCLASS32 1
#define ELFCLASS64 2
/* EI_DATA */
#define ELFDATA2LSB 1
/* e_version */
#define EV_CURRENT 1

/* e_type */
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3

/* e_machine */
#define EM_386 3
#define EM_X86_64 62

/* Program header types */
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_STACK 0x6474e551

/* Program header flags */
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

/************************************************************************/
// ELF-specific entry points

BOOL GetExecutableImageInfo_ELF(LPFILE File, LPEXECUTABLE_METADATA Metadata);
BOOL GetExecutableModuleInfo_ELF(LPFILE File, LPEXECUTABLE_METADATA Metadata);
BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLE_INFO Info);
BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLE_INFO Info, LINEAR CodeBase, LINEAR DataBase, LINEAR BssBase);

#endif
