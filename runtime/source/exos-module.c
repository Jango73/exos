/************************************************************************\

    EXOS Runtime
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


    EXOS executable module runtime API

\************************************************************************/

#include "../include/exos.h"
#include "../include/exos-runtime.h"

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
