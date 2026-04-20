
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


    Radix Tree

\************************************************************************/

#include "utils/RadixTree.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"
#include "utils/BlockList.h"

/************************************************************************/

#define RADIX_TREE_BITS_PER_LEVEL 4
#define RADIX_TREE_RADIX (1 << RADIX_TREE_BITS_PER_LEVEL)
#define RADIX_TREE_LEVEL_MASK (RADIX_TREE_RADIX - 1)
#define RADIX_TREE_KEY_BITS ((UINT)(sizeof(UINT) * 8))
#define RADIX_TREE_MAX_LEVELS ((RADIX_TREE_KEY_BITS + RADIX_TREE_BITS_PER_LEVEL - 1) / RADIX_TREE_BITS_PER_LEVEL)
#define RADIX_TREE_NODES_PER_SLAB 32
#define RADIX_TREE_INITIAL_SLABS 1

/************************************************************************/

typedef struct tag_RADIX_TREE_NODE RADIX_TREE_NODE, *LPRADIX_TREE_NODE;

struct tag_RADIX_TREE_NODE {
    LPRADIX_TREE_NODE Parent;
    UINT Level;
    UINT SlotIndex;
    U16 ChildMask;
    U16 ValueMask;
    LINEAR Slots[RADIX_TREE_RADIX];
};

struct tag_RADIX_TREE {
    LPRADIX_TREE_NODE Root;
    BLOCK_LIST NodeAllocator;
    MUTEX Mutex;
    UINT EntryCount;
};

/************************************************************************/

static UINT RadixTreeLevelToShift(UINT Level) {
    if (Level >= RADIX_TREE_MAX_LEVELS) {
        return 0;
    }

    UINT RemainingLevels = (UINT)(RADIX_TREE_MAX_LEVELS - Level - 1);
    return (UINT)(RemainingLevels * RADIX_TREE_BITS_PER_LEVEL);
}

/************************************************************************/

static UINT RadixTreeExtractIndex(UINT Handle, UINT Level) {
    if (Level >= RADIX_TREE_MAX_LEVELS) {
        return 0;
    }

    UINT Shift = RadixTreeLevelToShift(Level);
    return (UINT)((Handle >> Shift) & RADIX_TREE_LEVEL_MASK);
}

/************************************************************************/

static LPRADIX_TREE_NODE RadixTreeAllocateNode(LPRADIX_TREE Tree,
                                               LPRADIX_TREE_NODE Parent,
                                               UINT Level,
                                               UINT SlotIndex) {
    if (Tree == NULL) {
        return NULL;
    }

    LINEAR Address = BlockListAllocate(&Tree->NodeAllocator);
    if (Address == 0) {
        ERROR(TEXT("Cannot allocate node (level=%u slot=%u)"), Level, SlotIndex);
        return NULL;
    }

    LPRADIX_TREE_NODE Node = (LPRADIX_TREE_NODE)Address;
    SAFE_USE(Node) {
        MemorySet(Node, 0, (UINT)sizeof(RADIX_TREE_NODE));
        Node->Parent = Parent;
        Node->Level = Level;
        Node->SlotIndex = SlotIndex;
    }

    return Node;
}

/************************************************************************/

static void RadixTreeReleaseNode(LPRADIX_TREE Tree, LPRADIX_TREE_NODE Node) {
    if (Tree == NULL || Node == NULL) {
        return;
    }

    for (UINT Index = 0; Index < RADIX_TREE_RADIX; Index++) {
        UINT Bit = (UINT)(1 << Index);

        if ((Node->ChildMask & (U16)Bit) != 0) {
            LPRADIX_TREE_NODE Child = (LPRADIX_TREE_NODE)Node->Slots[Index];
            RadixTreeReleaseNode(Tree, Child);
        }

        Node->Slots[Index] = 0;
    }

    Node->ChildMask = 0;
    Node->ValueMask = 0;

    BlockListFree(&Tree->NodeAllocator, (LINEAR)Node);
}

/************************************************************************/

static void RadixTreeTrimUpwards(LPRADIX_TREE Tree, LPRADIX_TREE_NODE Node) {
    while (Tree != NULL && Node != NULL && Node->Parent != NULL) {
        if (Node->ChildMask != 0 || Node->ValueMask != 0) {
            break;
        }

        LPRADIX_TREE_NODE Parent = Node->Parent;
        UINT SlotIndex = Node->SlotIndex;
        UINT Bit = (UINT)(1 << SlotIndex);

        Parent->Slots[SlotIndex] = 0;
        Parent->ChildMask &= (U16)(~Bit);

        BlockListFree(&Tree->NodeAllocator, (LINEAR)Node);
        Node = Parent;
    }
}

/************************************************************************/

static LPRADIX_TREE_NODE RadixTreeDescend(LPRADIX_TREE Tree,
                                          LPRADIX_TREE_NODE Node,
                                          UINT NextIndex,
                                          UINT NextLevel) {
    UINT Bit = (UINT)(1 << NextIndex);

    if ((Node->ChildMask & (U16)Bit) == 0) {
        LPRADIX_TREE_NODE Child = RadixTreeAllocateNode(Tree, Node, NextLevel, NextIndex);
        if (Child == NULL) {
            return NULL;
        }

        Node->Slots[NextIndex] = (LINEAR)Child;
        Node->ChildMask |= (U16)Bit;
    }

    return (LPRADIX_TREE_NODE)Node->Slots[NextIndex];
}

/************************************************************************/

static BOOL RadixTreeIterateNode(LPRADIX_TREE_NODE Node,
                                 UINT HandlePrefix,
                                 RADIX_TREE_VISITOR Visitor,
                                 LPVOID Context,
                                 BOOL* Continue) {
    if (Node == NULL || Visitor == NULL || Continue == NULL) {
        return FALSE;
    }

    UINT Level = Node->Level;
    UINT Shift = RadixTreeLevelToShift(Level);

    for (UINT Index = 0; Index < RADIX_TREE_RADIX && *Continue; Index++) {
        UINT Bit = (UINT)(1 << Index);

        if ((Node->ChildMask & (U16)Bit) != 0) {
            LPRADIX_TREE_NODE Child = (LPRADIX_TREE_NODE)Node->Slots[Index];
            UINT NextPrefix = HandlePrefix | (Index << Shift);

            if (!RadixTreeIterateNode(Child, NextPrefix, Visitor, Context, Continue)) {
                return FALSE;
            }
        } else if ((Node->ValueMask & (U16)Bit) != 0) {
            UINT Handle = HandlePrefix | (Index << Shift);
            LINEAR Value = Node->Slots[Index];

            if (!Visitor(Handle, Value, Context)) {
                *Continue = FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Create a new radix tree instance.
 *
 * @return Newly created radix tree or NULL if allocation failed.
 */
LPRADIX_TREE RadixTreeCreate(void) {
    LPRADIX_TREE Tree = (LPRADIX_TREE)KernelHeapAlloc((UINT)sizeof(RADIX_TREE));

    if (Tree == NULL) {
        ERROR(TEXT("KernelHeapAlloc failed"));
        return NULL;
    }

    MemorySet(Tree, 0, (UINT)sizeof(RADIX_TREE));
    InitMutex(&Tree->Mutex);

    BOOL AllocatorInit = BlockListInit(&Tree->NodeAllocator,
                                       (UINT)sizeof(RADIX_TREE_NODE),
                                       RADIX_TREE_NODES_PER_SLAB,
                                       RADIX_TREE_INITIAL_SLABS,
                                       0);
    if (!AllocatorInit) {
        ERROR(TEXT("BlockListInit failed"));
        KernelHeapFree(Tree);
        return NULL;
    }

    Tree->Root = RadixTreeAllocateNode(Tree, NULL, 0, 0);
    if (Tree->Root == NULL) {
        BlockListFinalize(&Tree->NodeAllocator);
        KernelHeapFree(Tree);
        return NULL;
    }

    DEBUG(TEXT("Tree=%p created"), Tree);

    return Tree;
}

/************************************************************************/

/**
 * @brief Destroy a radix tree and release all associated resources.
 *
 * @param Tree Radix tree to destroy.
 */
void RadixTreeDestroy(LPRADIX_TREE Tree) {
    if (Tree == NULL) {
        return;
    }

    LockMutex(&Tree->Mutex, INFINITY);

    if (Tree->Root != NULL) {
        RadixTreeReleaseNode(Tree, Tree->Root);
        Tree->Root = NULL;
    }

    Tree->EntryCount = 0;

    UnlockMutex(&Tree->Mutex);

    BlockListFinalize(&Tree->NodeAllocator);
    KernelHeapFree(Tree);

    DEBUG(TEXT("Tree destroyed"));
}

/************************************************************************/

/**
 * @brief Insert or update a value in the radix tree.
 *
 * @param Tree   Radix tree instance.
 * @param Handle Handle used as key.
 * @param Value  LINEAR pointer to associate with the handle.
 * @return TRUE when the value is stored, FALSE otherwise.
 */
BOOL RadixTreeInsert(LPRADIX_TREE Tree, UINT Handle, LINEAR Value) {
    if (Tree == NULL || Value == 0) {
        ERROR(TEXT("Invalid parameters (tree=%p handle=%u value=%p)"), Tree, Handle, Value);
        return FALSE;
    }

    LockMutex(&Tree->Mutex, INFINITY);

    LPRADIX_TREE_NODE Node = Tree->Root;

    for (UINT Level = 0; Level < RADIX_TREE_MAX_LEVELS; Level++) {
        UINT Index = RadixTreeExtractIndex(Handle, Level);
        UINT Bit = (UINT)(1 << Index);

        if (Level == RADIX_TREE_MAX_LEVELS - 1) {
            if ((Node->ChildMask & (U16)Bit) != 0) {
                WARNING(TEXT("Leaf collision detected (handle=%u index=%u)"), Handle, Index);
                UnlockMutex(&Tree->Mutex);
                return FALSE;
            }

            if ((Node->ValueMask & (U16)Bit) == 0) {
                Tree->EntryCount++;
            }

            Node->Slots[Index] = Value;
            Node->ValueMask |= (U16)Bit;
            UnlockMutex(&Tree->Mutex);

            return TRUE;
        }

        LPRADIX_TREE_NODE Child = RadixTreeDescend(Tree, Node, Index, Level + 1);
        if (Child == NULL) {
            UnlockMutex(&Tree->Mutex);
            return FALSE;
        }

        Node = Child;
    }

    UnlockMutex(&Tree->Mutex);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Remove a value from the radix tree.
 *
 * @param Tree   Radix tree instance.
 * @param Handle Handle to remove.
 * @return TRUE when the handle was present and removed, FALSE otherwise.
 */
BOOL RadixTreeRemove(LPRADIX_TREE Tree, UINT Handle) {
    if (Tree == NULL) {
        return FALSE;
    }

    LockMutex(&Tree->Mutex, INFINITY);

    LPRADIX_TREE_NODE Node = Tree->Root;

    for (UINT Level = 0; Level < RADIX_TREE_MAX_LEVELS; Level++) {
        UINT Index = RadixTreeExtractIndex(Handle, Level);
        UINT Bit = (UINT)(1 << Index);

        if (Level == RADIX_TREE_MAX_LEVELS - 1) {
            if ((Node->ValueMask & (U16)Bit) == 0) {
                UnlockMutex(&Tree->Mutex);
                return FALSE;
            }

            Node->Slots[Index] = 0;
            Node->ValueMask &= (U16)(~Bit);
            if (Tree->EntryCount > 0) {
                Tree->EntryCount--;
            }

            RadixTreeTrimUpwards(Tree, Node);

            UnlockMutex(&Tree->Mutex);
            return TRUE;
        }

        if ((Node->ChildMask & (U16)Bit) == 0) {
            UnlockMutex(&Tree->Mutex);
            return FALSE;
        }

        Node = (LPRADIX_TREE_NODE)Node->Slots[Index];
    }

    UnlockMutex(&Tree->Mutex);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Find a value in the radix tree.
 *
 * @param Tree   Radix tree instance.
 * @param Handle Handle to look up.
 * @return Stored LINEAR pointer or 0 when not found.
 */
LINEAR RadixTreeFind(LPRADIX_TREE Tree, UINT Handle) {
    if (Tree == NULL) {
        return 0;
    }

    LockMutex(&Tree->Mutex, INFINITY);

    LPRADIX_TREE_NODE Node = Tree->Root;

    for (UINT Level = 0; Level < RADIX_TREE_MAX_LEVELS; Level++) {
        UINT Index = RadixTreeExtractIndex(Handle, Level);
        UINT Bit = (UINT)(1 << Index);

        if (Level == RADIX_TREE_MAX_LEVELS - 1) {
            if ((Node->ValueMask & (U16)Bit) == 0) {
                UnlockMutex(&Tree->Mutex);
                return 0;
            }

            LINEAR Value = Node->Slots[Index];
            UnlockMutex(&Tree->Mutex);
            return Value;
        }

        if ((Node->ChildMask & (U16)Bit) == 0) {
            UnlockMutex(&Tree->Mutex);
            return 0;
        }

        Node = (LPRADIX_TREE_NODE)Node->Slots[Index];
    }

    UnlockMutex(&Tree->Mutex);
    return 0;
}

/************************************************************************/

/**
 * @brief Iterate over all stored entries.
 *
 * @param Tree     Radix tree instance.
 * @param Visitor  Callback invoked for each entry.
 * @param Context  Opaque context forwarded to the callback.
 * @return TRUE when the iteration completed, FALSE if aborted or invalid.
 */
BOOL RadixTreeIterate(LPRADIX_TREE Tree, RADIX_TREE_VISITOR Visitor, LPVOID Context) {
    if (Tree == NULL || Visitor == NULL) {
        return FALSE;
    }

    LockMutex(&Tree->Mutex, INFINITY);

    BOOL Continue = TRUE;
    BOOL Result = RadixTreeIterateNode(Tree->Root, 0, Visitor, Context, &Continue);

    UnlockMutex(&Tree->Mutex);
    return Result && Continue;
}

/************************************************************************/

/**
 * @brief Retrieve the number of stored entries.
 *
 * @param Tree Radix tree instance.
 * @return Number of active entries.
 */
UINT RadixTreeGetCount(const RADIX_TREE* Tree) {
    if (Tree == NULL) {
        return 0;
    }

    return Tree->EntryCount;
}
