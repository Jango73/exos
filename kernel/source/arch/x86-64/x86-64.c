
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


    Intel x86-64 Architecture Support

\************************************************************************/

#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

#include "console/Console.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process-Arena.h"
#include "process/Schedule.h"
#include "process/Stack.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "text/Text.h"
#include "system/Interrupt.h"
#include "system/SYSCall.h"

/************************************************************************/

#define INTERRUPTS_VER_MAJOR 1
#define INTERRUPTS_VER_MINOR 0
#define X86_64_SYSTEM_STACK_GUARD_GAP N_64KB

static UINT InterruptsDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION InterruptsDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INTERRUPT,
    .VersionMajor = INTERRUPTS_VER_MAJOR,
    .VersionMinor = INTERRUPTS_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "Interrupts",
    .Alias = "interrupts",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = InterruptsDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the interrupts driver descriptor.
 * @return Pointer to the interrupts driver.
 */
LPDRIVER InterruptsGetDriver(void) {
    return &InterruptsDriver;
}

/************************************************************************\

                              ┌──────────────────────────────────────────┐
                              │        48-bit Virtual Address            │
                              │  [ 47 ................. 0 ]              │
                              └──────────────────────────────────────────┘
                                               │
                                               ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 1: PML4 (Page-Map Level-4 Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [47:39] = index into the PML4 table (512 entries)
     Each PML4E → points to one Page-Directory-Pointer Table (PDPT)

            +------------------+
            | PML4 Entry (PML4E) ───► PDPT base address
            +------------------+
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 2: PDPT (Page-Directory-Pointer Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [38:30] = index into PDPT (512 entries)
     Each PDPTE normally points to a Page Directory.
     But if bit 7 (PS) = 1 → 1 GiB *large page*.

             ┌──────────────────────────────┐
             │ PDPTE                       │
             │ ─ bit 7 (PS) = 1 → 1 GiB page│────► Physical 1 GiB page
             │ ─ bit 7 (PS) = 0 → Page Dir. │────► PD base address
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 3: PD (Page Directory)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [29:21] = index into PD (512 entries)
     Each PDE normally points to a Page Table.
     But if bit 7 (PS) = 1 → 2 MiB *large page*.

             ┌──────────────────────────────┐
             │ PDE                         │
             │ ─ bit 7 (PS) = 1 → 2 MiB page│────► Physical 2 MiB page
             │ ─ bit 7 (PS) = 0 → Page Tbl. │────► PT base address
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 4: PT (Page Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [20:12] = index into PT (512 entries)
     Each PTE points to a 4 KiB physical page.

             ┌──────────────────────────────┐
             │ PTE → Physical 4 KiB page    │
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 5: Physical Address
    ────────────────────────────────────────────────────────────────────────────
     Offset bits [11:0] select the byte within the final page.

             Physical Address = { FrameBase[51:12], VA[11:0] }

    ────────────────────────────────────────────────────────────────────────────
     Summary of page sizes per level (4-level paging)
    ────────────────────────────────────────────────────────────────────────────

     | Level | Table name | Page size (if PS=1) | Entries | Coverage per entry |
     |--------|-------------|--------------------|----------|--------------------|
     | PML4   | PML4 table  | —                  | 512      | 512 GiB            |
     | PDPT   | PDP table   | 1 GiB (PS=1)       | 512      | 1 GiB              |
     | PD     | Page Dir.   | 2 MiB (PS=1)       | 512      | 2 MiB              |
     | PT     | Page Table  | 4 KiB              | 512      | 4 KiB              |

    ────────────────────────────────────────────────────────────────────────────
     Example:
       0x00007F12_3456_789A
       ├─[47:39]→ PML4 index
       ├─[38:30]→ PDPT index
       ├─[29:21]→ PD index
       ├─[20:12]→ PT index
       └─[11:0] → Offset inside 4 KiB page
    ────────────────────────────────────────────────────────────────────────────

\************************************************************************/

KERNEL_DATA_X86_64 DATA_SECTION Kernel_x86_32 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL,
};

extern void Interrupt_SystemCall(void);

/**
 * @brief Read a 64-bit value from the specified MSR.
 * @param Msr Model-specific register index to read.
 * @return Combined 64-bit value of the MSR contents.
 */
static U64 ReadMSR64Local(U32 Msr) {
    U32 Low;
    U32 High;

    __asm__ volatile ("rdmsr" : "=a"(Low), "=d"(High) : "c"(Msr));

    return (((U64)High) << 32) | (U64)Low;
}

/************************************************************************/

/**
 * @brief Set the handler address for a 64-bit IDT gate descriptor.
 * @param Descriptor IDT entry to update.
 * @param Handler Linear address of the interrupt handler.
 */
void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler) {
    U64 Offset = (U64)Handler;

    Descriptor->Offset_00_15 = (U16)(Offset & 0x0000FFFF);
    Descriptor->Offset_16_31 = (U16)((Offset >> 16) & 0x0000FFFF);
    Descriptor->Offset_32_63 = (U32)((Offset >> 32) & 0xFFFFFFFF);
    Descriptor->Reserved_2 = 0;
}

/************************************************************************/

/**
 * @brief Initialize a 64-bit IDT gate descriptor.
 * @param Descriptor IDT entry to configure.
 * @param Handler Linear address of the interrupt handler.
 * @param Type Gate type to install.
 * @param Privilege Descriptor privilege level.
 */
void InitializeGateDescriptor(
    LPGATE_DESCRIPTOR Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege,
    U8 InterruptStackTable) {
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->InterruptStackTable = InterruptStackTable & 0x7u;
    Descriptor->Reserved_0 = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;
    Descriptor->Reserved_1 = 0;

    SetGateDescriptorOffset(Descriptor, Handler);
}

/***************************************************************************/

extern GATE_DESCRIPTOR IDT[];
extern VOIDFUNC InterruptTable[];

/***************************************************************************/

static U8 SelectInterruptStackTable(U32 InterruptIndex) {
    switch (InterruptIndex) {
    case 8u:   // Double fault
    case 10u:  // Invalid TSS
    case 11u:  // Segment not present
    case 12u:  // Stack fault
    case 13u:  // General protection fault
    case 14u:  // Page fault
        return 1u;
    default:
        return 0u;
    }
}

/***************************************************************************/

void InitializeInterrupts(void) {
    Kernel_x86_32.IDT = IDT;

    for (U32 Index = 0; Index < NUM_INTERRUPTS; Index++) {
        U8 InterruptStack = SelectInterruptStackTable(Index);

        InitializeGateDescriptor(
            IDT + Index,
            (LINEAR)(InterruptTable[Index]),
            GATE_TYPE_386_INT,
            CPU_PRIVILEGE_KERNEL,
            InterruptStack);
    }

    InitializeSystemCall();

    LoadInterruptDescriptorTable((LINEAR)IDT, IDT_SIZE - 1u);

    ClearDR7();

    InitializeSystemCallTable();
}

/************************************************************************/

static void InitLongModeSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, BOOL Executable, U32 Privilege) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
    This->Code = Executable;
    This->S = 1;
    This->Privilege = Privilege;
    This->Present = 1;
    This->Limit_16_19 = 0x0F;
    This->Available = 0;
    This->LongMode = 1;
    This->DefaultSize = 0;
    This->Granularity = 1;
    This->Base_24_31 = 0x00;
}

/***************************************************************************/

/**
 * @brief Initialize a 32-bit legacy segment descriptor for compatibility gates.
 * @param This Descriptor to configure.
 * @param Executable TRUE for a code segment, FALSE for data.
 */
static void InitLegacySegmentDescriptor(LPSEGMENT_DESCRIPTOR This, BOOL Executable) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Limit_16_19 = 0x0F;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Base_24_31 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
    This->Code = Executable;
    This->S = 1;
    This->Privilege = CPU_PRIVILEGE_KERNEL;
    This->Present = 1;
    This->Available = 0;
    This->LongMode = 0;
    This->DefaultSize = 0;
    This->Granularity = 0;
}

/***************************************************************************/

/**
 * @brief Populate the shared GDT with long mode and compatibility segments.
 * @param Table Pointer to the descriptor table buffer.
 */
void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("Enter"));

    MemorySet(Table, 0, GDT_SIZE);

    InitLongModeSegmentDescriptor(&Table[1], TRUE, CPU_PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[2], FALSE, CPU_PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[3], TRUE, CPU_PRIVILEGE_USER);
    InitLongModeSegmentDescriptor(&Table[4], FALSE, CPU_PRIVILEGE_USER);
    InitLegacySegmentDescriptor(&Table[5], TRUE);
    InitLegacySegmentDescriptor(&Table[6], FALSE);

    DEBUG(TEXT("Exit"));
}

/***************************************************************************/

/**
 * @brief Initialize the architecture-specific context for a task.
 *
 * Allocates kernel and user stacks for the task, clears the interrupt frame,
 * and seeds the register snapshot so the generic scheduler can operate while
 * the long mode context-switching code is under construction.
 */
BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASK_INFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    UNUSED(BaseVMA);
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    LINEAR StackTop;
    LINEAR SysStackTop;
    LINEAR BootStackTop;
    LINEAR RSP, RBP;
    U64 CR4;

    DEBUG(TEXT("Enter"));

    if (Process->Privilege == CPU_PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    Task->Arch.Stack.Size = Info->StackSize;
    Task->Arch.SystemStack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;
    Task->Arch.Ist1Stack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;

    Task->Arch.Stack.Base = ProcessArenaAllocateTaskStack(Process, Task->Arch.Stack.Size);
    Task->Arch.SystemStack.Base =
        AllocKernelRegion(0, Task->Arch.SystemStack.Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("SystemStack"));
    if (Task->Arch.SystemStack.Base != 0) {
        LINEAR MinimumIst1Base =
            Task->Arch.SystemStack.Base + (LINEAR)Task->Arch.SystemStack.Size + X86_64_SYSTEM_STACK_GUARD_GAP;
        Task->Arch.Ist1Stack.Base = AllocRegion(
            MinimumIst1Base,
            0,
            Task->Arch.Ist1Stack.Size,
            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
            TEXT("Ist1Stack"));
    } else {
        Task->Arch.Ist1Stack.Base = 0;
    }

    DEBUG(TEXT("BaseVMA=%p, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("Actually got StackBase=%p"), Task->Arch.Stack.Base);

    if (Task->Arch.Stack.Base == NULL || Task->Arch.SystemStack.Base == NULL || Task->Arch.Ist1Stack.Base == NULL) {
        if (Task->Arch.Stack.Base != NULL) {
            FreeRegion(Task->Arch.Stack.Base, Task->Arch.Stack.Size);
            Task->Arch.Stack.Base = 0;
            Task->Arch.Stack.Size = 0;
        }

        if (Task->Arch.SystemStack.Base != NULL) {
            FreeRegion(Task->Arch.SystemStack.Base, Task->Arch.SystemStack.Size);
            Task->Arch.SystemStack.Base = 0;
            Task->Arch.SystemStack.Size = 0;
        }

        if (Task->Arch.Ist1Stack.Base != NULL) {
            FreeRegion(Task->Arch.Ist1Stack.Base, Task->Arch.Ist1Stack.Size);
            Task->Arch.Ist1Stack.Base = 0;
            Task->Arch.Ist1Stack.Size = 0;
        }

        ERROR(TEXT("Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("Stack (%u bytes) allocated at %p"), Task->Arch.Stack.Size, Task->Arch.Stack.Base);
    DEBUG(TEXT("System stack (%u bytes) allocated at %p"), Task->Arch.SystemStack.Size,
        Task->Arch.SystemStack.Base);
    DEBUG(TEXT("IST1 stack (%u bytes) allocated at %p"), Task->Arch.Ist1Stack.Size,
        Task->Arch.Ist1Stack.Base);

    MemorySet((LPVOID)(Task->Arch.Stack.Base), 0, Task->Arch.Stack.Size);
    MemorySet((LPVOID)(Task->Arch.SystemStack.Base), 0, Task->Arch.SystemStack.Size);
    MemorySet((LPVOID)(Task->Arch.Ist1Stack.Base), 0, Task->Arch.Ist1Stack.Size);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    GetCR4(CR4);

    Task->Arch.Context.Registers.RAX = (UINT)Task->Parameter;
    Task->Arch.Context.Registers.RBX = (LINEAR)Task->Function;
    Task->Arch.Context.Registers.RCX = 0;
    Task->Arch.Context.Registers.RDX = 0;
    Task->Arch.Context.Registers.RSI = 0;
    Task->Arch.Context.Registers.RDI = 0;
    Task->Arch.Context.Registers.R8 = 0;
    Task->Arch.Context.Registers.R9 = 0;
    Task->Arch.Context.Registers.R10 = 0;
    Task->Arch.Context.Registers.R11 = 0;
    Task->Arch.Context.Registers.R12 = 0;
    Task->Arch.Context.Registers.R13 = 0;
    Task->Arch.Context.Registers.R14 = 0;
    Task->Arch.Context.Registers.R15 = 0x0000DEADBEEF0000;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = (Process->Privilege == CPU_PRIVILEGE_USER) ? SELECTOR_NULL : DataSelector;
    Task->Arch.Context.Registers.GS = (Process->Privilege == CPU_PRIVILEGE_USER) ? SELECTOR_NULL : DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.RFlags = RFLAGS_IF | RFLAGS_ALWAYS_1;
    Task->Arch.Context.Registers.CR3 = Process->PageDirectory;
    Task->Arch.Context.Registers.CR4 = CR4;
    Task->Arch.UserTlsBase = 0;

    StackTop = Task->Arch.Stack.Base + Task->Arch.Stack.Size;
    SysStackTop = Task->Arch.SystemStack.Base + Task->Arch.SystemStack.Size;

    if (Process->Privilege == CPU_PRIVILEGE_KERNEL) {
        DEBUG(TEXT("Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.RIP = (LINEAR)TaskRunner;
        Task->Arch.Context.Registers.RSP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.RIP = VMA_TASK_RUNNER;
        Task->Arch.Context.Registers.RSP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->SchedulerState.Status = TASK_STATUS_RUNNING;

        Task->Arch.Context.SS0 = SELECTOR_KERNEL_DATA;
        Task->Arch.Context.RSP0 = SysStackTop - STACK_SAFETY_MARGIN;

        BootStackTop = (LINEAR)KernelStartup.StackTop;

        GetESP(RSP);
        UINT StackUsed = (BootStackTop - RSP) + 256;

        DEBUG(TEXT("BootStackTop = %p"), BootStackTop);
        DEBUG(TEXT("StackTop = %p"), StackTop);
        DEBUG(TEXT("StackUsed = %u"), StackUsed);
        DEBUG(TEXT("Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed) == TRUE) {
            Task->Arch.Context.Registers.RSP = 0;
            GetEBP(RBP);
            Task->Arch.Context.Registers.RBP = RBP;
            DEBUG(TEXT("Main task stack switched successfully"));
        } else {
            ERROR(TEXT("Stack switch failed"));
        }
    }

    DEBUG(TEXT("Exit"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepare architectural state ahead of a context switch.
 * @param CurrentTask Task that is currently running (may be NULL).
 * @param NextTask Task that will become active.
 */
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
    SAFE_USE(NextTask) {
        FINE_DEBUG(TEXT("CurrentTask = %p (%s), NextTask = %p (%s)"),
            CurrentTask, CurrentTask->Name, NextTask, NextTask->Name);

        LINEAR NextSysStackTop = NextTask->Arch.SystemStack.Base + NextTask->Arch.SystemStack.Size;
        LINEAR NextIst1StackTop = NextTask->Arch.Ist1Stack.Base + NextTask->Arch.Ist1Stack.Size;

        FINE_DEBUG(TEXT("NextSysStackTop = %p"), NextSysStackTop);
        FINE_DEBUG(TEXT("NextIst1StackTop = %p"), NextIst1StackTop);

        Kernel_x86_32.TSS->RSP0 = NextSysStackTop - STACK_SAFETY_MARGIN;
        Kernel_x86_32.TSS->IST1 = NextIst1StackTop - STACK_SAFETY_MARGIN;
        Kernel_x86_32.TSS->IOMapBase = (U16)sizeof(X86_64_TASK_STATE_SEGMENT);

        SAFE_USE(CurrentTask) {
            GetFS(CurrentTask->Arch.Context.Registers.FS);
            GetGS(CurrentTask->Arch.Context.Registers.GS);
            CurrentTask->Arch.UserTlsBase = (LINEAR)ReadMSR64Local(IA32_FS_BASE_MSR);
            SaveFPU(&(CurrentTask->Arch.Context.FPURegisters));
        }

        // SetSS(NextTask->Arch.Context.Registers.SS);
        // SetDS(NextTask->Arch.Context.Registers.DS);
        // SetES(NextTask->Arch.Context.Registers.ES);
        SetFS(NextTask->Arch.Context.Registers.FS);
        SetGS(NextTask->Arch.Context.Registers.GS);
        WriteMSR64(IA32_FS_BASE_MSR,
                   (U32)(((U64)NextTask->Arch.UserTlsBase) & 0xFFFFFFFF),
                   (U32)(((U64)NextTask->Arch.UserTlsBase) >> 32));
        WriteMSR64(IA32_GS_BASE_MSR, 0, 0);

        RestoreFPU(&(NextTask->Arch.Context.FPURegisters));
    }
}

/************************************************************************/

/**
 * @brief Set the x86-64 user TLS anchor for a task.
 *
 * @param Task Target task.
 * @param Anchor User thread control block base, or zero.
 * @return TRUE when the task architecture state was updated.
 */
BOOL TaskSetUserTlsAnchor(struct tag_TASK* Task, LINEAR Anchor) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (Task->OwnerProcess == NULL || Task->OwnerProcess->Privilege != CPU_PRIVILEGE_USER) {
            return Anchor == 0;
        }

        Task->Arch.UserTlsBase = Anchor;
        Task->Arch.Context.Registers.FS = SELECTOR_NULL;
        Task->Arch.Context.Registers.GS = SELECTOR_NULL;
        if (Task == GetCurrentTask()) {
            SetFS(Task->Arch.Context.Registers.FS);
            SetGS(Task->Arch.Context.Registers.GS);
            WriteMSR64(IA32_FS_BASE_MSR,
                       (U32)(((U64)Task->Arch.UserTlsBase) & 0xFFFFFFFF),
                       (U32)(((U64)Task->Arch.UserTlsBase) >> 32));
            WriteMSR64(IA32_GS_BASE_MSR, 0, 0);
        }
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for x86-64.
 */
/************************************************************************/

/**
 * @brief Translate a linear address to its physical counterpart (page-level granularity).
 * @param Address Linear address.
 * @return Physical address or 0 when unmapped.
 */
void PreInitializeKernel(void) {
    GDT_REGISTER Gdtr;
    U64 Cr0;
    U64 Cr4;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_x86_32.GDT = (LPVOID)(LINEAR)Gdtr.Base;

    KernelStartup.PageDirectory = GetPageDirectory();
    KernelStartup.IRQMask_21_RM = 0;
    KernelStartup.IRQMask_A1_RM = 0;

    DEBUG(TEXT("CR0 : CR0_COPROCESSOR on and CR0_EMULATION off"));

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 |= (U64)(CR0_COPROCESSOR | CR0_NUMERIC_ERROR);
    Cr0 &= ~(U64)(CR0_EMULATION | CR0_TASKSWITCH);
    __asm__ volatile("mov %0, %%cr0" : : "r"(Cr0));


    DEBUG(TEXT("CR4 : CR4_OSFXSR and CR4_OSXMMEXCPT on"));

    __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= (U64)(CR4_OSFXSR | CR4_OSXMMEXCPT);
    __asm__ volatile("mov %0, %%cr4" : : "r"(Cr4));

    __asm__ volatile("fninit");
    __asm__ volatile("fnclex");

    InitializePat();

}

/************************************************************************/

/**
 * @brief Configure MSRs required for SYSCALL/SYSRET transitions.
 */
void InitializeSystemCall(void) {
#if USE_SYSCALL
    U64 StarValue;
    U64 EntryPoint;
    U64 MaskValue;
    U64 EferValue;

    StarValue = ((U64)SELECTOR_USER_CODE << 48) | ((U64)SELECTOR_KERNEL_CODE << 32);
    WriteMSR64(IA32_STAR_MSR, (U32)(StarValue & 0xFFFFFFFF), (U32)(StarValue >> 32));

    EntryPoint = (U64)(LINEAR)Interrupt_SystemCall;
    WriteMSR64(IA32_LSTAR_MSR, (U32)(EntryPoint & 0xFFFFFFFF), (U32)(EntryPoint >> 32));

    MaskValue = RFLAGS_TF | RFLAGS_IF | RFLAGS_DF;
    WriteMSR64(IA32_FMASK_MSR, (U32)(MaskValue & 0xFFFFFFFF), (U32)(MaskValue >> 32));

    EferValue = ReadMSR64Local(IA32_EFER_MSR);
    EferValue |= IA32_EFER_SCE;
    WriteMSR64(IA32_EFER_MSR, (U32)(EferValue & 0xFFFFFFFF), (U32)(EferValue >> 32));
#else
    InitializeGateDescriptor(
        IDT + EXOS_USER_CALL,
        (LINEAR)Interrupt_SystemCall,
        GATE_TYPE_386_TRAP,
        CPU_PRIVILEGE_USER,
        0u);
#endif
}

/************************************************************************/

/**
 * @brief Log syscall stack frame information before returning to userland.
 * @param SaveArea Base address of the saved register block on the user stack.
 * @param FunctionId System call identifier that is about to complete.
 */
void DebugLogSyscallFrame(LINEAR SaveArea, UINT FunctionId) {
    U8* SavePtr;
    LINEAR StackPointer;
    LINEAR SavedRbxValue;
    LINEAR ReturnAddress;
    UNUSED(FunctionId);

    if (SaveArea == (LINEAR)0) {
        DEBUG(TEXT("SaveArea missing for Function=%u"), FunctionId);
        return;
    }

    SavePtr = (U8*)(SaveArea);
    StackPointer = (LINEAR)(SaveArea + (LINEAR)SYSCALL_SAVE_AREA_SIZE);
    SavedRbxValue = *((LINEAR*)(SavePtr + SYSCALL_SAVE_AREA_SIZE));
    ReturnAddress = *((LINEAR*)(SavePtr + SYSCALL_SAVE_AREA_SIZE + sizeof(LINEAR)));
    UNUSED(StackPointer);
    UNUSED(SavedRbxValue);
    UNUSED(ReturnAddress);

    DEBUG(TEXT("Function=%u SaveArea=%p StackPtr=%p SavedRBX=%p Return=%p"),
          FunctionId, (LPVOID)SaveArea, (LPVOID)StackPointer, (LPVOID)SavedRbxValue, (LPVOID)ReturnAddress);
}

/************************************************************************/

/**
 * @brief Driver command handler for the interrupt subsystem.
 *
 * DF_LOAD initializes the IDT while DF_UNLOAD only clears the ready flag
 * as no shutdown routine is available.
 */
static UINT InterruptsDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((InterruptsDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeInterrupts();
            InterruptsDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((InterruptsDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            InterruptsDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(INTERRUPTS_VER_MAJOR, INTERRUPTS_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
