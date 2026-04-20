
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


    Mutex

\************************************************************************/

#include "system/Clock.h"
#include "core/Kernel.h"
#include "core/KernelData.h"
#include "log/Log.h"
#include "process/Process.h"
#include "utils/DeadlockMonitor.h"
#include "utils/ThresholdLatch.h"

/***************************************************************************/

#define MUTEX_WAIT_SLEEP_INTERVAL_MS 20
#define MUTEX_REENTRANT_ERROR_TIMEOUT_MS 2000
#define MUTEX_REENTRANT_FORCE_UNLOCK_TIMEOUT_MS 5000
#define MUTEX_TIMEOUT_MIN_LOOP_LIMIT 8
#define MUTEX_TIMEOUT_EXTRA_LOOP_MARGIN 8

/***************************************************************************/

/***************************************************************************/

MUTEX KernelMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&LogMutex, .Prev = NULL, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_KERNEL, .DebugName = TEXT("KernelMutex"), .Lock = 0};
MUTEX LogMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&MemoryMutex, .Prev = (LPLISTNODE)&KernelMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_LOG, .DebugName = TEXT("LogMutex"), .Lock = 0};
MUTEX MemoryMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ScheduleMutex, .Prev = (LPLISTNODE)&LogMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_MEMORY, .DebugName = TEXT("MemoryMutex"), .Lock = 0};
MUTEX ScheduleMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&DesktopMutex, .Prev = (LPLISTNODE)&MemoryMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_SCHEDULE, .DebugName = TEXT("ScheduleMutex"), .Lock = 0};
MUTEX DesktopMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ProcessMutex, .Prev = (LPLISTNODE)&ScheduleMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_DESKTOP, .DebugName = TEXT("DesktopMutex"), .Lock = 0};
MUTEX ProcessMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&TaskMutex, .Prev = (LPLISTNODE)&DesktopMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_PROCESS, .DebugName = TEXT("ProcessMutex"), .Lock = 0};
MUTEX TaskMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&FileSystemMutex, .Prev = (LPLISTNODE)&ProcessMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_TASK, .DebugName = TEXT("TaskMutex"), .Lock = 0};
MUTEX FileSystemMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&FileMutex, .Prev = (LPLISTNODE)&TaskMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_FILESYSTEM, .DebugName = TEXT("FileSystemMutex"), .Lock = 0};
MUTEX FileMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ConsoleStateMutex, .Prev = (LPLISTNODE)&FileSystemMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_FILE, .DebugName = TEXT("FileMutex"), .Lock = 0};
MUTEX ConsoleStateMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ConsoleRenderMutex, .Prev = (LPLISTNODE)&FileMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_CONSOLE_STATE, .DebugName = TEXT("ConsoleStateMutex"), .Lock = 0};
MUTEX ConsoleRenderMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&UserAccountMutex, .Prev = (LPLISTNODE)&ConsoleStateMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_CONSOLE_RENDER, .DebugName = TEXT("ConsoleRenderMutex"), .Lock = 0};
MUTEX UserAccountMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&SessionMutex, .Prev = (LPLISTNODE)&ConsoleRenderMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_USER_ACCOUNT, .DebugName = TEXT("UserAccountMutex"), .Lock = 0};
MUTEX SessionMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = NULL, .Prev = (LPLISTNODE)&UserAccountMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .DebugClass = MUTEX_CLASS_SESSION, .DebugName = TEXT("SessionMutex"), .Lock = 0};

/***************************************************************************/

/**
 * @brief Initializes a mutex structure.
 *
 * @param This Pointer to the mutex to initialize.
 */
void InitMutex(LPMUTEX This) {
    if (This == NULL) return;

    // LISTNODE_FIELDS already initialized if created with CreateKernelObject
    // Only initialize ID, References, Next, Prev if not already set
    if (This->TypeID == 0) {
        This->TypeID = KOID_MUTEX;
        This->References = 1;
        This->OwnerProcess = GetCurrentProcess();
        This->Next = NULL;
        This->Prev = NULL;
        This->Parent = NULL;
    }

    This->Owner = NULL;
    This->Process = NULL;
    This->Task = NULL;
    This->DebugClass = MUTEX_CLASS_NONE;
    This->DebugName = NULL;
    This->Lock = 0;
}

/***************************************************************************/

/**
 * @brief Creates a new mutex and adds it to the kernel mutex list.
 *
 * @return Pointer to the new mutex, or NULL on failure.
 */
LPMUTEX CreateMutex(void) {
    LPMUTEX Mutex = (LPMUTEX)CreateKernelObject(sizeof(MUTEX), KOID_MUTEX);

    SAFE_USE(Mutex) {
        Mutex->Owner = NULL;
        Mutex->Process = NULL;
        Mutex->Task = NULL;
        Mutex->DebugClass = MUTEX_CLASS_NONE;
        Mutex->DebugName = NULL;
        Mutex->Lock = 0;

        LPLIST MutexList = GetMutexList();
        ListAddItem(MutexList, Mutex);

        return Mutex;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Deletes a mutex by decrementing its reference count.
 *
 * @param Mutex Pointer to the mutex to delete.
 * @return TRUE on success, FALSE on failure.
 */
BOOL DeleteMutex(LPMUTEX Mutex) {
    if (Mutex != NULL && Mutex->TypeID == KOID_MUTEX) {
        ReleaseKernelObject(Mutex);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set mutex debug lock-order information.
 *
 * @param Mutex Target mutex.
 * @param DebugClass Lock class identifier.
 * @param DebugName Optional diagnostic name.
 */
void SetMutexDebugInfo(LPMUTEX Mutex, U32 DebugClass, LPCSTR DebugName) {
    SAFE_USE_ID(Mutex, KOID_MUTEX) {
        Mutex->DebugClass = DebugClass;
        Mutex->DebugName = DebugName;
    }
}

/***************************************************************************/

/**
 * @brief Acquires a mutex lock, blocking until available.
 *
 * If the mutex is already owned by the calling task, increments the lock count.
 * Otherwise, waits until the mutex becomes available and then acquires it.
 *
 * @param Mutex Pointer to the mutex to lock.
 * @param TimeOut Timeout value (currently unused).
 * @return Lock count on success, 0 on failure.
 */
UINT LockMutex(LPMUTEX Mutex, UINT TimeOut) {
    LPPROCESS Process;
    LPTASK Task;
    UINT Flags;
    UINT Ret = 0;
    BOOL UseDeadlockMonitor = FALSE;
    BOOL WaitStateActive = FALSE;
    SaveFlags(&Flags);
    DisableInterrupts();

    UseDeadlockMonitor = GetUseDeadlockMonitor();

    //-------------------------------------
    // Check validity of parameters

    SAFE_USE_ID(Mutex, KOID_MUTEX) {
        // Have at leat two tasks
        LPLIST TaskList = GetTaskList();
        SAFE_USE_ID_2(TaskList->First, TaskList->First->Next, KOID_TASK) {
            Task = GetCurrentTask();

            if (Task != NULL && Task->TypeID == KOID_TASK) {
                Process = Task->OwnerProcess;

                if (Process != NULL && Process->TypeID == KOID_PROCESS) {
                    if (Mutex->Task == Task) {
                        Mutex->Lock++;
                        Ret = Mutex->Lock;
                    } else {
                        //-------------------------------------
                        // Wait for mutex to be unlocked by its owner task

                        UINT StartWaitTime = GetSystemTime();
                        UINT LastDebugTime = StartWaitTime;
                        UINT WaitLoopCount = 0;
                        UINT WaitLoopLimit = MUTEX_TIMEOUT_MIN_LOOP_LIMIT;
                        THRESHOLD_LATCH ReentrantErrorLatch;
                        THRESHOLD_LATCH ReentrantForceUnlockLatch;

                        if (UseDeadlockMonitor != FALSE && Mutex->Task != NULL) {
                            DeadlockMonitorOnWaitStart(Task, Mutex);
                            WaitStateActive = TRUE;
                        }

                        if (TimeOut != INFINITY) {
                            WaitLoopLimit = (TimeOut / MUTEX_WAIT_SLEEP_INTERVAL_MS) + MUTEX_TIMEOUT_EXTRA_LOOP_MARGIN;
                            if (WaitLoopLimit < MUTEX_TIMEOUT_MIN_LOOP_LIMIT) {
                                WaitLoopLimit = MUTEX_TIMEOUT_MIN_LOOP_LIMIT;
                            }
                        }

                        ThresholdLatchInit(&ReentrantErrorLatch,
                                           TEXT("Mutex reentrant wait error"),
                                           MUTEX_REENTRANT_ERROR_TIMEOUT_MS,
                                           StartWaitTime);
                        ThresholdLatchInit(&ReentrantForceUnlockLatch,
                                           TEXT("Mutex reentrant force unlock"),
                                           MUTEX_REENTRANT_FORCE_UNLOCK_TIMEOUT_MS,
                                           StartWaitTime);

                        FOREVER {
                            //-------------------------------------
                            // Check if a process deleted this mutex

                            if (Mutex->TypeID != KOID_MUTEX) {
                                if (UseDeadlockMonitor != FALSE && WaitStateActive != FALSE) {
                                    DeadlockMonitorOnWaitCancel(Task, Mutex);
                                }
                                RestoreFlags(&Flags);
                                return 0;
                            }

                            //-------------------------------------
                            // Check if the mutex is not locked anymore

                            if (Mutex->Task == NULL) {
                                break;
                            }

                            //-------------------------------------
                            // Apply caller timeout for non-infinite waits

                            if (TimeOut != INFINITY) {
                                if (HasOperationTimedOut(StartWaitTime, WaitLoopCount, WaitLoopLimit, TimeOut) != FALSE) {
                                    if (UseDeadlockMonitor != FALSE && WaitStateActive != FALSE) {
                                        DeadlockMonitorOnWaitCancel(Task, Mutex);
                                    }
                                    WARNING(TEXT("Timeout while waiting mutex=%p owner_task=%p waiter_task=%p timeout=%u"),
                                        Mutex,
                                        Mutex->Task,
                                        Task,
                                        TimeOut);
                                    RestoreFlags(&Flags);
                                    return 0;
                                }
                            }

                            //-------------------------------------
                            // Detect prolonged recursive ownership and force recovery

                            if (Mutex->Lock > 1) {
                                UINT Now = GetSystemTime();
                                if (ThresholdLatchCheck(&ReentrantErrorLatch, Now)) {
                                    ERROR(TEXT("Reentrant mutex hold timeout mutex=%p owner_task=%p waiter_task=%p lock=%u elapsed=%u ms"),
                                          Mutex,
                                          Mutex->Task,
                                          Task,
                                          Mutex->Lock,
                                          (U32)(Now - StartWaitTime));
                                }

                                if (ThresholdLatchCheck(&ReentrantForceUnlockLatch, Now)) {
                                    ERROR(TEXT("Force unlock after reentrant hold timeout mutex=%p owner_task=%p waiter_task=%p lock=%u elapsed=%u ms"),
                                          Mutex,
                                          Mutex->Task,
                                          Task,
                                          Mutex->Lock,
                                          (U32)(Now - StartWaitTime));
                                    Mutex->Lock = 0;
                                    Mutex->Process = NULL;
                                    Mutex->Task = NULL;
                                    break;
                                }
                            }

                            //-------------------------------------
                            // Periodic debug output every 2 seconds

                            UINT CurrentTime = GetSystemTime();
                            if (CurrentTime - LastDebugTime >= 2000) {
                                DEBUG(TEXT("Task %p (%s) waiting for mutex %p owned by task %p (%s) for %u ms"),
                                      Task, Task->Name,
                                      Mutex, Mutex->Task, Mutex->Task->Name,
                                      (U32)(CurrentTime - StartWaitTime));
                                LastDebugTime = CurrentTime;
                            }

                            //-------------------------------------
                            // Sleep with proper interrupt handling

                            SetTaskStatusDirect(Task, TASK_STATUS_SLEEPING);
                            Task->SchedulerState.WakeUpTime = GetSystemTime() + MUTEX_WAIT_SLEEP_INTERVAL_MS;

                            // Keep interrupts disabled during critical section
                            while (Task->SchedulerState.Status == TASK_STATUS_SLEEPING) {
                                IdleCPU();            // IdleCPU enables interrupts
                                DisableInterrupts();  // Disable immediately after
                            }
                            WaitLoopCount++;
                            // Continue loop with interrupts already disabled
                        }

                        Mutex->Process = Process;
                        Mutex->Task = Task;
                        Mutex->Lock = 1;
                        if (UseDeadlockMonitor != FALSE) {
                            DeadlockMonitorOnAcquire(Task, Mutex);
                        }

                        Ret = Mutex->Lock;
                    }
                }
            }
        }
        else {
            // Consider mutex free if no task valid
            Ret = 1;
        }
    }

    RestoreFlags(&Flags);
    return Ret;
}

/***************************************************************************/

/**
 * @brief Releases a mutex lock.
 *
 * Decrements the lock count and releases the mutex if count reaches zero.
 * Only the task that owns the mutex can unlock it.
 *
 * @param Mutex Pointer to the mutex to unlock.
 * @return TRUE on success, FALSE if the calling task doesn't own the mutex.
 */
BOOL UnlockMutex(LPMUTEX Mutex) {
    LPTASK Task = NULL;
    U32 Flags;

    //-------------------------------------
    // Check validity of parameters

    if (Mutex == NULL) return 0;
    if (Mutex->TypeID != KOID_MUTEX) return 0;

    SaveFlags(&Flags);
    DisableInterrupts();

    Task = GetCurrentTask();

    if (Mutex->Task != Task) goto Out_Error;

    if (Mutex->Lock != 0) Mutex->Lock--;

    if (Mutex->Lock == 0) {
        if (GetUseDeadlockMonitor() != FALSE) {
            DeadlockMonitorOnRelease(Task, Mutex, NULL);
        }
        Mutex->Process = NULL;
        Mutex->Task = NULL;
    }

    RestoreFlags(&Flags);
    return TRUE;

Out_Error:

    RestoreFlags(&Flags);
    return FALSE;
}
