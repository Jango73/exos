
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


    X86_32

\************************************************************************/

#include "arch/x86-32/x86-32.h"

#include "log/Log.h"
#include "memory/Memory.h"
#include "console/Console.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "arch/x86-32/x86-32-Log.h"
#include "process/Stack.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "process/Task.h"
#include "text/Text.h"
#include "core/Kernel.h"
#include "system/Interrupt.h"
#include "system/SYSCall.h"

/************************************************************************\

    Virtual Address Space (32-bit)
    ┌──────────────────────────────────────────────────────────────────────────┐
    │ 0x00000000 .................................................. 0xBFFFFFFF │
    │                [User space]  (PDE 0..KernelDir-1)                        │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xC0000000 .................................................. 0xFFFFEFFF │
    │                [Kernel space] (PDE KernelDir .. 1022)                    │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xFFFFF000 .................................................. 0xFFFFFFFF │
    │                [Self-map window]                                         │
    │                0xFFFFF000 = PD_VA (Page Directory as an array of PDEs)   │
    │                0xFFC00000 = PT_BASE_VA (all Page Tables visible)         │
    └──────────────────────────────────────────────────────────────────────────┘

    Page Directory (1024 PDEs, each 4B)
    dir = (VMA >> 22)
    tab = (VMA >> 12) & 0x3FF
    ofs =  VMA & 0xFFF

                      PDE index
            ┌────────────┬────────────┬────────────┬────────────┬─────────────┐
            │     0      │     1      │   ...      │ KernelDir  │   1023      │
            ├────────────┼────────────┼────────────┼────────────┼─────────── ─┤
    points→ │  Low PT    │   PT #1    │   ...      │ Kernel PT  │  SELF-MAP   │
    to PA   │ (0..4MB)   │            │            │ (VMA_KERNEL)│ (PD itself)│
            └────────────┴────────────┴────────────┴────────────┴─────────────┘
                                                              ^
                                                              |
                                         PDE[1023] -> PD physical page (recursive)
                                                              |
                                                              v
    PD_VA = 0xFFFFF000 ----------------------------------> Page Directory (VA alias)


    All Page Tables via the recursive window:
    PT_BASE_VA = 0xFFC00000
    PT for PDE = D is at:   PT_VA(D) = 0xFFC00000 + D * 0x1000

    Examples:
    - PT of PDE 0:        0xFFC00000
    - PT of KernelDir:    0xFFC00000 + KernelDir*0x1000
    - PT of PDE 1023:     0xFFC00000 + 1023*0x1000  (not used for mappings)


    Resolution path for any VMA:
           VMA
            │
       dir = VMA>>22  ------>  PD_VA[dir] (PDE)  ------>  PT_VA(dir)[tab] (PTE)  ------>  PA + ofs

    Kernel mappings installed at init:
    - PDE[0]         -> Low PT (identity map 0..4MB)
    - PDE[KernelDir] -> Kernel PT (maps VMA_KERNEL .. VMA_KERNEL+4MB-1)
    - PDE[1023]      -> PD itself (self-map)


    Temporary mapping mechanism (MapTemporaryPhysicalPage1):
    1) Two VAs reserved dynamically (e.g., G_TempLinear1, G_TempLinear2).
    2) To map a physical frame P into G_TempLinear1:
       - Compute dir/tab of G_TempLinear1
       - Write the PTE via the PT window:
           PT_VA(dir) = PT_BASE_VA + dir*0x1000, entry [tab]
       - Execute `invlpg [G_TempLinear1]`
       - The physical frame P is now accessible via the VA G_TempLinear1

    Simplified view of the two temporary pages:

                         (reserved via AllocRegion, not present by default)
    G_TempLinear1  -\    ┌────────────────────────────────────────────┐
                    |-─> │ PTE < (Present=1, RW=1, ..., Address=P>>12)│  map/unmap to chosen PA
    G_TempLinear2  -/    └────────────────────────────────────────────┘
                                   ^
                                   │ (written through) PT_VA(dir(G_TempLinearX)) = PT_BASE_VA + dir*0x1000
                                   │
                              PD self-map (PD_VA, PT_BASE_VA)

    PDE[1023] points to the Page Directory itself.
    PD_VA = 0xFFFFF000 gives access to the current PD (as PTE-like entries).
    PT_BASE_VA = 0xFFC00000 provides a window for Page Tables:
    PT for directory index D is at PT_BASE_VA + (D * PAGE_SIZE).

    Temporary physical access is done by remapping two reserved
    linear pages (G_TempLinear1, G_TempLinear2, G_TempLinear3) on demand.

    =================================================================

    PCI BAR mapping process (example: Intel E1000 NIC)

    ┌───────────────────────────┐
    │  PCI Configuration Space  │
    │  (accessed via PCI config │
    │   reads/writes)           │
    └───────────┬───────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
    ┌────────────────────────────────┐
    │ BAR0 value = Physical address  │
    │ of device registers (MMIO)     │
    │ + resource size                │
    └───────────┬────────────────────┘
                │
                │ Map physical MMIO region into
                │ kernel virtual space
                │ (uncached for DMA safety)
                ▼
    ┌───────────────────────────┐
    │ AllocRegion(Base=0,       │
    │   Target=BAR0,            │
    │   Size=MMIO size,         │
    │   Flags=ALLOC_PAGES_COMMIT│
    │         | ALLOC_PAGES_UC) │
    └───────────┬───────────────┘
                │
                │ Returns Linear (VMA) address
                │ where the driver can access MMIO
                ▼
    ┌───────────────────────────────┐
    │ Driver reads/writes registers │
    │ via *(volatile U32*)(VMA+ofs) │
    │ Example: E1000_CTRL register  │
    └───────────────────────────────┘

    NOTES:
    - MMIO (Memory-Mapped I/O) must be UNCACHED (UC) to avoid
     stale data and incorrect ordering.
    - BARs can also point to I/O port ranges instead of MMIO.
    - PCI devices can have multiple BARs for different resources.

\************************************************************************/

extern GATE_DESCRIPTOR IDT[];
extern void Interrupt_SystemCall(void);
extern VOIDFUNC InterruptTable[];

/************************************************************************/

static UINT InterruptsDriverCommands(UINT Function, UINT Parameter);

/************************************************************************/

// Uncomment below to mark BIOS memory pages "not present" in the page tables
// #define PROTECT_BIOS
#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

#define INTERRUPTS_VER_MAJOR 1
#define INTERRUPTS_VER_MINOR 0

/************************************************************************/

KERNEL_DATA_X86_32 DATA_SECTION Kernel_x86_32 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL
};

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

#define GDT_USER_TLS_FIRST_INDEX (GDT_TSS_INDEX + 1)

/**
 * @brief Retrieves the interrupts driver descriptor.
 * @return Pointer to the interrupts driver.
 */
LPDRIVER InterruptsGetDriver(void) {
    return &InterruptsDriver;
}

/************************************************************************/

/**
 * @brief Set the handler address for an IDT gate descriptor.
 * @param Descriptor IDT entry to update.
 * @param Handler Linear address of the interrupt handler.
 */
void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler) {
    U32 Offset = (U32)Handler;

    Descriptor->Offset_00_15 = (U16)(Offset & 0x0000FFFFu);
    Descriptor->Offset_16_31 = (U16)((Offset >> 16) & 0x0000FFFFu);
}

/***************************************************************************/

/**
 * @brief Initialize an IDT gate descriptor.
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
    UNUSED(InterruptStackTable);
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->Reserved = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;

    SetGateDescriptorOffset(Descriptor, Handler);
}

/***************************************************************************/

void InitializeInterrupts(void) {
    Kernel_x86_32.IDT = IDT;

    for (U32 Index = 0; Index < NUM_INTERRUPTS; Index++) {
        InitializeGateDescriptor(
            IDT + Index,
            (LINEAR)(InterruptTable[Index]),
            GATE_TYPE_386_INT,
            CPU_PRIVILEGE_KERNEL,
            0u);
    }

    InitializeSystemCall();

    LoadInterruptDescriptorTable((LINEAR)IDT, IDT_SIZE - 1u);

    ClearDR7();

    InitializeSystemCallTable();
}

/***************************************************************************/

void InitSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, U32 Type) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
    This->Type = Type;
    This->Segment = 1;
    This->Privilege = CPU_PRIVILEGE_USER;
    This->Present = 1;
    This->Limit_16_19 = 0x0F;
    This->Available = 0;
    This->OperandSize = 1;
    This->Granularity = GDT_GRANULAR_4KB;
    This->Base_24_31 = 0x00;
}

/***************************************************************************/

void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Enter"));

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] GDT address = %X"), (U32)Table);

    MemorySet(Table, 0, GDT_SIZE);

    InitSegmentDescriptor(&Table[1], GDT_TYPE_CODE);
    Table[1].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[2], GDT_TYPE_DATA);
    Table[2].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[3], GDT_TYPE_CODE);
    Table[3].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[4], GDT_TYPE_DATA);
    Table[4].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[5], GDT_TYPE_CODE);
    Table[5].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[5].OperandSize = GDT_OPERANDSIZE_16;
    Table[5].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[5], N_1MB_M1);

    InitSegmentDescriptor(&Table[6], GDT_TYPE_DATA);
    Table[6].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[6].OperandSize = GDT_OPERANDSIZE_16;
    Table[6].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[6], N_1MB_M1);

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Exit"));
}

/***************************************************************************/

void SetSegmentDescriptorBase(LPSEGMENT_DESCRIPTOR This, U32 Base) {
    This->Base_00_15 = (Base & (U32)0x0000FFFF) >> 0x00;
    This->Base_16_23 = (Base & (U32)0x00FF0000) >> 0x10;
    This->Base_24_31 = (Base & (U32)0xFF000000) >> 0x18;
}

/***************************************************************************/

void SetSegmentDescriptorLimit(LPSEGMENT_DESCRIPTOR This, U32 Limit) {
    This->Limit_00_15 = (Limit >> 0x00) & 0x0000FFFF;
    This->Limit_16_19 = (Limit >> 0x10) & 0x0000000F;
}

/************************************************************************/

BOOL GetSegmentInfo(LPSEGMENT_DESCRIPTOR This, LPSEGMENT_INFO Info) {
    if (Info) {
        Info->Base = SEGMENTBASE(This);
        Info->Limit = SEGMENTLIMIT(This);
        Info->Type = This->Type;
        Info->Privilege = This->Privilege;
        Info->Granularity = SEGMENTGRANULAR(This);
        Info->CanWrite = This->CanWrite;
        Info->OperandSize = This->OperandSize ? 32 : 16;
        Info->Conforming = This->ConformExpand;
        Info->Present = This->Present;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL SegmentInfoToString(LPSEGMENT_INFO This, LPSTR Text) {
    if (This && Text) {
        STR Temp[64];

        Text[0] = STR_NULL;

        StringConcat(Text, TEXT("Segment"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Base           : "));
        U32ToHexString(This->Base, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Limit          : "));
        U32ToHexString(This->Limit, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Type           : "));
        StringConcat(Text, This->Type ? TEXT("Code") : TEXT("Data"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Privilege      : "));
        U32ToHexString(This->Privilege, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Granularity    : "));
        U32ToHexString(This->Granularity, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Can write      : "));
        StringConcat(Text, This->CanWrite ? TEXT("True") : TEXT("False"));
        StringConcat(Text, Text_NewLine);

        /*
            StringConcat(Text, "Operand Size   : ");
            StringConcat(Text, This->OperandSize ? "32-bit" : "16-bit");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Conforming     : ");
            StringConcat(Text, This->Conforming ? "True" : "False");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Present        : ");
            StringConcat(Text, This->Present ? "True" : "False");
            StringConcat(Text, Text_NewLine);
        */

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Find a free GDT descriptor for one user TLS anchor.
 *
 * @return Free descriptor index or zero.
 */
static UINT FindFreeUserTlsDescriptorIndex(void) {
    if (Kernel_x86_32.GDT == NULL) {
        return 0;
    }

    for (UINT Index = GDT_USER_TLS_FIRST_INDEX; Index < GDT_NUM_DESCRIPTORS; Index++) {
        if (Kernel_x86_32.GDT[Index].Present == 0) {
            return Index;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Release the GDT descriptor used by a task TLS anchor.
 *
 * @param Task Target task.
 */
static void ReleaseUserTlsDescriptor(struct tag_TASK* Task) {
    if (Task == NULL || Task->Arch.UserTlsDescriptorIndex == 0 || Kernel_x86_32.GDT == NULL) {
        return;
    }

    MemorySet(Kernel_x86_32.GDT + Task->Arch.UserTlsDescriptorIndex, 0, sizeof(SEGMENT_DESCRIPTOR));
    Task->Arch.UserTlsDescriptorIndex = 0;
    Task->Arch.UserTlsSelector = SELECTOR_NULL;
    Task->Arch.Context.Registers.FS = SELECTOR_NULL;
    Task->Arch.Context.Registers.GS = SELECTOR_NULL;
    if (Task == GetCurrentTask()) {
        SetFS(Task->Arch.Context.Registers.FS);
        SetGS(Task->Arch.Context.Registers.GS);
    }
}

/************************************************************************/

/**
 * @brief Synchronize syscall return segment registers with the current task.
 *
 * @param StackPointer System call stack pointer at the saved GS slot.
 */
void TaskSynchronizeCurrentSystemCallSegments(LINEAR StackPointer) {
    LPTASK Task;
    U32* Stack;

    Task = GetCurrentTask();
    Stack = (U32*)StackPointer;

    if (Task == NULL || Stack == NULL) {
        return;
    }

    Stack[0] = Task->Arch.Context.Registers.GS;
    Stack[1] = Task->Arch.Context.Registers.FS;
}

/************************************************************************/

/**
 * @brief Set the x86-32 user TLS anchor for a task.
 *
 * @param Task Target task.
 * @param Anchor User thread control block base, or zero.
 * @return TRUE when the task architecture state was updated.
 */
BOOL TaskSetUserTlsAnchor(struct tag_TASK* Task, LINEAR Anchor) {
    UINT DescriptorIndex;
    LPSEGMENT_DESCRIPTOR Descriptor;
    SELECTOR Selector;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (Task->OwnerProcess == NULL || Task->OwnerProcess->Privilege != CPU_PRIVILEGE_USER || Anchor == 0) {
            ReleaseUserTlsDescriptor(Task);
            return TRUE;
        }

        DescriptorIndex = Task->Arch.UserTlsDescriptorIndex;
        if (DescriptorIndex == 0) {
            DescriptorIndex = FindFreeUserTlsDescriptorIndex();
            if (DescriptorIndex == 0) {
                ERROR(TEXT("[TaskSetUserTlsAnchor] No free user TLS descriptor task=%p"), Task);
                return FALSE;
            }
        }

        Descriptor = Kernel_x86_32.GDT + DescriptorIndex;
        InitSegmentDescriptor(Descriptor, GDT_TYPE_DATA);
        Descriptor->Privilege = GDT_PRIVILEGE_USER;
        SetSegmentDescriptorBase(Descriptor, (U32)Anchor);
        SetSegmentDescriptorLimit(Descriptor, N_4GB);

        Selector = MAKE_GDT_SELECTOR(DescriptorIndex, GDT_PRIVILEGE_USER);
        Task->Arch.UserTlsDescriptorIndex = DescriptorIndex;
        Task->Arch.UserTlsSelector = Selector;
        Task->Arch.Context.Registers.FS = SELECTOR_NULL;
        Task->Arch.Context.Registers.GS = Selector;
        if (Task == GetCurrentTask()) {
            SetFS(Task->Arch.Context.Registers.FS);
            SetGS(Task->Arch.Context.Registers.GS);
        }
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Perform x86-32-specific initialisation for a freshly created task.
 *
 * Allocates and clears the user and system stacks, seeds the interrupt frame
 * with the correct segment selectors, and configures the boot-time stack when
 * creating the main kernel task. The generic KernelCreateTask routine handles the
 * architecture-neutral bookkeeping and delegates the hardware specific work to
 * this helper.
 */
BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASK_INFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    LINEAR StackTop;
    LINEAR SysStackTop;
    LINEAR BootStackTop;
    LINEAR ESP, EBP;
    U32 CR4;

    DEBUG(TEXT("[SetupTask] Enter"));

    if (Process->Privilege == CPU_PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }
    UNUSED(BaseVMA);

    Task->Arch.Stack.Size = Info->StackSize;
    Task->Arch.SystemStack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;

    Task->Arch.Stack.Base = ProcessArenaAllocateTaskStack(Process, Task->Arch.Stack.Size);

    Task->Arch.SystemStack.Base =
        AllocKernelRegion(0, Task->Arch.SystemStack.Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("SystemStack"));

    DEBUG(TEXT("[SetupTask] BaseVMA=%p, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[SetupTask] Actually got StackBase=%p"), Task->Arch.Stack.Base);

    if (Task->Arch.Stack.Base == NULL || Task->Arch.SystemStack.Base == NULL) {
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

        ERROR(TEXT("[SetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupTask] Stack (%u bytes) allocated at %p"), Task->Arch.Stack.Size, Task->Arch.Stack.Base);
    DEBUG(TEXT("[SetupTask] System stack (%u bytes) allocated at %p"), Task->Arch.SystemStack.Size,
        Task->Arch.SystemStack.Base);

    MemorySet((LPVOID)(Task->Arch.Stack.Base), 0, Task->Arch.Stack.Size);
    MemorySet((LPVOID)(Task->Arch.SystemStack.Base), 0, Task->Arch.SystemStack.Size);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    GetCR4(CR4);

    Task->Arch.Context.Registers.EAX = (UINT)Task->Parameter;
    Task->Arch.Context.Registers.EBX = (LINEAR)Task->Function;
    Task->Arch.Context.Registers.ECX = 0;
    Task->Arch.Context.Registers.EDX = 0;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = (Process->Privilege == CPU_PRIVILEGE_USER) ? SELECTOR_NULL : DataSelector;
    Task->Arch.Context.Registers.GS = (Process->Privilege == CPU_PRIVILEGE_USER) ? SELECTOR_NULL : DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.EFlags = EFLAGS_IF | EFLAGS_A1;
    Task->Arch.Context.Registers.CR3 = Process->PageDirectory;
    Task->Arch.Context.Registers.CR4 = CR4;
    Task->Arch.UserTlsDescriptorIndex = 0;
    Task->Arch.UserTlsSelector = SELECTOR_NULL;

    StackTop = Task->Arch.Stack.Base + Task->Arch.Stack.Size;
    SysStackTop = Task->Arch.SystemStack.Base + Task->Arch.SystemStack.Size;

    if (Process->Privilege == CPU_PRIVILEGE_KERNEL) {
        DEBUG(TEXT("[SetupTask] Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.EIP = (LINEAR)TaskRunner;
        Task->Arch.Context.Registers.ESP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("[SetupTask] Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.EIP = VMA_TASK_RUNNER;
        Task->Arch.Context.Registers.ESP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->SchedulerState.Status = TASK_STATUS_RUNNING;

        Kernel_x86_32.TSS->ESP0 = SysStackTop - STACK_SAFETY_MARGIN;

        BootStackTop = KernelStartup.StackTop;

        GetESP(ESP);
        UINT StackUsed = (BootStackTop - ESP) + 256;

        DEBUG(TEXT("[SetupTask] BootStackTop = %p"), BootStackTop);
        DEBUG(TEXT("[SetupTask] StackTop = %p"), StackTop);
        DEBUG(TEXT("[SetupTask] StackUsed = %u"), StackUsed);
        DEBUG(TEXT("[SetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed) == TRUE) {
            Task->Arch.Context.Registers.ESP = 0;
            GetEBP(EBP);
            Task->Arch.Context.Registers.EBP = EBP;
            DEBUG(TEXT("[SetupTask] Main task stack switched successfully"));
        } else {
            ERROR(TEXT("[SetupTask] Stack switch failed"));
        }
    }

    DEBUG(TEXT("[SetupTask] Exit"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepares architecture-specific state for the next task switch.
 *
 * Saves the current task's segment and FPU state, configures the TSS and
 * kernel stack for the next task, loads its address space and restores its
 * segment and FPU state so that SwitchToNextTask_3 can perform the generic
 * scheduling steps.
 */
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
    SAFE_USE(NextTask) {
        LINEAR NextSysStackTop = NextTask->Arch.SystemStack.Base + NextTask->Arch.SystemStack.Size;

        Kernel_x86_32.TSS->SS0 = SELECTOR_KERNEL_DATA;
        Kernel_x86_32.TSS->ESP0 = NextSysStackTop - STACK_SAFETY_MARGIN;

        SAFE_USE(CurrentTask) {
            GetFS(CurrentTask->Arch.Context.Registers.FS);
            GetGS(CurrentTask->Arch.Context.Registers.GS);
            SaveFPU((LPVOID)&(CurrentTask->Arch.Context.FPURegisters));
        }

        SetDS(NextTask->Arch.Context.Registers.DS);
        SetES(NextTask->Arch.Context.Registers.ES);
        SetFS(NextTask->Arch.Context.Registers.FS);
        SetGS(NextTask->Arch.Context.Registers.GS);

        RestoreFPU(&(NextTask->Arch.Context.FPURegisters));
    }
}

/************************************************************************/

/**
 * @brief Configure x87 control flags and clear pending exceptions.
 */
static void InitializeFPUState(void) {
    U32 Cr0Value;
    U32 Cr4Value;

    DEBUG(TEXT("[InitializeFPUState] Enter"));

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0Value));
    Cr0Value |= (CR0_COPROCESSOR | CR0_80387 | CR0_NUMERIC_ERROR);
    Cr0Value &= ~(CR0_EMULATION | CR0_TASKSWITCH);
    __asm__ volatile("mov %0, %%cr0" : : "r"(Cr0Value));

    __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4Value));
    Cr4Value |= (CR4_OSFXSR | CR4_OSXMMEXCPT);
    __asm__ volatile("mov %0, %%cr4" : : "r"(Cr4Value));

    __asm__ volatile("fninit");
    __asm__ volatile("fnclex");

    DEBUG(TEXT("[InitializeFPUState] CR0=%x CR4=%x"), Cr0Value, Cr4Value);
}

/************************************************************************/

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void PreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_x86_32.GDT = (LPSEGMENT_DESCRIPTOR)(LINEAR)Gdtr.Base;

    KernelStartup.PageDirectory = GetPageDirectory();
    KernelStartup.IRQMask_21_RM = 0;
    KernelStartup.IRQMask_A1_RM = 0;

    InitializeFPUState();
    InitializePat();
}

/***************************************************************************/

void InitializeSystemCall(void) {
    InitializeGateDescriptor(
        IDT + EXOS_USER_CALL,
        (LINEAR)Interrupt_SystemCall,
        GATE_TYPE_386_TRAP,
        CPU_PRIVILEGE_USER,
        0u);
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
