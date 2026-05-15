
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


    Heap

\************************************************************************/

#include "memory/Heap.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Process.h"
#include "memory/Memory.h"

/************************************************************************/

static BOOL HeapResizeProcess(LPVOID Context, LPHEAP_CONTROL_BLOCK ControlBlock, UINT NewSize) {
    LPPROCESS Process = (LPPROCESS)Context;

    if (Process == NULL || ControlBlock == NULL) {
        return FALSE;
    }

    return ResizeRegion(ControlBlock->HeapBase, 0, ControlBlock->HeapSize, NewSize, ControlBlock->RegionFlags);
}

/************************************************************************/

/**
 * @brief Determines the size class for a given allocation size
 * @param Size Size in bytes to categorize
 * @return Size class index (0-7) or 0xFF for large blocks (>2048 bytes)
 *
 * Maps allocation sizes to predefined size classes for efficient freelist management.
 * Size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
 */
static UINT GetSizeClass(UINT Size) {
    if (Size <= 16) return 0;
    if (Size <= 32) return 1;
    if (Size <= 64) return 2;
    if (Size <= 128) return 3;
    if (Size <= 256) return 4;
    if (Size <= 512) return 5;
    if (Size <= 1024) return 6;
    if (Size <= 2048) return 7;
    return 0xFF; // Large block
}

/************************************************************************/

/**
 * @brief Returns the actual allocation size for a given size class
 * @param SizeClass Size class index (0-7)
 * @return Size in bytes for the given class, or 0 if invalid class
 *
 * Converts size class indices back to their corresponding byte sizes.
 * Used to determine the actual allocation size for small blocks.
 */
static UINT GetSizeForClass(UINT SizeClass) {
    UINT Sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    if (SizeClass < HEAP_NUM_SIZE_CLASSES) {
        return Sizes[SizeClass];
    }
    return 0;
}

/************************************************************************/

/**
 * @brief Computes the freelist class for an existing block.
 * @param Block Heap block header.
 * @return Size class index, or 0xFF for large blocks.
 */
static UINT GetBlockSizeClass(LPHEAP_BLOCK_HEADER Block) {
    if (Block == NULL || Block->Size <= sizeof(HEAP_BLOCK_HEADER)) {
        return 0xFF;
    }

    return GetSizeClass(Block->Size - sizeof(HEAP_BLOCK_HEADER));
}

/************************************************************************/

/**
 * @brief Returns TRUE when a block header points to a free block.
 * @param Block Heap block header.
 * @return TRUE if free, FALSE otherwise.
 */
static BOOL IsBlockFree(LPHEAP_BLOCK_HEADER Block) {
    if (Block == NULL) {
        return FALSE;
    }

    return (Block->Flags & HEAP_BLOCK_FLAG_FREE) != 0;
}

/************************************************************************/

/**
 * @brief Returns TRUE when a block lies inside the initialized heap range.
 * @param ControlBlock Heap control block.
 * @param Block Candidate block.
 * @return TRUE when valid, FALSE otherwise.
 */
static BOOL IsBlockInHeap(LPHEAP_CONTROL_BLOCK ControlBlock, LPHEAP_BLOCK_HEADER Block) {
    LINEAR FirstBlock;
    LINEAR FirstUnallocated;
    LINEAR Address;

    if (ControlBlock == NULL || Block == NULL) {
        return FALSE;
    }

    FirstBlock = (ControlBlock->HeapBase + sizeof(HEAP_CONTROL_BLOCK) + 15) & ~15;
    FirstUnallocated = (LINEAR)ControlBlock->FirstUnallocated;
    Address = (LINEAR)Block;

    if (Address < FirstBlock || Address >= FirstUnallocated) {
        return FALSE;
    }

    if (Block->TypeID != KOID_HEAP || Block->Size < sizeof(HEAP_BLOCK_HEADER)) {
        return FALSE;
    }

    if (Address + Block->Size > FirstUnallocated) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Finds the physical predecessor block of a target block.
 * @param ControlBlock Heap control block.
 * @param Target Target block.
 * @return Previous adjacent block, or NULL.
 */
static LPHEAP_BLOCK_HEADER FindPreviousPhysicalBlock(LPHEAP_CONTROL_BLOCK ControlBlock, LPHEAP_BLOCK_HEADER Target) {
    LINEAR Cursor;
    LINEAR FirstBlock;
    LINEAR FirstUnallocated;
    LPHEAP_BLOCK_HEADER Block;
    LPHEAP_BLOCK_HEADER Previous;

    if (ControlBlock == NULL || Target == NULL) {
        return NULL;
    }

    FirstBlock = (ControlBlock->HeapBase + sizeof(HEAP_CONTROL_BLOCK) + 15) & ~15;
    FirstUnallocated = (LINEAR)ControlBlock->FirstUnallocated;
    Cursor = FirstBlock;
    Previous = NULL;

    while (Cursor < FirstUnallocated) {
        Block = (LPHEAP_BLOCK_HEADER)Cursor;

        if (Block == Target) {
            return Previous;
        }

        if (IsBlockInHeap(ControlBlock, Block) == FALSE) {
            return NULL;
        }

        Previous = Block;
        Cursor += Block->Size;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Count total bytes held by free blocks inside an initialized heap.
 * @param ControlBlock Heap control block.
 * @return Total free payload bytes across all free blocks.
 */
static UINT CountFreePayloadBytes(LPHEAP_CONTROL_BLOCK ControlBlock) {
    LINEAR Cursor;
    LINEAR FirstBlock;
    LINEAR FirstUnallocated;
    LPHEAP_BLOCK_HEADER Block;
    UINT TotalFreeBytes = 0;

    if (ControlBlock == NULL) {
        return 0;
    }

    FirstBlock = (ControlBlock->HeapBase + sizeof(HEAP_CONTROL_BLOCK) + 15) & ~15;
    FirstUnallocated = (LINEAR)ControlBlock->FirstUnallocated;
    Cursor = FirstBlock;

    while (Cursor < FirstUnallocated) {
        Block = (LPHEAP_BLOCK_HEADER)Cursor;
        if (IsBlockInHeap(ControlBlock, Block) == FALSE) {
            return 0;
        }

        if (IsBlockFree(Block) && Block->Size > sizeof(HEAP_BLOCK_HEADER)) {
            TotalFreeBytes += Block->Size - sizeof(HEAP_BLOCK_HEADER);
        }

        Cursor += Block->Size;
    }

    return TotalFreeBytes;
}

/************************************************************************/

/**
 * @brief Adds a free block to the appropriate freelist
 * @param ControlBlock Pointer to the heap control block
 * @param Block Pointer to the block header to add
 * @param SizeClass Size class of the block (0-7 for small blocks, 0xFF for large blocks)
 *
 * Inserts the block at the head of the corresponding freelist as a doubly-linked list.
 * Large blocks (>2048 bytes) are added to the separate large block freelist.
 */
static void AddToFreeList(LPHEAP_CONTROL_BLOCK ControlBlock, LPHEAP_BLOCK_HEADER Block, UINT SizeClass) {
    if (IsBlockFree(Block)) {
        ERROR(TEXT("Block already marked free"));
        return;
    }

    Block->Flags |= HEAP_BLOCK_FLAG_FREE;

    if (SizeClass == 0xFF) {
        // Large block
        Block->Next = ControlBlock->LargeFreeList;
        Block->Prev = NULL;
        if (ControlBlock->LargeFreeList) {
            ControlBlock->LargeFreeList->Prev = Block;
        }
        ControlBlock->LargeFreeList = Block;
    } else {
        // Small block
        Block->Next = ControlBlock->FreeLists[SizeClass];
        Block->Prev = NULL;
        if (ControlBlock->FreeLists[SizeClass]) {
            ControlBlock->FreeLists[SizeClass]->Prev = Block;
        }
        ControlBlock->FreeLists[SizeClass] = Block;
    }
}

/************************************************************************/

/**
 * @brief Removes a block from its freelist
 * @param ControlBlock Pointer to the heap control block
 * @param Block Pointer to the block header to remove
 * @param SizeClass Size class of the block (0-7 for small blocks, 0xFF for large blocks)
 *
 * Removes the block from its doubly-linked freelist by updating the previous and next
 * block pointers. Updates the freelist head if removing the first block.
 */
static void RemoveFromFreeList(LPHEAP_CONTROL_BLOCK ControlBlock, LPHEAP_BLOCK_HEADER Block, UINT SizeClass) {
    if (Block == NULL) return;

    if (Block->Prev) {
        Block->Prev->Next = Block->Next;
    } else {
        if (SizeClass == 0xFF) {
            ControlBlock->LargeFreeList = Block->Next;
        } else {
            ControlBlock->FreeLists[SizeClass] = Block->Next;
        }
    }
    if (Block->Next) {
        Block->Next->Prev = Block->Prev;
    }

    Block->Next = NULL;
    Block->Prev = NULL;
    Block->Flags &= ~HEAP_BLOCK_FLAG_FREE;
}

/************************************************************************/

/**
 * @brief Initializes a heap with freelist-based allocation
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 *
 * Sets up the heap control block and initializes all freelists to empty.
 * The control block contains metadata for managing the heap, including
 * size class freelists and a pointer to the first unallocated space.
 * Memory is 16-byte aligned for optimal performance.
 */
void HeapInit(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize) {
    MemorySet((LPVOID)HeapBase, 0, HeapSize);

    LPHEAP_CONTROL_BLOCK ControlBlock = (LPHEAP_CONTROL_BLOCK)HeapBase;

    ControlBlock->TypeID = KOID_HEAP;
    ControlBlock->HeapBase = HeapBase;
    ControlBlock->HeapSize = HeapSize;
    ControlBlock->Owner = Process;

    // Initialize all freelists to NULL
    for (UINT i = 0; i < HEAP_NUM_SIZE_CLASSES; i++) {
        ControlBlock->FreeLists[i] = NULL;
    }
    ControlBlock->LargeFreeList = NULL;

    // Set first unallocated to after control block, aligned to 16 bytes
    ControlBlock->FirstUnallocated = (LPVOID)((HeapBase + sizeof(HEAP_CONTROL_BLOCK) + 15) & ~15);
    ControlBlock->ResizeContext = Process;
    ControlBlock->ResizeCallback = HeapResizeProcess;
    ControlBlock->MaximumSize = (Process != NULL) ? Process->MaximumAllocatedMemory : HeapSize;
    ControlBlock->RegionFlags = ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE;
    if (Process != NULL && Process->Privilege == CPU_PRIVILEGE_KERNEL) {
        ControlBlock->RegionFlags |= ALLOC_PAGES_AT_OR_OVER;
    }

}

/************************************************************************/

void HeapConfigureGrowth(
    LINEAR HeapBase,
    LPVOID ResizeContext,
    HEAP_RESIZE_CALLBACK ResizeCallback,
    UINT MaximumSize,
    U32 RegionFlags) {
    LPHEAP_CONTROL_BLOCK ControlBlock = (LPHEAP_CONTROL_BLOCK)HeapBase;

    if (ControlBlock == NULL || ControlBlock->TypeID != KOID_HEAP) {
        return;
    }

    ControlBlock->ResizeContext = ResizeContext;
    ControlBlock->ResizeCallback = ResizeCallback;
    ControlBlock->MaximumSize = MaximumSize;
    ControlBlock->RegionFlags = RegionFlags;
}

/************************************************************************/

/**
 * @brief Attempt to expand the heap when additional memory is required.
 * @param ControlBlock Pointer to the heap control block.
 * @param RequiredSize Allocation size (including header) that triggered the expansion.
 * @return TRUE if the heap was expanded, FALSE otherwise.
 */
static BOOL TryExpandHeap(LPHEAP_CONTROL_BLOCK ControlBlock, UINT RequiredSize) {
    if (ControlBlock == NULL) {
        ERROR(TEXT("Heap control block is undefined"));
        return FALSE;
    }

    UINT CurrentSize = ControlBlock->HeapSize;
    UINT Limit = ControlBlock->MaximumSize;
    UINT AdditionalRequired = (UINT)RequiredSize;
    UINT DesiredSize = CurrentSize << 1;

    if (DesiredSize < CurrentSize) {
        DesiredSize = Limit;
    }

    UINT MinimumRequired = CurrentSize + AdditionalRequired;
    if (MinimumRequired < CurrentSize) {
        MinimumRequired = Limit;
    }

    if (DesiredSize < MinimumRequired) {
        DesiredSize = MinimumRequired;
    }

    if (DesiredSize > Limit) {
        DesiredSize = Limit;
    }

    if (DesiredSize <= CurrentSize) {
        ERROR(TEXT("Heap limit reached (Current=%x Limit=%x)"), CurrentSize, Limit);
        return FALSE;
    }

    if (ControlBlock->ResizeCallback == NULL) {
        ERROR(TEXT("Heap resize callback is undefined"));
        return FALSE;
    }

    if (ControlBlock->ResizeCallback(ControlBlock->ResizeContext, ControlBlock, DesiredSize) == FALSE) {
        ERROR(TEXT("ResizeRegion failed for heap at %x (from %x to %x)"), ControlBlock->HeapBase, CurrentSize,
            DesiredSize);
        return FALSE;
    }

    ControlBlock->HeapSize = DesiredSize;
    if (ControlBlock->Owner != NULL && ControlBlock->Owner->HeapBase == ControlBlock->HeapBase) {
        ControlBlock->Owner->HeapSize = DesiredSize;
    }

    DEBUG(TEXT("Expanded heap from %u to %u (required %u)"),
          CurrentSize, DesiredSize, RequiredSize);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocates memory from a heap using heap base, heap size, and size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * This function implements a freelist-based heap allocation algorithm with size classes.
 * Memory is 16-byte aligned for optimal performance.
 */
LPVOID HeapAlloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, UINT Size) {
    UNUSED(HeapSize);

    LPHEAP_CONTROL_BLOCK ControlBlock = (LPHEAP_CONTROL_BLOCK)HeapBase;
    if (Process != NULL) {
        ControlBlock->Owner = Process;
    }
    LPHEAP_BLOCK_HEADER Block = NULL;
    UINT SizeClass = 0;
    UINT ActualSize = 0;
    UINT TotalSize = 0;

    // Check validity of parameters
    if (ControlBlock == NULL) return NULL;
    if (ControlBlock->TypeID != KOID_HEAP) return NULL;
    if (Size == 0) return NULL;

    // Determine size class and actual allocation size
    SizeClass = GetSizeClass(Size);
    if (SizeClass != 0xFF) {
        ActualSize = GetSizeForClass(SizeClass);
    } else {
        // Large block - align to 16 bytes
        ActualSize = (Size + 15) & ~15;
    }

    TotalSize = ActualSize + sizeof(HEAP_BLOCK_HEADER);

    // Try to find a block in the appropriate freelist
    if (SizeClass != 0xFF) {
        // Small block - check exact size class first
        Block = ControlBlock->FreeLists[SizeClass];
        if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
            RemoveFromFreeList(ControlBlock, Block, SizeClass);
            return (LPVOID)((LINEAR)Block + sizeof(HEAP_BLOCK_HEADER));
        }

        // Try larger size classes
        for (UINT i = SizeClass + 1; i < HEAP_NUM_SIZE_CLASSES; i++) {
            Block = ControlBlock->FreeLists[i];
            if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
                RemoveFromFreeList(ControlBlock, Block, i);

                // Split the block if it's significantly larger
                if (Block->Size > TotalSize) {
                    UINT RemainingSize = Block->Size - TotalSize;

                    if (RemainingSize >= sizeof(HEAP_BLOCK_HEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAP_BLOCK_HEADER SplitBlock = (LPHEAP_BLOCK_HEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Flags = 0;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;

                        UINT SplitSizeClass = GetSizeClass(RemainingSize - sizeof(HEAP_BLOCK_HEADER));
                        AddToFreeList(ControlBlock, SplitBlock, SplitSizeClass);

                        Block->Size = TotalSize;
                    }
                }

                return (LPVOID)((LINEAR)Block + sizeof(HEAP_BLOCK_HEADER));
            }
        }
    } else {
        // Large block - search large freelist
        Block = ControlBlock->LargeFreeList;
        while (Block != NULL) {
            if (Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
                RemoveFromFreeList(ControlBlock, Block, 0xFF);

                // Split if significantly larger
                if (Block->Size > TotalSize) {
                    UINT RemainingSize = Block->Size - TotalSize;

                    if (RemainingSize >= sizeof(HEAP_BLOCK_HEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAP_BLOCK_HEADER SplitBlock = (LPHEAP_BLOCK_HEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Flags = 0;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;

                        UINT SplitSizeClass = GetSizeClass(RemainingSize - sizeof(HEAP_BLOCK_HEADER));
                        AddToFreeList(ControlBlock, SplitBlock, SplitSizeClass);

                        Block->Size = TotalSize;
                    }
                }

                return (LPVOID)((LINEAR)Block + sizeof(HEAP_BLOCK_HEADER));
            }
            Block = Block->Next;
        }
    }

    // No suitable free block found, allocate from unallocated space
    LINEAR NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;
    if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
        if (TryExpandHeap(ControlBlock, TotalSize) == FALSE) {
            return NULL;
        }

        NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;

        if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
            return NULL;
        }
    }

    Block = (LPHEAP_BLOCK_HEADER)NewBlockAddr;
    Block->TypeID = KOID_HEAP;
    Block->Size = TotalSize;
    Block->Flags = 0;
    Block->Next = NULL;
    Block->Prev = NULL;

    ControlBlock->FirstUnallocated = (LPVOID)(NewBlockAddr + TotalSize);

    return (LPVOID)(NewBlockAddr + sizeof(HEAP_BLOCK_HEADER));
}

/************************************************************************/

/**
 * @brief Reallocates memory from a heap using heap base, heap size, and size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * This function behaves like standard realloc():
 * - If Pointer is NULL, behaves like HeapAlloc_HBHS()
 * - If Size is 0, frees the memory and returns NULL
 * - Otherwise, changes the size of the memory block, potentially moving it
 */
LPVOID HeapRealloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, LPVOID Pointer, UINT Size) {
    if (Pointer == NULL) {
        return HeapAlloc_HBHS(Process, HeapBase, HeapSize, Size);
    }

    if (Size == 0) {
        HeapFree_HBHS(HeapBase, HeapSize, Pointer);
        return NULL;
    }

    LPHEAP_CONTROL_BLOCK ControlBlock = (LPHEAP_CONTROL_BLOCK)HeapBase;
    if (Process != NULL) {
        ControlBlock->Owner = Process;
    }
    if (ControlBlock == NULL || ControlBlock->TypeID != KOID_HEAP) return NULL;

    // Get the block header
    LPHEAP_BLOCK_HEADER Block = (LPHEAP_BLOCK_HEADER)((LINEAR)Pointer - sizeof(HEAP_BLOCK_HEADER));
    if (Block->TypeID != KOID_HEAP) {
        ERROR(TEXT("Invalid block header ID"));
        return NULL;
    }

    UINT OldDataSize = Block->Size - sizeof(HEAP_BLOCK_HEADER);
    UINT NewSizeClass = GetSizeClass(Size);
    UINT NewActualSize = (NewSizeClass != 0xFF) ? GetSizeForClass(NewSizeClass) : ((Size + 15) & ~15);
    UINT NewTotalSize = NewActualSize + sizeof(HEAP_BLOCK_HEADER);

    // If new size fits in current block, just return the same pointer
    if (NewTotalSize <= Block->Size) {
        return Pointer;
    }

    // Need to allocate new block
    LPVOID NewPointer = HeapAlloc_HBHS(Process, HeapBase, HeapSize, Size);
    SAFE_USE(NewPointer) {
        // Copy old data to new location
        MemoryCopy(NewPointer, Pointer, OldDataSize < Size ? OldDataSize : Size);
        // Free old block
        HeapFree_HBHS(HeapBase, HeapSize, Pointer);
    }

    return NewPointer;
}

/************************************************************************/

/**
 * @brief Frees memory allocated from a heap using heap base and heap size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes (unused but kept for consistency)
 * @param Pointer Pointer to memory to free
 *
 * This function frees a block and attempts to coalesce with adjacent free blocks.
 */
void HeapFree_HBHS(LINEAR HeapBase, UINT HeapSize, LPVOID Pointer) {
    UNUSED(HeapSize);

    LPHEAP_CONTROL_BLOCK ControlBlock = (LPHEAP_CONTROL_BLOCK)HeapBase;
    LPHEAP_BLOCK_HEADER Block = NULL;
    LPHEAP_BLOCK_HEADER Previous = NULL;
    LPHEAP_BLOCK_HEADER Next = NULL;
    BOOL Merged;
    UINT SizeClass = 0;

    if (Pointer == NULL) return;
    if (ControlBlock == NULL || ControlBlock->TypeID != KOID_HEAP) return;

    // DEBUG("[HeapFree_HBHS] Freeing pointer %x", Pointer);

    // Get the block header
    Block = (LPHEAP_BLOCK_HEADER)((LINEAR)Pointer - sizeof(HEAP_BLOCK_HEADER));
    if (Block->TypeID != KOID_HEAP) {
        ERROR(TEXT("Invalid block header ID"));
        return;
    }

    if (IsBlockInHeap(ControlBlock, Block) == FALSE) {
        ERROR(TEXT("Block outside heap bounds"));
        return;
    }

    if (IsBlockFree(Block)) {
        ERROR(TEXT("Double free detected"));
        return;
    }

    // DEBUG("[HeapFree_HBHS] Freeing block at %x, size %x", Block, Block->Size);

    FOREVER {
        Merged = FALSE;

        Next = (LPHEAP_BLOCK_HEADER)((LINEAR)Block + Block->Size);
        if ((LINEAR)Next < (LINEAR)ControlBlock->FirstUnallocated &&
            IsBlockInHeap(ControlBlock, Next) &&
            IsBlockFree(Next)) {
            RemoveFromFreeList(ControlBlock, Next, GetBlockSizeClass(Next));
            Block->Size += Next->Size;
            Merged = TRUE;
        }

        Previous = FindPreviousPhysicalBlock(ControlBlock, Block);
        if (Previous != NULL &&
            IsBlockInHeap(ControlBlock, Previous) &&
            IsBlockFree(Previous)) {
            RemoveFromFreeList(ControlBlock, Previous, GetBlockSizeClass(Previous));
            Previous->Size += Block->Size;
            Block = Previous;
            Merged = TRUE;
        }

        if (Merged == FALSE) {
            break;
        }
    }

    if ((LINEAR)Block + Block->Size == (LINEAR)ControlBlock->FirstUnallocated) {
        ControlBlock->FirstUnallocated = (LPVOID)Block;

        FOREVER {
            LPHEAP_BLOCK_HEADER Tail = (LPHEAP_BLOCK_HEADER)ControlBlock->FirstUnallocated;
            LPHEAP_BLOCK_HEADER TailPrevious = FindPreviousPhysicalBlock(ControlBlock, Tail);

            if (TailPrevious == NULL || IsBlockInHeap(ControlBlock, TailPrevious) == FALSE || IsBlockFree(TailPrevious) == FALSE) {
                break;
            }

            RemoveFromFreeList(ControlBlock, TailPrevious, GetBlockSizeClass(TailPrevious));
            ControlBlock->FirstUnallocated = (LPVOID)TailPrevious;
        }

        return;
    }

    UINT DataSize = Block->Size - sizeof(HEAP_BLOCK_HEADER);
    SizeClass = GetSizeClass(DataSize);

    AddToFreeList(ControlBlock, Block, SizeClass);

    // DEBUG("[HeapFree_HBHS] Added block to freelist, size class %x", SizeClass);
}

/************************************************************************/

/**
 * @brief Query memory statistics for a process heap.
 * @param Process Target process.
 * @param Info Output structure to fill.
 * @return TRUE on success, FALSE on invalid arguments.
 */
BOOL HeapQueryProcessMemoryInfo(LPPROCESS Process, LPPROCESS_MEMORY_INFO Info) {
    LPHEAP_CONTROL_BLOCK ControlBlock;
    UINT HeapHeaderEndOffset;
    UINT FirstUnallocatedOffset;
    UINT TotalPayloadBytes;
    UINT FreePayloadBytes;

    if (Process == NULL || Info == NULL || Process->HeapBase == 0 || Process->HeapSize == 0) {
        return FALSE;
    }

    LockMutex(&(Process->HeapMutex), INFINITY);

    ControlBlock = (LPHEAP_CONTROL_BLOCK)Process->HeapBase;
    if (ControlBlock->TypeID != KOID_HEAP) {
        UnlockMutex(&(Process->HeapMutex));
        return FALSE;
    }

    HeapHeaderEndOffset = ((sizeof(HEAP_CONTROL_BLOCK) + 15) & ~15);
    FirstUnallocatedOffset = (UINT)((LINEAR)ControlBlock->FirstUnallocated - ControlBlock->HeapBase);
    if (FirstUnallocatedOffset < HeapHeaderEndOffset) {
        UnlockMutex(&(Process->HeapMutex));
        return FALSE;
    }

    TotalPayloadBytes = FirstUnallocatedOffset - HeapHeaderEndOffset;
    FreePayloadBytes = CountFreePayloadBytes(ControlBlock);
    if (FreePayloadBytes > TotalPayloadBytes) {
        UnlockMutex(&(Process->HeapMutex));
        return FALSE;
    }

    Info->HeapBase = Process->HeapBase;
    Info->HeapReservedSize = Process->HeapSize;
    Info->HeapFirstUnallocatedOffset = FirstUnallocatedOffset;
    Info->HeapFreeBytes = FreePayloadBytes;
    Info->HeapUsedBytes = TotalPayloadBytes - FreePayloadBytes;

    UnlockMutex(&(Process->HeapMutex));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocates memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * This function provides thread-safe memory allocation by acquiring the
 * process's heap mutex before calling the core allocation function.
 */
LPVOID HeapAlloc_P(LPPROCESS Process, UINT Size) {
    LPVOID Pointer = NULL;

    if (Process == NULL) {
        ERROR(TEXT("Process pointer is NULL"));
        return NULL;
    }

    LockMutex(&(Process->HeapMutex), INFINITY);
    Pointer = HeapAlloc_HBHS(Process, Process->HeapBase, Process->HeapSize, Size);
    UnlockMutex(&(Process->HeapMutex));

    return Pointer;
}

/************************************************************************/

/**
 * @brief Reallocates memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * This function provides thread-safe memory reallocation by acquiring the
 * process's heap mutex before calling the core reallocation function.
 */
LPVOID HeapRealloc_P(LPPROCESS Process, LPVOID Pointer, UINT Size) {
    LPVOID NewPointer = NULL;
    LockMutex(&(Process->HeapMutex), INFINITY);
    NewPointer = HeapRealloc_HBHS(Process, Process->HeapBase, Process->HeapSize, Pointer, Size);
    UnlockMutex(&(Process->HeapMutex));
    return NewPointer;
}

/************************************************************************/

/**
 * @brief Frees memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Pointer Pointer to memory to free
 *
 * This function provides thread-safe memory deallocation by acquiring the
 * process's heap mutex before calling the core deallocation function.
 */
void HeapFree_P(LPPROCESS Process, LPVOID Pointer) {
    LockMutex(&(Process->HeapMutex), INFINITY);
    HeapFree_HBHS(Process->HeapBase, Process->HeapSize, Pointer);
    UnlockMutex(&(Process->HeapMutex));
}

/************************************************************************/

/**
 * @brief Allocates memory from the kernel heap
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * Convenience function for allocating memory from the kernel process heap.
 */
LPVOID KernelHeapAlloc(UINT Size) {
    LPVOID Pointer = HeapAlloc_P(&KernelProcess, Size);

    if (Pointer == NULL) {
        ERROR(TEXT("Allocation failed"));
    }

    return Pointer;
}

/************************************************************************/

/**
 * @brief Reallocates memory from the kernel heap
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * Convenience function for reallocating memory from the kernel process heap.
 */
LPVOID KernelHeapRealloc(LPVOID Pointer, UINT Size) {
    return HeapRealloc_P(&KernelProcess, Pointer, Size);
}

/***************************************************************************/

/**
 * @brief Frees memory from the kernel heap
 * @param Pointer Pointer to memory to free
 *
 * Convenience function for freeing memory from the kernel process heap.
 */
void KernelHeapFree(LPVOID Pointer) { HeapFree_P(&KernelProcess, Pointer); }

/***************************************************************************/

/**
 * @brief Allocates memory from the current process's heap
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * Convenience function that automatically determines the current process
 * and allocates memory from its heap.
 */
LPVOID HeapAlloc(UINT Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;
    return HeapAlloc_P(Process, Size);
}

/***************************************************************************/

/**
 * @brief Frees memory from the current process's heap
 * @param Pointer Pointer to memory to free
 *
 * Convenience function that automatically determines the current process
 * and frees memory from its heap.
 */
void HeapFree(LPVOID Pointer) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return;
    HeapFree_P(Process, Pointer);
}

/***************************************************************************/

/**
 * @brief Reallocates memory from the current process's heap
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * Convenience function that automatically determines the current process
 * and reallocates memory from its heap.
 */
LPVOID HeapRealloc(LPVOID Pointer, UINT Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;
    return HeapRealloc_P(Process, Pointer, Size);
}
