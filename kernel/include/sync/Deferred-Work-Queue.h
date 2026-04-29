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


    Deferred work queue engine

\************************************************************************/

#ifndef DEFERREDWORKQUEUE_H_INCLUDED
#define DEFERREDWORKQUEUE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "core/KernelEvent.h"
#include "utils/Helpers.h"

/************************************************************************/
// Macros

#define DEFERRED_WORK_QUEUE_NAME_LENGTH 32
#define DEFERRED_WORK_INVALID_SLOT 0xFFFFFFFF

/************************************************************************/
// Type definitions

typedef void (*DEFERRED_WORK_CALLBACK)(LPVOID Context);
typedef void (*DEFERRED_WORK_POLL_CALLBACK)(LPVOID Context);
typedef U32 (*DEFERRED_WORK_TASK_CALLBACK)(LPVOID Context);

typedef struct tag_DEFERRED_WORK_REGISTRATION {
    DEFERRED_WORK_CALLBACK WorkCallback;
    DEFERRED_WORK_POLL_CALLBACK PollCallback;
    LPVOID Context;
    LPCSTR Name;
} DEFERRED_WORK_REGISTRATION, *LPDEFERRED_WORK_REGISTRATION;

typedef struct tag_DEFERRED_WORK_QUEUE_ITEM {
    volatile BOOL InUse;
    volatile BOOL Unregistering;
    DEFERRED_WORK_CALLBACK WorkCallback;
    DEFERRED_WORK_POLL_CALLBACK PollCallback;
    LPVOID Context;
    volatile U32 PendingCount;
    volatile U32 ActiveCallbacks;
    STR Name[DEFERRED_WORK_QUEUE_NAME_LENGTH];
} DEFERRED_WORK_QUEUE_ITEM, *LPDEFERRED_WORK_QUEUE_ITEM;

typedef struct tag_DEFERRED_WORK_QUEUE {
    DEFERRED_WORK_QUEUE_ITEM WorkItems[DEFERRED_WORK_MAX_ITEMS];
    LPKERNEL_EVENT DeferredEvent;
    BOOL PollingMode;
    BOOL DispatcherStarted;
    UINT WaitTimeoutMS;
    UINT PollDelayMS;
    UINT TaskPriority;
    STR Name[DEFERRED_WORK_QUEUE_NAME_LENGTH];
} DEFERRED_WORK_QUEUE, *LPDEFERRED_WORK_QUEUE;

typedef struct tag_DEFERRED_WORK_QUEUE_CONFIG {
    LPCSTR Name;
    DEFERRED_WORK_TASK_CALLBACK TaskCallback;
    BOOL PollingMode;
    UINT WaitTimeoutMS;
    UINT PollDelayMS;
    UINT TaskPriority;
} DEFERRED_WORK_QUEUE_CONFIG, *LPDEFERRED_WORK_QUEUE_CONFIG;

/************************************************************************/
// External functions

BOOL DeferredWorkQueueInitialize(LPDEFERRED_WORK_QUEUE Queue, LPDEFERRED_WORK_QUEUE_CONFIG Config);
void DeferredWorkQueueShutdown(LPDEFERRED_WORK_QUEUE Queue);
U32 DeferredWorkQueueRegister(LPDEFERRED_WORK_QUEUE Queue, const DEFERRED_WORK_REGISTRATION* Registration);
U32 DeferredWorkQueueRegisterPollOnly(
    LPDEFERRED_WORK_QUEUE Queue, DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name);
void DeferredWorkQueueUnregister(LPDEFERRED_WORK_QUEUE Queue, U32 SlotID);
void DeferredWorkQueueSignal(LPDEFERRED_WORK_QUEUE Queue, U32 SlotID);
BOOL DeferredWorkQueueIsPollingMode(LPDEFERRED_WORK_QUEUE Queue);
void DeferredWorkQueueProcessPendingWork(LPDEFERRED_WORK_QUEUE Queue);
void DeferredWorkQueueProcessPollCallbacks(LPDEFERRED_WORK_QUEUE Queue);

/************************************************************************/

#endif  // DEFERREDWORKQUEUE_H_INCLUDED
