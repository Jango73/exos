
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


    Radix Tree - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "utils/RadixTree.h"

/************************************************************************/

typedef struct tag_RADIX_TREE_ITER_CONTEXT {
    UINT* Handles;
    LINEAR* Values;
    UINT Capacity;
    UINT Count;
} RADIX_TREE_ITER_CONTEXT, *LPRADIX_TREE_ITER_CONTEXT;

/************************************************************************/

static BOOL CollectEntry(UINT Handle, LINEAR Value, LPVOID Context) {
    LPRADIX_TREE_ITER_CONTEXT Iter = (LPRADIX_TREE_ITER_CONTEXT)Context;

    if (Iter == NULL || Iter->Handles == NULL || Iter->Values == NULL) {
        return FALSE;
    }

    if (Iter->Count >= Iter->Capacity) {
        return FALSE;
    }

    Iter->Handles[Iter->Count] = Handle;
    Iter->Values[Iter->Count] = Value;
    Iter->Count++;

    return TRUE;
}

/************************************************************************/

void TestRadixTree(TEST_RESULTS* Results) {
    if (Results == NULL) {
        return;
    }

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Create and destroy
    Results->TestsRun++;
    {
        LPRADIX_TREE Tree = RadixTreeCreate();
        BOOL Created = (Tree != NULL);

        if (Tree != NULL) {
            RadixTreeDestroy(Tree);
        }

        if (Created) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Creation failed"));
        }
    }

    // Test 2: Insert and find entries
    Results->TestsRun++;
    {
        LPRADIX_TREE Tree = RadixTreeCreate();
        BOOL InsertedAll = TRUE;
        BOOL FoundAll = TRUE;
        UINT EntryCount = 64;

        if (Tree == NULL) {
            InsertedAll = FALSE;
            FoundAll = FALSE;
        } else {
            for (UINT Index = 0; Index < EntryCount; Index++) {
                LINEAR Value = (LINEAR)(0x1000U + (Index * 0x10U));
                if (!RadixTreeInsert(Tree, Index, Value)) {
                    InsertedAll = FALSE;
                    break;
                }
            }

            if (InsertedAll) {
                for (UINT Index = 0; Index < EntryCount; Index++) {
                    LINEAR Value = (LINEAR)(0x1000U + (Index * 0x10U));
                    LINEAR Retrieved = RadixTreeFind(Tree, Index);

                    if (Retrieved != Value) {
                        FoundAll = FALSE;
                        break;
                    }
                }
            }

            RadixTreeDestroy(Tree);
        }

        if (InsertedAll && FoundAll) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Insert/find failed (inserted=%u found=%u)"), InsertedAll, FoundAll);
        }
    }

    // Test 3: Removal and pruning
    Results->TestsRun++;
    {
        LPRADIX_TREE Tree = RadixTreeCreate();
        BOOL RemovedAll = TRUE;
        BOOL Cleared = TRUE;
        UINT EntryCount = 32;

        if (Tree != NULL) {
            for (UINT Index = 0; Index < EntryCount; Index++) {
                LINEAR Value = (LINEAR)(0x8000U + (Index * 0x20U));
                if (!RadixTreeInsert(Tree, Index, Value)) {
                    RemovedAll = FALSE;
                    break;
                }
            }

            if (RemovedAll) {
                for (UINT Index = 0; Index < EntryCount; Index++) {
                    if (!RadixTreeRemove(Tree, Index)) {
                        RemovedAll = FALSE;
                        break;
                    }
                }
            }

            if (RadixTreeGetCount(Tree) != 0U) {
                Cleared = FALSE;
            }

            RadixTreeDestroy(Tree);
        } else {
            RemovedAll = FALSE;
            Cleared = FALSE;
        }

        if (RemovedAll && Cleared) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Removal failed (removed=%u cleared=%u)"), RemovedAll, Cleared);
        }
    }

    // Test 4: Iteration order and coverage
    Results->TestsRun++;
    {
        LPRADIX_TREE Tree = RadixTreeCreate();
        BOOL IterationOk = TRUE;
        UINT EntryCount = 48;

        if (Tree != NULL) {
            for (UINT Index = 0; Index < EntryCount; Index++) {
                LINEAR Value = (LINEAR)(0xA000U + (Index * 0x08U));
                if (!RadixTreeInsert(Tree, Index, Value)) {
                    IterationOk = FALSE;
                    break;
                }
            }

            if (IterationOk) {
                RADIX_TREE_ITER_CONTEXT Context;
                Context.Capacity = EntryCount;
                Context.Count = 0;
                Context.Handles = (UINT*)KernelHeapAlloc(Context.Capacity * (UINT)sizeof(UINT));
                Context.Values = (LINEAR*)KernelHeapAlloc(Context.Capacity * (UINT)sizeof(LINEAR));

                if (Context.Handles == NULL || Context.Values == NULL) {
                    IterationOk = FALSE;
                } else {
                    MemorySet(Context.Handles, 0, Context.Capacity * (UINT)sizeof(UINT));
                    MemorySet(Context.Values, 0, Context.Capacity * (UINT)sizeof(LINEAR));

                    if (!RadixTreeIterate(Tree, CollectEntry, &Context)) {
                        IterationOk = FALSE;
                    } else if (Context.Count != EntryCount) {
                        IterationOk = FALSE;
                    } else {
                        for (UINT Index = 0; Index < EntryCount; Index++) {
                            LINEAR Expected = (LINEAR)(0xA000U + (Index * 0x08U));
                            BOOL Found = FALSE;

                            for (UINT J = 0; J < Context.Count; J++) {
                                if (Context.Handles[J] == Index && Context.Values[J] == Expected) {
                                    Found = TRUE;
                                    break;
                                }
                            }

                            if (!Found) {
                                IterationOk = FALSE;
                                break;
                            }
                        }
                    }
                }

                if (Context.Handles != NULL) {
                    KernelHeapFree(Context.Handles);
                }
                if (Context.Values != NULL) {
                    KernelHeapFree(Context.Values);
                }
            }

            RadixTreeDestroy(Tree);
        } else {
            IterationOk = FALSE;
        }

        if (IterationOk) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Iteration failed"));
        }
    }
}

/************************************************************************/
