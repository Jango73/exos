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


    Task access mediation

\************************************************************************/

#include "process/Task-Access.h"

#include "core/Kernel.h"
#include "utils/ProcessAccess.h"

/************************************************************************/

/**
 * @brief Kill one task only when the caller may target it.
 *
 * @param Caller Calling process.
 * @param Task Task to kill.
 * @param AllowAdminOverride Whether administrator override is accepted.
 * @return TRUE when the task was killed.
 */
BOOL KillTaskForCaller(LPPROCESS Caller, LPTASK Task, BOOL AllowAdminOverride) {
    if (!ProcessAccessCanTargetTask(Caller, Task, AllowAdminOverride)) {
        return FALSE;
    }

    return KernelKillTask(Task);
}

/************************************************************************/

/**
 * @brief Kill one task for the current process when access is granted.
 *
 * @param Task Task to kill.
 * @param AllowAdminOverride Whether administrator override is accepted.
 * @return TRUE when the task was killed.
 */
BOOL KillTaskForCurrentProcess(LPTASK Task, BOOL AllowAdminOverride) {
    return KillTaskForCaller(GetCurrentProcess(), Task, AllowAdminOverride);
}
