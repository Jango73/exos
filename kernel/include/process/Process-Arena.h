
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


    Process address space arenas

\************************************************************************/

#ifndef PROCESS_ARENA_H_INCLUDED
#define PROCESS_ARENA_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

typedef struct tag_PROCESS_ARENA_RANGE {
    LINEAR Base;
    LINEAR Limit;
    LINEAR NextLow;
    LINEAR NextHigh;
} PROCESS_ARENA_RANGE, *LPPROCESS_ARENA_RANGE;

#define PROCESS_ARENA_IMAGE 0
#define PROCESS_ARENA_HEAP 1
#define PROCESS_ARENA_STACK 2
#define PROCESS_ARENA_MODULE 3
#define PROCESS_ARENA_SYSTEM 4
#define PROCESS_ARENA_MMIO 5
#define PROCESS_ARENA_COUNT 6

#define PROCESS_MODULE_ALLOCATION_SHARED 0
#define PROCESS_MODULE_ALLOCATION_PRIVATE 1
#define PROCESS_MODULE_ALLOCATION_TLS 2
#define PROCESS_MODULE_ALLOCATION_BOOKKEEPING 3
#define PROCESS_MODULE_ALLOCATION_COUNT 4

typedef struct tag_PROCESS_ADDRESS_SPACE {
    PROCESS_ARENA_RANGE Ranges[PROCESS_ARENA_COUNT];
    BOOL Initialized;
} PROCESS_ADDRESS_SPACE, *LPPROCESS_ADDRESS_SPACE;

/************************************************************************/

struct tag_PROCESS;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/************************************************************************/

void ProcessArenaReset(LPPROCESS Process);
BOOL ProcessArenaInitializeKernel(LPPROCESS Process);
BOOL ProcessArenaInitializeUser(LPPROCESS Process,
                                LINEAR ImageBase,
                                UINT ImageSize,
                                LINEAR HeapBase,
                                UINT InitialHeapSize);
void ProcessArenaConfigureMainHeap(LPPROCESS Process);
LINEAR ProcessArenaAllocateSystem(LPPROCESS Process, UINT Size, U32 Flags, LPCSTR Tag);
LINEAR ProcessArenaAllocateMmio(LPPROCESS Process, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag);
LINEAR ProcessArenaAllocateTaskStack(LPPROCESS Process, UINT Size);
LINEAR ProcessArenaAllocateUserStack(LPPROCESS Process, UINT Size);
LINEAR ProcessArenaAllocateModule(LPPROCESS Process, UINT Purpose, UINT Size, U32 Flags, LPCSTR Tag);
LINEAR ProcessArenaMapModulePages(
    LPPROCESS Process,
    UINT Purpose,
    PHYSICAL* PhysicalPages,
    UINT PageCount,
    U32 Flags,
    LPCSTR Tag);

/************************************************************************/

#endif  // PROCESS_ARENA_H_INCLUDED
