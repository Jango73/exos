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
    {"add_user", "new_user", "username", "Create user account", CMD_adduser},
    {"autotest", "autotest", "stack", "Run built-in tests", CMD_autotest},
    {"cd", "cd", "Name", "Change current folder", CMD_cd},
    {"clear", "cls", "", "Clear console screen", CMD_cls},
    {"commands", "help", "", "List available commands", CMD_commands},
    {"con_mode", "mode", "Columns Rows|list", "Set or list console mode", CMD_conmode},
    {"copy", "cp", "", "Copy file or folder", CMD_copy},
    {"credits", "credits", "", "Show credits", CMD_credits},
    {"data", "data_view", "", "View kernel data", CMD_dataview},
    {"del_user", "delete_user", "username", "Remove user account", CMD_deluser},
    {"dis", "disasm", "Address InstructionCount", "Disassemble memory range", CMD_disasm},
    {"disk", "disk", "list", "Show disk information", CMD_disk},
    {"drv", "driver", "list|Alias", "Show driver details", CMD_driver},
    {"desktop", "desktop", "show|status|theme <path-or-name>", "Control desktop and theme runtime", CMD_desktop},
    {"edit", "edit", "Name", "Open text editor", CMD_edit},
    {"fs", "file_system", "list", "Show file system information", CMD_filesystem},
    {"keyboard", "keyboard", "--layout Code", "Change keyboard layout", CMD_keyboard},
    {"list", "dir", "[Name] [-p] [-r] [-s|--stress]", "List folder entries", CMD_dir},
    {"login", "login", "", "Authenticate user session", CMD_login},
    {"logout", "logout", "", "End current user session", CMD_logout},
    {"mem", "mem_edit", "Address", "Edit memory at address", CMD_memedit},
    {"mem_map", "memory_map", "", "Show memory map", CMD_memorymap},
    {"mkdir", "md", "Name", "Create a folder", CMD_md},
    {"net", "network", "devices", "List network devices", CMD_network},
    {"nvme", "nvme", "list", "List NVMe devices", CMD_nvme},
    {"package", "package", "run|list|add ...", "Manage packages", CMD_package},
    {"passwd", "set_password", "", "Change user password", CMD_passwd},
    {"pause", "pause", "on|off", "Toggle paged output", CMD_pause},
    {"pic", "pic", "", "Show interrupt controller info", CMD_pic},
    {"prof", "profiling", "[reset]", "Show profiling statistics", CMD_prof},
    {"quit", "exit", "", "Exit shell", CMD_exit},
    {"reboot", "reboot", "", "Reboot system", CMD_reboot},
    {"run", "launch", "Name [-b|--background]", "Launch executable", CMD_run},
    {"shutdown", "power_off", "", "Power off system", CMD_shutdown},
    {"sys", "sys_info", "", "Show system information", CMD_sysinfo},
    {"task", "task", "list", "List visible tasks", CMD_task},
    {"type", "show", "", "Show file content", CMD_type},
    {"usb", "usb", "ports|devices|device-tree|drives|probe", "Inspect USB devices", CMD_usb},
    {"who_am_i", "who", "", "Show current user identity", CMD_whoami},
    {"", "", "", "", NULL},
};
