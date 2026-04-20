
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


    Fault

\************************************************************************/

#include "console/Console.h"
#include "memory/Heap.h"
#include "arch/Disassemble.h"
#include "arch/x86-32/x86-32.h"
#include "arch/x86-32/x86-32-Log.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "text/Text.h"

// Conservative pointer checks. If StackLow/High are zero, use heuristics.
/*
static BOOL IsFramePointerSane(U32 CurEbp, U32 PrevEbp, U32 StackLow, U32 StackHigh) {
    // EBP must increase (stack grows down in x86, older frame is at higher address).
    if (CurEbp <= PrevEbp) return FALSE;

    // Heuristic: frame size should not be absurd (e.g., > 256 KiB).
    if ((CurEbp - PrevEbp) > (256u * 1024u)) return FALSE;

    // If we have valid stack bounds, enforce them.
    if (StackLow | StackHigh) {
        if (CurEbp < StackLow || CurEbp >= StackHigh) return FALSE;
    } else {
        // Heuristic bounds (typical higher-half kernel). Adjust if your layout differs.
        if (CurEbp < VMA_KERNEL) return FALSE;
    }
    // 4-byte alignment is expected.
    if ((CurEbp & 3u) != 0) return FALSE;

    return TRUE;
}
*/

/************************************************************************/

void LogCPUState(LPINTERRUPT_FRAME Frame) {
    BOOL PreviousPagingActive = ConsoleGetPagingActive();

    // Fault dumps must never block on console pager prompts.
    ConsoleSetPagingActive(FALSE);

    STR DisasmBuffer[MAX_STRING_BUFFER];
    LogFrame(Frame);
    Disassemble(DisasmBuffer, Frame->Registers.EIP, 5, 32);
    ERROR(TEXT("Code at EIP:\n%s"), DisasmBuffer);
    BacktraceFrom(Frame->Registers.EBP, 10);

    ConsoleSetPagingActive(PreviousPagingActive);
}

/************************************************************************/

/**
 * @brief Terminate the current task and halt if necessary.
 */
void Die(void) {
    LPTASK Task;

    DEBUG(TEXT("Enter"));

    Task = GetCurrentTask();

    SAFE_USE(Task) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

        FreezeScheduler();

        KernelKillTask(Task);

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

/**
 * @brief Handle unknown interrupts.
 * @param Frame Interrupt frame context.
 */
void DefaultHandler(LPINTERRUPT_FRAME Frame) {
    UNUSED(Frame);
    // DEBUG(TEXT("Unknown interrupt"));
    // LogCPUState(Frame);
    return;
}

/************************************************************************/

/**
 * @brief Handle divide-by-zero faults.
 * @param Frame Interrupt frame context.
 */
void DivideErrorHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Divide error"));
    LogCPUState(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle debug exceptions and log diagnostic information.
 * @param Frame Interrupt frame context.
 */
void DebugExceptionHandler(LPINTERRUPT_FRAME Frame) {
    LPTASK Task = GetCurrentTask();
    U32 dr6, dr0, dr7;

    ERROR(TEXT("FAULT: Debug exception"));

    // Read debug registers
    GetDR0(dr0);
    GetDR6(dr6);
    GetDR7(dr7);

    if (dr6 & 0x1) {  // DR0 breakpoint hit
        ERROR(TEXT("=== HARDWARE BREAKPOINT HIT at %x ==="), dr0);

        // Dump CPU state
        U32 cr0, cr2, cr3;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        ERROR(TEXT("CR0=%X CR2=%X CR3=%X"), cr0, cr2, cr3);

        // Dump memory at breakpoint
        if (IsValidMemory(dr0)) {
            U32* mem = (U32*)dr0;
            ERROR(TEXT("Memory[%x] = %08x %08x %08x %08x"), dr0, mem[0], mem[1], mem[2], mem[3]);
        } else {
            ERROR(TEXT("Memory[%x] NOT ACCESSIBLE!"), dr0);
        }

        // Check page table mapping
        U32 pde_idx = dr0 >> 22;
        U32 pte_idx = (dr0 >> 12) & 0x3FF;
        U32* pd = (U32*)0xFFFFF000;  // PD_VA

        if (pd[pde_idx] & 1) {
            U32* pt = (U32*)(0xFFC00000 + pde_idx * 0x1000);  // PT_BASE_VA
            U32 pte = pt[pte_idx];
            ERROR(TEXT("PDE[%d]=%08x PTE[%d]=%08x (Present=%d User=%d RW=%d)"), pde_idx, pd[pde_idx], pte_idx,
                pte, pte & 1, (pte >> 2) & 1, (pte >> 1) & 1);
        } else {
            ERROR(TEXT("PDE[%d]=%08x NOT PRESENT!"), pde_idx, pd[pde_idx]);
        }

        // Clear debug registers and continue
        ClearDR6();
        ClearDR7();
        return;  // Don't die, continue execution
    }

    // Original debug handler for other debug exceptions
    ConsolePrint(TEXT("Debug exception !\n"));
    ConsolePrint(TEXT("The current task (%x) triggered a debug exception "), Task ? Task : 0);
    ConsolePrint(TEXT("at EIP : %x\n"), Frame->Registers.EIP);

    LogCPUState(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle non-maskable interrupts.
 * @param Frame Interrupt frame context.
 */
void NMIHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Non-maskable interrupt"));
    LogCPUState(Frame);
}

/***************************************************************************/

/**
 * @brief Handle breakpoint exceptions.
 * @param Frame Interrupt frame context.
 */
void BreakPointHandler(LPINTERRUPT_FRAME Frame) {
    UNUSED(Frame);
    ERROR(TEXT("FAULT: Breakpoint"));
    // LogCPUState(Frame);
}

/***************************************************************************/

/**
 * @brief Handle overflow exceptions.
 * @param Frame Interrupt frame context.
 */
void OverflowHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Overflow"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle bound range exceeded faults.
 * @param Frame Interrupt frame context.
 */
void BoundRangeHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Bound range fault"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle invalid opcode faults.
 * @param Frame Interrupt frame context.
 */
void InvalidOpcodeHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Invalid opcode"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle device-not-available faults.
 * @param Frame Interrupt frame context.
 */
void DeviceNotAvailHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Device not available"));
    LogCPUState(Frame);
}

/***************************************************************************/

/**
 * @brief Handle double fault exceptions.
 * @param Frame Interrupt frame context.
 */
void DoubleFaultHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Double fault"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle math overflow exceptions.
 * @param Frame Interrupt frame context.
 */
void MathOverflowHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Math overflow"));
    ConsolePrint(TEXT("Math overflow!\n"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle invalid TSS faults.
 * @param Frame Interrupt frame context.
 */
void InvalidTSSHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Invalid TSS"));
    ConsolePrint(TEXT("Invalid TSS!\n"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle segment not present faults.
 * @param Frame Interrupt frame context.
 */
void SegmentFaultHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Segment fault"));
    ConsolePrint(TEXT("Segment fault!\n"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle stack fault exceptions.
 * @param Frame Interrupt frame context.
 */
void StackFaultHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Stack fault"));
    ConsolePrint(TEXT("Stack fault!\n"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle general protection faults.
 * @param Frame Interrupt frame context.
 */
void GeneralProtectionHandler(LPINTERRUPT_FRAME Frame) {
    LPTASK Task = GetCurrentTask();
    ERROR(TEXT("FAULT: General protection fault"));

    ConsolePrint(TEXT("General protection fault !\n"));
    ConsolePrint(TEXT("The current thread (%X) triggered a general protection "), Task ? Task : 0);
    ConsolePrint(TEXT("fault with error code : %X, at EIP : %X\n"), Frame->ErrCode, Frame->Registers.EIP);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting\n"));

    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle page fault exceptions.
 * @param Frame Interrupt frame context.
 */
void PageFaultHandler(LPINTERRUPT_FRAME Frame) {
    LINEAR FaultAddress;
    __asm__ volatile("mov %%cr2, %0" : "=r"(FaultAddress));

    DEBUG(TEXT("CR2=%X Err=%X EIP=%X ESP=%X"),
          FaultAddress,
          Frame->ErrCode,
          Frame->Registers.EIP,
          Frame->Registers.ESP);

    if (ResolveKernelPageFault(FaultAddress)) {
        DEBUG(TEXT("Resolved kernel page fault %X"), FaultAddress);
        return;
    }

    ERROR(TEXT("FAULT: Page fault %X (EIP %X)"), FaultAddress, Frame->Registers.EIP);

    LPTASK Task = GetCurrentTask();

    ConsolePrint(TEXT("Page fault !\n"));
    ConsolePrint(TEXT("The current thread (%X) did an unauthorized access "), Task ? Task : 0);
    ConsolePrint(
        TEXT("at linear address : %X, error code : %X, EIP : %X\n"), FaultAddress, Frame->ErrCode,
        Frame->Registers.EIP);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting\n"));

    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle alignment check faults.
 * @param Frame Interrupt frame context.
 */
void AlignmentCheckHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("Alignment check fault"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle alignment check faults.
 * @param Frame Interrupt frame context.
 */
void MachineCheckHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("FAULT: Machine check exception"));
    LogCPUState(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle floating point exceptions.
 * @param Frame Interrupt frame context.
 */
void FloatingPointHandler(LPINTERRUPT_FRAME Frame) {
    ERROR(TEXT("Floating point exception"));
    LogCPUState(Frame);
    Die();
}
