
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


    Interrupt Frame Management for x86-32

\************************************************************************/

/************************************************************************\

    Trap entry stack for #DB, #DF, #TS, #NP, #SS, #GP, #PF, #AC


    High addresses
         |
         v

 E  +------------------+ <-- ESP before exception
 S  |                  |
 P  |   User Stack     |     (user data before exception)
    |                  |
 G  +------------------+
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - ALWAYS PRESENT
 N  +------------------+
    |      EIP         | <-- 32 bits - ALWAYS PRESENT
 |  +------------------+
 v  |   ERROR_CODE     | <-- 32 bits - ALWAYS PRESENT
    +------------------+ <-- ESP after exception (in handler)
    |                  |
    |  Kernel Stack    |     (exception handler)
    |                  |
         |
         v
    Low addresses

    ERROR_CODE = 0 for #DB, #DF, #AC
               = selector for #TS, #NP, #SS, #GP
               = info for #PF

--------------------------------------------------------------------------

    Trap entry stack for IRQs and #DE, #BR, #UD, #NM, #MF
    Just short of an error code


    High addresses
         |
         v

 E  +------------------+ <-- ESP before exception
 S  |                  |
 P  |   User Stack     |     (user data before exception)
    |                  |
 G  +------------------+
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - ALWAYS PRESENT
 N  +------------------+
    |      EIP         | <-- 32 bits - ALWAYS PRESENT
 |  +------------------+ <-- ESP after exception (in handler)
 v  |                  |
    |  Kernel Stack    |     (exception handler)
    |                  |
         |
         v
    Low addresses

--------------------------------------------------------------------------

    Trap stack after all pushes from stub, without an error code

 E  +------------------+ <-- ESP before exception (may be top of system stack)
 S  |                  |
 P  | Some stack data  |     (user data before exception)
    |                  |
 G  +------------------+ <-- TRAP (may be top of system stack)
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - pushed by CPU
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - pushed by CPU
 N  +------------------+
    |      EIP         | <-- 32 bits - pushed by CPU
 |  +------------------+ <-- ESP after exception (in handler)
 v  |      EAX         | <-- pushed by pushad
    +------------------+
    |      ECX         | <-- pushed by "pushad"
    +------------------+
    |      EDX         | <-- pushed by "pushad"
    +------------------+
    |      EBX         | <-- pushed by "pushad"
    +------------------+
    |      ESP         | <-- pushed by "pushad"
    +------------------+
    |      EBP         | <-- pushed by "pushad"
    +------------------+
    |      ESI         | <-- pushed by "pushad"
    +------------------+
    |      EDI         | <-- pushed by "pushad"
    +------------------+
    |        |   DS    | <-- pushed by "push ds"
    +------------------+
    |        |   ES    | <-- pushed by "push es"
    +------------------+
    |        |   FS    | <-- pushed by "push fs"
    +------------------+
    |        |   GS    | <-- pushed by "push gs"
    +------------------+
    |      EBP         |
    +------------------+
    |        |   SS    |
    +------------------+
    |      DATA        |
    |       OF         |
    |  INTERRUPT_FRAME  |
    +------------------+ <-- ESP as given to BuildInterruptFrame

\************************************************************************/

#include "arch/InterruptFrame.h"

#include "Base.h"
#include "arch/x86-32/x86-32.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"
#include "system/System.h"

/************************************************************************/

#define INCOMING_SS_INDEX 0
#define INCOMING_C_EBP_INDEX 1
#define INCOMING_GS_INDEX 2
#define INCOMING_FS_INDEX 3
#define INCOMING_ES_INDEX 4
#define INCOMING_DS_INDEX 5
#define INCOMING_EDI_INDEX 6
#define INCOMING_ESI_INDEX 7
#define INCOMING_EBP_INDEX 8
#define INCOMING_ESP_INDEX 9
#define INCOMING_EBX_INDEX 10
#define INCOMING_EDX_INDEX 11
#define INCOMING_ECX_INDEX 12
#define INCOMING_EAX_INDEX 13
#define INCOMING_ERROR_CODE_INDEX 14  // If present, the following indexes will be shifted up by 1 (+ HasErrorCode)
#define INCOMING_EIP_INDEX 14         // Yes, it is the same index as INCOMING_ERROR_CODE_INDEX, don't touch this
#define INCOMING_CS_INDEX 15
#define INCOMING_EFLAGS_INDEX 16
#define INCOMING_R3_ESP_INDEX 17
#define INCOMING_R3_SS_INDEX 18

/************************************************************************/

LPINTERRUPT_FRAME BuildInterruptFrame(U32 InterruptNumber, U32 HasErrorCode, U32 StackPointer) {
    LPINTERRUPT_FRAME Frame;
    U32* Stack;
    U32 UserMode;

    if (HasErrorCode > 1) HasErrorCode = 1;

    Frame = (LPINTERRUPT_FRAME)StackPointer;
    Stack = (U32*)(StackPointer + sizeof(INTERRUPT_FRAME));

    if (IsValidMemory((LINEAR)Stack) == FALSE) {
        DEBUG(TEXT("Invalid stack computed : %p"), Stack);
        DO_THE_SLEEPING_BEAUTY;
    }

    UserMode = (Stack[INCOMING_CS_INDEX + HasErrorCode] & SELECTOR_RPL_MASK) != 0;

    MemorySet(Frame, 0, sizeof(INTERRUPT_FRAME));

    Frame->Registers.EFlags = Stack[INCOMING_EFLAGS_INDEX + HasErrorCode];
    Frame->Registers.EIP = Stack[INCOMING_EIP_INDEX + HasErrorCode];
    Frame->Registers.CS = Stack[INCOMING_CS_INDEX + HasErrorCode] & MAX_U16;

    FINE_DEBUG(TEXT("FRAME BUILD DEBUG - InterruptNumber=%d HasErrorCode=%d UserMode=%d"),
        InterruptNumber, HasErrorCode, UserMode);
    FINE_DEBUG(TEXT("Stack at %p:"), (LINEAR)Stack);
    if (SCHEDULING_DEBUG_OUTPUT == 1) {
        KernelLogMem(LOG_DEBUG, (U32)Stack, 256);
    }
    FINE_DEBUG(TEXT("Extracted: EIP=%p CS=%x EFlags=%x"), (LINEAR)Frame->Registers.EIP,
        Frame->Registers.CS, Frame->Registers.EFlags);

    Frame->Registers.EAX = Stack[INCOMING_EAX_INDEX];
    Frame->Registers.EBX = Stack[INCOMING_EBX_INDEX];
    Frame->Registers.ECX = Stack[INCOMING_ECX_INDEX];
    Frame->Registers.EDX = Stack[INCOMING_EDX_INDEX];
    Frame->Registers.ESI = Stack[INCOMING_ESI_INDEX];
    Frame->Registers.EDI = Stack[INCOMING_EDI_INDEX];
    Frame->Registers.EBP = Stack[INCOMING_EBP_INDEX];

    Frame->Registers.DS = Stack[INCOMING_DS_INDEX] & MAX_U16;
    Frame->Registers.ES = Stack[INCOMING_ES_INDEX] & MAX_U16;
    Frame->Registers.FS = Stack[INCOMING_FS_INDEX] & MAX_U16;
    Frame->Registers.GS = Stack[INCOMING_GS_INDEX] & MAX_U16;

    if (UserMode) {
        Frame->Registers.ESP = Stack[INCOMING_R3_ESP_INDEX + HasErrorCode];
        Frame->Registers.SS = Stack[INCOMING_R3_SS_INDEX + HasErrorCode] & MAX_U16;
    } else {
        Frame->Registers.ESP = Stack[INCOMING_ESP_INDEX];
        Frame->Registers.SS = Stack[INCOMING_SS_INDEX] & MAX_U16;
    }

    __asm__ volatile("mov %%cr0, %0" : "=r"(Frame->Registers.CR0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(Frame->Registers.CR2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(Frame->Registers.CR3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(Frame->Registers.CR4));

    __asm__ volatile("mov %%dr0, %0" : "=r"(Frame->Registers.DR0));
    __asm__ volatile("mov %%dr1, %0" : "=r"(Frame->Registers.DR1));
    __asm__ volatile("mov %%dr2, %0" : "=r"(Frame->Registers.DR2));
    __asm__ volatile("mov %%dr3, %0" : "=r"(Frame->Registers.DR3));
    __asm__ volatile("mov %%dr6, %0" : "=r"(Frame->Registers.DR6));
    __asm__ volatile("mov %%dr7, %0" : "=r"(Frame->Registers.DR7));

    Frame->Registers.DR4 = 0;
    Frame->Registers.DR5 = 0;
    Frame->IntNo = InterruptNumber;

    if (HasErrorCode) {
        Frame->ErrCode = Stack[INCOMING_ERROR_CODE_INDEX];
    } else {
        Frame->ErrCode = 0;
    }

    return Frame;
}
