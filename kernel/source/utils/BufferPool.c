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


    BufferPool - Fixed Size Buffer Pool

\************************************************************************/

#include "utils/BufferPool.h"

#include "log/Log.h"

/************************************************************************/

/**
 * @brief Initialize a buffer pool for fixed-size allocations.
 *
 * @param Pool Pointer to pool descriptor to initialize.
 * @param ObjectSize Size of each buffer in bytes.
 * @param ObjectsPerSlab Requested number of objects per slab before alignment.
 * @param InitialSlabCount Number of slabs to pre-allocate (can be zero).
 * @param Flags Allocation flags to forward to AllocRegion/ResizeRegion.
 * @return TRUE on success, FALSE if initialization failed.
 */
BOOL BufferPoolInit(
    LPBUFFER_POOL Pool, UINT ObjectSize, UINT ObjectsPerSlab, UINT InitialSlabCount, U32 Flags) {
    if (Pool == NULL || ObjectSize == 0) {
        return FALSE;
    }

    if (Pool->List.ObjectSize != 0) {
        return TRUE;
    }

    InitMutex(&Pool->Mutex);

    if (!BlockListInit(&Pool->List, ObjectSize, ObjectsPerSlab, InitialSlabCount, Flags)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Release all resources owned by a buffer pool.
 *
 * @param Pool Pointer to pool descriptor to finalize.
 */
void BufferPoolDeinit(LPBUFFER_POOL Pool) {
    if (Pool == NULL) {
        return;
    }

    if (Pool->List.ObjectSize == 0) {
        return;
    }

    BlockListFinalize(&Pool->List);
}

/************************************************************************/

/**
 * @brief Acquire a buffer from the pool.
 *
 * @param Pool Pointer to pool descriptor.
 * @return Pointer to a buffer, or NULL on failure.
 */
LPVOID BufferPoolAcquire(LPBUFFER_POOL Pool) {
    LINEAR Address;

    if (Pool == NULL) {
        return NULL;
    }

    if (Pool->List.ObjectSize == 0) {
        return NULL;
    }

    LockMutex(&Pool->Mutex, INFINITY);
    Address = BlockListAllocate(&Pool->List);
    UnlockMutex(&Pool->Mutex);

    return (LPVOID)Address;
}

/************************************************************************/

/**
 * @brief Return a buffer to the pool.
 *
 * @param Pool Pointer to pool descriptor.
 * @param Buffer Buffer previously obtained from BufferPoolAcquire.
 */
void BufferPoolRelease(LPBUFFER_POOL Pool, LPVOID Buffer) {
    BOOL Released;

    if (Pool == NULL || Buffer == NULL) {
        return;
    }

    if (Pool->List.ObjectSize == 0) {
        return;
    }

    LockMutex(&Pool->Mutex, INFINITY);
    Released = BlockListFree(&Pool->List, (LINEAR)Buffer);
    UnlockMutex(&Pool->Mutex);

    if (!Released) {
        ERROR(TEXT("Failed to release buffer %p"), Buffer);
    }
}

/************************************************************************/

/**
 * @brief Ensure a minimum number of free buffers are available.
 *
 * @param Pool Pointer to pool descriptor.
 * @param DesiredFree Minimum number of free objects to guarantee.
 * @return TRUE when the requested capacity is satisfied, FALSE otherwise.
 */
BOOL BufferPoolReserve(LPBUFFER_POOL Pool, UINT DesiredFree) {
    BOOL Result;

    if (Pool == NULL) {
        return FALSE;
    }

    if (Pool->List.ObjectSize == 0) {
        return FALSE;
    }

    LockMutex(&Pool->Mutex, INFINITY);
    Result = BlockListReserve(&Pool->List, DesiredFree);
    UnlockMutex(&Pool->Mutex);

    return Result;
}

/************************************************************************/
