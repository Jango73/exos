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


    Process control

\************************************************************************/

#include "process/Process-Control.h"

#include "log/Log.h"
#include "memory/Memory.h"

/************************************************************************/

BOOL ProcessControlIsMessage(U32 Message) {
    switch (Message) {
        case ETM_INTERRUPT:
        case ETM_PROCESS_KILL:
        case ETM_PROCESS_TOGGLE_PAUSE:
            return TRUE;
        default:
            return FALSE;
    }
}

/************************************************************************/

void ProcessControlRequestInterrupt(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Process->ControlFlags |= PROCESS_CONTROL_FLAG_INTERRUPT_PENDING;
        UnlockMutex(&(Process->Mutex));
    }
}

/************************************************************************/

BOOL ProcessControlIsInterruptRequested(LPPROCESS Process) {
    BOOL Requested = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Requested = (Process->ControlFlags & PROCESS_CONTROL_FLAG_INTERRUPT_PENDING) != 0;
        UnlockMutex(&(Process->Mutex));
        return Requested;
    }

    return FALSE;
}

/************************************************************************/

BOOL ProcessControlConsumeInterrupt(LPPROCESS Process) {
    BOOL Requested = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Requested = (Process->ControlFlags & PROCESS_CONTROL_FLAG_INTERRUPT_PENDING) != 0;
        Process->ControlFlags &= ~PROCESS_CONTROL_FLAG_INTERRUPT_PENDING;
        UnlockMutex(&(Process->Mutex));
        return Requested;
    }

    return FALSE;
}

/************************************************************************/

BOOL ProcessControlCheckpoint(LPPROCESS Process) {
    return ProcessControlConsumeInterrupt(Process);
}

/************************************************************************/

BOOL ProcessControlSetPaused(LPPROCESS Process, BOOL Paused) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process == &KernelProcess) {
            return FALSE;
        }

        LockMutex(&(Process->Mutex), INFINITY);
        Process->SchedulerState.Paused = Paused ? TRUE : FALSE;
        UnlockMutex(&(Process->Mutex));
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL ProcessControlTogglePaused(LPPROCESS Process) {
    BOOL Paused = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process == &KernelProcess) {
            return FALSE;
        }

        LockMutex(&(Process->Mutex), INFINITY);
        Process->SchedulerState.Paused = Process->SchedulerState.Paused ? FALSE : TRUE;
        Paused = Process->SchedulerState.Paused;
        UnlockMutex(&(Process->Mutex));

        DEBUG(TEXT("Process %s is %s"), Process->FileName, Paused ? TEXT("paused") : TEXT("running"));

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL ProcessControlIsProcessPaused(LPPROCESS Process) {
    BOOL Paused = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Paused = Process->SchedulerState.Paused;
        UnlockMutex(&(Process->Mutex));
        return Paused;
    }

    return FALSE;
}

/************************************************************************/

BOOL GetProcessSchedulerState(LPPROCESS Process, LPPROCESS_SCHEDULER_STATE State) {
    if (State == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        State->Paused = Process->SchedulerState.Paused;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL ProcessControlHandleMessage(LPPROCESS Process, U32 Message, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    if (ProcessControlIsMessage(Message) == FALSE) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        switch (Message) {
            case ETM_INTERRUPT:
                ProcessControlRequestInterrupt(Process);
                return TRUE;

            case ETM_PROCESS_KILL:
                if (Process == &KernelProcess) {
                    ProcessControlRequestInterrupt(Process);
                } else {
                    KillProcess(Process);
                }
                return TRUE;

            case ETM_PROCESS_TOGGLE_PAUSE:
                if (Process == &KernelProcess) {
                    return TRUE;
                }

                ProcessControlTogglePaused(Process);
                return TRUE;
        }
    }

    return FALSE;
}
