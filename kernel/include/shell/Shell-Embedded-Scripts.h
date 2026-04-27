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


    Shell - Embedded E0 Scripts

\************************************************************************/

#ifndef SHELL_EMBEDDED_SCRIPTS_H_INCLUDED
#define SHELL_EMBEDDED_SCRIPTS_H_INCLUDED

#include "shell/Shell-Shared.h"

/************************************************************************/

typedef enum {
    SHELL_EMBEDDED_SCRIPT_DRIVER_LIST = 0,
    SHELL_EMBEDDED_SCRIPT_DRIVER_DETAILS,
    SHELL_EMBEDDED_SCRIPT_TASK_LIST,
    SHELL_EMBEDDED_SCRIPT_DISK_LIST,
    SHELL_EMBEDDED_SCRIPT_FILE_SYSTEM_LIST,
    SHELL_EMBEDDED_SCRIPT_FILE_SYSTEM_LIST_LONG,
    SHELL_EMBEDDED_SCRIPT_MEMORY_MAP,
    SHELL_EMBEDDED_SCRIPT_NETWORK_DEVICES,
    SHELL_EMBEDDED_SCRIPT_USB_PORTS,
    SHELL_EMBEDDED_SCRIPT_USB_DEVICES,
    SHELL_EMBEDDED_SCRIPT_USB_DRIVES,
    SHELL_EMBEDDED_SCRIPT_USB_DEVICE_TREE,
    SHELL_EMBEDDED_SCRIPT_USB_PROBE,
    SHELL_EMBEDDED_SCRIPT_NVME_LIST,
    SHELL_EMBEDDED_SCRIPT_COUNT
} SHELL_EMBEDDED_SCRIPT_ID;

/************************************************************************/

LPCSTR ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_ID ScriptId);

/************************************************************************/

#endif
