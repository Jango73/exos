
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


    BlockList Allocator - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "utils/BlockList.h"

/************************************************************************/

static BOOL ValidateUniqueAddresses(const LINEAR* Addresses, UINT Count) {
    UINT I = 0;
    UINT J = 0;

    if (!Addresses || Count == 0) {
        return FALSE;
    }

    for (I = 0; I < Count; I++) {
        if (Addresses[I] == 0) {
            return FALSE;
        }
        for (J = I + 1; J < Count; J++) {
            if (Addresses[I] == Addresses[J]) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

void TestBlockList(TEST_RESULTS* Results) {
    if (Results == NULL) {
        return;
    }

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Basic initialization, reserve, allocate and free
    Results->TestsRun++;
    {
        BLOCK_LIST List;
        BOOL Init = BlockListInit(&List, 64, 16, 0, 0);
        BOOL Reserve = BlockListReserve(&List, 32);
        LINEAR Pointer = BlockListAllocate(&List);
        BOOL FreeOk = BlockListFree(&List, Pointer);
        BOOL CapacityOk = (BlockListGetUsage(&List) == 0) &&
                          (BlockListGetFreeCount(&List) >= 32);

        BlockListFinalize(&List);

        if (Init && Reserve && Pointer != 0 && FreeOk && CapacityOk) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Basic path failed (init=%u reserve=%u pointer=%p free=%u capacity=%u/%u)"),
                  Init,
                  Reserve,
                  Pointer,
                  FreeOk,
                  BlockListGetUsage(&List),
                  BlockListGetFreeCount(&List));
        }
    }

    // Test 2: Growth across multiple slabs and shrink
    Results->TestsRun++;
    {
        BLOCK_LIST List;
        BOOL Init = BlockListInit(&List, 128, 8, 1, 0);
        UINT RequestedAllocations = 0;
        LINEAR* Addresses = NULL;
        UINT Index = 0;
        BOOL AllocationOk = TRUE;
        BOOL ShrinkOk = FALSE;
        UINT SlabsAfterGrow = 0;
        BOOL UniqueOk = FALSE;

        if (Init) {
            RequestedAllocations = (List.ObjectsPerSlab * 2);
            Addresses = (LINEAR*)KernelHeapAlloc(RequestedAllocations * (UINT)sizeof(LINEAR));
            if (Addresses != NULL) {
                MemorySet(Addresses, 0, RequestedAllocations * (UINT)sizeof(LINEAR));
                for (Index = 0; Index < RequestedAllocations; Index++) {
                    Addresses[Index] = BlockListAllocate(&List);
                    if (Addresses[Index] == 0) {
                        AllocationOk = FALSE;
                        break;
                    }
                }

                SlabsAfterGrow = BlockListGetSlabCount(&List);
                if (AllocationOk) {
                    UniqueOk = ValidateUniqueAddresses(Addresses, RequestedAllocations);
                }

                for (Index = 0; Index < RequestedAllocations; Index++) {
                    if (Addresses[Index] != 0) {
                        AllocationOk = AllocationOk && BlockListFree(&List, Addresses[Index]);
                    }
                }

                ShrinkOk = BlockListReleaseUnused(&List);
                KernelHeapFree(Addresses);
            } else {
                AllocationOk = FALSE;
            }
        }

        BOOL FinalStateOk = (BlockListGetUsage(&List) == 0) &&
                            (BlockListGetFreeCount(&List) == 0) &&
                            (BlockListGetSlabCount(&List) == 0);

        BlockListFinalize(&List);

        if (Init && AllocationOk && ShrinkOk && UniqueOk && FinalStateOk && SlabsAfterGrow >= 2) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Growth/shrink failed (init=%u alloc=%u shrink=%u unique=%u slabs=%u final=%u/%u/%u)"),
                  Init,
                  AllocationOk,
                  ShrinkOk,
                  UniqueOk,
                  SlabsAfterGrow,
                  BlockListGetUsage(&List),
                  BlockListGetFreeCount(&List),
                  BlockListGetSlabCount(&List));
        }
    }

    // Test 3: Double free detection
    Results->TestsRun++;
    {
        BLOCK_LIST List;
        BOOL Init = BlockListInit(&List, 96, 4, 1, 0);
        LINEAR Pointer = 0;
        BOOL FirstFree = FALSE;
        BOOL SecondFree = TRUE;

        if (Init) {
            Pointer = BlockListAllocate(&List);
            FirstFree = BlockListFree(&List, Pointer);
            SecondFree = BlockListFree(&List, Pointer);
        }

        BlockListFinalize(&List);

        if (Init && Pointer != 0 && FirstFree && !SecondFree) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Double free detection failed (init=%u ptr=%p first=%u second=%u)"),
                  Init,
                  Pointer,
                  FirstFree,
                  SecondFree);
        }
    }
}

/************************************************************************/
