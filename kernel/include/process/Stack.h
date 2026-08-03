
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


    Stack operations

\************************************************************************/

#ifndef STACK_H_INCLUDED
#define STACK_H_INCLUDED

/************************************************************************/

#include "Arch.h"
#include "Base.h"
#include "Task-Stack.h"

/************************************************************************/

#define STACK_GROW_MIN_INCREMENT N_16KB
#define STACK_GROW_EXTRA_HEADROOM N_16KB

#if defined(__EXOS_ARCH_X86_32__)
#define STACK_MAXIMUM_TASK_STACK_SIZE N_1MB
#define STACK_MAXIMUM_SYSTEM_STACK_SIZE N_256KB
#else
#define STACK_MAXIMUM_TASK_STACK_SIZE N_2MB
#define STACK_MAXIMUM_SYSTEM_STACK_SIZE N_512KB
#endif

/************************************************************************/

// Copy stack content from source to destination and adjust frame pointers
BOOL CopyStack(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size);

// Copy stack content with specified EBP instead of using GetEBP()
BOOL CopyStackWithEBP(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size, LINEAR StartEBP);

// Copy stack and switch ESP/EBP to new location
BOOL SwitchStack(LINEAR DestStackTop, LINEAR SourceStackTop, UINT Size);

// Compute remaining bytes available on the current stack
UINT GetCurrentStackFreeBytes(void);

// Grow current stack by allocating additional space and migrating contents
BOOL GrowCurrentStack(UINT AdditionalBytes);

// Ensure a minimum amount of free stack space is available
BOOL EnsureCurrentStackSpace(UINT MinimumFreeBytes);

// Grow the current task system stack reactively to cover a stack-underflow fault
BOOL GrowFaultingSystemStack(LINEAR FaultAddress, LPINTERRUPT_FRAME Frame);

// Release a stack region, freeing the full tracked allocation
void StackRelease(LPSTACK Stack);

// Check current task's stack safety
BOOL CheckStack(void);

#endif
