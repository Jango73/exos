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


    Executable module system calls

\************************************************************************/

#include "system/SYSCall.h"

#include "core/Kernel.h"
#include "exec/ExecutableModule.h"
#include "fs/File.h"
#include "log/Log.h"
#include "process/Process-Module.h"
#include "process/Schedule.h"
#include "utils/ProcessAccess.h"

/***************************************************************************/

/**
 * @brief Load one executable module into the current process.
 *
 * @param Path Module file path.
 * @return Module binding pointer or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING LoadCurrentProcessModuleByPath(LPCSTR Path) {
    LPEXECUTABLE_MODULE_IMAGE Image = NULL;
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;
    FILE_OPEN_INFO OpenInfo;
    LPFILE File = NULL;
    LPPROCESS Process = GetCurrentProcess();

    if (Path == NULL || Process == NULL) {
        return NULL;
    }

    OpenInfo.Header.Size = sizeof(OpenInfo);
    OpenInfo.Header.Version = EXOS_ABI_VERSION;
    OpenInfo.Header.Flags = 0;
    OpenInfo.Name = Path;
    OpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&OpenInfo);
    SAFE_USE_VALID_ID(File, KOID_FILE) {
        Image = AcquireExecutableModuleImage(File);
        CloseFile(File);
        File = NULL;
    }

    if (File != NULL) {
        CloseFile(File);
    }

    SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
        Binding = AcquireProcessModuleBinding(Process, Image);
        ReleaseExecutableModuleImage(Image);

        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            if (InstallProcessModuleBindingSegments(Process, Binding)) {
                return Binding;
            }

            ReleaseProcessModuleBinding(Binding);
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Load one module into the caller process.
 *
 * @param Parameter Pointer to MODULE_LOAD_INFO.
 * @return DF_RETURN_SUCCESS on success.
 */
UINT SysCall_LoadModule(UINT Parameter) {
    LPMODULE_LOAD_INFO Info = (LPMODULE_LOAD_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MODULE_LOAD_INFO) {
        SAFE_USE_VALID((LPCSTR)Info->Path) {
            LPEXECUTABLE_MODULE_BINDING Binding = LoadCurrentProcessModuleByPath(Info->Path);

            SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
                HANDLE Handle = PointerToHandle((LINEAR)Binding);

                if (Handle == 0) {
                    ReleaseProcessModuleBinding(Binding);
                    return DF_RETURN_GENERIC;
                }

                Info->Module = Handle;
                Info->ModuleIdentifierHigh = U64_High32(Binding->InstanceID);
                Info->ModuleIdentifierLow = U64_Low32(Binding->InstanceID);
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_GENERIC;
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief Resolve one exported symbol from a loaded module.
 *
 * @param Parameter Pointer to MODULE_SYMBOL_INFO.
 * @return DF_RETURN_SUCCESS on success.
 */
UINT SysCall_GetModuleSymbol(UINT Parameter) {
    LPMODULE_SYMBOL_INFO Info = (LPMODULE_SYMBOL_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MODULE_SYMBOL_INFO) {
        Info->Address = 0;

        SAFE_USE_VALID((LPCSTR)Info->Name) {
            LPEXECUTABLE_MODULE_BINDING Binding =
                (LPEXECUTABLE_MODULE_BINDING)HandleToPointer(Info->Module);

            SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Binding, KOID_EXECUTABLE_MODULE_BINDING, FALSE) {
                if (ResolveProcessModuleBindingExport(GetCurrentProcess(), Binding, Info->Name, &(Info->Address))) {
                    return DF_RETURN_SUCCESS;
                }

                return DF_RETURN_GENERIC;
            }
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief Reject runtime unload for one module owned by the caller process.
 *
 * @param Parameter Module handle.
 * @return DF_RETURN_NOT_IMPLEMENTED for valid module handles.
 */
UINT SysCall_ReleaseModule(UINT Parameter) {
    LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)HandleToPointer((HANDLE)Parameter);

    SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Binding, KOID_EXECUTABLE_MODULE_BINDING, FALSE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_BAD_PARAMETER;
}
