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


    Shell commands - private definitions

\************************************************************************/

#ifndef SHELL_COMMANDS_PRIVATE_H_INCLUDED
#define SHELL_COMMANDS_PRIVATE_H_INCLUDED

#include "shell/Shell-Shared.h"

/************************************************************************/

U32 CMD_commands(LPSHELLCONTEXT Context);
U32 CMD_cls(LPSHELLCONTEXT Context);
U32 CMD_conmode(LPSHELLCONTEXT Context);
U32 CMD_keyboard(LPSHELLCONTEXT Context);
U32 CMD_pause(LPSHELLCONTEXT Context);
U32 CMD_dir(LPSHELLCONTEXT Context);
U32 CMD_cd(LPSHELLCONTEXT Context);
U32 CMD_md(LPSHELLCONTEXT Context);
U32 CMD_run(LPSHELLCONTEXT Context);
U32 CMD_package(LPSHELLCONTEXT Context);
U32 CMD_exit(LPSHELLCONTEXT Context);
U32 CMD_sysinfo(LPSHELLCONTEXT Context);
U32 CMD_task(LPSHELLCONTEXT Context);
U32 CMD_memedit(LPSHELLCONTEXT Context);
U32 CMD_disasm(LPSHELLCONTEXT Context);
U32 CMD_type(LPSHELLCONTEXT Context);
U32 CMD_copy(LPSHELLCONTEXT Context);
U32 CMD_edit(LPSHELLCONTEXT Context);
U32 CMD_memorymap(LPSHELLCONTEXT Context);
U32 CMD_disk(LPSHELLCONTEXT Context);
U32 CMD_filesystem(LPSHELLCONTEXT Context);
U32 CMD_network(LPSHELLCONTEXT Context);
U32 CMD_pic(LPSHELLCONTEXT Context);
U32 CMD_driver(LPSHELLCONTEXT Context);
U32 CMD_desktop(LPSHELLCONTEXT Context);
U32 CMD_reboot(LPSHELLCONTEXT Context);
U32 CMD_shutdown(LPSHELLCONTEXT Context);
U32 CMD_deluser(LPSHELLCONTEXT Context);
U32 CMD_logout(LPSHELLCONTEXT Context);
U32 CMD_whoami(LPSHELLCONTEXT Context);
U32 CMD_passwd(LPSHELLCONTEXT Context);
U32 CMD_prof(LPSHELLCONTEXT Context);
U32 CMD_autotest(LPSHELLCONTEXT Context);
U32 CMD_usb(LPSHELLCONTEXT Context);
U32 CMD_nvme(LPSHELLCONTEXT Context);
U32 CMD_dataview(LPSHELLCONTEXT Context);
U32 CMD_credits(LPSHELLCONTEXT Context);

void ListDirectory(LPSHELLCONTEXT Context, LPCSTR Base, U32 Indent, BOOL Pause, BOOL Recurse, U32* NumListed);
BOOL RunScriptFile(LPSHELLCONTEXT Context, LPCSTR ScriptFileName);
UINT RunEmbeddedScript(LPSHELLCONTEXT Context, LPCSTR ScriptText);
UINT ShellCreateAccount(LPCSTR UserName, LPCSTR Password, U32 Privilege);
UINT ShellDeleteAccount(LPCSTR UserName);
UINT ShellChangePassword(LPCSTR OldPassword, LPCSTR NewPassword);

/************************************************************************/

#endif
