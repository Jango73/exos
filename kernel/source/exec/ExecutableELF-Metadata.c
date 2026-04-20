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


    Executable ELF Metadata

\************************************************************************/

#include "ExecutableELF-Private.h"

#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/
// Dynamic tags

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_INIT_ARRAY 25
#define DT_FINI_ARRAY 26
#define DT_GNU_HASH 0x6ffffef5

/************************************************************************/

static U32 ELFGetArchitecture(U32 Machine) {
    if (Machine == EM_386) return EXECUTABLE_ARCHITECTURE_X86_32;
    if (Machine == EM_X86_64) return EXECUTABLE_ARCHITECTURE_X86_64;
    return EXECUTABLE_ARCHITECTURE_UNKNOWN;
}

/************************************************************************/

static BOOL ELFHeaderMatchesTarget(const ELF_FILE_HEADER* Header, U32 Target) {
    if (Header == NULL) return FALSE;

    if (Target == EXECUTABLE_TARGET_IMAGE) return Header->Type == ET_EXEC;
    if (Target == EXECUTABLE_TARGET_MODULE) return Header->Type == ET_DYN;
    return FALSE;
}

/************************************************************************/

static U32 ELFMakeSegmentAccess(U32 Flags) {
    U32 Access;

    Access = 0;

    if ((Flags & PF_R) != 0) Access |= EXECUTABLE_SEGMENT_ACCESS_READ;
    if ((Flags & PF_W) != 0) Access |= EXECUTABLE_SEGMENT_ACCESS_WRITE;
    if ((Flags & PF_X) != 0) Access |= EXECUTABLE_SEGMENT_ACCESS_EXECUTE;

    return Access;
}

/************************************************************************/

static U32 ELFMakeSegmentMapping(U32 Type, U32 Flags) {
    if (Type == PT_TLS) return EXECUTABLE_SEGMENT_MAPPING_TLS;
    if ((Flags & PF_X) != 0) return EXECUTABLE_SEGMENT_MAPPING_CODE;
    if ((Flags & PF_W) != 0 || (Flags & PF_R) != 0) return EXECUTABLE_SEGMENT_MAPPING_DATA;
    return EXECUTABLE_SEGMENT_MAPPING_NONE;
}

/************************************************************************/

static void ELFInitializeMetadata(U32 Target, LPEXECUTABLE_METADATA Metadata) {
    MemorySet(Metadata, 0, sizeof(EXECUTABLE_METADATA));
    Metadata->Format = EXECUTABLE_FORMAT_ELF;
    Metadata->Target = Target;
}

/************************************************************************/

static BOOL ELFMetadataAddSegment(LPEXECUTABLE_METADATA Metadata, const ELF_PROGRAM_HEADER* ProgramHeader) {
    EXECUTABLE_SEGMENT_DESCRIPTOR* Segment;

    if (Metadata == NULL || ProgramHeader == NULL) return FALSE;
    if (Metadata->SegmentCount >= EXECUTABLE_MAX_SEGMENTS) return FALSE;

    Segment = &Metadata->Segments[Metadata->SegmentCount++];
    Segment->SourceType = ProgramHeader->Type;
    Segment->Access = ELFMakeSegmentAccess(ProgramHeader->Flags);
    Segment->Mapping = ELFMakeSegmentMapping(ProgramHeader->Type, ProgramHeader->Flags);
    Segment->FileOffset = ProgramHeader->Offset;
    Segment->VirtualAddress = ProgramHeader->VirtualAddress;
    Segment->FileSize = ProgramHeader->FileSize;
    Segment->MemorySize = ProgramHeader->MemorySize;
    Segment->Alignment = ProgramHeader->Alignment;
    return TRUE;
}

/************************************************************************/

static BOOL ELFMetadataAddRelocationTable(
    LPEXECUTABLE_DYNAMIC_INFO Dynamic,
    U32 Type,
    UINT VirtualAddress,
    UINT Size,
    UINT EntrySize
) {
    EXECUTABLE_RELOCATION_TABLE_INFO* Table;

    if (Dynamic == NULL) return FALSE;
    if (Size == 0) return TRUE;
    if (Dynamic->RelocationTableCount >= EXECUTABLE_MAX_RELOCATION_TABLES) return FALSE;

    Table = &Dynamic->RelocationTables[Dynamic->RelocationTableCount++];
    Table->Type = Type;
    Table->VirtualAddress = VirtualAddress;
    Table->Size = Size;
    Table->EntrySize = EntrySize;
    return TRUE;
}

/************************************************************************/

static BOOL ELFMapVirtualRangeToFileOffset(
    LPFILE_OPERATION FileOperation,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    UINT VirtualAddress,
    UINT Size,
    UINT* FileOffset
) {
    U32 Index;

    if (Header == NULL || FileOffset == NULL) return FALSE;

    for (Index = 0; Index < (U32)Header->ProgramHeaderCount; ++Index) {
        ELF_PROGRAM_HEADER ProgramHeader;
        UINT SegmentEnd;
        UINT TargetEnd;

        if (!ELFReadProgramHeader(FileOperation, Header, Class, Index, &ProgramHeader)) return FALSE;
        if (ProgramHeader.Type != PT_LOAD) continue;
        if (!AddUIntOverflow(ProgramHeader.VirtualAddress, ProgramHeader.FileSize, &SegmentEnd)) return FALSE;
        if (!AddUIntOverflow(VirtualAddress, Size, &TargetEnd)) return FALSE;
        if (VirtualAddress < ProgramHeader.VirtualAddress) continue;
        if (TargetEnd > SegmentEnd) continue;

        *FileOffset = ProgramHeader.Offset + (VirtualAddress - ProgramHeader.VirtualAddress);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL ELFReadDynamicEntry(
    LPFILE_OPERATION FileOperation,
    UINT FileOffset,
    U8 Class,
    LPELF_DYNAMIC_ENTRY Entry
) {
    if (Entry == NULL) return FALSE;

    if (Class == ELFCLASS32) {
        EXOS_ELF32_DYN RawEntry;

        if (!ELFReadBytes(FileOperation, FileOffset, &RawEntry, sizeof(RawEntry))) return FALSE;
        Entry->Tag = (UINT)RawEntry.d_tag;
        Entry->Value = (UINT)RawEntry.d_val;
        return TRUE;
    }

#if defined(__EXOS_ARCH_X86_64__)
    if (Class == ELFCLASS64) {
        EXOS_ELF64_DYN RawEntry;

        if (!ELFReadBytes(FileOperation, FileOffset, &RawEntry, sizeof(RawEntry))) return FALSE;
        if (RawEntry.d_tag > 0xFFFFFFFF) return FALSE;
        if (RawEntry.d_val > 0xFFFFFFFF) return FALSE;
        Entry->Tag = (UINT)RawEntry.d_tag;
        Entry->Value = (UINT)RawEntry.d_val;
        return TRUE;
    }
#endif

    return FALSE;
}

/************************************************************************/

static BOOL ELFInspectDynamicTable(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    UINT DynamicVirtualAddress,
    UINT DynamicSize,
    LPEXECUTABLE_DYNAMIC_INFO Dynamic
) {
    UINT DynamicFileOffset;
    UINT DynamicEntrySize;
    UINT DynamicEntryCount;
    UINT Index;
    UINT RelAddress;
    UINT RelSize;
    UINT RelEntrySize;
    UINT RelaAddress;
    UINT RelaSize;
    UINT RelaEntrySize;
    UINT JmpRelAddress;
    UINT PltRelSize;
    UINT PltRelType;
    UINT SymbolCount;

    if (Dynamic == NULL) return FALSE;
    if (DynamicSize == 0) return TRUE;
    if (!ELFMapVirtualRangeToFileOffset(FileOperation, Header, Class, DynamicVirtualAddress, DynamicSize, &DynamicFileOffset)) return FALSE;

    if (Class == ELFCLASS32) {
        DynamicEntrySize = sizeof(EXOS_ELF32_DYN);
    } else if (Class == ELFCLASS64) {
        DynamicEntrySize = sizeof(EXOS_ELF64_DYN);
    } else {
        return FALSE;
    }

    if (DynamicSize % DynamicEntrySize != 0) return FALSE;

    DynamicEntryCount = DynamicSize / DynamicEntrySize;
    Dynamic->Present = TRUE;
    Dynamic->DynamicTableAddress = DynamicVirtualAddress;
    Dynamic->DynamicTableSize = DynamicSize;

    RelAddress = 0;
    RelSize = 0;
    RelEntrySize = 0;
    RelaAddress = 0;
    RelaSize = 0;
    RelaEntrySize = 0;
    JmpRelAddress = 0;
    PltRelSize = 0;
    PltRelType = 0;
    SymbolCount = 0;

    for (Index = 0; Index < DynamicEntryCount; ++Index) {
        ELF_DYNAMIC_ENTRY Entry;
        UINT EntryOffset;

        if (!AddUIntOverflow(DynamicFileOffset, Index * DynamicEntrySize, &EntryOffset)) return FALSE;
        if (EntryOffset >= FileSize) return FALSE;
        if (!ELFReadDynamicEntry(FileOperation, EntryOffset, Class, &Entry)) return FALSE;

        if (Entry.Tag == DT_NULL) break;

        switch (Entry.Tag) {
            case DT_NEEDED:
                Dynamic->NeededLibraryCount++;
                break;
            case DT_STRTAB:
                Dynamic->SymbolTable.Present = TRUE;
                Dynamic->SymbolTable.StringTableAddress = Entry.Value;
                break;
            case DT_STRSZ:
                Dynamic->SymbolTable.StringTableSize = Entry.Value;
                break;
            case DT_SYMTAB:
                Dynamic->SymbolTable.Present = TRUE;
                Dynamic->SymbolTable.SymbolTableAddress = Entry.Value;
                break;
            case DT_SYMENT:
                Dynamic->SymbolTable.SymbolEntrySize = Entry.Value;
                break;
            case DT_HASH:
                Dynamic->SymbolTable.HashTableAddress = Entry.Value;
                break;
            case DT_GNU_HASH:
                Dynamic->SymbolTable.GnuHashTableAddress = Entry.Value;
                break;
            case DT_REL:
                RelAddress = Entry.Value;
                break;
            case DT_RELSZ:
                RelSize = Entry.Value;
                break;
            case DT_RELENT:
                RelEntrySize = Entry.Value;
                break;
            case DT_RELA:
                RelaAddress = Entry.Value;
                break;
            case DT_RELASZ:
                RelaSize = Entry.Value;
                break;
            case DT_RELAENT:
                RelaEntrySize = Entry.Value;
                break;
            case DT_JMPREL:
                JmpRelAddress = Entry.Value;
                break;
            case DT_PLTRELSZ:
                PltRelSize = Entry.Value;
                break;
            case DT_PLTREL:
                PltRelType = Entry.Value;
                break;
            case DT_TEXTREL:
                Dynamic->RequiresTextRelocation = TRUE;
                break;
            case DT_INIT:
            case DT_FINI:
            case DT_INIT_ARRAY:
            case DT_FINI_ARRAY:
                Dynamic->HasConstructors = TRUE;
                break;
            default:
                break;
        }
    }

    if (Dynamic->SymbolTable.HashTableAddress != 0) {
        UINT HashTableFileOffset;
        U32 HashHeader[2];

        if (!ELFMapVirtualRangeToFileOffset(
                FileOperation,
                Header,
                Class,
                Dynamic->SymbolTable.HashTableAddress,
                sizeof(HashHeader),
                &HashTableFileOffset)) {
            return FALSE;
        }

        if (!ELFReadBytes(FileOperation, HashTableFileOffset, HashHeader, sizeof(HashHeader))) return FALSE;
        SymbolCount = (UINT)HashHeader[1];
    }

    if (SymbolCount != 0 && Dynamic->SymbolTable.SymbolEntrySize != 0) {
        Dynamic->SymbolTable.SymbolTableSize = SymbolCount * Dynamic->SymbolTable.SymbolEntrySize;
    }

    if (!ELFMetadataAddRelocationTable(Dynamic, EXECUTABLE_RELOCATION_TABLE_REL, RelAddress, RelSize, RelEntrySize)) return FALSE;
    if (!ELFMetadataAddRelocationTable(Dynamic, EXECUTABLE_RELOCATION_TABLE_RELA, RelaAddress, RelaSize, RelaEntrySize)) return FALSE;

    if (JmpRelAddress != 0 && PltRelSize != 0) {
        if (PltRelType == DT_REL) {
            if (!ELFMetadataAddRelocationTable(Dynamic, EXECUTABLE_RELOCATION_TABLE_PLT_REL, JmpRelAddress, PltRelSize, RelEntrySize)) {
                return FALSE;
            }
        } else if (PltRelType == DT_RELA) {
            if (!ELFMetadataAddRelocationTable(Dynamic, EXECUTABLE_RELOCATION_TABLE_PLT_RELA, JmpRelAddress, PltRelSize, RelaEntrySize)) {
                return FALSE;
            }
        } else {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static void ELFStoreMetadataLayout(
    const ELF_FILE_HEADER* Header,
    const ELF_LAYOUT_INFO* Layout,
    LPEXECUTABLE_METADATA Metadata
) {
    Metadata->EntryPoint = Header->EntryPoint;
    ELFStoreExecutableInfo(Header, Layout, &Metadata->Layout);
}

/************************************************************************/

static BOOL ELFInspectMetadata(
    LPFILE_OPERATION FileOperation,
    U32 FileSize,
    const ELF_FILE_HEADER* Header,
    U8 Class,
    U32 Target,
    LPEXECUTABLE_METADATA Metadata
) {
    ELF_LAYOUT_INFO Layout;
    U32 Index;
    UINT DynamicVirtualAddress;
    UINT DynamicSize;

    if (Metadata == NULL) return FALSE;
    if (!ELFHeaderMatchesTarget(Header, Target)) return FALSE;
    if (!ELFAnalyzeLayout(FileOperation, FileSize, Header, Class, &Layout)) return FALSE;

    ELFInitializeMetadata(Target, Metadata);
    Metadata->Architecture = ELFGetArchitecture(Header->Machine);
    if (Metadata->Architecture == EXECUTABLE_ARCHITECTURE_UNKNOWN) return FALSE;
    ELFStoreMetadataLayout(Header, &Layout, Metadata);

    DynamicVirtualAddress = 0;
    DynamicSize = 0;

    for (Index = 0; Index < (U32)Header->ProgramHeaderCount; ++Index) {
        ELF_PROGRAM_HEADER ProgramHeader;

        if (!ELFReadProgramHeader(FileOperation, Header, Class, Index, &ProgramHeader)) return FALSE;

        if (ProgramHeader.Type == PT_LOAD || ProgramHeader.Type == PT_TLS) {
            if (!ELFMetadataAddSegment(Metadata, &ProgramHeader)) return FALSE;
        }

        if (ProgramHeader.Type == PT_DYNAMIC) {
            if (Metadata->Dynamic.Present) return FALSE;
            DynamicVirtualAddress = ProgramHeader.VirtualAddress;
            DynamicSize = ProgramHeader.MemorySize;
            Metadata->Dynamic.Present = TRUE;
        }

        if (ProgramHeader.Type == PT_TLS) {
            if (Metadata->Tls.Present) return FALSE;
            Metadata->Tls.Present = TRUE;
            Metadata->Tls.TemplateAddress = ProgramHeader.VirtualAddress;
            Metadata->Tls.TemplateFileOffset = ProgramHeader.Offset;
            Metadata->Tls.TemplateSize = ProgramHeader.FileSize;
            Metadata->Tls.TotalSize = ProgramHeader.MemorySize;
            Metadata->Tls.Alignment = ProgramHeader.Alignment;
        }
    }

    if (Target == EXECUTABLE_TARGET_MODULE && !Metadata->Dynamic.Present) return FALSE;

    if (DynamicSize != 0) {
        Metadata->Dynamic.Present = FALSE;
        if (!ELFInspectDynamicTable(
                FileOperation,
                FileSize,
                Header,
                Class,
                DynamicVirtualAddress,
                DynamicSize,
                &Metadata->Dynamic)) {
            return FALSE;
        }
    }

    Metadata->Dynamic.RequiresInterpreter = Layout.HasInterp;
    return TRUE;
}

/************************************************************************/
// GetExecutableImageInfo_ELF
// Reads ELF metadata for one main executable image without process-specific logic.

BOOL GetExecutableImageInfo_ELF(LPFILE File, LPEXECUTABLE_METADATA Metadata) {
    FILE_OPERATION FileOperation;
    ELF_FILE_HEADER Header;
    U32 FileSize;
    U8 Ident[EI_NIDENT];
    U8 Class;

    if (File == NULL) return FALSE;
    if (Metadata == NULL) return FALSE;

    DEBUG(TEXT("[GetExecutableImageInfo_ELF] Enter"));

    ELFInitializeFileOperation(File, &FileOperation);

    FileSize = GetFileSize(File);
    if (!ELFReadIdent(&FileOperation, FileSize, Ident)) goto Out_Error;

    Class = Ident[EI_CLASS];
    if (!ELFReadHeader(&FileOperation, FileSize, Class, Ident, &Header)) goto Out_Error;
    if (!ELFValidateProgramHeaderTable(FileSize, &Header)) goto Out_Error;
    if (!ELFInspectMetadata(&FileOperation, FileSize, &Header, Class, EXECUTABLE_TARGET_IMAGE, Metadata)) goto Out_Error;

    DEBUG(TEXT("[GetExecutableImageInfo_ELF] Exit (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("[GetExecutableImageInfo_ELF] Exit (error)"));
    return FALSE;
}

/************************************************************************/
// GetExecutableModuleInfo_ELF
// Reads ELF metadata for one loadable executable module without process-specific logic.

BOOL GetExecutableModuleInfo_ELF(LPFILE File, LPEXECUTABLE_METADATA Metadata) {
    FILE_OPERATION FileOperation;
    ELF_FILE_HEADER Header;
    U32 FileSize;
    U8 Ident[EI_NIDENT];
    U8 Class;

    if (File == NULL) return FALSE;
    if (Metadata == NULL) return FALSE;

    DEBUG(TEXT("[GetExecutableModuleInfo_ELF] Enter"));

    ELFInitializeFileOperation(File, &FileOperation);

    FileSize = GetFileSize(File);
    if (!ELFReadIdent(&FileOperation, FileSize, Ident)) goto Out_Error;

    Class = Ident[EI_CLASS];
    if (!ELFReadHeader(&FileOperation, FileSize, Class, Ident, &Header)) goto Out_Error;
    if (!ELFValidateProgramHeaderTable(FileSize, &Header)) goto Out_Error;
    if (!ELFInspectMetadata(&FileOperation, FileSize, &Header, Class, EXECUTABLE_TARGET_MODULE, Metadata)) goto Out_Error;

    DEBUG(TEXT("[GetExecutableModuleInfo_ELF] Exit (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("[GetExecutableModuleInfo_ELF] Exit (error)"));
    return FALSE;
}

/************************************************************************/
// GetExecutableInfo_ELF
// Reads ELF header and program headers, classifies segments, computes layout.

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLE_INFO Info) {
    EXECUTABLE_METADATA Metadata;

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    DEBUG(TEXT("[GetExecutableInfo_ELF] Enter"));

    if (!GetExecutableImageInfo_ELF(File, &Metadata)) goto Out_Error;
    *Info = Metadata.Layout;

    DEBUG(TEXT("[GetExecutableInfo_ELF] Exit (success)"));
    return TRUE;

Out_Error:
    DEBUG(TEXT("[GetExecutableInfo_ELF] Exit (error)"));
    return FALSE;
}
