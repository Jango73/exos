
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


    Kernel definitions

\************************************************************************/

#ifndef KERNEL_H_INCLUDED
#define KERNEL_H_INCLUDED

/************************************************************************/

// Privilege levels (rings)
#define CPU_PRIVILEGE_KERNEL 0x00
#define CPU_PRIVILEGE_DRIVERS 0x01
#define CPU_PRIVILEGE_ROUTINES 0x02
#define CPU_PRIVILEGE_USER 0x03

/************************************************************************/

#include "core/KernelData.h"

/************************************************************************/

#pragma pack(push, 1)

struct tag_PROCESS;
struct tag_SEGMENT_DESCRIPTOR;
struct tag_TSS_DESCRIPTOR;

/************************************************************************/
// EXOS system calls

#define EXOS_USER_CALL 0x70

typedef UINT (*SYSCALLFUNC)(UINT);

typedef struct tag_SYSCALL_ENTRY {
    SYSCALLFUNC Function;
    U32 Privilege;
} SYSCALL_ENTRY, *LPSYSCALL_ENTRY;

/************************************************************************/
// Functions in Kernel.c

void InitializeQuantumTime(void);
U32 ClockTestTask(LPVOID);
UINT GetPhysicalMemoryUsed(void);
void TestProcess(void);
void InitializeKernel(void);
void ShutdownKernel(void);
void RebootKernel(void);
void StoreObjectTerminationState(LPVOID Object, UINT ExitCode);
PHYSICAL KernelToPhysical(LINEAR Symbol);
LPVOID CreateKernelObject(UINT Size, U32 ObjectTypeID);
void SetKernelObjectDestructor(LPVOID Object, OBJECTDESTRUCTOR Destructor);
void DestroyKernelObject(LPVOID Object);
void ReleaseKernelObject(LPVOID Object);
void ReleaseProcessKernelObjects(struct tag_PROCESS* Process);
void DoPageFault(void);
HANDLE PointerToHandle(LINEAR Pointer);
LINEAR HandleToPointer(HANDLE Handle);
LINEAR EnsureKernelPointer(LINEAR Value);
HANDLE EnsureHandle(LINEAR Value);
void ReleaseHandle(HANDLE Handle);

/************************************************************************/
// Functions in MemoryEditor.c

void PrintMemory(U32, U32);
void MemoryEditor(U32);

/************************************************************************/
// Functions in Edit-Main.c

U32 Edit(U32, LPCSTR*, BOOL);

/************************************************************************/

#pragma pack(pop)

#endif  // KERNEL_H_INCLUDED
