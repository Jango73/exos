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


    Memory region descriptors

\************************************************************************/

#include "memory/Memory-Descriptors.h"

#include "console/Console.h"
#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "system/System.h"

/************************************************************************/
// Region descriptor tracking state

BOOL DATA_SECTION G_RegionDescriptorsEnabled = FALSE;
BOOL DATA_SECTION G_RegionDescriptorBootstrap = FALSE;
LPMEMORY_REGION_DESCRIPTOR DATA_SECTION G_FreeRegionDescriptors = NULL;
UINT DATA_SECTION G_FreeRegionDescriptorCount = 0;
UINT DATA_SECTION G_TotalRegionDescriptorCount = 0;
UINT DATA_SECTION G_RegionDescriptorPages = 0;

/************************************************************************/
/**
 * @brief Allocate and chain one descriptor slab.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL GrowDescriptorSlab(void) {
    PHYSICAL Physical = AllocPhysicalPage();

    if (Physical == NULL) {
        ERROR(TEXT("No physical page available"));
        return FALSE;
    }


    G_RegionDescriptorBootstrap = TRUE;

    LINEAR Linear = AllocKernelRegion(
        Physical,
        PAGE_SIZE,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
        TEXT("RegionDescriptorSlab"));
    G_RegionDescriptorBootstrap = FALSE;

    if (Linear == NULL) {
        ERROR(TEXT("Failed to map descriptor slab"));
        FreePhysicalPage(Physical);
        return FALSE;
    }


    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);


    UINT Capacity = (UINT)(PAGE_SIZE / (UINT)sizeof(MEMORY_REGION_DESCRIPTOR));
    LPMEMORY_REGION_DESCRIPTOR DescriptorArray = (LPMEMORY_REGION_DESCRIPTOR)(LINEAR)Linear;

    for (UINT Index = 0; Index < Capacity; Index++) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = DescriptorArray + Index;
        Descriptor->Next = (LPLISTNODE)G_FreeRegionDescriptors;
        Descriptor->Prev = NULL;
        G_FreeRegionDescriptors = Descriptor;
        G_FreeRegionDescriptorCount++;
        G_TotalRegionDescriptorCount++;
    }

    G_RegionDescriptorPages++;

    return TRUE;
}

/************************************************************************/
/**
 * @brief Allocate a new descriptor slab when the free list runs empty.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL EnsureDescriptorSlab(void) {
    if (G_FreeRegionDescriptors != NULL) {
        return TRUE;
    }

    return GrowDescriptorSlab();
}

/************************************************************************/
/**
 * @brief Obtain an initialized descriptor from the free list.
 * @return Descriptor pointer or NULL when exhausted.
 */
static LPMEMORY_REGION_DESCRIPTOR AcquireRegionDescriptor(void) {
    if (G_FreeRegionDescriptors == NULL) {
        if (EnsureDescriptorSlab() == FALSE) {
            return NULL;
        }
    }

    LPMEMORY_REGION_DESCRIPTOR Descriptor = G_FreeRegionDescriptors;
    if (Descriptor != NULL) {
        G_FreeRegionDescriptors = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;
        if (G_FreeRegionDescriptors != NULL) {
            G_FreeRegionDescriptors->Prev = NULL;
        }
        Descriptor->Next = NULL;
        Descriptor->Prev = NULL;
        if (G_FreeRegionDescriptorCount != 0) {
            G_FreeRegionDescriptorCount--;
        }
    }

    return Descriptor;
}

/************************************************************************/
/**
 * @brief Return a descriptor to the free list.
 * @param Descriptor Descriptor to recycle.
 */
static void ReleaseRegionDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    if (Descriptor == NULL) {
        return;
    }

    Descriptor->TypeID = KOID_NONE;
    Descriptor->References = 0;
    Descriptor->InstanceID = U64_Make(0, 0);
    Descriptor->OwnerProcess = NULL;
    Descriptor->Base = 0;
    Descriptor->CanonicalBase = 0;
    Descriptor->PhysicalBase = 0;
    Descriptor->Size = 0;
    Descriptor->PageCount = 0;
    Descriptor->Flags = 0;
    Descriptor->Attributes = 0;
    Descriptor->Granularity = MEMORY_REGION_GRANULARITY_4K;
    Descriptor->Tag[0] = STR_NULL;

    Descriptor->Next = (LPLISTNODE)G_FreeRegionDescriptors;
    Descriptor->Prev = NULL;
    G_FreeRegionDescriptors = Descriptor;
    G_FreeRegionDescriptorCount++;
}

/************************************************************************/
/**
 * @brief Insert a descriptor into the region list sorted by base address.
 * @param List Target list.
 * @param Descriptor Descriptor to link.
 */
static void InsertDescriptorOrdered(LPMEMORY_REGION_LIST List, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    LPMEMORY_REGION_DESCRIPTOR Current = List->Head;
    LPMEMORY_REGION_DESCRIPTOR Previous = NULL;

    while (Current != NULL && Current->CanonicalBase < Descriptor->CanonicalBase) {
        Previous = Current;
        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    Descriptor->Next = (LPLISTNODE)Current;
    Descriptor->Prev = (LPLISTNODE)Previous;

    if (Current != NULL) {
        Current->Prev = (LPLISTNODE)Descriptor;
    } else {
        List->Tail = Descriptor;
    }

    if (Previous != NULL) {
        Previous->Next = (LPLISTNODE)Descriptor;
    } else {
        List->Head = Descriptor;
    }

    List->Count++;
}

/************************************************************************/
/**
 * @brief Remove a descriptor from the region list.
 * @param List Target list.
 * @param Descriptor Descriptor to unlink.
 */
static void RemoveDescriptor(LPMEMORY_REGION_LIST List, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    LPMEMORY_REGION_DESCRIPTOR Prev = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Prev;
    LPMEMORY_REGION_DESCRIPTOR Next = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;

    if (Prev != NULL) {
        Prev->Next = (LPLISTNODE)Next;
    } else {
        List->Head = Next;
    }

    if (Next != NULL) {
        Next->Prev = (LPLISTNODE)Prev;
    } else {
        List->Tail = Prev;
    }

    Descriptor->Next = NULL;
    Descriptor->Prev = NULL;

    if (List->Count != 0) {
        List->Count--;
    }
}

/************************************************************************/
/**
 * @brief Find the descriptor that starts at the specified canonical base.
 * @param List Target list.
 * @param CanonicalBase Canonical linear base.
 * @return Descriptor pointer or NULL when absent.
 */
LPMEMORY_REGION_DESCRIPTOR FindDescriptorForBase(LPMEMORY_REGION_LIST List, LINEAR CanonicalBase) {
    LPMEMORY_REGION_DESCRIPTOR Current = List->Head;

    while (Current != NULL) {
        if (Current->CanonicalBase == CanonicalBase) {
            return Current;
        }
        if (Current->CanonicalBase > CanonicalBase) {
            break;
        }
        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    return NULL;
}

/************************************************************************/
/**
 * @brief Find the descriptor covering a given canonical address.
 * @param List Target list.
 * @param CanonicalBase Address to resolve.
 * @return Descriptor pointer or NULL when no descriptor covers the address.
 */
LPMEMORY_REGION_DESCRIPTOR FindDescriptorCoveringAddress(
    LPMEMORY_REGION_LIST List,
    LINEAR CanonicalBase) {
    LPMEMORY_REGION_DESCRIPTOR Current = List->Head;

    while (Current != NULL) {
        LINEAR RegionStart = Current->CanonicalBase;
        LINEAR RegionEnd = RegionStart + (LINEAR)Current->Size;

        if (CanonicalBase >= RegionStart && CanonicalBase < RegionEnd) {
            return Current;
        }

        if (RegionStart > CanonicalBase) {
            break;
        }

        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    return NULL;
}

/************************************************************************/
/**
 * @brief Refresh the granularity metadata stored in a descriptor.
 * @param Descriptor Descriptor to update.
 */
void RefreshDescriptorGranularity(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    if (Descriptor == NULL) {
        return;
    }

    Descriptor->Granularity = ComputeDescriptorGranularity(
        Descriptor->CanonicalBase,
        Descriptor->PageCount);
}

/************************************************************************/
/**
 * @brief Extend an existing descriptor when pages are appended.
 * @param Descriptor Descriptor to update.
 * @param AdditionalPages Number of new pages.
 */
void ExtendDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor, UINT AdditionalPages) {
    if (Descriptor == NULL || AdditionalPages == 0) {
        return;
    }

    UINT AdditionalBytes = AdditionalPages << PAGE_SIZE_MUL;
    Descriptor->Size += AdditionalBytes;
    Descriptor->PageCount += AdditionalPages;
    RefreshDescriptorGranularity(Descriptor);

}

/************************************************************************/

/**
 * @brief Resolve the descriptor list for one tracking owner.
 * @param Process Explicit tracking owner, or NULL to use the current process.
 * @return Descriptor list pointer or NULL when unavailable.
 */
static LPMEMORY_REGION_LIST ResolveTrackingList(LPPROCESS Process) {
    if (Process != NULL) {
        return GetProcessMemoryRegionList(Process);
    }

    return GetCurrentMemoryRegionList();
}

/************************************************************************/
/**
 * @brief Register a freshly allocated region descriptor.
 * @param OwnerProcess Explicit owner process for the descriptor.
 * @param Base Canonical base address.
 * @param NumPages Number of pages covered.
 * @param Target Physical base when fixed, 0 otherwise.
 * @param Flags Allocation flags.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL RegisterRegionDescriptor(LPPROCESS OwnerProcess, LPMEMORY_REGION_LIST List, LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags, LPCSTR Tag) {
    LPMEMORY_REGION_DESCRIPTOR Descriptor = AcquireRegionDescriptor();

    if (Descriptor == NULL) {
        ERROR(TEXT("Descriptor pool exhausted (base=%p sizePages=%u)"),
            (LPVOID)Base,
            NumPages);
        return FALSE;
    }

    Descriptor->TypeID = KOID_MEMORY_REGION_DESCRIPTOR;
    Descriptor->References = 1;
    Descriptor->InstanceID = U64_Make(0, 0);
    MemoryRegionDescriptorAssignOwner(Descriptor, OwnerProcess);
    Descriptor->CanonicalBase = CanonicalizeLinearAddress(Base);
    Descriptor->Base = Descriptor->CanonicalBase;
    Descriptor->PhysicalBase = Target;
    Descriptor->PageCount = NumPages;
    Descriptor->Size = NumPages << PAGE_SIZE_MUL;
    Descriptor->Flags = Flags;
    RefreshDescriptorGranularity(Descriptor);

    U32 Attributes = 0;
    if ((Flags & ALLOC_PAGES_COMMIT) != 0) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_COMMIT;
    }
    if ((Flags & ALLOC_PAGES_IO) != 0) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_IO;
    }
    if ((Flags & (ALLOC_PAGES_IO | ALLOC_PAGES_FIXED)) != 0) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_FIXED;
    }
    Descriptor->Attributes = Attributes;
    if (Tag != NULL) {
        StringCopyLimit(Descriptor->Tag, Tag, MEMORY_REGION_TAG_MAX);
    } else {
        Descriptor->Tag[0] = STR_NULL;
    }

    InsertDescriptorOrdered(List, Descriptor);

    return TRUE;
}

/************************************************************************/
/**
 * @brief Update descriptors to account for a freed virtual span.
 * @param Base Canonical base of the range being freed.
 * @param SizeBytes Size of the freed range in bytes.
 */
void UpdateDescriptorsForFree(LPMEMORY_REGION_LIST List, LINEAR Base, UINT SizeBytes) {
    if (SizeBytes == 0) {
        return;
    }

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    LINEAR Cursor = CanonicalBase;
    UINT RemainingBytes = SizeBytes;

    while (RemainingBytes != 0) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorCoveringAddress(List, Cursor);
        if (Descriptor == NULL) {
            WARNING(TEXT("Missing descriptor for base=%p size=%u"),
                (LPVOID)Cursor,
                RemainingBytes);
            break;
        }

        LINEAR RegionStart = Descriptor->CanonicalBase;
        LINEAR RegionEnd = RegionStart + (LINEAR)Descriptor->Size;
        LINEAR FreeStart = Cursor;
        LINEAR FreeEnd = Cursor + (LINEAR)RemainingBytes;
        if (FreeEnd > RegionEnd) {
            FreeEnd = RegionEnd;
        }

        UINT SegmentBytes = (UINT)(FreeEnd - FreeStart);
        if (SegmentBytes == 0) {
            break;
        }

        BOOL EntireRegion = (FreeStart == RegionStart && FreeEnd == RegionEnd);
        BOOL TrimHead = (FreeStart == RegionStart && FreeEnd < RegionEnd);
        BOOL TrimTail = (FreeStart > RegionStart && FreeEnd == RegionEnd);

        if (EntireRegion) {
            RemoveDescriptor(List, Descriptor);
            ReleaseRegionDescriptor(Descriptor);
        } else if (TrimTail) {
            UINT Remaining = (UINT)(FreeStart - RegionStart);
            if (Remaining == 0) {
                RemoveDescriptor(List, Descriptor);
                ReleaseRegionDescriptor(Descriptor);
            } else {
                Descriptor->Size = Remaining;
                Descriptor->PageCount = Remaining >> PAGE_SIZE_MUL;
                RefreshDescriptorGranularity(Descriptor);
            }
        } else if (TrimHead) {
            UINT Remaining = (UINT)(RegionEnd - FreeEnd);
            RemoveDescriptor(List, Descriptor);
            Descriptor->Base = FreeEnd;
            Descriptor->CanonicalBase = FreeEnd;
            if (Descriptor->PhysicalBase != 0) {
                Descriptor->PhysicalBase += (PHYSICAL)(FreeEnd - RegionStart);
            }
            Descriptor->Size = Remaining;
            Descriptor->PageCount = Remaining >> PAGE_SIZE_MUL;
            RefreshDescriptorGranularity(Descriptor);
            if (Descriptor->Size == 0) {
                ReleaseRegionDescriptor(Descriptor);
            } else {
                InsertDescriptorOrdered(List, Descriptor);
            }
        } else {
            UINT LeftBytes = (UINT)(FreeStart - RegionStart);
            UINT RightBytes = (UINT)(RegionEnd - FreeEnd);
            LPMEMORY_REGION_DESCRIPTOR Right = AcquireRegionDescriptor();

            if (Right == NULL) {
                ERROR(TEXT("Unable to split descriptor at %p"),
                    (LPVOID)FreeStart);
                ConsolePanic(TEXT("Descriptor split allocation failed"));
            }

            RemoveDescriptor(List, Descriptor);

            Descriptor->Size = LeftBytes;
            Descriptor->PageCount = LeftBytes >> PAGE_SIZE_MUL;
            PHYSICAL DescriptorPhysicalBase = Descriptor->PhysicalBase;
            RefreshDescriptorGranularity(Descriptor);

            if (Descriptor->Size == 0) {
                ReleaseRegionDescriptor(Descriptor);
            } else {
                InsertDescriptorOrdered(List, Descriptor);
            }

            Right->TypeID = KOID_MEMORY_REGION_DESCRIPTOR;
            Right->References = 1;
            Right->InstanceID = U64_Make(0, 0);
            Right->OwnerProcess = Descriptor->OwnerProcess;
            Right->Base = FreeEnd;
            Right->CanonicalBase = FreeEnd;
            Right->PhysicalBase = DescriptorPhysicalBase;
            if (Right->PhysicalBase != 0) {
                Right->PhysicalBase += (PHYSICAL)(FreeEnd - RegionStart);
            }
            Right->Size = RightBytes;
            Right->PageCount = RightBytes >> PAGE_SIZE_MUL;
            Right->Flags = Descriptor->Flags;
            Right->Attributes = Descriptor->Attributes;
            Right->Granularity = Descriptor->Granularity;
            StringCopyLimit(Right->Tag, Descriptor->Tag, MEMORY_REGION_TAG_MAX);
            RefreshDescriptorGranularity(Right);

            if (Right->Size == 0) {
                ReleaseRegionDescriptor(Right);
            } else {
                InsertDescriptorOrdered(List, Right);
            }

        }

        if (RemainingBytes >= SegmentBytes) {
            RemainingBytes -= SegmentBytes;
        } else {
            RemainingBytes = 0;
        }

        Cursor = FreeEnd;
    }
}

/************************************************************************/
/**
 * @brief Initialize the descriptor tracking subsystem.
 */
void InitializeRegionDescriptorTracking(void) {
    if (G_RegionDescriptorsEnabled == TRUE) {
        return;
    }


    if (EnsureDescriptorSlab() == FALSE) {
        ERROR(TEXT("Initial slab allocation failed"));
        return;
    }
    G_RegionDescriptorsEnabled = TRUE;

    DEBUG(TEXT("Enabled (free=%u total=%u)"),
        G_FreeRegionDescriptorCount,
        G_TotalRegionDescriptorCount);
}

/************************************************************************/
/**
 * @brief Track a successful region allocation in the descriptor list.
 * @param Base Base address for the allocation.
 * @param Target Physical base when fixed, 0 otherwise.
 * @param Size Size in bytes.
 * @param Flags Allocation flags.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackAlloc(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    return RegionTrackAllocForProcess(NULL, Base, Target, Size, Flags, Tag);
}

/************************************************************************/
/**
 * @brief Track a successful region allocation in one process descriptor list.
 * @param Process Tracking owner, or NULL for the current process.
 * @param Base Base address for the allocation.
 * @param Target Physical base when fixed, 0 otherwise.
 * @param Size Size in bytes.
 * @param Flags Allocation flags.
 * @param Tag Optional allocation tag.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackAllocForProcess(LPPROCESS Process, LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    LPMEMORY_REGION_LIST List = ResolveTrackingList(Process);

    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (Size == 0) {
        return FALSE;
    }

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    if (NumPages == 0) {
        return FALSE;
    }

    if (List == NULL) {
        return FALSE;
    }

    return RegisterRegionDescriptor(Process, List, Base, NumPages, Target, Flags, Tag);
}

/************************************************************************/
/**
 * @brief Track a successful region release in the descriptor list.
 * @param Base Base address for the free.
 * @param Size Size in bytes.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackFree(LINEAR Base, UINT Size) {
    return RegionTrackFreeForProcess(NULL, Base, Size);
}

/************************************************************************/
/**
 * @brief Track a successful region release in one process descriptor list.
 * @param Process Tracking owner, or NULL for the current process.
 * @param Base Base address for the free.
 * @param Size Size in bytes.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackFreeForProcess(LPPROCESS Process, LINEAR Base, UINT Size) {
    LPMEMORY_REGION_LIST List = ResolveTrackingList(Process);

    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (Size == 0) {
        return FALSE;
    }

    if (List == NULL) {
        return FALSE;
    }

    UpdateDescriptorsForFree(List, Base, Size);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Track a successful resize in the descriptor list.
 * @param Base Base address for the region.
 * @param OldSize Previous size in bytes.
 * @param NewSize New size in bytes.
 * @param Flags Allocation flags.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackResize(LINEAR Base, UINT OldSize, UINT NewSize, U32 Flags) {
    return RegionTrackResizeForProcess(NULL, Base, OldSize, NewSize, Flags);
}

/************************************************************************/
/**
 * @brief Mark descriptors overlapping one committed range as committed.
 * @param Base Base address for the committed range.
 * @param Size Size of the committed range.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackCommit(LINEAR Base, UINT Size) {
    return RegionTrackCommitForProcess(NULL, Base, Size);
}

/************************************************************************/
/**
 * @brief Track a successful resize in one process descriptor list.
 * @param Process Tracking owner, or NULL for the current process.
 * @param Base Base address for the region.
 * @param OldSize Previous size in bytes.
 * @param NewSize New size in bytes.
 * @param Flags Allocation flags.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackResizeForProcess(LPPROCESS Process, LINEAR Base, UINT OldSize, UINT NewSize, U32 Flags) {
    LPMEMORY_REGION_LIST List = ResolveTrackingList(Process);

    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (OldSize == NewSize) {
        return TRUE;
    }

    if (NewSize < OldSize) {
        LINEAR FreeBase = Base + (LINEAR)NewSize;
        UINT FreeSize = OldSize - NewSize;
        if (List == NULL) {
            return FALSE;
        }
        UpdateDescriptorsForFree(List, FreeBase, FreeSize);
        return TRUE;
    }

    UINT AdditionalSize = NewSize - OldSize;
    UINT AdditionalPages = (AdditionalSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    if (AdditionalPages == 0) {
        return TRUE;
    }

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    if (List == NULL) {
        return FALSE;
    }

    LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorForBase(List, CanonicalBase);
    if (Descriptor == NULL) {
        return RegisterRegionDescriptor(Process, List, Base, (NewSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL, 0, Flags, NULL);
    }

    ExtendDescriptor(Descriptor, AdditionalPages);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Mark one tracked range as committed.
 * @param Process Tracking owner, or NULL for the current process.
 * @param Base Base address for the committed range.
 * @param Size Size of the committed range.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackCommitForProcess(LPPROCESS Process, LINEAR Base, UINT Size) {
    LPMEMORY_REGION_LIST List = ResolveTrackingList(Process);
    LINEAR Cursor;
    LINEAR End;

    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (List == NULL || Size == 0) {
        return FALSE;
    }

    Cursor = CanonicalizeLinearAddress(Base);
    End = Cursor + (LINEAR)Size;
    if (End < Cursor) {
        return FALSE;
    }

    while (Cursor < End) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorCoveringAddress(List, Cursor);
        LINEAR RegionEnd;

        if (Descriptor == NULL) {
            return FALSE;
        }

        Descriptor->Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_COMMIT;
        RegionEnd = Descriptor->CanonicalBase + (LINEAR)Descriptor->Size;
        if (RegionEnd <= Cursor) {
            return FALSE;
        }

        Cursor = RegionEnd;
    }

    return TRUE;
}
