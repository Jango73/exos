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


    Process executable module symbol access

\************************************************************************/

#include "process/Process-Module.h"

#include "exec/Executable.h"
#include "log/Log.h"

/***************************************************************************/

/**
 * @brief Resolve one exported symbol from a process module binding.
 *
 * @param Process Owning process.
 * @param Binding Module binding owned by the process.
 * @param Name Exported symbol name.
 * @param Address Receives the installed user address.
 * @return TRUE when the symbol resolves.
 */
BOOL ResolveProcessModuleBindingExport(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPCSTR Name,
    LINEAR* Address) {
    BOOL Result = FALSE;

    if (Name == NULL || Address == NULL) {
        return FALSE;
    }

    *Address = 0;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process == Process && Binding->Image != NULL &&
                (Binding->StateFlags & EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED) != 0) {
                Result = ResolveExecutableMappedSymbol(
                    &(Binding->Image->Metadata),
                    MapProcessModuleBindingAddress,
                    Binding,
                    Name,
                    Address);
            }

            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}
