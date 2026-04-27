/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS executable module runtime API

\************************************************************************/

#include "exos/exos-runtime-main.h"
#include "exos/exos.h"

/***************************************************************************/

HANDLE LoadModule(LPCSTR Path) {
    MODULE_LOAD_INFO Info;

    if (Path == NULL) {
        return 0;
    }

    Info.Header.Size = sizeof(Info);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Path = Path;
    Info.Module = 0;
    Info.Flags = 0;
    Info.ModuleIdentifierHigh = 0;
    Info.ModuleIdentifierLow = 0;

    if (exoscall(SYSCALL_LoadModule, EXOS_PARAM(&Info)) != DF_RETURN_SUCCESS) {
        return 0;
    }

    return Info.Module;
}

/***************************************************************************/

LPVOID GetModuleSymbol(HANDLE Module, LPCSTR Name) {
    MODULE_SYMBOL_INFO Info;

    if (Module == 0 || Name == NULL) {
        return NULL;
    }

    Info.Header.Size = sizeof(Info);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Module = Module;
    Info.Name = Name;
    Info.Address = 0;

    if (exoscall(SYSCALL_GetModuleSymbol, EXOS_PARAM(&Info)) != DF_RETURN_SUCCESS) {
        return NULL;
    }

    return (LPVOID)Info.Address;
}

/***************************************************************************/

BOOL ReleaseModule(HANDLE Module) {
    if (Module == 0) {
        return FALSE;
    }

    return exoscall(SYSCALL_ReleaseModule, EXOS_PARAM(Module)) == DF_RETURN_SUCCESS;
}
