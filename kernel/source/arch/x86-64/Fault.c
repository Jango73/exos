
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


    Fault handling stubs (x86-64)

\************************************************************************/

#include "Arch.h"
#include "console/Console.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Schedule.h"
#include "system/System.h"
#include "text/Text.h"
#include "arch/x86-64/x86-64-Log.h"

/************************************************************************/

#define DEFINE_FATAL_HANDLER(FunctionName, Description)             \
    void FunctionName(LPINTERRUPT_FRAME Frame) {                    \
        ERROR(TEXT("%s"), TEXT(Description));                       \
        LogCPUState(Frame);                                         \
        Die();                                                      \
    }

/************************************************************************/

void LogCPUState(LPINTERRUPT_FRAME Frame) {
    BOOL PreviousPagingActive = ConsoleGetPagingActive();

    // Fault dumps must never block on console pager prompts.
    ConsoleSetPagingActive(FALSE);

    if (Frame == NULL) {
        ERROR(TEXT("No interrupt frame available"));
        ConsoleSetPagingActive(PreviousPagingActive);
        return;
    }

    LogFrame(Frame);

    if (Frame->Registers.RSP != (LINEAR)0) {
        LINEAR StackPtr = Frame->Registers.RSP;
        UINT Index;

        for (Index = 0; Index < 8; Index++) {
            LINEAR EntryAddress = StackPtr + (LINEAR)(Index * sizeof(LINEAR));
            LINEAR Value = 0;

            if (!IsValidMemory(EntryAddress)) {
                DEBUG(TEXT("Stack[%u] @ %p invalid"), Index, (LPVOID)EntryAddress);
                break;
            }

            Value = *((LINEAR*)EntryAddress);
            UNUSED(Value);
            DEBUG(TEXT("Stack[%u] @ %p = %p"), Index, (LPVOID)EntryAddress, (LPVOID)Value);
        }
    }

    BacktraceFrom(Frame->Registers.RBP, 10);

    ConsoleSetPagingActive(PreviousPagingActive);
}

/************************************************************************/

void Die(void) {
    LPTASK Task;

    DEBUG(TEXT("Enter"));

    Task = GetCurrentTask();

    SAFE_USE(Task) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

        FreezeScheduler();

        if (Task->OwnerProcess != &KernelProcess) {
            KernelKillTask(Task);
        } else {
            ERROR(TEXT("Fatal fault in kernel task, halting without KernelKillTask"));
            ConsolePanic(TEXT("Fatal fault in kernel task"));
        }

        UnlockMutex(MUTEX_CONSOLE_STATE);
        UnlockMutex(MUTEX_MEMORY);
        UnlockMutex(MUTEX_KERNEL);

        UnfreezeScheduler();

        EnableInterrupts();
    }

    // Wait forever
    do {
        __asm__ __volatile__(
            "1:\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
            :
            :
            : "memory");
    } while (0);
}

/************************************************************************/

void DefaultHandler(LPINTERRUPT_FRAME Frame) {
    UNUSED(Frame);
}

/************************************************************************/

DEFINE_FATAL_HANDLER(DivideErrorHandler, "Divide error fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DebugExceptionHandler, "Debug exception fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(NMIHandler, "Non-maskable interrupt")

/************************************************************************/

DEFINE_FATAL_HANDLER(BreakPointHandler, "Breakpoint fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(OverflowHandler, "Overflow fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(BoundRangeHandler, "BOUND range fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(InvalidOpcodeHandler, "Invalid opcode fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DeviceNotAvailHandler, "Device not available fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DoubleFaultHandler, "Double fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(MathOverflowHandler, "Coprocessor segment overrun")

/************************************************************************/

DEFINE_FATAL_HANDLER(InvalidTSSHandler, "Invalid TSS fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(SegmentFaultHandler, "Segment not present fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(StackFaultHandler, "Stack fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(GeneralProtectionHandler, "General protection fault")

/************************************************************************/

void PageFaultHandler(LPINTERRUPT_FRAME Frame) {
    U64 FaultAddress = 0;

    __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));

    DEBUG(TEXT("CR2=%p Err=%x RIP=%p RSP=%p"),
          (LPVOID)FaultAddress,
          (UINT)Frame->ErrCode,
          (LPVOID)Frame->Registers.RIP,
          (LPVOID)Frame->Registers.RSP);

    if (ResolveKernelPageFault((LINEAR)FaultAddress)) {
        DEBUG(TEXT("Resolved kernel page fault %p"), (LPVOID)FaultAddress);
        return;
    }

    ERROR(TEXT("Page fault at %p"), (LINEAR)FaultAddress);
    ERROR(TEXT("Error code = %x"), (UINT)Frame->ErrCode);
    LogCPUState(Frame);
    Die();
}

/************************************************************************/

DEFINE_FATAL_HANDLER(AlignmentCheckHandler, "Alignment check fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(MachineCheckHandler, "Machine check fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(FloatingPointHandler, "Floating point fault")
