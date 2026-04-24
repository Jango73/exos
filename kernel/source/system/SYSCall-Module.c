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

// Module load diagnostic stages written to MODULE_LOAD_INFO.Flags.
#define MODULE_LOAD_STAGE_NONE            0x0
#define MODULE_LOAD_STAGE_BAD_INPUT       0x1
#define MODULE_LOAD_STAGE_OPEN_FILE       0x2
#define MODULE_LOAD_STAGE_IMAGE_ACQUIRE   0x3
#define MODULE_LOAD_STAGE_BINDING_ACQUIRE 0x4
#define MODULE_LOAD_STAGE_INSTALL_SEGMENT 0x5
#define MODULE_LOAD_STAGE_HANDLE_EXPORT   0x6

/***************************************************************************/

/**
 * @brief Load one executable module into the current process.
 *
 * @param Path Module file path.
 * @param FailureStageOut Optional failure stage output.
 * @return Module binding pointer or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING LoadCurrentProcessModuleByPath(LPCSTR Path, U32* FailureStageOut) {
    LPEXECUTABLE_MODULE_IMAGE Image = NULL;
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;
    FILE_OPEN_INFO OpenInfo;
    LPFILE File = NULL;
    LPPROCESS Process = GetCurrentProcess();

    if (FailureStageOut != NULL) {
        *FailureStageOut = MODULE_LOAD_STAGE_NONE;
    }

    if (Path == NULL || Process == NULL) {
        if (FailureStageOut != NULL) {
            *FailureStageOut = MODULE_LOAD_STAGE_BAD_INPUT;
        }
        return NULL;
    }

    OpenInfo.Header.Size = sizeof(OpenInfo);
    OpenInfo.Header.Version = EXOS_ABI_VERSION;
    OpenInfo.Header.Flags = 0;
    OpenInfo.Name = Path;
    OpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&OpenInfo);
    if (File == NULL) {
        if (FailureStageOut != NULL) {
            *FailureStageOut = MODULE_LOAD_STAGE_OPEN_FILE;
        }
        return NULL;
    }

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
        if (Binding == NULL) {
            if (FailureStageOut != NULL) {
                *FailureStageOut = MODULE_LOAD_STAGE_BINDING_ACQUIRE;
            }
            return NULL;
        }

        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            if (InstallProcessModuleBindingSegments(Process, Binding)) {
                return Binding;
            }

            if (FailureStageOut != NULL) {
                *FailureStageOut = MODULE_LOAD_STAGE_INSTALL_SEGMENT;
            }
            ReleaseProcessModuleBinding(Binding);
        }
    }

    if (FailureStageOut != NULL && *FailureStageOut == MODULE_LOAD_STAGE_NONE) {
        *FailureStageOut = MODULE_LOAD_STAGE_IMAGE_ACQUIRE;
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
        Info->Flags = MODULE_LOAD_STAGE_NONE;
        Info->Module = 0;
        Info->ModuleIdentifierHigh = 0;
        Info->ModuleIdentifierLow = 0;

        SAFE_USE_VALID((LPCSTR)Info->Path) {
            U32 FailureStage = MODULE_LOAD_STAGE_NONE;
            LPEXECUTABLE_MODULE_BINDING Binding = LoadCurrentProcessModuleByPath(Info->Path, &FailureStage);

            SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
                HANDLE Handle = PointerToHandle((LINEAR)Binding);

                if (Handle == 0) {
                    Info->Flags = MODULE_LOAD_STAGE_HANDLE_EXPORT;
                    WARNING(TEXT("[SysCall_LoadModule] Handle export failed path=%s"),
                            Info->Path);
                    ReleaseProcessModuleBinding(Binding);
                    return DF_RETURN_GENERIC;
                }

                Info->Module = Handle;
                Info->ModuleIdentifierHigh = U64_High32(Binding->InstanceID);
                Info->ModuleIdentifierLow = U64_Low32(Binding->InstanceID);
                Info->Flags = MODULE_LOAD_STAGE_NONE;
                return DF_RETURN_SUCCESS;
            }

            Info->Flags = FailureStage;
            WARNING(TEXT("[SysCall_LoadModule] Load failed path=%s stage=%u"),
                    Info->Path,
                    FailureStage);
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
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;
    LPEXECUTABLE_SYMBOL_TABLE_INFO SymbolTable = NULL;

    SAFE_USE_INPUT_POINTER(Info, MODULE_SYMBOL_INFO) {
        Info->Address = 0;

        SAFE_USE_VALID((LPCSTR)Info->Name) {
            Binding = (LPEXECUTABLE_MODULE_BINDING)HandleToPointer(Info->Module);
            if (Binding == NULL) {
                WARNING(TEXT("Invalid module handle=%p name=%s"),
                        Info->Module,
                        Info->Name);
                return DF_RETURN_BAD_PARAMETER;
            }

            SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(Binding, KOID_EXECUTABLE_MODULE_BINDING, FALSE) {
                if (ResolveProcessModuleBindingExport(GetCurrentProcess(), Binding, Info->Name, &(Info->Address))) {
                    return DF_RETURN_SUCCESS;
                }

                if (Binding->Image != NULL) {
                    SymbolTable = &(Binding->Image->Metadata.Dynamic.SymbolTable);
                }

                if (SymbolTable != NULL) {
                    WARNING(TEXT("Resolve failed module=%p name=%s arch=%u sym=%x str=%x sym_size=%u ent=%u str_size=%u"),
                            Info->Module,
                            Info->Name,
                            Binding->Image->Metadata.Architecture,
                            SymbolTable->SymbolTableAddress,
                            SymbolTable->StringTableAddress,
                            SymbolTable->SymbolTableSize,
                            SymbolTable->SymbolEntrySize,
                            SymbolTable->StringTableSize);
                } else {
                    WARNING(TEXT("Resolve failed module=%p name=%s no-image"),
                            Info->Module,
                            Info->Name);
                }
                return DF_RETURN_GENERIC;
            }

            WARNING(TEXT("Module access denied handle=%p name=%s"),
                    Info->Module,
                    Info->Name);
            return DF_RETURN_BAD_PARAMETER;
        }

        WARNING(TEXT("Invalid symbol name pointer module=%p"),
                Info->Module);
        return DF_RETURN_BAD_PARAMETER;
    }

    WARNING(TEXT("Invalid input pointer param=%x"),
            Parameter);
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
