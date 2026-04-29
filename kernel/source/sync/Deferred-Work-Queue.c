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


    Deferred work queue engine

\************************************************************************/

#include "sync/Deferred-Work-Queue.h"

#include "Arch.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "system/System.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

static BOOL DeferredWorkQueueAcquirePendingDispatch(
    LPDEFERRED_WORK_QUEUE Queue, UINT SlotID, DEFERRED_WORK_CALLBACK* Callback, LPVOID* Context, U32* PendingCount);
static BOOL DeferredWorkQueueAcquirePollDispatch(
    LPDEFERRED_WORK_QUEUE Queue, UINT SlotID, DEFERRED_WORK_POLL_CALLBACK* Callback, LPVOID* Context);
static void DeferredWorkQueueReleaseDispatch(LPDEFERRED_WORK_QUEUE Queue, UINT SlotID);
static void DeferredWorkQueueWaitForQuiesced(LPDEFERRED_WORK_QUEUE Queue, UINT SlotID);

/************************************************************************/

/**
 * @brief Claim one pending work callback execution for one slot.
 * @param Queue Deferred work queue to inspect.
 * @param SlotID Work item slot identifier.
 * @param Callback Output callback pointer.
 * @param Context Output callback context.
 * @param PendingCount Output number of queued runs consumed by this claim.
 * @return TRUE when one dispatch batch was acquired, FALSE otherwise.
 */
static BOOL DeferredWorkQueueAcquirePendingDispatch(
    LPDEFERRED_WORK_QUEUE Queue, UINT SlotID, DEFERRED_WORK_CALLBACK* Callback, LPVOID* Context, U32* PendingCount) {
    LPDEFERRED_WORK_QUEUE_ITEM Item;
    UINT Flags;

    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS || Callback == NULL || Context == NULL ||
        PendingCount == NULL) {
        return FALSE;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    Item = &(Queue->WorkItems[SlotID]);
    if (!Item->InUse || Item->Unregistering || Item->WorkCallback == NULL || Item->PendingCount == 0) {
        RestoreFlags(&Flags);
        return FALSE;
    }

    *Callback = Item->WorkCallback;
    *Context = Item->Context;
    *PendingCount = Item->PendingCount;
    Item->PendingCount = 0;
    Item->ActiveCallbacks++;

    RestoreFlags(&Flags);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Claim one polling callback execution for one slot.
 * @param Queue Deferred work queue to inspect.
 * @param SlotID Work item slot identifier.
 * @param Callback Output callback pointer.
 * @param Context Output callback context.
 * @return TRUE when one polling callback was acquired, FALSE otherwise.
 */
static BOOL DeferredWorkQueueAcquirePollDispatch(
    LPDEFERRED_WORK_QUEUE Queue, UINT SlotID, DEFERRED_WORK_POLL_CALLBACK* Callback, LPVOID* Context) {
    LPDEFERRED_WORK_QUEUE_ITEM Item;
    UINT Flags;

    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS || Callback == NULL || Context == NULL) {
        return FALSE;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    Item = &(Queue->WorkItems[SlotID]);
    if (!Item->InUse || Item->Unregistering || Item->PollCallback == NULL) {
        RestoreFlags(&Flags);
        return FALSE;
    }

    *Callback = Item->PollCallback;
    *Context = Item->Context;
    Item->ActiveCallbacks++;

    RestoreFlags(&Flags);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Release one previously acquired dispatch claim.
 * @param Queue Deferred work queue owning the slot.
 * @param SlotID Work item slot identifier.
 */
static void DeferredWorkQueueReleaseDispatch(LPDEFERRED_WORK_QUEUE Queue, UINT SlotID) {
    UINT Flags;

    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    if (Queue->WorkItems[SlotID].ActiveCallbacks > 0) {
        Queue->WorkItems[SlotID].ActiveCallbacks--;
    }

    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Wait until one work item no longer has in-flight callbacks.
 * @param Queue Deferred work queue owning the slot.
 * @param SlotID Work item slot identifier.
 */
static void DeferredWorkQueueWaitForQuiesced(LPDEFERRED_WORK_QUEUE Queue, UINT SlotID) {
    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    while (Queue->WorkItems[SlotID].ActiveCallbacks > 0) {
        Sleep(1);
    }
}

/************************************************************************/

/**
 * @brief Initialize one deferred work queue and create its dispatcher task.
 * @param Queue Queue storage to initialize.
 * @param Config Initialization configuration.
 * @return TRUE on success, FALSE on allocation or task creation failure.
 */
BOOL DeferredWorkQueueInitialize(LPDEFERRED_WORK_QUEUE Queue, LPDEFERRED_WORK_QUEUE_CONFIG Config) {
    TASK_INFO TaskInfo;

    if (Queue == NULL || Config == NULL || Config->TaskCallback == NULL || Config->Name == NULL) {
        return FALSE;
    }

    if (Queue->DispatcherStarted) {
        return TRUE;
    }

    MemorySet(Queue->WorkItems, 0, sizeof(Queue->WorkItems));
    Queue->PollingMode = Config->PollingMode;
    Queue->WaitTimeoutMS = Config->WaitTimeoutMS;
    Queue->PollDelayMS = Config->PollDelayMS;
    Queue->TaskPriority = Config->TaskPriority;
    MemorySet(Queue->Name, 0, sizeof(Queue->Name));
    StringCopyLimit(Queue->Name, Config->Name, sizeof(Queue->Name));

    Queue->DeferredEvent = CreateKernelEvent();
    if (Queue->DeferredEvent == NULL) {
        ERROR(TEXT("Failed to create event for %s"), Queue->Name);
        return FALSE;
    }

    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TASK_INFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Func = Config->TaskCallback;
    TaskInfo.Parameter = Queue;
    TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    TaskInfo.Priority = Config->TaskPriority;
    TaskInfo.Flags = 0;
    StringCopy(TaskInfo.Name, Config->Name);

    if (KernelCreateTask(&KernelProcess, &TaskInfo) == NULL) {
        ERROR(TEXT("Failed to create dispatcher for %s"), Queue->Name);
        DeleteKernelEvent(Queue->DeferredEvent);
        Queue->DeferredEvent = NULL;
        return FALSE;
    }

    Queue->DispatcherStarted = TRUE;
    DEBUG(TEXT("Dispatcher %s started event=%p"), Queue->Name, Queue->DeferredEvent);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shut down one deferred work queue.
 * @param Queue Queue to shut down.
 */
void DeferredWorkQueueShutdown(LPDEFERRED_WORK_QUEUE Queue) {
    if (Queue == NULL) return;

    Queue->DispatcherStarted = FALSE;
    Queue->PollingMode = FALSE;
    SAFE_USE(Queue->DeferredEvent) { ResetKernelEvent(Queue->DeferredEvent); }
}

/************************************************************************/

/**
 * @brief Register a deferred work item in one queue.
 * @param Queue Queue receiving the registration.
 * @param Registration Registration information defining callbacks and context.
 * @return Slot identifier or DEFERRED_WORK_INVALID_SLOT.
 */
U32 DeferredWorkQueueRegister(LPDEFERRED_WORK_QUEUE Queue, const DEFERRED_WORK_REGISTRATION* Registration) {
    UINT Flags;
    U32 Index;

    if (Queue == NULL || Registration == NULL) {
        return DEFERRED_WORK_INVALID_SLOT;
    }

    if (Registration->WorkCallback == NULL && Registration->PollCallback == NULL) {
        return DEFERRED_WORK_INVALID_SLOT;
    }

    for (Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        SaveFlags(&Flags);
        DisableInterrupts();

        if (!Queue->WorkItems[Index].InUse && !Queue->WorkItems[Index].Unregistering &&
            Queue->WorkItems[Index].ActiveCallbacks == 0) {
            Queue->WorkItems[Index].InUse = TRUE;
            Queue->WorkItems[Index].Unregistering = FALSE;
            Queue->WorkItems[Index].WorkCallback = Registration->WorkCallback;
            Queue->WorkItems[Index].PollCallback = Registration->PollCallback;
            Queue->WorkItems[Index].Context = Registration->Context;
            Queue->WorkItems[Index].PendingCount = 0;
            Queue->WorkItems[Index].ActiveCallbacks = 0;
            MemorySet(Queue->WorkItems[Index].Name, 0, sizeof(Queue->WorkItems[Index].Name));
            if (Registration->Name) {
                StringCopyLimit(Queue->WorkItems[Index].Name, Registration->Name, sizeof(Queue->WorkItems[Index].Name));
            }

            RestoreFlags(&Flags);

            DEBUG(TEXT("Queue=%s slot=%u name=%s"), Queue->Name, Index, Queue->WorkItems[Index].Name);
            return Index;
        }

        RestoreFlags(&Flags);
    }

    ERROR(TEXT("No free slots in %s"), Queue->Name);
    return DEFERRED_WORK_INVALID_SLOT;
}

/************************************************************************/

/**
 * @brief Register a polling-only deferred work item in one queue.
 * @param Queue Queue receiving the registration.
 * @param PollCallback Callback invoked during polling.
 * @param Context User-provided context passed to callback.
 * @param Name Debug name for the registration.
 * @return Slot identifier or DEFERRED_WORK_INVALID_SLOT.
 */
U32 DeferredWorkQueueRegisterPollOnly(
    LPDEFERRED_WORK_QUEUE Queue, DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name) {
    DEFERRED_WORK_REGISTRATION Registration;

    MemorySet(&Registration, 0, sizeof(Registration));
    Registration.WorkCallback = NULL;
    Registration.PollCallback = PollCallback;
    Registration.Context = Context;
    Registration.Name = Name;
    return DeferredWorkQueueRegister(Queue, &Registration);
}

/************************************************************************/

/**
 * @brief Unregister one deferred work item and clear its slot.
 * @param Queue Queue owning the slot.
 * @param SlotID Slot identifier to remove.
 */
void DeferredWorkQueueUnregister(LPDEFERRED_WORK_QUEUE Queue, U32 SlotID) {
    LPDEFERRED_WORK_QUEUE_ITEM Item;
    UINT Flags;

    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    Item = &(Queue->WorkItems[SlotID]);
    if (!Item->InUse) {
        RestoreFlags(&Flags);
        return;
    }

    Item->Unregistering = TRUE;
    Item->PendingCount = 0;

    RestoreFlags(&Flags);

    DeferredWorkQueueWaitForQuiesced(Queue, SlotID);

    SaveFlags(&Flags);
    DisableInterrupts();

    Item->InUse = FALSE;
    Item->Unregistering = FALSE;
    Item->WorkCallback = NULL;
    Item->PollCallback = NULL;
    Item->Context = NULL;
    Item->PendingCount = 0;
    MemorySet(Item->Name, 0, sizeof(Item->Name));

    RestoreFlags(&Flags);

    DEBUG(TEXT("Queue=%s slot=%u"), Queue->Name, SlotID);
}

/************************************************************************/

/**
 * @brief Signal one deferred work item to run its work callback.
 * @param Queue Queue owning the slot.
 * @param SlotID Slot identifier to signal.
 */
void DeferredWorkQueueSignal(LPDEFERRED_WORK_QUEUE Queue, U32 SlotID) {
    LPDEFERRED_WORK_QUEUE_ITEM Item;
    UINT Flags;

    if (Queue == NULL || SlotID >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    Item = &(Queue->WorkItems[SlotID]);
    if (!Item->InUse || Item->Unregistering || Item->WorkCallback == NULL) {
        RestoreFlags(&Flags);
        return;
    }

    Item->PendingCount++;
    RestoreFlags(&Flags);

    SAFE_USE(Queue->DeferredEvent) { SignalKernelEvent(Queue->DeferredEvent); }
}

/************************************************************************/

/**
 * @brief Tell whether one deferred work queue uses polling mode.
 * @param Queue Queue to inspect.
 * @return TRUE when polling mode is enabled.
 */
BOOL DeferredWorkQueueIsPollingMode(LPDEFERRED_WORK_QUEUE Queue) {
    if (Queue == NULL) return FALSE;
    return Queue->PollingMode;
}

/************************************************************************/

/**
 * @brief Process pending deferred work callbacks until the queue drains.
 * @param Queue Queue to process.
 */
void DeferredWorkQueueProcessPendingWork(LPDEFERRED_WORK_QUEUE Queue) {
    BOOL WorkFound;
    BOOL PendingLeft;
    UINT Flags;
    U32 Index;

    if (Queue == NULL) return;

    do {
        WorkFound = FALSE;

        for (Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
            U32 Pending = 0;
            LPVOID Context = NULL;
            DEFERRED_WORK_CALLBACK Callback = NULL;

            if (!DeferredWorkQueueAcquirePendingDispatch(Queue, Index, &Callback, &Context, &Pending)) {
                continue;
            }

            while (Pending > 0) {
                Callback(Context);
                Pending--;
                WorkFound = TRUE;
            }

            DeferredWorkQueueReleaseDispatch(Queue, Index);
        }
    } while (WorkFound);

    SaveFlags(&Flags);
    DisableInterrupts();

    PendingLeft = FALSE;
    for (Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        if (Queue->WorkItems[Index].InUse && Queue->WorkItems[Index].PendingCount > 0) {
            PendingLeft = TRUE;
            break;
        }
    }

    if (!PendingLeft) {
        SAFE_USE(Queue->DeferredEvent) { ResetKernelEvent(Queue->DeferredEvent); }
    }

    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Run all registered polling callbacks in one queue.
 * @param Queue Queue to process.
 */
void DeferredWorkQueueProcessPollCallbacks(LPDEFERRED_WORK_QUEUE Queue) {
    U32 Index;

    if (Queue == NULL) return;

    for (Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        LPVOID Context = NULL;
        DEFERRED_WORK_POLL_CALLBACK Callback = NULL;

        if (!DeferredWorkQueueAcquirePollDispatch(Queue, Index, &Callback, &Context)) {
            continue;
        }

        Callback(Context);
        DeferredWorkQueueReleaseDispatch(Queue, Index);
    }
}

/************************************************************************/
