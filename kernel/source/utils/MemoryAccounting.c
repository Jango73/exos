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

#include "utils/MemoryAccounting.h"

#include "text/CoreString.h"

/************************************************************************/

static LPVOID MemoryAccountingAlloc(LPVOID Context, UINT Size);
static LPVOID MemoryAccountingRealloc(LPVOID Context, LPVOID Pointer, UINT Size);
static void MemoryAccountingFree(LPVOID Context, LPVOID Pointer);

/************************************************************************/

/**
 * @brief Initialize an accounting allocator wrapping a base allocator.
 *
 * The wrapper prefixes every allocation with a fixed size header so the live
 * byte count can be adjusted on free and realloc without knowing block sizes.
 * @param This Accounting allocator to initialize.
 * @param Base Base allocator whose memory is accounted.
 */
void MemoryAccountingAllocatorInit(LPMEMORY_ACCOUNTING_ALLOCATOR This, LPCALLOCATOR Base) {
    if (This == NULL || Base == NULL) {
        return;
    }

    MemorySet(This, 0, sizeof(MEMORY_ACCOUNTING_ALLOCATOR));
    This->Base = *Base;

    AllocatorInitFunctions(&This->Wrapper, This, MemoryAccountingAlloc, MemoryAccountingRealloc, MemoryAccountingFree);
}

/************************************************************************/

/**
 * @brief Reset the peak counters to the current live values.
 *
 * Used to measure one isolated work unit (for example one script execution)
 * so the peak reflects only that unit.
 * @param This Accounting allocator whose peaks are reset.
 */
void MemoryAccountingAllocatorResetPeak(LPMEMORY_ACCOUNTING_ALLOCATOR This) {
    if (This == NULL) {
        return;
    }

    This->Stats.PeakBytes = This->Stats.LiveBytes;
    This->Stats.PeakAllocationCount = This->Stats.AllocationCount;
}

/************************************************************************/

/**
 * @brief Copy the accounting statistics of an accounting allocator.
 * @param This Accounting allocator to inspect.
 * @param OutStats Destination for the statistics snapshot.
 */
void MemoryAccountingAllocatorGetStats(LPMEMORY_ACCOUNTING_ALLOCATOR This, LPMEMORY_ACCOUNTING_STATS OutStats) {
    if (This == NULL || OutStats == NULL) {
        return;
    }

    *OutStats = This->Stats;
}

/************************************************************************/

static LPVOID MemoryAccountingAlloc(LPVOID Context, UINT Size) {
    LPMEMORY_ACCOUNTING_ALLOCATOR This = (LPMEMORY_ACCOUNTING_ALLOCATOR)Context;
    LPMEMORY_ACCOUNTING_HEADER Header;
    LPVOID Raw;

    if (This == NULL) {
        return NULL;
    }

    Raw = AllocatorAlloc(&This->Base, Size + MEMORY_ACCOUNTING_HEADER_SIZE);
    if (Raw == NULL) {
        return NULL;
    }

    Header = (LPMEMORY_ACCOUNTING_HEADER)Raw;
    Header->Size = Size;

    This->Stats.LiveBytes += Size;
    if (This->Stats.LiveBytes > This->Stats.PeakBytes) {
        This->Stats.PeakBytes = This->Stats.LiveBytes;
    }
    This->Stats.AllocationCount++;
    if (This->Stats.AllocationCount > This->Stats.PeakAllocationCount) {
        This->Stats.PeakAllocationCount = This->Stats.AllocationCount;
    }

    return (LPVOID)((U8*)Raw + MEMORY_ACCOUNTING_HEADER_SIZE);
}

/************************************************************************/

static LPVOID MemoryAccountingRealloc(LPVOID Context, LPVOID Pointer, UINT Size) {
    LPMEMORY_ACCOUNTING_ALLOCATOR This = (LPMEMORY_ACCOUNTING_ALLOCATOR)Context;
    LPMEMORY_ACCOUNTING_HEADER Header;
    LPVOID Raw;
    LPVOID NewRaw;
    UINT OldSize;

    if (This == NULL) {
        return NULL;
    }

    if (Pointer == NULL) {
        return MemoryAccountingAlloc(Context, Size);
    }

    Raw = (LPVOID)((U8*)Pointer - MEMORY_ACCOUNTING_HEADER_SIZE);
    OldSize = ((LPMEMORY_ACCOUNTING_HEADER)Raw)->Size;

    NewRaw = AllocatorRealloc(&This->Base, Raw, Size + MEMORY_ACCOUNTING_HEADER_SIZE);
    if (NewRaw == NULL) {
        return NULL;
    }

    Header = (LPMEMORY_ACCOUNTING_HEADER)NewRaw;
    Header->Size = Size;

    if (Size >= OldSize) {
        This->Stats.LiveBytes += Size - OldSize;
    } else {
        This->Stats.LiveBytes -= OldSize - Size;
    }
    if (This->Stats.LiveBytes > This->Stats.PeakBytes) {
        This->Stats.PeakBytes = This->Stats.LiveBytes;
    }

    return (LPVOID)((U8*)NewRaw + MEMORY_ACCOUNTING_HEADER_SIZE);
}

/************************************************************************/

static void MemoryAccountingFree(LPVOID Context, LPVOID Pointer) {
    LPMEMORY_ACCOUNTING_ALLOCATOR This = (LPMEMORY_ACCOUNTING_ALLOCATOR)Context;
    LPMEMORY_ACCOUNTING_HEADER Header;
    LPVOID Raw;
    UINT Size;

    if (This == NULL || Pointer == NULL) {
        return;
    }

    Raw = (LPVOID)((U8*)Pointer - MEMORY_ACCOUNTING_HEADER_SIZE);
    Header = (LPMEMORY_ACCOUNTING_HEADER)Raw;
    Size = Header->Size;

    if (Size <= This->Stats.LiveBytes) {
        This->Stats.LiveBytes -= Size;
    } else {
        This->Stats.LiveBytes = 0;
    }
    if (This->Stats.AllocationCount > 0) {
        This->Stats.AllocationCount--;
    }

    AllocatorFree(&This->Base, Raw);
}

/************************************************************************/
