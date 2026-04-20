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


    Deadlock monitor

\************************************************************************/

#include "utils/DeadlockMonitor.h"

#include "system/Clock.h"
#include "console/Console.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"
#include "process/Task.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define DEADLOCK_MONITOR_MAX_CHAIN_DEPTH 32

/************************************************************************/

static RATE_LIMITER DATA_SECTION DeadlockMonitorCycleLogLimiter = {0};
static BOOL DATA_SECTION DeadlockMonitorCycleLogLimiterInitialized = FALSE;
static RATE_LIMITER DATA_SECTION DeadlockMonitorOrderLogLimiter = {0};
static BOOL DATA_SECTION DeadlockMonitorOrderLogLimiterInitialized = FALSE;

/************************************************************************/

/**
 * @brief Ensure the cycle log limiter is initialized once.
 *
 * @return TRUE when the limiter can be used.
 */
static BOOL DeadlockMonitorEnsureCycleLogLimiter(void) {
    if (DeadlockMonitorCycleLogLimiterInitialized != FALSE) {
        return TRUE;
    }

    if (RateLimiterInit(&DeadlockMonitorCycleLogLimiter, 2, 1000) == FALSE) {
        return FALSE;
    }

    DeadlockMonitorCycleLogLimiterInitialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Ensure the lock-order log limiter is initialized once.
 *
 * @return TRUE when the limiter can be used.
 */
static BOOL DeadlockMonitorEnsureOrderLogLimiter(void) {
    if (DeadlockMonitorOrderLogLimiterInitialized != FALSE) {
        return TRUE;
    }

    if (RateLimiterInit(&DeadlockMonitorOrderLogLimiter, 4, 1000) == FALSE) {
        return FALSE;
    }

    DeadlockMonitorOrderLogLimiterInitialized = TRUE;
    return TRUE;
}

/**
 * @brief Validate one mutex pointer for deadlock analysis.
 *
 * @param Mutex Mutex pointer to validate.
 * @return Valid mutex pointer, or NULL when invalid.
 */
static LPMUTEX DeadlockMonitorGetValidMutex(LPMUTEX Mutex) {
    SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) {
        return Mutex;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Validate one task pointer for deadlock analysis.
 *
 * @param Task Task pointer to validate.
 * @return Valid task pointer, or NULL when invalid.
 */
static LPTASK DeadlockMonitorGetValidTask(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        return Task;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Clear mutex wait tracking for one task when it matches the target mutex.
 *
 * @param Task Task whose wait state is updated.
 * @param Mutex Mutex to clear, or NULL to clear unconditionally.
 */
static void DeadlockMonitorClearWaitState(LPTASK Task, LPMUTEX Mutex) {
    Task = DeadlockMonitorGetValidTask(Task);
    if (Task == NULL) {
        return;
    }

    if (Mutex != NULL && Task->WaitingMutex != Mutex) {
        return;
    }

    Task->WaitingMutex = NULL;
    Task->WaitingSince = 0;
}

/************************************************************************/

/**
 * @brief Report one lock-order inversion candidate.
 *
 * @param Task Current task.
 * @param HeldClass Top class already held.
 * @param Mutex New mutex being acquired.
 */
static void DeadlockMonitorLogOrderViolation(LPTASK Task, U32 HeldClass, LPMUTEX Mutex) {
    U32 Now;
    U32 Suppressed = 0;
    LPCSTR MutexName;

    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    Now = GetSystemTime();
    if (DeadlockMonitorEnsureOrderLogLimiter() != FALSE) {
        if (RateLimiterShouldTrigger(&DeadlockMonitorOrderLogLimiter, Now, &Suppressed) == FALSE) {
            return;
        }
    }

    MutexName = Mutex->DebugName != NULL ? Mutex->DebugName : TEXT("UnnamedMutex");

    WARNING(TEXT("Lock order inversion task=%p (%s) held_class=%u new_class=%u mutex=%p name=%s suppressed=%u"),
            Task,
            Task->Name[0] != STR_NULL ? Task->Name : TEXT("Unnamed"),
            HeldClass,
            Mutex->DebugClass,
            Mutex,
            MutexName,
            Suppressed);
}

/************************************************************************/

/**
 * @brief Check whether acquiring one mutex violates the current class order.
 *
 * @param Task Current task.
 * @param Mutex Mutex being acquired.
 */
static void DeadlockMonitorCheckLockOrder(LPTASK Task, LPMUTEX Mutex) {
    U32 HeldClass;

    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    if (Mutex->DebugClass == MUTEX_CLASS_NONE || Task->HeldMutexClassDepth == 0) {
        return;
    }

    HeldClass = Task->HeldMutexClasses[Task->HeldMutexClassDepth - 1];
    if (HeldClass == 0 || Mutex->DebugClass >= HeldClass) {
        return;
    }

    DeadlockMonitorLogOrderViolation(Task, HeldClass, Mutex);
}

/************************************************************************/

/**
 * @brief Push one held mutex class onto the current task stack.
 *
 * @param Task Current task.
 * @param Mutex Acquired mutex.
 */
static void DeadlockMonitorPushHeldClass(LPTASK Task, LPMUTEX Mutex) {
    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    if (Mutex->DebugClass == MUTEX_CLASS_NONE) {
        return;
    }

    if (Task->HeldMutexClassDepth >= TASK_MUTEX_CLASS_STACK_MAX_DEPTH) {
        WARNING(TEXT("Held class stack overflow task=%p (%s) mutex=%p class=%u"),
                Task,
                Task->Name[0] != STR_NULL ? Task->Name : TEXT("Unnamed"),
                Mutex,
                Mutex->DebugClass);
        return;
    }

    Task->HeldMutexClasses[Task->HeldMutexClassDepth] = Mutex->DebugClass;
    Task->HeldMutexClassDepth++;
}

/************************************************************************/

/**
 * @brief Pop one held mutex class from the current task stack.
 *
 * @param Task Current task.
 * @param Mutex Released mutex.
 */
static void DeadlockMonitorPopHeldClass(LPTASK Task, LPMUTEX Mutex) {
    U32 ExpectedClass;

    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    if (Mutex->DebugClass == MUTEX_CLASS_NONE) {
        return;
    }

    if (Task->HeldMutexClassDepth == 0) {
        WARNING(TEXT("Empty held class stack task=%p (%s) mutex=%p class=%u"),
                Task,
                Task->Name[0] != STR_NULL ? Task->Name : TEXT("Unnamed"),
                Mutex,
                Mutex->DebugClass);
        return;
    }

    ExpectedClass = Task->HeldMutexClasses[Task->HeldMutexClassDepth - 1];
    if (ExpectedClass != Mutex->DebugClass) {
        WARNING(TEXT("Held class mismatch task=%p (%s) mutex=%p expected=%u actual=%u"),
                Task,
                Task->Name[0] != STR_NULL ? Task->Name : TEXT("Unnamed"),
                Mutex,
                ExpectedClass,
                Mutex->DebugClass);
        return;
    }

    Task->HeldMutexClassDepth--;
    Task->HeldMutexClasses[Task->HeldMutexClassDepth] = 0;
}

/************************************************************************/

/**
 * @brief Log one confirmed mutex deadlock chain.
 *
 * @param WaiterTask Task that started the wait.
 * @param Mutex Initial mutex waited by the task.
 */
static void DeadlockMonitorLogCycle(LPTASK WaiterTask, LPMUTEX Mutex) {
    UINT Depth;
    U32 Now;
    U32 Suppressed = 0;
    LPTASK OwnerTask;
    LPTASK CurrentTask;
    LPMUTEX CurrentMutex;

    WaiterTask = DeadlockMonitorGetValidTask(WaiterTask);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (WaiterTask == NULL || Mutex == NULL) {
        return;
    }

    OwnerTask = DeadlockMonitorGetValidTask(Mutex->Task);
    if (OwnerTask == NULL) {
        return;
    }

    Now = GetSystemTime();
    if (DeadlockMonitorEnsureCycleLogLimiter() != FALSE) {
        if (RateLimiterShouldTrigger(&DeadlockMonitorCycleLogLimiter, Now, &Suppressed) == FALSE) {
            return;
        }
    }

    ERROR(TEXT("Mutex deadlock detected waiter=%p (%s) mutex=%p owner=%p (%s) suppressed=%u"),
          WaiterTask,
          WaiterTask->Name[0] != STR_NULL ? WaiterTask->Name : TEXT("Unnamed"),
          Mutex,
          OwnerTask,
          OwnerTask->Name[0] != STR_NULL ? OwnerTask->Name : TEXT("Unnamed"),
          Suppressed);

    CurrentTask = WaiterTask;
    CurrentMutex = Mutex;

    for (Depth = 0; Depth < DEADLOCK_MONITOR_MAX_CHAIN_DEPTH; Depth++) {
        OwnerTask = DeadlockMonitorGetValidTask(CurrentMutex->Task);
        if (OwnerTask == NULL) {
            DEBUG(TEXT("Chain[%u] task=%p (%s) waits for mutex=%p with no valid owner"),
                  Depth,
                  CurrentTask,
                  CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"),
                  CurrentMutex);
            return;
        }

        DEBUG(TEXT("Chain[%u] task=%p (%s) waits for mutex=%p owned by task=%p (%s)"),
              Depth,
              CurrentTask,
              CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"),
              CurrentMutex,
              OwnerTask,
              OwnerTask->Name[0] != STR_NULL ? OwnerTask->Name : TEXT("Unnamed"));

        if (OwnerTask == WaiterTask) {
            return;
        }

        CurrentTask = OwnerTask;
        CurrentMutex = DeadlockMonitorGetValidMutex(CurrentTask->WaitingMutex);
        if (CurrentMutex == NULL) {
            DEBUG(TEXT("Chain[%u] task=%p (%s) has no waited mutex"),
                  Depth + 1,
                  CurrentTask,
                  CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"));
            return;
        }
    }

    DEBUG(TEXT("Chain truncated at depth=%u"), DEADLOCK_MONITOR_MAX_CHAIN_DEPTH);

#if DEBUG_OUTPUT == 1
    ConsolePanic(TEXT("Mutex deadlock detected"));
#endif
}

/************************************************************************/

/**
 * @brief Record that one task starts waiting on one mutex.
 *
 * @param Task Waiting task.
 * @param Mutex Target mutex.
 */
void DeadlockMonitorOnWaitStart(LPTASK Task, LPMUTEX Mutex) {
    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    Task->WaitingMutex = Mutex;
    Task->WaitingSince = GetSystemTime();

    if (DeadlockMonitorWouldCreateCycle(Task, Mutex) != FALSE) {
        DeadlockMonitorLogCycle(Task, Mutex);
    }
}

/************************************************************************/

/**
 * @brief Record that one mutex wait was canceled or ended without acquisition.
 *
 * @param Task Waiting task.
 * @param Mutex Waited mutex.
 */
void DeadlockMonitorOnWaitCancel(LPTASK Task, LPMUTEX Mutex) {
    DeadlockMonitorClearWaitState(Task, Mutex);
}

/************************************************************************/

/**
 * @brief Record that one task acquired one mutex after a wait.
 *
 * @param Task Owner task.
 * @param Mutex Acquired mutex.
 */
void DeadlockMonitorOnAcquire(LPTASK Task, LPMUTEX Mutex) {
    DeadlockMonitorCheckLockOrder(Task, Mutex);
    DeadlockMonitorPushHeldClass(Task, Mutex);
    DeadlockMonitorClearWaitState(Task, Mutex);
}

/************************************************************************/

/**
 * @brief Record that one task released one mutex.
 *
 * @param Task Releasing task.
 * @param Mutex Released mutex.
 * @param NextOwner Next owner if known.
 */
void DeadlockMonitorOnRelease(LPTASK Task, LPMUTEX Mutex, LPTASK NextOwner) {
    DeadlockMonitorPopHeldClass(Task, Mutex);
    UNUSED(Mutex);
    UNUSED(NextOwner);
}

/************************************************************************/

/**
 * @brief Check whether a blocking wait would create a mutex wait cycle.
 *
 * @param Task Waiting task.
 * @param Mutex Target mutex already owned by another task.
 * @return TRUE if a cycle is found, FALSE otherwise.
 */
BOOL DeadlockMonitorWouldCreateCycle(LPTASK Task, LPMUTEX Mutex) {
    UINT Depth;
    LPTASK WaiterTask;
    LPTASK CurrentOwner;
    LPMUTEX CurrentWaitedMutex;

    WaiterTask = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (WaiterTask == NULL || Mutex == NULL) {
        return FALSE;
    }

    CurrentOwner = DeadlockMonitorGetValidTask(Mutex->Task);
    if (CurrentOwner == NULL || CurrentOwner == WaiterTask) {
        return FALSE;
    }

    for (Depth = 0; Depth < DEADLOCK_MONITOR_MAX_CHAIN_DEPTH; Depth++) {
        if (CurrentOwner == WaiterTask) {
            return TRUE;
        }

        CurrentWaitedMutex = DeadlockMonitorGetValidMutex(CurrentOwner->WaitingMutex);
        if (CurrentWaitedMutex == NULL) {
            return FALSE;
        }

        CurrentOwner = DeadlockMonitorGetValidTask(CurrentWaitedMutex->Task);
        if (CurrentOwner == NULL) {
            return FALSE;
        }
    }

    return FALSE;
}

/************************************************************************/
