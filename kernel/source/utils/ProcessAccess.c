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


    Process ownership access helpers

\************************************************************************/

#include "utils/ProcessAccess.h"

#include "core/Kernel.h"
#include "user/Account.h"
#include "process/Process.h"
#include "process/Task.h"

/************************************************************************/

/**
 * @brief Resolve the owning process for one process-owned kernel object.
 * @param Object Kernel object to inspect.
 * @param OwnerProcessOut Receives the owning process when one exists.
 * @return TRUE when the object resolves to one owning process.
 */
static BOOL ProcessAccessGetOwnerProcessForObject(LPVOID Object, LPPROCESS* OwnerProcessOut) {
    LPPROCESS OwnerProcess = NULL;
    LPOBJECT KernelObject = (LPOBJECT)Object;

    if (OwnerProcessOut == NULL) {
        return FALSE;
    }

    *OwnerProcessOut = NULL;

    SAFE_USE_VALID(KernelObject) {
        if (KernelObject->TypeID == KOID_PROCESS) {
            OwnerProcess = (LPPROCESS)KernelObject;
        } else {
            OwnerProcess = KernelObject->OwnerProcess;
        }
    }

    SAFE_USE_VALID_ID(OwnerProcess, KOID_PROCESS) {
        *OwnerProcessOut = OwnerProcess;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Resolve the effective owner user identifier for one process.
 * @param Process Process to inspect.
 * @param UserIDOut Receives the effective user identifier.
 * @return TRUE when the process exposes a valid effective owner.
 */
BOOL ProcessAccessGetEffectiveUserID(LPPROCESS Process, U64* UserIDOut) {
    if (UserIDOut == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Process->Session, KOID_USER_SESSION) {
            *UserIDOut = Process->Session->UserID;
            return TRUE;
        }

        if (U64_Cmp(Process->UserID, U64_Make(0, 0)) != 0) {
            *UserIDOut = Process->UserID;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Test whether one process runs with kernel CPU privilege.
 * @param Process Process to inspect.
 * @return TRUE when the process runs with kernel privilege.
 */
BOOL ProcessAccessIsKernelProcess(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        return Process->Privilege == CPU_PRIVILEGE_KERNEL;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Test whether one process resolves to administrator privilege.
 * @param Process Process to inspect.
 * @return TRUE when the process is kernel or belongs to an administrator.
 */
BOOL ProcessAccessIsAdministratorProcess(LPPROCESS Process) {
    U64 UserID = U64_Make(0, 0);
    LPUSER_ACCOUNT Account = NULL;

    if (ProcessAccessIsKernelProcess(Process)) {
        return TRUE;
    }

    if (!ProcessAccessGetEffectiveUserID(Process, &UserID)) {
        return FALSE;
    }

    Account = FindAccountByID(UserID);
    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        return Account->Privilege == EXOS_PRIVILEGE_ADMIN;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Test whether two processes resolve to the same effective owner.
 * @param Caller Calling process.
 * @param Target Target process.
 * @return TRUE when both processes belong to the same user.
 */
BOOL ProcessAccessIsSameUser(LPPROCESS Caller, LPPROCESS Target) {
    U64 CallerUserID = U64_Make(0, 0);
    U64 TargetUserID = U64_Make(0, 0);

    if (!ProcessAccessGetEffectiveUserID(Caller, &CallerUserID)) {
        return FALSE;
    }

    if (!ProcessAccessGetEffectiveUserID(Target, &TargetUserID)) {
        return FALSE;
    }

    return U64_Cmp(CallerUserID, TargetUserID) == 0;
}

/************************************************************************/

/**
 * @brief Test whether one caller may target one process.
 * @param Caller Calling process.
 * @param Target Target process.
 * @param AllowAdminOverride Whether administrator/kernel override is accepted.
 * @return TRUE when access is granted.
 */
BOOL ProcessAccessCanTargetProcess(LPPROCESS Caller, LPPROCESS Target, BOOL AllowAdminOverride) {
    SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Target, KOID_PROCESS) {
            if (Caller == Target) {
                return TRUE;
            }

            if (ProcessAccessIsSameUser(Caller, Target)) {
                return TRUE;
            }

            if (AllowAdminOverride && ProcessAccessIsAdministratorProcess(Caller)) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Test whether one caller may target one task.
 * @param Caller Calling process.
 * @param TargetTask Target task.
 * @param AllowAdminOverride Whether administrator/kernel override is accepted.
 * @return TRUE when access is granted.
 */
BOOL ProcessAccessCanTargetTask(LPPROCESS Caller, LPTASK TargetTask, BOOL AllowAdminOverride) {
    SAFE_USE_VALID_ID(TargetTask, KOID_TASK) {
        return ProcessAccessCanTargetProcess(Caller, TargetTask->OwnerProcess, AllowAdminOverride);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Test whether one caller may target one owned kernel object.
 * @param Caller Calling process.
 * @param Object Kernel object to inspect.
 * @param AllowAdminOverride Whether administrator/kernel override is accepted.
 * @return TRUE when access is granted or the object has no process owner model.
 */
BOOL ProcessAccessCanTargetObject(LPPROCESS Caller, LPVOID Object, BOOL AllowAdminOverride) {
    LPPROCESS OwnerProcess = NULL;

    if (!ProcessAccessGetOwnerProcessForObject(Object, &OwnerProcess)) {
        return TRUE;
    }

    return ProcessAccessCanTargetProcess(Caller, OwnerProcess, AllowAdminOverride);
}

/************************************************************************/

/**
 * @brief Test whether the current process may target one owned kernel object.
 * @param Object Kernel object to inspect.
 * @param AllowAdminOverride Whether administrator/kernel override is accepted.
 * @return TRUE when access is granted.
 */
BOOL ProcessAccessCanCurrentProcessTargetObject(LPVOID Object, BOOL AllowAdminOverride) {
    return ProcessAccessCanTargetObject(GetCurrentProcess(), Object, AllowAdminOverride);
}
