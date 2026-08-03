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


    BufferPool - Fixed Size Buffer Pool

\************************************************************************/

#ifndef BUFFER_POOL_H_INCLUDED
#define BUFFER_POOL_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "sync/Mutex.h"
#include "utils/BlockList.h"

/************************************************************************/
// typedefs

typedef struct tag_BUFFER_POOL {
    BLOCK_LIST List;
    MUTEX Mutex;
} BUFFER_POOL, *LPBUFFER_POOL;

/************************************************************************/
// External symbols

/**
 * @brief Initialize a buffer pool for fixed-size allocations.
 *
 * @param Pool Pointer to pool descriptor to initialize.
 * @param ObjectSize Size of each buffer in bytes.
 * @param ObjectsPerSlab Requested number of objects per slab before alignment.
 * @param InitialSlabCount Number of slabs to pre-allocate (can be zero).
 * @param Flags Allocation flags to forward to AllocRegion/ResizeRegion.
 * @param Tag Owner tag copied into the allocated memory regions.
 * @return TRUE on success, FALSE if initialization failed.
 */
BOOL BufferPoolInit(
    LPBUFFER_POOL Pool, UINT ObjectSize, UINT ObjectsPerSlab, UINT InitialSlabCount, U32 Flags, LPCSTR Tag);

/************************************************************************/

/**
 * @brief Release all resources owned by a buffer pool.
 *
 * @param Pool Pointer to pool descriptor to finalize.
 */
void BufferPoolDeinit(LPBUFFER_POOL Pool);

/************************************************************************/

/**
 * @brief Acquire a buffer from the pool.
 *
 * @param Pool Pointer to pool descriptor.
 * @return Pointer to a buffer, or NULL on failure.
 */
LPVOID BufferPoolAcquire(LPBUFFER_POOL Pool);

/************************************************************************/

/**
 * @brief Return a buffer to the pool.
 *
 * @param Pool Pointer to pool descriptor.
 * @param Buffer Buffer previously obtained from BufferPoolAcquire.
 */
void BufferPoolRelease(LPBUFFER_POOL Pool, LPVOID Buffer);

/************************************************************************/

/**
 * @brief Ensure a minimum number of free buffers are available.
 *
 * @param Pool Pointer to pool descriptor.
 * @param DesiredFree Minimum number of free objects to guarantee.
 * @return TRUE when the requested capacity is satisfied, FALSE otherwise.
 */
BOOL BufferPoolReserve(LPBUFFER_POOL Pool, UINT DesiredFree);

/************************************************************************/

#endif  // BUFFER_POOL_H_INCLUDED
