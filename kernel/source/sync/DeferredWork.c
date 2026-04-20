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


    Deferred work dispatcher infrastructure

\************************************************************************/

#include "sync/DeferredWork.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Process.h"
#include "process/Task.h"
#include "system/System.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/
// Macros

#define DEFERRED_WORK_VER_MAJOR 1
#define DEFERRED_WORK_VER_MINOR 0

/************************************************************************/
// Other

static DEFERRED_WORK_QUEUE DATA_SECTION g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_COUNT] = {0};

/************************************************************************/

static UINT DeferredWorkDriverCommands(UINT Function, UINT Parameter);
static U32 DeferredWorkQueueDispatcherTask(LPVOID Param);
static LPDEFERRED_WORK_QUEUE DeferredWorkGetQueueByID(U32 QueueID);
static DEFERRED_WORK_TOKEN DeferredWorkMakeInvalidToken(void);
static DEFERRED_WORK_TOKEN DeferredWorkMakeToken(U32 QueueID, U32 SlotID);
static BOOL DeferredWorkInitializeQueue(
    U32 QueueID, LPCSTR Name, BOOL PollingMode, UINT WaitTimeoutMS, UINT PollDelayMS);

/************************************************************************/

DRIVER DATA_SECTION DeferredWorkDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = DEFERRED_WORK_VER_MAJOR,
    .VersionMinor = DEFERRED_WORK_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "DeferredWork",
    .Alias = "deferred_work",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = DeferredWorkDriverCommands};

/************************************************************************/

/**
 * @brief Retrieve the deferred work driver descriptor.
 * @return Pointer to the deferred work driver.
 */
LPDRIVER DeferredWorkGetDriver(void) { return &DeferredWorkDriver; }

/************************************************************************/

/**
 * @brief Retrieve one deferred work queue by public identifier.
 * @param QueueID Queue identifier.
 * @return Queue pointer or NULL.
 */
static LPDEFERRED_WORK_QUEUE DeferredWorkGetQueueByID(U32 QueueID) {
    if (QueueID >= DEFERRED_WORK_QUEUE_COUNT) return NULL;
    return &(g_DeferredWorkQueues[QueueID]);
}

/************************************************************************/

/**
 * @brief Build an invalid deferred work token.
 * @return Invalid token.
 */
static DEFERRED_WORK_TOKEN DeferredWorkMakeInvalidToken(void) {
    DEFERRED_WORK_TOKEN Token;

    Token.QueueID = DEFERRED_WORK_QUEUE_INVALID;
    Token.SlotID = DEFERRED_WORK_INVALID_SLOT;
    return Token;
}

/************************************************************************/

/**
 * @brief Build a deferred work token from queue and slot identifiers.
 * @param QueueID Queue identifier.
 * @param SlotID Slot identifier.
 * @return Token identifying the queue slot.
 */
static DEFERRED_WORK_TOKEN DeferredWorkMakeToken(U32 QueueID, U32 SlotID) {
    DEFERRED_WORK_TOKEN Token;

    Token.QueueID = QueueID;
    Token.SlotID = SlotID;
    return Token;
}

/************************************************************************/

/**
 * @brief Tell whether a deferred work token references an existing queue slot.
 * @param Token Token to inspect.
 * @return TRUE when the token can be routed.
 */
BOOL DeferredWorkTokenIsValid(DEFERRED_WORK_TOKEN Token) {
    if (Token.QueueID >= DEFERRED_WORK_QUEUE_COUNT) return FALSE;
    if (Token.SlotID >= DEFERRED_WORK_MAX_ITEMS) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize one deferred work queue instance.
 * @param QueueID Queue identifier.
 * @param Name Dispatcher task name.
 * @param PollDelayMS Polling delay in milliseconds.
 * @return TRUE on success.
 */
static BOOL DeferredWorkInitializeQueue(
    U32 QueueID, LPCSTR Name, BOOL PollingMode, UINT WaitTimeoutMS, UINT PollDelayMS) {
    DEFERRED_WORK_QUEUE_CONFIG Config;
    LPDEFERRED_WORK_QUEUE Queue = DeferredWorkGetQueueByID(QueueID);

    if (Queue == NULL || Name == NULL) return FALSE;

    MemorySet(&Config, 0, sizeof(Config));
    Config.Name = Name;
    Config.TaskCallback = DeferredWorkQueueDispatcherTask;
    Config.PollingMode = PollingMode;
    Config.WaitTimeoutMS = WaitTimeoutMS;
    Config.PollDelayMS = PollDelayMS;
    Config.TaskPriority = TASK_PRIORITY_LOWER;

    return DeferredWorkQueueInitialize(Queue, &Config);
}

/************************************************************************/

/**
 * @brief Initialize deferred work dispatchers and events.
 * @return TRUE on success, FALSE on allocation or task creation failure.
 */
BOOL InitializeDeferredWork(void) {
    LPCSTR WaitTimeoutValue;
    LPCSTR PollDelayValue;
    LPCSTR ModeValue;
    U32 Numeric;
    BOOL PollingMode = FALSE;

    if (g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_STANDARD].DispatcherStarted &&
        g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_FAST].DispatcherStarted) {
        return TRUE;
    }

    SetDeferredWorkWaitTimeout(DEFERRED_WORK_WAIT_TIMEOUT_MS);
    SetDeferredWorkPollDelay(DEFERRED_WORK_POLL_DELAY_MS);

    WaitTimeoutValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_WAIT_TIMEOUT_MS));
    if (STRING_EMPTY(WaitTimeoutValue) == FALSE) {
        SetDeferredWorkWaitTimeout(StringToU32(WaitTimeoutValue));
    }

    PollDelayValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_POLL_DELAY_MS));
    if (STRING_EMPTY(PollDelayValue) == FALSE) {
        SetDeferredWorkPollDelay(StringToU32(PollDelayValue));
    }

    ModeValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_POLLING));
    if (STRING_EMPTY(ModeValue) == FALSE) {
        Numeric = StringToU32(ModeValue);
        if (Numeric != 0) {
            PollingMode = TRUE;
        } else if (StringCompareNC(ModeValue, TEXT("true")) == 0) {
            PollingMode = TRUE;
        }
    }

    if (PollingMode != FALSE) {
        ConsolePrint(TEXT("WARNING : Devices in polling mode.\n"));
    }

    if (DeferredWorkInitializeQueue(
            DEFERRED_WORK_QUEUE_STANDARD, TEXT("DeferredWork"), PollingMode, GetDeferredWorkWaitTimeout(),
            GetDeferredWorkPollDelay()) == FALSE) {
        return FALSE;
    }

    if (DeferredWorkInitializeQueue(
            DEFERRED_WORK_QUEUE_FAST, TEXT("DeferredWorkFast"), PollingMode, DEFERRED_WORK_FAST_DELAY_MS,
            DEFERRED_WORK_FAST_DELAY_MS) == FALSE) {
        DeferredWorkQueueShutdown(&(g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_STANDARD]));
        return FALSE;
    }

    DEBUG(
        TEXT("[InitializeDeferredWork] Queues initialized standard_delay=%u fast_delay=%u"), GetDeferredWorkPollDelay(),
        DEFERRED_WORK_FAST_DELAY_MS);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shut down deferred work dispatcher state.
 */
void ShutdownDeferredWork(void) {
    DeferredWorkQueueShutdown(&(g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_STANDARD]));
    DeferredWorkQueueShutdown(&(g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_FAST]));
}

/************************************************************************/

/**
 * @brief Register a deferred work item in the standard queue.
 * @param Registration Registration information defining callbacks and context.
 * @return Token to the registered work item or an invalid token.
 */
DEFERRED_WORK_TOKEN DeferredWorkRegister(const DEFERRED_WORK_REGISTRATION* Registration) {
    return DeferredWorkRegisterForQueue(DEFERRED_WORK_QUEUE_STANDARD, Registration);
}

/************************************************************************/

/**
 * @brief Register a deferred work item in a selected queue.
 * @param QueueID Queue identifier.
 * @param Registration Registration information defining callbacks and context.
 * @return Token to the registered work item or an invalid token.
 */
DEFERRED_WORK_TOKEN DeferredWorkRegisterForQueue(U32 QueueID, const DEFERRED_WORK_REGISTRATION* Registration) {
    LPDEFERRED_WORK_QUEUE Queue = DeferredWorkGetQueueByID(QueueID);
    U32 SlotID;

    if (Queue == NULL) return DeferredWorkMakeInvalidToken();

    SlotID = DeferredWorkQueueRegister(Queue, Registration);
    if (SlotID == DEFERRED_WORK_INVALID_SLOT) return DeferredWorkMakeInvalidToken();

    return DeferredWorkMakeToken(QueueID, SlotID);
}

/************************************************************************/

/**
 * @brief Register a polling-only deferred work item in the standard queue.
 * @param PollCallback Callback invoked during polling.
 * @param Context User-provided context passed to callback.
 * @param Name Debug name for the registration.
 * @return Token to the registered work item or an invalid token.
 */
DEFERRED_WORK_TOKEN DeferredWorkRegisterPollOnly(
    DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name) {
    return DeferredWorkRegisterPollOnlyForQueue(DEFERRED_WORK_QUEUE_STANDARD, PollCallback, Context, Name);
}

/************************************************************************/

/**
 * @brief Register a polling-only deferred work item in a selected queue.
 * @param QueueID Queue identifier.
 * @param PollCallback Callback invoked during polling.
 * @param Context User-provided context passed to callback.
 * @param Name Debug name for the registration.
 * @return Token to the registered work item or an invalid token.
 */
DEFERRED_WORK_TOKEN DeferredWorkRegisterPollOnlyForQueue(
    U32 QueueID, DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name) {
    LPDEFERRED_WORK_QUEUE Queue = DeferredWorkGetQueueByID(QueueID);
    U32 SlotID;

    if (Queue == NULL) return DeferredWorkMakeInvalidToken();

    SlotID = DeferredWorkQueueRegisterPollOnly(Queue, PollCallback, Context, Name);
    if (SlotID == DEFERRED_WORK_INVALID_SLOT) return DeferredWorkMakeInvalidToken();

    return DeferredWorkMakeToken(QueueID, SlotID);
}

/************************************************************************/

/**
 * @brief Unregister a deferred work item and clear its slot.
 * @param Token Deferred work token to remove.
 */
void DeferredWorkUnregister(DEFERRED_WORK_TOKEN Token) {
    LPDEFERRED_WORK_QUEUE Queue;

    if (DeferredWorkTokenIsValid(Token) == FALSE) return;

    Queue = DeferredWorkGetQueueByID(Token.QueueID);
    if (Queue == NULL) return;

    DeferredWorkQueueUnregister(Queue, Token.SlotID);
}

/************************************************************************/

/**
 * @brief Signal a deferred work item to run its work callback.
 * @param Token Deferred work token to signal.
 */
void DeferredWorkSignal(DEFERRED_WORK_TOKEN Token) {
    LPDEFERRED_WORK_QUEUE Queue;

    if (DeferredWorkTokenIsValid(Token) == FALSE) return;

    Queue = DeferredWorkGetQueueByID(Token.QueueID);
    if (Queue == NULL) return;

    DeferredWorkQueueSignal(Queue, Token.SlotID);
}

/************************************************************************/

/**
 * @brief Tell whether the standard queue uses polling mode.
 * @return TRUE when polling mode is enabled.
 */
BOOL DeferredWorkIsPollingMode(void) { return DeferredWorkIsPollingModeForQueue(DEFERRED_WORK_QUEUE_STANDARD); }

/************************************************************************/

/**
 * @brief Tell whether a selected queue uses polling mode.
 * @param QueueID Queue identifier.
 * @return TRUE when polling mode is enabled.
 */
BOOL DeferredWorkIsPollingModeForQueue(U32 QueueID) {
    LPDEFERRED_WORK_QUEUE Queue = DeferredWorkGetQueueByID(QueueID);

    if (Queue == NULL) return FALSE;
    return DeferredWorkQueueIsPollingMode(Queue);
}

/************************************************************************/

/**
 * @brief Refresh polling mode from configuration for all deferred work queues.
 */
void DeferredWorkUpdateMode(void) {
    LPCSTR ModeValue;
    U32 Numeric;
    BOOL PollingMode = FALSE;

    ModeValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_POLLING));
    if (STRING_EMPTY(ModeValue) == FALSE) {
        Numeric = StringToU32(ModeValue);
        if (Numeric != 0) {
            PollingMode = TRUE;
        } else if (StringCompareNC(ModeValue, TEXT("true")) == 0) {
            PollingMode = TRUE;
        }
    }

    g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_STANDARD].PollingMode = PollingMode;
    g_DeferredWorkQueues[DEFERRED_WORK_QUEUE_FAST].PollingMode = PollingMode;
}

/************************************************************************/

/**
 * @brief Dispatcher task for one deferred work queue.
 * @param Param Deferred work queue pointer.
 * @return Always 0 when the task exits.
 */
static U32 DeferredWorkQueueDispatcherTask(LPVOID Param) {
    LPDEFERRED_WORK_QUEUE Queue = (LPDEFERRED_WORK_QUEUE)Param;
    WAIT_INFO WaitInfo;
    U32 WaitResult;

    if (Queue == NULL) return 0;

    MemorySet(&WaitInfo, 0, sizeof(WaitInfo));
    WaitInfo.Header.Size = sizeof(WAIT_INFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.Objects[0] = (HANDLE)Queue->DeferredEvent;
    WaitInfo.MilliSeconds = Queue->WaitTimeoutMS;

    FOREVER {
        if (DeferredWorkQueueIsPollingMode(Queue)) {
            DeferredWorkQueueProcessPollCallbacks(Queue);
            Sleep(Queue->PollDelayMS);
            continue;
        }

        WaitInfo.MilliSeconds = Queue->WaitTimeoutMS;
        WaitResult = Wait(&WaitInfo);
        if (WaitResult == WAIT_TIMEOUT) {
            DeferredWorkQueueProcessPollCallbacks(Queue);
            continue;
        }

        if (WaitResult != WAIT_OBJECT_0) {
            WARNING(
                TEXT("[DeferredWorkQueueDispatcherTask] Queue=%s unexpected wait result %u"), Queue->Name, WaitResult);
            continue;
        }

        DeferredWorkQueueProcessPendingWork(Queue);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Driver command handler for deferred work initialization.
 * @param Function Driver command identifier.
 * @param Parameter Unused command parameter.
 * @return Driver command status.
 */
static UINT DeferredWorkDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((DeferredWorkDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeDeferredWork()) {
                DeferredWorkDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;

        case DF_UNLOAD:
            if ((DeferredWorkDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ShutdownDeferredWork();
            DeferredWorkDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(DEFERRED_WORK_VER_MAJOR, DEFERRED_WORK_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
