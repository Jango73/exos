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


    Desktop timer

\************************************************************************/

#include "Desktop-Timer.h"

#include "system/Clock.h"
#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Task-Messaging.h"

/***************************************************************************/

#define DESKTOP_TIMER_TASK_NAME TEXT("DesktopTimer")
#define DESKTOP_TIMER_POLL_MS 10

/***************************************************************************/

typedef struct tag_DESKTOP_WINDOW_TIMER {
    LISTNODE_FIELDS
    LPWINDOW Window;
    U32 TimerID;
    U32 IntervalMilliseconds;
    U32 NextTick;
} DESKTOP_WINDOW_TIMER, *LPDESKTOP_WINDOW_TIMER;

/***************************************************************************/

typedef struct tag_DESKTOP_TIMER_DUE_ENTRY {
    HANDLE Window;
    U32 TimerID;
} DESKTOP_TIMER_DUE_ENTRY, *LPDESKTOP_TIMER_DUE_ENTRY;

/***************************************************************************/

static void DesktopWindowTimerDestructor(LPVOID Item) {
    SAFE_USE(Item) {
        KernelHeapFree(Item);
    }
}

/***************************************************************************/

static BOOL IsTimerDue(U32 Now, U32 DueTime) {
    I32 Delta = (I32)(Now - DueTime);
    return (Delta >= 0);
}

/***************************************************************************/

static BOOL EnsureDesktopTimerList(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->TimerMutex), INFINITY);

    if (Desktop->Timers == NULL) {
        Desktop->Timers = NewList(DesktopWindowTimerDestructor, KernelHeapAlloc, KernelHeapFree);
    }

    UnlockMutex(&(Desktop->TimerMutex));

    return (Desktop->Timers != NULL);
}

/***************************************************************************/

static U32 DesktopTimerTask(LPVOID Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;
    U32 Now;
    LPLIST Timers;
    LPLISTNODE Node;
    LPDESKTOP_WINDOW_TIMER Timer;
    LPDESKTOP_TIMER_DUE_ENTRY DueEntries = NULL;
    UINT DueCapacity = 0;
    UINT DueCount = 0;
    UINT Index;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return 0;
    }

    FOREVER {
        if (Desktop->TypeID != KOID_DESKTOP) {
            break;
        }

        Now = GetSystemTime();

        LockMutex(&(Desktop->TimerMutex), INFINITY);
        Timers = Desktop->Timers;
        DueCapacity = (Timers != NULL) ? Timers->NumItems : 0;
        UnlockMutex(&(Desktop->TimerMutex));

        if (DueCapacity > 0) {
            DueEntries = (LPDESKTOP_TIMER_DUE_ENTRY)KernelHeapAlloc(sizeof(DESKTOP_TIMER_DUE_ENTRY) * DueCapacity);
            if (DueEntries == NULL) {
                Sleep(DESKTOP_TIMER_POLL_MS);
                continue;
            }
        }

        DueCount = 0;

        LockMutex(&(Desktop->TimerMutex), INFINITY);

        Timers = Desktop->Timers;
        for (Node = Timers != NULL ? Timers->First : NULL; Node != NULL; Node = Node->Next) {
            Timer = (LPDESKTOP_WINDOW_TIMER)Node;
            if (Timer == NULL) continue;
            if (Timer->IntervalMilliseconds == 0) continue;
            if (Timer->Window == NULL || Timer->Window->TypeID != KOID_WINDOW) continue;
            if (IsTimerDue(Now, Timer->NextTick) == FALSE) continue;

            Timer->NextTick = Now + Timer->IntervalMilliseconds;
            if (DueEntries != NULL && DueCount < DueCapacity) {
                DueEntries[DueCount].Window = (HANDLE)Timer->Window;
                DueEntries[DueCount].TimerID = Timer->TimerID;
                DueCount++;
            }
        }

        UnlockMutex(&(Desktop->TimerMutex));

        for (Index = 0; Index < DueCount; Index++) {
            (void)PostMessage(DueEntries[Index].Window, EWM_TIMER, DueEntries[Index].TimerID, 0);
        }

        if (DueEntries != NULL) {
            KernelHeapFree(DueEntries);
            DueEntries = NULL;
        }

        Sleep(DESKTOP_TIMER_POLL_MS);
    }

    return 0;
}

/***************************************************************************/

BOOL DesktopTimerEnsureTask(LPDESKTOP Desktop) {
    TASK_INFO TaskInfo;
    LPTASK TimerTask;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (EnsureDesktopTimerList(Desktop) == FALSE) return FALSE;

    LockMutex(&(Desktop->TimerMutex), INFINITY);

    SAFE_USE_VALID_ID(Desktop->TimerTask, KOID_TASK) {
        UnlockMutex(&(Desktop->TimerMutex));
        return TRUE;
    }

    UnlockMutex(&(Desktop->TimerMutex));

    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TaskInfo);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = DesktopTimerTask;
    TaskInfo.Parameter = Desktop;
    TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;
    StringCopy(TaskInfo.Name, DESKTOP_TIMER_TASK_NAME);

    TimerTask = KernelCreateTask(&KernelProcess, &TaskInfo);
    if (TimerTask == NULL) {
        WARNING(TEXT("[DesktopTimerEnsureTask] Unable to create desktop timer task"));
        return FALSE;
    }

    if (EnsureAllMessageQueues(TimerTask, TRUE) == FALSE) {
        WARNING(TEXT("[DesktopTimerEnsureTask] Unable to initialize timer task message queues"));
        return FALSE;
    }

    LockMutex(&(Desktop->TimerMutex), INFINITY);
    Desktop->TimerTask = TimerTask;
    UnlockMutex(&(Desktop->TimerMutex));

    return TRUE;
}

/***************************************************************************/

BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds) {
    LPWINDOW This = (LPWINDOW)Window;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    LPDESKTOP_WINDOW_TIMER Timer;
    U32 NextTick;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (TimerID == 0) return FALSE;
    if (IntervalMilliseconds == 0) return KillWindowTimer(Window, TimerID);

    Desktop = DesktopGetWindowDesktop(This);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (DesktopTimerEnsureTask(Desktop) == FALSE) return FALSE;

    NextTick = GetSystemTime() + IntervalMilliseconds;


    LockMutex(&(Desktop->TimerMutex), INFINITY);

    for (Node = Desktop->Timers != NULL ? Desktop->Timers->First : NULL; Node != NULL; Node = Node->Next) {
        Timer = (LPDESKTOP_WINDOW_TIMER)Node;
        if (Timer == NULL) continue;
        if (Timer->Window != This) continue;
        if (Timer->TimerID != TimerID) continue;

        Timer->IntervalMilliseconds = IntervalMilliseconds;
        Timer->NextTick = NextTick;
        UnlockMutex(&(Desktop->TimerMutex));
        return TRUE;
    }

    Timer = (LPDESKTOP_WINDOW_TIMER)KernelHeapAlloc(sizeof(DESKTOP_WINDOW_TIMER));
    if (Timer == NULL) {
        UnlockMutex(&(Desktop->TimerMutex));
        return FALSE;
    }

    MemorySet(Timer, 0, sizeof(DESKTOP_WINDOW_TIMER));
    Timer->TypeID = KOID_NONE;
    Timer->Window = This;
    Timer->TimerID = TimerID;
    Timer->IntervalMilliseconds = IntervalMilliseconds;
    Timer->NextTick = NextTick;
    ListAddItem(Desktop->Timers, Timer);

    UnlockMutex(&(Desktop->TimerMutex));
    return TRUE;
}

/***************************************************************************/

BOOL KillWindowTimer(HANDLE Window, U32 TimerID) {
    LPWINDOW This = (LPWINDOW)Window;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    LPLISTNODE Next;
    LPDESKTOP_WINDOW_TIMER Timer;
    BOOL Removed = FALSE;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (TimerID == 0) return FALSE;

    Desktop = DesktopGetWindowDesktop(This);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->TimerMutex), INFINITY);

    for (Node = Desktop->Timers != NULL ? Desktop->Timers->First : NULL; Node != NULL; Node = Next) {
        Next = Node->Next;
        Timer = (LPDESKTOP_WINDOW_TIMER)Node;
        if (Timer == NULL) continue;
        if (Timer->Window != This) continue;
        if (Timer->TimerID != TimerID) continue;

        ListRemove(Desktop->Timers, Timer);
        KernelHeapFree(Timer);
        Removed = TRUE;
    }

    UnlockMutex(&(Desktop->TimerMutex));
    return Removed;
}

/***************************************************************************/

void DesktopTimerRemoveWindowTimers(LPDESKTOP Desktop, LPWINDOW Window) {
    LPLISTNODE Node;
    LPLISTNODE Next;
    LPDESKTOP_WINDOW_TIMER Timer;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    LockMutex(&(Desktop->TimerMutex), INFINITY);

    for (Node = Desktop->Timers != NULL ? Desktop->Timers->First : NULL; Node != NULL; Node = Next) {
        Next = Node->Next;
        Timer = (LPDESKTOP_WINDOW_TIMER)Node;
        if (Timer == NULL) continue;
        if (Timer->Window != Window) continue;

        ListRemove(Desktop->Timers, Timer);
        KernelHeapFree(Timer);
    }

    UnlockMutex(&(Desktop->TimerMutex));
}

/***************************************************************************/
