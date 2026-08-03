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


    Desktop dispatcher

\************************************************************************/

#include "Desktop-Dispatcher.h"
#include "Desktop-Private.h"

#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Process.h"
#include "process/Task.h"
#include "process/Task-Messaging.h"

/***************************************************************************/
// Macros

#define DESKTOP_DISPATCHER_TASK_NAME TEXT("DesktopDispatcher")

/***************************************************************************/

/**
 * @brief Check whether a task is the desktop dispatcher.
 * @param Task Task to inspect.
 * @return TRUE when the task matches the desktop dispatcher identity.
 */
static BOOL IsDesktopDispatcherTask(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (StringCompareNC(Task->Name, DESKTOP_DISPATCHER_TASK_NAME) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Recursively assign one owner task to a window subtree.
 * @param Window Subtree root window.
 * @param Task Target owner task.
 */
static void DesktopAssignWindowTaskRecursive(LPWINDOW Window, LPTASK Task) {
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    UINT ChildCount = 0;
    UINT ChildIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (Task == NULL || Task->TypeID != KOID_TASK) return;

    (void)DesktopSetWindowTask(Window, Task);
    (void)DesktopSnapshotWindowChildren(Window, &Children, &ChildCount);

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Child = Children[ChildIndex];
        DesktopAssignWindowTaskRecursive(Child, Task);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }
}

/***************************************************************************/

/**
 * @brief Dedicated desktop message loop task.
 * @param Parameter Desktop pointer.
 * @return Unused.
 */
static U32 DesktopDispatcherTask(LPVOID Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;
    MESSAGE_INFO Message;
    U32 DispatchFailureCount = 0;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
    }

    FOREVER {
        MemorySet(&Message, 0, sizeof(Message));
        Message.Header.Size = sizeof(Message);
        Message.Header.Version = EXOS_ABI_VERSION;
        Message.Header.Flags = 0;
        Message.Target = NULL;

        if (GetMessage(&Message) == FALSE) {
            continue;
        }

        if (DispatchMessage(&Message) == FALSE && DispatchFailureCount < 64) {
            DispatchFailureCount++;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Ensure one desktop dispatcher task exists for one desktop.
 * @param Desktop Desktop that owns the dispatcher.
 * @return TRUE when dispatcher is available.
 */
BOOL DesktopEnsureDispatcherTask(LPDESKTOP Desktop) {
    TASK_INFO TaskInfo;
    LPTASK DispatcherTask;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Desktop->Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Desktop->Task->OwnerProcess, KOID_PROCESS) {
            if (Desktop->Task->OwnerProcess->Privilege == CPU_PRIVILEGE_USER) {
                return EnsureAllMessageQueues(Desktop->Task, TRUE);
            }
        }

        if (IsDesktopDispatcherTask(Desktop->Task) != FALSE) {
            if (EnsureAllMessageQueues(Desktop->Task, TRUE) == FALSE) {
                WARNING(TEXT("Unable to initialize dispatcher message queues"));
                return FALSE;
            }
            SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
                DesktopAssignWindowTaskRecursive(Desktop->Window, Desktop->Task);
            }
            return TRUE;
        }
    }

    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TaskInfo);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = DesktopDispatcherTask;
    TaskInfo.Parameter = Desktop;
    TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_HIGHEST;
    TaskInfo.Flags = 0;
    StringCopy(TaskInfo.Name, DESKTOP_DISPATCHER_TASK_NAME);

    DispatcherTask = KernelCreateTask(&KernelProcess, &TaskInfo);
    if (DispatcherTask == NULL) {
        WARNING(TEXT("Unable to create desktop dispatcher"));
        return FALSE;
    }

    if (EnsureAllMessageQueues(DispatcherTask, TRUE) == FALSE) {
        WARNING(TEXT("Unable to initialize dispatcher message queues"));
        return FALSE;
    }

    Desktop->Task = DispatcherTask;

    SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
        DesktopAssignWindowTaskRecursive(Desktop->Window, DispatcherTask);
    }


    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve the owner task for one desktop window.
 * @param Desktop Desktop owning the window.
 * @param FallbackTask Fallback task when no desktop dispatcher is available.
 * @return Task chosen for message delivery.
 */
LPTASK DesktopResolveWindowTask(LPDESKTOP Desktop, LPTASK FallbackTask) {
    SAFE_USE_VALID_ID(FallbackTask, KOID_TASK) {
        SAFE_USE_VALID_ID(FallbackTask->OwnerProcess, KOID_PROCESS) {
            if (FallbackTask->OwnerProcess->Privilege == CPU_PRIVILEGE_USER) {
                return FallbackTask;
            }
        }
    }

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        SAFE_USE_VALID_ID(Desktop->Task, KOID_TASK) {
            return Desktop->Task;
        }
    }

    return FallbackTask;
}
