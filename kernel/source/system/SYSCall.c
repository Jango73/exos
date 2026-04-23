
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


    System call

\************************************************************************/

#include "Base.h"
#include "system/Clock.h"
#include "console/Console.h"
#include "GFX.h"
#include "fs/File.h"
#include "memory/Heap.h"
#include "utils/Helpers.h"
#include "core/ID.h"
#include "core/Kernel.h"
#include "drivers/input/Keyboard.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "input/Mouse.h"
#include "Desktop.h"
#include "desktop/Desktop-NonClient.h"
#include "desktop/Desktop-WindowClass.h"
#include "log/Profile.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "user/Account.h"
#include "user/UserSession.h"
#include "core/Security.h"
#include "network/Socket.h"
#include "system/SYSCall.h"
#include "utils/ProcessAccess.h"

extern BOOL ReleaseWindowGC(HANDLE Handle);

/************************************************************************/

static LPWINDOW_CLASS SysCallResolveAccessibleWindowClass(HANDLE WindowClassHandle, LPCSTR WindowClassName) {
    LPWINDOW_CLASS WindowClass = NULL;

    if (WindowClassHandle != 0) {
        WindowClass = WindowClassFindByHandle((U32)WindowClassHandle);
    } else if (WindowClassName != NULL) {
        WindowClass = WindowClassFindByName(WindowClassName);
    }

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(WindowClass, KOID_WINDOW_CLASS, TRUE) {
        return WindowClass;
    }

    return NULL;
}

/**
 * @brief Emit a debug string originating from user space.
 *
 * Validates the provided linear address before forwarding the message to
 * the kernel logger through DEBUG().
 *
 * @param Parameter Linear address of a null-terminated string.
 * @return UINT Always returns 0.
 */
UINT SysCall_Debug(UINT Parameter) {
    SAFE_USE_VALID((LPCSTR)Parameter) { DEBUG((LPCSTR)Parameter); }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the kernel major version encoded as 16.16 fixed point.
 *
 * @param Parameter Reserved.
 * @return UINT Version number where major is stored in the high word.
 */
UINT SysCall_GetVersion(UINT Parameter) {
    UNUSED(Parameter);
    return ((UINT)1 << 16);
}

/************************************************************************/

/**
 * @brief Collect global system information for the caller.
 *
 * Ensures the SYSTEM_INFO buffer is accessible, populates it, and keeps
 * handles untouched because the structure only contains plain data.
 *
 * @param Parameter Pointer to a SYSTEM_INFO structure.
 * @return UINT TRUE on success, FALSE when the buffer is invalid.
 */
UINT SysCall_GetSystemInfo(UINT Parameter) {
    LPSYSTEM_INFO Info = (LPSYSTEM_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SYSTEM_INFO) {
        Info->TotalPhysicalMemory = U64_FromUINT(KernelStartup.MemorySize);
        Info->PhysicalMemoryUsed = U64_FromUINT(GetPhysicalMemoryUsed());
        Info->PhysicalMemoryAvail = U64_Sub(Info->TotalPhysicalMemory, Info->PhysicalMemoryUsed);
        Info->TotalSwapMemory = U64_FromUINT(0);
        Info->SwapMemoryUsed = U64_FromUINT(0);
        Info->SwapMemoryAvail = U64_FromUINT(0);
        Info->TotalMemoryUsed = U64_Add(Info->PhysicalMemoryUsed, Info->SwapMemoryUsed);
        Info->TotalMemoryAvail = U64_Add(Info->PhysicalMemoryAvail, Info->SwapMemoryAvail);
        Info->PageSize = PAGE_SIZE;
        Info->TotalPhysicalPages = KernelStartup.PageCount;
        Info->MinimumLinearAddress = VMA_USER;
        Info->MaximumLinearAddress = VMA_KERNEL - 1;
        LPLIST ProcessList = GetProcessList();
        LPLIST TaskList = GetTaskList();
        Info->NumProcesses = ProcessList != NULL ? ProcessList->NumItems : 0;
        Info->NumTasks = TaskList != NULL ? TaskList->NumItems : 0;

        LPUSER_ACCOUNT User = GetCurrentUser();

        StringCopy(Info->UserName, User != NULL ? User->UserName : TEXT(""));
        StringCopy(Info->KeyboardLayout, GetKeyboardCode());

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Retrieve the thread-local last error value.
 *
 * Placeholder implementation until per-thread error codes are wired.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Record a thread-local last error value.
 *
 * Currently ignored; present for ABI compatibility.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current system tick count.
 *
 * @param Parameter Reserved.
 * @return UINT Value returned by GetSystemTime().
 */
UINT SysCall_GetSystemTime(UINT Parameter) {
    UNUSED(Parameter);
    return GetSystemTime();
}

/************************************************************************/

/**
 * @brief Retrieve the current local time.
 *
 * @param Parameter Linear address of a DATETIME structure to fill.
 * @return UINT TRUE on success, FALSE if the buffer is invalid.
 */
UINT SysCall_GetLocalTime(UINT Parameter) {
    LPDATETIME Time = (LPDATETIME)Parameter;
    SAFE_USE_VALID(Time) { return GetLocalTime(Time); }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Update the system local time.
 *
 * @param Parameter Linear address of a DATETIME structure with the new time.
 * @return UINT TRUE on success, FALSE otherwise.
 */
UINT SysCall_SetLocalTime(UINT Parameter) {
    LPDATETIME Time = (LPDATETIME)Parameter;
    SAFE_USE_VALID(Time) { return SetLocalTime(Time); }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Delete a kernel object referenced by a user handle.
 *
 * Resolves the provided handle, invokes the object-specific destructor,
 * then releases the handle from the mapping when the deletion succeeds.
 *
 * @param Parameter Handle referencing the object to delete.
 * @return UINT Result code from the underlying delete operation.
 */
UINT SysCall_DeleteObject(UINT Parameter) {
    LPPROCESS Caller = GetCurrentProcess();
    LINEAR ObjectPointer = HandleToPointer((HANDLE)Parameter);

    if (ObjectPointer == 0) {
        return 0;
    }

    if (!ProcessAccessCanTargetObject(Caller, (LPVOID)ObjectPointer, TRUE)) {
        return 0;
    }

    if (DeleteObject((HANDLE)ObjectPointer) == FALSE) {
        return 0;
    }

    ReleaseHandle((HANDLE)Parameter);
    return 1;
}

/************************************************************************/

/**
 * @brief Create a process and return handles for the new process and task.
 *
 * Invokes the kernel process creation logic, replaces returned pointers
 * with user-visible handles, and tears everything down if handle export
 * fails.
 *
 * @param Parameter Pointer to PROCESS_INFO structure supplied by userland.
 * @return UINT DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT SysCall_CreateProcess(UINT Parameter) {
    LPPROCESS_INFO Info = (LPPROCESS_INFO)Parameter;
    LPPROCESS Process;
    LPTASK Task;
    HANDLE ProcessHandle;
    HANDLE TaskHandle;

    SAFE_USE_INPUT_POINTER(Info, PROCESS_INFO) {
        if (CreateProcess(Info) == FALSE) {
            return DF_RETURN_GENERIC;
        }

        Process = (LPPROCESS)Info->Process;
        Task = (LPTASK)Info->Task;
        ProcessHandle = PointerToHandle((LINEAR)Process);
        TaskHandle = PointerToHandle((LINEAR)Task);

        if (ProcessHandle == NULL || TaskHandle == NULL) {
            if (ProcessHandle != NULL) {
                ReleaseHandle(ProcessHandle);
            }

            if (TaskHandle != NULL) {
                ReleaseHandle(TaskHandle);
            }

            KillProcess(Process);
            return DF_RETURN_GENERIC;
        }

        Info->Process = ProcessHandle;
        Info->Task = TaskHandle;

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Terminate a process referenced by a handle.
 *
 * @param Parameter Handle identifying the process to terminate, or 0 for the current process.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_KillProcess(UINT Parameter) {
    LPPROCESS Caller = GetCurrentProcess();
    LINEAR ProcessPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentProcess();
    LPPROCESS Process = (LPPROCESS)ProcessPointer;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (!ProcessAccessCanTargetProcess(Caller, Process, TRUE)) {
            return 0;
        }

        KillProcess(Process);
        if (Parameter) ReleaseHandle(Parameter);
        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve information about a process, using handles for inputs.
 *
 * Converts an optional handle in PROCESS_INFO into a kernel pointer before
 * filling the structure.
 *
 * @param Parameter Pointer to PROCESS_INFO provided by userland.
 * @return UINT DF_RETURN_SUCCESS on success, DF_RETURN_GENERIC on error.
 */
UINT SysCall_GetProcessInfo(UINT Parameter) {
    LPPROCESS_INFO Info = (LPPROCESS_INFO)Parameter;
    LPPROCESS Caller = GetCurrentProcess();
    LPPROCESS CurrentProcess;

    DEBUG(TEXT("Enter, Parameter=%x"), Parameter);

    SAFE_USE_INPUT_POINTER(Info, PROCESS_INFO) {
        CurrentProcess = Info->Process ? (LPPROCESS)HandleToPointer(Info->Process) : GetCurrentProcess();

        SAFE_USE_VALID_ID(CurrentProcess, KOID_PROCESS) {
            if (!ProcessAccessCanTargetProcess(Caller, CurrentProcess, TRUE)) {
                return DF_RETURN_GENERIC;
            }

            DEBUG(TEXT("Info->CommandLine = %s"), Info->CommandLine);
            DEBUG(TEXT("CurrentProcess=%p"), CurrentProcess);
            DEBUG(TEXT("CurrentProcess->CommandLine = %s"), CurrentProcess->CommandLine);

            // Copy the command line and work folder within buffer limits
            StringCopyLimit(Info->CommandLine, CurrentProcess->CommandLine, MAX_PATH_NAME);
            StringCopyLimit(Info->WorkFolder, CurrentProcess->WorkFolder, MAX_PATH_NAME);

            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Retrieve heap usage information for a process.
 *
 * Converts the optional process handle to a kernel pointer, validates read
 * access for the caller, then returns a consistent heap snapshot.
 *
 * @param Parameter Pointer to PROCESS_MEMORY_INFO provided by userland.
 * @return UINT DF_RETURN_SUCCESS on success, DF_RETURN_GENERIC on error.
 */
UINT SysCall_GetProcessMemoryInfo(UINT Parameter) {
    LPPROCESS_MEMORY_INFO Info = (LPPROCESS_MEMORY_INFO)Parameter;
    LPPROCESS Caller = GetCurrentProcess();
    LPPROCESS TargetProcess;

    SAFE_USE_INPUT_POINTER(Info, PROCESS_MEMORY_INFO) {
        TargetProcess = GetCurrentProcess();

        if (Info->Process != 0) {
            TargetProcess = (LPPROCESS)HandleToPointer(Info->Process);

            if (!ProcessAccessCanTargetProcess(Caller, TargetProcess, TRUE)) {
                return DF_RETURN_GENERIC;
            }
        }

        SAFE_USE_VALID_ID(TargetProcess, KOID_PROCESS) {
            if (HeapQueryProcessMemoryInfo(TargetProcess, Info) == FALSE) {
                return DF_RETURN_GENERIC;
            }

            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Copy a bounded profiling snapshot into a user buffer.
 *
 * @param Parameter Pointer to PROFILE_QUERY_INFO provided by userland.
 * @return UINT DF_RETURN_SUCCESS on success, DF_RETURN_GENERIC on error.
 */
UINT SysCall_GetProfileInfo(UINT Parameter) {
    LPPROFILE_QUERY_INFO Info = (LPPROFILE_QUERY_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PROFILE_QUERY_INFO) {
        if (Info->Capacity == 0) {
            ProfileGetStats(Info);
            return DF_RETURN_SUCCESS;
        }

        SAFE_USE_VALID(Info->Entries) {
            ProfileGetStats(Info);
            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Create a task for the current process and return its handle.
 *
 * @param Parameter Pointer to TASK_INFO structure provided by userland.
 * @return UINT DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT SysCall_CreateTask(UINT Parameter) {
    LPTASK_INFO TaskInfo = (LPTASK_INFO)Parameter;
    LPTASK Task;
    HANDLE Handle;

    SAFE_USE_INPUT_POINTER(TaskInfo, TASK_INFO) {
        Task = KernelCreateTask(GetCurrentProcess(), TaskInfo);

        if (Task == NULL) {
            return DF_RETURN_GENERIC;
        }

        Handle = PointerToHandle((LINEAR)Task);

        if (Handle == NULL) {
            KernelKillTask(Task);
            return DF_RETURN_GENERIC;
        }

        TaskInfo->Task = Handle;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Terminate a task referenced by a handle.
 *
 * Resolves the provided task handle (or uses the current task when the handle
 * is zero) and forwards the request to KernelKillTask. Releases the handle upon
 * successful termination.
 *
 * @param Parameter Handle identifying the task to terminate, or 0 for the current task.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_KillTask(UINT Parameter) {
    DEBUG(TEXT("Enter, Parameter=%x"), Parameter);

    LPPROCESS Caller = GetCurrentProcess();
    LINEAR TaskPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentTask();
    LPTASK Task = (LPTASK)TaskPointer;

    UINT Result = 0;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (!ProcessAccessCanTargetTask(Caller, Task, TRUE)) {
            return 0;
        }

        Result = (UINT)KernelKillTask(Task);
        if (Parameter && Result) ReleaseHandle(Parameter);
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Terminate the current task with the provided exit code.
 *
 * Converts the running task pointer into a verified kernel object prior to
 * delegating to KernelKillTask().
 *
 * @param Parameter Exit code stored in the task object.
 * @return UINT Result of KernelKillTask().
 */
UINT SysCall_Exit(UINT Parameter) {
    DEBUG(TEXT("Enter, Parameter=%x"), Parameter);

    LPTASK Task = GetCurrentTask();
    UINT ReturnValue = 0;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SetTaskExitCode(Task, Parameter);
        ReturnValue = KernelKillTask(Task);
    }

    DEBUG(TEXT("Exit"));

    return ReturnValue;
}

/************************************************************************/

/**
 * @brief Suspend execution of a task identified by handle.
 *
 * Resolves the task handle, checks same-user/admin access, and marks the task
 * as suspended so the scheduler stops selecting it while preserving its
 * current wait state.
 *
 * @param Parameter Handle identifying the task to suspend, or 0 for the current task.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_SuspendTask(UINT Parameter) {
    LPPROCESS Caller = GetCurrentProcess();
    LINEAR TaskPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentTask();
    LPTASK Task = (LPTASK)TaskPointer;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (!ProcessAccessCanTargetTask(Caller, Task, TRUE)) {
            return 0;
        }

        return (UINT)SuspendTaskExecution(Task);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Resume execution of a suspended task.
 *
 * Resolves the task handle, checks same-user/admin access, and clears the
 * suspension flag so the scheduler may run the task again.
 *
 * @param Parameter Handle identifying the task to resume, or 0 for the current task.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_ResumeTask(UINT Parameter) {
    LPPROCESS Caller = GetCurrentProcess();
    LINEAR TaskPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentTask();
    LPTASK Task = (LPTASK)TaskPointer;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (!ProcessAccessCanTargetTask(Caller, Task, TRUE)) {
            return 0;
        }

        return (UINT)ResumeTaskExecution(Task);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Block the current task for the specified duration in milliseconds.
 *
 * @param Parameter Sleep duration in milliseconds.
 * @return UINT TRUE when the sleep request was queued.
 */
UINT SysCall_Sleep(UINT Parameter) {
    Sleep(Parameter);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Wait for one or more kernel objects referenced by handles.
 *
 * Resolves every handle in WAIT_INFO into a kernel pointer before delegating
 * to Wait(), restoring the original handles afterwards.
 *
 * @param Parameter Pointer to WAIT_INFO structure provided by userland.
 * @return UINT Wait() return code or WAIT_INVALID_PARAMETER on invalid handle.
 */
UINT SysCall_Wait(UINT Parameter) {
    LPWAIT_INFO WaitInfo = (LPWAIT_INFO)Parameter;
    LPPROCESS Caller = GetCurrentProcess();

    SAFE_USE_INPUT_POINTER(WaitInfo, WAIT_INFO) {
        if (WaitInfo->Count == 0 || WaitInfo->Count > WAIT_INFO_MAX_OBJECTS) {
            return WAIT_INVALID_PARAMETER;
        }

        HANDLE OriginalHandles[WAIT_INFO_MAX_OBJECTS];

        for (UINT Index = 0; Index < WaitInfo->Count; Index++) {
            OriginalHandles[Index] = WaitInfo->Objects[Index];
            WaitInfo->Objects[Index] = (HANDLE)HandleToPointer(WaitInfo->Objects[Index]);

            if (WaitInfo->Objects[Index] == NULL) {
                for (UINT Restore = 0; Restore <= Index; Restore++) {
                    WaitInfo->Objects[Restore] = OriginalHandles[Restore];
                }
                return WAIT_INVALID_PARAMETER;
            }

            if (!ProcessAccessCanTargetObject(Caller, (LPVOID)WaitInfo->Objects[Index], TRUE)) {
                for (UINT Restore = 0; Restore <= Index; Restore++) {
                    WaitInfo->Objects[Restore] = OriginalHandles[Restore];
                }
                return WAIT_INVALID_PARAMETER;
            }
        }

        UINT Result = Wait(WaitInfo);

        for (UINT Index = 0; Index < WaitInfo->Count; Index++) {
            WaitInfo->Objects[Index] = OriginalHandles[Index];
        }

        return Result;
    }

    return WAIT_INVALID_PARAMETER;
}

/************************************************************************/

/**
 * @brief Post an asynchronous message to a task or window handle.
 *
 * Resolves the target handle to its kernel pointer and forwards the request
 * to PostMessage without altering the original ABI structure.
 *
 * @param Parameter Pointer to MESSAGE_INFO provided by userland.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_PostMessage(UINT Parameter) {
    LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
        LINEAR TargetPointer = HandleToPointer(Message->Target);

        if (Message->Target == 0) {
            return (UINT)PostMessage(NULL, Message->Message, Message->Param1, Message->Param2);
        }

        SAFE_USE_VALID((LPVOID)TargetPointer) {
            return (UINT)PostMessage((HANDLE)TargetPointer, Message->Message, Message->Param1, Message->Param2);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief SendMessage syscall stub.
 *
 * @param Parameter Pointer to MESSAGE_INFO supplied by userland.
 * @return UINT DF_RETURN_NOT_IMPLEMENTED.
 */
UINT SysCall_SendMessage(UINT Parameter) {
    UNUSED(Parameter);

    // Legacy synchronous path kept for reference:
    // LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;
    // SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
    //     LINEAR TargetPointer = HandleToPointer(Message->Target);
    //     if (Message->Target == 0) {
    //         return (UINT)SendMessage(NULL, Message->Message, Message->Param1, Message->Param2);
    //     }
    //     SAFE_USE_VALID((LPVOID)TargetPointer) {
    //         return (UINT)SendMessage((HANDLE)TargetPointer, Message->Message, Message->Param1, Message->Param2);
    //     }
    // }
    // return 0;

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Peek at the message queue without removing entries.
 *
 * Currently unimplemented; reserved for future handle-aware implementation.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_PeekMessage(UINT Parameter) {
    LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
        HANDLE Filter = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Filter);

        if (Message->Target == NULL && Filter != 0) {
            Message->Target = Filter;
            return 0;
        }

        UINT Result = (UINT)KernelPeekMessage(Message);
        Message->Target = PointerToHandle((LINEAR)Message->Target);
        if (Message->Target == 0) {
            Message->Target = Filter;
        }

        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the next message, translating handles as needed.
 *
 * Accepts an optional handle filter in MESSAGE_INFO.Target, resolves it to a
 * kernel pointer before invoking KernelGetMessage(), then converts the returned
 * pointer back into a handle.
 *
 * @param Parameter Pointer to MESSAGE_INFO supplied by userland.
 * @return UINT Non-zero on success, zero on failure or invalid handle.
 */
UINT SysCall_GetMessage(UINT Parameter) {
    LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
        HANDLE Filter = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Filter);

        if (Message->Target == NULL && Filter != 0) {
            Message->Target = Filter;
            return 0;
        }

        UINT Result = (UINT)KernelGetMessage(Message);
        Message->Target = PointerToHandle((LINEAR)Message->Target);
        if (Message->Target == 0) {
            Message->Target = Filter;
        }

        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Dispatch a message to its target window or task handle.
 *
 * Converts the target handle into a kernel pointer for the duration of the
 * dispatch, restoring the handle afterwards.
 *
 * @param Parameter Pointer to MESSAGE_INFO provided by userland.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_DispatchMessage(UINT Parameter) {
    LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
        HANDLE Original = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Original);

        if (Message->Target == NULL && Original != 0) {
            Message->Target = Original;
            return 0;
        }

        UINT Result = (UINT)KernelDispatchMessage(Message);

        Message->Target = Original;
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a kernel mutex and return a handle to it.
 *
 * @param Parameter Reserved.
 * @return UINT Handle referencing the newly created mutex, or 0 on failure.
 */
UINT SysCall_CreateMutex(UINT Parameter) {
    UNUSED(Parameter);

    LPMUTEX Mutex = CreateMutex();
    HANDLE Handle = PointerToHandle((LINEAR)Mutex);

    if (Handle == 0) {
        DeleteMutex(Mutex);
    }

    return Handle;
}

/************************************************************************/

/**
 * @brief Delete a mutex referenced by a handle.
 *
 * @param Parameter Handle of the mutex to delete.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_DeleteMutex(UINT Parameter) {
    LINEAR MutexPointer = HandleToPointer(Parameter);
    LPMUTEX Mutex = (LPMUTEX)MutexPointer;
    UINT Result = 0;

    SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) {
        Result = (UINT)DeleteMutex(Mutex);
        if (Parameter && Result) ReleaseHandle(Parameter);
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Lock a mutex identified by a handle.
 *
 * @param Parameter Pointer to MUTEX_INFO structure containing the handle and timeout.
 * @return UINT Lock count on success, MAX_U32 on invalid handle.
 */
UINT SysCall_LockMutex(UINT Parameter) {
    LPMUTEX_INFO Info = (LPMUTEX_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MUTEX_INFO) {
        LINEAR MutexPointer = HandleToPointer(Info->Mutex);
        LPMUTEX Mutex = (LPMUTEX)MutexPointer;

        SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) { return LockMutex(Mutex, Info->MilliSeconds); }
    }

    return (UINT)MAX_U32;
}

/************************************************************************/

/**
 * @brief Unlock a mutex identified by a handle.
 *
 * @param Parameter Pointer to MUTEX_INFO structure containing the handle.
 * @return UINT Lock count on success, MAX_U32 on invalid handle.
 */
UINT SysCall_UnlockMutex(UINT Parameter) {
    LPMUTEX_INFO Info = (LPMUTEX_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MUTEX_INFO) {
        LINEAR MutexPointer = HandleToPointer(Info->Mutex);
        LPMUTEX Mutex = (LPMUTEX)MutexPointer;

        SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) { return UnlockMutex(Mutex); }
    }

    return (UINT)MAX_U32;
}

/************************************************************************/

/**
 * @brief Allocate a region of virtual memory with specified attributes.
 *
 * @param Parameter Pointer to ALLOC_REGION_INFO structure.
 * @return UINT Operation result from AllocRegion or 0 on invalid input.
 */
UINT SysCall_AllocRegion(UINT Parameter) {
    LPALLOC_REGION_INFO Info = (LPALLOC_REGION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ALLOC_REGION_INFO) {
        return AllocRegion(Info->Base, Info->Target, Info->Size, Info->Flags, NULL);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Free a previously allocated virtual memory region.
 *
 * @param Parameter Pointer to ALLOC_REGION_INFO describing the region.
 * @return UINT Operation result from FreeRegion or 0 on invalid input.
 */
UINT SysCall_FreeRegion(UINT Parameter) {
    LPALLOC_REGION_INFO Info = (LPALLOC_REGION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ALLOC_REGION_INFO) { return FreeRegion(Info->Base, Info->Size); }

    return 0;
}

/************************************************************************/

/**
 * @brief Test whether a linear address is valid in the current context.
 *
 * @param Parameter Linear address to test.
 * @return UINT Non-zero if valid, zero otherwise.
 */
/**
 * @brief Check whether a linear address is mapped in the caller context.
 *
 * @param Parameter Linear address to validate.
 * @return UINT TRUE if the address is valid, FALSE otherwise.
 */
UINT SysCall_IsMemoryValid(UINT Parameter) {
    return (UINT)IsValidMemory((LINEAR)Parameter);
}

/************************************************************************/

/**
 * @brief Retrieve the heap base for a process referenced by handle.
 *
 * @param Parameter Process handle or 0 for the current process.
 * @return UINT Linear address of the process heap, or 0 on failure.
 */
UINT SysCall_GetProcessHeap(UINT Parameter) {
    LPPROCESS Caller = GetCurrentProcess();
    LINEAR ProcessPointer = Parameter ? HandleToPointer(Parameter) : 0;
    LPPROCESS Process = (LPPROCESS)ProcessPointer;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (!ProcessAccessCanTargetProcess(Caller, Process, TRUE)) {
            return 0;
        }

        return (UINT)GetProcessHeap(Process);
    }

    if (Parameter == 0) {
        return (UINT)GetProcessHeap(NULL);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate memory from the current process heap.
 *
 * @param Parameter Size in bytes to allocate.
 * @return UINT Linear address of the allocated block, or 0 on failure.
 */
UINT SysCall_HeapAlloc(UINT Parameter) { return (UINT)HeapAlloc(Parameter); }

/************************************************************************/

/**
 * @brief Free a block previously obtained from SysCall_HeapAlloc.
 *
 * @param Parameter Linear address of the block to free.
 * @return UINT Always returns 0.
 */
UINT SysCall_HeapFree(UINT Parameter) {
    HeapFree((LPVOID)Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Resize a heap allocation while preserving its contents.
 *
 * @param Parameter Linear address of HEAP_REALLOC_INFO describing the request.
 * @return UINT Linear address of the resized block, or 0 on failure.
 */
UINT SysCall_HeapRealloc(UINT Parameter) {
    LPHEAP_REALLOC_INFO Info = (LPHEAP_REALLOC_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, HEAP_REALLOC_INFO) { return (UINT)HeapRealloc(Info->Pointer, Info->Size); }

    return 0;
}

/************************************************************************/

/**
 * @brief Enumerate mounted volumes, exposing handles to the callback.
 *
 * @param Parameter Pointer to ENUM_VOLUMES_INFO describing the callback and context.
 * @return UINT Non-zero when enumeration ran, zero on error.
 */
UINT SysCall_EnumVolumes(UINT Parameter) {
    LPENUM_VOLUMES_INFO Info = (LPENUM_VOLUMES_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ENUM_VOLUMES_INFO) {
        if (Info->Func == NULL) return 0;

        LockMutex(MUTEX_FILESYSTEM, INFINITY);

        LPLIST FileSystemList = GetFileSystemList();
        for (LPLISTNODE Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node;
             Node = Node->Next) {
            LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
            HANDLE VolumeHandle = PointerToHandle((LINEAR)FileSystem);

            if (VolumeHandle == 0) continue;

            U32 Result = Info->Func(VolumeHandle, Info->Parameter);
            ReleaseHandle(VolumeHandle);

            if (Result == 0) break;
        }

        UnlockMutex(MUTEX_FILESYSTEM);
        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve information for a specific volume handle.
 *
 * @param Parameter Pointer to VOLUME_INFO containing the target handle.
 * @return UINT Non-zero on success.
 */
UINT SysCall_GetVolumeInfo(UINT Parameter) {
    LPVOLUME_INFO Info = (LPVOLUME_INFO)Parameter;

    SAFE_USE_VALID(Info) {
        if (Info->Size < sizeof(VOLUME_INFO)) {
            return 0;
        }

        LPFILESYSTEM FileSystem = (LPFILESYSTEM)HandleToPointer(Info->Volume);

        SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
            LockMutex(&(FileSystem->Mutex), INFINITY);
            StringCopy(Info->Name, FileSystem->Name);
            UnlockMutex(&(FileSystem->Mutex));
            return 1;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Open a file and return a handle to user space.
 *
 * @param Parameter Pointer to FILE_OPEN_INFO describing the request.
 * @return UINT File handle on success, 0 otherwise.
 */
UINT SysCall_OpenFile(UINT Parameter) {
    LPFILE_OPEN_INFO Info = (LPFILE_OPEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILE_OPEN_INFO) {
        LPFILE File = OpenFile(Info);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            HANDLE Handle = PointerToHandle((LINEAR)File);

            if (Handle != 0) {
                return Handle;
            }

            CloseFile(File);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Read data from a file handle into a caller-provided buffer.
 *
 * @param Parameter Pointer to FILE_OPERATION containing the read request.
 * @return UINT Bytes read on success, 0 on failure.
 */
UINT SysCall_ReadFile(UINT Parameter) {
    LPFILE_OPERATION Operation = (LPFILE_OPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILE_OPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = ReadFile(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Write data from a caller buffer into a file handle.
 *
 * @param Parameter Pointer to FILE_OPERATION describing the write.
 * @return UINT Bytes written on success, 0 otherwise.
 */
UINT SysCall_WriteFile(UINT Parameter) {
    LPFILE_OPERATION Operation = (LPFILE_OPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILE_OPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = WriteFile(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the size of a file handle.
 *
 * @param Parameter File handle.
 * @return UINT File size or 0 on error.
 */
UINT SysCall_GetFileSize(UINT Parameter) {
    LPFILE File = (LPFILE)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(File, KOID_FILE) { return GetFileSize(File); }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current file position for a handle.
 *
 * @param Parameter File handle.
 * @return UINT File position or 0 on error.
 */
UINT SysCall_GetFilePosition(UINT Parameter) {
    LPFILE File = (LPFILE)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(File, KOID_FILE) { return GetFilePosition(File); }

    return 0;
}

/************************************************************************/

/**
 * @brief Update the file pointer for a handle.
 *
 * @param Parameter Pointer to FILE_OPERATION describing the seek.
 * @return UINT Result of SetFilePosition, 0 on failure.
 */
UINT SysCall_SetFilePosition(UINT Parameter) {
    LPFILE_OPERATION Operation = (LPFILE_OPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILE_OPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = SetFilePosition(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

static BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern) {
    /* Simple '*' wildcard matcher */
    if (Pattern == NULL || Pattern[0] == STR_NULL) {
        return TRUE;
    }

    /* If no wildcard, direct compare */
    LPSTR Star = StringFindChar((LPCSTR)Pattern, '*');
    if (Star == NULL) {
        return STRINGS_EQUAL(Name, Pattern);
    }

    STR Prefix[MAX_FILE_NAME];
    STR Suffix[MAX_FILE_NAME];

    U32 PrefixLen = (U32)(Star - (LPSTR)Pattern);
    for (U32 i = 0; i < PrefixLen && i < MAX_FILE_NAME - 1; i++) {
        Prefix[i] = Pattern[i];
    }
    Prefix[PrefixLen] = STR_NULL;
    StringCopy(Suffix, (LPCSTR)(Star + 1));

    /* Check prefix */
    for (U32 i = 0; i < PrefixLen; i++) {
        if (Name[i] == STR_NULL || Name[i] != Prefix[i]) {
            return FALSE;
        }
    }

    U32 NameLen = StringLength(Name);
    U32 SuffixLen = StringLength(Suffix);
    if (SuffixLen > NameLen) return FALSE;

    if (SuffixLen == 0) return TRUE;

    return STRINGS_EQUAL(Name + (NameLen - SuffixLen), Suffix);
}

static BOOL BuildEnumeratePattern(LPCSTR Path, LPSTR OutPattern) {
    if (OutPattern == NULL) return FALSE;
    OutPattern[0] = STR_NULL;

    if (Path == NULL || Path[0] == STR_NULL) {
        StringCopy(OutPattern, TEXT("*"));
        return TRUE;
    }

    StringCopy(OutPattern, Path);
    U32 Len = StringLength(OutPattern);
    if (Len > 0 && OutPattern[Len - 1] != PATH_SEP) {
        StringConcat(OutPattern, TEXT("/"));
    }
    StringConcat(OutPattern, TEXT("*"));
    return TRUE;
}

UINT SysCall_FindFirstFile(UINT Parameter) {
    LPFILE_FIND_INFO Info = (LPFILE_FIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILE_FIND_INFO) {
        STR EnumeratePattern[MAX_PATH_NAME];
        if (!BuildEnumeratePattern(Info->Path, EnumeratePattern)) {
            return FALSE;
        }

        FILE_INFO Find;
        Find.Size = sizeof(FILE_INFO);
        Find.FileSystem = GetSystemFS();
        Find.Attributes = MAX_U32;
        Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
        StringCopy(Find.Name, EnumeratePattern);

        LPFILESYSTEM FS = GetSystemFS();
        if (FS == NULL || FS->Driver == NULL || FS->Driver->Command == NULL) {
            return FALSE;
        }

        LPFILE File = (LPFILE)FS->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        if (File == NULL) {
            return FALSE;
        }

        BOOL Found = FALSE;
        do {
            if (MatchPattern(File->Name, Info->Pattern)) {
                StringCopy(Info->Name, File->Name);
                Info->Attributes = File->Attributes;
                Found = TRUE;
                break;
            }
        } while (FS->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RETURN_SUCCESS);

        if (!Found) {
            FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
            return FALSE;
        }

        HANDLE Handle = PointerToHandle((LINEAR)File);
        if (Handle == 0) {
            FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
            return FALSE;
        }

        Info->SearchHandle = Handle;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

UINT SysCall_FindNextFile(UINT Parameter) {
    LPFILE_FIND_INFO Info = (LPFILE_FIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILE_FIND_INFO) {
        LPFILE File = (LPFILE)HandleToPointer(Info->SearchHandle);
        LPFILESYSTEM FS = GetSystemFS();

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if (FS == NULL || FS->Driver == NULL || FS->Driver->Command == NULL) {
                return FALSE;
            }

            BOOL Found = FALSE;
            while (FS->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RETURN_SUCCESS) {
                if (MatchPattern(File->Name, Info->Pattern)) {
                    StringCopy(Info->Name, File->Name);
                    Info->Attributes = File->Attributes;
                    Found = TRUE;
                    break;
                }
            }

            if (!Found) {
                FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
                Info->SearchHandle = 0;
                return FALSE;
            }

            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Peek the next keyboard character without removing it.
 *
 * @param Parameter Reserved.
 * @return UINT Non-zero when a key is available.
 */
UINT SysCall_ConsolePeekKey(UINT Parameter) {
    UNUSED(Parameter);
    return (UINT)PeekChar();
}

/************************************************************************/

/**
 * @brief Retrieve the next key event details.
 *
 * @param Parameter Linear address of a KEYCODE structure to fill.
 * @return UINT TRUE on success, FALSE on error.
 */
UINT SysCall_ConsoleGetKey(UINT Parameter) {
    LPKEYCODE KeyCode = (LPKEYCODE)Parameter;
    SAFE_USE_VALID(KeyCode) { return (UINT)GetKeyCode(KeyCode); }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve current key modifier state.
 *
 * @param Parameter Linear address of a U32 to fill with KEYMOD_* flags.
 * @return UINT TRUE on success, FALSE on error.
 */
UINT SysCall_ConsoleGetKeyModifiers(UINT Parameter) {
    U32* Modifiers = (U32*)Parameter;
    SAFE_USE_VALID(Modifiers) {
        *Modifiers = GetKeyModifiers();
        return 1;
    }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the next character from the console input.
 *
 * Currently unimplemented; reserved for future console work.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGetChar(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Output a string to the system console.
 *
 * @param Parameter Linear address of a null-terminated string.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsolePrint(UINT Parameter) {
    SAFE_USE_VALID((LPCSTR)Parameter) { ConsolePrint((LPCSTR)Parameter); }
    return 0;
}

/************************************************************************/

/**
 * @brief Blit a text buffer to the console at the given position.
 *
 * @param Parameter Linear address of CONSOLE_BLIT_BUFFER.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleBlitBuffer(UINT Parameter) {
    LPCONSOLE_BLIT_BUFFER Info = (LPCONSOLE_BLIT_BUFFER)Parameter;
    return ConsoleBlitBuffer(Info);
}

/************************************************************************/

/**
 * @brief Read a string from the console.
 *
 * Placeholder implementation; not yet supported.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGetString(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Move the console cursor to the specified position.
 *
 * @param Parameter Linear address of a POINT structure.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGotoXY(UINT Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    SAFE_USE_VALID(Point) { SetConsoleCursorPosition(Point->X, Point->Y); }
    return 0;
}

/************************************************************************/

/**
 * @brief Clear the entire console screen.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleClear(UINT Parameter) {
    UNUSED(Parameter);
    ClearConsole();
    return 0;
}

/************************************************************************/

/**
 * @brief Set the console text mode.
 *
 * @param Parameter Linear address of GRAPHICS_MODE_INFO (Width/Height in chars).
 * @return UINT DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT SysCall_ConsoleSetMode(UINT Parameter) {
    LPGRAPHICS_MODE_INFO Info = (LPGRAPHICS_MODE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, GRAPHICS_MODE_INFO) { return ConsoleSetMode(Info); }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Retrieve the number of console modes.
 *
 * @param Parameter Reserved.
 * @return UINT Number of modes.
 */
UINT SysCall_ConsoleGetModeCount(UINT Parameter) {
    UNUSED(Parameter);
    return ConsoleGetModeCount();
}

/************************************************************************/

/**
 * @brief Retrieve console mode info by index.
 *
 * @param Parameter Linear address of CONSOLE_MODE_INFO.
 * @return UINT DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT SysCall_ConsoleGetModeInfo(UINT Parameter) {
    LPCONSOLE_MODE_INFO Info = (LPCONSOLE_MODE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, CONSOLE_MODE_INFO) { return ConsoleGetModeInfo(Info); }

    return DF_RETURN_GENERIC;
}

/************************************************************************/

/**
 * @brief Retrieve the current console mode geometry.
 *
 * @param Parameter Linear address of CONSOLE_MODE_INFO.
 * @return UINT TRUE on success.
 */
UINT SysCall_ConsoleGetCurrentMode(UINT Parameter) {
    LPCONSOLE_MODE_INFO Info = (LPCONSOLE_MODE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, CONSOLE_MODE_INFO) {
        Info->Index = 0;
        Info->Columns = GetConsoleWidth();
        Info->Rows = GetConsoleHeight();
        Info->CharHeight = GetConsoleCharHeight();
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Create a new desktop for the current process.
 *
 * @param Parameter Reserved.
 * @return UINT Desktop handle on success, 0 otherwise.
 */
UINT SysCall_CreateDesktop(UINT Parameter) {
    UNUSED(Parameter);

    LPDESKTOP Desktop = KernelCreateDesktop();
    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        HANDLE Handle = PointerToHandle((LINEAR)Desktop);

        if (Handle != 0) {
            return Handle;
        }

        DeleteDesktop(Desktop);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Show the desktop associated with the provided handle.
 *
 * @param Parameter Desktop handle.
 * @return UINT Result of KernelShowDesktop or 0 on error.
 */
UINT SysCall_ShowDesktop(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Desktop, KOID_DESKTOP, TRUE) {
        return (UINT)KernelShowDesktop(Desktop);
    }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the top-level window handle for a desktop.
 *
 * @param Parameter Desktop handle.
 * @return UINT Window handle or 0 on error.
 */
UINT SysCall_GetDesktopWindow(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)HandleToPointer(Parameter);
    LPWINDOW Window = NULL;

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Desktop, KOID_DESKTOP, TRUE) {
        LockMutex(&(Desktop->Mutex), INFINITY);
        Window = Desktop->Window;
        UnlockMutex(&(Desktop->Mutex));

        if (Window != NULL && !ProcessAccessCanCurrentProcessTargetObject(Window, TRUE)) {
            return 0;
        }

        HANDLE Handle = PointerToHandle((LINEAR)Window);
        return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Return the desktop handle associated with the current process.
 *
 * @param Parameter Reserved.
 * @return UINT Desktop handle or 0 on failure.
 */
UINT SysCall_GetCurrentDesktop(UINT Parameter) {
    UNUSED(Parameter);

    LPPROCESS Process = GetCurrentProcess();

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LPDESKTOP Desktop = Process->Desktop;

        SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
            HANDLE Handle = PointerToHandle((LINEAR)Desktop);
            return Handle;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Apply one desktop theme target for the current process.
 *
 * The target may reference a built-in alias, an already staged theme, or one
 * loadable theme path, with the resolution policy owned by desktop runtime.
 *
 * @param Parameter Pointer to DESKTOP_THEME_INFO.
 * @return UINT TRUE on success, FALSE on error.
 */
UINT SysCall_ApplyDesktopTheme(UINT Parameter) {
    LPDESKTOP_THEME_INFO ApplyInfo = (LPDESKTOP_THEME_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(ApplyInfo, DESKTOP_THEME_INFO) {
        SAFE_USE_VALID(ApplyInfo->Target) {
            return (UINT)ApplyDesktopTheme(ApplyInfo->Target);
        }
    }

    return FALSE;
}

/**
 * @brief Create a window and return its handle.
 *
 * @param Parameter Pointer to WINDOW_INFO describing the window.
 * @return UINT Window handle on success, 0 otherwise.
 */
UINT SysCall_CreateWindow(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        if (WindowInfo->Parent != 0) {
            LPWINDOW ParentWindow = (LPWINDOW)HandleToPointer(WindowInfo->Parent);

            SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(ParentWindow, KOID_WINDOW, TRUE) {
            } else {
                WindowInfo->Window = 0;
                return 0;
            }
        }

        if (WindowInfo->WindowClass != 0 || WindowInfo->WindowClassName != NULL) {
            if (SysCallResolveAccessibleWindowClass(WindowInfo->WindowClass, WindowInfo->WindowClassName) == NULL) {
                WindowInfo->Window = 0;
                return 0;
            }
        }

        HANDLE ParentHandle = WindowInfo->Parent;
        WindowInfo->Parent = (HANDLE)HandleToPointer(ParentHandle);

        LPWINDOW Window = (LPWINDOW)CreateWindow(WindowInfo);

        WindowInfo->Parent = ParentHandle;

        SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
            HANDLE WindowHandle = PointerToHandle((LINEAR)Window);

            if (WindowHandle != 0) {
                WindowInfo->Window = WindowHandle;
                return WindowHandle;
            }

            DeleteWindow((HANDLE)Window);
        }

        WindowInfo->Window = 0;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Show a window referenced by handle.
 *
 * @param Parameter Pointer to WINDOW_INFO containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_ShowWindow(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)ShowWindow((HANDLE)Window);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Hide a window referenced by handle.
 *
 * @param Parameter Pointer to WINDOW_INFO containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_HideWindow(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)HideWindow((HANDLE)Window);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Move and/or resize one window.
 *
 * @param Parameter Pointer to WINDOW_RECT containing the target window and new rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_MoveWindow(UINT Parameter) {
    LPWINDOW_RECT WindowRect = (LPWINDOW_RECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOW_RECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)MoveWindow((HANDLE)Window, &(WindowRect->Rect));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Resize a window.
 *
 * @param Parameter Pointer to WINDOW_INFO containing the target window and new size.
 * @return UINT TRUE on success.
 */
UINT SysCall_SizeWindow(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)SizeWindow((HANDLE)Window, &(WindowInfo->WindowSize));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set a custom window procedure.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current window procedure.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Update window style flags.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetWindowStyle(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)SetWindowStyle((HANDLE)Window, WindowInfo->Style);
        }
    }

    return 0;
}

/************************************************************************/

UINT SysCall_ClearWindowStyle(UINT Parameter) {
    LPWINDOW_INFO WindowInfo = (LPWINDOW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOW_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)ClearWindowStyle((HANDLE)Window, WindowInfo->Style);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve window style flags.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetWindowStyle(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Associate a custom property with a window.
 *
 * @param Parameter Pointer to PROP_INFO describing the property.
 * @return UINT Previous property value, or 0.
 */
UINT SysCall_SetWindowProp(UINT Parameter) {
    LPPROP_INFO PropInfo = (LPPROP_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(PropInfo, PROP_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(PropInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return SetWindowProp((HANDLE)Window, PropInfo->Name, PropInfo->Value);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a custom property from a window.
 *
 * @param Parameter Pointer to PROP_INFO containing the window handle and property name.
 * @return UINT Property value or 0.
 */
UINT SysCall_GetWindowProp(UINT Parameter) {
    LPPROP_INFO PropInfo = (LPPROP_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(PropInfo, PROP_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(PropInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return GetWindowProp((HANDLE)Window, PropInfo->Name);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the screen rectangle for a window.
 *
 * @param Parameter Pointer to WINDOW_RECT containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetWindowRect(UINT Parameter) {
    LPWINDOW_RECT WindowRect = (LPWINDOW_RECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOW_RECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)GetWindowRect((HANDLE)Window, &(WindowRect->Rect));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the client rectangle for a window.
 *
 * @param Parameter Pointer to WINDOW_RECT containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetWindowClientRect(UINT Parameter) {
    LPWINDOW_RECT WindowRect = (LPWINDOW_RECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOW_RECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)GetWindowClientRect((HANDLE)Window, &(WindowRect->Rect));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Convert one screen point to one window-relative point.
 *
 * @param Parameter Pointer to WINDOW_POINT_INFO containing the target window.
 * @return UINT TRUE on success.
 */
UINT SysCall_ScreenPointToWindowPoint(UINT Parameter) {
    LPWINDOW_POINT_INFO WindowPointInfo = (LPWINDOW_POINT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowPointInfo, WINDOW_POINT_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowPointInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            return (UINT)ScreenPointToWindowPoint(
                (HANDLE)Window,
                &(WindowPointInfo->ScreenPoint),
                &(WindowPointInfo->WindowPoint));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the parent handle for one window.
 *
 * @param Parameter Window handle.
 * @return UINT Parent handle or 0.
 */
UINT SysCall_GetWindowParent(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        HANDLE ParentWindow = GetWindowParent((HANDLE)Window);

        if (ParentWindow != NULL && !ProcessAccessCanCurrentProcessTargetObject((LPVOID)ParentWindow, TRUE)) {
            return 0;
        }

        HANDLE ParentHandle = PointerToHandle((LINEAR)ParentWindow);
        return ParentHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the direct child count for one window.
 *
 * @param Parameter Window handle.
 * @return UINT Number of direct children.
 */
UINT SysCall_GetWindowChildCount(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        return (UINT)GetWindowChildCount((HANDLE)Window);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve one direct child handle by index.
 *
 * @param Parameter Pointer to WINDOW_CHILD_INFO.
 * @return UINT Child handle or 0.
 */
UINT SysCall_GetWindowChild(UINT Parameter) {
    LPWINDOW_CHILD_INFO WindowChildInfo = (LPWINDOW_CHILD_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowChildInfo, WINDOW_CHILD_INFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowChildInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            HANDLE ChildWindow;

            ChildWindow = GetWindowChild((HANDLE)Window, WindowChildInfo->ChildIndex);
            if (ChildWindow != NULL && !ProcessAccessCanCurrentProcessTargetObject((LPVOID)ChildWindow, TRUE)) {
                return 0;
            }

            HANDLE ChildHandle = PointerToHandle((LINEAR)ChildWindow);
            return ChildHandle;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the next sibling handle for one window.
 *
 * @param Parameter Window handle.
 * @return UINT Next sibling handle or 0.
 */
UINT SysCall_GetNextWindowSibling(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        HANDLE SiblingWindow;

        SiblingWindow = GetNextWindowSibling((HANDLE)Window);
        if (SiblingWindow != NULL && !ProcessAccessCanCurrentProcessTargetObject((LPVOID)SiblingWindow, TRUE)) {
            return 0;
        }

        HANDLE SiblingHandle = PointerToHandle((LINEAR)SiblingWindow);
        return SiblingHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the previous sibling handle for one window.
 *
 * @param Parameter Window handle.
 * @return UINT Previous sibling handle or 0.
 */
UINT SysCall_GetPreviousWindowSibling(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        HANDLE SiblingWindow;

        SiblingWindow = GetPreviousWindowSibling((HANDLE)Window);
        if (SiblingWindow != NULL && !ProcessAccessCanCurrentProcessTargetObject((LPVOID)SiblingWindow, TRUE)) {
            return 0;
        }

        HANDLE SiblingHandle = PointerToHandle((LINEAR)SiblingWindow);
        return SiblingHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Register one userland window class.
 *
 * @param Parameter Pointer to WINDOW_CLASS_INFO.
 * @return UINT Class identifier on success, 0 on failure.
 */
UINT SysCall_RegisterWindowClass(UINT Parameter) {
    LPWINDOW_CLASS_INFO ClassInfo = (LPWINDOW_CLASS_INFO)Parameter;
    LPWINDOW_CLASS WindowClass;
    LPPROCESS Process;

    SAFE_USE_INPUT_POINTER(ClassInfo, WINDOW_CLASS_INFO) {
        if (ClassInfo->BaseClass != 0 || ClassInfo->BaseClassName != NULL) {
            if (SysCallResolveAccessibleWindowClass(ClassInfo->BaseClass, ClassInfo->BaseClassName) == NULL) {
                return 0;
            }
        }

        Process = GetCurrentProcess();
        if (Process == NULL || Process->TypeID != KOID_PROCESS) return 0;

        WindowClass = WindowClassRegisterUserClass(
            ClassInfo->ClassName,
            (U32)ClassInfo->BaseClass,
            ClassInfo->BaseClassName,
            ClassInfo->Function,
            ClassInfo->ClassDataSize,
            Process);

        if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return 0;

        ClassInfo->WindowClass = (HANDLE)WindowClass->ClassID;
        return (UINT)WindowClass->ClassID;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Unregister one userland window class.
 *
 * @param Parameter Pointer to WINDOW_CLASS_INFO.
 * @return UINT TRUE on success, FALSE on failure.
 */
UINT SysCall_UnregisterWindowClass(UINT Parameter) {
    LPWINDOW_CLASS_INFO ClassInfo = (LPWINDOW_CLASS_INFO)Parameter;
    LPPROCESS Process;

    SAFE_USE_INPUT_POINTER(ClassInfo, WINDOW_CLASS_INFO) {
        Process = GetCurrentProcess();
        if (Process == NULL || Process->TypeID != KOID_PROCESS) return FALSE;

        return (UINT)WindowClassUnregisterUserClass((U32)ClassInfo->WindowClass, ClassInfo->ClassName, Process);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Find one userland window class by name.
 *
 * @param Parameter Pointer to WINDOW_CLASS_INFO.
 * @return UINT Class identifier on success, 0 when absent or on failure.
 */
UINT SysCall_FindWindowClass(UINT Parameter) {
    LPWINDOW_CLASS_INFO ClassInfo = (LPWINDOW_CLASS_INFO)Parameter;
    LPWINDOW_CLASS WindowClass;

    SAFE_USE_INPUT_POINTER(ClassInfo, WINDOW_CLASS_INFO) {
        WindowClass = WindowClassFindByName(ClassInfo->ClassName);
        if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) {
            ClassInfo->WindowClass = 0;
            return 0;
        }

        if (!ProcessAccessCanCurrentProcessTargetObject(WindowClass, TRUE)) {
            ClassInfo->WindowClass = 0;
            return 0;
        }

        ClassInfo->WindowClass = (HANDLE)WindowClass->ClassID;
        return (UINT)WindowClass->ClassID;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Resolve whether one window inherits from one window class.
 *
 * @param Parameter Pointer to WINDOW_CLASS_QUERY_INFO.
 * @return UINT TRUE when the window inherits from the class.
 */
UINT SysCall_WindowInheritsClass(UINT Parameter) {
    LPWINDOW_CLASS_QUERY_INFO QueryInfo = (LPWINDOW_CLASS_QUERY_INFO)Parameter;
    LPWINDOW Window;

    SAFE_USE_INPUT_POINTER(QueryInfo, WINDOW_CLASS_QUERY_INFO) {
        Window = (LPWINDOW)HandleToPointer(QueryInfo->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            if ((QueryInfo->WindowClass != 0 || QueryInfo->ClassName != NULL) &&
                SysCallResolveAccessibleWindowClass(QueryInfo->WindowClass, QueryInfo->ClassName) == NULL) {
                return FALSE;
            }

            return (UINT)WindowInheritsClass((HANDLE)Window, QueryInfo->WindowClass, QueryInfo->ClassName);
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Mark a client window region as needing redraw.
 *
 * @param Parameter Pointer to WINDOW_RECT with the target window and client rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_InvalidateClientRect(UINT Parameter) {
    LPWINDOW_RECT WindowRect = (LPWINDOW_RECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOW_RECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);

        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            if ((WindowRect->Header.Flags & WINDOW_RECT_FLAG_ALL) != 0) {
                return (UINT)InvalidateClientRect((HANDLE)Window, NULL);
            }

            return (UINT)InvalidateClientRect((HANDLE)Window, &(WindowRect->Rect));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Mark a window region as needing redraw.
 *
 * @param Parameter Pointer to WINDOW_RECT with the target window and rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_InvalidateWindowRect(UINT Parameter) {
    LPWINDOW_RECT WindowRect = (LPWINDOW_RECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOW_RECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
            if ((WindowRect->Header.Flags & WINDOW_RECT_FLAG_ALL) != 0) {
                return (UINT)InvalidateWindowRect((HANDLE)Window, NULL);
            }

            return (UINT)InvalidateWindowRect((HANDLE)Window, &(WindowRect->Rect));
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Obtain a graphics context for drawing into a window.
 *
 * @param Parameter Window handle.
 * @return UINT Graphics context handle or 0 on failure.
 */
UINT SysCall_GetWindowGC(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        HANDLE ContextPointer = GetWindowGC((HANDLE)Window);

        SAFE_USE_VALID((LPVOID)ContextPointer) {
            HANDLE Handle = PointerToHandle((LINEAR)ContextPointer);

            if (Handle != 0) {
                return Handle;
            }

            ReleaseWindowGC(ContextPointer);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Release a previously obtained graphics context.
 *
 * @param Parameter Graphics context handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_ReleaseWindowGC(UINT Parameter) {
    LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Context, KOID_GRAPHICSCONTEXT, TRUE) {
        UINT Result = (UINT)ReleaseWindowGC((HANDLE)Context);
        if (Result) ReleaseHandle(Parameter);
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Enumerate windows for the current desktop.
 *
 * Not implemented yet.
 *
 * @param Parameter Reserved.
 * @return UINT Always 0.
 */
UINT SysCall_EnumWindows(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Invoke the base window procedure.
 *
 * @param Parameter Pointer to MESSAGE_INFO structure.
 * @return UINT Result of BaseWindowFunc on success, 0 on error.
 */
UINT SysCall_BaseWindowFunc(UINT Parameter) {
    LPMESSAGE_INFO Message = (LPMESSAGE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGE_INFO) {
        HANDLE Original = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Original);

        if (Message->Target == NULL && Original != 0) {
            Message->Target = Original;
            return 0;
        }

        UINT Result = (UINT)BaseWindowFunc(Message->Target, Message->Message, Message->Param1, Message->Param2);

        Message->Target = Original;
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a system brush handle by identifier.
 *
 * @param Parameter System brush identifier.
 * @return UINT Brush handle or 0 if unavailable.
 */
UINT SysCall_GetSystemBrush(UINT Parameter) {
    HANDLE BrushPointer = GetSystemBrush(Parameter);

    SAFE_USE_VALID((LPVOID)BrushPointer) {
        HANDLE Handle = PointerToHandle((LINEAR)BrushPointer);
        if (Handle != 0) return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a system pen handle by identifier.
 *
 * @param Parameter System pen identifier.
 * @return UINT Pen handle or 0 if unavailable.
 */
UINT SysCall_GetSystemPen(UINT Parameter) {
    HANDLE PenPointer = GetSystemPen(Parameter);

    SAFE_USE_VALID((LPVOID)PenPointer) {
        HANDLE Handle = PointerToHandle((LINEAR)PenPointer);
        if (Handle != 0) return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a brush and expose it as a handle.
 *
 * @param Parameter Pointer to BRUSH_INFO describing the brush.
 * @return UINT Brush handle or 0 on failure.
 */
UINT SysCall_CreateBrush(UINT Parameter) {
    LPBRUSH_INFO Info = (LPBRUSH_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, BRUSH_INFO) {
        LPBRUSH Brush = (LPBRUSH)CreateBrush(Info);

        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) {
            HANDLE Handle = PointerToHandle((LINEAR)Brush);
            if (Handle != 0) return Handle;
            KernelHeapFree(Brush);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a pen and expose it as a handle.
 *
 * @param Parameter Pointer to PEN_INFO describing the pen.
 * @return UINT Pen handle or 0 on failure.
 */
UINT SysCall_CreatePen(UINT Parameter) {
    LPPEN_INFO Info = (LPPEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PEN_INFO) {
        LPPEN Pen = (LPPEN)CreatePen(Info);

        SAFE_USE_VALID_ID(Pen, KOID_PEN) {
            HANDLE Handle = PointerToHandle((LINEAR)Pen);
            if (Handle != 0) return Handle;
            KernelHeapFree(Pen);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Select a brush into a graphics context.
 *
 * @param Parameter Pointer to GCSELECT containing GC and brush handles.
 * @return UINT Previous brush handle, or 0.
 */
UINT SysCall_SelectBrush(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;

    SAFE_USE_INPUT_POINTER(Sel, GCSELECT) {
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Sel->GC);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Context, KOID_GRAPHICSCONTEXT, TRUE) {
            if (Sel->Object != 0) {
                LPBRUSH Brush = (LPBRUSH)HandleToPointer(Sel->Object);
                SAFE_USE_VALID_ID(Brush, KOID_BRUSH) {
                    HANDLE Previous = SelectBrush((HANDLE)Context, (HANDLE)Brush);
                    SAFE_USE_VALID((LPVOID)Previous) {
                        HANDLE Handle = PointerToHandle((LINEAR)Previous);
                        if (Handle != 0) return Handle;
                    }
                    return 0;
                }
                return 0;
            }

            HANDLE Previous = SelectBrush((HANDLE)Context, NULL);
            SAFE_USE_VALID((LPVOID)Previous) {
                HANDLE Handle = PointerToHandle((LINEAR)Previous);
                if (Handle != 0) return Handle;
            }
            return 0;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Select a pen into a graphics context.
 *
 * @param Parameter Pointer to GCSELECT containing GC and pen handles.
 * @return UINT Previous pen handle, or 0.
 */
UINT SysCall_SelectPen(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;

    SAFE_USE_INPUT_POINTER(Sel, GCSELECT) {
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Sel->GC);
        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Context, KOID_GRAPHICSCONTEXT, TRUE) {
            if (Sel->Object != 0) {
                LPPEN Pen = (LPPEN)HandleToPointer(Sel->Object);
                SAFE_USE_VALID_ID(Pen, KOID_PEN) {
                    HANDLE Previous = SelectPen((HANDLE)Context, (HANDLE)Pen);
                    SAFE_USE_VALID((LPVOID)Previous) {
                        HANDLE Handle = PointerToHandle((LINEAR)Previous);
                        if (Handle != 0) return Handle;
                    }
                    return 0;
                }
                return 0;
            }

            HANDLE Previous = SelectPen((HANDLE)Context, NULL);
            SAFE_USE_VALID((LPVOID)Previous) {
                HANDLE Handle = PointerToHandle((LINEAR)Previous);
                if (Handle != 0) return Handle;
            }
            return 0;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set a pixel within a graphics context.
 *
 * @param Parameter Pointer to PIXEL_INFO containing the draw parameters.
 * @return UINT TRUE on success.
 */
UINT SysCall_SetPixel(UINT Parameter) {
    LPPIXEL_INFO PixelInfo = (LPPIXEL_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(PixelInfo, PIXEL_INFO) {
        HANDLE OriginalGC = PixelInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (!ProcessAccessCanCurrentProcessTargetObject(Context, TRUE)) {
                PixelInfo->GC = OriginalGC;
                return 0;
            }

            PixelInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)KernelSetPixel(PixelInfo);
            PixelInfo->GC = OriginalGC;
            return Result;
        }

        PixelInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a pixel from a graphics context.
 *
 * @param Parameter Pointer to PIXEL_INFO containing coordinates.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetPixel(UINT Parameter) {
    LPPIXEL_INFO PixelInfo = (LPPIXEL_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(PixelInfo, PIXEL_INFO) {
        HANDLE OriginalGC = PixelInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (!ProcessAccessCanCurrentProcessTargetObject(Context, TRUE)) {
                PixelInfo->GC = OriginalGC;
                return 0;
            }

            PixelInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)KernelGetPixel(PixelInfo);
            PixelInfo->GC = OriginalGC;
            return Result;
        }

        PixelInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw a line using the current graphics context pen.
 *
 * @param Parameter Pointer to LINE_INFO with GC handle and coordinates.
 * @return UINT TRUE on success.
 */
UINT SysCall_Line(UINT Parameter) {
    LPLINE_INFO LineInfo = (LPLINE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(LineInfo, LINE_INFO) {
        HANDLE OriginalGC = LineInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (!ProcessAccessCanCurrentProcessTargetObject(Context, TRUE)) {
                LineInfo->GC = OriginalGC;
                return 0;
            }

            LineInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)Line(LineInfo);
            LineInfo->GC = OriginalGC;
            return Result;
        }

        LineInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw a rectangle using the current pen and brush.
 *
 * @param Parameter Pointer to RECT_INFO with GC handle and rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_Rectangle(UINT Parameter) {
    LPRECT_INFO RectInfo = (LPRECT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(RectInfo, RECT_INFO) {
        HANDLE OriginalGC = RectInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (!ProcessAccessCanCurrentProcessTargetObject(Context, TRUE)) {
                RectInfo->GC = OriginalGC;
                return 0;
            }

            RectInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)KernelRectangle(RectInfo);
            RectInfo->GC = OriginalGC;
            return Result;
        }

        RectInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw one text string using the current graphics context colors.
 *
 * @param Parameter Pointer to TEXT_DRAW_INFO with GC handle and text parameters.
 * @return UINT TRUE on success.
 */
UINT SysCall_DrawText(UINT Parameter) {
    LPTEXT_DRAW_INFO TextInfo = (LPTEXT_DRAW_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(TextInfo, TEXT_DRAW_INFO) {
        GFX_TEXT_DRAW_INFO DrawInfo;
        HANDLE OriginalGC = TextInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Context, KOID_GRAPHICSCONTEXT, TRUE) {
            if (TextInfo->Text == NULL || TextInfo->Font != 0) {
                return 0;
            }

            SAFE_USE_VALID(TextInfo->Text) {
                DrawInfo = (GFX_TEXT_DRAW_INFO){
                    .Header = TextInfo->Header,
                    .GC = (HANDLE)Context,
                    .X = TextInfo->X,
                    .Y = TextInfo->Y,
                    .Text = TextInfo->Text,
                    .Font = NULL
                };
                return (UINT)DesktopDrawText(&DrawInfo);
            }
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Measure one text string using the default font.
 *
 * @param Parameter Pointer to TEXT_MEASURE_INFO.
 * @return UINT TRUE on success.
 */
UINT SysCall_MeasureText(UINT Parameter) {
    LPTEXT_MEASURE_INFO TextInfo = (LPTEXT_MEASURE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(TextInfo, TEXT_MEASURE_INFO) {
        GFX_TEXT_MEASURE_INFO MeasureInfo;

        if (TextInfo->Text == NULL || TextInfo->Font != 0) {
            return 0;
        }

        SAFE_USE_VALID(TextInfo->Text) {
            MeasureInfo = (GFX_TEXT_MEASURE_INFO){
                .Header = TextInfo->Header,
                .Text = TextInfo->Text,
                .Font = NULL,
                .Width = 0,
                .Height = 0
            };

            if (DesktopMeasureText(&MeasureInfo) == FALSE) {
                return 0;
            }

            TextInfo->Width = MeasureInfo.Width;
            TextInfo->Height = MeasureInfo.Height;
            return 1;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the latest mouse delta values.
 *
 * @param Parameter Linear address of a POINT structure.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetMousePos(UINT Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    U32 UX, UY;

    SAFE_USE_VALID(Point) {
        UX = GetMouseDriver()->Command(DF_MOUSE_GETDELTAX, 0);
        UY = GetMouseDriver()->Command(DF_MOUSE_GETDELTAY, 0);

        Point->X = *((I32*)&UX);
        Point->Y = *((I32*)&UY);

        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set the mouse cursor position.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetMousePos(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the state of mouse buttons.
 *
 * @param Parameter Reserved.
 * @return UINT Bitmask of button states.
 */
UINT SysCall_GetMouseButtons(UINT Parameter) {
    UNUSED(Parameter);
    return GetMouseDriver()->Command(DF_MOUSE_GETBUTTONS, 0);
}

/************************************************************************/

/**
 * @brief Show the mouse cursor.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ShowMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Hide the mouse cursor.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_HideMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Confine the mouse cursor to a rectangle.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ClipMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Draw one themed window background into one graphics context.
 *
 * @param Parameter Pointer to WINDOW_BACKGROUND_INFO.
 * @return UINT TRUE on success.
 */
UINT SysCall_DrawWindowBackground(UINT Parameter) {
    LPWINDOW_BACKGROUND_INFO BackgroundInfo = (LPWINDOW_BACKGROUND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(BackgroundInfo, WINDOW_BACKGROUND_INFO) {
        HANDLE OriginalGC = BackgroundInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);
        LPWINDOW Window = NULL;

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (BackgroundInfo->Window != NULL) {
                Window = (LPWINDOW)HandleToPointer(BackgroundInfo->Window);
                if (Window == NULL || Window->TypeID != KOID_WINDOW) {
                    return 0;
                }

                if (!ProcessAccessCanCurrentProcessTargetObject(Window, TRUE)) {
                    return 0;
                }
            }

            if (!ProcessAccessCanCurrentProcessTargetObject(Context, TRUE)) {
                return 0;
            }

            return (UINT)DrawWindowBackground((HANDLE)Window, (HANDLE)Context, &(BackgroundInfo->Rect), BackgroundInfo->ThemeToken);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Capture mouse input to a specific window.
 *
 * @param Parameter Target window handle.
 * @return UINT Captured window handle on success, 0 on failure.
 */
UINT SysCall_CaptureMouse(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer((HANDLE)Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Window, KOID_WINDOW, TRUE) {
        return (UINT)CaptureMouse((HANDLE)Window);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Release mouse capture.
 *
 * @param Parameter Reserved.
 * @return UINT TRUE on success, FALSE on failure.
 */
UINT SysCall_ReleaseMouse(UINT Parameter) {
    UNUSED(Parameter);
    return (UINT)ReleaseMouse();
}

/************************************************************************/

/**
 * @brief Authenticate a user and create a session.
 *
 * @param Parameter Pointer to LOGIN_INFO credentials.
 * @return UINT TRUE on success.
 */
UINT SysCall_Login(UINT Parameter) {
    LPLOGIN_INFO LoginInfo = (LPLOGIN_INFO)Parameter;
    UINT WaitRemaining;

    SAFE_USE_INPUT_POINTER(LoginInfo, LOGIN_INFO) {
        LPUSER_ACCOUNT Account = FindAccount(LoginInfo->UserName);
        if (Account == NULL) return FALSE;

        WaitRemaining = 0;
        if (!CanAttemptUserAuthentication(Account, &WaitRemaining)) {
            return FALSE;
        }

        if (!VerifyPassword(LoginInfo->Password, Account->PasswordHash)) {
            (void)RecordUserAuthenticationFailure(Account, NULL);
            return FALSE;
        }

        RecordUserAuthenticationSuccess(Account);

        LPUSER_SESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
        if (Session == NULL) {
            return FALSE;
        }

        GetLocalTime(&Account->LastLoginTime);
        SetCurrentSession(Session);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Terminate the current user session.
 *
 * @param Parameter Reserved.
 * @return UINT TRUE on success.
 */
UINT SysCall_Logout(UINT Parameter) {
    UNUSED(Parameter);
    LPUSER_SESSION Session = GetCurrentSession();
    if (Session == NULL) {
        return FALSE;
    }

    DestroyUserSession(Session);
    SetCurrentSession(NULL);
    return TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieve information about the current user session.
 *
 * @param Parameter Pointer to CURRENT_USER_INFO buffer.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetCurrentUser(UINT Parameter) {
    LPCURRENT_USER_INFO UserInfo = (LPCURRENT_USER_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(UserInfo, CURRENT_USER_INFO) {
        LPUSER_ACCOUNT Account = GetCurrentUser();
        if (Account == NULL) return FALSE;

        LPUSER_SESSION Session = GetCurrentSession();
        if (Session == NULL) return FALSE;

        StringCopy(UserInfo->UserName, Account->UserName);
        UserInfo->Privilege = Account->Privilege;
        UserInfo->LoginTime = U64_FromUINT(GetSystemTime());
        UserInfo->SessionID = Session->SessionID;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Change the password of the current user.
 *
 * @param Parameter Pointer to PASSWORD_CHANGE data.
 * @return UINT TRUE on success.
 */
UINT SysCall_ChangePassword(UINT Parameter) {
    LPPASSWORD_CHANGE PasswordChange = (LPPASSWORD_CHANGE)Parameter;

    SAFE_USE_INPUT_POINTER(PasswordChange, PASSWORD_CHANGE) {
        LPUSER_ACCOUNT Account = GetCurrentUser();
        if (Account == NULL) {
            return FALSE;
        }

        return ChangeUserPassword(Account->UserName, PasswordChange->OldPassword, PasswordChange->NewPassword);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Create a new user account.
 *
 * @param Parameter Pointer to USER_CREATE_INFO data.
 * @return UINT TRUE on success.
 */
UINT SysCall_CreateUser(UINT Parameter) {
    LPUSER_CREATE_INFO CreateInfo = (LPUSER_CREATE_INFO)Parameter;
    UINT AccountCount;

    SAFE_USE_INPUT_POINTER(CreateInfo, USER_CREATE_INFO) {
        LPUSER_ACCOUNT CurrentAccount = GetCurrentUser();
        AccountCount = GetAccountCount();

        if (AccountCount == 0) {
            if (CreateInfo->Privilege != EXOS_PRIVILEGE_ADMIN) {
                return FALSE;
            }
        } else if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        LPUSER_ACCOUNT NewAccount = CreateAccount(CreateInfo->UserName, CreateInfo->Password, CreateInfo->Privilege);
        return (NewAccount != NULL) ? TRUE : FALSE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Delete an existing user account.
 *
 * @param Parameter Pointer to USER_DELETE_INFO data.
 * @return UINT TRUE on success.
 */
UINT SysCall_DeleteUser(UINT Parameter) {
    LPUSER_DELETE_INFO DeleteInfo = (LPUSER_DELETE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(DeleteInfo, USER_DELETE_INFO) {
        LPUSER_ACCOUNT CurrentAccount = GetCurrentUser();
        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        return DeleteAccount(DeleteInfo->UserName);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Enumerate existing user accounts.
 *
 * @param Parameter Pointer to USER_LIST_INFO buffer.
 * @return UINT TRUE on success.
 */
UINT SysCall_ListUsers(UINT Parameter) {
    LPUSER_LIST_INFO ListInfo = (LPUSER_LIST_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(ListInfo, USER_LIST_INFO) {
        LPUSER_ACCOUNT CurrentAccount = GetCurrentUser();
        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        ListInfo->UserCount = 0;
        LPLIST AccountList = GetAccountList();
        LPUSER_ACCOUNT Account =
            (LPUSER_ACCOUNT)(AccountList != NULL ? AccountList->First : NULL);

        while (Account != NULL && ListInfo->UserCount < ListInfo->MaxUsers) {
            StringCopy(ListInfo->UserNames[ListInfo->UserCount], Account->UserName);
            ListInfo->UserCount++;
            Account = (LPUSER_ACCOUNT)Account->Next;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/
// Socket syscalls

/**
 * @brief Create a socket and return its descriptor.
 *
 * @param Parameter Pointer to SOCKET_CREATE_INFO request.
 * @return UINT Socket descriptor or error code.
 */
UINT SysCall_SocketCreate(UINT Parameter) {
    LPSOCKET_CREATE_INFO Info = (LPSOCKET_CREATE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CREATE_INFO) {
        return SocketCreate(Info->AddressFamily, Info->SocketType, Info->Protocol);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Bind a socket to a local address.
 *
 * @param Parameter Pointer to SOCKET_BIND_INFO request.
 * @return UINT Result code from SocketBind.
 */
UINT SysCall_SocketBind(UINT Parameter) {
    LPSOCKET_BIND_INFO Info = (LPSOCKET_BIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_BIND_INFO) {
        return SocketBind(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Transition a socket into listening mode.
 *
 * @param Parameter Pointer to SOCKET_LISTEN_INFO request.
 * @return UINT Result code from SocketListen.
 */
UINT SysCall_SocketListen(UINT Parameter) {
    LPSOCKET_LISTEN_INFO Info = (LPSOCKET_LISTEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_LISTEN_INFO) {
        return SocketListen(Info->SocketHandle, Info->Backlog);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Accept a pending connection on a listening socket.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketAccept.
 */
UINT SysCall_SocketAccept(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketAccept(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Connect a socket to a remote endpoint.
 *
 * @param Parameter Pointer to SOCKET_CONNECT_INFO request.
 * @return UINT Result code from SocketConnect.
 */
UINT SysCall_SocketConnect(UINT Parameter) {
    LPSOCKET_CONNECT_INFO Info = (LPSOCKET_CONNECT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CONNECT_INFO) {
        return SocketConnect(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Send data on a connected socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO request.
 * @return UINT Bytes sent or error code.
 */
UINT SysCall_SocketSend(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSend(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Receive data from a connected socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO buffer.
 * @return UINT Bytes received or error code.
 */
UINT SysCall_SocketReceive(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceive(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Send data to a specific address using a datagram socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO request.
 * @return UINT Bytes sent or error code.
 */
UINT SysCall_SocketSendTo(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSendTo(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Receive data along with the sender address on a datagram socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO buffer.
 * @return UINT Bytes received or error code.
 */
UINT SysCall_SocketReceiveFrom(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        U32 AddressLength = Info->AddressLength;
        UINT Result = SocketReceiveFrom(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, &AddressLength);
        Info->AddressLength = AddressLength;
        return Result;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Close a socket descriptor.
 *
 * @param Parameter Socket descriptor.
 * @return UINT Result code from SocketClose.
 */
UINT SysCall_SocketClose(UINT Parameter) {
    SOCKET_HANDLE SocketHandle = (SOCKET_HANDLE)Parameter;
    return SocketClose(SocketHandle);
}

/************************************************************************/

/**
 * @brief Shut down parts of a socket connection.
 *
 * @param Parameter Pointer to SOCKET_SHUTDOWN_INFO request.
 * @return UINT Result code from SocketShutdown.
 */
UINT SysCall_SocketShutdown(UINT Parameter) {
    LPSOCKET_SHUTDOWN_INFO Info = (LPSOCKET_SHUTDOWN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_SHUTDOWN_INFO) {
        return SocketShutdown(Info->SocketHandle, Info->How);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Retrieve a socket option value.
 *
 * @param Parameter Pointer to SOCKET_OPTION_INFO buffer.
 * @return UINT Result code from SocketGetOption.
 */
UINT SysCall_SocketGetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        U32 OptionLength = Info->OptionLength;
        UINT Result = SocketGetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, &OptionLength);
        Info->OptionLength = OptionLength;
        return Result;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Set a socket option value.
 *
 * @param Parameter Pointer to SOCKET_OPTION_INFO request.
 * @return UINT Result code from SocketSetOption.
 */
UINT SysCall_SocketSetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketSetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, Info->OptionLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Retrieve the address of the connected peer.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketGetPeerName.
 */
UINT SysCall_SocketGetPeerName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetPeerName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Retrieve the local address of a socket.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketGetSocketName.
 */
UINT SysCall_SocketGetSocketName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetSocketName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

UINT SystemCallHandler(U32 Function, UINT Parameter) {
    if (Function >= SYSCALL_Last || SysCallTable[Function].Function == NULL) {
        return 0;
    }

    LPUSER_ACCOUNT CurrentUser = GetCurrentUser();
    U32 RequiredPrivilege = SysCallTable[Function].Privilege;

    if (CurrentUser == NULL) {
        if (RequiredPrivilege != EXOS_PRIVILEGE_USER) {
            return 0;
        }
    } else {
        if (CurrentUser->Privilege > RequiredPrivilege) {
            return 0;
        }
    }

    return SysCallTable[Function].Function(Parameter);
}
