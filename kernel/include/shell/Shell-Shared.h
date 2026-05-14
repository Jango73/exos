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


    Shell - Shared Definitions

\************************************************************************/

#ifndef SHELL_SHARED_H_INCLUDED
#define SHELL_SHARED_H_INCLUDED

#include "arch/Disassemble.h"
#include "system/Clock.h"
#include "console/Console.h"
#include "drivers/input/Keyboard.h"
#include "drivers/filesystems/NTFS.h"
#include "drivers/storage/NVMe-Core.h"
#include "drivers/storage/USBStorage.h"
#include "drivers/usb/XHCI.h"
#include "core/DriverEnum.h"
#include "expose/Exposed.h"
#include "fs/File.h"
#include "core/Kernel.h"
#include "text/Lang.h"
#include "log/Log.h"
#include "memory/Memory-Descriptors.h"
#include "network/Network.h"
#include "network/NetworkManager.h"
#include "process/Process.h"
#include "log/Profile.h"
#include "script/Script.h"
#include "user/Account.h"
#include "user/UserSession.h"
#include "utils/CommandLineEditor.h"
#include "utils/Helpers.h"
#include "utils/Allocator.h"
#include "utils/Path.h"
#include "utils/ReservedHeap.h"
#include "utils/StringArray.h"
#include "input/VKey.h"

/************************************************************************/

#define SHELL_NUM_BUFFERS 8
#define BUFFER_SIZE 1024
#define HISTORY_SIZE 20
#define SHELL_RESERVED_HEAP_INITIAL_SIZE N_256KB
#define SHELL_RESERVED_HEAP_MAXIMUM_SIZE N_2MB

/************************************************************************/

typedef struct tag_SHELLINPUTSTATE {
    STR CommandLine[MAX_PATH_NAME];
    COMMANDLINEEDITOR Editor;
} SHELLINPUTSTATE, *LPSHELLINPUTSTATE;

typedef struct tag_SHELLCONTEXT {
    U32 Component;
    U32 CommandChar;
    SHELLINPUTSTATE Input;
    STR Command[256];
    STR CurrentFolder[MAX_PATH_NAME];
    RESERVED_HEAP ReservedHeap;
    ALLOCATOR Allocator;
    LPVOID BufferBase;
    U32 BufferSize;
    LPSTR Buffer[SHELL_NUM_BUFFERS];
    STRINGARRAY Options;
    PATHCOMPLETION PathCompletion;
    LPSCRIPT_CONTEXT ScriptContext;
} SHELLCONTEXT, *LPSHELLCONTEXT;

typedef U32 (*SHELLCOMMAND)(LPSHELLCONTEXT Context);

typedef struct tag_SHELL_COMMAND_ENTRY {
    STR Name[MAX_COMMAND_NAME];
    STR AltName[MAX_COMMAND_NAME];
    STR Usage[MAX_COMMAND_NAME];
    STR Description[MAX_COMMAND_NAME];
    SHELLCOMMAND Command;
} SHELL_COMMAND_ENTRY;

/************************************************************************/

extern SHELL_COMMAND_ENTRY COMMANDS[];

/************************************************************************/

void InitShellContext(LPSHELLCONTEXT Context);
void DeinitShellContext(LPSHELLCONTEXT Context);
void ClearOptions(LPSHELLCONTEXT Context);
BOOL ShowPrompt(LPSHELLCONTEXT Context);
BOOL ParseNextCommandLineComponent(LPSHELLCONTEXT Context);
BOOL HasOption(LPSHELLCONTEXT Context, LPCSTR ShortName, LPCSTR LongName);
BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName);
BOOL QualifyCommandLine(LPSHELLCONTEXT Context, LPCSTR RawCommandLine, LPSTR QualifiedCommandLine);
BOOL SpawnExecutable(LPSHELLCONTEXT Context, LPCSTR CommandName, BOOL Background);
BOOL RunScriptFile(LPSHELLCONTEXT Context, LPCSTR ScriptFileName);

void ExecuteStartupCommands(void);
void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine);
BOOL ParseCommand(LPSHELLCONTEXT Context);

void ShellScriptOutput(LPCSTR Message, LPVOID UserData);
UINT ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData);
LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData);
INT ShellScriptCallFunction(LPCSTR FuncName, UINT ArgumentCount, LPCSTR* Arguments, LPVOID UserData);
BOOL ShellGetAccountCount(LPSHELLCONTEXT Context, UINT* OutCount);

U32 CMD_addUser(LPSHELLCONTEXT Context);
U32 CMD_login(LPSHELLCONTEXT Context);
U32 ShowMainDesktopFromShell(void);

void SystemDataViewMode(void);

/************************************************************************/

#endif
