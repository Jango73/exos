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


    Memory accounting allocator wrapper

\************************************************************************/

#ifndef MEMORYACCOUNTING_H_INCLUDED
#define MEMORYACCOUNTING_H_INCLUDED

/**************************************************************************/

#include "Base.h"
#include "utils/Allocator.h"

/**************************************************************************/

// The heap returns 16-byte aligned blocks. The prefix header must keep the
// payload at the same alignment, so it is a fixed 16-byte block.
#define MEMORY_ACCOUNTING_HEADER_SIZE 16

/**************************************************************************/

typedef struct tag_MEMORY_ACCOUNTING_STATS {
    UINT LiveBytes;
    UINT PeakBytes;
    UINT AllocationCount;
    UINT PeakAllocationCount;
} MEMORY_ACCOUNTING_STATS, *LPMEMORY_ACCOUNTING_STATS;

typedef struct tag_MEMORY_ACCOUNTING_HEADER {
    UINT Size;
    U8 Padding[MEMORY_ACCOUNTING_HEADER_SIZE - sizeof(UINT)];
} MEMORY_ACCOUNTING_HEADER, *LPMEMORY_ACCOUNTING_HEADER;

typedef struct tag_MEMORY_ACCOUNTING_ALLOCATOR {
    MEMORY_ACCOUNTING_STATS Stats;
    ALLOCATOR Base;
    ALLOCATOR Wrapper;
} MEMORY_ACCOUNTING_ALLOCATOR, *LPMEMORY_ACCOUNTING_ALLOCATOR;

/**************************************************************************/

void MemoryAccountingAllocatorInit(LPMEMORY_ACCOUNTING_ALLOCATOR This, LPCALLOCATOR Base);
void MemoryAccountingAllocatorResetPeak(LPMEMORY_ACCOUNTING_ALLOCATOR This);
void MemoryAccountingAllocatorGetStats(LPMEMORY_ACCOUNTING_ALLOCATOR This, LPMEMORY_ACCOUNTING_STATS OutStats);

/**************************************************************************/

#endif  // MEMORYACCOUNTING_H_INCLUDED
