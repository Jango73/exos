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


    Per-task profiling helpers

\************************************************************************/

#include "utils/TaskProfile.h"

#include "core/KernelData.h"
#include "process/Process.h"
#include "sync/Mutex.h"
#include "system/Clock.h"
#include "text/CoreString.h"

/************************************************************************/

#if PROFILING

/************************************************************************/

static BOOL TaskProfileIsSleepingStatus(U32 Status) {
    return Status == TASK_STATUS_SLEEPING || Status == TASK_STATUS_WAITING || Status == TASK_STATUS_WAITMESSAGE;
}

/************************************************************************/

static UINT TaskProfileElapsed(LPTASK_PROFILE_STATE State, UINT CurrentTime) {
    if (State == NULL) {
        return 0;
    }

    if (CurrentTime >= State->LastEventTime) {
        return CurrentTime - State->LastEventTime;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Initialize one task profiling state.
 *
 * Zeroes every counter and anchors the event clock at the provided time.
 *
 * @param State Profiling state to initialize.
 * @param CurrentTime Current system time in milliseconds.
 */
void TaskProfileInitialize(LPTASK_PROFILE_STATE State, UINT CurrentTime) {
    if (State == NULL) {
        return;
    }

    MemorySet(State, 0, sizeof(TASK_PROFILE_STATE));
    State->LastEventTime = CurrentTime;
}

/************************************************************************/

/**
 * @brief Account one task status transition.
 *
 * The elapsed time since the previous transition is attributed to the old
 * status: RUNNING time is counted as CPU time and, when the task leaves the
 * CPU, as the quantum actually used; sleeping-like statuses are counted as
 * sleep time. Entering RUNNING records a dispatch and, when the task was
 * woken from sleep, the wakeup latency.
 *
 * Callers must pass a monotonic CurrentTime; the function is interrupt-safe
 * and lock-free.
 *
 * @param State Profiling state to update.
 * @param OldStatus Status the task had before the transition.
 * @param NewStatus Status the task now has.
 * @param CurrentTime Current system time in milliseconds.
 */
void TaskProfileStatusChanged(LPTASK_PROFILE_STATE State, U32 OldStatus, U32 NewStatus, UINT CurrentTime) {
    UINT Elapsed;

    if (State == NULL) {
        return;
    }

    Elapsed = TaskProfileElapsed(State, CurrentTime);

    if (OldStatus == TASK_STATUS_RUNNING) {
        State->TotalRunTimeMilliseconds += Elapsed;
        State->QuantumUsedMilliseconds = Elapsed;
        if (Elapsed > State->MaxQuantumUsedMilliseconds) {
            State->MaxQuantumUsedMilliseconds = Elapsed;
        }

        if (NewStatus == TASK_STATUS_READY) {
            State->PreemptionCount++;
        }
    } else if (TaskProfileIsSleepingStatus(OldStatus)) {
        if (NewStatus == TASK_STATUS_READY || NewStatus == TASK_STATUS_RUNNING) {
            State->TotalSleepTimeMilliseconds += Elapsed;
        }

        if (NewStatus == TASK_STATUS_READY) {
            State->LastWakeupTime = CurrentTime;
            State->WakeupPending = TRUE;
        }
    }

    if (NewStatus == TASK_STATUS_RUNNING) {
        State->DispatchCount++;
        if (State->WakeupPending != FALSE) {
            State->WakeupCount++;
            if (CurrentTime >= State->LastWakeupTime) {
                State->TotalWakeupLatencyMilliseconds += CurrentTime - State->LastWakeupTime;
            }
            State->WakeupPending = FALSE;
        }

        State->LastDispatchTime = CurrentTime;
    }

    State->LastStatus = NewStatus;
    State->LastEventTime = CurrentTime;
}

/************************************************************************/

/**
 * @brief Record the quantum granted to a task.
 *
 * Called when the scheduler rearms a time slice so the displayed quantum
 * reflects the policy actually applied. Infinite slices are ignored.
 *
 * @param State Profiling state to update.
 * @param QuantumGranted Granted quantum in milliseconds.
 */
void TaskProfileSetQuantumGranted(LPTASK_PROFILE_STATE State, UINT QuantumGranted) {
    if (State == NULL) {
        return;
    }

    if (QuantumGranted == INFINITY) {
        return;
    }

    State->QuantumGrantedMilliseconds = QuantumGranted;
}

/************************************************************************/

static void TaskProfileReset(LPTASK_PROFILE_STATE State, UINT CurrentTime) {
    if (State == NULL) {
        return;
    }

    MemorySet(State, 0, sizeof(TASK_PROFILE_STATE));
    State->LastEventTime = CurrentTime;
}

/************************************************************************/

static void TaskProfileFillEntry(LPTASK Task, LPTASK_PROFILE_ENTRY_INFO Entry) {
    if (Task == NULL || Entry == NULL) {
        return;
    }

    MemorySet(Entry, 0, sizeof(TASK_PROFILE_ENTRY_INFO));

    StringCopyLimit(Entry->Name, Task->Name, TASK_PROFILE_NAME_LENGTH);
    Entry->Status = Task->SchedulerState.Status;
    Entry->Priority = Task->Priority;
    Entry->QuantumGrantedMilliseconds = Task->Profile.QuantumGrantedMilliseconds;
    Entry->QuantumUsedMilliseconds = Task->Profile.QuantumUsedMilliseconds;
    Entry->MaxQuantumUsedMilliseconds = Task->Profile.MaxQuantumUsedMilliseconds;
    Entry->TotalRunTimeMilliseconds = Task->Profile.TotalRunTimeMilliseconds;
    Entry->TotalSleepTimeMilliseconds = Task->Profile.TotalSleepTimeMilliseconds;
    Entry->TotalWakeupLatencyMilliseconds = Task->Profile.TotalWakeupLatencyMilliseconds;
    Entry->DispatchCount = Task->Profile.DispatchCount;
    Entry->PreemptionCount = Task->Profile.PreemptionCount;
    Entry->WakeupCount = Task->Profile.WakeupCount;
}

/************************************************************************/

/**
 * @brief Capture a bounded snapshot of per-task profiling counters.
 *
 * Iterates the kernel task list and copies each task profile into the
 * destination buffer. Totals are computed over every task regardless of the
 * destination capacity. When TASK_PROFILE_QUERY_FLAG_RESET is set, all
 * profiles are reset after the snapshot.
 *
 * @param Info Snapshot descriptor and destination buffer.
 * @return UINT Number of copied entries.
 */
UINT TaskProfileGetStats(LPTASK_PROFILE_QUERY_INFO Info) {
    UINT EntryCount = 0;
    UINT TotalTaskCount = 0;
    UINT TotalDispatchCount = 0;
    UINT TotalRunTimeMilliseconds = 0;
    UINT TotalSleepTimeMilliseconds = 0;

    if (Info == NULL) {
        return 0;
    }

    LockMutex(MUTEX_TASK, INFINITY);

    LPLIST TaskList = GetTaskList();
    if (TaskList != NULL) {
        for (LPLISTNODE Node = TaskList->First; Node; Node = Node->Next) {
            LPTASK Task = (LPTASK)Node;

            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                TotalTaskCount++;
                TotalDispatchCount += Task->Profile.DispatchCount;
                TotalRunTimeMilliseconds += Task->Profile.TotalRunTimeMilliseconds;
                TotalSleepTimeMilliseconds += Task->Profile.TotalSleepTimeMilliseconds;

                if (EntryCount < Info->Capacity && Info->Entries != NULL) {
                    TaskProfileFillEntry(Task, &(Info->Entries[EntryCount]));
                    EntryCount++;
                }
            }
        }

        if ((Info->Flags & TASK_PROFILE_QUERY_FLAG_RESET) != 0) {
            for (LPLISTNODE Node = TaskList->First; Node; Node = Node->Next) {
                LPTASK Task = (LPTASK)Node;

                SAFE_USE_VALID_ID(Task, KOID_TASK) {
                    TaskProfileReset(&(Task->Profile), GetSystemTime());
                }
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    Info->EntryCount = EntryCount;
    Info->TotalTaskCount = TotalTaskCount;
    Info->TotalDispatchCount = TotalDispatchCount;
    Info->TotalRunTimeMilliseconds = TotalRunTimeMilliseconds;
    Info->TotalSleepTimeMilliseconds = TotalSleepTimeMilliseconds;

    return EntryCount;
}

/************************************************************************/

#else

void TaskProfileInitialize(LPTASK_PROFILE_STATE State, UINT CurrentTime) {
    UNUSED(State);
    UNUSED(CurrentTime);
}

/************************************************************************/

void TaskProfileStatusChanged(LPTASK_PROFILE_STATE State, U32 OldStatus, U32 NewStatus, UINT CurrentTime) {
    UNUSED(State);
    UNUSED(OldStatus);
    UNUSED(NewStatus);
    UNUSED(CurrentTime);
}

/************************************************************************/

void TaskProfileSetQuantumGranted(LPTASK_PROFILE_STATE State, UINT QuantumGranted) {
    UNUSED(State);
    UNUSED(QuantumGranted);
}

/************************************************************************/

UINT TaskProfileGetStats(LPTASK_PROFILE_QUERY_INFO Info) {
    if (Info != NULL) {
        Info->EntryCount = 0;
        Info->TotalTaskCount = 0;
        Info->TotalDispatchCount = 0;
        Info->TotalRunTimeMilliseconds = 0;
        Info->TotalSleepTimeMilliseconds = 0;
    }

    return 0;
}

/************************************************************************/

#endif
