
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


    List

\************************************************************************/

#include "utils/List.h"

#include "memory/Heap.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "text/CoreString.h"

static void ListFree(LPLIST This, LPVOID Pointer) {
    if (This == NULL || Pointer == NULL) {
        return;
    }

    if (This->MemFreeContextFunc != NULL) {
        This->MemFreeContextFunc(This->MemoryContext, Pointer);
        return;
    }

    if (This->MemFreeFunc != NULL) {
        This->MemFreeFunc(Pointer);
    }
}

/***************************************************************************/

/**
 * @brief Recursive QuickSort implementation.
 *
 * @param Base Pointer to the array being sorted.
 * @param Left Left boundary index for the current partition.
 * @param Rite Right boundary index for the current partition.
 * @param ItemSize Size of each element in bytes.
 * @param Func Comparison function.
 * @param Buffer Temporary buffer for swapping elements.
 */
static void RecursiveSort(U8* Base, I32 Left, I32 Rite, U32 ItemSize, COMPAREFUNC Func, U8* Buffer) {
    I32 i = Left;
    I32 j = Rite;

    U8* x = (U8*)KernelHeapAlloc(ItemSize);
    if (x == NULL) return;

    MemoryCopy(x, Base + (((Left + Rite) / 2) * ItemSize), ItemSize);

    while (i <= j) {
        while (Func((LPCVOID)x, (LPCVOID)(Base + (i * ItemSize))) > 0) i++;

        while (Func((LPCVOID)(Base + (j * ItemSize)), (LPCVOID)x) > 0) j--;

        if (i <= j) {
            if (i != j) {
                MemoryCopy(Buffer, Base + (i * ItemSize), ItemSize);
                MemoryCopy(Base + (i * ItemSize), Base + (j * ItemSize), ItemSize);
                MemoryCopy(Base + (j * ItemSize), Buffer, ItemSize);
            }
            i++;
            j--;
        }
    }

    KernelHeapFree(x);

    if (Left < j) RecursiveSort(Base, Left, j, ItemSize, Func, Buffer);
    if (i < Rite) RecursiveSort(Base, i, Rite, ItemSize, Func, Buffer);
}

/***************************************************************************/

/**
 * @brief Sorts an array using the QuickSort algorithm.
 *
 * @param Base Pointer to the array to be sorted.
 * @param NumItems Number of elements in the array.
 * @param ItemSize Size of each element in bytes.
 * @param Func Comparison function that returns > 0 if first parameter is greater than second.
 */
void QuickSort(LPVOID Base, U32 NumItems, U32 ItemSize, COMPAREFUNC Func) {
    U8* Buffer;

    if (Base == NULL) return;
    if (NumItems == 0) return;
    if (ItemSize == 0) return;
    if (Func == NULL) return;

    Buffer = (U8*)KernelHeapAlloc(ItemSize);

    SAFE_USE(Buffer) {
        RecursiveSort((U8*)Base, 0, NumItems - 1, ItemSize, Func, Buffer);
        KernelHeapFree(Buffer);
    } else {
        ERROR(TEXT("Failed to allocate temporary buffer"));
    }
}

/***************************************************************************/

/**
 * @brief Creates a new doubly linked list.
 *
 * @param ItemDestructor Optional destructor function called when items are deleted.
 * @param MemAlloc Memory allocation function (uses HeapAlloc if NULL).
 * @param MemFree Memory deallocation function (uses HeapFree if NULL).
 * @return Pointer to the new list, or NULL on failure.
 */
LPLIST NewList(LISTITEMDESTRUCTOR ItemDestructor, MEMALLOCFUNC MemAlloc, MEMFREEFUNC MemFree) {
    if (MemAlloc == NULL) MemAlloc = KernelHeapAlloc;
    if (MemFree == NULL) MemFree = KernelHeapFree;

    LPLIST This = (LPLIST)MemAlloc(sizeof(LIST));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(LIST));
    This->MemAllocFunc = MemAlloc;
    This->MemFreeFunc = MemFree;
    This->Destructor = ItemDestructor;

    return This;
}

/*************************************************************************************************/

LPLIST NewListEx(
    LISTITEMDESTRUCTOR ItemDestructor,
    LPVOID MemoryContext,
    MEMALLOCCONTEXTFUNC MemAlloc,
    MEMFREECONTEXTFUNC MemFree) {
    LPLIST This;

    if (MemAlloc == NULL || MemFree == NULL) {
        return NULL;
    }

    This = (LPLIST)MemAlloc(MemoryContext, sizeof(LIST));
    if (This == NULL) {
        return NULL;
    }

    MemorySet(This, 0, sizeof(LIST));
    This->MemoryContext = MemoryContext;
    This->MemAllocContextFunc = MemAlloc;
    This->MemFreeContextFunc = MemFree;
    This->Destructor = ItemDestructor;

    return This;
}

/*************************************************************************************************/

/**
 * @brief Deletes a list and all its items.
 *
 * @param This Pointer to the list to delete.
 * @return TRUE on success.
 */
U32 DeleteList(LPLIST This) {
    ListReset(This);
    ListFree(This, This);
    return TRUE;
}

/*************************************************************************************************/

/**
 * @brief Gets the number of items in the list.
 *
 * @param This Pointer to the list.
 * @return Number of items in the list.
 */
U32 ListGetSize(LPLIST This) {
    return This->NumItems;
}

/*************************************************************************************************/

/**
 * @brief Adds an item at the end of the list.
 *
 * @param This Pointer to the list.
 * @param Item Pointer to the item to add (must be castable to LPLISTNODE).
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddItem(LPLIST This, LPVOID Item) {
    LPLISTNODE NewNode = (LPLISTNODE)Item;

    SAFE_USE_VALID_2(This, Item) {
        if (NewNode) {
            if (This->First == NULL) {
                This->First = NewNode;
            } else {
                This->Last->Next = NewNode;
                NewNode->Prev = This->Last;
            }

            This->Last = NewNode;
            NewNode->Next = NULL;
            NewNode->Parent = NULL;

            This->NumItems++;

            return TRUE;
        }
    }

    return FALSE;
}

/*************************************************************************************************/

/**
 * @brief Adds an item at the end of the list with an explicit parent.
 *
 * @param This Pointer to the list.
 * @param Item Pointer to the item to add (must be castable to LPLISTNODE).
 * @param Parent Parent node to associate with the item.
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddItemWithParent(LPLIST This, LPVOID Item, LPLISTNODE Parent) {
    if (!ListAddItem(This, Item)) {
        return FALSE;
    }

    LPLISTNODE Node = (LPLISTNODE)Item;
    if (Node != NULL) {
        Node->Parent = Parent;
    }

    return TRUE;
}

/*************************************************************************************************/

/**
 * @brief Inserts an item before a reference item in the list.
 *
 * @param This Pointer to the list.
 * @param RefItem Reference item to insert before.
 * @param NewItem New item to insert.
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddBefore(LPLIST This, LPVOID RefItem, LPVOID NewItem) {
    LPLISTNODE CurNode = NULL;
    LPLISTNODE PrevNode = NULL;
    LPLISTNODE NewNode = (LPLISTNODE)NewItem;
    LPLISTNODE RefNode = (LPLISTNODE)RefItem;

    if (This->First == NULL) return ListAddItem(This, NewItem);

    CurNode = This->First;
    PrevNode = This->First;

    while (CurNode) {
        if (CurNode == RefNode) {
            if (CurNode == This->First) {
                This->First = NewNode;
                NewNode->Next = CurNode;
                NewNode->Prev = NULL;
                NewNode->Parent = NULL;
                CurNode->Prev = NewNode;

                This->NumItems++;

                return TRUE;
            } else {
                NewNode->Next = CurNode;
                NewNode->Prev = PrevNode;
                NewNode->Parent = NULL;
                PrevNode->Next = NewNode;
                CurNode->Prev = NewNode;

                This->NumItems++;

                return TRUE;
            }
        }
        PrevNode = CurNode;
        CurNode = CurNode->Next;
    }

    return ListAddItem(This, NewItem);
}

/*************************************************************************************************/

/**
 * @brief Inserts an item after a reference item in the list.
 *
 * @param This Pointer to the list.
 * @param RefItem Reference item to insert after.
 * @param NewItem New item to insert.
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddAfter(LPLIST This, LPVOID RefItem, LPVOID NewItem) {
    LPLISTNODE PrevNode = NULL;
    LPLISTNODE NextNode = NULL;
    LPLISTNODE NewNode = (LPLISTNODE)NewItem;
    LPLISTNODE RefNode = (LPLISTNODE)RefItem;

    if (This->First == NULL) return ListAddItem(This, NewItem);

    PrevNode = This->First;

    while (PrevNode) {
        if (PrevNode == RefNode) {
            NextNode = PrevNode->Next;

            if (NextNode) {
                PrevNode->Next = NewNode;
                NextNode->Prev = NewNode;
                NewNode->Prev = PrevNode;
                NewNode->Next = NextNode;
                NewNode->Parent = NULL;

                This->NumItems++;

                return TRUE;
            } else {
                return ListAddItem(This, NewItem);
            }
        }
        PrevNode = PrevNode->Next;
    }

    return ListAddItem(This, NewItem);
}

/*************************************************************************************************/

/**
 * @brief Adds an item at the beginning of the list.
 *
 * @param This Pointer to the list.
 * @param Item Item to add at the head.
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddHead(LPLIST This, LPVOID Item) {
    return ListAddBefore(This, This->First, Item);
}

/*************************************************************************************************/

/**
 * @brief Adds an item at the end of the list.
 *
 * @param This Pointer to the list.
 * @param Item Item to add at the tail.
 * @return TRUE on success, FALSE on failure.
 */
U32 ListAddTail(LPLIST This, LPVOID Item) {
    return ListAddAfter(This, This->Last, Item);
}

/*************************************************************************************************/

/**
 * @brief Removes an item from the list without destroying it.
 *
 * @param This Pointer to the list.
 * @param Item Item to remove from the list.
 * @return Pointer to the removed item, or NULL if not found.
 */
LPVOID ListRemove(LPLIST This, LPVOID Item) {
    LPLISTNODE Temp;
    LPLISTNODE Node = (LPLISTNODE)Item;

    if (Node == NULL) return NULL;

    if (This->Current == Node) {
        Temp = Node;
        This->Current = NULL;

        if (Temp->Prev) {
            Temp->Prev->Next = Temp->Next;
            This->Current = Temp->Prev;
        }

        if (Temp->Next) {
            Temp->Next->Prev = Temp->Prev;
            This->Current = Temp->Next;
        }

        This->NumItems--;

        if (This->First == Temp) This->First = Temp->Next;
        if (This->Last == Temp) This->Last = Temp->Prev;

        Temp->Next = NULL;
        Temp->Prev = NULL;
        Temp->Parent = NULL;

        return Temp;
    }

    Temp = This->First;

    while (Temp) {
        if (Temp == Node) {
            if (Temp->Prev) Temp->Prev->Next = Temp->Next;
            if (Temp->Next) Temp->Next->Prev = Temp->Prev;

            This->NumItems--;

            if (This->First == Temp) This->First = Temp->Next;
            if (This->Last == Temp) This->Last = Temp->Prev;

            Temp->Next = NULL;
            Temp->Prev = NULL;
            Temp->Parent = NULL;

            return Temp;
        } else {
            Temp = Temp->Next;
        }
    }

    return NULL;
}

/*************************************************************************************************/

/**
 * @brief Removes and destroys an item from the list.
 *
 * @param This Pointer to the list.
 * @param Item Item to erase from the list.
 */
void ListErase(LPLIST This, LPVOID Item) {
    Item = ListRemove(This, Item);

    // if (Item && This->MemFreeFunc) This->MemFreeFunc(Item);

    if (Item && This->Destructor) {
        This->Destructor(Item);
    }
}

/*************************************************************************************************/

/**
 * @brief Removes and destroys the last item in the list.
 *
 * @param This Pointer to the list.
 * @return TRUE if an item was erased, FALSE if list was empty.
 */
U32 ListEraseLast(LPLIST This) {
    LPLISTNODE Node = NULL;

    for (Node = This->First; Node != NULL; Node = Node->Next) {
        if (Node->Next == NULL) {
            ListErase(This, Node);
            return TRUE;
        }
    }

    return FALSE;
}

/*************************************************************************************************/

/**
 * @brief Finds and erases a specific item from the list.
 *
 * @param This Pointer to the list.
 * @param Item Item to find and erase.
 * @return TRUE if item was found and erased, FALSE otherwise.
 */
U32 ListEraseItem(LPLIST This, LPVOID Item) {
    LPLISTNODE Node = NULL;

    for (Node = This->First; Node != NULL; Node = Node->Next) {
        if (Node == (LPLISTNODE)Item) {
            ListErase(This, Node);
            return TRUE;
        }
    }

    return FALSE;
}

/*************************************************************************************************/

/**
 * @brief Removes and destroys all items in the list.
 *
 * @param This Pointer to the list.
 */
void ListReset(LPLIST This) {
    LPLISTNODE Node = This->First;

    while (Node) {
        This->Current = Node;
        Node = This->Current->Next;

        SAFE_USE(This->Destructor) {
            This->Destructor(This->Current);
        } else {
            // This->MemFreeFunc(This->Current);
        }
    }

    This->First = NULL;
    This->Current = NULL;
    This->Last = NULL;
    This->NumItems = 0;
}

/*************************************************************************************************/

/**
 * @brief Gets an item at a specific index in the list.
 *
 * @param This Pointer to the list.
 * @param Index Zero-based index of the item to retrieve.
 * @return Pointer to the item at the specified index, or NULL if index is out of bounds.
 */
LPVOID ListGetItem(LPLIST This, U32 Index) {
    LPLISTNODE Node = This->First;
    U32 Counter = 0;

    if (This->NumItems == 0) return NULL;

    if (Index >= This->NumItems) return NULL;

    while (Node) {
        if (Counter == Index) break;
        Counter++;
        Node = Node->Next;
    }

    return Node;
}

/*************************************************************************************************/

/**
 * @brief Gets the index of a specific item in the list.
 *
 * @param This Pointer to the list.
 * @param Item Item to find the index of.
 * @return Index of the item, or MAX_U32 if not found.
 */
U32 ListGetItemIndex(LPLIST This, LPVOID Item) {
    LPLISTNODE Node = NULL;
    U32 Index = MAX_U32;

    for (Node = This->First; Node; Node = Node->Next) {
        Index++;
        if (Node == (LPLISTNODE)Item) break;
    }

    return Index;
}

/*************************************************************************************************/

/**
 * @brief Merges two lists by appending all items from the second list to the first.
 *
 * @param This Destination list to merge into.
 * @param That Source list to merge from (will be deleted after merge).
 * @return Pointer to the merged list (This).
 */
LPLIST ListMergeList(LPLIST This, LPLIST That) {
    LPLISTNODE Node = NULL;

    for (Node = That->First; Node != NULL; Node = Node->Next) {
        ListAddItem(This, Node);
    }

    DeleteList(That);

    return This;
}

/*************************************************************************************************/

/**
 * @brief Sorts the list using a comparison function.
 *
 * @param This Pointer to the list to sort.
 * @param Func Comparison function that returns > 0 if first parameter is greater than second.
 * @return TRUE on success, FALSE on failure.
 */
BOOL ListSort(LPLIST This, COMPAREFUNC Func) {
    LPLISTNODE Node = NULL;
    LPVOID* Data = NULL;
    U32 NumItems = 0;
    U32 Index = 0;

    if (This->NumItems == 0) return TRUE;

    NumItems = This->NumItems;
    Data = (LPVOID*)This->MemAllocFunc(sizeof(LPVOID) * NumItems);

    if (Data == NULL) return FALSE;

    // Record all items in the list and clear nodes

    for (Node = This->First, Index = 0; Node; Node = Node->Next, Index++) {
        Data[Index] = Node;
    }

    // Clear the list

    This->First = NULL;
    This->Last = NULL;
    This->Current = NULL;
    This->NumItems = 0;

    // Do the sort

    QuickSort(Data, NumItems, sizeof(LPVOID), Func);

    // Rebuild the list with the sorted items

    for (Index = 0; Index < NumItems; Index++) {
        ListAddItem(This, Data[Index]);
    }

    This->MemFreeFunc(Data);

    return TRUE;
}
