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


    Command Line Editor

\************************************************************************/

#ifndef COMMANDLINEEDITOR_H_INCLUDED
#define COMMANDLINEEDITOR_H_INCLUDED

#include "Base.h"
#include "utils/Allocator.h"
#include "utils/StringArray.h"

/***************************************************************************/

typedef struct tag_COMMANDLINE_COMPLETION_CONTEXT {
    LPCSTR Buffer;
    U32 BufferLength;
    U32 CursorPosition;
    U32 TokenStart;
    LPCSTR Token;
    U32 TokenLength;
    LPVOID UserData;
} COMMANDLINE_COMPLETION_CONTEXT, *LPCOMMANDLINE_COMPLETION_CONTEXT;

/***************************************************************************/

typedef BOOL (*COMMANDLINEEDITOR_COMPLETION_CALLBACK)(
    const COMMANDLINE_COMPLETION_CONTEXT*,
    LPSTR,
    U32);

/***************************************************************************/

typedef BOOL (*COMMANDLINEEDITOR_IDLE_CALLBACK)(LPVOID);

/***************************************************************************/

typedef struct tag_COMMANDLINEEDITOR {
    STRING_ARRAY History;
    U32 HistoryCapacity;
    COMMANDLINEEDITOR_COMPLETION_CALLBACK CompletionCallback;
    LPVOID CompletionUserData;
    COMMANDLINEEDITOR_IDLE_CALLBACK IdleCallback;
    LPVOID IdleUserData;
} COMMAND_LINE_EDITOR, *LPCOMMANDLINEEDITOR;

/***************************************************************************/

void CommandLineEditorInit(LPCOMMANDLINEEDITOR Editor, U32 HistoryCapacity);
void CommandLineEditorInitA(LPCOMMANDLINEEDITOR Editor, U32 HistoryCapacity, LPCALLOCATOR Allocator);
void CommandLineEditorDeinit(LPCOMMANDLINEEDITOR Editor);
void CommandLineEditorSetCompletionCallback(
    LPCOMMANDLINEEDITOR Editor,
    COMMANDLINEEDITOR_COMPLETION_CALLBACK Callback,
    LPVOID UserData);
void CommandLineEditorSetIdleCallback(
    LPCOMMANDLINEEDITOR Editor,
    COMMANDLINEEDITOR_IDLE_CALLBACK Callback,
    LPVOID UserData);
BOOL CommandLineEditorReadLine(
    LPCOMMANDLINEEDITOR Editor,
    LPSTR Buffer,
    U32 BufferSize,
    BOOL MaskCharacters);
void CommandLineEditorRemember(
    LPCOMMANDLINEEDITOR Editor,
    LPCSTR CommandLine);
void CommandLineEditorClearHistory(LPCOMMANDLINEEDITOR Editor);

/***************************************************************************/

#endif
