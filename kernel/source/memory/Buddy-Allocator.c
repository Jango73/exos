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


    Buddy allocator (physical pages)

\************************************************************************/

#include "memory/Buddy-Allocator.h"

#include "arch/Memory.h"
#include "text/CoreString.h"

/************************************************************************/
// #defines

#define BUDDY_MAGIC ((U32)0x42444459)
#define BUDDY_INVALID_INDEX MAX_UINT

/************************************************************************/
// typedefs

typedef struct tag_BUDDY_HEADER {
    U32 Magic;
    UINT TotalPages;
    UINT MaxOrder;
    UINT UsedPages;
    UINT Ready;
} BUDDY_HEADER, *LPBUDDY_HEADER;

typedef struct tag_BUDDY_NODE {
    UINT Prev;
    UINT Next;
} BUDDY_NODE, *LPBUDDY_NODE;

/************************************************************************/
// inlines

static inline UINT BuddyBlockPages(UINT Order) { return ((UINT)1) << Order; }

/************************************************************************/
// external symbols

/************************************************************************/
// other

static LPBUDDY_HEADER DATA_SECTION G_BuddyHeader = NULL;
static UINT* DATA_SECTION G_OrderHeads = NULL;
static LPBUDDY_NODE DATA_SECTION G_BlockLinks = NULL;
static U8* DATA_SECTION G_BlockOrder = NULL;
static U8* DATA_SECTION G_PageUsed = NULL;

/************************************************************************/
/**
 * @brief Align a value to the specified power-of-two boundary.
 * @param Value Value to align.
 * @param Alignment Alignment boundary.
 * @return Aligned value.
 */
static UINT AlignUp(UINT Value, UINT Alignment) {
    if (Alignment == 0) {
        return Value;
    }

    return (Value + Alignment - 1) & ~(Alignment - 1);
}

/************************************************************************/
/**
 * @brief Return the largest buddy order that fits in the page span.
 * @param TotalPages Number of managed pages.
 * @return Maximum order.
 */
static UINT ComputeMaxOrder(UINT TotalPages) {
    UINT Order = 0;
    UINT Span = 1;

    if (TotalPages <= 1) {
        return 0;
    }

    while (Span <= (TotalPages >> 1)) {
        Span = Span << 1;
        Order++;
    }

    return Order;
}

/************************************************************************/
/**
 * @brief Push one free block to the head of an order list.
 * @param Index Start page index of the block.
 * @param Order Buddy order of the block.
 */
static void AddFreeBlock(UINT Index, UINT Order) {
    UINT Head = G_OrderHeads[Order];

    G_BlockOrder[Index] = (U8)Order;
    G_BlockLinks[Index].Prev = BUDDY_INVALID_INDEX;
    G_BlockLinks[Index].Next = Head;

    if (Head != BUDDY_INVALID_INDEX) {
        G_BlockLinks[Head].Prev = Index;
    }

    G_OrderHeads[Order] = Index;
}

/************************************************************************/
/**
 * @brief Unlink one free block from its order list.
 * @param Index Start page index of the block.
 * @param Order Buddy order of the block.
 */
static void RemoveFreeBlock(UINT Index, UINT Order) {
    UINT Prev = G_BlockLinks[Index].Prev;
    UINT Next = G_BlockLinks[Index].Next;

    if (Prev == BUDDY_INVALID_INDEX) {
        G_OrderHeads[Order] = Next;
    } else {
        G_BlockLinks[Prev].Next = Next;
    }

    if (Next != BUDDY_INVALID_INDEX) {
        G_BlockLinks[Next].Prev = Prev;
    }

    G_BlockLinks[Index].Prev = BUDDY_INVALID_INDEX;
    G_BlockLinks[Index].Next = BUDDY_INVALID_INDEX;
}

/************************************************************************/
/**
 * @brief Check whether a specific block start is present in a free list.
 * @param Index Start page index of the candidate block.
 * @param Order Buddy order to inspect.
 * @return TRUE if the block is present.
 */
static BOOL IsBlockInFreeList(UINT Index, UINT Order) {
    UINT Cursor = G_OrderHeads[Order];

    while (Cursor != BUDDY_INVALID_INDEX) {
        if (Cursor == Index) {
            return TRUE;
        }

        Cursor = G_BlockLinks[Cursor].Next;
    }

    return FALSE;
}

/************************************************************************/
/**
 * @brief Rebuild the allocator into a fully free state.
 * @return TRUE on success.
 */
static BOOL ResetToAllFree(void) {
    UINT TotalPages = G_BuddyHeader->TotalPages;
    UINT MaxOrder = G_BuddyHeader->MaxOrder;

    for (UINT Order = 0; Order <= MaxOrder; Order++) {
        G_OrderHeads[Order] = BUDDY_INVALID_INDEX;
    }

    for (UINT Index = 0; Index < TotalPages; Index++) {
        G_BlockLinks[Index].Prev = BUDDY_INVALID_INDEX;
        G_BlockLinks[Index].Next = BUDDY_INVALID_INDEX;
        G_BlockOrder[Index] = 0;
        G_PageUsed[Index] = 0;
    }

    UINT Cursor = 0;
    UINT Remaining = TotalPages;

    while (Remaining != 0) {
        UINT Order = MaxOrder;

        while (Order > 0) {
            UINT Pages = BuddyBlockPages(Order);

            if (Pages <= Remaining && (Cursor & (Pages - 1)) == 0) {
                break;
            }

            Order--;
        }

        AddFreeBlock(Cursor, Order);

        UINT BlockPages = BuddyBlockPages(Order);
        if (BlockPages == 0 || BlockPages > Remaining) {
            return FALSE;
        }

        Cursor += BlockPages;
        Remaining -= BlockPages;
    }

    G_BuddyHeader->UsedPages = 0;
    return TRUE;
}

/************************************************************************/
/**
 * @brief Reserve one physical page in the buddy allocator.
 * @param PageIndex Page index to reserve.
 * @return TRUE on success.
 */
static BOOL ReserveOnePage(UINT PageIndex) {
    if (PageIndex >= G_BuddyHeader->TotalPages) {
        return FALSE;
    }

    if (G_PageUsed[PageIndex] != 0) {
        return TRUE;
    }

    UINT FoundStart = BUDDY_INVALID_INDEX;
    UINT FoundOrder = 0;
    UINT MaxOrder = G_BuddyHeader->MaxOrder;

    for (UINT Order = 0; Order <= MaxOrder; Order++) {
        UINT BlockPages = BuddyBlockPages(Order);
        UINT Start = PageIndex & ~(BlockPages - 1);

        if (IsBlockInFreeList(Start, Order)) {
            FoundStart = Start;
            FoundOrder = Order;
            break;
        }
    }

    if (FoundStart == BUDDY_INVALID_INDEX) {
        return FALSE;
    }

    RemoveFreeBlock(FoundStart, FoundOrder);

    while (FoundOrder > 0) {
        FoundOrder--;
        UINT HalfPages = BuddyBlockPages(FoundOrder);
        UINT LeftStart = FoundStart;
        UINT RightStart = FoundStart + HalfPages;
        UINT FreeStart = 0;

        if (PageIndex < RightStart) {
            FoundStart = LeftStart;
            FreeStart = RightStart;
        } else {
            FoundStart = RightStart;
            FreeStart = LeftStart;
        }

        AddFreeBlock(FreeStart, FoundOrder);
    }

    G_BlockOrder[FoundStart] = 0;
    G_PageUsed[FoundStart] = 1;
    G_BuddyHeader->UsedPages++;
    return TRUE;
}

/************************************************************************/
/**
 * @brief Release one previously reserved page.
 * @param PageIndex Page index to release.
 * @return TRUE on success.
 */
static BOOL ReleaseOnePage(UINT PageIndex) {
    if (PageIndex >= G_BuddyHeader->TotalPages) {
        return FALSE;
    }

    if (G_PageUsed[PageIndex] == 0) {
        return TRUE;
    }

    UINT CurrentStart = PageIndex;
    UINT CurrentOrder = (UINT)G_BlockOrder[PageIndex];
    UINT MaxOrder = G_BuddyHeader->MaxOrder;

    G_PageUsed[PageIndex] = 0;

    if (G_BuddyHeader->UsedPages != 0) {
        G_BuddyHeader->UsedPages--;
    }

    while (CurrentOrder < MaxOrder) {
        UINT BlockPages = BuddyBlockPages(CurrentOrder);
        UINT BuddyStart = CurrentStart ^ BlockPages;

        if (BuddyStart >= G_BuddyHeader->TotalPages) {
            break;
        }

        if (G_PageUsed[BuddyStart] != 0) {
            break;
        }

        if ((UINT)G_BlockOrder[BuddyStart] != CurrentOrder) {
            break;
        }

        if (IsBlockInFreeList(BuddyStart, CurrentOrder) == FALSE) {
            break;
        }

        RemoveFreeBlock(BuddyStart, CurrentOrder);

        if (BuddyStart < CurrentStart) {
            CurrentStart = BuddyStart;
        }

        CurrentOrder++;
    }

    AddFreeBlock(CurrentStart, CurrentOrder);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Return allocator metadata footprint for the specified page count.
 * @param TotalPages Number of pages managed by the allocator.
 * @return Number of bytes required.
 */
UINT BuddyGetMetadataSize(UINT TotalPages) {
    UINT MaxOrder = ComputeMaxOrder(TotalPages);
    UINT Size = 0;

    Size += (UINT)sizeof(BUDDY_HEADER);
    Size = AlignUp(Size, (UINT)sizeof(UINT));

    Size += (MaxOrder + 1) * (UINT)sizeof(UINT);
    Size = AlignUp(Size, (UINT)sizeof(UINT));

    Size += TotalPages * (UINT)sizeof(BUDDY_NODE);
    Size = AlignUp(Size, (UINT)sizeof(UINT));

    Size += TotalPages * (UINT)sizeof(U8);
    Size = AlignUp(Size, (UINT)sizeof(UINT));

    Size += TotalPages * (UINT)sizeof(U8);
    Size = AlignUp(Size, PAGE_SIZE);

    return Size;
}

/************************************************************************/
/**
 * @brief Initialize the buddy allocator metadata in place.
 * @param MetadataAddress Linear base of the metadata buffer.
 * @param MetadataSize Size of metadata buffer in bytes.
 * @param TotalPages Number of pages to manage.
 * @return TRUE on success.
 */
BOOL BuddyInitialize(LINEAR MetadataAddress, UINT MetadataSize, UINT TotalPages) {
    if (MetadataAddress == 0 || MetadataSize == 0 || TotalPages == 0) {
        return FALSE;
    }

    UINT RequiredSize = BuddyGetMetadataSize(TotalPages);
    if (MetadataSize < RequiredSize) {
        return FALSE;
    }

    U8* Base = (U8*)MetadataAddress;
    MemorySet(Base, 0, MetadataSize);

    UINT Offset = 0;
    G_BuddyHeader = (LPBUDDY_HEADER)(Base + Offset);
    Offset += (UINT)sizeof(BUDDY_HEADER);
    Offset = AlignUp(Offset, (UINT)sizeof(UINT));

    UINT MaxOrder = ComputeMaxOrder(TotalPages);

    G_OrderHeads = (UINT*)(Base + Offset);
    Offset += (MaxOrder + 1) * (UINT)sizeof(UINT);
    Offset = AlignUp(Offset, (UINT)sizeof(UINT));

    G_BlockLinks = (LPBUDDY_NODE)(Base + Offset);
    Offset += TotalPages * (UINT)sizeof(BUDDY_NODE);
    Offset = AlignUp(Offset, (UINT)sizeof(UINT));

    G_BlockOrder = (U8*)(Base + Offset);
    Offset += TotalPages * (UINT)sizeof(U8);
    Offset = AlignUp(Offset, (UINT)sizeof(UINT));

    G_PageUsed = (U8*)(Base + Offset);

    G_BuddyHeader->Magic = BUDDY_MAGIC;
    G_BuddyHeader->TotalPages = TotalPages;
    G_BuddyHeader->MaxOrder = MaxOrder;
    G_BuddyHeader->UsedPages = 0;
    G_BuddyHeader->Ready = 1;

    return ResetToAllFree();
}

/************************************************************************/
/**
 * @brief Reset allocator state to "all pages free".
 * @return TRUE on success.
 */
BOOL BuddyResetAllReserved(void) {
    if (BuddyIsReady() == FALSE) {
        return FALSE;
    }

    return ResetToAllFree();
}

/************************************************************************/
/**
 * @brief Mark a contiguous page range as used or free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages.
 * @param Used Non-zero to reserve pages, zero to free pages.
 * @return TRUE on success.
 */
BOOL BuddySetRange(UINT FirstPage, UINT PageCount, UINT Used) {
    if (BuddyIsReady() == FALSE) {
        return FALSE;
    }

    UINT End = FirstPage + PageCount;
    if (FirstPage >= G_BuddyHeader->TotalPages) {
        return TRUE;
    }
    if (End > G_BuddyHeader->TotalPages) {
        End = G_BuddyHeader->TotalPages;
    }

    if (Used != 0) {
        for (UINT Page = FirstPage; Page < End; Page++) {
            if (ReserveOnePage(Page) == FALSE) {
                return FALSE;
            }
        }
    } else {
        for (UINT Page = FirstPage; Page < End; Page++) {
            if (ReleaseOnePage(Page) == FALSE) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Check whether a contiguous physical page range is entirely free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages to inspect.
 * @return TRUE when every page is free, FALSE otherwise.
 */
BOOL BuddyIsRangeFree(UINT FirstPage, UINT PageCount) {
    UINT End;

    if (BuddyIsReady() == FALSE || PageCount == 0) {
        return FALSE;
    }

    End = FirstPage + PageCount;
    if (FirstPage >= G_BuddyHeader->TotalPages || End > G_BuddyHeader->TotalPages) {
        return FALSE;
    }

    for (UINT Page = FirstPage; Page < End; Page++) {
        if (G_PageUsed[Page] != 0) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Allocate one 4K physical page.
 * @return Physical address of allocated page, 0 on failure.
 */
PHYSICAL BuddyAllocPage(void) {
    if (BuddyIsReady() == FALSE) {
        return 0;
    }

    UINT CandidateOrder = BUDDY_INVALID_INDEX;

    for (UINT Order = 0; Order <= G_BuddyHeader->MaxOrder; Order++) {
        if (G_OrderHeads[Order] != BUDDY_INVALID_INDEX) {
            CandidateOrder = Order;
            break;
        }
    }

    if (CandidateOrder == BUDDY_INVALID_INDEX) {
        return 0;
    }

    UINT BlockStart = G_OrderHeads[CandidateOrder];
    RemoveFreeBlock(BlockStart, CandidateOrder);

    while (CandidateOrder > 0) {
        CandidateOrder--;
        UINT HalfPages = BuddyBlockPages(CandidateOrder);
        UINT BuddyStart = BlockStart + HalfPages;
        AddFreeBlock(BuddyStart, CandidateOrder);
    }

    G_BlockOrder[BlockStart] = 0;
    G_PageUsed[BlockStart] = 1;
    G_BuddyHeader->UsedPages++;

    return (PHYSICAL)(BlockStart << PAGE_SIZE_MUL);
}

/************************************************************************/
/**
 * @brief Free one 4K physical page.
 * @param Page Physical address to release.
 * @return TRUE on success.
 */
BOOL BuddyFreePage(PHYSICAL Page) {
    if (BuddyIsReady() == FALSE) {
        return FALSE;
    }

    if ((Page & (PAGE_SIZE - 1)) != 0) {
        return FALSE;
    }

    UINT PageIndex = (UINT)(Page >> PAGE_SIZE_MUL);
    return ReleaseOnePage(PageIndex);
}

/************************************************************************/
/**
 * @brief Return whether the allocator has been initialized.
 * @return TRUE when ready.
 */
BOOL BuddyIsReady(void) {
    if (G_BuddyHeader == NULL) {
        return FALSE;
    }

    if (G_BuddyHeader->Magic != BUDDY_MAGIC) {
        return FALSE;
    }

    return (G_BuddyHeader->Ready != 0);
}

/************************************************************************/
/**
 * @brief Return the number of reserved pages.
 * @return Reserved page count.
 */
UINT BuddyGetUsedPageCount(void) {
    if (BuddyIsReady() == FALSE) {
        return 0;
    }

    return G_BuddyHeader->UsedPages;
}
