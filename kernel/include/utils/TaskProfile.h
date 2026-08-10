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

#ifndef TASKPROFILE_H_INCLUDED
#define TASKPROFILE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "User.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_TASK_PROFILE_STATE {
    UINT TotalRunTimeMilliseconds;        // CPU time spent running
    UINT TotalSleepTimeMilliseconds;      // time spent sleeping or waiting
    UINT TotalWakeupLatencyMilliseconds;  // cumulated sleep-deadline to dispatch delay
    UINT DispatchCount;                   // times dispatched to the CPU
    UINT PreemptionCount;                 // times forced out before quantum expiry
    UINT WakeupCount;                     // times woken from sleep and then dispatched
    UINT QuantumGrantedMilliseconds;      // last quantum granted
    UINT QuantumUsedMilliseconds;         // duration of the last run
    UINT MaxQuantumUsedMilliseconds;      // longest run observed
    UINT LastEventTime;                   // internal last status-change time
    UINT LastDispatchTime;                // internal last RUNNING entry time
    UINT LastWakeupTime;                  // internal last sleep-to-ready time
    U32 LastStatus;                       // internal last observed status
    BOOL WakeupPending;                   // internal wakeup latency pending accounting
} TASK_PROFILE_STATE, *LPTASK_PROFILE_STATE;

/************************************************************************/

void TaskProfileInitialize(LPTASK_PROFILE_STATE State, UINT CurrentTime);
void TaskProfileStatusChanged(LPTASK_PROFILE_STATE State, U32 OldStatus, U32 NewStatus, UINT CurrentTime);
void TaskProfileSetQuantumGranted(LPTASK_PROFILE_STATE State, UINT QuantumGranted);
UINT TaskProfileGetStats(LPTASK_PROFILE_QUERY_INFO Info);

/************************************************************************/

#pragma pack(pop)

#endif
