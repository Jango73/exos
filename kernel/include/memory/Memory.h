
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


    Memory manager

\************************************************************************/

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include "Base.h"
#include "core/Driver.h"
#include "utils/List.h"
#include "arch/Memory.h"

/************************************************************************/
// #defines

#define MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_COMMIT ((U32)0x00000001)
#define MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_IO ((U32)0x00000002)
#define MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_FIXED ((U32)0x00000004)
#define MEMORY_REGION_TAG_MAX 32

/************************************************************************/
// typedefs

typedef struct tag_PROCESS PROCESS, *LPPROCESS;

typedef enum {
    MEMORY_REGION_GRANULARITY_4K = 0,
    MEMORY_REGION_GRANULARITY_2M = 1,
    MEMORY_REGION_GRANULARITY_1G = 2
} MEMORY_REGION_GRANULARITY;

typedef struct tag_MEMORY_REGION_DESCRIPTOR MEMORY_REGION_DESCRIPTOR, *LPMEMORY_REGION_DESCRIPTOR;

struct tag_MEMORY_REGION_DESCRIPTOR {
    LISTNODE_FIELDS
    LINEAR Base;
    LINEAR CanonicalBase;
    PHYSICAL PhysicalBase;
    UINT Size;
    UINT PageCount;
    U32 Flags;
    U32 Attributes;
    MEMORY_REGION_GRANULARITY Granularity;
    STR Tag[MEMORY_REGION_TAG_MAX];
};

typedef struct tag_MEMORY_REGION_LIST {
    LPMEMORY_REGION_DESCRIPTOR Head;
    LPMEMORY_REGION_DESCRIPTOR Tail;
    UINT Count;
} MEMORY_REGION_LIST, *LPMEMORY_REGION_LIST;

/************************************************************************/

LPMEMORY_REGION_LIST GetCurrentMemoryRegionList(void);
void MemoryRegionDescriptorAssignCurrentOwner(LPMEMORY_REGION_DESCRIPTOR Descriptor);

// External symbols
// Initializes the memory manager
void InitializeMemoryManager(void);

// Architecture helpers
void UpdateKernelMemoryMetricsFromMultibootMap(void);
void MarkUsedPhysicalMemory(void);
void SetLoaderReservedPhysicalRange(PHYSICAL Start, PHYSICAL End);
void SetPhysicalAllocatorMetadataRange(PHYSICAL Start, PHYSICAL End);
void SetAllocPhysicalPageTraceEnabled(BOOL Enabled);
BOOL IsPhysicalMemoryRangeFree(PHYSICAL BaseAddress, UINT Size);
BOOL FindAvailableMemoryRange(PHYSICAL MinimumAddress, UINT Size, PHYSICAL* OutAddress);
BOOL FindAvailableMemoryRangeInWindow(
    PHYSICAL MinimumAddress,
    PHYSICAL MaximumAddress,
    PHYSICAL ExcludedStart,
    PHYSICAL ExcludedEnd,
    UINT Size,
    PHYSICAL* OutAddress);

// Uses temp page tables to get access to random physical pages
LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage4(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage5(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage6(PHYSICAL Physical);
BOOL ReadPhysicalMemory(PHYSICAL PhysicalAddress, LPVOID Buffer, UINT Length);

// Allocates physical space for a new page directory
PHYSICAL AllocPageDirectory(void);

// Allocates physical space for a new page directory for userland processes
PHYSICAL AllocUserPageDirectory(void);

// Allocates a physical page
PHYSICAL AllocPhysicalPage(void);

// Frees a physical page
void FreePhysicalPage(PHYSICAL Page);

// Returns TRUE if a pointer is an valid address (mapped in the calling process space)
BOOL IsValidMemory(LINEAR Pointer);

// Attempts to mirror kernel mappings into the current address space for a faulting kernel address
BOOL ResolveKernelPageFault(LINEAR FaultAddress);

// Returns the physical address for a given virtual address
PHYSICAL MapLinearToPhysical(LINEAR Address);

// Allocates physical space for a new region of virtual memory
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag);
LINEAR AllocRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag);
BOOL CommitRegionRange(LINEAR Base, UINT Size, U32 Flags);
BOOL CommitRegionRangeForProcess(LPPROCESS TrackingProcess, LINEAR Base, UINT Size, U32 Flags);

// Resizes an existing region of virtual memory
BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags);
BOOL ResizeRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags);

// Frees physical space of a region of virtual memory
BOOL FreeRegion(LINEAR Base, UINT Size);
BOOL FreeRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, UINT Size);

// Map/unmap a physical MMIO region (BAR or Base Address Register) as Uncached Read/Write
LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size);
LINEAR MapFramebufferMemory(PHYSICAL PhysicalBase, UINT Size);
BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size);

// Kernel region allocation wrapper - automatically uses VMA_KERNEL and AT_OR_OVER
LINEAR AllocKernelRegion(PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag);

// Kernel region resize wrapper - automatically uses VMA_KERNEL and AT_OR_OVER
LINEAR ResizeKernelRegion(LINEAR Base, UINT Size, UINT NewSize, U32 Flags);

// Returns the preferred base address for the kernel heap for the running architecture
LINEAR GetKernelHeapPreferredBase(UINT HeapSize);

/************************************************************************/

#endif  // MEMORY_H_INCLUDED
