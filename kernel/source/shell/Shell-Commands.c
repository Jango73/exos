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


    Shell commands - command table

\************************************************************************/

#include "shell/Shell-Commands-Private.h"

/************************************************************************/

SHELL_COMMAND_ENTRY COMMANDS[] = {
    {"addUser", "newUser", "username", "Create user account", CMD_addUser},
    {"autotest", "autotest", "stack", "Run built-in tests", CMD_autotest},
    {"cf", "cd", "Name", "Change current folder", CMD_changeFolder},
    {"clear", "cls", "", "Clear console screen", CMD_clearScreen},
    {"commands", "help", "", "List available commands", CMD_commands},
    {"consoleMode", "mode", "Columns Rows|list", "Set or list console mode", CMD_consoleMode},
    {"copy", "cp", "", "Copy file or folder", CMD_copy},
    {"credits", "credits", "", "Show credits", CMD_credits},
    {"dataView", "data", "", "View kernel data", CMD_dataView},
    {"deleteUser", "delUser", "username", "Remove user account", CMD_deleteUser},
    {"disasm", "dis", "Address InstructionCount", "Disassemble memory range", CMD_disasm},
    {"disk", "disk", "list", "Show disk information", CMD_disk},
    {"driver", "drv", "list|Alias", "Show driver details", CMD_driver},
    {"desktop", "dskt", "show|status|theme <path-or-name>", "Control desktop and theme runtime", CMD_desktop},
    {"edit", "ed", "Name", "Open text editor", CMD_edit},
    {"fileSystem", "fs", "list", "Show file system information", CMD_fileSystem},
    {"keyboard", "keyb", "--layout Code", "Change keyboard layout", CMD_keyboard},
    {"listFolder", "lf", "[Name] [-p] [-r] [-s|--stress]", "List folder entries", CMD_listFolder},
    {"login", "login", "", "Authenticate user session", CMD_login},
    {"logout", "logout", "", "End current user session", CMD_logout},
    {"memEdit", "mem", "Address", "Edit memory at address", CMD_memEdit},
    {"memoryMap", "memMap", "", "Show memory map", CMD_memorymap},
    {"makeFolder", "mf", "Name", "Create a folder", CMD_makeFolder},
    {"net", "network", "devices", "List network devices", CMD_network},
    {"nvme", "nvme", "list", "List NVMe devices", CMD_nvme},
    {"package", "package", "run|list|add ...", "Manage packages", CMD_package},
    {"setPassword", "passwd", "", "Change user password", CMD_setPassword},
    {"pause", "pause", "on|off", "Toggle paged output", CMD_pause},
    {"pic", "pic", "", "Show interrupt controller info", CMD_pic},
    {"profiling", "prof", "[reset]", "Show profiling statistics", CMD_profiling},
    {"quit", "exit", "", "Exit shell", CMD_exit},
    {"reboot", "reboot", "", "Reboot system", CMD_reboot},
    {"run", "launch", "Name [-b|--background]", "Launch executable", CMD_run},
    {"shutdown", "powerOff", "", "Power off system", CMD_shutdown},
    {"systemInfo", "sys", "", "Show system information", CMD_systemInfo},
    {"task", "task", "list", "List visible tasks", CMD_task},
    {"type", "show", "", "Show file content", CMD_type},
    {"usb", "usb", "ports|devices|deviceTree|drives|probe", "Inspect USB devices", CMD_usb},
    {"whoAmI", "who", "", "Show current user identity", CMD_whoAmI},
    {"", "", "", "", NULL},
};
