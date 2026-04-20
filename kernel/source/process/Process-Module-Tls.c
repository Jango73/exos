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


    Process executable module TLS

\************************************************************************/

#include "process/Process-Module.h"

#include "log/Log.h"

/***************************************************************************/

/**
 * @brief Initialize TLS state for every existing task of one module binding.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @return TRUE when TLS state is ready for every task.
 */
BOOL InitializeProcessModuleTls(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding) {
    LPEXECUTABLE_TLS_INFO Tls = NULL;
    LINEAR TemplateBase = 0;

    if (Process == NULL || Binding == NULL || Binding->Image == NULL) {
        return FALSE;
    }

    Tls = &(Binding->Image->Metadata.Tls);
    if (Tls->Present == FALSE) {
        Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_TLS_REGISTERED;
        return TRUE;
    }

    if (Tls->TotalSize == 0 || Tls->TemplateSize > Tls->TotalSize) {
        WARNING(TEXT("Invalid module TLS layout"));
        return FALSE;
    }

    if (Tls->TemplateSize != 0) {
        TemplateBase = MapProcessModuleBindingAddress(Binding, Tls->TemplateAddress);
        if (TemplateBase == 0) {
            WARNING(TEXT("Module TLS template is not mapped"));
            return FALSE;
        }
    }

    if (!TaskInstallProcessModuleTlsBlocks(Process,
                                           Binding,
                                           TemplateBase,
                                           Tls->TemplateSize,
                                           Tls->TotalSize,
                                           Tls->Alignment)) {
        WARNING(TEXT("Module TLS expansion failed"));
        return FALSE;
    }

    Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_TLS_REGISTERED;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize module TLS blocks for one newly created task.
 *
 * @param Process Owning process.
 * @param Task Target task.
 * @return TRUE when every loaded module TLS block is available to the task.
 */
BOOL InitializeTaskProcessModuleTlsBindings(LPPROCESS Process, LPTASK Task) {
    BOOL Result = TRUE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Process->ModuleBindings == NULL) {
                return TRUE;
            }

            for (LPLISTNODE Node = Process->ModuleBindings->First; Node != NULL; Node = Node->Next) {
                LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)Node;
                LPEXECUTABLE_TLS_INFO Tls = NULL;
                LINEAR TemplateBase = 0;

                if (Binding == NULL || Binding->Image == NULL) {
                    continue;
                }

                if ((Binding->StateFlags & EXECUTABLE_MODULE_BINDING_STATE_TLS_REGISTERED) == 0) {
                    continue;
                }

                Tls = &(Binding->Image->Metadata.Tls);
                if (Tls->Present == FALSE) {
                    continue;
                }

                if (Tls->TemplateSize != 0) {
                    TemplateBase = MapProcessModuleBindingAddress(Binding, Tls->TemplateAddress);
                    if (TemplateBase == 0) {
                        Result = FALSE;
                        break;
                    }
                }

                if (!TaskEnsureModuleTlsBlock(Task,
                                              Binding,
                                              TemplateBase,
                                              Tls->TemplateSize,
                                              Tls->TotalSize,
                                              Tls->Alignment)) {
                    Result = FALSE;
                    break;
                }
            }

            if (Result == FALSE) {
                TaskReleaseModuleTlsBlocks(Task);
            }
        }
    }

    return Result;
}
