
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


    BlockList - Fixed Size Slab Allocator

\************************************************************************/

#ifndef BLOCK_LIST_H_INCLUDED
#define BLOCK_LIST_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#define BLOCK_LIST_TAG_MAX 32

/************************************************************************/
// typedefs

typedef struct tag_BLOCK_LIST BLOCK_LIST, *LPBLOCK_LIST;

struct tag_BLOCK_LIST {
    LINEAR RegionBase;
    UINT RegionSize;
    UINT ObjectSize;
    UINT ObjectStride;
    UINT ObjectsPerSlab;
    UINT SlabSize;
    UINT SlabCount;
    UINT SlabCapacity;
    UINT UsedCount;
    UINT FreeCount;
    UINT HighWaterMark;
    U32 AllocationFlags;
    LPVOID FreeListHead;
    UINT* SlabUsage;
    STR Tag[BLOCK_LIST_TAG_MAX];  // Owner tag forwarded to allocated memory regions
};

/************************************************************************/
// External symbols

/**
 * @brief Initialize a BlockList allocator for fixed-size objects.
 *
 * @param List Pointer to the allocator descriptor to initialize.
 * @param ObjectSize Size in bytes for each object to allocate.
 * @param ObjectsPerSlab Requested number of objects per slab before alignment.
 * @param InitialSlabCount Number of slabs to pre-allocate (can be zero).
 * @param Flags Allocation flags to forward to AllocRegion/ResizeRegion.
 * @param Tag Owner tag copied into the allocated memory regions.
 * @return TRUE on success, FALSE if initialization failed.
 */
BOOL BlockListInit(
    LPBLOCK_LIST List, UINT ObjectSize, UINT ObjectsPerSlab, UINT InitialSlabCount, U32 Flags, LPCSTR Tag);

/************************************************************************/

/**
 * @brief Release all resources owned by a BlockList allocator.
 *
 * @param List Pointer to the allocator descriptor to finalize.
 */
void BlockListFinalize(LPBLOCK_LIST List);

/************************************************************************/

/**
 * @brief Allocate a new object from the BlockList.
 *
 * @param List Pointer to the allocator descriptor.
 * @return LINEAR address of the allocated object, or 0 on failure.
 */
LINEAR BlockListAllocate(LPBLOCK_LIST List);

/************************************************************************/

/**
 * @brief Return an object to the BlockList allocator.
 *
 * @param List Pointer to the allocator descriptor.
 * @param Address LINEAR address previously obtained from BlockListAllocate.
 * @return TRUE on success, FALSE if the address is invalid or already freed.
 */
BOOL BlockListFree(LPBLOCK_LIST List, LINEAR Address);

/************************************************************************/

/**
 * @brief Ensure a minimum number of free objects are available.
 *
 * @param List Pointer to the allocator descriptor.
 * @param DesiredFree Minimum number of free objects to guarantee.
 * @return TRUE when the requested capacity is satisfied, FALSE otherwise.
 */
BOOL BlockListReserve(LPBLOCK_LIST List, UINT DesiredFree);

/************************************************************************/

/**
 * @brief Release completely free trailing slabs back to the system.
 *
 * @param List Pointer to the allocator descriptor.
 * @return TRUE when the operation succeeds, FALSE if shrink failed.
 */
BOOL BlockListReleaseUnused(LPBLOCK_LIST List);

/************************************************************************/

/**
 * @brief Retrieve the total number of objects managed by the allocator.
 *
 * @param List Pointer to the allocator descriptor.
 * @return Total number of objects across all slabs (used + free).
 */
UINT BlockListGetCapacity(const BLOCK_LIST* List);

/************************************************************************/

/**
 * @brief Retrieve the number of objects currently in use.
 *
 * @param List Pointer to the allocator descriptor.
 * @return Number of allocated objects.
 */
UINT BlockListGetUsage(const BLOCK_LIST* List);

/************************************************************************/

/**
 * @brief Retrieve the number of free objects currently available.
 *
 * @param List Pointer to the allocator descriptor.
 * @return Number of free objects held by the allocator.
 */
UINT BlockListGetFreeCount(const BLOCK_LIST* List);

/************************************************************************/

/**
 * @brief Retrieve the number of active slabs.
 *
 * @param List Pointer to the allocator descriptor.
 * @return Number of slabs currently mapped.
 */
UINT BlockListGetSlabCount(const BLOCK_LIST* List);

/************************************************************************/

#endif  // BLOCK_LIST_H_INCLUDED
