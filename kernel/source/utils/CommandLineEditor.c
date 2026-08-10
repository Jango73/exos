
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

#include "utils/CommandLineEditor.h"

#include "console/Console.h"
#include "drivers/input/Keyboard.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "user/UserSession.h"
#include "input/VKey.h"

/***************************************************************************/

static void UpdateInputCursor(U32 StartX, U32 StartY, U32 CursorPos) {
    U32 TargetX = StartX + CursorPos;
    U32 TargetY = StartY;

    if (Console.Width != 0) {
        TargetY += TargetX / Console.Width;
        TargetX %= Console.Width;
    }

    SetConsoleCursorPosition(TargetX, TargetY);
}

/***************************************************************************/

/**
 * @brief Ensure the input rendering fits on screen by scrolling when needed.
 * @param StartX Input start column.
 * @param StartY Input start row, updated when the console scrolls.
 * @param DisplayLength Maximum length rendered on screen.
 */
static void AdjustInputStartForScroll(U32 StartX, U32* StartY, U32 DisplayLength) {
    U32 Width;
    U32 Height;
    U32 LastRow;
    U32 ScrollCount;

    if (StartY == NULL || DisplayLength == 0) return;

    Width = Console.Width;
    Height = Console.Height;

    if (Width == 0 || Height == 0) return;

    LastRow = *StartY + ((StartX + DisplayLength) / Width);

    if (LastRow < Height) return;

    ScrollCount = LastRow - (Height - 1);

    while (ScrollCount > 0) {
        ScrollConsole();
        if (*StartY > 0) {
            (*StartY)--;
        }
        ScrollCount--;
    }
}

/***************************************************************************/

static void RefreshInputDisplay(
    LPCSTR Buffer,
    U32 StartX,
    U32* StartY,
    U32 Length,
    U32 PreviousLength,
    U32 CursorPos,
    BOOL MaskCharacters) {
    U32 Index;
    U32 DisplayLength;

    DisplayLength = (Length > PreviousLength) ? Length : PreviousLength;

    AdjustInputStartForScroll(StartX, StartY, DisplayLength);

    SetConsoleCursorPosition(StartX, *StartY);

    for (Index = 0; Index < Length; Index++) {
        if (MaskCharacters) {
            ConsolePrintChar('*');
        } else {
            ConsolePrintChar(Buffer[Index]);
        }
    }

    for (Index = Length; Index < PreviousLength; Index++) {
        ConsolePrintChar(STR_SPACE);
    }

    UpdateInputCursor(StartX, *StartY, CursorPos);
}

/***************************************************************************/

/**
 * @brief Record input activity for the current process session.
 */
static void MarkCurrentSessionActivity(void) {
    LPPROCESS Process = GetCurrentProcess();
    LPUSER_SESSION Session;

    SAFE_USE(Process) {
        Session = Process->Session;
        SAFE_USE(Session) { UpdateSessionActivity(Session); }
    }
}

/***************************************************************************/

void CommandLineEditorInit(LPCOMMANDLINEEDITOR Editor, U32 HistoryCapacity) {
    CommandLineEditorInitA(Editor, HistoryCapacity, NULL);
}

/***************************************************************************/

void CommandLineEditorInitA(LPCOMMANDLINEEDITOR Editor, U32 HistoryCapacity, LPCALLOCATOR Allocator) {
    MemorySet(Editor, 0, sizeof(COMMAND_LINE_EDITOR));

    Editor->HistoryCapacity = HistoryCapacity;
    StringArrayInitA(&Editor->History, HistoryCapacity, Allocator);
    Editor->CompletionCallback = NULL;
    Editor->CompletionUserData = NULL;
    Editor->IdleCallback = NULL;
    Editor->IdleUserData = NULL;
}

/***************************************************************************/

void CommandLineEditorDeinit(LPCOMMANDLINEEDITOR Editor) {
    StringArrayDeinit(&Editor->History);
    Editor->HistoryCapacity = 0;
    Editor->CompletionCallback = NULL;
    Editor->CompletionUserData = NULL;
}

/***************************************************************************/

void CommandLineEditorSetCompletionCallback(
    LPCOMMANDLINEEDITOR Editor,
    COMMANDLINEEDITOR_COMPLETION_CALLBACK Callback,
    LPVOID UserData) {
    Editor->CompletionCallback = Callback;
    Editor->CompletionUserData = UserData;
}

/***************************************************************************/

void CommandLineEditorSetIdleCallback(
    LPCOMMANDLINEEDITOR Editor,
    COMMANDLINEEDITOR_IDLE_CALLBACK Callback,
    LPVOID UserData) {
    Editor->IdleCallback = Callback;
    Editor->IdleUserData = UserData;
}

/***************************************************************************/

BOOL CommandLineEditorReadLine(
    LPCOMMANDLINEEDITOR Editor,
    LPSTR Buffer,
    U32 BufferSize,
    BOOL MaskCharacters) {
    KEY_CODE KeyCode;
    U32 CursorPos = 0;
    U32 Length = 0;
    U32 DisplayedLength = 0;
    U32 HistoryPos = Editor->History.Count;
    U32 StartX = 0;
    U32 StartY = 0;
    BOOL PreviousPagingActive = FALSE;

    if (BufferSize == 0) return FALSE;

    Buffer[0] = STR_NULL;
    GetConsoleCursorPosition(&StartX, &StartY);

    PreviousPagingActive = ConsoleGetPagingActive();
    ConsoleSetPagingActive(FALSE);

    FOREVER {
        if (PeekChar() == FALSE) {
            if (Editor->IdleCallback != NULL) {
                Editor->IdleCallback(Editor->IdleUserData);
            }
            Sleep(10);
            continue;
        }

        GetKeyCode(&KeyCode);

        if (KeyCode.VirtualKey == VK_ESCAPE) {
            Length = 0;
            CursorPos = 0;
            Buffer[0] = STR_NULL;
            RefreshInputDisplay(
                Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
            DisplayedLength = Length;
            MarkCurrentSessionActivity();
        } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
            if (CursorPos > 0) {
                MemoryMove(Buffer + CursorPos - 1, Buffer + CursorPos, (Length - CursorPos) + 1);
                CursorPos--;
                Length--;
                RefreshInputDisplay(
                    Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
                MarkCurrentSessionActivity();
            }
        } else if (KeyCode.VirtualKey == VK_DELETE) {
            if (CursorPos < Length) {
                MemoryMove(Buffer + CursorPos, Buffer + CursorPos + 1, (Length - CursorPos));
                Length--;
                Buffer[Length] = STR_NULL;
                RefreshInputDisplay(
                    Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
                MarkCurrentSessionActivity();
            }
        } else if (KeyCode.VirtualKey == VK_LEFT) {
            if (CursorPos > 0) {
                CursorPos--;
                UpdateInputCursor(StartX, StartY, CursorPos);
                MarkCurrentSessionActivity();
            }
        } else if (KeyCode.VirtualKey == VK_RIGHT) {
            if (CursorPos < Length) {
                CursorPos++;
                UpdateInputCursor(StartX, StartY, CursorPos);
                MarkCurrentSessionActivity();
            }
        } else if (KeyCode.VirtualKey == VK_HOME) {
            CursorPos = 0;
            UpdateInputCursor(StartX, StartY, CursorPos);
            MarkCurrentSessionActivity();
        } else if (KeyCode.VirtualKey == VK_END) {
            CursorPos = Length;
            UpdateInputCursor(StartX, StartY, CursorPos);
            MarkCurrentSessionActivity();
        } else if (KeyCode.VirtualKey == VK_ENTER) {
            ConsolePrintChar(STR_NEWLINE);
            Buffer[Length] = STR_NULL;
            MarkCurrentSessionActivity();
            break;
        } else if (KeyCode.VirtualKey == VK_UP) {
            if (HistoryPos > 0) {
                HistoryPos--;
                StringCopy(Buffer, StringArrayGet(&Editor->History, HistoryPos));
                Length = StringLength(Buffer);
                CursorPos = Length;
                RefreshInputDisplay(
                    Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
                MarkCurrentSessionActivity();
            }
        } else if (KeyCode.VirtualKey == VK_DOWN) {
            if (HistoryPos < Editor->History.Count) HistoryPos++;
            if (HistoryPos == Editor->History.Count) {
                Buffer[0] = STR_NULL;
                Length = 0;
                CursorPos = 0;
            } else {
                StringCopy(Buffer, StringArrayGet(&Editor->History, HistoryPos));
                Length = StringLength(Buffer);
                CursorPos = Length;
            }
            RefreshInputDisplay(
                Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
            DisplayedLength = Length;
            MarkCurrentSessionActivity();
        } else if (KeyCode.VirtualKey == VK_TAB) {
            if (Editor->CompletionCallback) {
                STR Replacement[MAX_PATH_NAME];
                U32 Start = CursorPos;

                while (Start && Buffer[Start - 1] != STR_SPACE) {
                    Start--;
                }

                COMMANDLINE_COMPLETION_CONTEXT CompletionContext;
                CompletionContext.Buffer = Buffer;
                CompletionContext.BufferLength = Length;
                CompletionContext.CursorPosition = CursorPos;
                CompletionContext.TokenStart = Start;
                CompletionContext.Token = Buffer + Start;
                CompletionContext.TokenLength = CursorPos - Start;
                CompletionContext.UserData = Editor->CompletionUserData;

                if (Editor->CompletionCallback(
                        &CompletionContext,
                        Replacement,
                        MAX_PATH_NAME)) {
                    U32 TokenLength = CursorPos - Start;
                    U32 ReplacementLength = StringLength(Replacement);
                    U32 TailLength = Length - CursorPos;
                    U32 NewLength = Length - TokenLength + ReplacementLength;

                    if (NewLength < BufferSize) {
                        MemoryMove(
                            Buffer + Start + ReplacementLength,
                            Buffer + CursorPos,
                            TailLength + 1);
                        MemoryCopy(Buffer + Start, Replacement, ReplacementLength);
                        Length = NewLength;
                        CursorPos = Start + ReplacementLength;
                        RefreshInputDisplay(
                            Buffer,
                            StartX,
                            &StartY,
                            Length,
                            DisplayedLength,
                            CursorPos,
                            MaskCharacters);
                        DisplayedLength = Length;
                        MarkCurrentSessionActivity();
                    }
                }
            }
        } else if (KeyCode.ASCIICode >= STR_SPACE) {
            if (Length < BufferSize - 1) {
                MemoryMove(Buffer + CursorPos + 1, Buffer + CursorPos, (Length - CursorPos) + 1);
                Buffer[CursorPos] = KeyCode.ASCIICode;
                CursorPos++;
                Length++;
                Buffer[Length] = STR_NULL;
                RefreshInputDisplay(
                    Buffer, StartX, &StartY, Length, DisplayedLength, CursorPos, MaskCharacters);
                DisplayedLength = Length;
                MarkCurrentSessionActivity();
            }
        }
    }

    ConsoleSetPagingActive(PreviousPagingActive);

    return TRUE;
}

/***************************************************************************/

void CommandLineEditorRemember(
    LPCOMMANDLINEEDITOR Editor,
    LPCSTR CommandLine) {
    if (StringLength(CommandLine) == 0) return;
    StringArrayMoveToEnd(&Editor->History, CommandLine);
}

/***************************************************************************/

void CommandLineEditorClearHistory(LPCOMMANDLINEEDITOR Editor) {
    U32 Index;

    if (Editor->History.Items == NULL) return;

    for (Index = 0; Index < Editor->History.Count; Index++) {
        if (Editor->History.Items[Index]) {
            AllocatorFree(&Editor->History.Allocator, Editor->History.Items[Index]);
            Editor->History.Items[Index] = NULL;
        }
    }

    Editor->History.Count = 0;
}

/***************************************************************************/
