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


    EXT2 Buffer Pool

\************************************************************************/

#include "drivers/filesystems/EXT2-Private.h"

/************************************************************************/

#define EXT2_BLOCK_BUFFER_ALLOC_FLAGS (ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE)

/************************************************************************/

/**
 * @brief Initialize the EXT2 block buffer pool for a filesystem.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL Ext2BufferPoolInit(LPEXT2FILESYSTEM FileSystem) {
    if (FileSystem == NULL) {
        return FALSE;
    }

    if (FileSystem->BlockSize == 0) {
        return FALSE;
    }

    if (FileSystem->BlockBufferPool.List.ObjectSize != 0) {
        return TRUE;
    }

    if (!BufferPoolInit(
            &FileSystem->BlockBufferPool, FileSystem->BlockSize, EXT2_BLOCK_BUFFER_OBJECTS_PER_SLAB,
            EXT2_BLOCK_BUFFER_INITIAL_SLABS, EXT2_BLOCK_BUFFER_ALLOC_FLAGS, TEXT("Ext2BlockBuffer"))) {
        return FALSE;
    }

    if (!BufferPoolReserve(&FileSystem->BlockBufferPool, EXT2_BLOCK_BUFFER_MIN_FREE)) {
        BufferPoolDeinit(&FileSystem->BlockBufferPool);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Release all resources owned by the EXT2 block buffer pool.
 * @param FileSystem Pointer to the EXT2 file system instance.
 */
void Ext2BufferPoolDeinit(LPEXT2FILESYSTEM FileSystem) {
    if (FileSystem == NULL) {
        return;
    }

    if (FileSystem->BlockBufferPool.List.ObjectSize == 0) {
        return;
    }

    BufferPoolDeinit(&FileSystem->BlockBufferPool);
}

/************************************************************************/

/**
 * @brief Acquire a block-sized buffer from the EXT2 pool.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @return Pointer to a block-sized buffer, or NULL on failure.
 */
LPVOID Ext2AcquireBlockBuffer(LPEXT2FILESYSTEM FileSystem) {
    LINEAR Address;

    if (FileSystem == NULL) {
        return NULL;
    }

    if (FileSystem->BlockBufferPool.List.ObjectSize == 0) {
        return NULL;
    }

    Address = (LINEAR)BufferPoolAcquire(&FileSystem->BlockBufferPool);

    return (LPVOID)Address;
}

/************************************************************************/

/**
 * @brief Return a block-sized buffer to the EXT2 pool.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Buffer Buffer previously obtained from Ext2AcquireBlockBuffer.
 */
void Ext2ReleaseBlockBuffer(LPEXT2FILESYSTEM FileSystem, LPVOID Buffer) {
    if (FileSystem == NULL || Buffer == NULL) {
        return;
    }

    if (FileSystem->BlockBufferPool.List.ObjectSize == 0) {
        return;
    }

    BufferPoolRelease(&FileSystem->BlockBufferPool, Buffer);
}

/************************************************************************/
