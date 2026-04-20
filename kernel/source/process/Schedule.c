
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


    Schedule

\************************************************************************/

#include "Base.h"
#include "system/Clock.h"
#include "core/ID.h"
#include "core/Kernel.h"
#include "core/KernelEvent.h"
#include "utils/List.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process-Control.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "process/Stack.h"
#include "system/System.h"
#include "process/Task.h"

/***************************************************************************/

typedef struct tag_TASKLIST {
    volatile U32 Freeze;
    volatile U32 SchedulerTime;
    volatile UINT NumTasks;
    volatile UINT CurrentIndex;  // Index of current task instead of pointer
    LPTASK Tasks[NUM_TASKS];
} TASKLIST, *LPTASKLIST;

/***************************************************************************/

#define SCHEDULER_TICK_MAX_CALLBACKS 16

typedef struct tag_SCHEDULER_TICK_SLOT {
    SCHEDULER_TICK_CALLBACK Callback;
    LPVOID Context;
    BOOL InUse;
} SCHEDULER_TICK_SLOT, *LPSCHEDULER_TICK_SLOT;

/***************************************************************************/

static TASKLIST DATA_SECTION TaskList = {.Freeze = 0, .SchedulerTime = 0, .NumTasks = 0, .CurrentIndex = 0, .Tasks = {NULL}};
static SCHEDULER_TICK_SLOT DATA_SECTION SchedulerTickSlots[SCHEDULER_TICK_MAX_CALLBACKS];

/***************************************************************************/

/**
 * @brief Snapshot scheduler-visible state for one task.
 * @param Task Target task.
 * @param Snapshot Receives current scheduler-visible state.
 * @return TRUE on success.
 */
static BOOL ScheduleGetTaskState(LPTASK Task, LPTASK_SCHEDULER_STATE State) {
    return GetTaskSchedulerState(Task, State);
}

/***************************************************************************/

/**
 * @brief Snapshot scheduler-visible state for one process.
 * @param Process Target process.
 * @param Snapshot Receives current scheduler-visible state.
 * @return TRUE on success.
 */
static BOOL ScheduleGetProcessState(LPPROCESS Process, LPPROCESS_SCHEDULER_STATE State) {
    return GetProcessSchedulerState(Process, State);
}

/***************************************************************************/

/**
 * @brief Run registered lightweight scheduler tick callbacks.
 *
 * Callbacks execute in scheduler context and must not block, lock mutexes or
 * perform heavy work. They are intended for cheap state sampling and deferred
 * work signaling only.
 */
static void RunSchedulerTickCallbacks(void) {
    for (UINT Index = 0; Index < SCHEDULER_TICK_MAX_CALLBACKS; Index++) {
        SCHEDULER_TICK_CALLBACK Callback = SchedulerTickSlots[Index].Callback;
        LPVOID Context = SchedulerTickSlots[Index].Context;

        if (SchedulerTickSlots[Index].InUse == FALSE || Callback == NULL) {
            continue;
        }

        Callback(Context);
    }
}

/***************************************************************************/

/**
 * @brief Wakes up tasks whose sleep time has expired.
 *
 * Centralized function to check and wake sleeping tasks. Should be called
 * periodically by the scheduler or timer interrupt.
 */
static void WakeUpExpiredTasks(void) {
    UINT CurrentTime = GetSystemTime();

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        LPTASK Task = TaskList.Tasks[Index];
        TASK_SCHEDULER_STATE State;

        if (ScheduleGetTaskState(Task, &State) == FALSE) continue;

        if (State.Status == TASK_STATUS_SLEEPING && State.WakeUpTime != INFINITY &&
            CurrentTime >= State.WakeUpTime) {
            (void)SetTaskSchedulerStatus(Task, TASK_STATUS_RUNNING);
        }
    }
}

/***************************************************************************/

/**
 * @brief Removes dead tasks from the scheduler queue during context switches.
 *
 * Called when switching TO a task that is not dead. Removes all dead tasks
 * from the queue to prevent them from being scheduled again. This is done
 * during context switches to avoid silent CurrentIndex adjustments.
 *
 * @param ExceptTask Task pointer we're switching to (don't remove it)
 * @return New index of ExceptTask after removals
 */
static UINT RemoveDeadTasksFromQueue(LPTASK ExceptTask) {
    UINT NewExceptIndex = INFINITY;
    TASK_SCHEDULER_STATE State;

    // Search backwards to handle array compaction safely
    for (I32 Index = (I32)(TaskList.NumTasks - 1); Index >= 0; Index--) {
        LPTASK Task = TaskList.Tasks[Index];

        if (Task == ExceptTask) continue;
        if (ScheduleGetTaskState(Task, &State) == FALSE) continue;

        if (State.Status == TASK_STATUS_DEAD) {
            FINE_DEBUG(TEXT("Removing dead task %s at index %d"), Task->Name, Index);

            // Shift remaining tasks down
            for (UINT ShiftIndex = (UINT)Index; ShiftIndex < TaskList.NumTasks - 1; ShiftIndex++) {
                TaskList.Tasks[ShiftIndex] = TaskList.Tasks[ShiftIndex + 1];
            }

            TaskList.NumTasks--;
            TaskList.Tasks[TaskList.NumTasks] = NULL;  // Clear last slot
        }
    }

    // Find new index of ExceptTask
    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == ExceptTask) {
            NewExceptIndex = Index;
            break;
        }
    }

    return NewExceptIndex;
}

/***************************************************************************/

/**
 * @brief Counts the number of tasks that are ready to run.
 *
 * Also wakes up any sleeping tasks whose wake-up time has expired.
 *
 * @return Number of runnable tasks
 */
static UINT CountRunnableTasks(void) {
    UINT RunnableCount = 0;

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        LPTASK Task = TaskList.Tasks[Index];
        TASK_SCHEDULER_STATE State;
        PROCESS_SCHEDULER_STATE ProcessState;

        if (ScheduleGetTaskState(Task, &State) == FALSE) continue;
        if (ScheduleGetProcessState(Task->OwnerProcess, &ProcessState) == FALSE) continue;

        if ((State.Status == TASK_STATUS_READY || State.Status == TASK_STATUS_RUNNING) &&
            State.Suspended == FALSE && ProcessState.Paused == FALSE) {
            RunnableCount++;
        }
    }

    return RunnableCount;
}

/***************************************************************************/

/**
 * @brief Finds the next runnable task starting from a given index.
 *
 * Performs round-robin search through the task list, skipping dead and sleeping tasks.
 * Returns the index of the next runnable task.
 *
 * @param StartIndex Index to start searching from
 * @return Index of next runnable task, or INFINITY if none found
 */
UINT FindNextRunnableTask(UINT StartIndex) {
    for (UINT Attempts = 0; Attempts < TaskList.NumTasks; Attempts++) {
        UINT Index = (StartIndex + Attempts) % TaskList.NumTasks;
        LPTASK Task = TaskList.Tasks[Index];
        TASK_SCHEDULER_STATE State;
        PROCESS_SCHEDULER_STATE ProcessState;

        // Skip dead tasks - they will be removed during context switch
        if (ScheduleGetTaskState(Task, &State) == FALSE) continue;
        if (ScheduleGetProcessState(Task->OwnerProcess, &ProcessState) == FALSE) continue;

        if ((State.Status == TASK_STATUS_READY || State.Status == TASK_STATUS_RUNNING) &&
            State.Suspended == FALSE && ProcessState.Paused == FALSE) {
            return Index;
        }
    }

    return INFINITY;  // No runnable task found
}

/************************************************************************/

/**
 * @brief Adds a task to the scheduler's execution queue.
 *
 * Validates the task, checks for duplicates and capacity, then adds the task
 * to the scheduling queue. Calculates and assigns the task's quantum time
 * based on its priority. If this is the first task, makes it current.
 *
 * @param NewTask Pointer to task to add to scheduler queue
 * @return TRUE if task added successfully, FALSE on error or capacity exceeded
 */
BOOL AddTaskToQueue(LPTASK NewTask) {
    TRACED_FUNCTION;

    FINE_DEBUG(TEXT("NewTask = %p"), NewTask);

    FreezeScheduler();

    // Check validity of parameters
    SAFE_USE_VALID_ID(NewTask, KOID_TASK) {
        // Check if task queue is full
        if (TaskList.NumTasks >= NUM_TASKS) {
            ERROR(TEXT("Cannot add task %p, too many tasks"), NewTask);
            UnfreezeScheduler();

            TRACED_EPILOGUE("AddTaskToQueue");
            return FALSE;
        }

        // Check if task already in task queue
        for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
            if (TaskList.Tasks[Index] == NewTask) {
                UnfreezeScheduler();

                TRACED_EPILOGUE("AddTaskToQueue");
                return TRUE;  // Already present, success
            }
        }

        // Add task to queue
        FINE_DEBUG(TEXT("Adding %p"), NewTask);

        TaskList.Tasks[TaskList.NumTasks] = NewTask;

        // Set quantum time for this task
        SetTaskWakeUpTime(NewTask, ComputeTaskQuantumTime(NewTask->Priority));

        // If this is the first task, make it current
        if (TaskList.NumTasks == 0) {
            TaskList.CurrentIndex = 0;
        }

        TaskList.NumTasks++;

        UnfreezeScheduler();

        TRACED_EPILOGUE("AddTaskToQueue");
        return TRUE;
    }

    UnfreezeScheduler();
    TRACED_EPILOGUE("AddTaskToQueue");
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Removes a task from the scheduler's execution queue.
 *
 * Searches for the task in the queue and removes it, compacting the array.
 * Adjusts the current task index appropriately to maintain scheduling order.
 *
 * @param OldTask Pointer to task to remove from scheduler queue
 * @return TRUE if task removed successfully, FALSE if not found
 */
BOOL RemoveTaskFromQueue(LPTASK OldTask) {
    TRACED_FUNCTION;

    FreezeScheduler();

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == OldTask) {
            // If removing current task, adjust current index
            if (Index == TaskList.CurrentIndex) {
                // Find next runnable task or wrap around
                if (TaskList.NumTasks > 1) {
                    TaskList.CurrentIndex = FindNextRunnableTask((Index + 1) % TaskList.NumTasks);

                    if (TaskList.CurrentIndex >= TaskList.NumTasks - 1) {
                        TaskList.CurrentIndex = 0;  // Wrap to beginning
                    }
                } else {
                    TaskList.CurrentIndex = 0;
                }
            } else if (Index < TaskList.CurrentIndex) {
                TaskList.CurrentIndex--;  // Adjust index after removal
            }

            // Shift remaining tasks
            for (UINT ShiftIndex = Index; ShiftIndex < TaskList.NumTasks - 1; ShiftIndex++) {
                TaskList.Tasks[ShiftIndex] = TaskList.Tasks[ShiftIndex + 1];
            }

            TaskList.NumTasks--;
            TaskList.Tasks[TaskList.NumTasks] = NULL;  // Clear last slot

            UnfreezeScheduler();

            TRACED_EPILOGUE("RemoveTaskFromQueue");
            return TRUE;
        }
    }

    UnfreezeScheduler();

    TRACED_EPILOGUE("RemoveTaskFromQueue");
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Returns the process that owns the currently executing task.
 *
 * @return Pointer to current process, or KernelProcess if no current task
 */
LPPROCESS GetCurrentProcess(void) {
    LPTASK Task = GetCurrentTask();
    SAFE_USE(Task) { return Task->OwnerProcess; }
    return &KernelProcess;
}

/***************************************************************************/

/**
 * @brief Returns the currently executing task.
 *
 * @return Pointer to current task, or NULL if no tasks are scheduled
 */
LPTASK GetCurrentTask(void) {
    LPTASK Task = NULL;

    FreezeScheduler();
    if (TaskList.NumTasks == 0 || TaskList.CurrentIndex >= TaskList.NumTasks) {
    } else {
        Task = TaskList.Tasks[TaskList.CurrentIndex];
    }
    UnfreezeScheduler();

    return Task;
}

/***************************************************************************/

/**
 * @brief Temporarily disables task switching.
 *
 * Increments the freeze counter to prevent the scheduler from switching tasks.
 * Used for atomic operations that must not be interrupted by task switches.
 *
 * @return Always TRUE
 */
BOOL FreezeScheduler(void) {
    U32 Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    TaskList.Freeze++;
    RestoreFlags(&Flags);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Re-enables task switching.
 *
 * Decrements the freeze counter. Task switching is only enabled when the
 * counter reaches zero, allowing nested freeze/unfreeze calls.
 *
 * @return Always TRUE
 */
BOOL UnfreezeScheduler(void) {
    U32 Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    if (TaskList.Freeze) TaskList.Freeze--;
    RestoreFlags(&Flags);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Report whether the scheduler is currently frozen.
 * @return TRUE when the scheduler is frozen, FALSE otherwise.
 */
BOOL IsSchedulerFrozen(void) {
    U32 Flags;
    BOOL Frozen;

    SaveFlags(&Flags);
    DisableInterrupts();
    Frozen = (TaskList.Freeze != 0);
    RestoreFlags(&Flags);

    return Frozen;
}

/************************************************************************/

/**
 * @brief Register one scheduler tick callback.
 * @param Callback Lightweight callback to run from scheduler context.
 * @param Context Opaque callback context.
 * @return Registration handle or SCHEDULER_TICK_INVALID_HANDLE.
 */
U32 SchedulerRegisterTickCallback(SCHEDULER_TICK_CALLBACK Callback, LPVOID Context) {
    UINT Flags;

    if (Callback == NULL) {
        return SCHEDULER_TICK_INVALID_HANDLE;
    }

    for (U32 Index = 0; Index < SCHEDULER_TICK_MAX_CALLBACKS; Index++) {
        SaveFlags(&Flags);
        DisableInterrupts();

        if (SchedulerTickSlots[Index].InUse == FALSE) {
            SchedulerTickSlots[Index].Callback = Callback;
            SchedulerTickSlots[Index].Context = Context;
            SchedulerTickSlots[Index].InUse = TRUE;

            RestoreFlags(&Flags);
            return Index;
        }

        RestoreFlags(&Flags);
    }

    ERROR(TEXT("No free scheduler tick callback slots"));
    return SCHEDULER_TICK_INVALID_HANDLE;
}

/************************************************************************/

/**
 * @brief Unregister one scheduler tick callback.
 * @param Handle Registration handle returned by SchedulerRegisterTickCallback.
 */
void SchedulerUnregisterTickCallback(U32 Handle) {
    UINT Flags;

    if (Handle >= SCHEDULER_TICK_MAX_CALLBACKS) {
        return;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    SchedulerTickSlots[Handle].Callback = NULL;
    SchedulerTickSlots[Handle].Context = NULL;
    SchedulerTickSlots[Handle].InUse = FALSE;

    RestoreFlags(&Flags);
}

/************************************************************************/

void SwitchToNextTask(LPTASK CurrentTask, LPTASK NextTask) {
    TASK_SCHEDULER_STATE NextTaskState;

    FINE_DEBUG(TEXT("CurrentTask = %p (%s), NextTask = %p (%s)"),
        CurrentTask, CurrentTask->Name, NextTask, NextTask->Name);

#if SCHEDULING_DEBUG_OUTPUT == 1
    LINEAR CurrentStackPointer, CurrentFramePointer;
    GetCurrentStackPointer(CurrentStackPointer);
    GetCurrentFramePointer(CurrentFramePointer);
    DEBUG(TEXT("Current SP = %p, current BP = %p"), CurrentStackPointer, CurrentFramePointer);
#endif

    if (ScheduleGetTaskState(NextTask, &NextTaskState) == FALSE) {
        ERROR(TEXT("Invalid next task snapshot"));
        return;
    }

    if (NextTaskState.Status > TASK_STATUS_DEAD) {
        ERROR(TEXT("MEMORY CORRUPTION: Task status %x is out of range"),
            NextTaskState.Status);
        return;
    }

    PHYSICAL NextCr3 = 0;
    if (NextTask != NULL && NextTask->OwnerProcess != NULL) {
        NextCr3 = NextTask->OwnerProcess->PageDirectory;
    }
    if (NextCr3 == 0) {
        NextCr3 = GetPageDirectory();
    }

    // SAFE_USE_VALID_ID_2(CurrentTask, NextTask, KOID_TASK) {
        // __asm__ __volatile__("xchg %%bx,%%bx" : : );     // A breakpoint
        SwitchToNextTask_2(CurrentTask, NextTask, NextCr3);
    // }

    FINE_DEBUG(TEXT("Exit for task %p (%s)"), CurrentTask, CurrentTask->Name);
}

/************************************************************************/

void SwitchToNextTask_3(register LPTASK CurrentTask, register LPTASK NextTask) {
    TASK_SCHEDULER_STATE NextTaskState;

    FINE_DEBUG(TEXT("CurrentTask = %p (%s), NextTask = %p (%s)"),
        CurrentTask, CurrentTask->Name, NextTask, NextTask->Name);

#if SCHEDULING_DEBUG_OUTPUT == 1
    LINEAR CurrentStackPointer, CurrentFramePointer;
    GetCurrentStackPointer(CurrentStackPointer);
    GetCurrentFramePointer(CurrentFramePointer);
    DEBUG(TEXT("Current SP = %p, current BP = %p"), CurrentStackPointer, CurrentFramePointer);
#endif

    PrepareNextTaskSwitch(CurrentTask, NextTask);

    if (ScheduleGetTaskState(NextTask, &NextTaskState) == FALSE) {
        return;
    }

    // First time run for the task
    if (NextTaskState.Status == TASK_STATUS_READY) {
        (void)SetTaskSchedulerStatus(NextTask, TASK_STATUS_RUNNING);

        if (NextTask->OwnerProcess->Privilege == CPU_PRIVILEGE_KERNEL) {
            LINEAR StackPointer = NextTask->Arch.Stack.Base + NextTask->Arch.Stack.Size - STACK_SAFETY_MARGIN;

            FINE_DEBUG(TEXT("StackPointer = %p"), StackPointer);

            SetupStackForKernelMode(NextTask, StackPointer, StackPointer);

#if SCHEDULING_DEBUG_OUTPUT == 1
            KernelLogMem(LOG_DEBUG, StackPointer, 256);
            LogTaskSystemStructures(LOG_DEBUG);
#endif

            FINE_DEBUG(TEXT("Calling JumpToReadyTask (StackPointer = %p)"), StackPointer);

            JumpToReadyTask(NextTask, StackPointer);
        } else {
            LINEAR StackPointer = NextTask->Arch.Stack.Base + NextTask->Arch.Stack.Size - STACK_SAFETY_MARGIN;
            LINEAR SysStackPointer =
                NextTask->Arch.SystemStack.Base + NextTask->Arch.SystemStack.Size - STACK_SAFETY_MARGIN;

            FINE_DEBUG(TEXT("SysStackPointer = %p"), SysStackPointer);
            FINE_DEBUG(TEXT("StackPointer = %p"), StackPointer);

            SetupStackForUserMode(NextTask, SysStackPointer, StackPointer);

#if SCHEDULING_DEBUG_OUTPUT == 1
            KernelLogMem(LOG_DEBUG, SysStackPointer, 256);
            LogTaskSystemStructures(LOG_DEBUG);
#endif

            FINE_DEBUG(TEXT("Calling JumpToReadyTask (SysStackPointer = %p)"), SysStackPointer);

            JumpToReadyTask(NextTask, SysStackPointer);
        }
    }

    FINE_DEBUG(TEXT("Exit"));

#if SCHEDULING_DEBUG_OUTPUT == 1
    LINEAR ESP; GetESP(ESP);
    KernelLogMem(LOG_DEBUG, ESP, 256);
    LogTaskSystemStructures(LOG_DEBUG);
#endif

    // Returning normally to next task
}

/************************************************************************/

/**
 * @brief Main scheduler function called by timer interrupt.
 *
 * This is the core scheduling function that implements preemptive multitasking.
 * It saves the current task context, checks for stack overflows, manages task
 * quantums, wakes sleeping tasks, and selects the next task to run. Performs
 * context switching by setting up ESP0 for Ring 3 tasks and returning the
 * interrupt frame of the next task.
 *
 */
void Scheduler(void) {
    TASK_SCHEDULER_STATE CurrentTaskState;
    TASK_SCHEDULER_STATE NextTaskState;
    PROCESS_SCHEDULER_STATE CurrentProcessState;
    U32 Flags = 0;
    SaveFlags(&Flags);
    FINE_DEBUG(TEXT("Enter : IF = %x"), Flags & 0x200);
    UNUSED(Flags);

    // If scheduler is frozen, don't switch (atomic read - safe in interrupt context)
    if (TaskList.Freeze) {
        FINE_DEBUG(TEXT("TaskList frozen: Returning NULL"));
        return;
    }

    TaskList.SchedulerTime += 10;
    RunSchedulerTickCallbacks();

    // Check for stack overflow - kill dangerous tasks immediately
    /*
    if (!CheckStack()) {
        LPTASK DangerousTask = GetCurrentTask();

        if (DangerousTask) {

            ERROR(TEXT("Killing task due to overflow : %X"), DangerousTask);

            // Mark task as dead - will be removed during next context switch
            DangerousTask->SchedulerState.Status = TASK_STATUS_DEAD;
        }
    }
    */

    // No tasks to schedule
    if (TaskList.NumTasks == 0) {
        return;
    }

    // Check if current task quantum has expired
    LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
    BOOL HasCurrentTaskSnapshot = FALSE;
    BOOL QuantumExpired = FALSE;

    if (CurrentTask != NULL && ScheduleGetTaskState(CurrentTask, &CurrentTaskState) != FALSE) {
        HasCurrentTaskSnapshot = TRUE;
        QuantumExpired =
            CurrentTaskState.WakeUpTime != INFINITY && GetSystemTime() >= CurrentTaskState.WakeUpTime;
    }

    // Wake up expired sleeping tasks first
    WakeUpExpiredTasks();

    // Update sleeping tasks status
    UINT RunnableCount = CountRunnableTasks();

    // No runnable tasks - system idle
    if (RunnableCount == 0) {
        FINE_DEBUG(TEXT("No runnable tasks"));

        return;
    }

    // If current task is still running and quantum not expired, keep it
    if (CurrentTask && HasCurrentTaskSnapshot != FALSE && CurrentTaskState.Status == TASK_STATUS_RUNNING &&
        CurrentTaskState.Suspended == FALSE && !QuantumExpired &&
        ScheduleGetProcessState(CurrentTask->OwnerProcess, &CurrentProcessState) != FALSE &&
        CurrentProcessState.Paused == FALSE) {
        FINE_DEBUG(TEXT("Current task continues"));

        return;
    }

    // Time to switch - find next runnable task
    UINT NextIndex = FindNextRunnableTask((TaskList.CurrentIndex + 1) % TaskList.NumTasks);

    if (TaskList.CurrentIndex != NextIndex) {
        // Get task pointers BEFORE any queue manipulation
        LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
        LPTASK NextTask = TaskList.Tasks[NextIndex];

        FINE_DEBUG(TEXT("Switch between task index %u (%s @ %s) and %u (%s @ %s)"),
            TaskList.CurrentIndex, CurrentTask ? CurrentTask->Name : TEXT("NULL"),
            CurrentTask ? CurrentTask->OwnerProcess->FileName : TEXT("NULL"), NextIndex, NextTask->Name,
            NextTask->OwnerProcess->FileName);

        if (NextIndex >= TaskList.NumTasks) {
            // Should not happen if RunnableCount > 0, but safety check
            FINE_DEBUG(TEXT("No next task found"));

            return;
        }

        // Remove dead tasks from queue now that we're switching TO a non-dead task
        // This prevents silent CurrentIndex adjustments and phantom task changes
        if (ScheduleGetTaskState(NextTask, &NextTaskState) == FALSE) {
            return;
        }

        if (NextTaskState.Status != TASK_STATUS_DEAD) {
            NextIndex = RemoveDeadTasksFromQueue(NextTask);

            if (NextIndex == INFINITY) {
                // NextTask was somehow removed - this should not happen
                ERROR(TEXT("NextTask was removed during cleanup!"));
                return;
            }
        }

        TaskList.CurrentIndex = NextIndex;
        TaskList.SchedulerTime = 0;

        if (CurrentTask && CurrentTask->OwnerProcess != NextTask->OwnerProcess &&
            CurrentTask->OwnerProcess->Privilege != NextTask->OwnerProcess->Privilege) {
            FINE_DEBUG(TEXT("Different ring switch :"));
        }

        SwitchToNextTask(CurrentTask, NextTask);
    }
}

/************************************************************************/

static BOOL MatchObject(LPVOID Data, LPVOID Context) {
    LPOBJECT_TERMINATION_STATE State = (LPOBJECT_TERMINATION_STATE)Data;
    LPOBJECT KernelObject = (LPOBJECT)Context;

    if (State == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID(KernelObject) {
        return U64_EQUAL(State->InstanceID, KernelObject->InstanceID);
    }

    return FALSE;
}

/************************************************************************/

static BOOL IsObjectSignaled(LPVOID Object) {
    LockMutex(MUTEX_KERNEL, INFINITY);

    // First check termination cache
    LPOBJECT_TERMINATION_STATE TermState = (LPOBJECT_TERMINATION_STATE)CacheFind(
        GetObjectTerminationCache(),
        MatchObject,
        Object
    );

    SAFE_USE(TermState) {
        DEBUG(TEXT("Object %x found in termination cache - marking as signaled"), Object);
        UnlockMutex(MUTEX_KERNEL);
        return TRUE;
    }

    UnlockMutex(MUTEX_KERNEL);

    SAFE_USE_VALID((LPOBJECT)Object) {
        LPOBJECT KernelObject = (LPOBJECT)Object;

        if (KernelObject->TypeID == KOID_KERNELEVENT) {
            return KernelEventIsSignaled((LPKERNEL_EVENT)Object);
        }
    }

    return FALSE;
}

/************************************************************************/

static UINT GetObjectExitCode(LPVOID Object) {

    LockMutex(MUTEX_KERNEL, INFINITY);

    // First check termination cache
    LPOBJECT_TERMINATION_STATE TermState = (LPOBJECT_TERMINATION_STATE)CacheFind(
        GetObjectTerminationCache(),
        MatchObject,
        Object
    );

    SAFE_USE(TermState) {
        DEBUG(TEXT("Object %x found in termination cache, ExitCode=%u"), Object, TermState->ExitCode);
        UnlockMutex(MUTEX_KERNEL);
        return TermState->ExitCode;
    }

    UnlockMutex(MUTEX_KERNEL);

    SAFE_USE_VALID((LPOBJECT)Object) {
        LPOBJECT KernelObject = (LPOBJECT)Object;

        if (KernelObject->TypeID == KOID_KERNELEVENT) {
            return KernelEventGetSignalCount((LPKERNEL_EVENT)Object);
        }
    }

    return MAX_UINT;
}

/************************************************************************/

U32 Wait(LPWAIT_INFO WaitInfo) {
    UINT Index;
    UINT StartTime;
    LPTASK CurrentTask;

    if (WaitInfo == NULL || WaitInfo->Count == 0 || WaitInfo->Count > WAIT_INFO_MAX_OBJECTS) {
        return WAIT_INVALID_PARAMETER;
    }

    CurrentTask = GetCurrentTask();
    if (CurrentTask == NULL) {
        return WAIT_INVALID_PARAMETER;
    }

    StartTime = GetSystemTime();
    UINT LastDebugTime = StartTime;

    FOREVER {
        UINT SignaledCount = 0;

        // Count signaled objects
        for (Index = 0; Index < WaitInfo->Count; Index++) {
            if (IsObjectSignaled((LPVOID)WaitInfo->Objects[Index])) {
                SignaledCount++;
            }
        }

        if (WaitInfo->Flags & WAIT_FLAG_ALL) {
            if (SignaledCount == WaitInfo->Count) {
                for (Index = 0; Index < WaitInfo->Count; Index++) {
                    WaitInfo->ExitCodes[Index] = GetObjectExitCode((LPVOID)WaitInfo->Objects[Index]);
                }
                return WAIT_OBJECT_0;
            }
        } else {
            if (SignaledCount > 0) {
                for (Index = 0; Index < WaitInfo->Count; Index++) {
                    if (IsObjectSignaled((LPVOID)WaitInfo->Objects[Index])) {
                        WaitInfo->ExitCodes[Index] = GetObjectExitCode((LPVOID)WaitInfo->Objects[Index]);
                        return WAIT_OBJECT_0 + Index;
                    }
                }
            }
        }

        UINT CurrentTime = GetSystemTime();

        if (WaitInfo->MilliSeconds != MAX_U32) {
            if (CurrentTime - StartTime >= WaitInfo->MilliSeconds) {
                return WAIT_TIMEOUT;
            }
        }

        // Periodic debug output every 2 seconds
        if (CurrentTime - LastDebugTime >= 2000) {
            DEBUG(TEXT("Task %p (%s) waiting for %u objects for %u ms"),
                  CurrentTask, CurrentTask->Name, WaitInfo->Count, (U32)(CurrentTime - StartTime));
            LastDebugTime = CurrentTime;
        }

        SetTaskStatus(CurrentTask, TASK_STATUS_SLEEPING);
        Sleep(50);
        SetTaskStatus(CurrentTask, TASK_STATUS_RUNNING);
    }

    return WAIT_TIMEOUT;
}
