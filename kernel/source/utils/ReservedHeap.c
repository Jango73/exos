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


    ReservedHeap

\************************************************************************/

#include "utils/ReservedHeap.h"

#include "text/CoreString.h"
#include "log/Log.h"
#include "process/Process-Arena.h"

/************************************************************************/

static BOOL ReservedHeapResize(LPVOID Context, LPHEAP_CONTROL_BLOCK ControlBlock, UINT NewSize) {
    LPRESERVED_HEAP Heap = (LPRESERVED_HEAP)Context;
    UINT GrowthSize;

    if (Heap == NULL || ControlBlock == NULL || Heap->HeapBase != ControlBlock->HeapBase) {
        return FALSE;
    }

    if (Heap->HeapSize != ControlBlock->HeapSize) {
        WARNING(TEXT("Reserved heap size mismatch for %s at %p (tracked=%u requested=%u)"),
                Heap->Tag,
                Heap->HeapBase,
                Heap->HeapSize,
                ControlBlock->HeapSize);
        return FALSE;
    }

    if (Heap->RegionFlags != ControlBlock->RegionFlags) {
        WARNING(TEXT("Reserved heap flags mismatch for %s at %p (tracked=%x requested=%x)"),
                Heap->Tag,
                Heap->HeapBase,
                Heap->RegionFlags,
                ControlBlock->RegionFlags);
        return FALSE;
    }

    if (NewSize > Heap->MaximumSize) {
        return FALSE;
    }

    GrowthSize = NewSize - ControlBlock->HeapSize;
    if (GrowthSize == 0) {
        return TRUE;
    }

    if (CommitRegionRange(ControlBlock->HeapBase + ControlBlock->HeapSize, GrowthSize, ControlBlock->RegionFlags) ==
        FALSE) {
        return FALSE;
    }

    Heap->HeapSize = NewSize;
    return TRUE;
}

/************************************************************************/

static LPVOID ReservedHeapAllocatorAlloc(LPVOID Context, UINT Size) {
    return ReservedHeapAlloc((LPRESERVED_HEAP)Context, Size);
}

/************************************************************************/

static LPVOID ReservedHeapAllocatorRealloc(LPVOID Context, LPVOID Pointer, UINT Size) {
    return ReservedHeapRealloc((LPRESERVED_HEAP)Context, Pointer, Size);
}

/************************************************************************/

static void ReservedHeapAllocatorFree(LPVOID Context, LPVOID Pointer) {
    ReservedHeapFree((LPRESERVED_HEAP)Context, Pointer);
}

/************************************************************************/

BOOL ReservedHeapInit(
    LPRESERVED_HEAP Heap,
    LPPROCESS Process,
    UINT InitialSize,
    UINT MaximumSize,
    U32 RegionFlags,
    LPCSTR Tag) {
    LINEAR HeapBase;

    if (Heap == NULL || Process == NULL || InitialSize == 0 || MaximumSize < InitialSize) {
        return FALSE;
    }

    MemorySet(Heap, 0, sizeof(RESERVED_HEAP));
    InitMutex(&Heap->Mutex);

    Heap->Process = Process;
    Heap->HeapSize = InitialSize;
    Heap->MaximumSize = MaximumSize;
    Heap->RegionFlags = RegionFlags;
    StringCopy(Heap->Tag, (Tag != NULL) ? Tag : TEXT("ReservedHeap"));

    HeapBase = ProcessArenaAllocateSystem(Process, MaximumSize, RegionFlags & ~ALLOC_PAGES_COMMIT, Heap->Tag);
    if (HeapBase == 0) {
        ERROR(TEXT("Failed to allocate region for %s"), Heap->Tag);
        return FALSE;
    }

    if (CommitRegionRange(HeapBase, InitialSize, RegionFlags) == FALSE) {
        FreeRegion(HeapBase, MaximumSize);
        ERROR(TEXT("Failed to commit initial region for %s"), Heap->Tag);
        return FALSE;
    }

    Heap->HeapBase = HeapBase;
    HeapInit(Process, HeapBase, InitialSize);
    HeapConfigureGrowth(HeapBase, Heap, ReservedHeapResize, MaximumSize, RegionFlags);
    return TRUE;
}

/************************************************************************/

void ReservedHeapDeinit(LPRESERVED_HEAP Heap) {
    if (Heap == NULL || Heap->HeapBase == 0 || Heap->HeapSize == 0) {
        return;
    }

    if (!FreeRegion(Heap->HeapBase, Heap->MaximumSize)) {
        WARNING(TEXT("FreeRegion failed for %s at %p size=%u"), Heap->Tag, Heap->HeapBase, Heap->MaximumSize);
    }

    Heap->HeapBase = 0;
    Heap->HeapSize = 0;
}

/************************************************************************/

LPVOID ReservedHeapAlloc(LPRESERVED_HEAP Heap, UINT Size) {
    LPVOID Pointer;

    if (Heap == NULL || Heap->HeapBase == 0) {
        return NULL;
    }

    LockMutex(&Heap->Mutex, INFINITY);
    Pointer = HeapAlloc_HBHS(Heap->Process, Heap->HeapBase, Heap->HeapSize, Size);
    Heap->HeapSize = ((LPHEAP_CONTROL_BLOCK)Heap->HeapBase)->HeapSize;
    UnlockMutex(&Heap->Mutex);

    return Pointer;
}

/************************************************************************/

LPVOID ReservedHeapRealloc(LPRESERVED_HEAP Heap, LPVOID Pointer, UINT Size) {
    LPVOID NewPointer;

    if (Heap == NULL || Heap->HeapBase == 0) {
        return NULL;
    }

    LockMutex(&Heap->Mutex, INFINITY);
    NewPointer = HeapRealloc_HBHS(Heap->Process, Heap->HeapBase, Heap->HeapSize, Pointer, Size);
    Heap->HeapSize = ((LPHEAP_CONTROL_BLOCK)Heap->HeapBase)->HeapSize;
    UnlockMutex(&Heap->Mutex);

    return NewPointer;
}

/************************************************************************/

void ReservedHeapFree(LPRESERVED_HEAP Heap, LPVOID Pointer) {
    if (Heap == NULL || Heap->HeapBase == 0) {
        return;
    }

    LockMutex(&Heap->Mutex, INFINITY);
    HeapFree_HBHS(Heap->HeapBase, Heap->HeapSize, Pointer);
    UnlockMutex(&Heap->Mutex);
}

/************************************************************************/

void ReservedHeapInitAllocator(LPRESERVED_HEAP Heap, LPALLOCATOR Allocator) {
    if (Heap == NULL || Allocator == NULL) {
        return;
    }

    AllocatorInitFunctions(
        Allocator,
        Heap,
        ReservedHeapAllocatorAlloc,
        ReservedHeapAllocatorRealloc,
        ReservedHeapAllocatorFree);
}

/************************************************************************/
