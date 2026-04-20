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


    DMA buffer helper

\************************************************************************/

#include "utils/DMABuffer.h"

#include "text/CoreString.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "User.h"

/************************************************************************/

/**
 * @brief Allocate a DMA-visible kernel buffer.
 * @param Buffer Output DMA buffer description.
 * @param Size Requested size in bytes.
 * @param RequireContiguous TRUE for one contiguous physical span, FALSE for paged backing.
 * @param Tag Region tag used for the kernel region.
 * @return TRUE on success, FALSE on failure.
 */
BOOL DMABufferAllocate(LPDMA_BUFFER Buffer, UINT Size, BOOL RequireContiguous, LPCSTR Tag) {
    UINT AllocatedSize;
    PHYSICAL PhysicalBase;
    LINEAR LinearBase;

    if (Buffer == NULL || Size == 0 || StringEmpty(Tag)) {
        return FALSE;
    }

    MemorySet(Buffer, 0, sizeof(DMA_BUFFER));
    AllocatedSize = (UINT)PAGE_ALIGN(Size);

    PhysicalBase = 0;
    if (RequireContiguous) {
        PHYSICAL MaximumBaseAddress;

        if (AllocatedSize > MAX_U32) {
            ERROR(TEXT("DMA size exceeds low-32-bit window (%u)"), AllocatedSize);
            return FALSE;
        }

        MaximumBaseAddress = (PHYSICAL)(MAX_U32 - (AllocatedSize - 1));
        if (!FindAvailableMemoryRangeInWindow((PHYSICAL)N_1MB, MaximumBaseAddress, 0, 0, AllocatedSize, &PhysicalBase)) {
            ERROR(TEXT("No contiguous DMA range found for %s size=%u"), Tag, AllocatedSize);
            return FALSE;
        }
    }

    LinearBase = AllocKernelRegion(
        PhysicalBase,
        AllocatedSize,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
        Tag);
    if (LinearBase == 0) {
        ERROR(TEXT("AllocKernelRegion failed for %s size=%u contiguous=%u"),
              Tag,
              AllocatedSize,
              RequireContiguous ? 1 : 0);
        return FALSE;
    }

    MemorySet((LPVOID)LinearBase, 0, AllocatedSize);

    Buffer->LinearBase = LinearBase;
    Buffer->PhysicalBase = PhysicalBase;
    Buffer->Size = Size;
    Buffer->AllocatedSize = AllocatedSize;
    Buffer->IsContiguous = RequireContiguous;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Release a DMA buffer allocated with DMABufferAllocate().
 * @param Buffer DMA buffer description to release.
 */
void DMABufferRelease(LPDMA_BUFFER Buffer) {
    UINT Index;
    UINT PageCount;

    if (Buffer == NULL) {
        return;
    }

    if (Buffer->LinearBase != 0 && Buffer->AllocatedSize != 0) {
        FreeRegion(Buffer->LinearBase, Buffer->AllocatedSize);
    }

    if (Buffer->IsContiguous && Buffer->PhysicalBase != 0 && Buffer->AllocatedSize != 0) {
        PageCount = Buffer->AllocatedSize >> PAGE_SIZE_MUL;
        for (Index = 0; Index < PageCount; Index++) {
            FreePhysicalPage(Buffer->PhysicalBase + (PHYSICAL)(Index << PAGE_SIZE_MUL));
        }
    }

    MemorySet(Buffer, 0, sizeof(DMA_BUFFER));
}

/************************************************************************/

/**
 * @brief Resolve the physical address backing one offset inside a DMA buffer.
 * @param Buffer DMA buffer description.
 * @param Offset Offset in bytes from the start of the buffer.
 * @return Physical address on success, 0 on failure.
 */
PHYSICAL DMABufferGetPhysical(const DMA_BUFFER* Buffer, UINT Offset) {
    if (Buffer == NULL || Buffer->LinearBase == 0 || Offset >= Buffer->Size) {
        return 0;
    }

    if (Buffer->IsContiguous) {
        return Buffer->PhysicalBase + (PHYSICAL)Offset;
    }

    return MapLinearToPhysical(Buffer->LinearBase + Offset);
}

/************************************************************************/

/**
 * @brief Resolve the linear address of one indexed element inside a DMA buffer.
 * @param Buffer DMA buffer description.
 * @param Index Element index.
 * @param Stride Element stride in bytes.
 * @return Linear address on success, 0 on failure.
 */
LINEAR DMABufferGetIndexedLinear(const DMA_BUFFER* Buffer, UINT Index, UINT Stride) {
    UINT Offset;

    if (Buffer == NULL || Stride == 0) {
        return 0;
    }

    Offset = Index * Stride;
    if (Offset >= Buffer->Size) {
        return 0;
    }

    return Buffer->LinearBase + Offset;
}

/************************************************************************/

/**
 * @brief Resolve the physical address of one indexed element inside a DMA buffer.
 * @param Buffer DMA buffer description.
 * @param Index Element index.
 * @param Stride Element stride in bytes.
 * @return Physical address on success, 0 on failure.
 */
PHYSICAL DMABufferGetIndexedPhysical(const DMA_BUFFER* Buffer, UINT Index, UINT Stride) {
    UINT Offset;

    if (Buffer == NULL || Stride == 0) {
        return 0;
    }

    Offset = Index * Stride;
    return DMABufferGetPhysical(Buffer, Offset);
}
