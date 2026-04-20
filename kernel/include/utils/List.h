
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


    List

\************************************************************************/

#ifndef LIST_H_INCLUDED
#define LIST_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#define LISTNODE_FIELDS     \
    OBJECT_FIELDS           \
    LPLISTNODE Next;        \
    LPLISTNODE Prev;        \
    LPLISTNODE Parent;

typedef struct tag_LISTNODE LISTNODE, *LPLISTNODE;

struct tag_LISTNODE {
    LISTNODE_FIELDS
};

/*************************************************************************************************/

typedef OBJECTDESTRUCTOR LISTITEMDESTRUCTOR;
typedef LPVOID (*MEMALLOCFUNC)(UINT);
typedef void (*MEMFREEFUNC)(LPVOID);
typedef LPVOID (*MEMALLOCCONTEXTFUNC)(LPVOID, UINT);
typedef void (*MEMFREECONTEXTFUNC)(LPVOID, LPVOID);

typedef struct tag_LIST {
    LPLISTNODE First;
    LPLISTNODE Last;
    LPLISTNODE Current;
    UINT NumItems;
    LPVOID MemoryContext;
    MEMALLOCFUNC MemAllocFunc;
    MEMFREEFUNC MemFreeFunc;
    MEMALLOCCONTEXTFUNC MemAllocContextFunc;
    MEMFREECONTEXTFUNC MemFreeContextFunc;
    LISTITEMDESTRUCTOR Destructor;
} LIST, *LPLIST;

/*************************************************************************************************/

typedef I32 (*COMPAREFUNC)(LPCVOID, LPCVOID);

/*************************************************************************************************/

void QuickSort(LPVOID Base, U32 NumItems, U32 ItemSize, COMPAREFUNC Func);
LPLIST NewList(LISTITEMDESTRUCTOR Destructor, MEMALLOCFUNC Alloc, MEMFREEFUNC Free);
LPLIST NewListEx(
    LISTITEMDESTRUCTOR Destructor,
    LPVOID MemoryContext,
    MEMALLOCCONTEXTFUNC Alloc,
    MEMFREECONTEXTFUNC Free);
U32 DeleteList(LPLIST List);
U32 ListGetSize(LPLIST List);
U32 ListAddItem(LPLIST List, LPVOID Item);
U32 ListAddItemWithParent(LPLIST List, LPVOID Item, LPLISTNODE Parent);
U32 ListAddBefore(LPLIST List, LPVOID RefItem, LPVOID NewItem);
U32 ListAddAfter(LPLIST List, LPVOID RefItem, LPVOID NewItem);
U32 ListAddHead(LPLIST List, LPVOID Item);
U32 ListAddTail(LPLIST List, LPVOID Item);
LPVOID ListRemove(LPLIST List, LPVOID Item);
void ListErase(LPLIST List, LPVOID Item);
U32 ListEraseLast(LPLIST List);
U32 ListEraseItem(LPLIST List, LPVOID Item);
void ListReset(LPLIST List);
LPVOID ListGetItem(LPLIST List, U32 Index);
U32 ListGetItemIndex(LPLIST List, LPVOID Item);
LPLIST ListMergeList(LPLIST List, LPLIST That);
BOOL ListSort(LPLIST List, COMPAREFUNC Func);

/*************************************************************************************************/

#pragma pack(pop)

#endif
