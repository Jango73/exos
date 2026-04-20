
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

#ifndef X86_32_H_INCLUDED
#define X86_32_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "arch/intel/x86-Common.h"
#include "arch/x86-32/x86-32-Memory.h"
#include "process/Task-Stack.h"

/************************************************************************/
// #define declarations

/* Segment descriptor attributes */
#define GDT_TYPE_DATA 0x00
#define GDT_TYPE_CODE 0x01
#define GDT_PRIVILEGE_KERNEL 0x00
#define GDT_PRIVILEGE_DRIVERS 0x01
#define GDT_PRIVILEGE_ROUTINES 0x02
#define GDT_PRIVILEGE_USER 0x03
#define GDT_OPERANDSIZE_16 0x00
#define GDT_OPERANDSIZE_32 0x01
#define GDT_GRANULAR_1B 0x00
#define GDT_GRANULAR_4KB 0x01

#define SEGMENTBASE(PSD)                                                                                      \
    (NULL32 | ((((U32)(PSD)->Base_00_15) & 0xFFFF) << 0x00) | ((((U32)(PSD)->Base_16_23) & 0x00FF) << 0x10) | \
     ((((U32)(PSD)->Base_24_31) & 0x00FF) << 0x18))

#define SEGMENTGRANULAR(PSD) (((PSD)->Granularity == 0x00) ? N_1B : N_4KB)

#define SEGMENTLIMIT(PSD) \
    (NULL32 | ((((U32)(PSD)->Limit_00_15) & 0xFFFF) << 0x00) | ((((U32)(PSD)->Limit_16_19) & 0x000F) << 0x10))

/* Gate descriptor and TSS descriptor attributes */
#define GATE_TYPE_286_TSS_AVAIL 0x01
#define GATE_TYPE_LDT 0x02
#define GATE_TYPE_286_TSS_BUSY 0x03
#define GATE_TYPE_CALL 0x04
#define GATE_TYPE_TASK 0x05
#define GATE_TYPE_286_INT 0x06
#define GATE_TYPE_286_TRAP 0x07
#define GATE_TYPE_386_TSS_AVAIL 0x09
#define GATE_TYPE_386_TSS_BUSY 0x0B
#define GATE_TYPE_386_CALL 0x0C
#define GATE_TYPE_386_INT 0x0E   // This clears interrupt flag on entry
#define GATE_TYPE_386_TRAP 0x0F  // This DOES NOT clear interrupt flag on entry

/* Selector bitfield layout (x86) */
#define SELECTOR_RPL_BITS 2u
#define SELECTOR_RPL_MASK 0x0003u
#define SELECTOR_RPL_SHIFT 0u

#define SELECTOR_TI_MASK 0x0001u
#define SELECTOR_TI_SHIFT 2u
#define SELECTOR_TABLE_GDT 0u
#define SELECTOR_TABLE_LDT 1u

#define SELECTOR_INDEX_SHIFT 3u

#define SELECTOR_INDEX(sel) ((U16)(sel) >> SELECTOR_INDEX_SHIFT)
#define SELECTOR_RPL(sel) ((U16)(sel)&SELECTOR_RPL_MASK)
#define SELECTOR_TI(sel) ((((U16)(sel)) >> SELECTOR_TI_SHIFT) & SELECTOR_TI_MASK)

#define MAKE_SELECTOR(index, ti, rpl) \
    ((SELECTOR)((((U16)(index)) << SELECTOR_INDEX_SHIFT) | ((((U16)(ti)) & SELECTOR_TI_MASK) << SELECTOR_TI_SHIFT) | (((U16)(rpl)) & SELECTOR_RPL_MASK)))
#define MAKE_GDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_GDT, (rpl))
#define MAKE_LDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_LDT, (rpl))

/* Canonical selector values (x86-32 layout) */
#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL 0x04

#define SELECTOR_NULL 0x00
#define SELECTOR_KERNEL_CODE (0x08 | SELECTOR_GLOBAL | GDT_PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x10 | SELECTOR_GLOBAL | GDT_PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE (0x18 | SELECTOR_GLOBAL | GDT_PRIVILEGE_USER)
#define SELECTOR_USER_DATA (0x20 | SELECTOR_GLOBAL | GDT_PRIVILEGE_USER)
#define SELECTOR_REAL_CODE (0x28 | SELECTOR_GLOBAL | GDT_PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA (0x30 | SELECTOR_GLOBAL | GDT_PRIVILEGE_KERNEL)

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB

#define DESCRIPTOR_SIZE 10
#define GDT_NUM_DESCRIPTORS (GDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_BASE_DESCRIPTORS 8
#define GDT_TSS_INDEX GDT_NUM_BASE_DESCRIPTORS
#define SELECTOR_TSS MAKE_GDT_SELECTOR(GDT_TSS_INDEX, GDT_PRIVILEGE_KERNEL)

#define GDT_NUM_TASKS (GDT_NUM_DESCRIPTORS - GDT_NUM_BASE_DESCRIPTORS)
#define NUM_TASKS GDT_NUM_TASKS

#define NUM_INTERRUPTS (DEVICE_INTERRUPT_VECTOR_BASE + DEVICE_INTERRUPT_VECTOR_MAX)

#define STACK_TRACE_WARNING 256

/* Exception and interrupt numbers */
#define INT_DIVIDE 0
#define INT_DEBUG 1
#define INT_NMI 2
#define INT_BREAKPOINT 3
#define INT_OVERFLOW 4
#define INT_BOUNDS 5
#define INT_OPCODE 6
#define INT_MATHGONE 7
#define INT_DOUBLE 8
#define INT_MATHOVER 9
#define INT_TSS 10
#define INT_SEGMENT 11
#define INT_STACK 12
#define INT_GENERAL 13
#define INT_PAGE 14
#define INT_RESERVED15 15
#define INT_MATHERR 16
#define INT_RESERVED17 17
#define INT_RESERVED18 18
#define INT_RESERVED19 19
#define INT_RESERVED20 20
#define INT_RESERVED21 21
#define INT_RESERVED22 22
#define INT_RESERVED23 23
#define INT_RESERVED24 24
#define INT_RESERVED25 25
#define INT_RESERVED26 26
#define INT_RESERVED27 27
#define INT_RESERVED28 28
#define INT_RESERVED29 29
#define INT_RESERVED30 30
#define INT_RESERVED31 31
#define INT_KERNELCLOCK 32
#define INT_KEYBOARD 33
#define INT_UNUSED34 34
#define INT_UNUSED35 35
#define INT_UNUSED36 36
#define INT_UNUSED37 37
#define INT_UNUSED38 38
#define INT_UNUSED39 39
#define INT_UNUSED40 40
#define INT_UNUSED41 41
#define INT_UNUSED42 42
#define INT_UNUSED43 43
#define INT_UNUSED44 44
#define INT_UNUSED45 45
#define INT_UNUSED46 46
#define INT_UNUSED47 47

/* Bit layout of the EFlags register */
#define EFLAGS_CF 0x00000001  // Carry flag
#define EFLAGS_A1 0x00000002  // Always 1
#define EFLAGS_PF 0x00000004  // Parity flag
#define EFLAGS_RES1 0x00000008
#define EFLAGS_AF 0x00000010
#define EFLAGS_RES2 0x00000020
#define EFLAGS_ZF 0x00000040  // Zero flag
#define EFLAGS_SF 0x00000080  // Sign flag
#define EFLAGS_TF 0x00000100  // Trap flag
#define EFLAGS_IF 0x00000200  // Interrupt flag
#define EFLAGS_RES3 0x00000400
#define EFLAGS_OF 0x00000800     // Overflow flag
#define EFLAGS_IOPL1 0x00001000  // IO privilege level bit 1
#define EFLAGS_IOPL2 0x00002000  // IO privilege level bit 2
#define EFLAGS_NT 0x00004000     // Nested task
#define EFLAGS_RES4 0x00008000
#define EFLAGS_RF 0x00010000  // Resume flag
#define EFLAGS_VM 0x00020000  // Virtual 8086 mode
#define EFLAGS_RES5 0x00040000
#define EFLAGS_RES6 0x00080000
#define EFLAGS_RES7 0x00100000
#define EFLAGS_RES8 0x00200000
#define EFLAGS_RES9 0x00400000
#define EFLAGS_RES10 0x00800000
#define EFLAGS_RES11 0x01000000
#define EFLAGS_RES12 0x02000000
#define EFLAGS_RES13 0x04000000
#define EFLAGS_RES14 0x08000000
#define EFLAGS_RES15 0x10000000
#define EFLAGS_RES16 0x20000000
#define EFLAGS_RES17 0x40000000
#define EFLAGS_RES18 0x80000000

/* PIC and IRQ helpers */
#define INTERRUPT_COMMAND 0x0020
#define MAX_IRQ 16
#define IRQ_KEYBOARD 0x01
#define IRQ_MOUSE 0x04
#define IRQ_ATA 0x0E

/* CMOS helpers */
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

/* PIT clock */
#define CLOCK_COMMAND 0x0043
#define CLOCK_DATA 0x0040

/* Keyboard controller */
#define KEYBOARD_COMMAND 0x0064  // Keyboard command port
#define KEYBOARD_DATA 0x0060     // Keyboard data port
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

/* Low memory pages reserved by VBR */
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

/************************************************************************/
// Typedef declarations

#pragma pack(push, 1)

typedef struct tag_INTEL_32_REGISTERS {
    U32 EFlags;
    U32 EAX, EBX, ECX, EDX;
    U32 ESI, EDI, ESP, EBP;
    U32 EIP;
    U32 CS, DS, SS;
    U32 ES, FS, GS;
    U32 CR0, CR2, CR3, CR4;
    U32 DR0, DR1, DR2, DR3;
    U32 DR4, DR5, DR6, DR7;
} INTEL_32_REGISTERS, *LPINTEL_32_REGISTERS;

// The segment descriptor
// Size : 8 bytes

typedef struct tag_SEGMENT_DESCRIPTOR {
    U32 Limit_00_15 : 16;   // Bits 0-15 of segment limit
    U32 Base_00_15 : 16;    // Bits 0-15 of segment base
    U32 Base_16_23 : 8;     // Bits 16-23 of segment base
    U32 Accessed : 1;       // Segment has been accessed since OS cleared this flag
    U32 CanWrite : 1;       // Read-only for data segments, Exec-Only for code
                            // segments
    U32 ConformExpand : 1;  // Conforming for code segments, expand-down for
                            // data segments
    U32 Type : 1;           // Data = 0, code = 1
    U32 Segment : 1;        //
    U32 Privilege : 2;      // Privilege level, 0-3
    U32 Present : 1;        //
    U32 Limit_16_19 : 4;    // Bits 16-19 of segment limit
    U32 Available : 1;      //
    U32 Unused : 1;         // Reserved
    U32 OperandSize : 1;    // 0 = 16-bit, 1 = 32-bit
    U32 Granularity : 1;    // 0 = 1 byte granular, 1 = 4096 bytes granular
    U32 Base_24_31 : 8;     // Bits 24-31 of segment base
} SEGMENT_DESCRIPTOR, *LPSEGMENT_DESCRIPTOR;

// The Gate Descriptor

typedef struct tag_GATE_DESCRIPTOR {
    U32 Offset_00_15 : 16;  // Bits 0-15 of entry point offset
    U32 Selector : 16;      // Selector for code segment
    U32 Reserved : 8;       // Reserved
    U32 Type : 5;           // Type
    U32 Privilege : 2;      // Privilege level
    U32 Present : 1;        //
    U32 Offset_16_31 : 16;  // Bits 16-31 of entry point offset
} GATE_DESCRIPTOR, *LPGATE_DESCRIPTOR;

void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler);
void InitializeGateDescriptor(
    LPGATE_DESCRIPTOR Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege,
    U8 InterruptStackTable);

// The TSS descriptor

typedef struct tag_TSS_DESCRIPTOR {
    U32 Limit_00_15 : 16;  // Bits 0-15 of segment limit
    U32 Base_00_15 : 16;   // Bits 0-15 of segment base
    U32 Base_16_23 : 8;    // Bits 16-23 of segment base
    U32 Type : 5;          // Must be GATE_TYPE_386_TSS_xxxx
    U32 Privilege : 2;     // Privilege level
    U32 Present : 1;       //
    U32 Limit_16_19 : 4;   // Bits 16-19 of segment limit
    U32 Available : 1;     //
    U32 Unused : 2;        // Reserved
    U32 Granularity : 1;   // 0 = 1 byte granular, 1 = 4096 bytes granular
    U32 Base_24_31 : 8;    // Bits 24-31 of segment base
} TSS_DESCRIPTOR, *LPTSS_DESCRIPTOR;

// The Task State Segment
// It must be 256 bytes long

typedef struct tag_TASK_STATE_SEGMENT {
    U16 BackLink;  // TSS backlink
    U16 Res1;      // Reserved
    U32 ESP0;      // Stack 0 pointer (CPL = 0)
    U16 SS0;       // Stack 0 segment
    U16 Res2;      // Reserved
    U32 ESP1;      // Stack 1 pointer (CPL = 1)
    U16 SS1;       // Stack 1 segment
    U16 Res3;      // Reserved
    U32 ESP2;      // Stack 2 pointer (CPL = 2)
    U16 SS2;       // Stack 2 segment
    U16 Res4;      // Reserved
    U32 CR3;       // Control register 3
    U32 EIP;       // Instruction pointer
    U32 EFlags;    // Extended flags
    U32 EAX;       // EAX general purpose register
    U32 ECX;       // ECX general purpose register
    U32 EDX;       // EDX general purpose register
    U32 EBX;       // EBX general purpose register
    U32 ESP;       // Stack pointer
    U32 EBP;       // Alternate stack pointer
    U32 ESI;       // ESI general purpose register
    U32 EDI;       // EDI general purpose register

    U16 ES;    // ES segment register (Extra segment)
    U16 Res5;  // Reserved

    U16 CS;    // CS segment register (Code segment)
    U16 Res6;  // Reserved

    U16 SS;    // SS segment register (Stack segment)
    U16 Res7;  // Reserved

    U16 DS;    // DS segment register (Data segment)
    U16 Res8;  // Reserved

    U16 FS;    // FS segment register (Extra segment)
    U16 Res9;  // Reserved

    U16 GS;     // GS segment register (Extra segment)
    U16 Res10;  // Reserved

    U16 LDT;    // Local descriptor table segment selector
    U16 Res11;  // Reserved

    U8 Trap;
    U8 Res12;

    U16 IOMap;          // I/O Map Base Address
    U8 IOMapBits[152];  // Map 1024 port adresses
} TASK_STATE_SEGMENT, *LPTASK_STATE_SEGMENT;

// NOTE: fields not meaningful for a given trap are set to 0
// !! Structure MUST BE IDENTICAL to STRUC INTERRUPT_FRAME in x86-32.inc !!

typedef struct tag_INTERRUPT_FRAME {
    INTEL_32_REGISTERS Registers;
    INTEL_FPU_REGISTERS FPURegisters;
    U32 SS0;      // SS in ring 0
    U32 ESP0;     // ESP in ring 0
    U32 IntNo;    // Interrupt / exception vector
    U32 ErrCode;  // CPU error code (0 for #UD)
} INTERRUPT_FRAME, *LPINTERRUPT_FRAME;

typedef struct tag_ARCH_TASK_DATA {
    INTERRUPT_FRAME Context;
    STACK Stack;
    STACK SystemStack;
    UINT UserTlsDescriptorIndex;
    U16 UserTlsSelector;
} ARCH_TASK_DATA, *LPARCH_TASK_DATA;

// The GDT register

typedef struct {
    U16 Limit;
    U32 Base;
} GDT_REGISTER;

// Architecture-specific kernel data exposed to assembly and C

typedef struct tag_KERNEL_DATA_X86_32 {
    LPGATE_DESCRIPTOR IDT;
    LPSEGMENT_DESCRIPTOR GDT;
    LPTASK_STATE_SEGMENT TSS;
} KERNEL_DATA_X86_32, *LPKERNEL_DATA_X86_32;

#pragma pack(pop)

typedef U16 SELECTOR;
typedef U32 OFFSET;

typedef struct tag_FAR_POINTER {
    OFFSET Offset;
    SELECTOR Selector;
} FAR_POINTER, *LPFAR_POINTER;

// Structure to receive information about a segment in a more friendly way
typedef struct tag_SEGMENT_INFO {
    U32 Base;
    U32 Limit;
    U32 Type;
    U32 Privilege;
    U32 Granularity;
    U32 CanWrite;
    U32 OperandSize;
    U32 Conforming;
    U32 Present;
} SEGMENT_INFO, *LPSEGMENT_INFO;

/************************************************************************/
// Inline helpers

#if TRACE_STACK_USAGE == 1
#define TRACED_FUNCTION                                                                                           \
    LINEAR __StackTraceStart;                                                                                     \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__StackTraceStart) : :)
#else
#define TRACED_FUNCTION
#endif  // TRACE_STACK_USAGE == 1

#if TRACE_STACK_USAGE == 1
#if SCHEDULING_DEBUG_OUTPUT == 1
#define TRACED_EPILOGUE(FunctionName)                                                                              \
    LINEAR __StackTraceEnd;                                                                                        \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__StackTraceEnd) : :);                                        \
    LINEAR __StackTraceUsed = __StackTraceStart - __StackTraceEnd;                                                 \
    DEBUG(TEXT("ESP in " #FunctionName " = %x"), __StackTraceEnd);                                                  \
    if (__StackTraceUsed > STACK_TRACE_WARNING) {                                                                  \
        WARNING(TEXT("Stack usage exceeds limit (%x) in " #FunctionName), __StackTraceUsed);                       \
    }
#else
#define TRACED_EPILOGUE(FunctionName)                                                                              \
    LINEAR __StackTraceEnd;                                                                                        \
    __asm__ __volatile__("movl %%esp, %0\n\t" : "=r"(__StackTraceEnd) : :);                                        \
    LINEAR __StackTraceUsed = __StackTraceStart - __StackTraceEnd;                                                 \
    if (__StackTraceUsed > STACK_TRACE_WARNING) {                                                                  \
        WARNING(TEXT("Stack usage exceeds limit (%x) in " #FunctionName), __StackTraceUsed);                       \
    }
#endif  // TRACE_STACK_USAGE == 1
#else
#define TRACED_EPILOGUE(FunctionName)
#endif

/************************************************************************/
// Context switching

#define SetupStackForKernelMode(Task, StackTop, UserESP)            \
    (StackTop) -= (sizeof(U32) * 3);                                \
    ((U32*)(StackTop))[2] = (Task)->Arch.Context.Registers.EFlags;  \
    ((U32*)(StackTop))[1] = (Task)->Arch.Context.Registers.CS;      \
    ((U32*)(StackTop))[0] = (Task)->Arch.Context.Registers.EIP;

#define SetupStackForUserMode(Task, StackTop, UserESP)              \
    (StackTop) -= (sizeof(U32) * 5);                                \
    ((U32*)(StackTop))[4] = (Task)->Arch.Context.Registers.SS;      \
    ((U32*)(StackTop))[3] = (UserESP);                              \
    ((U32*)(StackTop))[2] = (Task)->Arch.Context.Registers.EFlags;  \
    ((U32*)(StackTop))[1] = (Task)->Arch.Context.Registers.CS;      \
    ((U32*)(StackTop))[0] = (Task)->Arch.Context.Registers.EIP;

#define SwitchToNextTask_2(prev, next, next_cr3)                                      \
    do {                                                                              \
        PHYSICAL __target_cr3 = (next_cr3);                                           \
        __asm__ __volatile__(                                                         \
            "pusha\n\t"                                                               \
            "movl %%esp, %0\n\t"                                                      \
            "movl $1f, %1\n\t"                                                        \
            "movl %4, %%cr3\n\t"                                                      \
            "movl %2, %%esp\n\t"                                                      \
            "pushl %5\n\t"                                                            \
            "pushl %3\n\t"                                                            \
            "call SwitchToNextTask_3\n\t"                                             \
            "1:\n\t"                                                                  \
            "add $8, %%esp\n\t"                                                       \
            "popa\n\t"                                                                \
            : "=m"((prev)->Arch.Context.Registers.ESP),                               \
              "=m"((prev)->Arch.Context.Registers.EIP)                                \
            : "m"((next)->Arch.Context.Registers.ESP),                                \
              "r"(prev),                                                              \
              "r"(__target_cr3),                                                      \
              "r"(next)                                                               \
            : "memory", "cc");                                                        \
    } while (0)

#define JumpToReadyTask(Task, StackPointer)                         \
    do {                                                            \
        __asm__ __volatile__(                                       \
            "finit\n\t"                                             \
            "mov %0, %%eax\n\t"                                     \
            "mov %1, %%ebx\n\t"                                     \
            "mov %2, %%esp\n\t"                                     \
            "iret"                                                  \
            :                                                       \
            : "m"((Task)->Arch.Context.Registers.EAX),              \
              "m"((Task)->Arch.Context.Registers.EBX),              \
              "m"(StackPointer)                                     \
            : "eax", "ebx", "memory");                              \
    } while (0)

/************************************************************************/
// Register getters/setters

#define GetCurrentStackPointer(var) __asm__ volatile("mov %%esp, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetCurrentFramePointer(var) __asm__ volatile("mov %%ebp, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")

#define GetCR4(var) __asm__ volatile("mov %%cr4, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetESP(var) __asm__ volatile("mov %%esp, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetEBP(var) __asm__ volatile("mov %%ebp, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetCS(var) __asm__ volatile("movw %%cs, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetDS(var) __asm__ volatile("movw %%ds, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetES(var) __asm__ volatile("movw %%es, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetFS(var) __asm__ volatile("movw %%fs, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")
#define GetGS(var) __asm__ volatile("movw %%gs, %%ax; movl %%eax, %0" : "=m"(var) : : "eax")

#define SetDS(var) __asm__ volatile("movl %0, %%eax; movw %%ax, %%ds" : "=m"(var) : : "eax")
#define SetES(var) __asm__ volatile("movl %0, %%eax; movw %%ax, %%es" : "=m"(var) : : "eax")
#define SetFS(var) __asm__ volatile("movl %0, %%eax; movw %%ax, %%fs" : "=m"(var) : : "eax")
#define SetGS(var) __asm__ volatile("movl %0, %%eax; movw %%ax, %%gs" : "=m"(var) : : "eax")

#define GetDR0(var) __asm__ volatile("mov %%dr0, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetDR6(var) __asm__ volatile("mov %%dr6, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")
#define GetDR7(var) __asm__ volatile("mov %%dr7, %%eax; mov %%eax, %0" : "=m"(var) : : "eax")

#define SetDR6(var) __asm__ volatile("mov %0, %%eax; mov %%eax, %%dr6" : : "r"(var) : "eax")
#define SetDR7(var) __asm__ volatile("mov %0, %%eax; mov %%eax, %%dr7" : : "r"(var) : "eax")

#define ClearDR6() __asm__ volatile("xor %%eax, %%eax; mov %%eax, %%dr6" : : : "eax")
#define ClearDR7() __asm__ volatile("xor %%eax, %%eax; mov %%eax, %%dr7" : : : "eax")

#define DisableInterrupts() __asm__ __volatile__("cli" : : : "memory")
#define EnableInterrupts() __asm__ __volatile__("sti" : : : "memory")

#define SaveFlags(Flags)                             \
    do {                                             \
        UINT Value;                                  \
                                                     \
        __asm__ __volatile__(                        \
            "pushfl\n\t"                             \
            "pop %0"                                 \
            : "=r"(Value)                            \
            :                                        \
            : "memory");                             \
                                                     \
        *(Flags) = Value;                            \
    } while (0)

#define RestoreFlags(Flags)                          \
    do {                                             \
        UINT Value = *(Flags);                       \
                                                     \
        __asm__ __volatile__(                        \
            "push %0\n\t"                            \
            "popfl"                                  \
            :                                        \
            : "r"(Value)                             \
            : "memory", "cc");                       \
    } while (0)

#define SET_HW_BREAKPOINT(addr)                      \
    __asm__ volatile(                                \
        "mov %0, %%eax; mov %%eax, %%dr0\n"          \
        "mov $0x00000001, %%eax; mov %%eax, %%dr7\n" \
        :                                            \
        : "r"(addr)                                  \
        : "eax")

/************************************************************************/
// Inline helpers

static inline U32 LoadInterruptDescriptorTable(PHYSICAL Base, U32 Limit)
{
    struct PACKED
    {
        U16 Limit;
        PHYSICAL Base;
    } Descriptor;
    U32 Flags;

    Descriptor.Limit = (U16)Limit;
    Descriptor.Base = Base;

    __asm__ __volatile__(
        "pushfl\n\t"
        "pop %0\n\t"
        "cli\n\t"
        "lidt %1\n\t"
        "push %0\n\t"
        "popfl"
        : "=&r"(Flags)
        : "m"(Descriptor)
        : "memory");

    return (U32)Base;
}

static inline U32 LoadInitialTaskRegister(U32 TaskRegister)
{
    U16 Selector = (U16)TaskRegister;
    U32 Flags;

    __asm__ __volatile__("ltr %0" : : "m"(Selector) : "memory");

    __asm__ __volatile__(
        "pushfl\n\t"
        "pop %0"
        : "=r"(Flags)
        :
        : "memory");

    Flags &= (U32)(~EFLAGS_NT);

    __asm__ __volatile__(
        "push %0\n\t"
        "popfl"
        :
        : "r"(Flags)
        : "memory", "cc");

    __asm__ __volatile__("clts" : : : "memory");

    return Flags;
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

/************************************************************************/
// External symbols

extern KERNEL_DATA_X86_32 Kernel_x86_32;

BOOL GetSegmentInfo(LPSEGMENT_DESCRIPTOR This, LPSEGMENT_INFO Info);
BOOL SegmentInfoToString(LPSEGMENT_INFO This, LPSTR Text);

void InitSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, U32 Type);
void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table);
void InitializeTaskSegments(void);
void SetSegmentDescriptorBase(LPSEGMENT_DESCRIPTOR Desc, U32 Base);
void SetSegmentDescriptorLimit(LPSEGMENT_DESCRIPTOR Desc, U32 Limit);
void SetTSSDescriptorBase(LPTSS_DESCRIPTOR Desc, U32 Base);
void SetTSSDescriptorLimit(LPTSS_DESCRIPTOR Desc, U32 Limit);
void PreInitializeKernel(void);
void InitializeSystemCall(void);

struct tag_TASK;
struct tag_PROCESS;
struct tag_TASK_INFO;

BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASK_INFO* Info);
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask);

/***************************************************************************/

#endif  // X86_32_H_INCLUDED
