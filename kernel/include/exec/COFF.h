
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


    COFF

\************************************************************************/
#ifndef COFF_H_INCLUDED
#define COFF_H_INCLUDED

/***************************************************************************/

#include "../Base.h"

/***************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

typedef struct tag_COFFHEADER {
    U16 Magic;               // Target machine
    U16 NumSections;         // Number of sections
    U32 TimeStamp;           // Time and date stamp of the file
    U32 SymbolTable;         // File pointer to the symbol table
    U32 NumSymbols;          // Number of entries in the symbol table
    U16 OptionalHeaderSize;  // Size in bytes of the optional header
    U16 Flags;               // Flags
} COFF_HEADER, *LPCOFFHEADER;

#define COFF_HEADER_FLAG_RELOCSTRIPPED 0x0001
#define COFF_HEADER_FLAG_EXECUTABLE 0x0002
#define COFF_HEADER_FLAG_LINENOSTRIPPED 0x0004
#define COFF_HEADER_FLAG_LOCALSYMSTRIPPED 0x0010
#define COFF_HEADER_FLAG_16WR 0x0200
#define COFF_HEADER_FLAG_32WR 0x0400

#define COFF_MACHINE_INTEL_386 0x014C

/***************************************************************************/

typedef struct tag_COFFSECTION {
    U8 Name[8];           // Section name, null padded
    U32 PhysicalAddress;  // Physical address
    U32 VirtualAddress;   // Virtual address
    U32 Size;             // Size in bytes
    U32 Data;             // File pointer to raw data
    U32 Relocations;      // File pointer to relocation entries
    U32 LineNumbers;      // File pointer to line number entries
    U16 NumRelocations;   // Number of relocation entries
    U16 NumLineNumbers;   // Number of line number entries
    U32 Flags;            // Flags
} COFF_SECTION, *LPCOFFSECTION;

#define COFF_SECTION_FLAG_REGULAR 0x0000
#define COFF_SECTION_FLAG_DUMMY 0x0001
#define COFF_SECTION_FLAG_NOLOAD 0x0002
#define COFF_SECTION_FLAG_GROUP 0x0004
#define COFF_SECTION_FLAG_PAD 0x0008
#define COFF_SECTION_FLAG_COPY 0x0010
#define COFF_SECTION_FLAG_TEXT 0x0020
#define COFF_SECTION_FLAG_DATA 0x0040
#define COFF_SECTION_FLAG_BSS 0x0080
#define COFF_SECTION_FLAG_INFO 0x0200
#define COFF_SECTION_FLAG_OVERLAY 0x0400
#define COFF_SECTION_FLAG_LIB 0x0800

/***************************************************************************/

typedef struct tag_COFFRELOCATION {
    U32 Address;
    U32 SymbolIndex;
    U16 Index;
} COFF_RELOCATION, *LPCOFFRELOCATION;

#define COFF_RELOCATION_ABSOLUTE 0x0000
#define COFF_RELOCATION_DIRECT_16 0x0001
#define COFF_RELOCATION_RELATIVE_16 0x0002
#define COFF_RELOCATION_DIRECT_32 0x0006
#define COFF_RELOCATION_SEGMENT_12 0x000B
#define COFF_RELOCATION_RELATIVE_32 0x0018

/***************************************************************************/

typedef struct tag_COFFSYMBOL {
    union tag_COFFSYMBOL_NAME  // Name or index to a symbol
    {
        U8 NameAscii[8];
        struct tag_COFFSYMBOL_NAMEINDEX {
            U32 Zero;
            U32 Offset;
        } NameIndex;
    } Name;

    U32 Value;    // Symbol value, storage class dependent
    U16 Section;  // Section number of symbol
    U16 Type;     // Basic and derived type specification
    U8 Storage;   // Storage class
    U8 NumAux;    // Number of auxiliary entries
} COFF_SYMBOL, *LPCOFFSYMBOL;

/***************************************************************************/

#define COFF_STORAGE_EFCN 255     // physical end of a function
#define COFF_STORAGE_NULL 0       // -
#define COFF_STORAGE_AUTO 1       // automatic variable
#define COFF_STORAGE_EXT 2        // external symbol
#define COFF_STORAGE_STAT 3       // static
#define COFF_STORAGE_REG 4        // register variable
#define COFF_STORAGE_EXTDEF 5     // external definition
#define COFF_STORAGE_LABEL 6      // label
#define COFF_STORAGE_ULABEL 7     // undefined label
#define COFF_STORAGE_MOS 8        // member of structure
#define COFF_STORAGE_ARG 9        // function argument
#define COFF_STORAGE_STRTAG 10    // structure tag
#define COFF_STORAGE_MOU 11       // member of union
#define COFF_STORAGE_UNTAG 12     // union tag
#define COFF_STORAGE_TPDEF 13     // type definition
#define COFF_STORAGE_USTATIC 14   // uninitialized static
#define COFF_STORAGE_ENTAG 15     // enumeration tag
#define COFF_STORAGE_MOE 16       // member of enumeration
#define COFF_STORAGE_REGPARM 17   // register parameter
#define COFF_STORAGE_FIELD 18     // bit field
#define COFF_STORAGE_BLOCK 100    // beginning and end of block
#define COFF_STORAGE_FCN 101      // beginning and end of function
#define COFF_STORAGE_EOS 102      // end of structure
#define COFF_STORAGE_FILE 103     // file name
#define COFF_STORAGE_LINE 104     // used only by utility programs
#define COFF_STORAGE_ALIAS 105    // duplicated tag
#define COFF_STORAGE_HIDDEN 106   // like static, used to avoid name conflicts
#define COFF_STORAGE_SHADOW 107   // shadow symbol
#define COFF_STORAGE_WEAKEXT 108  // like C_EXT, but with weak linkage

/***************************************************************************/

#ifdef __cplusplus
}
#endif

/***************************************************************************/

#pragma pack(pop)

#endif
