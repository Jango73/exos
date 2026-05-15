
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


    Heap

\************************************************************************/

#ifndef HEAP_H_INCLUDED
#define HEAP_H_INCLUDED

#include "Base.h"
#include "process/Process.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

typedef struct tag_HEAP_CONTROL_BLOCK HEAP_CONTROL_BLOCK, *LPHEAP_CONTROL_BLOCK;

/***************************************************************************/

typedef BOOL (*HEAP_RESIZE_CALLBACK)(LPVOID Context, LPHEAP_CONTROL_BLOCK ControlBlock, UINT NewSize);

/***************************************************************************/

#define HEAP_NUM_SIZE_CLASSES 8
#define HEAP_MIN_BLOCK_SIZE 16
#define HEAP_MAX_SMALL_BLOCK_SIZE 2048
#define HEAP_BLOCK_FLAG_FREE 0x1

/***************************************************************************/

typedef struct tag_HEAP_BLOCK_HEADER {
    UINT TypeID;
    UINT Size;
    UINT Flags;
    struct tag_HEAP_BLOCK_HEADER* Next;
    struct tag_HEAP_BLOCK_HEADER* Prev;
} HEAP_BLOCK_HEADER, *LPHEAP_BLOCK_HEADER;

/***************************************************************************/

struct tag_HEAP_CONTROL_BLOCK {
    UINT TypeID;
    LINEAR HeapBase;
    UINT HeapSize;
    LPPROCESS Owner;
    LPHEAP_BLOCK_HEADER FreeLists[HEAP_NUM_SIZE_CLASSES];
    LPHEAP_BLOCK_HEADER LargeFreeList;
    LPVOID FirstUnallocated;
    LPVOID ResizeContext;
    HEAP_RESIZE_CALLBACK ResizeCallback;
    UINT MaximumSize;
    U32 RegionFlags;
};

/***************************************************************************/

typedef struct tag_PROCESS_MEMORY_INFO PROCESS_MEMORY_INFO, *LPPROCESS_MEMORY_INFO;

/***************************************************************************/

void HeapInit(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize);
void HeapConfigureGrowth(
    LINEAR HeapBase,
    LPVOID ResizeContext,
    HEAP_RESIZE_CALLBACK ResizeCallback,
    UINT MaximumSize,
    U32 RegionFlags);

// Allocates memory space in the calling process' heap
// Must provide the heap's limits.
LPVOID HeapAlloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, UINT Size);

// Reallocates memory space in the calling process' heap
// Must provide the heap's limits.
LPVOID HeapRealloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, LPVOID Pointer, UINT Size);

// Frees memory space in the calling process' heap
// Must provide the heap's limits.
void HeapFree_HBHS(LINEAR HeapBase, UINT HeapSize, LPVOID Pointer);

// Allocates memory space in the specified process' heap
LPVOID HeapAlloc_P(LPPROCESS Process, UINT Size);

// Reallocates memory space in the specified process' heap
LPVOID HeapRealloc_P(LPPROCESS Process, LPVOID Pointer, UINT Size);

// Frees memory space in the specified process' heap
void HeapFree_P(LPPROCESS Process, LPVOID Pointer);

// Allocates memory space in the kernel's heap
LPVOID KernelHeapAlloc(UINT Size);

// Reallocates memory space in the kernel's heap
LPVOID KernelHeapRealloc(LPVOID Pointer, UINT Size);

// Frees memory space in the kernel's heap
void KernelHeapFree(LPVOID Pointer);

// Allocates memory space in the calling process' heap
LPVOID HeapAlloc(UINT Size);

// Reallocates memory space in the calling process' heap
LPVOID HeapRealloc(LPVOID Pointer, UINT Size);

// Frees memory space in the calling process' heap
void HeapFree(LPVOID Pointer);

BOOL HeapQueryProcessMemoryInfo(LPPROCESS Process, LPPROCESS_MEMORY_INFO Info);

/***************************************************************************/

#pragma pack(pop)

#endif  // HEAP_H_INCLUDED
