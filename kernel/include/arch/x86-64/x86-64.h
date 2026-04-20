
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


    Intel x86-64 Architecture Support

\************************************************************************/

#ifndef X86_64_H_INCLUDED
#define X86_64_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "core/Driver.h"
#include "arch/intel/x86-Common.h"
#include "arch/x86-64/x86-64-Memory.h"
#include "process/Task-Stack.h"

/***************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// Descriptor and selector helpers

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB
#define NUM_INTERRUPTS (DEVICE_INTERRUPT_VECTOR_BASE + DEVICE_INTERRUPT_VECTOR_MAX)
#define NUM_TASKS 128

#define GATE_TYPE_386_INT 0x0E
#define GATE_TYPE_386_TRAP 0x0F
#define GDT_TYPE_TSS_AVAILABLE 0x09
#define GDT_TYPE_TSS_BUSY 0x0B

#define SELECTOR_RPL_BITS 2u
#define SELECTOR_RPL_MASK 0x0003u
#define SELECTOR_RPL_SHIFT 0u

#define SELECTOR_TI_MASK 0x0001u
#define SELECTOR_TI_SHIFT 2u
#define SELECTOR_TABLE_GDT 0u
#define SELECTOR_TABLE_LDT 1u

#define SELECTOR_INDEX_SHIFT 3u

#define SELECTOR_INDEX(sel) ((U16)(sel) >> SELECTOR_INDEX_SHIFT)
#define SELECTOR_RPL(sel) ((U16)(sel) & SELECTOR_RPL_MASK)
#define SELECTOR_TI(sel) ((((U16)(sel)) >> SELECTOR_TI_SHIFT) & SELECTOR_TI_MASK)

#define MAKE_SELECTOR(index, ti, rpl) \
    ((U16)((((U16)(index)) << SELECTOR_INDEX_SHIFT) | ((((U16)(ti)) & SELECTOR_TI_MASK) << SELECTOR_TI_SHIFT) | \
            (((U16)(rpl)) & SELECTOR_RPL_MASK)))
#define MAKE_GDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_GDT, (rpl))
#define MAKE_LDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_LDT, (rpl))

#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL 0x04

#define SELECTOR_NULL 0x00
#define SELECTOR_KERNEL_CODE (0x08 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x10 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE (0x18 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
#define SELECTOR_USER_DATA (0x20 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
#define SELECTOR_REAL_CODE (0x28 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA (0x30 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
#define GDT_TSS_INDEX 7
#define SELECTOR_TSS MAKE_GDT_SELECTOR(GDT_TSS_INDEX, CPU_PRIVILEGE_KERNEL)

#define RFLAGS_ALWAYS_1 0x0000000000000002
#define RFLAGS_TF 0x0000000000000100
#define RFLAGS_IF 0x0000000000000200
#define RFLAGS_DF 0x0000000000000400
#define RFLAGS_NT 0x0000000000004000        // Nested task


/************************************************************************/
// Model Specific Registers

#define IA32_EFER_MSR 0xC0000080            // Extended Feature Enable Register 
#define IA32_STAR_MSR 0xC0000081
#define IA32_LSTAR_MSR 0xC0000082
#define IA32_FMASK_MSR 0xC0000084
#define IA32_FS_BASE_MSR 0xC0000100
#define IA32_GS_BASE_MSR 0xC0000101

#define IA32_EFER_SCE 0x1                   // SYSCALL enable
#define IA32_EFER_LME 0x100                 // IA-32e mode operation

/************************************************************************/
// PIC and IRQ helpers

#define INTERRUPT_COMMAND 0x0020
#define MAX_IRQ 16
#define IRQ_KEYBOARD 0x01
#define IRQ_MOUSE 0x04
#define IRQ_ATA 0x0E

/************************************************************************/
// Syscall register save layout

#define SYSCALL_SAVE_REGISTER_COUNT 15u
#define SYSCALL_SAVE_AREA_SIZE ((UINT)SYSCALL_SAVE_REGISTER_COUNT * sizeof(U64))

/************************************************************************/

void DebugLogSyscallFrame(LINEAR SaveArea, UINT FunctionId);

/************************************************************************/
// CMOS helpers

#define CMOS_COMMAND 0x0070
#define CMOS_DATA 0x0071
#define CMOS_SECOND 0x00
#define CMOS_ALARM_SECOND 0x01
#define CMOS_MINUTE 0x02
#define CMOS_ALARM_MINUTE 0x03
#define CMOS_HOUR 0x04
#define CMOS_ALARM_HOUR 0x05
#define CMOS_DAY_OF_WEEK 0x06
#define CMOS_DAY_OF_MONTH 0x07
#define CMOS_MONTH 0x08
#define CMOS_YEAR 0x09
#define CMOS_CENTURY 0x32

/***************************************************************************/

// PIT clock

#define CLOCK_COMMAND 0x0043
#define CLOCK_DATA 0x0040

/***************************************************************************/

// Keyboard controller

#define KEYBOARD_COMMAND 0x0064
#define KEYBOARD_DATA 0x0060
#define KSR_OUT_FULL 0x01
#define KSR_IN_FULL 0x02
#define KSR_COMMAND 0x08
#define KSR_ACTIVE 0x10
#define KSR_OUT_ERROR 0x20
#define KSR_IN_ERROR 0x40
#define KSR_PARITY_ERROR 0x80
#define KSL_SCROLL 0x01
#define KSL_NUM 0x02
#define KSL_CAPS 0x04
#define KSC_READ_MODE 0x20
#define KSC_WRITE_MODE 0x60
#define KSC_SELF_TEST 0xAA
#define KSC_ENABLE 0xAE
#define KSC_SETLEDSTATUS 0xED
#define KSS_ACK 0xFA

/***************************************************************************/

// Low memory pages reserved by VBR

#ifndef LOW_MEMORY_PAGE_1
#define LOW_MEMORY_PAGE_1 0x1000
#endif
#ifndef LOW_MEMORY_PAGE_2
#define LOW_MEMORY_PAGE_2 0x2000
#endif
#ifndef LOW_MEMORY_PAGE_3
#define LOW_MEMORY_PAGE_3 0x3000
#endif
#ifndef LOW_MEMORY_PAGE_4
#define LOW_MEMORY_PAGE_4 0x4000
#endif
#ifndef LOW_MEMORY_PAGE_5
#define LOW_MEMORY_PAGE_5 0x5000
#endif
#ifndef LOW_MEMORY_PAGE_6
#define LOW_MEMORY_PAGE_6 0x6000
#endif
#ifndef LOW_MEMORY_PAGE_7
#define LOW_MEMORY_PAGE_7 0x7000
#endif

/***************************************************************************/
// General purpose registers for 64-bit mode

typedef struct tag_INTEL_64_REGISTERS {
    U64 RFlags;
    U64 RAX, RBX, RCX, RDX;
    U64 RSI, RDI, RSP, RBP;
    U64 R8, R9, R10, R11, R12, R13, R14, R15;
    U64 RIP;
    U64 CS, DS, SS;
    U64 ES, FS, GS;
    U64 CR0, CR2, CR3, CR4, CR8;
    U64 DR0, DR1, DR2, DR3, DR6, DR7;
} INTEL_64_REGISTERS, *LPINTEL_64_REGISTERS;

/***************************************************************************/
// Segment descriptor for 64-bit mode

typedef struct tag_SEGMENT_DESCRIPTOR {
    U16 Limit_00_15;
    U16 Base_00_15;
    U8 Base_16_23;

    U8 Accessed : 1;
    U8 CanWrite : 1;
    U8 ConformExpand : 1;
    U8 Code : 1;

    U8 S : 1;
    U8 Privilege : 2;
    U8 Present : 1;

    U8 Limit_16_19 : 4;
    U8 Available : 1;
    U8 LongMode : 1;
    U8 DefaultSize : 1;
    U8 Granularity : 1;

    U8 Base_24_31;
} SEGMENT_DESCRIPTOR, *LPSEGMENT_DESCRIPTOR;

/************************************************************************/
// System segment descriptor (e.g. TSS/LDT) for 64-bit mode (16 bytes)

typedef struct tag_X86_64_SYSTEM_SEGMENT_DESCRIPTOR {
    U16 Limit_00_15;
    U16 Base_00_15;
    U8 Base_16_23;

    U8 Accessed : 1;
    U8 CanWrite : 1;
    U8 ConformExpand : 1;
    U8 Code : 1;

    U8 S : 1;
    U8 Privilege : 2;
    U8 Present : 1;

    U8 Limit_16_19 : 4;
    U8 Available : 1;
    U8 LongMode : 1;
    U8 DefaultSize : 1;
    U8 Granularity : 1;

    U8 Base_24_31;
    U32 Base_32_63;
    U32 Reserved;
} X86_64_SYSTEM_SEGMENT_DESCRIPTOR, *LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR;

/***************************************************************************/

// IDT entry layout for 64-bit mode (16 bytes)

typedef struct tag_GATE_DESCRIPTOR {
    U16 Offset_00_15;                   // Bits 0-15 of handler address
    U16 Selector;
    U16 InterruptStackTable : 3;        // IST # to use for this gate
    U16 Reserved_0 : 5;
    U16 Type : 4;                       // Type of gate (int, trap, ...)
    U16 Reserved_1 : 1;
    U16 Privilege : 2;                  // Privilege level
    U16 Present : 1;                    // Is this entry valid?
    U16 Offset_16_31;                   // Bits 16-31 of handler address
    U32 Offset_32_63;                   // Bits 32-63 of handler address
    U32 Reserved_2;
} GATE_DESCRIPTOR, *LPGATE_DESCRIPTOR;

void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler);
void InitializeGateDescriptor(
    LPGATE_DESCRIPTOR Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege,
    U8 InterruptStackTable);

/************************************************************************/

typedef struct PACKED tag_X86_64_TASK_STATE_SEGMENT {
    U32 Reserved0;
    U64 RSP0;
    U64 RSP1;
    U64 RSP2;
    U64 Reserved1;
    U64 IST1;
    U64 IST2;
    U64 IST3;
    U64 IST4;
    U64 IST5;
    U64 IST6;
    U64 IST7;
    U64 Reserved2;
    U16 Reserved3;
    U16 IOMapBase;
} X86_64_TASK_STATE_SEGMENT, *LPX86_64_TASK_STATE_SEGMENT;

/************************************************************************/

// Interrupt context saved on entry

typedef struct tag_INTERRUPT_FRAME {
    INTEL_64_REGISTERS Registers;
    INTEL_FPU_REGISTERS FPURegisters;
    U64 SS0;
    U64 RSP0;
    U32 IntNo;
    U32 ErrCode;
} INTERRUPT_FRAME, *LPINTERRUPT_FRAME;

/************************************************************************/

// Architecture-specific task data

typedef struct tag_ARCH_TASK_DATA {
    INTERRUPT_FRAME Context;
    STACK Stack;
    STACK SystemStack;
    STACK Ist1Stack;
    LINEAR UserTlsBase;
} ARCH_TASK_DATA, *LPARCH_TASK_DATA;

/************************************************************************/

typedef struct tag_GDT_REGISTER {
    U16 Limit;
    U64 Base;
} GDT_REGISTER;

/************************************************************************/

typedef U16 SELECTOR;
typedef U64 OFFSET;

typedef struct tag_KERNEL_DATA_X86_64 {
    LPGATE_DESCRIPTOR IDT;
    LPVOID GDT;
    LPX86_64_TASK_STATE_SEGMENT TSS;
} KERNEL_DATA_X86_64, *LPKERNEL_DATA_X86_64;

/***************************************************************************/
// Inline helpers

#define TRACED_FUNCTION
#define TRACED_EPILOGUE(FunctionName)

/************************************************************************/
// Context switching

#define SetupStackForKernelMode(Task, StackTop, UserESP)                                        \
    do {                                                                                        \
        LINEAR _RequiredBytes = (LINEAR)(sizeof(U64) * 5);                                      \
        (StackTop) -= _RequiredBytes;                                                           \
        ((U64*)(StackTop))[4] = (U64)((Task)->Arch.Context.Registers.SS);                       \
        ((U64*)(StackTop))[3] = (U64)(UserESP);                                                 \
        ((U64*)(StackTop))[2] = (Task)->Arch.Context.Registers.RFlags;                          \
        ((U64*)(StackTop))[1] = (U64)((Task)->Arch.Context.Registers.CS);                       \
        ((U64*)(StackTop))[0] = (U64)((Task)->Arch.Context.Registers.RIP);                      \
    } while (0)

#define SetupStackForUserMode(Task, StackTop, UserESP) \
        SetupStackForKernelMode(Task, StackTop, UserESP)

#define SwitchToNextTask_2(prev, next, next_cr3)                        \
    do {                                                                \
        U64 __target_cr3 = (next_cr3);                                  \
        __asm__ __volatile__(                                           \
            "push %%rbp\n\t"                                            \
            "push %%rax\n\t"                                            \
            "push %%rbx\n\t"                                            \
            "push %%rcx\n\t"                                            \
            "push %%rdx\n\t"                                            \
            "push %%rsi\n\t"                                            \
            "push %%rdi\n\t"                                            \
            "push %%r8\n\t"                                             \
            "push %%r9\n\t"                                             \
            "push %%r10\n\t"                                            \
            "push %%r11\n\t"                                            \
            "push %%r12\n\t"                                            \
            "push %%r13\n\t"                                            \
            "push %%r14\n\t"                                            \
            "push %%r15\n\t"                                            \
            "movq %%rsp, %0\n\t"                                        \
            "mov %2, %%cr3\n\t"                                         \
            "movq %3, %%rsp\n\t"                                        \
            "leaq 1f(%%rip), %%rax\n\t"                                 \
            "movq %%rax, %1\n\t"                                        \
            "movq %5, %%rdi\n\t"                                        \
            "movq %6, %%rsi\n\t"                                        \
            "call SwitchToNextTask_3\n\t"                               \
            "1:\n\t"                                                    \
            "pop %%r15\n\t"                                             \
            "pop %%r14\n\t"                                             \
            "pop %%r13\n\t"                                             \
            "pop %%r12\n\t"                                             \
            "pop %%r11\n\t"                                             \
            "pop %%r10\n\t"                                             \
            "pop %%r9\n\t"                                              \
            "pop %%r8\n\t"                                              \
            "pop %%rdi\n\t"                                             \
            "pop %%rsi\n\t"                                             \
            "pop %%rdx\n\t"                                             \
            "pop %%rcx\n\t"                                             \
            "pop %%rbx\n\t"                                             \
            "pop %%rax\n\t"                                             \
            "pop %%rbp\n\t"                                             \
            : "=m"((prev)->Arch.Context.Registers.RSP),                 \
              "=m"((prev)->Arch.Context.Registers.RIP)                  \
            : "r"(__target_cr3),                                        \
              "m"((next)->Arch.Context.Registers.RSP),                  \
              "m"((next)->Arch.Context.Registers.RIP),                  \
              "r"(prev),                                                \
              "r"(next)                                                 \
            : "rax", "rbx", "rsi", "rdi", "memory");                    \
    } while (0)

#define JumpToReadyTask(Task, StackTop)                                 \
    do {                                                                \
        __asm__ __volatile__(                                           \
            "finit\n\t"                                                 \
            "mov %0, %%rsi\n\t"                                         \
            "mov %1, %%rdi\n\t"                                         \
            "mov %2, %%rsp\n\t"                                         \
            "iretq"                                                     \
            :                                                           \
            : "m"((Task)->Arch.Context.Registers.RAX),                  \
              "m"((Task)->Arch.Context.Registers.RBX),                  \
              "m"(StackTop)                                             \
            : "rdi", "rsi", "memory");                                  \
    } while (0)

/************************************************************************/
// Register getters/setters

#define GetCurrentStackPointer(var) __asm__ volatile("mov %%rsp, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetCurrentFramePointer(var) __asm__ volatile("mov %%rbp, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")

#define GetCR4(var) __asm__ volatile("mov %%cr4, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetCR8(var) __asm__ volatile("mov %%cr8, %0" : "=r"(var))
#define GetESP(var) __asm__ volatile("mov %%rsp, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetEBP(var) __asm__ volatile("mov %%rbp, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetCS(var) __asm__ volatile("movw %%cs, %%ax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetDS(var) __asm__ volatile("movw %%ds, %%ax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetES(var) __asm__ volatile("movw %%es, %%ax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetFS(var) __asm__ volatile("movw %%fs, %%ax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetGS(var) __asm__ volatile("movw %%gs, %%ax; mov %%rax, %0" : "=m"(var) : : "rax")

#define SetSS(var) __asm__ volatile("mov %0, %%rax; movw %%ax, %%ss" : "=m"(var) : : "rax")
#define SetDS(var) __asm__ volatile("mov %0, %%rax; movw %%ax, %%ds" : "=m"(var) : : "rax")
#define SetES(var) __asm__ volatile("mov %0, %%rax; movw %%ax, %%es" : "=m"(var) : : "rax")
#define SetFS(var) __asm__ volatile("mov %0, %%rax; movw %%ax, %%fs" : "=m"(var) : : "rax")
#define SetGS(var) __asm__ volatile("mov %0, %%rax; movw %%ax, %%gs" : "=m"(var) : : "rax")

#define SetCR8(value) __asm__ volatile("mov %0, %%cr8" : : "r"(value) : "memory")

#define SwapGS() __asm__ volatile("swapgs" : : : "memory")

#define GetRFLAGS64(var) __asm__ volatile("pushfq; pop %0" : "=r"(var))
#define SetRFLAGS64(value) __asm__ volatile("push %0; popfq" : : "r"(value) : "memory", "cc")

#define GetDR0(var) __asm__ volatile("mov %%dr0, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetDR6(var) __asm__ volatile("mov %%dr6, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")
#define GetDR7(var) __asm__ volatile("mov %%dr7, %%rax; mov %%rax, %0" : "=m"(var) : : "rax")

#define SetDR6(var) __asm__ volatile("mov %0, %%rax; mov %%rax, %%dr6" : : "r"(var) : "eax")
#define SetDR7(var) __asm__ volatile("mov %0, %%rax; mov %%rax, %%dr7" : : "r"(var) : "eax")

#define ClearDR6() __asm__ volatile("xor %%rax, %%rax; mov %%rax, %%dr6" : : : "eax")
#define ClearDR7() __asm__ volatile("xor %%rax, %%rax; mov %%rax, %%dr7" : : : "eax")

#define DisableInterrupts() __asm__ __volatile__("cli" : : : "memory")
#define EnableInterrupts() __asm__ __volatile__("sti" : : : "memory")

#define SaveFlags(Flags)                                                                                \
    do {                                                                                                \
        UINT Value;                                                                                     \
                                                                                                        \
        __asm__ __volatile__(                                                                           \
            "pushfq\n\t"                                                                                \
            "pop %0"                                                                                    \
            : "=r"(Value)                                                                               \
            :                                                                                           \
            : "memory");                                                                               \
                                                                                                        \
        *(Flags) = Value;                                                                               \
    } while (0)

#define RestoreFlags(Flags)                                                                             \
    do {                                                                                                \
        U64 Value = (U64)(*(Flags));                                                                    \
                                                                                                        \
        __asm__ __volatile__(                                                                           \
            "push %0\n\t"                                                                              \
            "popfq"                                                                                    \
            :                                                                                           \
            : "r"(Value)                                                                               \
            : "memory", "cc");                                                                         \
    } while (0)

/***************************************************************************/
// Inline helpers

static inline U32 LoadInterruptDescriptorTable(PHYSICAL Base, U32 Limit)
{
    struct PACKED
    {
        U16 Limit;
        PHYSICAL Base;
    } Descriptor;
    U64 Flags;

    Descriptor.Limit = (U16)Limit;
    Descriptor.Base = Base;

    __asm__ __volatile__(
        "pushfq\n\t"
        "pop %0\n\t"
        "cli\n\t"
        "lidt %1\n\t"
        "push %0\n\t"
        "popfq"
        : "=&r"(Flags)
        : "m"(Descriptor)
        : "memory");

    return (U32)Base;
}

static inline U32 LoadInitialTaskRegister(U32 TaskRegister)
{
    U16 Selector = (U16)TaskRegister;
    U64 Flags;

    __asm__ __volatile__("ltr %0" : : "m"(Selector) : "memory");

    __asm__ __volatile__(
        "pushfq\n\t"
        "pop %0"
        : "=r"(Flags)
        :
        : "memory");

    Flags &= ~((U64)RFLAGS_NT);

    __asm__ __volatile__(
        "push %0\n\t"
        "popfq"
        :
        : "r"(Flags)
        : "memory", "cc");

    __asm__ __volatile__("clts" : : : "memory");

    return (U32)Flags;
}

static inline U32 LoadPageDirectory(PHYSICAL Base)
{
    PHYSICAL Current;

    __asm__ __volatile__("mov %%cr3, %0" : "=r"(Current));

    if (Current != Base)
    {
        __asm__ __volatile__("mov %0, %%cr3" : : "r"(Base) : "memory");
    }

    return (U32)Base;
}

/***************************************************************************/

extern KERNEL_DATA_X86_64 Kernel_x86_32;

struct tag_TASK;
struct tag_PROCESS;
struct tag_TASK_INFO;

BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASK_INFO* Info);
void PreInitializeKernel(void);
void InitializeSystemCall(void);
void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table);
void InitializeTaskSegments(void);
void SetSystemSegmentDescriptorLimit(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U32 Limit);
void SetSystemSegmentDescriptorBase(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U64 Base);
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask);

/************************************************************************/

#pragma pack(pop)

#endif  // X86_64_H_INCLUDED
