
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


    Task manager

\************************************************************************/

#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#include "Base.h"
#include "process/Task-Stack.h"
#include "Arch.h"
#include "utils/List.h"
#include "sync/Mutex.h"
#include "User.h"
#include "utils/MessageQueue.h"

/************************************************************************/

#define TASK_TYPE_NONE 0
#define TASK_TYPE_KERNEL_MAIN 1
#define TASK_TYPE_KERNEL_OTHER 2
#define TASK_TYPE_USER_MAIN 3
#define TASK_TYPE_USER_OTHER 4

#define TASK_MESSAGE_QUEUE_MAX_MESSAGES 100
#define TASK_MUTEX_CLASS_STACK_MAX_DEPTH 32
#define TASK_USER_TLS_CONTROL_BLOCK_MAGIC 0x544C5343
#define TASK_USER_TLS_CONTROL_BLOCK_VERSION 1

/************************************************************************/
// Message queue

typedef struct tag_MESSAGEQUEUE {
    MUTEX Mutex;      // Queue mutex
    UINT Capacity;    // Optional max capacity (0 = unlimited)
    UINT Flags;       // Future flags
    BOOL Waiting;     // Indicates a waiter is sleeping on this queue
    MESSAGE_QUEUE_BUFFER MessageBuffer;  // Optional fixed-size message buffer
    LINEAR MessageBufferBase;            // Backing storage virtual base for task queue
    UINT MessageBufferSize;              // Backing storage size in bytes
} MESSAGEQUEUE, *LPMESSAGEQUEUE;

// Scheduler-owned task state

typedef struct tag_TASK_SCHEDULER_STATE {
    U32 Status;
    UINT WakeUpTime;
    BOOL Suspended;
} TASK_SCHEDULER_STATE, *LPTASK_SCHEDULER_STATE;

typedef struct tag_EXECUTABLE_MODULE_BINDING EXECUTABLE_MODULE_BINDING, *LPEXECUTABLE_MODULE_BINDING;

typedef struct tag_TASK_MODULE_TLS_BLOCK {
    LISTNODE_FIELDS
    LPEXECUTABLE_MODULE_BINDING Binding;
    LINEAR Base;
    UINT Size;
    UINT TemplateSize;
    UINT Alignment;
} TASK_MODULE_TLS_BLOCK, *LPTASK_MODULE_TLS_BLOCK;

typedef struct tag_TASK_USER_TLS_MODULE_ENTRY {
    U32 ModuleIdentifierHigh;
    U32 ModuleIdentifierLow;
    LINEAR Base;
    UINT Size;
    UINT TemplateSize;
    UINT Alignment;
} TASK_USER_TLS_MODULE_ENTRY, *LPTASK_USER_TLS_MODULE_ENTRY;

typedef struct tag_TASK_USER_TLS_CONTROL_BLOCK {
    U32 Magic;
    U32 Version;
    UINT Size;
    LINEAR Self;
    U32 ProcessIdentifierHigh;
    U32 ProcessIdentifierLow;
    U32 ThreadIdentifierHigh;
    U32 ThreadIdentifierLow;
    LINEAR ModuleTlsVector;
    UINT ModuleTlsVectorCount;
    UINT ModuleTlsVectorStride;
    UINT Reserved0;
    UINT Reserved1;
    UINT Reserved2;
    UINT Reserved3;
} TASK_USER_TLS_CONTROL_BLOCK, *LPTASK_USER_TLS_CONTROL_BLOCK;

/************************************************************************/
// The Task structure

struct tag_TASK {
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;          // This structure's mutex
    STR Name[MAX_USER_NAME];  // Task name for debugging
    U32 Type;                 // Type of task
    U32 Priority;             // Current priority of this task
    TASKFUNC Function;        // Start address of this task
    LPVOID Parameter;         // Parameter passed to the function
    UINT ExitCode;            // This task's exit code
    U32 Flags;                // Task creation flags
    ARCH_TASK_DATA Arch;      // Architecture-specific task data
    TASK_SCHEDULER_STATE SchedulerState;  // Scheduler-owned ISR-visible state
    LPMUTEX WaitingMutex;     // Mutex currently waited by this task
    UINT WaitingSince;        // Time at which the current mutex wait started
    U32 HeldMutexClassDepth;  // Number of held lock classes tracked for diagnostics
    U32 HeldMutexClasses[TASK_MUTEX_CLASS_STACK_MAX_DEPTH];  // Held lock class stack
    MESSAGEQUEUE MessageQueue;  // Message queue for this task
    LPVOID WindowDispatchWindow;          // Current window in nested window dispatch
    LPVOID WindowDispatchClass;           // Current class in nested window dispatch
    WINDOWFUNC WindowDispatchFunction;    // Current function in nested window dispatch
    U32 WindowDispatchDepth;              // Current nested window dispatch depth
    LPLIST ModuleTlsBlocks;               // Task-owned executable module TLS blocks
    UINT ModuleTlsBlockCount;             // Number of task-owned executable module TLS blocks
    LINEAR UserTlsAnchor;                 // User-visible thread control block base
    UINT UserTlsAnchorSize;               // User-visible thread control block mapping size
};

typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/

BOOL InitKernelTask(void);
LPTASK KernelCreateTask(LPPROCESS, LPTASK_INFO);
BOOL KernelKillTask(LPTASK Task);
BOOL SuspendTaskExecution(LPTASK Task);
BOOL ResumeTaskExecution(LPTASK Task);
BOOL SetTaskExitCode(LPTASK Task, UINT Code);
void DeleteDeadTasksAndProcesses(void);
U32 SetTaskPriority(LPTASK, U32);
void Sleep(U32);
U32 GetTaskStatus(LPTASK Task);
BOOL GetTaskSchedulerState(LPTASK Task, LPTASK_SCHEDULER_STATE State);
BOOL IsTaskExecutionSuspended(LPTASK Task);
void SetTaskStatus(LPTASK Task, U32 Status);
void SetTaskStatusDirect(LPTASK Task, U32 Status);
BOOL SetTaskSchedulerStatus(LPTASK Task, U32 Status);
void SetTaskWakeUpTime(LPTASK Task, UINT WakeupTime);
U32 ComputeTaskQuantumTime(U32 Priority);
BOOL TaskEnsureModuleTlsBlock(
    LPTASK Task,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment);
void TaskReleaseModuleTlsBlock(LPTASK Task, LPEXECUTABLE_MODULE_BINDING Binding);
void TaskReleaseModuleTlsBlocks(LPTASK Task);
void TaskReleaseProcessModuleTlsBlocks(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding);
BOOL TaskInstallProcessModuleTlsBlocks(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment);
BOOL InitializeTaskProcessModuleTlsBindings(LPPROCESS Process, LPTASK Task);
BOOL TaskSetUserTlsAnchor(LPTASK Task, LINEAR Anchor);
LINEAR TaskGetUserTlsAnchor(LPTASK Task);
BOOL TaskRefreshModuleTls(LPTASK Task);
void TaskReleaseUserTlsAnchor(LPTASK Task);
void DumpTask(LPTASK);

/************************************************************************/

#pragma pack(pop)

#include "process/Task-Messaging.h"

#endif  // TASK_H_INCLUDED
