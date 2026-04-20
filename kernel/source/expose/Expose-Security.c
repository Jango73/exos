
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


    Script Exposure Helpers - Security

\************************************************************************/

#include "expose/Exposed.h"

#include "core/Kernel.h"
#include "core/Security.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "utils/Helpers.h"
#include "utils/ProcessAccess.h"
#include "user/Account.h"

/************************************************************************/

/**
 * @brief Retrieves the calling process for exposure access checks.
 * @return Process pointer or NULL when no current process is available.
 */
LPPROCESS ExposeGetCallerProcess(void) {
    return GetCurrentProcess();
}

/************************************************************************/

/**
 * @brief Retrieves the calling user for exposure access checks.
 * @return User account pointer or NULL when no user is logged in.
 */
LPUSER_ACCOUNT ExposeGetCallerUser(void) {
    return GetCurrentUser();
}

/************************************************************************/

/**
 * @brief Tests whether the calling process runs with kernel privilege.
 * @return TRUE when the calling process has kernel privilege.
 */
BOOL ExposeIsKernelCaller(void) {
    return ProcessAccessIsKernelProcess(ExposeGetCallerProcess());
}

/************************************************************************/

/**
 * @brief Tests whether the calling user has administrator privilege.
 * @return TRUE when the calling user is an administrator.
 */
BOOL ExposeIsAdminCaller(void) {
    return ProcessAccessIsAdministratorProcess(ExposeGetCallerProcess());
}

/************************************************************************/

/**
 * @brief Tests whether two processes belong to the same user session.
 * @param Caller Calling process.
 * @param Target Target process.
 * @return TRUE when both processes belong to the same user.
 */
BOOL ExposeIsSameUser(LPPROCESS Caller, LPPROCESS Target) {
    return ProcessAccessIsSameUser(Caller, Target);
}

/************************************************************************/

/**
 * @brief Tests whether the caller matches the target process.
 * @param Caller Calling process.
 * @param Target Target process.
 * @return TRUE when both pointers refer to the same process.
 */
BOOL ExposeIsOwnerProcess(LPPROCESS Caller, LPPROCESS Target) {
    SAFE_USE_VALID_ID(Caller, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Target, KOID_PROCESS) {
            return Caller == Target;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Determines whether a caller can access a target process.
 * @param Caller Calling process.
 * @param Target Target process (can be NULL when no target is required).
 * @param RequiredAccess Access level flags.
 * @return TRUE when access is granted.
 */
BOOL ExposeCanReadProcess(LPPROCESS Caller, LPPROCESS Target, UINT RequiredAccess) {
    if (RequiredAccess == EXPOSE_ACCESS_PUBLIC) {
        return TRUE;
    }

    if ((RequiredAccess & EXPOSE_ACCESS_KERNEL) != 0u) {
        if (ProcessAccessIsKernelProcess(Caller)) {
            return TRUE;
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_ADMIN) != 0u) {
        if (ProcessAccessIsAdministratorProcess(Caller)) {
            return TRUE;
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_SAME_USER) != 0u) {
        if (ExposeIsSameUser(Caller, Target)) {
            return TRUE;
        }
    }

    if ((RequiredAccess & EXPOSE_ACCESS_OWNER_PROCESS) != 0u) {
        if (ExposeIsOwnerProcess(Caller, Target)) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/
