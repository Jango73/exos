
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


    Interrupt Frame Management for x86-64

\************************************************************************/

/************************************************************************\

    Intel® 64 and IA-32 Architectures Software Developer’s Manual :

    In 64-bit mode, interrupt and exception handling is similar to what has been described for non-64-bit modes. The
    following are the exceptions:

    - All interrupt handlers pointed by the IDT are in 64-bit code (this does not apply to the SMI handler).
    - The size of interrupt-stack pushes is fixed at 64 bits; and the processor uses 8-byte, zero extended stores.
    - The stack pointer (SS:RSP) is pushed unconditionally on interrupts. In legacy modes, this push is conditional
    and based on a change in current privilege level (CPL).
    - The new SS is set to NULL if there is a change in CPL.
    - IRET behavior changes.
    - There is a new interrupt stack-switch mechanism and a new interrupt shadow stack-switch mechanism.
    - The alignment of interrupt stack frame is different.

\************************************************************************/

#include "arch/InterruptFrame.h"

#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "text/Text.h"

/************************************************************************/

#define INCOMING_SS_INDEX 0
#define INCOMING_C_RBP_INDEX 1
#define INCOMING_GS_INDEX 2
#define INCOMING_FS_INDEX 3
#define INCOMING_ES_INDEX 4
#define INCOMING_DS_INDEX 5
#define INCOMING_R15_INDEX 6
#define INCOMING_R14_INDEX 7
#define INCOMING_R13_INDEX 8
#define INCOMING_R12_INDEX 9
#define INCOMING_R11_INDEX 10
#define INCOMING_R10_INDEX 11
#define INCOMING_R9_INDEX 12
#define INCOMING_R8_INDEX 13
#define INCOMING_RDI_INDEX 14
#define INCOMING_RSI_INDEX 15
#define INCOMING_RBP_INDEX 16
#define INCOMING_RSP_INDEX 17
#define INCOMING_RBX_INDEX 18
#define INCOMING_RDX_INDEX 19
#define INCOMING_RCX_INDEX 20
#define INCOMING_RAX_INDEX 21
#define INCOMING_ERROR_CODE_INDEX 22  // If present, following indexes shift by HasErrorCode
#define INCOMING_RIP_INDEX 22
#define INCOMING_CS_INDEX 23
#define INCOMING_RFLAGS_INDEX 24
#define INCOMING_USER_RSP_INDEX 25
#define INCOMING_USER_SS_INDEX 26

/************************************************************************/

LPINTERRUPT_FRAME BuildInterruptFrame(U32 InterruptNumber, U32 HasErrorCode, UINT StackPointer) {
    LPINTERRUPT_FRAME Frame;
    U64* Stack;
    U32 UserMode;

    if (HasErrorCode > 1) HasErrorCode = 1;

    Frame = (LPINTERRUPT_FRAME)StackPointer;
    Stack = (U64*)(StackPointer + sizeof(INTERRUPT_FRAME));

    if (IsValidMemory((LINEAR)Stack) == FALSE) {
        DEBUG(TEXT("Invalid stack computed : %p"), (LINEAR)Stack);
        DO_THE_SLEEPING_BEAUTY;
    }

    MemorySet(Frame, 0, sizeof(INTERRUPT_FRAME));

    UserMode = (Stack[INCOMING_CS_INDEX + HasErrorCode] & SELECTOR_RPL_MASK) != 0;
    UNUSED(UserMode);

    Frame->Registers.SS = (U16)(Stack[INCOMING_USER_SS_INDEX + HasErrorCode] & MAX_U16);
    Frame->Registers.RSP = Stack[INCOMING_USER_RSP_INDEX + HasErrorCode];
    Frame->Registers.RFlags = Stack[INCOMING_RFLAGS_INDEX + HasErrorCode];
    Frame->Registers.RIP = Stack[INCOMING_RIP_INDEX + HasErrorCode];
    Frame->Registers.CS = (U16)(Stack[INCOMING_CS_INDEX + HasErrorCode] & MAX_U16);

    FINE_DEBUG(TEXT("FRAME BUILD DEBUG - intNo=%d HasErrorCode=%d UserMode=%d"),
        InterruptNumber, HasErrorCode, UserMode);
    // FINE_DEBUG(TEXT("Stack at %p:"), (LINEAR)Stack);
    // if (SCHEDULING_DEBUG_OUTPUT == 1) {
        // KernelLogMem(LOG_DEBUG, (LINEAR)Stack, 256);
    // }
    FINE_DEBUG(TEXT("Extracted: RIP=%p CS=%x RFLAGS=%x"), (LINEAR)Frame->Registers.RIP,
        Frame->Registers.CS, Frame->Registers.RFlags);

    Frame->Registers.RAX = Stack[INCOMING_RAX_INDEX];
    Frame->Registers.RBX = Stack[INCOMING_RBX_INDEX];
    Frame->Registers.RCX = Stack[INCOMING_RCX_INDEX];
    Frame->Registers.RDX = Stack[INCOMING_RDX_INDEX];
    Frame->Registers.RSI = Stack[INCOMING_RSI_INDEX];
    Frame->Registers.RDI = Stack[INCOMING_RDI_INDEX];
    Frame->Registers.RBP = Stack[INCOMING_RBP_INDEX];
    Frame->Registers.RSP = Stack[INCOMING_RSP_INDEX];
    Frame->Registers.R8 = Stack[INCOMING_R8_INDEX];
    Frame->Registers.R9 = Stack[INCOMING_R9_INDEX];
    Frame->Registers.R10 = Stack[INCOMING_R10_INDEX];
    Frame->Registers.R11 = Stack[INCOMING_R11_INDEX];
    Frame->Registers.R12 = Stack[INCOMING_R12_INDEX];
    Frame->Registers.R13 = Stack[INCOMING_R13_INDEX];
    Frame->Registers.R14 = Stack[INCOMING_R14_INDEX];
    Frame->Registers.R15 = Stack[INCOMING_R15_INDEX];

    Frame->Registers.DS = (U16)(Stack[INCOMING_DS_INDEX] & MAX_U16);
    Frame->Registers.ES = (U16)(Stack[INCOMING_ES_INDEX] & MAX_U16);
    Frame->Registers.FS = (U16)(Stack[INCOMING_FS_INDEX] & MAX_U16);
    Frame->Registers.GS = (U16)(Stack[INCOMING_GS_INDEX] & MAX_U16);
    Frame->Registers.SS = (U16)(Stack[INCOMING_SS_INDEX] & MAX_U16);

    __asm__ volatile("mov %%cr0, %0" : "=r"(Frame->Registers.CR0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(Frame->Registers.CR2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(Frame->Registers.CR3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(Frame->Registers.CR4));
    __asm__ volatile("mov %%cr8, %0" : "=r"(Frame->Registers.CR8));

    __asm__ volatile("mov %%dr0, %0" : "=r"(Frame->Registers.DR0));
    __asm__ volatile("mov %%dr1, %0" : "=r"(Frame->Registers.DR1));
    __asm__ volatile("mov %%dr2, %0" : "=r"(Frame->Registers.DR2));
    __asm__ volatile("mov %%dr3, %0" : "=r"(Frame->Registers.DR3));
    __asm__ volatile("mov %%dr6, %0" : "=r"(Frame->Registers.DR6));
    __asm__ volatile("mov %%dr7, %0" : "=r"(Frame->Registers.DR7));

    Frame->IntNo = InterruptNumber;

    if (HasErrorCode != 0) {
        Frame->ErrCode = (U32)Stack[INCOMING_ERROR_CODE_INDEX];
    } else {
        Frame->ErrCode = 0;
    }

    return Frame;
}
