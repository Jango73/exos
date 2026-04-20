
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


    Handle Map

\************************************************************************/

#include "utils/HandleMap.h"

#include "console/Console.h"
#include "text/CoreString.h"
#include "log/Log.h"
#include "memory/Memory.h"

/************************************************************************/

#define HANDLE_MAP_ENTRIES_PER_SLAB 64
#define HANDLE_MAP_INITIAL_SLABS 1

/************************************************************************/

typedef struct tag_HANDLE_MAP_ENTRY {
    UINT Handle;
    LINEAR Pointer;
    BOOL Attached;
} HANDLE_MAP_ENTRY, *LPHANDLE_MAP_ENTRY;

/************************************************************************/

typedef struct tag_HANDLE_MAP_POINTER_SEARCH {
    LINEAR Pointer;
    UINT Handle;
    BOOL Found;
} HANDLE_MAP_POINTER_SEARCH, *LPHANDLE_MAP_POINTER_SEARCH;

/************************************************************************/

static BOOL HandleMapPointerSearchVisitor(UINT Handle, LINEAR Value, LPVOID Context) {
    LPHANDLE_MAP_POINTER_SEARCH Search = (LPHANDLE_MAP_POINTER_SEARCH)Context;
    LPHANDLE_MAP_ENTRY Entry = (LPHANDLE_MAP_ENTRY)Value;

    if (Search == NULL) {
        return FALSE;
    }

    SAFE_USE(Entry) {
        if (Entry->Attached && Entry->Pointer == Search->Pointer) {
            Search->Handle = Handle;
            Search->Found = TRUE;
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static LPHANDLE_MAP_ENTRY HandleMapAllocateEntry(LPHANDLE_MAP Map, UINT Handle) {
    if (Map == NULL) {
        return NULL;
    }

    LINEAR Address = BlockListAllocate(&Map->EntryAllocator);
    if (Address == 0) {
        ERROR(TEXT("BlockListAllocate failed (handle=%u)"), Handle);
        return NULL;
    }

    LPHANDLE_MAP_ENTRY Entry = (LPHANDLE_MAP_ENTRY)Address;

    SAFE_USE(Entry) {
        MemorySet(Entry, 0, (UINT)sizeof(HANDLE_MAP_ENTRY));
        Entry->Handle = Handle;
        Entry->Pointer = 0;
        Entry->Attached = FALSE;
    }

    return Entry;
}

/************************************************************************/

static void HandleMapReleaseEntry(LPHANDLE_MAP Map, LPHANDLE_MAP_ENTRY Entry) {
    if (Map == NULL || Entry == NULL) {
        return;
    }

    SAFE_USE(Entry) {
        Entry->Handle = 0;
        Entry->Pointer = 0;
        Entry->Attached = FALSE;
    }

    BlockListFree(&Map->EntryAllocator, (LINEAR)Entry);
}

/************************************************************************/

void HandleMapInit(LPHANDLE_MAP Map) {
    if (Map == NULL) {
        ConsolePanic(TEXT("[HandleMapInit] Map pointer is NULL"));
        return;
    }

    MemorySet(Map, 0, (UINT)sizeof(HANDLE_MAP));
    InitMutex(&Map->Mutex);

    Map->Tree = RadixTreeCreate();
    if (Map->Tree == NULL) {
        ConsolePanic(TEXT("[HandleMapInit] RadixTreeCreate failed"));
        return;
    }

    BOOL AllocatorReady = BlockListInit(&Map->EntryAllocator,
                                        (UINT)sizeof(HANDLE_MAP_ENTRY),
                                        HANDLE_MAP_ENTRIES_PER_SLAB,
                                        HANDLE_MAP_INITIAL_SLABS,
                                        0);
    if (!AllocatorReady) {
        ConsolePanic(TEXT("[HandleMapInit] BlockListInit failed"));
        return;
    }

    Map->NextHandle = HANDLE_MINIMUM;

    DEBUG(TEXT("Initialized handle map"));
}

/************************************************************************/

static LPHANDLE_MAP_ENTRY HandleMapGetEntryLocked(LPHANDLE_MAP Map, UINT Handle) {
    if (Map == NULL || Map->Tree == NULL) {
        return NULL;
    }

    LINEAR Address = RadixTreeFind(Map->Tree, Handle);
    if (Address == 0) {
        return NULL;
    }

    return (LPHANDLE_MAP_ENTRY)Address;
}

/************************************************************************/

UINT HandleMapAllocateHandle(LPHANDLE_MAP Map, UINT* HandleOut) {
    if (Map == NULL || HandleOut == NULL) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    UINT Candidate = Map->NextHandle;

    while (TRUE) {
        if (Candidate > MAX_U32) {
            UnlockMutex(&Map->Mutex);
            ConsolePanic(TEXT("[HandleMapAllocateHandle] Handle space exhausted"));
            return HANDLE_MAP_ERROR_OUT_OF_HANDLES;
        }

        if (HandleMapGetEntryLocked(Map, Candidate) == NULL) {
            break;
        }

        if (Candidate == MAX_U32) {
            UnlockMutex(&Map->Mutex);
            ConsolePanic(TEXT("[HandleMapAllocateHandle] Handle space exhausted (max in use)"));
            return HANDLE_MAP_ERROR_OUT_OF_HANDLES;
        }

        Candidate++;
    }

    LPHANDLE_MAP_ENTRY Entry = HandleMapAllocateEntry(Map, Candidate);
    if (Entry == NULL) {
        UnlockMutex(&Map->Mutex);
        return HANDLE_MAP_ERROR_OUT_OF_MEMORY;
    }

    if (!RadixTreeInsert(Map->Tree, Candidate, (LINEAR)Entry)) {
        HandleMapReleaseEntry(Map, Entry);
        UnlockMutex(&Map->Mutex);
        ERROR(TEXT("RadixTreeInsert failed (handle=%u)"), Candidate);
        return HANDLE_MAP_ERROR_INTERNAL;
    }

    if (Candidate < MAX_U32) {
        Map->NextHandle = Candidate + 1;
    } else {
        Map->NextHandle = MAX_U32;
    }

    *HandleOut = Candidate;

    UnlockMutex(&Map->Mutex);
    return HANDLE_MAP_OK;
}

/************************************************************************/

UINT HandleMapAttachPointer(LPHANDLE_MAP Map, UINT Handle, LINEAR Pointer) {
    if (Map == NULL || Pointer == 0) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    LPHANDLE_MAP_ENTRY Entry = HandleMapGetEntryLocked(Map, Handle);
    if (Entry == NULL) {
        UnlockMutex(&Map->Mutex);
        WARNING(TEXT("Unknown handle=%u"), Handle);
        return HANDLE_MAP_ERROR_NOT_FOUND;
    }

    UINT Result = HANDLE_MAP_OK;

    SAFE_USE(Entry) {
        if (Entry->Attached) {
            WARNING(TEXT("Handle=%u already attached to %p"), Handle, (LPVOID)Entry->Pointer);
            Result = HANDLE_MAP_ERROR_ALREADY_ATTACHED;
        } else {
            Entry->Pointer = Pointer;
            Entry->Attached = TRUE;
        }
    }

    UnlockMutex(&Map->Mutex);
    return Result;
}

/************************************************************************/

UINT HandleMapDetachPointer(LPHANDLE_MAP Map, UINT Handle, LINEAR* PointerOut) {
    if (Map == NULL) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    LPHANDLE_MAP_ENTRY Entry = HandleMapGetEntryLocked(Map, Handle);
    if (Entry == NULL) {
        UnlockMutex(&Map->Mutex);
        WARNING(TEXT("Unknown handle=%u"), Handle);
        return HANDLE_MAP_ERROR_NOT_FOUND;
    }

    UINT Result = HANDLE_MAP_OK;

    SAFE_USE(Entry) {
        if (!Entry->Attached || Entry->Pointer == 0) {
            Result = HANDLE_MAP_ERROR_NOT_ATTACHED;
        } else {
            if (PointerOut != NULL) {
                *PointerOut = Entry->Pointer;
            }
            Entry->Pointer = 0;
            Entry->Attached = FALSE;
        }
    }

    UnlockMutex(&Map->Mutex);
    return Result;
}

/************************************************************************/

UINT HandleMapResolveHandle(LPHANDLE_MAP Map, UINT Handle, LINEAR* PointerOut) {
    if (Map == NULL || PointerOut == NULL) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    LPHANDLE_MAP_ENTRY Entry = HandleMapGetEntryLocked(Map, Handle);
    if (Entry == NULL) {
        UnlockMutex(&Map->Mutex);
        return HANDLE_MAP_ERROR_NOT_FOUND;
    }

    UINT Result = HANDLE_MAP_OK;

    SAFE_USE(Entry) {
        if (!Entry->Attached || Entry->Pointer == 0) {
            Result = HANDLE_MAP_ERROR_NOT_ATTACHED;
        } else {
            *PointerOut = Entry->Pointer;
        }
    }

    UnlockMutex(&Map->Mutex);
    return Result;
}

/************************************************************************/

UINT HandleMapReleaseHandle(LPHANDLE_MAP Map, UINT Handle) {
    if (Map == NULL) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    LPHANDLE_MAP_ENTRY Entry = HandleMapGetEntryLocked(Map, Handle);
    if (Entry == NULL) {
        UnlockMutex(&Map->Mutex);
        WARNING(TEXT("Unknown handle=%u"), Handle);
        return HANDLE_MAP_ERROR_NOT_FOUND;
    }

    if (!RadixTreeRemove(Map->Tree, Handle)) {
        UnlockMutex(&Map->Mutex);
        ERROR(TEXT("RadixTreeRemove failed (handle=%u)"), Handle);
        return HANDLE_MAP_ERROR_INTERNAL;
    }

    BOOL WasAttached = FALSE;
    LINEAR Pointer = 0;

    SAFE_USE(Entry) {
        WasAttached = Entry->Attached;
        Pointer = Entry->Pointer;
    }

    HandleMapReleaseEntry(Map, Entry);

    if (WasAttached && Pointer != 0) {
        WARNING(TEXT("Handle=%u released while still attached to %p"), Handle, (LPVOID)Pointer);
    }

    UnlockMutex(&Map->Mutex);
    return HANDLE_MAP_OK;
}

/************************************************************************/

UINT HandleMapFindHandleByPointer(LPHANDLE_MAP Map, LINEAR Pointer, UINT* HandleOut) {
    if (Map == NULL || HandleOut == NULL || Pointer == 0) {
        return HANDLE_MAP_ERROR_INVALID_PARAMETER;
    }

    LockMutex(&Map->Mutex, INFINITY);

    HANDLE_MAP_POINTER_SEARCH Search = {.Pointer = Pointer, .Handle = 0, .Found = FALSE};
    BOOL IterateOk = RadixTreeIterate(Map->Tree, HandleMapPointerSearchVisitor, &Search);

    if (!IterateOk && Search.Found == FALSE) {
        UnlockMutex(&Map->Mutex);
        return HANDLE_MAP_ERROR_INTERNAL;
    }

    if (Search.Found == FALSE) {
        UnlockMutex(&Map->Mutex);
        return HANDLE_MAP_ERROR_NOT_FOUND;
    }

    *HandleOut = Search.Handle;

    UnlockMutex(&Map->Mutex);
    return HANDLE_MAP_OK;
}
