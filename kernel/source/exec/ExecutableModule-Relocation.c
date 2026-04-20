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


    Executable module relocation binding

\************************************************************************/

#include "exec/ExecutableModule.h"

#include "exec/ExecutableELF.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"

/***************************************************************************/

#define ELF_ST_BIND(Info) ((Info) >> 4)
#define ELF_ST_TYPE(Info) ((Info) & 0x0F)
#define ELF_STN_UNDEF 0

#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8
#define R_386_TLS_TPOFF 14

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_TPOFF64 18

/***************************************************************************/

typedef struct PACKED tag_EXECUTABLE_ELF32_SYMBOL {
    U32 Name;
    U32 Value;
    U32 Size;
    U8 Info;
    U8 Other;
    U16 SectionIndex;
} EXECUTABLE_ELF32_SYMBOL, *LPEXECUTABLE_ELF32_SYMBOL;

typedef struct PACKED tag_EXECUTABLE_ELF32_REL {
    U32 Offset;
    U32 Info;
} EXECUTABLE_ELF32_REL, *LPEXECUTABLE_ELF32_REL;

typedef struct PACKED tag_EXECUTABLE_ELF32_RELA {
    U32 Offset;
    U32 Info;
    I32 Addend;
} EXECUTABLE_ELF32_RELA, *LPEXECUTABLE_ELF32_RELA;

#if defined(__EXOS_ARCH_X86_64__)
typedef struct PACKED tag_EXECUTABLE_ELF64_SYMBOL {
    U32 Name;
    U8 Info;
    U8 Other;
    U16 SectionIndex;
    U64 Value;
    U64 Size;
} EXECUTABLE_ELF64_SYMBOL, *LPEXECUTABLE_ELF64_SYMBOL;

typedef struct PACKED tag_EXECUTABLE_ELF64_REL {
    U64 Offset;
    U64 Info;
} EXECUTABLE_ELF64_REL, *LPEXECUTABLE_ELF64_REL;

typedef struct PACKED tag_EXECUTABLE_ELF64_RELA {
    U64 Offset;
    U64 Info;
    I64 Addend;
} EXECUTABLE_ELF64_RELA, *LPEXECUTABLE_ELF64_RELA;
#endif

/***************************************************************************/

/**
 * @brief Return TRUE when one symbol can be exported to other images.
 */
static BOOL ExecutableSymbolCanExport(U8 Info, U16 SectionIndex) {
    U8 Bind;
    U8 Type;

    if (SectionIndex == ELF_STN_UNDEF) {
        return FALSE;
    }

    Bind = ELF_ST_BIND(Info);
    if (Bind != EXECUTABLE_SYMBOL_BIND_GLOBAL && Bind != EXECUTABLE_SYMBOL_BIND_WEAK) {
        return FALSE;
    }

    Type = ELF_ST_TYPE(Info);
    return Type == EXECUTABLE_SYMBOL_TYPE_NONE || Type == EXECUTABLE_SYMBOL_TYPE_OBJECT ||
           Type == EXECUTABLE_SYMBOL_TYPE_FUNCTION;
}

/***************************************************************************/

/**
 * @brief Resolve one exported symbol in one 32-bit mapped executable image.
 */
static BOOL ResolveExecutableMappedSymbol32(
    LPEXECUTABLE_METADATA Metadata,
    EXECUTABLE_VIRTUAL_ADDRESS_MAPPER Mapper,
    LPVOID MapperContext,
    LPCSTR Name,
    LINEAR* Address) {
    LPEXECUTABLE_SYMBOL_TABLE_INFO SymbolTable = &(Metadata->Dynamic.SymbolTable);
    LINEAR SymbolTableAddress;
    LINEAR StringTableAddress;
    UINT SymbolCount;

    SymbolTableAddress = Mapper(MapperContext, SymbolTable->SymbolTableAddress);
    StringTableAddress = Mapper(MapperContext, SymbolTable->StringTableAddress);
    if (SymbolTableAddress == 0 || StringTableAddress == 0 || SymbolTable->SymbolEntrySize == 0) {
        return FALSE;
    }

    SymbolCount = SymbolTable->SymbolTableSize / SymbolTable->SymbolEntrySize;
    for (UINT SymbolIndex = 0; SymbolIndex < SymbolCount; SymbolIndex++) {
        LPEXECUTABLE_ELF32_SYMBOL Symbol = (LPEXECUTABLE_ELF32_SYMBOL)(SymbolTableAddress + SymbolIndex * SymbolTable->SymbolEntrySize);
        LPCSTR SymbolName = (LPCSTR)(StringTableAddress + Symbol->Name);

        if (!ExecutableSymbolCanExport(Symbol->Info, Symbol->SectionIndex)) {
            continue;
        }

        if (StringCompare(SymbolName, Name) != 0) {
            continue;
        }

        *Address = Mapper(MapperContext, Symbol->Value);
        return *Address != 0;
    }

    return FALSE;
}

/***************************************************************************/

#if defined(__EXOS_ARCH_X86_64__)
/**
 * @brief Resolve one exported symbol in one 64-bit mapped executable image.
 */
static BOOL ResolveExecutableMappedSymbol64(
    LPEXECUTABLE_METADATA Metadata,
    EXECUTABLE_VIRTUAL_ADDRESS_MAPPER Mapper,
    LPVOID MapperContext,
    LPCSTR Name,
    LINEAR* Address) {
    LPEXECUTABLE_SYMBOL_TABLE_INFO SymbolTable = &(Metadata->Dynamic.SymbolTable);
    LINEAR SymbolTableAddress;
    LINEAR StringTableAddress;
    UINT SymbolCount;

    SymbolTableAddress = Mapper(MapperContext, SymbolTable->SymbolTableAddress);
    StringTableAddress = Mapper(MapperContext, SymbolTable->StringTableAddress);
    if (SymbolTableAddress == 0 || StringTableAddress == 0 || SymbolTable->SymbolEntrySize == 0) {
        return FALSE;
    }

    SymbolCount = SymbolTable->SymbolTableSize / SymbolTable->SymbolEntrySize;
    for (UINT SymbolIndex = 0; SymbolIndex < SymbolCount; SymbolIndex++) {
        LPEXECUTABLE_ELF64_SYMBOL Symbol = (LPEXECUTABLE_ELF64_SYMBOL)(SymbolTableAddress + SymbolIndex * SymbolTable->SymbolEntrySize);
        LPCSTR SymbolName = (LPCSTR)(StringTableAddress + Symbol->Name);

        if (!ExecutableSymbolCanExport(Symbol->Info, Symbol->SectionIndex)) {
            continue;
        }

        if (StringCompare(SymbolName, Name) != 0) {
            continue;
        }

        *Address = Mapper(MapperContext, (UINT)Symbol->Value);
        return *Address != 0;
    }

    return FALSE;
}
#endif

/***************************************************************************/

/**
 * @brief Resolve one exported symbol in one mapped executable image.
 */
BOOL ResolveExecutableMappedSymbol(
    LPEXECUTABLE_METADATA Metadata,
    EXECUTABLE_VIRTUAL_ADDRESS_MAPPER Mapper,
    LPVOID MapperContext,
    LPCSTR Name,
    LINEAR* Address) {
    if (Metadata == NULL || Mapper == NULL || Name == NULL || Address == NULL) {
        return FALSE;
    }

    *Address = 0;
    if (Metadata->Dynamic.SymbolTable.Present == FALSE) {
        return FALSE;
    }

    if (Metadata->Architecture == EXECUTABLE_ARCHITECTURE_X86_32) {
        return ResolveExecutableMappedSymbol32(Metadata, Mapper, MapperContext, Name, Address);
    }

#if defined(__EXOS_ARCH_X86_64__)
    if (Metadata->Architecture == EXECUTABLE_ARCHITECTURE_X86_64) {
        return ResolveExecutableMappedSymbol64(Metadata, Mapper, MapperContext, Name, Address);
    }
#endif

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Translate one module virtual address into its installed process address.
 *
 * @param Image Module image metadata.
 * @param SegmentBases Installed process segment bases.
 * @param SegmentSizes Installed process segment sizes.
 * @param VirtualAddress Module virtual address.
 * @param RequireWritable TRUE when the resolved address must target writable memory.
 * @return Installed address, or zero when the address is not mapped.
 */
static LINEAR MapExecutableModuleVirtualAddress(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    UINT VirtualAddress,
    BOOL RequireWritable) {
    if (Image == NULL || SegmentBases == NULL || SegmentSizes == NULL) {
        return 0;
    }

    for (UINT SegmentIndex = 0; SegmentIndex < Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Image->Metadata.Segments[SegmentIndex]);
        UINT SegmentEnd;
        UINT Offset;

        if (Segment->SourceType != PT_LOAD || SegmentBases[SegmentIndex] == 0) {
            continue;
        }

        if (RequireWritable != FALSE && (Segment->Access & EXECUTABLE_SEGMENT_ACCESS_WRITE) == 0) {
            continue;
        }

        if (Segment->MemorySize == 0) {
            continue;
        }

        SegmentEnd = Segment->VirtualAddress + Segment->MemorySize;
        if (VirtualAddress < Segment->VirtualAddress || VirtualAddress >= SegmentEnd) {
            continue;
        }

        Offset = VirtualAddress - Segment->VirtualAddress;
        if (Offset >= SegmentSizes[SegmentIndex]) {
            return 0;
        }

        return SegmentBases[SegmentIndex] + Offset;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Resolve one ELF symbol into an installed process address.
 */
static BOOL ResolveExecutableModuleSymbol(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    EXECUTABLE_SYMBOL_RESOLVER Resolver,
    LPVOID ResolverContext,
    UINT SymbolIndex,
    LPCSTR SymbolName,
    U8 Info,
    U16 SectionIndex,
    UINT Value,
    LINEAR* Address) {
    EXECUTABLE_SYMBOL_RESOLUTION Resolution;
    U8 Bind;

    if (Address == NULL) {
        return FALSE;
    }

    *Address = 0;
    if (SymbolIndex == 0) {
        return TRUE;
    }

    if (SectionIndex != ELF_STN_UNDEF) {
        *Address = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Value, FALSE);
        return *Address != 0;
    }

    Bind = ELF_ST_BIND(Info);
    if (Bind != EXECUTABLE_SYMBOL_BIND_GLOBAL && Bind != EXECUTABLE_SYMBOL_BIND_WEAK) {
        return TRUE;
    }

    if (Resolver == NULL || SymbolName == NULL || SymbolName[0] == STR_NULL) {
        return Bind == EXECUTABLE_SYMBOL_BIND_WEAK;
    }

    MemorySet(&Resolution, 0, sizeof(Resolution));
    Resolution.Name = SymbolName;
    Resolution.SourceSymbolIndex = SymbolIndex;
    Resolution.Required = (Bind != EXECUTABLE_SYMBOL_BIND_WEAK);
    if (!Resolver(ResolverContext, &Resolution)) {
        return Bind == EXECUTABLE_SYMBOL_BIND_WEAK;
    }

    *Address = Resolution.Address;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply one 32-bit relocation value.
 */
static BOOL ApplyExecutableModuleRelocation32(U32 Type, LINEAR Target, LINEAR SymbolAddress, UINT Addend) {
    U32 Value;

    if (Target == 0) {
        return FALSE;
    }

    switch (Type) {
        case R_386_NONE:
            return TRUE;
        case R_386_32:
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
            Value = (U32)(SymbolAddress + Addend);
            break;
        case R_386_PC32:
            Value = (U32)(SymbolAddress + Addend - Target);
            break;
        case R_386_RELATIVE:
            Value = (U32)SymbolAddress;
            break;
        default:
            WARNING(TEXT("[ApplyExecutableModuleRelocation32] Unsupported relocation type=%u"), Type);
            return FALSE;
    }

    *((U32*)Target) = Value;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply one 32-bit TLS thread-pointer offset relocation.
 */
static BOOL ApplyExecutableModuleTlsRelocation32(U32 Type, LINEAR Target, UINT Addend, UINT TlsSize) {
    if (Target == 0 || TlsSize == 0) {
        return FALSE;
    }

    switch (Type) {
        case R_386_TLS_TPOFF:
            *((U32*)Target) = (U32)(Addend - TlsSize);
            return TRUE;
        default:
            return FALSE;
    }
}

/***************************************************************************/

/**
 * @brief Relocate one 32-bit ELF relocation table.
 */
static BOOL RelocateExecutableModuleTable32(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    LPEXECUTABLE_RELOCATION_TABLE_INFO Table,
    EXECUTABLE_SYMBOL_RESOLVER Resolver,
    LPVOID ResolverContext) {
    LPEXECUTABLE_SYMBOL_TABLE_INFO SymbolTable = &(Image->Metadata.Dynamic.SymbolTable);
    LINEAR TableAddress;
    LINEAR SymbolTableAddress;
    LINEAR StringTableAddress;
    UINT RelocationCount;

    TableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Table->VirtualAddress, FALSE);
    SymbolTableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, SymbolTable->SymbolTableAddress, FALSE);
    StringTableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, SymbolTable->StringTableAddress, FALSE);
    if (TableAddress == 0 || SymbolTableAddress == 0 || StringTableAddress == 0 || Table->EntrySize == 0) {
        return FALSE;
    }

    RelocationCount = Table->Size / Table->EntrySize;
    for (UINT RelocationIndex = 0; RelocationIndex < RelocationCount; RelocationIndex++) {
        U32 Offset;
        U32 Info;
        U32 Type;
        UINT SymbolIndex;
        UINT Addend;
        LINEAR Target;
        LINEAR SymbolAddress;
        LPEXECUTABLE_ELF32_SYMBOL Symbol;
        LPCSTR SymbolName;

        if (Table->Type == EXECUTABLE_RELOCATION_TABLE_REL || Table->Type == EXECUTABLE_RELOCATION_TABLE_PLT_REL) {
            LPEXECUTABLE_ELF32_REL Rel = (LPEXECUTABLE_ELF32_REL)(TableAddress + RelocationIndex * Table->EntrySize);

            Offset = Rel->Offset;
            Info = Rel->Info;
            Target = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Offset, TRUE);
            if (Target == 0) return FALSE;
            Addend = *((U32*)Target);
        } else {
            LPEXECUTABLE_ELF32_RELA Rela = (LPEXECUTABLE_ELF32_RELA)(TableAddress + RelocationIndex * Table->EntrySize);

            Offset = Rela->Offset;
            Info = Rela->Info;
            Addend = (UINT)Rela->Addend;
            Target = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Offset, TRUE);
            if (Target == 0) return FALSE;
        }

        SymbolIndex = Info >> 8;
        Type = Info & 0xFF;
        if (Type == R_386_TLS_TPOFF) {
            if (!ApplyExecutableModuleTlsRelocation32(Type, Target, Addend, Image->Metadata.Tls.TotalSize)) return FALSE;
            continue;
        }

        Symbol = (LPEXECUTABLE_ELF32_SYMBOL)(SymbolTableAddress + SymbolIndex * SymbolTable->SymbolEntrySize);
        SymbolName = (LPCSTR)(StringTableAddress + Symbol->Name);

        if (Type == R_386_RELATIVE) {
            SymbolAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Addend, FALSE);
            if (Addend != 0 && SymbolAddress == 0) return FALSE;
        } else if (!ResolveExecutableModuleSymbol(
                       Image,
                       SegmentBases,
                       SegmentSizes,
                       Resolver,
                       ResolverContext,
                       SymbolIndex,
                       SymbolName,
                       Symbol->Info,
                       Symbol->SectionIndex,
                       Symbol->Value,
                       &SymbolAddress)) {
            WARNING(TEXT("[RelocateExecutableModuleTable32] Unresolved symbol name=%s"), SymbolName);
            return FALSE;
        }

        if (!ApplyExecutableModuleRelocation32(Type, Target, SymbolAddress, Addend)) return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

#if defined(__EXOS_ARCH_X86_64__)
/**
 * @brief Apply one 64-bit relocation value.
 */
static BOOL ApplyExecutableModuleRelocation64(U32 Type, LINEAR Target, LINEAR SymbolAddress, UINT Addend) {
    U64 Value;

    if (Target == 0) {
        return FALSE;
    }

    switch (Type) {
        case R_X86_64_NONE:
            return TRUE;
        case R_X86_64_64:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
            Value = (U64)(SymbolAddress + Addend);
            *((U64*)Target) = Value;
            return TRUE;
        case R_X86_64_PC32:
            Value = (U64)(SymbolAddress + Addend - Target);
            *((U32*)Target) = (U32)Value;
            return TRUE;
        case R_X86_64_RELATIVE:
            *((U64*)Target) = (U64)SymbolAddress;
            return TRUE;
        case R_X86_64_32:
        case R_X86_64_32S:
            Value = (U64)(SymbolAddress + Addend);
            *((U32*)Target) = (U32)Value;
            return TRUE;
        default:
            WARNING(TEXT("[ApplyExecutableModuleRelocation64] Unsupported relocation type=%u"), Type);
            return FALSE;
    }
}

/***************************************************************************/

/**
 * @brief Apply one 64-bit TLS thread-pointer offset relocation.
 */
static BOOL ApplyExecutableModuleTlsRelocation64(U32 Type, LINEAR Target, UINT Addend, UINT TlsSize) {
    if (Target == 0 || TlsSize == 0) {
        return FALSE;
    }

    switch (Type) {
        case R_X86_64_TPOFF64:
            *((U64*)Target) = (U64)(Addend - TlsSize);
            return TRUE;
        default:
            return FALSE;
    }
}

/***************************************************************************/

/**
 * @brief Relocate one 64-bit ELF relocation table.
 */
static BOOL RelocateExecutableModuleTable64(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    LPEXECUTABLE_RELOCATION_TABLE_INFO Table,
    EXECUTABLE_SYMBOL_RESOLVER Resolver,
    LPVOID ResolverContext) {
    LPEXECUTABLE_SYMBOL_TABLE_INFO SymbolTable = &(Image->Metadata.Dynamic.SymbolTable);
    LINEAR TableAddress;
    LINEAR SymbolTableAddress;
    LINEAR StringTableAddress;
    UINT RelocationCount;

    TableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Table->VirtualAddress, FALSE);
    SymbolTableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, SymbolTable->SymbolTableAddress, FALSE);
    StringTableAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, SymbolTable->StringTableAddress, FALSE);
    if (TableAddress == 0 || SymbolTableAddress == 0 || StringTableAddress == 0 || Table->EntrySize == 0) {
        return FALSE;
    }

    RelocationCount = Table->Size / Table->EntrySize;
    for (UINT RelocationIndex = 0; RelocationIndex < RelocationCount; RelocationIndex++) {
        U64 Offset;
        U64 Info;
        U32 Type;
        UINT SymbolIndex;
        UINT Addend;
        LINEAR Target;
        LINEAR SymbolAddress;
        LPEXECUTABLE_ELF64_SYMBOL Symbol;
        LPCSTR SymbolName;

        if (Table->Type == EXECUTABLE_RELOCATION_TABLE_REL || Table->Type == EXECUTABLE_RELOCATION_TABLE_PLT_REL) {
            LPEXECUTABLE_ELF64_REL Rel = (LPEXECUTABLE_ELF64_REL)(TableAddress + RelocationIndex * Table->EntrySize);

            Offset = Rel->Offset;
            Info = Rel->Info;
            Target = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, (UINT)Offset, TRUE);
            if (Target == 0) return FALSE;
            Addend = *((UINT*)Target);
        } else {
            LPEXECUTABLE_ELF64_RELA Rela = (LPEXECUTABLE_ELF64_RELA)(TableAddress + RelocationIndex * Table->EntrySize);

            Offset = Rela->Offset;
            Info = Rela->Info;
            Addend = (UINT)Rela->Addend;
            Target = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, (UINT)Offset, TRUE);
            if (Target == 0) return FALSE;
        }

        SymbolIndex = (UINT)(Info >> 32);
        Type = (U32)(Info & 0xFFFFFFFF);
        if (Type == R_X86_64_TPOFF64) {
            if (!ApplyExecutableModuleTlsRelocation64(Type, Target, Addend, Image->Metadata.Tls.TotalSize)) return FALSE;
            continue;
        }

        Symbol = (LPEXECUTABLE_ELF64_SYMBOL)(SymbolTableAddress + SymbolIndex * SymbolTable->SymbolEntrySize);
        SymbolName = (LPCSTR)(StringTableAddress + Symbol->Name);

        if (Type == R_X86_64_RELATIVE) {
            SymbolAddress = MapExecutableModuleVirtualAddress(Image, SegmentBases, SegmentSizes, Addend, FALSE);
            if (Addend != 0 && SymbolAddress == 0) return FALSE;
        } else if (!ResolveExecutableModuleSymbol(
                       Image,
                       SegmentBases,
                       SegmentSizes,
                       Resolver,
                       ResolverContext,
                       SymbolIndex,
                       SymbolName,
                       Symbol->Info,
                       Symbol->SectionIndex,
                       (UINT)Symbol->Value,
                       &SymbolAddress)) {
            WARNING(TEXT("[RelocateExecutableModuleTable64] Unresolved symbol name=%s"), SymbolName);
            return FALSE;
        }

        if (!ApplyExecutableModuleRelocation64(Type, Target, SymbolAddress, Addend)) return FALSE;
    }

    return TRUE;
}
#endif

/***************************************************************************/

/**
 * @brief Apply relocations for one installed process module binding.
 *
 * @param Image Shared executable module image.
 * @param SegmentBases Installed process segment bases.
 * @param SegmentSizes Installed process segment sizes.
 * @param Resolver Symbol resolver callback.
 * @param ResolverContext Resolver private context.
 * @return TRUE when all supported relocations have been applied.
 */
BOOL RelocateExecutableModuleBinding(
    LPEXECUTABLE_MODULE_IMAGE Image,
    LINEAR SegmentBases[EXECUTABLE_MAX_SEGMENTS],
    UINT SegmentSizes[EXECUTABLE_MAX_SEGMENTS],
    EXECUTABLE_SYMBOL_RESOLVER Resolver,
    LPVOID ResolverContext) {
    if (Image == NULL || SegmentBases == NULL || SegmentSizes == NULL) {
        return FALSE;
    }

    if (Image->Metadata.Dynamic.RelocationTableCount == 0) {
        return TRUE;
    }

    if (Image->Metadata.Dynamic.SymbolTable.Present == FALSE) {
        WARNING(TEXT("[RelocateExecutableModuleBinding] Missing dynamic symbol table"));
        return FALSE;
    }

    for (UINT TableIndex = 0; TableIndex < Image->Metadata.Dynamic.RelocationTableCount; TableIndex++) {
        LPEXECUTABLE_RELOCATION_TABLE_INFO Table = &(Image->Metadata.Dynamic.RelocationTables[TableIndex]);

        if (Image->Metadata.Architecture == EXECUTABLE_ARCHITECTURE_X86_32) {
            if (!RelocateExecutableModuleTable32(Image, SegmentBases, SegmentSizes, Table, Resolver, ResolverContext)) {
                return FALSE;
            }
            continue;
        }

#if defined(__EXOS_ARCH_X86_64__)
        if (Image->Metadata.Architecture == EXECUTABLE_ARCHITECTURE_X86_64) {
            if (!RelocateExecutableModuleTable64(Image, SegmentBases, SegmentSizes, Table, Resolver, ResolverContext)) {
                return FALSE;
            }
            continue;
        }
#endif

        WARNING(TEXT("[RelocateExecutableModuleBinding] Unsupported module architecture=%u"), Image->Metadata.Architecture);
        return FALSE;
    }

    return TRUE;
}
