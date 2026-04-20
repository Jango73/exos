
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


    x86-64 logging helpers implementation

\************************************************************************/

#include "arch/x86-64/x86-64-Log.h"

#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process.h"
#include "process/Task.h"
#include "text/Text.h"
#include "arch/x86-64/x86-64-Memory.h"

/***************************************************************************/

/**
 * @brief Log the contents of the long mode global descriptor table.
 * @param Table Pointer to the descriptor array.
 * @param EntryCount Number of 8-byte slots to inspect.
 */
void LogGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table, U32 EntryCount) {
    if (Table == NULL || EntryCount == 0) {
        DEBUG(TEXT("Table is empty"));
        return;
    }

    const U64* RawEntries = (const U64*)Table;
    U32 Index = 0;

    while (Index < EntryCount) {
        U64 RawLow = RawEntries[Index];
        U64 RawHigh = 0;

        if (RawLow == 0) {
            DEBUG(TEXT("Entry %u: raw[63:0]=%p raw[127:64]=%p (null)"),
                Index,
                (LPVOID)RawLow,
                (LPVOID)RawHigh);
            Index++;
            continue;
        }

        const SEGMENT_DESCRIPTOR* Descriptor = &Table[Index];

        BOOL IsSystemDescriptor = FALSE;
        const X86_64_SYSTEM_SEGMENT_DESCRIPTOR* SystemDescriptor = NULL;

        if (Descriptor->S == 0 && (Index + 1u) < EntryCount) {
            SystemDescriptor = (const X86_64_SYSTEM_SEGMENT_DESCRIPTOR*)Descriptor;
            RawHigh = RawEntries[Index + 1u];
            IsSystemDescriptor = TRUE;
        }
        UNUSED(RawHigh);

        DEBUG(TEXT("Entry %u: raw[63:0]=%p raw[127:64]=%p"), Index, (LPVOID)RawLow, (LPVOID)RawHigh);

        if (IsSystemDescriptor) {
            U32 Limit = ((U32)SystemDescriptor->Limit_00_15)
                | ((U32)SystemDescriptor->Limit_16_19 << 16);
            U64 Base = (U64)SystemDescriptor->Base_00_15
                | ((U64)SystemDescriptor->Base_16_23 << 16)
                | ((U64)SystemDescriptor->Base_24_31 << 24)
                | ((U64)SystemDescriptor->Base_32_63 << 32);
            UNUSED(Limit);
            UNUSED(Base);

            DEBUG(TEXT("Limit_00_15=%x Limit_16_19=%x"),
                (U32)SystemDescriptor->Limit_00_15,
                (U32)SystemDescriptor->Limit_16_19);
            DEBUG(TEXT("Base_00_15=%x Base_16_23=%x Base_24_31=%x Base_32_63=%x"),
                (U32)SystemDescriptor->Base_00_15,
                (U32)SystemDescriptor->Base_16_23,
                (U32)SystemDescriptor->Base_24_31,
                (U32)SystemDescriptor->Base_32_63);
            DEBUG(TEXT("Accessed=%u CanWrite=%u ConformExpand=%u Code=%u"),
                (U32)SystemDescriptor->Accessed,
                (U32)SystemDescriptor->CanWrite,
                (U32)SystemDescriptor->ConformExpand,
                (U32)SystemDescriptor->Code);
            DEBUG(TEXT("S=%u Privilege=%u Present=%u"),
                (U32)SystemDescriptor->S,
                (U32)SystemDescriptor->Privilege,
                (U32)SystemDescriptor->Present);
            DEBUG(TEXT("Available=%u LongMode=%u DefaultSize=%u Granularity=%u Reserved=%x"),
                (U32)SystemDescriptor->Available,
                (U32)SystemDescriptor->LongMode,
                (U32)SystemDescriptor->DefaultSize,
                (U32)SystemDescriptor->Granularity,
                (U32)SystemDescriptor->Reserved);
            DEBUG(TEXT("Base=%p Limit=%x"), (LPVOID)Base, Limit);

            Index += 2u;
            continue;
        }

        U32 Limit = ((U32)Descriptor->Limit_00_15) | ((U32)Descriptor->Limit_16_19 << 16);
        U32 Base = (U32)Descriptor->Base_00_15
            | ((U32)Descriptor->Base_16_23 << 16)
            | ((U32)Descriptor->Base_24_31 << 24);
        UNUSED(Limit);
        UNUSED(Base);

        DEBUG(TEXT("Limit_00_15=%x Limit_16_19=%x"),
            (U32)Descriptor->Limit_00_15,
            (U32)Descriptor->Limit_16_19);
        DEBUG(TEXT("Base_00_15=%x Base_16_23=%x Base_24_31=%x"),
            (U32)Descriptor->Base_00_15,
            (U32)Descriptor->Base_16_23,
            (U32)Descriptor->Base_24_31);
        DEBUG(TEXT("Accessed=%u CanWrite=%u ConformExpand=%u Code=%u"),
            (U32)Descriptor->Accessed,
            (U32)Descriptor->CanWrite,
            (U32)Descriptor->ConformExpand,
            (U32)Descriptor->Code);
        DEBUG(TEXT("S=%u Privilege=%u Present=%u"),
            (U32)Descriptor->S,
            (U32)Descriptor->Privilege,
            (U32)Descriptor->Present);
        DEBUG(TEXT("AVL=%u LongMode=%u DefaultSize=%u Granularity=%u"),
            (U32)Descriptor->Available,
            (U32)Descriptor->LongMode,
            (U32)Descriptor->DefaultSize,
            (U32)Descriptor->Granularity);

        Index++;
    }
}

/***************************************************************************/

void LogRegisters64(const LPINTEL_64_REGISTERS Regs) {
    if (Regs == NULL) {
        KernelLogText(LOG_VERBOSE, TEXT("No register snapshot available"));
        return;
    }

    KernelLogText(LOG_VERBOSE, TEXT("CS : %x DS : %x SS : %x"), (U32)Regs->CS, (U32)Regs->DS, (U32)Regs->SS);
    KernelLogText(LOG_VERBOSE, TEXT("ES : %x FS : %x GS : %x"), (U32)Regs->ES, (U32)Regs->FS, (U32)Regs->GS);
    KernelLogText(LOG_VERBOSE, TEXT("RAX : %p RBX : %p RCX : %p RDX : %p"),
        (LPVOID)Regs->RAX,
        (LPVOID)Regs->RBX,
        (LPVOID)Regs->RCX,
        (LPVOID)Regs->RDX);
    KernelLogText(LOG_VERBOSE, TEXT("RSI : %p RDI : %p RBP : %p RSP : %p"),
        (LPVOID)Regs->RSI,
        (LPVOID)Regs->RDI,
        (LPVOID)Regs->RBP,
        (LPVOID)Regs->RSP);
    KernelLogText(LOG_VERBOSE, TEXT("R8 : %p R9 : %p R10 : %p R11 : %p"),
        (LPVOID)Regs->R8,
        (LPVOID)Regs->R9,
        (LPVOID)Regs->R10,
        (LPVOID)Regs->R11);
    KernelLogText(LOG_VERBOSE, TEXT("R12 : %p R13 : %p R14 : %p R15 : %p"),
        (LPVOID)Regs->R12,
        (LPVOID)Regs->R13,
        (LPVOID)Regs->R14,
        (LPVOID)Regs->R15);
    KernelLogText(LOG_VERBOSE, TEXT("RIP : %p RFLAGS : %p"), (LPVOID)Regs->RIP, (LPVOID)Regs->RFlags);
    KernelLogText(LOG_VERBOSE, TEXT("CR0 : %p CR2 : %p CR3 : %p CR4 : %p"),
        (LPVOID)Regs->CR0,
        (LPVOID)Regs->CR2,
        (LPVOID)Regs->CR3,
        (LPVOID)Regs->CR4);
    KernelLogText(LOG_VERBOSE, TEXT("CR8 : %p"), (LPVOID)Regs->CR8);
    KernelLogText(LOG_VERBOSE, TEXT("DR0 : %p DR1 : %p DR2 : %p"),
        (LPVOID)Regs->DR0,
        (LPVOID)Regs->DR1,
        (LPVOID)Regs->DR2);
    KernelLogText(LOG_VERBOSE, TEXT("DR3 : %p DR6 : %p DR7 : %p"),
        (LPVOID)Regs->DR3,
        (LPVOID)Regs->DR6,
        (LPVOID)Regs->DR7);
}

/***************************************************************************/

void LogTaskStateSegment(U32 LogType, const X86_64_TASK_STATE_SEGMENT* Tss) {
    if (Tss == NULL) {
        KernelLogText(LogType, TEXT("Null TSS pointer"));
        return;
    }

    KernelLogText(
        LogType,
        TEXT("TSS @ %p (sizeof=%u):\n"
             "  Reserved0   = %x\n"
             "  RSP0/1/2    = %p / %p / %p\n"
             "  Reserved1   = %x\n"
             "  IST1-4      = %p / %p / %p / %p\n"
             "  IST5-7      = %p / %p / %p\n"
             "  Reserved2   = %x\n"
             "  Reserved3   = %x\n"
             "  IOMapBase   = %x (linear @ %p)"),
        (const void*)Tss,
        (U32)sizeof(X86_64_TASK_STATE_SEGMENT),
        (U32)Tss->Reserved0,
        (LPVOID)Tss->RSP0,
        (LPVOID)Tss->RSP1,
        (LPVOID)Tss->RSP2,
        (LPVOID)Tss->Reserved1,
        (LPVOID)Tss->IST1,
        (LPVOID)Tss->IST2,
        (LPVOID)Tss->IST3,
        (LPVOID)Tss->IST4,
        (LPVOID)Tss->IST5,
        (LPVOID)Tss->IST6,
        (LPVOID)Tss->IST7,
        (LPVOID)Tss->Reserved2,
        (U32)Tss->Reserved3,
        (U32)Tss->IOMapBase,
        (const void*)((const U8*)Tss + (UINT)Tss->IOMapBase));
}

/***************************************************************************/

static U64 BuildLinearAddress(UINT Pml4Index, UINT PdptIndex, UINT DirectoryIndex, UINT TableIndex, U64 Offset) {
    U64 Address = ((U64)Pml4Index << 39)
        | ((U64)PdptIndex << 30)
        | ((U64)DirectoryIndex << 21)
        | ((U64)TableIndex << 12)
        | (Offset & PAGE_SIZE_MASK);
    return CanonicalizeLinearAddress(Address);
}

/***************************************************************************/

static U64 BuildRangeEnd(U64 Base, U64 Span) {
    return CanonicalizeLinearAddress(Base + (Span - 1));
}

/***************************************************************************/

/**
 * @brief Logs the complete hierarchical paging structures for x86-64.
 * @param Pml4Physical Physical address of the PML4 to inspect.
 */
void LogPageDirectory64(PHYSICAL Pml4Physical) {
    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == 0) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed for PML4 %p"), (LPVOID)Pml4Physical);
        return;
    }

    LPPML4 Pml4 = (LPPML4)Pml4Linear;

    DEBUG(TEXT("PML4 PA=%p contents:"), (LPVOID)Pml4Physical);

    for (UINT Pml4Index = 0; Pml4Index < PML4_ENTRY_COUNT; Pml4Index++) {
        const X86_64_PML4_ENTRY *Pml4Entry = &Pml4[Pml4Index];

        if (!Pml4Entry->Present) continue;

        U64 LinearBase = BuildLinearAddress(Pml4Index, 0, 0, 0, 0);
        U64 LinearEnd = BuildRangeEnd(LinearBase, (U64)1 << 39);
        PHYSICAL PdptPhysical = (PHYSICAL)(Pml4Entry->Address << 12);
        UNUSED(LinearEnd);

        DEBUG(TEXT("PML4E[%u]: VA=%p-%p -> PDPT_PA=%p Present=%u RW=%u Priv=%u NX=%u"),
            Pml4Index,
            (LPVOID)LinearBase,
            (LPVOID)LinearEnd,
            (LPVOID)PdptPhysical,
            (U32)Pml4Entry->Present,
            (U32)Pml4Entry->ReadWrite,
            (U32)Pml4Entry->Privilege,
            (U32)Pml4Entry->NoExecute);

        LINEAR PdptLinear = MapTemporaryPhysicalPage2(PdptPhysical);

        if (PdptLinear == 0) {
            ERROR(TEXT("MapTemporaryPhysicalPage2 failed for PDPT %p"),
                (LPVOID)PdptPhysical);
            continue;
        }

        LPPDPT Pdpt = (LPPDPT)PdptLinear;

        for (UINT PdptIndex = 0; PdptIndex < PDPT_ENTRY_COUNT; PdptIndex++) {
            const X86_64_PDPT_ENTRY *PdptEntry = &Pdpt[PdptIndex];

            if (!PdptEntry->Present) continue;

            U64 PdptBase = BuildLinearAddress(Pml4Index, PdptIndex, 0, 0, 0);
            U64 PdptEnd = BuildRangeEnd(PdptBase, (U64)1 << 30);
            UNUSED(PdptEnd);

            if (PdptEntry->PageSize) {
                PHYSICAL HugePhysical = (PHYSICAL)(PdptEntry->Address << 12);
                UNUSED(HugePhysical);

                DEBUG(TEXT("PDPTE[%u]: VA=%p-%p -> 1GB page PA=%p Present=%u RW=%u Priv=%u NX=%u"),
                    PdptIndex,
                    (LPVOID)PdptBase,
                    (LPVOID)PdptEnd,
                    (LPVOID)HugePhysical,
                    (U32)PdptEntry->Present,
                    (U32)PdptEntry->ReadWrite,
                    (U32)PdptEntry->Privilege,
                    (U32)PdptEntry->NoExecute);
                continue;
            }

            PHYSICAL PageDirectoryPhysical = (PHYSICAL)(PdptEntry->Address << 12);

            DEBUG(TEXT("PDPTE[%u]: VA=%p-%p -> PD_PA=%p Present=%u RW=%u Priv=%u NX=%u"),
                PdptIndex,
                (LPVOID)PdptBase,
                (LPVOID)PdptEnd,
                (LPVOID)PageDirectoryPhysical,
                (U32)PdptEntry->Present,
                (U32)PdptEntry->ReadWrite,
                (U32)PdptEntry->Privilege,
                (U32)PdptEntry->NoExecute);

            LINEAR DirectoryLinear = MapTemporaryPhysicalPage3(PageDirectoryPhysical);

            if (DirectoryLinear == 0) {
                ERROR(TEXT("MapTemporaryPhysicalPage3 failed for directory %p"),
                    (LPVOID)PageDirectoryPhysical);
                continue;
            }

            LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)DirectoryLinear;

            for (UINT DirectoryIndex = 0; DirectoryIndex < PAGE_DIRECTORY_ENTRY_COUNT; DirectoryIndex++) {
                const X86_64_PAGE_DIRECTORY_ENTRY *DirectoryEntry = &Directory[DirectoryIndex];

                if (!DirectoryEntry->Present) continue;

                U64 DirectoryBase = BuildLinearAddress(Pml4Index, PdptIndex, DirectoryIndex, 0, 0);
                U64 DirectoryEnd = BuildRangeEnd(DirectoryBase, (U64)1 << 21);
                UNUSED(DirectoryEnd);

                if (DirectoryEntry->PageSize) {
                    PHYSICAL LargePhysical = (PHYSICAL)(DirectoryEntry->Address << 12);
                    UNUSED(LargePhysical);

                    DEBUG(TEXT("PDE[%u]: VA=%p-%p -> 2MB page PA=%p Present=%u RW=%u Priv=%u Global=%u NX=%u"),
                        DirectoryIndex,
                        (LPVOID)DirectoryBase,
                        (LPVOID)DirectoryEnd,
                        (LPVOID)LargePhysical,
                        (UINT)DirectoryEntry->Present,
                        (UINT)DirectoryEntry->ReadWrite,
                        (UINT)DirectoryEntry->Privilege,
                        (UINT)DirectoryEntry->Global,
                        (UINT)DirectoryEntry->NoExecute);
                    continue;
                }

                PHYSICAL TablePhysical = (PHYSICAL)(DirectoryEntry->Address << 12);

                DEBUG(TEXT("PDE[%u]: VA=%p-%p -> PT_PA=%p Present=%u RW=%u Priv=%u Global=%u NX=%u"),
                    DirectoryIndex,
                    (LPVOID)DirectoryBase,
                    (LPVOID)DirectoryEnd,
                    (LPVOID)TablePhysical,
                    (UINT)DirectoryEntry->Present,
                    (UINT)DirectoryEntry->ReadWrite,
                    (UINT)DirectoryEntry->Privilege,
                    (UINT)DirectoryEntry->Global,
                    (UINT)DirectoryEntry->NoExecute);

                LINEAR TableLinear = MapTemporaryPhysicalPage2(TablePhysical);

                if (TableLinear == 0) {
                    ERROR(TEXT("MapTemporaryPhysicalPage2 failed for table %p"),
                        (LPVOID)TablePhysical);

                    PdptLinear = MapTemporaryPhysicalPage2(PdptPhysical);
                    if (PdptLinear == 0) {
                        ERROR(TEXT("Failed to restore PDPT mapping %p"),
                            (LPVOID)PdptPhysical);
                        return;
                    }

                    Pdpt = (LPPDPT)PdptLinear;
                    DirectoryLinear = MapTemporaryPhysicalPage3(PageDirectoryPhysical);
                    if (DirectoryLinear == 0) {
                        ERROR(TEXT("Failed to restore directory mapping %p"),
                            (LPVOID)PageDirectoryPhysical);
                        return;
                    }

                    Directory = (LPPAGE_DIRECTORY)DirectoryLinear;
                    continue;
                }

                LPPAGE_TABLE Table = (LPPAGE_TABLE)TableLinear;
                UINT MappedCount = 0;

                for (UINT TableIndex = 0; TableIndex < PAGE_TABLE_NUM_ENTRIES; TableIndex++) {
                    const X86_64_PAGE_TABLE_ENTRY *TableEntry = &Table[TableIndex];

                    if (!TableEntry->Present) continue;

                    MappedCount++;

                    if (MappedCount <= 3u || MappedCount >= PAGE_TABLE_NUM_ENTRIES - 2u) {
                        U64 TableVirtual = BuildLinearAddress(Pml4Index, PdptIndex, DirectoryIndex, TableIndex, 0);
                        PHYSICAL PagePhysical = (PHYSICAL)(TableEntry->Address << 12);
                        UNUSED(TableVirtual);
                        UNUSED(PagePhysical);

                        DEBUG(TEXT("PTE[%u]: VA=%p -> PA=%p Present=%u RW=%u Priv=%u Dirty=%u Global=%u NX=%u"),
                            TableIndex,
                            (LPVOID)TableVirtual,
                            (LPVOID)PagePhysical,
                            (U32)TableEntry->Present,
                            (U32)TableEntry->ReadWrite,
                            (U32)TableEntry->Privilege,
                            (U32)TableEntry->Dirty,
                            (U32)TableEntry->Global,
                            (U32)TableEntry->NoExecute);
                    } else if (MappedCount == 4u) {
                        DEBUG(TEXT("... (%u more mapped pages) ..."),
                            PAGE_TABLE_NUM_ENTRIES - 6u);
                    }
                }

                if (MappedCount > 0) {
                    DEBUG(TEXT("Total mapped pages in PT[%u]: %u/%u"),
                        DirectoryIndex,
                        MappedCount,
                        PAGE_TABLE_NUM_ENTRIES);
                }

                PdptLinear = MapTemporaryPhysicalPage2(PdptPhysical);
                if (PdptLinear == 0) {
                    ERROR(TEXT("Failed to restore PDPT mapping %p"),
                        (LPVOID)PdptPhysical);
                    return;
                }

                Pdpt = (LPPDPT)PdptLinear;

                DirectoryLinear = MapTemporaryPhysicalPage3(PageDirectoryPhysical);
                if (DirectoryLinear == 0) {
                    ERROR(TEXT("Failed to restore directory mapping %p"),
                        (LPVOID)PageDirectoryPhysical);
                    return;
                }

                Directory = (LPPAGE_DIRECTORY)DirectoryLinear;
            }
        }
    }

    DEBUG(TEXT("End of page directory"));
}

/************************************************************************/

void LogFrame(LPINTERRUPT_FRAME Frame) {
    if (Frame == NULL) {
        ERROR(TEXT("No interrupt frame provided"));
        return;
    }

    LPTASK Task = GetCurrentTask();
    LPPROCESS Process = GetCurrentProcess();

    KernelLogText(LOG_VERBOSE, TEXT("Task : %p (%s @ %s)"), Task, Task ? Task->Name : TEXT("?"), Process ? Process->FileName : TEXT("?"));
    KernelLogText(LOG_VERBOSE, TEXT("Registers :"));
    LogRegisters64(&(Frame->Registers));
}

/************************************************************************/

void BacktraceFrom(U64 StartRbp, U32 MaxFrames) {
    U32 Depth = 0;
    U64 Rbp = StartRbp;

    KernelLogText(LOG_VERBOSE, TEXT("Backtrace (RBP=%p, max=%u)"), (LPVOID)StartRbp, MaxFrames);

    while (Rbp != 0 && Depth < MaxFrames) {
        if (IsValidMemory((LINEAR)Rbp) == FALSE) {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  RBP=%p  [stop: invalid frame]"), Depth, (LPVOID)Rbp);
            break;
        }

        U64* FramePointer = (U64*)Rbp;
        U64 NextRbp = FramePointer[0];
        U64 ReturnAddress = FramePointer[1];

        if (ReturnAddress == 0) {
            KernelLogText(LOG_VERBOSE, TEXT("#%u  RBP=%p  RET=? [null]"), Depth, (LPVOID)Rbp);
            break;
        }

        KernelLogText(LOG_VERBOSE, TEXT("#%u  RIP=%p  RBP=%p"), Depth, (LPVOID)ReturnAddress, (LPVOID)Rbp);

        if (NextRbp == Rbp) {
            KernelLogText(LOG_VERBOSE,
                TEXT("#%u  Next frame pointer %p equals current frame (stop)"), Depth, (LPVOID)NextRbp);
            Depth++;
            break;
        }

        if (NextRbp != 0 && NextRbp <= Rbp) {
            KernelLogText(LOG_VERBOSE,
                TEXT("#%u  Next frame pointer %p is not greater than current %p (stop)"),
                Depth,
                (LPVOID)NextRbp,
                (LPVOID)Rbp);
            Depth++;
            break;
        }

        Rbp = NextRbp;
        Depth++;
    }

    KernelLogText(LOG_VERBOSE, TEXT("Backtrace end (frames=%u)"), Depth);
}

/************************************************************************/

/**
 * @brief Logs entries from the Interrupt Descriptor Table (IDT).
 * @param Type Log level to use when emitting messages.
 * @param Table Pointer to the IDT gate descriptors.
 * @param EntriesToLog Number of descriptors to dump starting at index 0.
 */
void LogInterruptDescriptorTable(U32 Type, const LPGATE_DESCRIPTOR Table, UINT EntriesToLog) {
    if (Table == NULL) {
        KernelLogText(Type, TEXT("Table pointer is null"));
        return;
    }

    if (EntriesToLog == 0) {
        KernelLogText(Type, TEXT("No entries requested"));
        return;
    }

    KernelLogText(
        Type,
        TEXT("Base=%p, dumping first %u entries"),
        (const void*)Table,
        EntriesToLog);

    UINT Index;
    for (Index = 0; Index < EntriesToLog; ++Index) {
        const LPGATE_DESCRIPTOR Entry = Table + Index;
        const U32* Raw = (const U32*)Entry;

        KernelLogText(
            Type,
            TEXT("Entry %u: raw[31:0]=%x raw[63:32]=%x raw[95:64]=%x raw[127:96]=%x"),
            Index,
            Raw[0],
            Raw[1],
            Raw[2],
            Raw[3]);
        KernelLogText(
            Type,
            TEXT("Selector=%x Type=%u IST=%u DPL=%u Present=%u"),
            (U32)Entry->Selector,
            (U32)Entry->Type,
            (U32)Entry->InterruptStackTable,
            (U32)Entry->Privilege,
            (U32)Entry->Present);
        KernelLogText(
            Type,
            TEXT("Offset_32_63=%x Offset_16_31=%x Offset_00_15=%x"),
            (U32)Entry->Offset_32_63,
            (U32)Entry->Offset_16_31,
            (U32)Entry->Offset_00_15);
    }
}

/************************************************************************/

void LogTaskSystemStructures(U32 Type) {
    LogGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_x86_32.GDT, 5);
    LogInterruptDescriptorTable(Type, (LPGATE_DESCRIPTOR)Kernel_x86_32.IDT, 5);
    LogTaskStateSegment(Type, Kernel_x86_32.TSS);
}
