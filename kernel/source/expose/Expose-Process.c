
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


    Script Exposure Helpers

\************************************************************************/

#include "expose/Exposed.h"

#include "sync/Mutex.h"
#include "process/Process.h"

/************************************************************************/

/************************************************************************/

/**
 * @brief Counts the processes visible to the calling context.
 * @param ProcessList Process list to scan.
 * @return Number of visible processes.
 */
static UINT ProcessGetVisibleCount(LPLIST ProcessList) {
    UINT Count = 0;
    BOOL IsKernelOrAdmin = ExposeIsKernelCaller() || ExposeIsAdminCaller();

    if (ProcessList == NULL) {
        return 0;
    }

    LockMutex(MUTEX_PROCESS, INFINITY);

    for (LPLISTNODE Node = ProcessList->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            if (Process == &KernelProcess && IsKernelOrAdmin == FALSE) {
                continue;
            }

            Count++;
        }
    }

    UnlockMutex(MUTEX_PROCESS);

    return Count;
}

/************************************************************************/

/**
 * @brief Retrieves a visible process by index for the calling context.
 * @param ProcessList Process list to scan.
 * @param Index Visible index requested by the caller.
 * @return Process pointer or NULL when out of range.
 */
static LPPROCESS ProcessGetVisibleByIndex(LPLIST ProcessList, UINT Index) {
    LPPROCESS Found = NULL;
    UINT MatchIndex = 0;
    BOOL IsKernelOrAdmin = ExposeIsKernelCaller() || ExposeIsAdminCaller();

    if (ProcessList == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_PROCESS, INFINITY);

    for (LPLISTNODE Node = ProcessList->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            if (Process == &KernelProcess && IsKernelOrAdmin == FALSE) {
                continue;
            }

            if (MatchIndex == Index) {
                Found = Process;
                break;
            }

            MatchIndex++;
        }
    }

    UnlockMutex(MUTEX_PROCESS);

    return Found;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a process exposed to the script engine.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent  Handle to the process instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    LPPROCESS Process = (LPPROCESS)Parent;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        EXPOSE_PROPERTY_GUARD();
        BOOL IsKernelOrAdmin = ExposeIsKernelCaller() || ExposeIsAdminCaller();
        if (Process == &KernelProcess && IsKernelOrAdmin == FALSE) {
            return SCRIPT_ERROR_UNAUTHORIZED;
        }

        EXPOSE_BIND_INTEGER("handle", (UINT)(LPVOID)Process);
        EXPOSE_BIND_INTEGER("status", Process->Status);
        EXPOSE_BIND_INTEGER("flags", Process->Flags);
        EXPOSE_BIND_INTEGER("exitCode", Process->ExitCode);
        EXPOSE_BIND_STRING("fileName", Process->FileName);
        EXPOSE_BIND_STRING("commandLine", Process->CommandLine);
        EXPOSE_BIND_STRING("workFolder", Process->WorkFolder);

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("pageDirectory"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = (INT)Process->PageDirectory;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("heapBase"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = (INT)Process->HeapBase;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("heapSize"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = (INT)Process->HeapSize;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("task"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = Process;
            OutValue->HostDescriptor = &TaskArrayDescriptor;
            OutValue->HostContext = NULL;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed kernel process array.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent Handle to the process list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST ProcessList = (LPLIST)Parent;
    if (ProcessList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ProcessGetVisibleCount(ProcessList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a process from the exposed kernel process array.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent Handle to the process list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting process handle
 * @return SCRIPT_OK when the process exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST ProcessList = (LPLIST)Parent;
    if (ProcessList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPPROCESS Process = ProcessGetVisibleByIndex(ProcessList, (UINT)Index);
    if (Process == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_SET_HOST_HANDLE(Process, &ProcessDescriptor, NULL, FALSE);
    return SCRIPT_OK;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR ProcessDescriptor = {
    ProcessGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ProcessArrayDescriptor = {
    ProcessArrayGetProperty,
    ProcessArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
