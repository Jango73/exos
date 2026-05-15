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


    Task messaging

\************************************************************************/

#include "system/Clock.h"
#include "Arch.h"
#include "DisplaySession.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Heap.h"
#include "Desktop.h"
#include "../desktop/Desktop-Private.h"
#include "process/Process-Control.h"
#include "process/Process.h"
#include "process/Task-Messaging.h"
#include "utils/Helpers.h"

/************************************************************************/

/************************************************************************/

static BOOL AddTaskMessage(LPTASK Task, LPMESSAGE Message);
static BOOL CopyMessageFromQueueLocked(LPMESSAGEQUEUE Queue, LPMESSAGE_INFO Message, BOOL Remove);
static BOOL AddProcessMessage(LPPROCESS Process, LPMESSAGE Message);
static BOOL InterceptProcessControlMessage(LPPROCESS Process, U32 Message, U32 Param1, U32 Param2);
static BOOL FetchProcessMessage(LPPROCESS Process, LPMESSAGE_INFO Message, BOOL Remove);
static BOOL FetchTaskMessage(LPTASK Task, LPMESSAGE_INFO Message, BOOL Remove);
static BOOL FindTaskMessageOffset(
    LPMESSAGEQUEUE Queue,
    HANDLE Target,
    U32 Message,
    BOOL MatchParam1,
    U32 Param1,
    UINT* Offset);
static LPWINDOW_CLASS ResolveWindowDispatchClass(LPWINDOW Window, WINDOWFUNC Function);
static void PushWindowDispatchContext(
    LPTASK Task,
    LPWINDOW Window,
    LPWINDOW_CLASS WindowClass,
    WINDOWFUNC Function,
    LPVOID* PreviousWindow,
    LPVOID* PreviousClass,
    WINDOWFUNC* PreviousFunction);
static void PopWindowDispatchContext(
    LPTASK Task,
    LPVOID PreviousWindow,
    LPVOID PreviousClass,
    WINDOWFUNC PreviousFunction);
static BOOL ShouldSuppressDesktopDrawMessage(U32 Message);
static void EnterTaskMessageWaitLocked(LPTASK Task);
static void WakeTaskMessageWaitLocked(LPTASK Task);
U32 DesktopWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/************************************************************************/

/**
 * @brief Resolve whether one dispatched message must first flow through desktop root mouse handling.
 * @param Window Target window.
 * @param Message Message identifier.
 * @return TRUE when one userland desktop root must execute DesktopWindowFunc.
 */
static BOOL ShouldDispatchDesktopRootMouseMessage(LPWINDOW Window, U32 Message) {
    LPDESKTOP Desktop;
    LPWINDOW RootWindow = NULL;

    if (Message != EWM_MOUSEMOVE && Message != EWM_MOUSEDOWN && Message != EWM_MOUSEUP) {
        return FALSE;
    }
    if (Window == NULL || Window->TypeID != KOID_WINDOW) {
        return FALSE;
    }

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }
    if (DesktopGetRootWindow(Desktop, &RootWindow) == FALSE || RootWindow != Window) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Window->Task->OwnerProcess, KOID_PROCESS) {
            return Window->Task->OwnerProcess->Privilege == CPU_PRIVILEGE_USER;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Execute kernel desktop root mouse routing before userland root processing.
 * @param Window Target root window.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 */
static void DispatchDesktopRootMouseMessage(LPWINDOW Window, U32 Message, U32 Param1, U32 Param2) {
    if (ShouldDispatchDesktopRootMouseMessage(Window, Message) == FALSE) {
        return;
    }

    (void)DesktopWindowFunc((HANDLE)Window, Message, Param1, Param2);
}

/************************************************************************/

static LPWINDOW_CLASS ResolveWindowDispatchClass(LPWINDOW Window, WINDOWFUNC Function) {
    LPWINDOW_CLASS This;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;

    for (This = Window->Class; This != NULL; This = This->BaseClass) {
        if (This->Function == Function) return This;
    }

    return Window->Class;
}

/************************************************************************/

/**
 * @brief Determine whether one desktop draw message must be suppressed.
 * @param Message Message identifier.
 * @return TRUE when the desktop frontend is inactive and draw must be skipped.
 */
static BOOL ShouldSuppressDesktopDrawMessage(U32 Message) {
    if (Message != EWM_DRAW) {
        return FALSE;
    }

    return DisplaySessionGetActiveFrontEnd() != DISPLAY_FRONTEND_DESKTOP;
}

/************************************************************************/

static void PushWindowDispatchContext(
    LPTASK Task,
    LPWINDOW Window,
    LPWINDOW_CLASS WindowClass,
    WINDOWFUNC Function,
    LPVOID* PreviousWindow,
    LPVOID* PreviousClass,
    WINDOWFUNC* PreviousFunction) {
    if (Task == NULL || Task->TypeID != KOID_TASK) return;
    if (PreviousWindow == NULL || PreviousClass == NULL || PreviousFunction == NULL) return;

    *PreviousWindow = Task->WindowDispatchWindow;
    *PreviousClass = Task->WindowDispatchClass;
    *PreviousFunction = Task->WindowDispatchFunction;

    Task->WindowDispatchWindow = Window;
    Task->WindowDispatchClass = WindowClass;
    Task->WindowDispatchFunction = Function;
    Task->WindowDispatchDepth++;
}

/************************************************************************/

static void PopWindowDispatchContext(
    LPTASK Task,
    LPVOID PreviousWindow,
    LPVOID PreviousClass,
    WINDOWFUNC PreviousFunction) {
    if (Task == NULL || Task->TypeID != KOID_TASK) return;

    Task->WindowDispatchWindow = PreviousWindow;
    Task->WindowDispatchClass = PreviousClass;
    Task->WindowDispatchFunction = PreviousFunction;

    if (Task->WindowDispatchDepth > 0) {
        Task->WindowDispatchDepth--;
    }
}

/************************************************************************/

/**
 * @brief Switch one task to message-wait state while holding its queue locks.
 * @param Task Target task. Caller must hold `Task->Mutex`, `Task->MessageQueue.Mutex`
 *             and the scheduler freeze.
 */
static void EnterTaskMessageWaitLocked(LPTASK Task) {
    if (Task == NULL || Task->TypeID != KOID_TASK) {
        return;
    }

    Task->MessageQueue.Waiting = TRUE;
    SetTaskStatusDirect(Task, TASK_STATUS_WAITMESSAGE);
    SetTaskWakeUpTimeDirect(Task, MAX_U16);
}

/************************************************************************/

/**
 * @brief Wake one task from message-wait state while holding its queue locks.
 * @param Task Target task. Caller must hold `Task->Mutex`, `Task->MessageQueue.Mutex`
 *             and the scheduler freeze.
 */
static void WakeTaskMessageWaitLocked(LPTASK Task) {
    if (Task == NULL || Task->TypeID != KOID_TASK) {
        return;
    }

    Task->MessageQueue.Waiting = FALSE;
    SetTaskStatusDirect(Task, TASK_STATUS_RUNNING);
}

/**
 * @brief Initializes a message queue structure.
 *
 * Sets default flags, initializes the mutex, and allocates the underlying
 * message list using the message destructor. The queue is ready for use
 * after successful initialization.
 *
 * @param Queue Pointer to the queue to initialize
 * @return TRUE on success, FALSE on invalid pointer or allocation failure
 */
BOOL InitMessageQueue(LPMESSAGEQUEUE Queue) {
    if (Queue == NULL) return FALSE;

    InitMutexWithDebugInfo(&(Queue->Mutex), MUTEX_CLASS_PROCESS_MESSAGE_QUEUE, TEXT("ProcessMessageQueue"));
    Queue->Capacity = 0;
    Queue->Flags = 0;
    Queue->Waiting = FALSE;
    Queue->MessageBuffer.Entries = NULL;
    Queue->MessageBuffer.Capacity = 0;
    Queue->MessageBuffer.Head = 0;
    Queue->MessageBuffer.Count = 0;
    Queue->MessageBufferBase = 0;
    Queue->MessageBufferSize = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Destroys a message queue and resets its fields.
 *
 * Frees the underlying message list and clears bookkeeping fields. The
 * function ignores NULL queues and NULL internal lists.
 *
 * @param Queue Pointer to the queue to destroy
 */
void DeleteMessageQueue(LPMESSAGEQUEUE Queue) {
    SAFE_USE(Queue) {
        MessageQueueBufferReset(&(Queue->MessageBuffer));
        Queue->MessageBuffer.Entries = NULL;
        Queue->MessageBuffer.Capacity = 0;
        Queue->MessageBufferBase = 0;
        Queue->MessageBufferSize = 0;
        Queue->Capacity = 0;
        Queue->Flags = 0;
        Queue->Waiting = FALSE;
    }
}

/************************************************************************/

BOOL EnsureTaskMessageQueue(LPTASK Task, BOOL CreateIfMissing) {
    UNUSED(CreateIfMissing);

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (Task->MessageQueue.MessageBuffer.Entries == NULL ||
            Task->MessageQueue.MessageBuffer.Capacity == 0) {
            ERROR(TEXT("Task %p has no message buffer"), Task);
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL EnsureProcessMessageQueue(LPPROCESS Process, BOOL CreateIfMissing) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->MessageQueue.MessageBuffer.Entries == NULL ||
            Process->MessageQueue.MessageBuffer.Capacity == 0) {
            UINT MessageBufferSize;
            UINT MessageQueueCapacity;
            LINEAR MessageBufferBase;

            if (CreateIfMissing == FALSE) {
                return FALSE;
            }

            MessageQueueCapacity = GetConfigurationUInt(TEXT(CONFIG_TASK_MESSAGE_QUEUE_MAX_MESSAGES),
                                                       TASK_MESSAGE_QUEUE_MAX_MESSAGES,
                                                       1,
                                                       MAX_U32 / sizeof(MESSAGE));
            MessageBufferSize = MessageQueueCapacity * sizeof(MESSAGE);
            MessageBufferBase = (LINEAR)KernelHeapAlloc(MessageBufferSize);
            if (MessageBufferBase == 0) {
                ERROR(TEXT("[EnsureProcessMessageQueue] Failed to allocate queue for process %p"), Process);
                return FALSE;
            }

            InitMutexWithDebugInfo(&(Process->MessageQueue.Mutex), MUTEX_CLASS_PROCESS_MESSAGE_QUEUE, TEXT("ProcessMessageQueue"));
            Process->MessageQueue.MessageBufferBase = MessageBufferBase;
            Process->MessageQueue.MessageBufferSize = MessageBufferSize;
            Process->MessageQueue.Waiting = FALSE;
            Process->MessageQueue.Capacity = MessageQueueCapacity;
            Process->MessageQueue.Flags = 0;
            MessageQueueBufferInitialize(&(Process->MessageQueue.MessageBuffer),
                                         (LPMESSAGE)MessageBufferBase,
                                         MessageQueueCapacity);
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL FetchProcessMessage(LPPROCESS Process, LPMESSAGE_INFO Message, BOOL Remove) {
    if (EnsureProcessMessageQueue(Process, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    BOOL Result = CopyMessageFromQueueLocked(&(Process->MessageQueue), Message, Remove);

    UnlockMutex(&(Process->MessageQueue.Mutex));

    return Result;
}

/************************************************************************/

static BOOL FetchTaskMessage(LPTASK Task, LPMESSAGE_INFO Message, BOOL Remove) {
    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageQueue.Mutex), INFINITY);

    BOOL Result = CopyMessageFromQueueLocked(&(Task->MessageQueue), Message, Remove);

    UnlockMutex(&(Task->MessageQueue.Mutex));
    UnlockMutex(&(Task->Mutex));

    return Result;
}

/************************************************************************/

/**
 * @brief Peek the next message from the current task's queue without removing it.
 *
 * Non-blocking; returns FALSE if no message is available or parameters are invalid.
 *
 * @param Message Pointer to message info structure to fill
 * @return TRUE if a message was found, FALSE otherwise
 */
BOOL PeekMessage(LPMESSAGE_INFO Message) {
    LPTASK Task;
    LPPROCESS TaskProcessPtr = NULL;
    LPPROCESS Process = NULL;

    if (Message == NULL) return FALSE;

    Task = GetCurrentTask();
    SAFE_USE_VALID_ID(Task, KOID_TASK) { TaskProcessPtr = Task->OwnerProcess; }
    DEBUG(TEXT("Task=%p Process=%p FocusedProcess=%p"), Task, TaskProcessPtr, GetFocusedProcess());

    Process = TaskProcessPtr;

    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) return FALSE;

    if (FetchProcessMessage(Process, Message, FALSE) == TRUE) {
        return TRUE;
    }

    return FetchTaskMessage(Task, Message, FALSE);
}

/************************************************************************/

/**
 * @brief Copy a message from a locked queue, optionally removing it.
 * @param Queue Message queue (caller must lock Queue->Mutex).
 * @param Message Target structure to fill.
 * @param Remove TRUE to erase the message from the queue.
 * @return TRUE if a message was found.
 */
static BOOL CopyMessageFromQueueLocked(LPMESSAGEQUEUE Queue, LPMESSAGE_INFO Message, BOOL Remove) {
    MESSAGE Current;

    if (Queue == NULL || Message == NULL) {
        return FALSE;
    }

    if (MessageQueueBufferGetCount(&(Queue->MessageBuffer)) == 0) {
        return FALSE;
    }

    if (Message->Target == NULL) {
        if (Remove) {
            if (MessageQueueBufferPop(&(Queue->MessageBuffer), &Current) == FALSE) {
                return FALSE;
            }
        } else {
            if (MessageQueueBufferPeek(&(Queue->MessageBuffer), &Current) == FALSE) {
                return FALSE;
            }
        }
    } else {
        UINT Offset = 0;
        BOOL Found = FALSE;

        for (Offset = 0; Offset < MessageQueueBufferGetCount(&(Queue->MessageBuffer)); Offset++) {
            if (MessageQueueBufferReadAt(&(Queue->MessageBuffer), Offset, &Current) == FALSE) {
                return FALSE;
            }

            if (Current.Target == Message->Target) {
                Found = TRUE;
                break;
            }
        }

        if (Found == FALSE) {
            return FALSE;
        }

        if (Remove) {
            if (MessageQueueBufferRemoveAt(&(Queue->MessageBuffer), Offset, &Current) == FALSE) {
                return FALSE;
            }
        }
    }

    Message->Target = Current.Target;
    Message->Time = Current.Time;
    Message->Message = Current.Message;
    Message->Param1 = Current.Param1;
    Message->Param2 = Current.Param2;

    return TRUE;
}

/************************************************************************/

static BOOL FindTaskMessageOffset(
    LPMESSAGEQUEUE Queue,
    HANDLE Target,
    U32 Message,
    BOOL MatchParam1,
    U32 Param1,
    UINT* Offset) {
    UINT Index = 0;
    MESSAGE Current;

    if (Queue == NULL || Offset == NULL) {
        return FALSE;
    }

    for (Index = 0; Index < MessageQueueBufferGetCount(&(Queue->MessageBuffer)); Index++) {
        if (MessageQueueBufferReadAt(&(Queue->MessageBuffer), Index, &Current) == FALSE) {
            return FALSE;
        }

        if (Current.Target != Target || Current.Message != Message) {
            continue;
        }

        if (MatchParam1 != FALSE && Current.Param1 != Param1) {
            continue;
        }

        *Offset = Index;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Adds a message to a task's message queue in a thread-safe manner.
 *
 * Adds the specified message to the task's message queue. This function
 * locks both the task's mutex and message mutex to ensure thread safety.
 * The message will be processed when the task calls GetMessage().
 *
 * @param Task Pointer to the target task
 * @param Message Pointer to the message to add to the queue
 *
 * @note This function acquires task and message mutexes
 */
static BOOL AddTaskMessage(LPTASK Task, LPMESSAGE Message) {
    if (Task == NULL || Task->TypeID != KOID_TASK || Message == NULL) {
        return FALSE;
    }

    if (InterceptProcessControlMessage(Task->OwnerProcess, Message->Message, Message->Param1, Message->Param2)) {
        return TRUE;
    }

    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageQueue.Mutex), INFINITY);

    if (MessageQueueBufferGetCount(&(Task->MessageQueue.MessageBuffer)) >= Task->MessageQueue.Capacity) {
        WARNING(TEXT("Queue full for task %p, dropping message %u"), Task, Message->Message);
        UnlockMutex(&(Task->MessageQueue.Mutex));
        UnlockMutex(&(Task->Mutex));
        return FALSE;
    }

    if (MessageQueueBufferPush(&(Task->MessageQueue.MessageBuffer), Message) == FALSE) {
        WARNING(TEXT("Could not enqueue message %u for task %p"), Message->Message, Task);
        UnlockMutex(&(Task->MessageQueue.Mutex));
        UnlockMutex(&(Task->Mutex));
        return FALSE;
    }

    if (Task->MessageQueue.Waiting != FALSE && Task->SchedulerState.Status == TASK_STATUS_WAITMESSAGE) {
        FreezeScheduler();
        WakeTaskMessageWaitLocked(Task);
        UnlockMutex(&(Task->MessageQueue.Mutex));
        UnlockMutex(&(Task->Mutex));
        UnfreezeScheduler();
        return TRUE;
    }

    UnlockMutex(&(Task->MessageQueue.Mutex));
    UnlockMutex(&(Task->Mutex));

    return TRUE;
}

/************************************************************************/

static BOOL AddProcessMessage(LPPROCESS Process, LPMESSAGE Message) {
    if (Process == NULL || Process->TypeID != KOID_PROCESS || Message == NULL) {
        return FALSE;
    }

    if (InterceptProcessControlMessage(Process, Message->Message, Message->Param1, Message->Param2)) {
        return TRUE;
    }

    if (EnsureProcessMessageQueue(Process, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Process->Mutex), INFINITY);
    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    if (MessageQueueBufferGetCount(&(Process->MessageQueue.MessageBuffer)) >= Process->MessageQueue.Capacity) {
        WARNING(TEXT("Queue full for process %p, dropping message %u"), Process, Message->Message);
        UnlockMutex(&(Process->MessageQueue.Mutex));
        UnlockMutex(&(Process->Mutex));
        return FALSE;
    }

    if (MessageQueueBufferPush(&(Process->MessageQueue.MessageBuffer), Message) == FALSE) {
        WARNING(TEXT("Could not enqueue message %u for process %p"), Message->Message, Process);
        UnlockMutex(&(Process->MessageQueue.Mutex));
        UnlockMutex(&(Process->Mutex));
        return FALSE;
    }

    UnlockMutex(&(Process->MessageQueue.Mutex));
    UnlockMutex(&(Process->Mutex));

    LockMutex(MUTEX_TASK, INFINITY);
    LPLIST TaskList = GetTaskList();
    for (LPLISTNODE Node = TaskList != NULL ? TaskList->First : NULL; Node; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Task->OwnerProcess == Process && GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
                SetTaskStatus(Task, TASK_STATUS_RUNNING);
            }
        }
    }
    UnlockMutex(MUTEX_TASK);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue an input message.
 *
 * Routes keyboard/mouse events to the focused window's task queue when a focused window exists,
 * otherwise to the focused process' message queue (created on-demand for the kernel process or
 * when the process has explicitly initialized its queue via PeekMessage/GetMessage).
 * If no suitable queue exists, the message is dropped.
 *
 * @param Msg Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return TRUE on success, FALSE if no queue is available.
 */
BOOL EnqueueInputMessage(U32 Msg, U32 Param1, U32 Param2) {
    LPDESKTOP Desktop = GetActiveDesktop();
    LPPROCESS Process = GetFocusedProcess();
    LPWINDOW FocusedWindow = NULL;
    BOOL IsMouseMessage;
    LPTASK TargetTask = NULL;

    IsMouseMessage = (Msg == EWM_MOUSEMOVE || Msg == EWM_MOUSEDOWN || Msg == EWM_MOUSEUP);

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        (void)DesktopGetFocusWindow(Desktop, &FocusedWindow);
    }

    // Mouse input must always target the desktop window task so hit testing,
    // capture, and non-client interactions keep using one dispatcher path.
    if (IsMouseMessage != FALSE) {
        SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
            SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
                SAFE_USE_VALID_ID(Desktop->Window->Task, KOID_TASK) {
                    if (Desktop->Window->Task->OwnerProcess == Process) {
                        FocusedWindow = Desktop->Window;
                        TargetTask = Desktop->Window->Task;
                    }
                }
            }
        }
    } else {
        // Only route keyboard-like input to a focused window if it belongs to the focused process.
        SAFE_USE_VALID_ID(FocusedWindow, KOID_WINDOW) {
            SAFE_USE_VALID_ID(FocusedWindow->Task, KOID_TASK) {
                if (FocusedWindow->Task->OwnerProcess == Process) {
                    TargetTask = FocusedWindow->Task;
                }
            }
        }
    }

    MESSAGE TaskMessage;
    MemorySet(&TaskMessage, 0, sizeof(MESSAGE));
    GetLocalTime(&(TaskMessage.Time));
    TaskMessage.Target = (TargetTask != NULL && FocusedWindow != NULL) ? (HANDLE)FocusedWindow : NULL;
    TaskMessage.Message = Msg;
    TaskMessage.Param1 = Param1;
    TaskMessage.Param2 = Param2;

    if (TargetTask != NULL) {
        if (AddTaskMessage(TargetTask, &TaskMessage) == TRUE) {
            return TRUE;
        }
    }

    MESSAGE ProcessMessage;
    MemorySet(&ProcessMessage, 0, sizeof(MESSAGE));
    GetLocalTime(&(ProcessMessage.Time));
    ProcessMessage.Target = NULL;
    ProcessMessage.Message = Msg;
    ProcessMessage.Param1 = Param1;
    ProcessMessage.Param2 = Param2;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (EnsureProcessMessageQueue(Process, FALSE) == TRUE) {
            return AddProcessMessage(Process, &ProcessMessage);
        }
    }

    return FALSE;
}

/************************************************************************/

static BOOL InterceptProcessControlMessage(LPPROCESS Process, U32 Message, U32 Param1, U32 Param2) {
    if (Process == NULL || Process->TypeID != KOID_PROCESS) {
        return FALSE;
    }

    return ProcessControlHandleMessage(Process, Message, Param1, Param2);
}

/************************************************************************/

BOOL PostProcessMessage(LPPROCESS Process, U32 Msg, U32 Param1, U32 Param2) {
    MESSAGE Message;
    MemorySet(&Message, 0, sizeof(MESSAGE));
    GetLocalTime(&(Message.Time));
    Message.Target = NULL;
    Message.Message = Msg;
    Message.Param1 = Param1;
    Message.Param2 = Param2;

    return AddProcessMessage(Process, &Message);
}

/************************************************************************/

/**
 * @brief Broadcast a message to all user processes with message queues.
 * @param Msg Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return TRUE when at least one message was posted.
 */
BOOL BroadcastProcessMessage(U32 Msg, U32 Param1, U32 Param2) {
    BOOL Sent = FALSE;
    LPLIST ProcessList = GetProcessList();

    if (ProcessList == NULL) return FALSE;

    LockMutex(MUTEX_TASK, INFINITY);

    for (LPLISTNODE Node = ProcessList->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;

        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            if (Process == &KernelProcess) {
                continue;
            }

            if (EnsureProcessMessageQueue(Process, FALSE) == FALSE) {
                continue;
            }
            MESSAGE Message;
            MemorySet(&Message, 0, sizeof(MESSAGE));
            GetLocalTime(&(Message.Time));
            Message.Target = NULL;
            Message.Message = Msg;
            Message.Param1 = Param1;
            Message.Param2 = Param2;

            if (AddProcessMessage(Process, &Message) == TRUE) {
                Sent = TRUE;
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    return Sent;
}

/************************************************************************/

/**
 * @brief Posts a message asynchronously to a task or window.
 *
 * Sends a message to the specified target without waiting for completion.
 * The target can be a task handle or window handle. For windows, the message
 * is queued to the window's owning task. If the target task is waiting for
 * messages (TASK_STATUS_WAITMESSAGE), it will be awakened.
 *
 * @param Target Handle to the target task or window
 * @param Msg Message identifier
 * @param Param1 First message parameter
 * @param Param2 Second message parameter
 * @return TRUE if message was posted successfully, FALSE on error
 *
 * @note For EWM_DRAW, duplicate messages are consolidated per target window
 * @note For EWM_NOTIFY, duplicate notifications are consolidated per target window and notification id
 * @note Structural locks are held only for target resolution, never across queue operations
 */
BOOL PostMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2) {
    LPTASK Task = NULL;
    LPWINDOW Window = NULL;
    LPLISTNODE Node;
    LPDESKTOP Desktop = NULL;
    HANDLE MessageTarget = Target;

    if (Target == NULL) {
        Task = GetCurrentTask();
    } else {
        LPLIST TaskList;

        LockMutex(MUTEX_TASK, INFINITY);
        TaskList = GetTaskList();
        for (Node = TaskList != NULL ? TaskList->First : NULL; Node; Node = Node->Next) {
            if ((HANDLE)Node == Target) {
                Task = (LPTASK)Node;
                break;
            }
        }
        UnlockMutex(MUTEX_TASK);

        if (Task == NULL) {
            Window = (LPWINDOW)Target;
            SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
                Task = Window->Task;
                MessageTarget = (HANDLE)Window;
            }

            if (Task == NULL) {
                SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
                    SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                        SAFE_USE_VALID_ID(Window->Task->OwnerProcess, KOID_PROCESS) {
                            Desktop = Window->Task->OwnerProcess->Desktop;
                        }
                    }
                }

                SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
                    (void)DesktopResolveWindowTarget(Desktop, Target, &Window);
                } else {
                    Window = NULL;
                }

                SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
                    Task = Window->Task;
                    MessageTarget = (HANDLE)Window;
                }
            }
        }
    }

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        MESSAGE TaskMessage;

        if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) {
            return FALSE;
        }

        if ((Msg == EWM_DRAW || Msg == EWM_NOTIFY) && Window != NULL) {
            UINT ExistingOffset = 0;
            MESSAGE Existing;
            BOOL HasExisting = FALSE;

            LockMutex(&(Task->Mutex), INFINITY);
            LockMutex(&(Task->MessageQueue.Mutex), INFINITY);

            if (Msg == EWM_DRAW) {
                HasExisting = FindTaskMessageOffset(
                    &(Task->MessageQueue),
                    (HANDLE)Window,
                    Msg,
                    FALSE,
                    0,
                    &ExistingOffset);
            } else {
                HasExisting = FindTaskMessageOffset(
                    &(Task->MessageQueue),
                    (HANDLE)Window,
                    Msg,
                    TRUE,
                    Param1,
                    &ExistingOffset);
            }

            if (HasExisting != FALSE &&
                MessageQueueBufferRemoveAt(&(Task->MessageQueue.MessageBuffer), ExistingOffset, &Existing) == TRUE) {
                GetLocalTime(&(Existing.Time));
                Existing.Param1 = Param1;
                Existing.Param2 = Param2;
                (void)MessageQueueBufferPush(&(Task->MessageQueue.MessageBuffer), &Existing);

                UnlockMutex(&(Task->MessageQueue.Mutex));
                UnlockMutex(&(Task->Mutex));

                if (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
                    SetTaskStatus(Task, TASK_STATUS_RUNNING);
                }

                return TRUE;
            }

            UnlockMutex(&(Task->MessageQueue.Mutex));
            UnlockMutex(&(Task->Mutex));
        }

        MemorySet(&TaskMessage, 0, sizeof(MESSAGE));
        GetLocalTime(&(TaskMessage.Time));
        TaskMessage.Target = MessageTarget;
        TaskMessage.Message = Msg;
        TaskMessage.Param1 = Param1;
        TaskMessage.Param2 = Param2;

        if (AddTaskMessage(Task, &TaskMessage) == FALSE) {
            return FALSE;
        }

        if (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
            SetTaskStatus(Task, TASK_STATUS_RUNNING);
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Sends a message synchronously to a window and waits for the result.
 *
 * Directly calls the window's message handler function and returns the result.
 * Unlike PostMessage(), this function waits for the window to process the
 * message before returning. Only works with window targets, not task targets.
 *
 * @param Target Handle to the target window
 * @param Msg Message identifier
 * @param Param1 First message parameter
 * @param Param2 Second message parameter
 * @return Result value returned by the window's message handler, or 0 on error
 *
 * @note Target resolution and dispatch state transitions are delegated to desktop owner APIs
 * @note The target must be a valid window in the current process's desktop
 */
U32 SendMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2) {
    LPDESKTOP Desktop = NULL;
    LPWINDOW Window = NULL;
    U32 Result = 0;

    //-------------------------------------
    // Check if the target is a window

    Desktop = GetCurrentProcess()->Desktop;

    if (Desktop == NULL) return 0;
    if (Desktop->TypeID != KOID_DESKTOP) return 0;

    (void)DesktopResolveWindowTarget(Desktop, Target, &Window);
    //-------------------------------------
    // Send message to window if found

    if (Window != NULL && Window->TypeID == KOID_WINDOW) {
        SAFE_USE(Window->Function) {
            LPVOID PreviousWindow = NULL;
            LPVOID PreviousClass = NULL;
            WINDOWFUNC PreviousFunction = NULL;
            LPWINDOW_CLASS DispatchClass = ResolveWindowDispatchClass(Window, Window->Function);

            SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                PushWindowDispatchContext(
                    Window->Task,
                    Window,
                    DispatchClass,
                    Window->Function,
                    &PreviousWindow,
                    &PreviousClass,
                    &PreviousFunction);
            }

            (void)DesktopMarkWindowDispatchBegin(Window, Msg);
            if (ShouldSuppressDesktopDrawMessage(Msg) != FALSE) {
                Result = TRUE;
            } else if (Msg == EWM_DRAW) {
                Result = DesktopDispatchWindowDraw(Window, Target, Param1, Param2) != FALSE;
            } else {
                DispatchDesktopRootMouseMessage(Window, Msg, Param1, Param2);
                Result = Window->Function(Target, Msg, Param1, Param2);
            }
            (void)DesktopMarkWindowDispatchEnd(Window, Msg);

            SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                PopWindowDispatchContext(Window->Task, PreviousWindow, PreviousClass, PreviousFunction);
            }
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Blocks the specified task until a message arrives in its queue.
 *
 * Sets the task status to TASK_STATUS_WAITMESSAGE and yields CPU cycles
 * until another thread posts a message to the task's queue. The task will
 * remain blocked until PostMessage() or another message-sending function
 * changes its status back to TASK_STATUS_RUNNING.
 *
 * @param Task Pointer to the task that should wait for messages
 *
 * @note This function freezes the scheduler temporarily during status change
 * @note Uses IdleCPU() to yield processor time while waiting
 */
void WaitForMessage(LPTASK Task) {
    //-------------------------------------
    // Change the task's status

    if (EnsureTaskMessageQueue(Task, TRUE) == TRUE) {
        LockMutex(&(Task->Mutex), INFINITY);
        LockMutex(&(Task->MessageQueue.Mutex), INFINITY);
        FreezeScheduler();

        EnterTaskMessageWaitLocked(Task);

        UnlockMutex(&(Task->MessageQueue.Mutex));
        UnlockMutex(&(Task->Mutex));
        UnfreezeScheduler();
    } else {
        SetTaskStatus(Task, TASK_STATUS_WAITMESSAGE);
        SetTaskWakeUpTime(Task, MAX_U16);
    }

    //-------------------------------------
    // The following loop is to make sure that
    // the task will not return immediately.
    // During the loop, the task does not get any
    // CPU cycles.

    while (Task != NULL && Task->TypeID == KOID_TASK && Task->SchedulerState.Status == TASK_STATUS_WAITMESSAGE) {
        SAFE_USE_VALID_ID(Task->OwnerProcess, KOID_PROCESS) {
            if (EnsureProcessMessageQueue(Task->OwnerProcess, TRUE) == TRUE) {
                LockMutex(&(Task->OwnerProcess->MessageQueue.Mutex), INFINITY);

                if (MessageQueueBufferGetCount(&(Task->OwnerProcess->MessageQueue.MessageBuffer)) > 0) {
                    UnlockMutex(&(Task->OwnerProcess->MessageQueue.Mutex));
                    SetTaskStatus(Task, TASK_STATUS_RUNNING);
                    break;
                }

                UnlockMutex(&(Task->OwnerProcess->MessageQueue.Mutex));
            }
        }

        IdleCPU();
    }

    if (EnsureTaskMessageQueue(Task, TRUE) == TRUE) {
        Task->MessageQueue.Waiting = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Retrieves the next message from the current task's message queue.
 *
 * If no messages are available, the task will wait until a message arrives.
 * Messages can be filtered by target or retrieved in FIFO order.
 *
 * @param Message Pointer to message info structure to fill
 * @return TRUE if message retrieved successfully, FALSE on ETM_QUIT or error
 */
BOOL GetMessage(LPMESSAGE_INFO Message) {
    LPTASK Task;
    LPPROCESS TaskProcessPtr = NULL;
    LPPROCESS Process = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;

    Task = GetCurrentTask();
    SAFE_USE_VALID_ID(Task, KOID_TASK) { TaskProcessPtr = Task->OwnerProcess; }

    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) return FALSE;
    Process = TaskProcessPtr;

    FOREVER {
        if (FetchProcessMessage(Process, Message, TRUE) == TRUE) {
            return Message->Message != ETM_QUIT;
        }

        if (FetchTaskMessage(Task, Message, TRUE) == TRUE) {
            return Message->Message != ETM_QUIT;
        }

        WaitForMessage(Task);
    }
}

/************************************************************************/

/**
 * @brief Dispatches a message to its target window within the current desktop.
 *
 * Routes a message to the appropriate window in the current process's desktop.
 * The function validates the current process and desktop, then uses
 * DispatchMessageToWindow() to find and deliver the message to the correct
 * window target.
 *
 * @param Message Pointer to the message information structure containing
 *                target handle and message parameters
 * @return TRUE if message was dispatched successfully, FALSE on error
 *
 * @note Resolves and dispatches through desktop owner APIs without holding window mutex across callback
 * @note Only works within the context of the current process's desktop
 */
BOOL DispatchMessage(LPMESSAGE_INFO Message) {
    LPPROCESS Process = NULL;
    LPDESKTOP Desktop = NULL;
    LPWINDOW Window = NULL;
    HANDLE TargetHandle;
    BOOL Result = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;
    if (Message->Target == NULL) return FALSE;

    Process = GetCurrentProcess();
    if (Process == NULL) return FALSE;
    if (Process->TypeID != KOID_PROCESS) return FALSE;

    Desktop = Process->Desktop;

    if (Process->Privilege == CPU_PRIVILEGE_KERNEL) {
        Window = (LPWINDOW)Message->Target;
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) {}
        else {
            Window = NULL;
        }
    } else {
        if (Desktop == NULL) return FALSE;
        if (Desktop->TypeID != KOID_DESKTOP) return FALSE;

        (void)DesktopResolveWindowTarget(Desktop, Message->Target, &Window);
    }

    SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
        SAFE_USE(Window->Function) {
            TargetHandle = EnsureHandle((LINEAR)Window);
            LPVOID PreviousWindow = NULL;
            LPVOID PreviousClass = NULL;
            WINDOWFUNC PreviousFunction = NULL;
            LPWINDOW_CLASS DispatchClass = ResolveWindowDispatchClass(Window, Window->Function);

            SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                SAFE_USE_VALID_ID(Window->Task->OwnerProcess, KOID_PROCESS) {
                    if (Window->Task->OwnerProcess->Privilege == CPU_PRIVILEGE_KERNEL) {
                        TargetHandle = (HANDLE)Window;
                    }
                }
            }

            SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                PushWindowDispatchContext(
                    Window->Task,
                    Window,
                    DispatchClass,
                    Window->Function,
                    &PreviousWindow,
                    &PreviousClass,
                    &PreviousFunction);
            }

            (void)DesktopMarkWindowDispatchBegin(Window, Message->Message);
            if (ShouldSuppressDesktopDrawMessage(Message->Message) != FALSE) {
            } else if (Message->Message == EWM_DRAW) {
                (void)DesktopDispatchWindowDraw(Window, TargetHandle, Message->Param1, Message->Param2);
            } else {
                DispatchDesktopRootMouseMessage(Window, Message->Message, Message->Param1, Message->Param2);
                Window->Function(TargetHandle, Message->Message, Message->Param1, Message->Param2);
            }
            (void)DesktopMarkWindowDispatchEnd(Window, Message->Message);

            SAFE_USE_VALID_ID(Window->Task, KOID_TASK) {
                PopWindowDispatchContext(Window->Task, PreviousWindow, PreviousClass, PreviousFunction);
            }
            Result = TRUE;
        }
    }

    return Result;
}
/**
 * @brief Ensure both task and process message queues exist.
 * @param Task Task whose queues must be initialized.
 * @param CreateIfMissing TRUE to create queues when absent.
 * @return TRUE if both queues are ready or created, FALSE otherwise.
 */
BOOL EnsureAllMessageQueues(LPTASK Task, BOOL CreateIfMissing) {
    if (EnsureTaskMessageQueue(Task, CreateIfMissing) == FALSE) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        return EnsureProcessMessageQueue(Task->OwnerProcess, CreateIfMissing);
    }

    return FALSE;
}

/************************************************************************/
