
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


    BlockList - Fixed Size Slab Allocator

\************************************************************************/

#include "utils/BlockList.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "User.h"

/************************************************************************/

typedef struct tag_BLOCK_LIST_NODE {
    struct tag_BLOCK_LIST_NODE* Next;
} BLOCK_LIST_NODE, *LPBLOCK_LIST_NODE;

/************************************************************************/

static UINT GetMaxValue(void) { return (UINT)(~(UINT)0); }

/************************************************************************/

static BOOL MultiplySafe(UINT A, UINT B, UINT* Result) {
    if (Result == NULL) {
        return FALSE;
    }

    if (A == 0 || B == 0) {
        *Result = 0;
        return TRUE;
    }

    if (A > GetMaxValue() / B) {
        return FALSE;
    }

    *Result = A * B;
    return TRUE;
}

/************************************************************************/

static BOOL AlignValue(UINT Value, UINT Alignment, UINT* Result) {
    if (Result == NULL || Alignment == 0) {
        return FALSE;
    }

    UINT Remainder = Value % Alignment;

    if (Remainder == 0) {
        *Result = Value;
        return TRUE;
    }

    UINT Increment = Alignment - Remainder;

    if (Value > GetMaxValue() - Increment) {
        return FALSE;
    }

    *Result = Value + Increment;
    return TRUE;
}

/************************************************************************/

static BOOL BlockListEnsureSlabMetadata(LPBLOCK_LIST List, UINT RequiredSlabs) {
    if (List == NULL) {
        return FALSE;
    }

    if (RequiredSlabs == 0) {
        RequiredSlabs = 1;
    }

    if (List->SlabCapacity >= RequiredSlabs) {
        return TRUE;
    }

    UINT NewCapacity = (List->SlabCapacity == 0) ? 1 : List->SlabCapacity;

    while (NewCapacity < RequiredSlabs) {
        if (NewCapacity > (GetMaxValue() >> 1)) {
            NewCapacity = RequiredSlabs;
            break;
        }
        NewCapacity <<= 1;
        if (NewCapacity < List->SlabCapacity) {
            NewCapacity = RequiredSlabs;
            break;
        }
    }

    UINT OldBytes = List->SlabCapacity * (UINT)sizeof(UINT);
    UINT NewBytes = NewCapacity * (UINT)sizeof(UINT);

    UINT* NewBuffer = (UINT*)KernelHeapRealloc(List->SlabUsage, NewBytes);
    if (NewBuffer == NULL) {
        ERROR(TEXT("Realloc failed (required=%u newBytes=%u)"),
              RequiredSlabs,
              NewBytes);
        return FALSE;
    }

    if (NewBytes > OldBytes) {
        MemorySet((LPVOID)(((U8*)NewBuffer) + OldBytes), 0, NewBytes - OldBytes);
    }

    List->SlabUsage = NewBuffer;
    List->SlabCapacity = NewCapacity;
    return TRUE;
}

/************************************************************************/

static BOOL BlockListIsAddressFree(const BLOCK_LIST* List, LINEAR Address) {
    if (List == NULL || Address == 0) {
        return FALSE;
    }

    LPBLOCK_LIST_NODE Current = (LPBLOCK_LIST_NODE)List->FreeListHead;

    while (Current != NULL) {
        if ((LINEAR)Current == Address) {
            return TRUE;
        }
        Current = Current->Next;
    }

    return FALSE;
}

/************************************************************************/

static BOOL BlockListInsertRange(LPBLOCK_LIST List, LINEAR Start, UINT ObjectCount) {
    if (List == NULL || Start == 0 || ObjectCount == 0) {
        return FALSE;
    }

    UINT Index = 0;
    LINEAR Address = Start;

    for (Index = 0; Index < ObjectCount; Index++) {
        LPBLOCK_LIST_NODE Node = (LPBLOCK_LIST_NODE)Address;
        SAFE_USE(Node) {
            Node->Next = (LPBLOCK_LIST_NODE)List->FreeListHead;
        }
        List->FreeListHead = Node;
        List->FreeCount++;
        Address += List->ObjectStride;
    }

    return TRUE;
}

/************************************************************************/

static BOOL BlockListGrowBySlabs(LPBLOCK_LIST List, UINT AdditionalSlabs) {
    if (List == NULL) {
        return FALSE;
    }

    if (AdditionalSlabs == 0) {
        return TRUE;
    }

    if (List->ObjectsPerSlab == 0 || List->SlabSize == 0) {
        ERROR(TEXT("Invalid slab parameters"));
        return FALSE;
    }

    UINT NewSlabCount = 0;
    if (List->SlabCount > GetMaxValue() - AdditionalSlabs) {
        ERROR(TEXT("Slab count overflow (%u + %u)"), List->SlabCount, AdditionalSlabs);
        return FALSE;
    }
    NewSlabCount = List->SlabCount + AdditionalSlabs;

    if (BlockListEnsureSlabMetadata(List, NewSlabCount) == FALSE) {
        return FALSE;
    }

    UINT NewSize = 0;
    if (MultiplySafe(NewSlabCount, List->SlabSize, &NewSize) == FALSE) {
        ERROR(TEXT("Size overflow (slabs=%u size=%u)"), NewSlabCount, List->SlabSize);
        return FALSE;
    }

    UINT OldSize = List->RegionSize;
    LINEAR Base = List->RegionBase;

    if (Base == 0) {
        LINEAR Allocated = AllocKernelRegion(0, NewSize, List->AllocationFlags, TEXT("BlockList"));
        if (Allocated == 0) {
            ERROR(TEXT("AllocKernelRegion failed (size=%u flags=%x)"),
                  NewSize,
                  List->AllocationFlags);
            return FALSE;
        }

        Base = Allocated;
        List->RegionBase = Allocated;
    } else {
        if (ResizeKernelRegion(Base, OldSize, NewSize, List->AllocationFlags) == FALSE) {
            ERROR(TEXT("ResizeRegion failed (old=%u new=%u flags=%x)"),
                  OldSize,
                  NewSize,
                  List->AllocationFlags);
            return FALSE;
        }
    }

    if (NewSize > OldSize) {
        UINT Delta = NewSize - OldSize;
        MemorySet((LPVOID)(Base + OldSize), 0, Delta);

        if (BlockListInsertRange(List, Base + OldSize, AdditionalSlabs * List->ObjectsPerSlab) == FALSE) {
            ERROR(TEXT("Failed to populate free list (slabs=%u)"), AdditionalSlabs);
            return FALSE;
        }
    }

    UINT Index = List->SlabCount;
    while (Index < NewSlabCount) {
        List->SlabUsage[Index] = 0;
        Index++;
    }

    List->RegionSize = NewSize;
    List->SlabCount = NewSlabCount;

    DEBUG(TEXT("Expanded to %u slabs (size=%u free=%u)"),
          List->SlabCount,
          List->RegionSize,
          List->FreeCount);

    return TRUE;
}

/************************************************************************/

static BOOL BlockListTrimTrailingSlabs(LPBLOCK_LIST List) {
    if (List == NULL) {
        return FALSE;
    }

    if (List->SlabCount == 0 || List->SlabUsage == NULL) {
        return TRUE;
    }

    UINT TrailingFree = 0;
    UINT Index = List->SlabCount;

    while (Index > 0) {
        if (List->SlabUsage[Index - 1] != 0) {
            break;
        }
        TrailingFree++;
        Index--;
    }

    if (TrailingFree == 0) {
        return TRUE;
    }

    UINT ObjectsToRemove = 0;
    if (MultiplySafe(TrailingFree, List->ObjectsPerSlab, &ObjectsToRemove) == FALSE) {
        ERROR(TEXT("Object count overflow (slabs=%u per=%u)"),
              TrailingFree,
              List->ObjectsPerSlab);
        return FALSE;
    }

    if (List->FreeCount < ObjectsToRemove) {
        WARNING(TEXT("Trailing slabs not fully free (expected=%u free=%u)"),
                ObjectsToRemove,
                List->FreeCount);
        return FALSE;
    }

    LINEAR Cutoff = List->RegionBase + (List->RegionSize - (TrailingFree * List->SlabSize));
    LPBLOCK_LIST_NODE Previous = NULL;
    LPBLOCK_LIST_NODE Current = (LPBLOCK_LIST_NODE)List->FreeListHead;

    while (Current != NULL) {
        LINEAR Address = (LINEAR)Current;
        LPBLOCK_LIST_NODE Next = Current->Next;
        if (Address >= Cutoff) {
            if (Previous == NULL) {
                List->FreeListHead = Next;
            } else {
                Previous->Next = Next;
            }
            List->FreeCount--;
        } else {
            Previous = Current;
        }
        Current = Next;
    }

    UINT NewSize = List->RegionSize - (TrailingFree * List->SlabSize);

    if (NewSize == 0) {
        if (FreeRegion(List->RegionBase, List->RegionSize) == FALSE) {
            ERROR(TEXT("FreeRegion failed (base=%p size=%u)"),
                  List->RegionBase,
                  List->RegionSize);
            return FALSE;
        }

        List->RegionBase = 0;
        List->RegionSize = 0;
        List->FreeListHead = NULL;
        List->FreeCount = 0;
        List->SlabCount = 0;
        return TRUE;
    }

    if (ResizeKernelRegion(List->RegionBase, List->RegionSize, NewSize, List->AllocationFlags) == FALSE) {
        ERROR(TEXT("ResizeRegion failed (old=%u new=%u flags=%x)"),
              List->RegionSize,
              NewSize,
              List->AllocationFlags);
        return FALSE;
    }

    List->RegionSize = NewSize;
    List->SlabCount -= TrailingFree;

    DEBUG(TEXT("Shrunk to %u slabs (size=%u free=%u)"),
          List->SlabCount,
          List->RegionSize,
          List->FreeCount);

    return TRUE;
}

/************************************************************************/

BOOL BlockListInit(LPBLOCK_LIST List,
                   UINT ObjectSize,
                   UINT ObjectsPerSlab,
                   UINT InitialSlabCount,
                   U32 Flags) {
    if (List == NULL || ObjectSize == 0) {
        ERROR(TEXT("Invalid parameters (list=%p size=%u)"), List, ObjectSize);
        return FALSE;
    }

    MemorySet(List, 0, (UINT)sizeof(BLOCK_LIST));

    UINT AlignedStride = ObjectSize;
    if (AlignedStride < (UINT)sizeof(LINEAR)) {
        AlignedStride = (UINT)sizeof(LINEAR);
    }

    if (AlignValue(AlignedStride, (UINT)sizeof(LINEAR), &AlignedStride) == FALSE) {
        ERROR(TEXT("Failed to align stride (%u)"), ObjectSize);
        return FALSE;
    }

    if (ObjectsPerSlab == 0) {
        UINT MinimumObjects = PAGE_SIZE / AlignedStride;
        ObjectsPerSlab = (MinimumObjects == 0) ? 1 : MinimumObjects;
    }

    UINT RawSlabSize = 0;

    if (MultiplySafe(AlignedStride, ObjectsPerSlab, &RawSlabSize) == FALSE) {
        ERROR(TEXT("Slab size overflow (stride=%u count=%u)"), AlignedStride, ObjectsPerSlab);
        return FALSE;
    }

    UINT SlabSize = 0;
    if (AlignValue(RawSlabSize, PAGE_SIZE, &SlabSize) == FALSE) {
        ERROR(TEXT("Failed to align slab size (%u)"), RawSlabSize);
        return FALSE;
    }

    UINT EffectiveObjects = SlabSize / AlignedStride;

    List->RegionBase = 0;
    List->RegionSize = 0;
    List->ObjectSize = ObjectSize;
    List->ObjectStride = AlignedStride;
    List->ObjectsPerSlab = EffectiveObjects;
    List->SlabSize = SlabSize;
    List->SlabCount = 0;
    List->SlabCapacity = 0;
    List->UsedCount = 0;
    List->FreeCount = 0;
    List->HighWaterMark = 0;
    List->AllocationFlags = Flags | ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE;
    List->FreeListHead = NULL;
    List->SlabUsage = NULL;

    DEBUG(TEXT("stride=%u slabSize=%u objectsPerSlab=%u initialSlabs=%u flags=%x"),
          List->ObjectStride,
          List->SlabSize,
          List->ObjectsPerSlab,
          InitialSlabCount,
          List->AllocationFlags);

    if (BlockListEnsureSlabMetadata(List, (InitialSlabCount == 0) ? 1 : InitialSlabCount) == FALSE) {
        MemorySet(List, 0, (UINT)sizeof(BLOCK_LIST));
        return FALSE;
    }

    if (InitialSlabCount != 0) {
        if (BlockListGrowBySlabs(List, InitialSlabCount) == FALSE) {
            BlockListFinalize(List);
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

void BlockListFinalize(LPBLOCK_LIST List) {
    if (List == NULL) {
        return;
    }

    if (List->RegionBase != 0 && List->RegionSize != 0) {
        if (FreeRegion(List->RegionBase, List->RegionSize) == FALSE) {
            WARNING(TEXT("FreeRegion failed (base=%p size=%u)"),
                    List->RegionBase,
                    List->RegionSize);
        }
    }

    if (List->SlabUsage != NULL) {
        KernelHeapFree(List->SlabUsage);
    }

    MemorySet(List, 0, (UINT)sizeof(BLOCK_LIST));
}

/************************************************************************/

LINEAR BlockListAllocate(LPBLOCK_LIST List) {
    if (List == NULL) {
        return 0;
    }

    if (List->FreeCount == 0) {
        if (BlockListGrowBySlabs(List, 1) == FALSE) {
            ERROR(TEXT("Growth failed"));
            return 0;
        }
    }

    LPBLOCK_LIST_NODE Node = (LPBLOCK_LIST_NODE)List->FreeListHead;
    if (Node == NULL) {
        ERROR(TEXT("Free list empty after grow"));
        return 0;
    }

    List->FreeListHead = Node->Next;
    if (List->FreeCount > 0) {
        List->FreeCount--;
    }

    List->UsedCount++;
    if (List->UsedCount > List->HighWaterMark) {
        List->HighWaterMark = List->UsedCount;
    }

    LINEAR Address = (LINEAR)Node;
    if (Address < List->RegionBase || Address >= List->RegionBase + List->RegionSize) {
        ERROR(TEXT("Corrupted node address %p"), Address);
        return 0;
    }

    UINT Offset = Address - List->RegionBase;
    UINT SlabIndex = Offset / List->SlabSize;

    if (SlabIndex < List->SlabCount && List->SlabUsage != NULL) {
        List->SlabUsage[SlabIndex]++;
    }

    MemorySet((LPVOID)Address, 0, List->ObjectStride);

    return Address;
}

/************************************************************************/

BOOL BlockListFree(LPBLOCK_LIST List, LINEAR Address) {
    if (List == NULL || Address == 0) {
        return FALSE;
    }

    if (List->RegionBase == 0 || List->RegionSize == 0) {
        WARNING(TEXT("No region mapped (address=%p)"), Address);
        return FALSE;
    }

    if (Address < List->RegionBase || Address >= List->RegionBase + List->RegionSize) {
        WARNING(TEXT("Address outside range (address=%p base=%p size=%u)"),
                Address,
                List->RegionBase,
                List->RegionSize);
        return FALSE;
    }

    UINT Offset = Address - List->RegionBase;

    if ((Offset % List->ObjectStride) != 0u) {
        WARNING(TEXT("Address not aligned to stride (address=%p stride=%u)"),
                Address,
                List->ObjectStride);
        return FALSE;
    }

    if (List->UsedCount == 0) {
        WARNING(TEXT("No allocations in use"));
        return FALSE;
    }

    if (BlockListIsAddressFree(List, Address)) {
        WARNING(TEXT("Double free detected at %p"), Address);
        return FALSE;
    }

    UINT SlabIndex = Offset / List->SlabSize;
    if (SlabIndex >= List->SlabCount) {
        ERROR(TEXT("Slab index out of range (%u >= %u)"), SlabIndex, List->SlabCount);
        return FALSE;
    }

    LPBLOCK_LIST_NODE Node = (LPBLOCK_LIST_NODE)Address;
    SAFE_USE(Node) {
        Node->Next = (LPBLOCK_LIST_NODE)List->FreeListHead;
    }

    List->FreeListHead = Node;
    List->FreeCount++;
    List->UsedCount--;

    if (List->SlabUsage != NULL) {
        if (List->SlabUsage[SlabIndex] > 0) {
            List->SlabUsage[SlabIndex]--;
        } else {
            WARNING(TEXT("Slab usage underflow at slab %u"), SlabIndex);
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL BlockListReserve(LPBLOCK_LIST List, UINT DesiredFree) {
    if (List == NULL) {
        return FALSE;
    }

    if (DesiredFree == 0) {
        return TRUE;
    }

    if (List->FreeCount >= DesiredFree) {
        return TRUE;
    }

    if (List->ObjectsPerSlab == 0) {
        ERROR(TEXT("Invalid ObjectsPerSlab"));
        return FALSE;
    }

    UINT Missing = DesiredFree - List->FreeCount;

    UINT Rounded = Missing + List->ObjectsPerSlab - 1;
    if (Rounded < Missing) {
        ERROR(TEXT("Rounded overflow (missing=%u slab=%u)"),
              Missing,
              List->ObjectsPerSlab);
        return FALSE;
    }

    UINT AdditionalSlabs = Rounded / List->ObjectsPerSlab;
    if (AdditionalSlabs == 0) {
        AdditionalSlabs = 1;
    }

    return BlockListGrowBySlabs(List, AdditionalSlabs);
}

/************************************************************************/

BOOL BlockListReleaseUnused(LPBLOCK_LIST List) { return BlockListTrimTrailingSlabs(List); }

/************************************************************************/

UINT BlockListGetCapacity(const BLOCK_LIST* List) {
    if (List == NULL || List->ObjectsPerSlab == 0) {
        return 0;
    }

    UINT Result = 0;
    if (MultiplySafe(List->SlabCount, List->ObjectsPerSlab, &Result) == FALSE) {
        return 0;
    }

    return Result;
}

/************************************************************************/

UINT BlockListGetUsage(const BLOCK_LIST* List) {
    if (List == NULL) {
        return 0;
    }
    return List->UsedCount;
}

/************************************************************************/

UINT BlockListGetFreeCount(const BLOCK_LIST* List) {
    if (List == NULL) {
        return 0;
    }
    return List->FreeCount;
}

/************************************************************************/

UINT BlockListGetSlabCount(const BLOCK_LIST* List) {
    if (List == NULL) {
        return 0;
    }
    return List->SlabCount;
}

/************************************************************************/
