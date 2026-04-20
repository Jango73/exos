
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

#include "ExecutableELF-Private.h"

#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/

// Packed structures (GCC/Clang).
typedef struct __attribute__((packed)) tag_EXOS_ELF32_EHDR {
    U8 e_ident[EI_NIDENT];
    U16 e_type;
    U16 e_machine;
    U32 e_version;
    U32 e_entry;
    U32 e_phoff;
    U32 e_shoff;
    U32 e_flags;
    U16 e_ehsize;
    U16 e_phentsize;
    U16 e_phnum;
    U16 e_shentsize;
    U16 e_shnum;
    U16 e_shstrndx;
} EXOS_ELF32_EHDR;

typedef struct __attribute__((packed)) tag_EXOS_ELF32_PHDR {
    U32 p_type;
    U32 p_offset;
    U32 p_vaddr;
    U32 p_paddr;
    U32 p_filesz;
    U32 p_memsz;
    U32 p_flags;
    U32 p_align;
} EXOS_ELF32_PHDR;

typedef struct __attribute__((packed)) tag_EXOS_ELF64_EHDR {
    U8 e_ident[EI_NIDENT];
    U16 e_type;
    U16 e_machine;
    U32 e_version;
    U64 e_entry;
    U64 e_phoff;
    U64 e_shoff;
    U32 e_flags;
    U16 e_ehsize;
    U16 e_phentsize;
    U16 e_phnum;
    U16 e_shentsize;
    U16 e_shnum;
    U16 e_shstrndx;
} EXOS_ELF64_EHDR;

typedef struct __attribute__((packed)) tag_EXOS_ELF64_PHDR {
    U32 p_type;
    U32 p_flags;
    U64 p_offset;
    U64 p_vaddr;
    U64 p_paddr;
    U64 p_filesz;
    U64 p_memsz;
    U64 p_align;
} EXOS_ELF64_PHDR;

// Local helpers

static U32 ELFMakeSig(const U8 Ident[EI_NIDENT]) {
    return ((U32)Ident[0]) | ((U32)Ident[1] << 8) | ((U32)Ident[2] << 16) | ((U32)Ident[3] << 24);
}

/************************************************************************/

static BOOL ELFIsCode(U32 Flags) { return (Flags & PF_X) != 0; }

/************************************************************************/

static BOOL ELFIsData(U32 Flags) { return (Flags & PF_W) != 0 || ((Flags & PF_X) == 0); }

/************************************************************************/

/**
 * @brief Safely add two register-sized values.
 * @param Left First value.
 * @param Right Second value.
 * @param Out Receives the sum on success.
 * @return FALSE on overflow.
 */
BOOL AddUIntOverflow(UINT Left, UINT Right, UINT* Out) {
    UINT Result = Left + Right;

    if (Result < Left) return FALSE;
    if (Out != NULL) *Out = Result;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize one file operation descriptor for ELF reads.
 * @param File Source file.
 * @param FileOperation Receives initialized operation state.
 */
void ELFInitializeFileOperation(LPFILE File, LPFILE_OPERATION FileOperation) {
    FileOperation->Header.Size = sizeof(FILE_OPERATION);
    FileOperation->Header.Version = EXOS_ABI_VERSION;
    FileOperation->Header.Flags = 0;
    FileOperation->File = (HANDLE)File;
    FileOperation->Buffer = NULL;
    FileOperation->NumBytes = 0;
}

/************************************************************************/

/**
 * @brief Read bytes from one explicit file position.
 * @param FileOperation File operation state.
 * @param Offset File offset.
 * @param Buffer Output buffer.
 * @param Size Number of bytes to read.
 * @return TRUE on success.
 */
BOOL ELFReadBytes(LPFILE_OPERATION FileOperation, UINT Offset, LPVOID Buffer, UINT Size) {
    if (Offset > 0xFFFFFFFF) return FALSE;
    if (Size > 0xFFFFFFFF) return FALSE;

    FileOperation->NumBytes = (U32)Offset;
    if (SetFilePosition(FileOperation) != DF_RETURN_SUCCESS) return FALSE;

    FileOperation->Buffer = Buffer;
    FileOperation->NumBytes = (U32)Size;
    return ReadFile(FileOperation) == Size;
}

/************************************************************************/

/**
 * @brief Read and validate the ELF identification bytes.
 * @param FileOperation File operation state.
 * @param FileSize File size in bytes.
 * @param Ident Receives EI_NIDENT bytes.
 * @return TRUE when the file starts with a supported ELF signature.
 */
BOOL ELFReadIdent(LPFILE_OPERATION FileOperation, U32 FileSize, U8 Ident[EI_NIDENT]) {
    if (FileSize < EI_NIDENT) return FALSE;
    if (!ELFReadBytes(FileOperation, 0, Ident, EI_NIDENT)) return FALSE;
    return ELFMakeSig(Ident) == ELF_SIGNATURE;
}

/************************************************************************/

/**
 * @brief Read one ELF header into a normalized representation.
 * @param FileOperation File operation state.
 * @param FileSize File size in bytes.
 * @param Class ELF class from EI_CLASS.
 * @param Ident Already-read ELF identification.
 * @param Header Receives normalized header values.
 * @return TRUE on success.
 */
BOOL ELFReadHeader(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    U8 Class,
    const U8 Ident[EI_NIDENT],
    LPELF_FILE_HEADER Header
) {
    if (Header == NULL) return FALSE;

    if (Class == ELFCLASS32) {
        EXOS_ELF32_EHDR RawHeader;

        if (FileSize < sizeof(RawHeader)) return FALSE;

        MemoryCopy(RawHeader.e_ident, Ident, EI_NIDENT);
        if (!ELFReadBytes(
                FileOperation,
                EI_NIDENT,
                ((U8*)&RawHeader) + EI_NIDENT,
                sizeof(RawHeader) - EI_NIDENT)) {
            return FALSE;
        }

        if (RawHeader.e_ident[EI_DATA] != ELFDATA2LSB) return FALSE;
        if (RawHeader.e_version != EV_CURRENT) return FALSE;
        if (RawHeader.e_machine != EM_386) return FALSE;
        if (RawHeader.e_phnum == 0) return FALSE;
        if (RawHeader.e_phentsize < sizeof(EXOS_ELF32_PHDR)) return FALSE;

        Header->Type = RawHeader.e_type;
        Header->Machine = RawHeader.e_machine;
        Header->Class = Class;
        Header->EntryPoint = (UINT)RawHeader.e_entry;
        Header->ProgramHeaderOffset = (UINT)RawHeader.e_phoff;
        Header->ProgramHeaderEntrySize = (UINT)RawHeader.e_phentsize;
        Header->ProgramHeaderCount = RawHeader.e_phnum;
        return TRUE;
    }

#if defined(__EXOS_ARCH_X86_64__)
    if (Class == ELFCLASS64) {
        EXOS_ELF64_EHDR RawHeader;

        if (FileSize < sizeof(RawHeader)) return FALSE;

        MemoryCopy(RawHeader.e_ident, Ident, EI_NIDENT);
        if (!ELFReadBytes(
                FileOperation,
                EI_NIDENT,
                ((U8*)&RawHeader) + EI_NIDENT,
                sizeof(RawHeader) - EI_NIDENT)) {
            return FALSE;
        }

        if (RawHeader.e_ident[EI_DATA] != ELFDATA2LSB) return FALSE;
        if (RawHeader.e_version != EV_CURRENT) return FALSE;
        if (RawHeader.e_machine != EM_X86_64) return FALSE;
        if (RawHeader.e_phnum == 0) return FALSE;
        if (RawHeader.e_phentsize < sizeof(EXOS_ELF64_PHDR)) return FALSE;
        if (RawHeader.e_phoff > 0xFFFFFFFF) return FALSE;
        if (RawHeader.e_entry > 0xFFFFFFFF) return FALSE;

        Header->Type = RawHeader.e_type;
        Header->Machine = RawHeader.e_machine;
        Header->Class = Class;
        Header->EntryPoint = (UINT)RawHeader.e_entry;
        Header->ProgramHeaderOffset = (UINT)RawHeader.e_phoff;
        Header->ProgramHeaderEntrySize = (UINT)RawHeader.e_phentsize;
        Header->ProgramHeaderCount = RawHeader.e_phnum;
        return TRUE;
    }
#else
    UNUSED(Ident);
#endif

    return FALSE;
}

/************************************************************************/

/**
 * @brief Validate that the program header table lies inside the file.
 * @param FileSize File size in bytes.
 * @param Header Normalized ELF header.
 * @return TRUE when the program header table is fully readable.
 */
BOOL ELFValidateProgramHeaderTable(U32 FileSize, const ELF_FILE_HEADER* Header) {
    UINT ProgramHeaderTableSize;
    UINT ProgramHeaderTableEnd;

    if (Header == NULL) return FALSE;

    ProgramHeaderTableSize = (UINT)Header->ProgramHeaderCount * Header->ProgramHeaderEntrySize;
    if (!AddUIntOverflow(Header->ProgramHeaderOffset, ProgramHeaderTableSize, &ProgramHeaderTableEnd)) return FALSE;
    if (ProgramHeaderTableEnd > FileSize) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Read one ELF program header into a normalized representation.
 * @param FileOperation File operation state.
 * @param Header Normalized file header.
 * @param Class ELF class from EI_CLASS.
 * @param Index Program header index.
 * @param ProgramHeader Receives normalized program header values.
 * @return TRUE on success.
 */
BOOL ELFReadProgramHeader(
    LPFILE_OPERATION FileOperation,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    U32 Index,
    LPELF_PROGRAM_HEADER ProgramHeader
) {
    UINT ProgramHeaderOffset;

    if (Header == NULL || ProgramHeader == NULL) return FALSE;
    if (!AddUIntOverflow(
            Header->ProgramHeaderOffset,
            (UINT)Index * Header->ProgramHeaderEntrySize,
            &ProgramHeaderOffset)) {
        return FALSE;
    }

    if (Class == ELFCLASS32) {
        EXOS_ELF32_PHDR RawHeader;

        if (!ELFReadBytes(FileOperation, ProgramHeaderOffset, &RawHeader, sizeof(RawHeader))) return FALSE;

        ProgramHeader->Type = RawHeader.p_type;
        ProgramHeader->Flags = RawHeader.p_flags;
        ProgramHeader->Offset = (UINT)RawHeader.p_offset;
        ProgramHeader->VirtualAddress = (UINT)RawHeader.p_vaddr;
        ProgramHeader->FileSize = (UINT)RawHeader.p_filesz;
        ProgramHeader->MemorySize = (UINT)RawHeader.p_memsz;
        ProgramHeader->Alignment = (UINT)RawHeader.p_align;
        return TRUE;
    }

#if defined(__EXOS_ARCH_X86_64__)
    if (Class == ELFCLASS64) {
        EXOS_ELF64_PHDR RawHeader;

        if (!ELFReadBytes(FileOperation, ProgramHeaderOffset, &RawHeader, sizeof(RawHeader))) return FALSE;
        if (RawHeader.p_offset > 0xFFFFFFFF) return FALSE;
        if (RawHeader.p_vaddr > 0xFFFFFFFF) return FALSE;
        if (RawHeader.p_filesz > 0xFFFFFFFF) return FALSE;
        if (RawHeader.p_memsz > 0xFFFFFFFF) return FALSE;

        ProgramHeader->Type = RawHeader.p_type;
        ProgramHeader->Flags = RawHeader.p_flags;
        ProgramHeader->Offset = (UINT)RawHeader.p_offset;
        ProgramHeader->VirtualAddress = (UINT)RawHeader.p_vaddr;
        ProgramHeader->FileSize = (UINT)RawHeader.p_filesz;
        ProgramHeader->MemorySize = (UINT)RawHeader.p_memsz;
        ProgramHeader->Alignment = (UINT)RawHeader.p_align;
        return TRUE;
    }
#endif

    return FALSE;
}

/************************************************************************/

/**
 * @brief Reset one layout accumulator before scanning PT_LOAD entries.
 * @param Layout Receives the reset state.
 */
static void ELFResetLayout(LPELF_LAYOUT_INFO Layout) {
    Layout->CodeMin = MAX_UINT;
    Layout->CodeMax = 0;
    Layout->DataMin = MAX_UINT;
    Layout->DataMax = 0;
    Layout->BssMin = MAX_UINT;
    Layout->BssMax = 0;
    Layout->HasLoadable = FALSE;
    Layout->HasCode = FALSE;
    Layout->HasInterp = FALSE;
}

/************************************************************************/

/**
 * @brief Scan normalized program headers and compute executable layout.
 * @param FileOperation File operation state.
 * @param FileSize File size in bytes.
 * @param Header Normalized file header.
 * @param Class ELF class from EI_CLASS.
 * @param Layout Receives computed ranges and flags.
 * @return TRUE on success.
 */
BOOL ELFAnalyzeLayout(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    LPELF_LAYOUT_INFO Layout
) {
    U32 Index;

    if (Layout == NULL) return FALSE;

    ELFResetLayout(Layout);

    for (Index = 0; Index < (U32)Header->ProgramHeaderCount; ++Index) {
        ELF_PROGRAM_HEADER ProgramHeader;
        UINT VirtualEnd;
        BOOL IsCode;
        BOOL IsData;

        if (!ELFReadProgramHeader(FileOperation, Header, Class, Index, &ProgramHeader)) return FALSE;

        if (ProgramHeader.Type == PT_INTERP) {
            Layout->HasInterp = TRUE;
        }
        if (ProgramHeader.Type != PT_LOAD) continue;

        Layout->HasLoadable = TRUE;

        if (!AddUIntOverflow(ProgramHeader.VirtualAddress, ProgramHeader.MemorySize, &VirtualEnd)) return FALSE;

        {
            UINT FileEnd;

            if (!AddUIntOverflow(ProgramHeader.Offset, ProgramHeader.FileSize, &FileEnd)) return FALSE;
            if (FileEnd > FileSize) return FALSE;
        }

        IsCode = ELFIsCode(ProgramHeader.Flags);
        IsData = ELFIsData(ProgramHeader.Flags);

        if (IsCode) {
            Layout->HasCode = TRUE;
            if (ProgramHeader.VirtualAddress < Layout->CodeMin) Layout->CodeMin = ProgramHeader.VirtualAddress;
            if (VirtualEnd > Layout->CodeMax) Layout->CodeMax = VirtualEnd;
        } else if (IsData) {
            if (ProgramHeader.VirtualAddress < Layout->DataMin) Layout->DataMin = ProgramHeader.VirtualAddress;
            if (VirtualEnd > Layout->DataMax) Layout->DataMax = VirtualEnd;
        } else {
            if (ProgramHeader.VirtualAddress < Layout->DataMin) Layout->DataMin = ProgramHeader.VirtualAddress;
            if (VirtualEnd > Layout->DataMax) Layout->DataMax = VirtualEnd;
        }

        if (ProgramHeader.MemorySize > ProgramHeader.FileSize) {
            UINT BssStart;
            UINT BssEnd;

            if (!AddUIntOverflow(ProgramHeader.VirtualAddress, ProgramHeader.FileSize, &BssStart)) return FALSE;
            if (!AddUIntOverflow(ProgramHeader.VirtualAddress, ProgramHeader.MemorySize, &BssEnd)) return FALSE;

            if (BssStart < Layout->BssMin) Layout->BssMin = BssStart;
            if (BssEnd > Layout->BssMax) Layout->BssMax = BssEnd;
        }
    }

    if (!Layout->HasLoadable) return FALSE;
    if (!Layout->HasCode) return FALSE;
    if (Layout->HasInterp) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Copy computed layout ranges into one executable info structure.
 * @param Header Normalized file header.
 * @param Layout Computed layout ranges.
 * @param Info Receives executable information.
 */
void ELFStoreExecutableInfo(
    const ELF_FILE_HEADER* Header,
    const ELF_LAYOUT_INFO* Layout,
    LPEXECUTABLE_INFO Info
) {
    Info->EntryPoint = Header->EntryPoint;

    if (Layout->CodeMin != MAX_UINT && Layout->CodeMax > Layout->CodeMin) {
        Info->CodeBase = Layout->CodeMin;
        Info->CodeSize = Layout->CodeMax - Layout->CodeMin;
    } else {
        Info->CodeBase = 0;
        Info->CodeSize = 0;
    }

    if (Layout->DataMin != MAX_UINT && Layout->DataMax > Layout->DataMin) {
        Info->DataBase = Layout->DataMin;
        Info->DataSize = Layout->DataMax - Layout->DataMin;
    } else {
        Info->DataBase = 0;
        Info->DataSize = 0;
    }

    if (Layout->BssMin != MAX_UINT && Layout->BssMax > Layout->BssMin) {
        Info->BssBase = Layout->BssMin;
        Info->BssSize = Layout->BssMax - Layout->BssMin;
    } else {
        Info->BssBase = 0;
        Info->BssSize = 0;
    }

    Info->StackMinimum = 0;
    Info->StackRequested = 0;
    Info->HeapMinimum = 0;
    Info->HeapRequested = 0;
}

/************************************************************************/

/**
 * @brief Load PT_LOAD segments into the provided destination ranges.
 * @param FileOperation File operation state.
 * @param FileSize File size in bytes.
 * @param Header Normalized file header.
 * @param Layout Computed layout ranges.
 * @param Class ELF class from EI_CLASS.
 * @param Info Executable info containing source reference ranges.
 * @param CodeBase Destination code base.
 * @param DataBase Destination data base.
 * @return TRUE on success.
 */
static BOOL ELFLoadSegments(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    const ELF_FILE_HEADER* Header,
    const ELF_LAYOUT_INFO* Layout,
    U8 Class,
    LPEXECUTABLE_INFO Info,
    LINEAR CodeBase,
    LINEAR DataBase
) {
    U32 Index;
    UINT CodeRef;
    UINT DataRef;

    CodeRef = Info->CodeBase;
    DataRef = Info->DataBase;

    for (Index = 0; Index < (U32)Header->ProgramHeaderCount; ++Index) {
        ELF_PROGRAM_HEADER ProgramHeader;
        LINEAR Base;
        UINT ReferenceBase;
        LINEAR Destination;
        UINT FileEnd;
        UINT ZeroSize;

        if (!ELFReadProgramHeader(FileOperation, Header, Class, Index, &ProgramHeader)) return FALSE;
        if (ProgramHeader.Type != PT_LOAD) continue;

        if (ELFIsCode(ProgramHeader.Flags)) {
            Base = CodeBase;
            ReferenceBase = CodeRef;
        } else {
            Base = DataBase;
            ReferenceBase = DataRef;
        }

        if (ProgramHeader.VirtualAddress < ReferenceBase) return FALSE;
        Destination = Base + (ProgramHeader.VirtualAddress - ReferenceBase);

        if (!AddUIntOverflow(ProgramHeader.Offset, ProgramHeader.FileSize, &FileEnd)) return FALSE;
        if (FileEnd > FileSize) return FALSE;

        if (ProgramHeader.FileSize > 0) {
            if (!ELFReadBytes(FileOperation, ProgramHeader.Offset, (LPVOID)Destination, ProgramHeader.FileSize)) return FALSE;
        }

        ZeroSize = (ProgramHeader.MemorySize > ProgramHeader.FileSize)
            ? (ProgramHeader.MemorySize - ProgramHeader.FileSize)
            : 0;
        if (ZeroSize > 0) {
            MemorySet((LPVOID)(Destination + ProgramHeader.FileSize), 0, ZeroSize);
        }
    }

    if (Header->EntryPoint >= Layout->CodeMin && Header->EntryPoint < Layout->CodeMax) {
        Info->EntryPoint = (UINT)CodeBase + (Header->EntryPoint - CodeRef);
        return TRUE;
    }

    if (Header->EntryPoint >= Layout->DataMin && Header->EntryPoint < Layout->DataMax) {
        Info->EntryPoint = (UINT)DataBase + (Header->EntryPoint - DataRef);
        return TRUE;
    }

    return FALSE;
}

// LoadExecutable_ELF
// Loads PT_LOAD segments into the provided base addresses, zero-fills BSS,
// and fixes up the effective entry point.
// COMMENTS & LOGS IN ENGLISH (per coding guideline)

BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLE_INFO Info, LINEAR CodeBase, LINEAR DataBase, LINEAR BssBase) {
    FILE_OPERATION FileOperation;
    ELF_FILE_HEADER Header;
    ELF_LAYOUT_INFO Layout;
    U32 FileSize;
    U8 Ident[EI_NIDENT];
    U8 Class;

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    UNUSED(BssBase);

    DEBUG(TEXT("[LoadExecutable_ELF] %s"), File->Name);

    ELFInitializeFileOperation(File, &FileOperation);

    FileSize = GetFileSize(File);
    if (!ELFReadIdent(&FileOperation, FileSize, Ident)) goto Out_Error;

    Class = Ident[EI_CLASS];
    if (!ELFReadHeader(&FileOperation, FileSize, Class, Ident, &Header)) goto Out_Error;
    if (!ELFValidateProgramHeaderTable(FileSize, &Header)) goto Out_Error;
    if (Header.Type != ET_EXEC) goto Out_Error;
    if (!ELFAnalyzeLayout(&FileOperation, FileSize, &Header, Class, &Layout)) goto Out_Error;
    if (!ELFLoadSegments(&FileOperation, FileSize, &Header, &Layout, Class, Info, CodeBase, DataBase)) goto Out_Error;

    DEBUG(TEXT("[LoadExecutable_ELF] Exit (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("[LoadExecutable_ELF] Exit (error)"));
    return FALSE;
}
