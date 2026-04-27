
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


    Edit

\************************************************************************/

#include "system/Edit-Private.h"
#include "text/CoreString.h"
#include "memory/Heap.h"
#include "User.h"
#include "drivers/input/Keyboard.h"
#include "process/Task.h"

/***************************************************************************/

static BOOL CommandExit(LPEDITCONTEXT Context);
static BOOL CommandSave(LPEDITCONTEXT Context);
static BOOL CommandCut(LPEDITCONTEXT Context);
static BOOL CommandCopy(LPEDITCONTEXT Context);
static BOOL CommandPaste(LPEDITCONTEXT Context);

EDITMENUITEM Menu[] = {
    {{VK_NONE, 0, 0}, {VK_ESCAPE, 0, 0}, TEXT("Exit"), CommandExit},
    {{VK_CONTROL, 0, 0}, {VK_S, 0, 0}, TEXT("Save"), CommandSave},
    {{VK_CONTROL, 0, 0}, {VK_X, 0, 0}, TEXT("Cut"), CommandCut},
    {{VK_CONTROL, 0, 0}, {VK_C, 0, 0}, TEXT("Copy"), CommandCopy},
    {{VK_CONTROL, 0, 0}, {VK_V, 0, 0}, TEXT("Paste"), CommandPaste},
};
const U32 MenuItems = sizeof(Menu) / sizeof(Menu[0]);

const KEYCODE ControlKey = {VK_CONTROL, 0, 0};
const KEYCODE ShiftKey = {VK_SHIFT, 0, 0};
/**
 * @brief Allocate a new editable line with a given capacity.
 * @param Size Maximum number of characters in the line.
 * @return Pointer to the newly created line or NULL on failure.
 */
LPEDITLINE NewEditLine(I32 Size) {
    LPEDITLINE This = (LPEDITLINE)HeapAlloc(sizeof(EDITLINE));

    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->MaxChars = Size;
    This->NumChars = 0;
    This->Chars = (LPSTR)HeapAlloc(Size);

    return This;
}

/***************************************************************************/

/**
 * @brief Free an editable line and its resources.
 * @param This Line to destroy.
 */
void DeleteEditLine(LPEDITLINE This) {
    if (This == NULL) return;

    HeapFree(This->Chars);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief List destructor callback for edit lines.
 * @param Item Item to delete.
 */
void EditLineDestructor(LPVOID Item) { DeleteEditLine((LPEDITLINE)Item); }

/***************************************************************************/

/**
 * @brief Create a new editable file instance.
 * @return Pointer to a new EDITFILE or NULL on failure.
 */
LPEDITFILE NewEditFile(void) {
    LPEDITFILE This;
    LPEDITLINE Line;

    This = (LPEDITFILE)HeapAlloc(sizeof(EDITFILE));
    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->Lines = NewList(EditLineDestructor, NULL, NULL);
    This->Cursor.X = 0;
    This->Cursor.Y = 0;
    This->SelStart.X = 0;
    This->SelStart.Y = 0;
    This->SelEnd.X = 0;
    This->SelEnd.Y = 0;
    This->Left = 0;
    This->Top = 0;
    This->Name = NULL;
    This->Modified = FALSE;

    Line = NewEditLine(8);
    ListAddItem(This->Lines, Line);

    return This;
}

/***************************************************************************/

/**
 * @brief Destroy an editable file and all contained lines.
 * @param This File to delete.
 */
void DeleteEditFile(LPEDITFILE This) {
    if (This == NULL) return;

    DeleteList(This->Lines);
    HeapFree(This->Name);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief List destructor callback for edit files.
 * @param Item Item to delete.
 */
void EditFileDestructor(LPVOID Item) { DeleteEditFile((LPEDITFILE)Item); }

/***************************************************************************/

/**
 * @brief Allocate a new editor context.
 * @return Pointer to a new EDITCONTEXT or NULL on failure.
 */
LPEDITCONTEXT NewEditContext(void) {
    LPEDITCONTEXT This = (LPEDITCONTEXT)HeapAlloc(sizeof(EDITCONTEXT));
    if (This == NULL) return NULL;

    This->Next = NULL;
    This->Prev = NULL;
    This->Files = NewList(EditFileDestructor, NULL, NULL);
    This->Current = NULL;
    This->Insert = 1;
    This->Clipboard = NULL;
    This->ClipboardSize = 0;
    This->ShowLineNumbers = FALSE;

    return This;
}

/***************************************************************************/

/**
 * @brief Destroy an editor context and its files list.
 * @param This Context to delete.
 */
void DeleteEditContext(LPEDITCONTEXT This) {
    if (This == NULL) return;

    DeleteList(This->Files);
    HeapFree(This->Clipboard);
    HeapFree(This);
}

/***************************************************************************/

/**
 * @brief Ensure cursor and viewport positions remain within bounds.
 * @param File File whose positions are validated.
 */
void CheckPositions(LPEDITFILE File) {
    I32 MinX = 0;
    I32 MinY = 0;
    I32 MaxX = MAX_COLUMNS;
    I32 MaxY = MAX_LINES;

    while (File->Cursor.X < MinX) {
        File->Left--;
        File->Cursor.X++;
    }
    while (File->Cursor.X >= MaxX) {
        File->Left++;
        File->Cursor.X--;
    }
    while (File->Cursor.Y < MinY) {
        File->Top--;
        File->Cursor.Y++;
    }
    while (File->Cursor.Y >= MaxY) {
        File->Top++;
        File->Cursor.Y--;
    }

    if (File->Left < 0) File->Left = 0;
    if (File->Top < 0) File->Top = 0;
}

/***************************************************************************/

/**
 * @brief Compute the absolute cursor position inside the file.
 * @param File File providing cursor and viewport information.
 * @return Absolute cursor coordinates or {0,0} if File is NULL.
 */
static void ConsoleFill(U32 Row, U32 Column, U32 Length);
static void RenderTitleBar(LPEDITFILE File, U32 ForeColor, U32 BackColor, U32 Width);
static void RenderMenu(U32 ForeColor, U32 BackColor, U32 Width);

/***************************************************************************/

/**
 * @brief Render the current file content to the console.
 * @param Context Editor context providing rendering settings.
 */
void Render(LPEDITCONTEXT Context) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    I32 Index;
    U32 RowIndex;
    LPEDITFILE File;
    BOOL ShowLineNumbers;
    U32 TextColumnOffset;
    U32 Width;
    BOOL PendingEofMarker = FALSE;
    BOOL EofDrawn = FALSE;
    BOOL HasSelection;
    POINT SelectionStart;
    POINT SelectionEnd;
    U32 DefaultForeColor;
    U32 DefaultBackColor;
    U32 MenuForeColor = CONSOLE_WHITE;
    U32 MenuBackColor = CONSOLE_BLUE;
    U32 TitleForeColor = CONSOLE_WHITE;
    U32 TitleBackColor = CONSOLE_BLUE;
    U32 LineNumberForeColor = CONSOLE_BLACK;
    U32 LineNumberBackColor = CONSOLE_WHITE;
    U32 SelectionForeColor;
    U32 SelectionBackColor;
    U32 CursorX;
    U32 CursorY;

    if (Context == NULL) return;

    File = Context->Current;

    if (File == NULL) return;
    if (File->Lines->NumItems == 0) return;

    ShowLineNumbers = Context->ShowLineNumbers;
    TextColumnOffset = ShowLineNumbers ? 4U : 0U;

    CheckPositions(File);

    for (Node = File->Lines->First, Index = 0; Node; Node = Node->Next) {
        if (Index == File->Top) break;
        Index++;
    }

    Width = GetConsoleWidth();
    DefaultForeColor = GetConsoleForeColor();
    DefaultBackColor = GetConsoleBackColor();
    SelectionForeColor = DefaultBackColor;
    SelectionBackColor = DefaultForeColor;

    HasSelection = SelectionHasRange(File);
    if (HasSelection) {
        NormalizeSelection(File, &SelectionStart, &SelectionEnd);
    }

    RenderTitleBar(File, TitleForeColor, TitleBackColor, Width);

    for (RowIndex = 0; RowIndex < MAX_LINES; RowIndex++) {
        LPEDITLINE CurrentLine = NULL;
        I32 LineLength = 0;
        I32 AbsoluteRow = File->Top + (I32)RowIndex;
        U32 TargetRow = EDIT_TITLE_HEIGHT + RowIndex;
        BOOL RowHasEofMarker = FALSE;

        SetConsoleForeColor(DefaultForeColor);
        SetConsoleBackColor(DefaultBackColor);
        ConsoleFill(TargetRow, 0, Width);

        if (ShowLineNumbers) {
            SetConsoleForeColor(LineNumberForeColor);
            SetConsoleBackColor(LineNumberBackColor);
            ConsoleFill(TargetRow, 0, TextColumnOffset);
        }

        if (Node) {
            LPLISTNODE CurrentNode = Node;
            I32 Start = File->Left;
            I32 Visible = 0;

            Line = (LPEDITLINE)CurrentNode;
            CurrentLine = Line;

            if (Start < 0) Start = 0;

            if (Start < Line->NumChars) {
                I32 End = Line->NumChars;

                Visible = End - Start;
                if (Visible > (I32)MAX_COLUMNS) {
                    Visible = (I32)MAX_COLUMNS;
                }

                I32 MaxVisible = (I32)Width - (I32)TextColumnOffset;
                if (MaxVisible < 0) MaxVisible = 0;
                if (Visible > MaxVisible) {
                    Visible = MaxVisible;
                }

                if (Visible > 0) {
                    SetConsoleForeColor(DefaultForeColor);
                    SetConsoleBackColor(DefaultBackColor);
                    ConsolePrintLine(TargetRow, TextColumnOffset, &Line->Chars[Start], (U32)Visible);
                }
                LineLength = Line->NumChars;
            } else {
                LineLength = Line->NumChars;
            }

            if (CurrentNode->Next == NULL) {
                PendingEofMarker = TRUE;
            }

            Node = CurrentNode->Next;

            if (ShowLineNumbers && CurrentLine) {
                STR LineNumberText[8];
                U32 DigitCount;

                SetConsoleForeColor(LineNumberForeColor);
                SetConsoleBackColor(LineNumberBackColor);
                StringPrintFormat(LineNumberText, TEXT("%3d"), AbsoluteRow + 1);
                DigitCount = StringLength(LineNumberText);
                if (DigitCount > TextColumnOffset) {
                    DigitCount = TextColumnOffset;
                }
                if (DigitCount > Width) {
                    DigitCount = Width;
                }
                if (DigitCount > 0) {
                    ConsolePrintLine(TargetRow, 0, LineNumberText, DigitCount);
                }
            }
        } else {
            if (PendingEofMarker && EofDrawn == FALSE) {
                U32 TargetColumn = TextColumnOffset;
                if (TargetColumn < Width) {
                    STR EofChar[1];
                    EofChar[0] = EDIT_EOF_CHAR;
                    SetConsoleForeColor(DefaultForeColor);
                    SetConsoleBackColor(DefaultBackColor);
                    ConsolePrintLine(TargetRow, TargetColumn, EofChar, 1);
                    RowHasEofMarker = TRUE;
                }
                EofDrawn = TRUE;
                PendingEofMarker = FALSE;
            }
        }

        if (HasSelection) {
            I32 RangeStart = 0;
            I32 RangeEnd = 0;

            if (AbsoluteRow < SelectionStart.Y || AbsoluteRow > SelectionEnd.Y) {
                RangeStart = 0;
                RangeEnd = 0;
            } else if (SelectionStart.Y == SelectionEnd.Y) {
                RangeStart = SelectionStart.X;
                RangeEnd = SelectionEnd.X;
            } else if (AbsoluteRow == SelectionStart.Y) {
                RangeStart = SelectionStart.X;
                RangeEnd = LineLength;
            } else if (AbsoluteRow == SelectionEnd.Y) {
                RangeStart = 0;
                RangeEnd = SelectionEnd.X;
            } else {
                RangeStart = 0;
                RangeEnd = LineLength;
            }

            if (RangeStart < 0) RangeStart = 0;
            if (RangeEnd < RangeStart) RangeEnd = RangeStart;

            if (CurrentLine) {
                if (RangeStart > CurrentLine->NumChars) RangeStart = CurrentLine->NumChars;
                if (RangeEnd > CurrentLine->NumChars) RangeEnd = CurrentLine->NumChars;
            } else {
                RangeStart = 0;
                if (RangeEnd < 0) RangeEnd = 0;
            }

            if (AbsoluteRow == SelectionEnd.Y && AbsoluteRow > SelectionStart.Y && SelectionEnd.X == 0) {
                RangeEnd = RangeStart + 1;
            }

            if (RangeEnd > RangeStart) {
                I32 VisibleStart = RangeStart - File->Left;
                I32 VisibleEnd = RangeEnd - File->Left;

                if (VisibleStart < 0) VisibleStart = 0;
                if (VisibleEnd < 0) VisibleEnd = 0;

                I32 MaxVisible = (I32)Width - (I32)TextColumnOffset;
                if (MaxVisible < 0) MaxVisible = 0;
                if (VisibleEnd > MaxVisible) VisibleEnd = MaxVisible;

                if (VisibleStart < VisibleEnd) {
                    U32 HighlightColumn = TextColumnOffset + (U32)VisibleStart;
                    U32 HighlightLength = (U32)(VisibleEnd - VisibleStart);

                    if (HighlightColumn < Width) {
                        if (HighlightLength > (Width - HighlightColumn)) {
                            HighlightLength = Width - HighlightColumn;
                        }

                        if (HighlightLength > 0) {
                            U32 Remaining = HighlightLength;
                            I32 SourceIndex = RangeStart;
                            U32 BufferOffset = 0;

                            while (Remaining > 0) {
                                STR HighlightBuffer[64];
                                U32 Chunk = Remaining;
                                U32 IndexInChunk;

                                if (Chunk > (U32)(sizeof(HighlightBuffer) / sizeof(HighlightBuffer[0]))) {
                                    Chunk = (U32)(sizeof(HighlightBuffer) / sizeof(HighlightBuffer[0]));
                                }

                                for (IndexInChunk = 0; IndexInChunk < Chunk; IndexInChunk++) {
                                    STR Character = STR_SPACE;

                                    if (CurrentLine && (SourceIndex + (I32)IndexInChunk) < CurrentLine->NumChars) {
                                        Character = CurrentLine->Chars[SourceIndex + (I32)IndexInChunk];
                                    } else if (RowHasEofMarker && HighlightColumn == TextColumnOffset && IndexInChunk == 0) {
                                        Character = EDIT_EOF_CHAR;
                                    }

                                    HighlightBuffer[IndexInChunk] = Character;
                                }

                                SetConsoleForeColor(SelectionForeColor);
                                SetConsoleBackColor(SelectionBackColor);
                                ConsolePrintLine(TargetRow, HighlightColumn + BufferOffset, HighlightBuffer, Chunk);

                                BufferOffset += Chunk;
                                SourceIndex += (I32)Chunk;
                                Remaining -= Chunk;
                            }

                            SetConsoleForeColor(DefaultForeColor);
                            SetConsoleBackColor(DefaultBackColor);
                        }
                    }
                }
            }
        }
    }

    RenderMenu(MenuForeColor, MenuBackColor, Width);

    CursorX = (U32)((I32)TextColumnOffset + File->Cursor.X);
    if (CursorX >= Width) {
        CursorX = Width - 1;
    }
    CursorY = EDIT_TITLE_HEIGHT + File->Cursor.Y;
    SetConsoleCursorPosition(CursorX, CursorY);

    SetConsoleForeColor(DefaultForeColor);
    SetConsoleBackColor(DefaultBackColor);
}

/***************************************************************************/

/**
 * @brief Fill a portion of the console with spaces using current colors.
 * @param Row Target console row.
 * @param Column Starting column inside the row.
 * @param Length Number of characters to overwrite.
 */
static void ConsoleFill(U32 Row, U32 Column, U32 Length) {
    STR SpaceBuffer[32];
    U32 Index;

    for (Index = 0; Index < (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0])); Index++) {
        SpaceBuffer[Index] = STR_SPACE;
    }

    while (Length > 0) {
        U32 Chunk = Length;

        if (Chunk > (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0]))) {
            Chunk = (U32)(sizeof(SpaceBuffer) / sizeof(SpaceBuffer[0]));
        }

        ConsolePrintLine(Row, Column, SpaceBuffer, Chunk);

        Column += Chunk;
        Length -= Chunk;
    }
}

/***************************************************************************/

/**
 * @brief Print a single character inside the menu line and advance the cursor.
 * @param Row Target console row.
 * @param Column Pointer to the current column offset.
 * @param Character Character to print.
 * @param Width Total width available on the row.
 */
static void PrintMenuChar(U32 Row, U32* Column, STR Character, U32 Width) {
    STR Buffer[1];

    if (Column == NULL) return;
    if (*Column >= Width) return;

    Buffer[0] = Character;
    ConsolePrintLine(Row, *Column, Buffer, 1);
    (*Column)++;
}

/***************************************************************************/

/**
 * @brief Print a string inside the menu line respecting the available width.
 * @param Row Target console row.
 * @param Column Pointer to the current column offset.
 * @param Text Text to display.
 * @param Width Total width available on the row.
 */
static void PrintMenuText(U32 Row, U32* Column, LPCSTR Text, U32 Width) {
    U32 Length;
    U32 Remaining;

    if (Column == NULL || Text == NULL) return;
    if (*Column >= Width) return;

    Remaining = Width - *Column;
    Length = StringLength(Text);
    if (Length > Remaining) {
        Length = Remaining;
    }

    if (Length == 0) return;

    ConsolePrintLine(Row, *Column, Text, Length);
    *Column += Length;
}

/***************************************************************************/

/**
 * @brief Render the editor title bar, including file name and modified flag.
 * @param File Currently active file.
 * @param ForeColor Foreground color to use.
 * @param BackColor Background color to use.
 * @param Width Console width in characters.
 */
static void RenderTitleBar(LPEDITFILE File, U32 ForeColor, U32 BackColor, U32 Width) {
    I32 Line;
    U32 Column = 0;
    LPCSTR Name;

    if (EDIT_TITLE_HEIGHT <= 0) return;

    SetConsoleForeColor(ForeColor);
    SetConsoleBackColor(BackColor);

    for (Line = 0; Line < EDIT_TITLE_HEIGHT; Line++) {
        ConsoleFill((U32)Line, 0, Width);
    }

    if (File && File->Modified && Column < Width) {
        STR Modified = '*';
        ConsolePrintLine(0, Column, &Modified, 1);
        Column++;
    }

    if (File && File->Name) {
        Name = File->Name;
    } else {
        Name = TEXT("<untitled>");
    }

    if (Name && Column < Width) {
        U32 NameLength = StringLength(Name);
        if (NameLength > (Width - Column)) {
            NameLength = Width - Column;
        }
        ConsolePrintLine(0, Column, Name, NameLength);
    }
}

/***************************************************************************/

/**
 * @brief Render the editor command menu at the bottom of the screen.
 * @param ForeColor Foreground color to use.
 * @param BackColor Background color to use.
 * @param Width Console width in characters.
 */
static void RenderMenu(U32 ForeColor, U32 BackColor, U32 Width) {
    U32 Item;
    I32 Line;
    U32 Column;
    U32 MenuRow = EDIT_TITLE_HEIGHT + MAX_LINES;

    SetConsoleForeColor(ForeColor);
    SetConsoleBackColor(BackColor);

    for (Line = 0; Line < EDIT_MENU_HEIGHT; Line++) {
        ConsoleFill((U32)(EDIT_TITLE_HEIGHT + MAX_LINES + Line), 0, Width);
    }

    Column = 0;

    for (Item = 0; Item < MenuItems && Column < Width; Item++) {
        LPEDITMENUITEM MenuItem = &Menu[Item];
        LPCSTR ModifierName = NULL;
        LPCSTR KeyName = NULL;

        if (MenuItem->Modifier.VirtualKey != VK_NONE) {
            ModifierName = GetKeyName(MenuItem->Modifier.VirtualKey);
            PrintMenuText(MenuRow, &Column, ModifierName, Width);
            PrintMenuChar(MenuRow, &Column, '+', Width);
        }

        KeyName = GetKeyName(MenuItem->Key.VirtualKey);
        PrintMenuText(MenuRow, &Column, KeyName, Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);

        PrintMenuText(MenuRow, &Column, MenuItem->Name, Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);
        PrintMenuChar(MenuRow, &Column, ' ', Width);
    }
}

/***************************************************************************/

/**
 * @brief Handle the exit command from the menu.
 * @param Context Active editor context (unused).
 * @return TRUE to terminate the editor loop.
 */
static BOOL CommandExit(LPEDITCONTEXT Context) {
    UNUSED(Context);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Save the current file to disk.
 * @param File File to save.
 * @return TRUE on success, FALSE on error.
 */
static BOOL SaveFile(LPEDITFILE File) {
    FILE_OPEN_INFO Info;
    FILE_OPERATION Operation;
    LPLISTNODE Node;
    LPEDITLINE Line;
    HANDLE Handle;
    U8 CRLF[2] = {13, 10};

    if (File == NULL || File->Name == NULL) return FALSE;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Name = File->Name;
    Info.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;

    Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&Info));
    if (Handle) {
        LPEDITLINE LastContentLine = NULL;

        for (Node = File->Lines->Last; Node; Node = Node->Prev) {
            Line = (LPEDITLINE)Node;
            if (Line->NumChars > 0) {
                LastContentLine = Line;
                break;
            }
        }

        if (LastContentLine) {
            for (Node = File->Lines->First; Node; Node = Node->Next) {
                Line = (LPEDITLINE)Node;

                Operation.Header.Size = sizeof Operation;
                Operation.Header.Version = EXOS_ABI_VERSION;
                Operation.Header.Flags = 0;
                Operation.File = Handle;
                Operation.Buffer = Line->Chars;
                Operation.NumBytes = Line->NumChars;
                DoSystemCall(SYSCALL_WriteFile, SYSCALL_PARAM(&Operation));

                Operation.Buffer = CRLF;
                Operation.NumBytes = 2;
                DoSystemCall(SYSCALL_WriteFile, SYSCALL_PARAM(&Operation));

                if (Line == LastContentLine) {
                    break;
                }
            }
        }

        File->Modified = FALSE;
        DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Handler for the save command.
 * @param Context Active editor context.
 * @return TRUE if the file was saved.
 */
static BOOL CommandSave(LPEDITCONTEXT Context) { return SaveFile(Context->Current); }

/***************************************************************************/

/**
 * @brief Cut the current line or selection into the clipboard.
 * @param Context Active editor context.
 * @return FALSE because the editor loop continues running.
 */
static BOOL CommandCut(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    LPEDITLINE Line;
    LPLISTNODE Node;
    BOOL HasNextLine;
    POINT CursorPosition;
    I32 LineY;
    I32 Length;
    LPSTR Buffer = NULL;
    I32 Index;
    BOOL Modified = FALSE;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;

    if (SelectionHasRange(File)) {
        if (CopySelectionToClipboard(Context)) {
            DeleteSelection(File);
        }
        return FALSE;
    }

    CursorPosition = GetAbsoluteCursor(File);
    LineY = CursorPosition.Y;

    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return FALSE;

    Node = (LPLISTNODE)Line;
    HasNextLine = (Node != NULL && Node->Next != NULL);

    Length = Line->NumChars;
    if (HasNextLine) {
        Length++;
    }

    if (Length > 0) {
        Buffer = (LPSTR)HeapAlloc(Length);
        if (Buffer == NULL) return FALSE;

        for (Index = 0; Index < Line->NumChars; Index++) {
            Buffer[Index] = Line->Chars[Index];
        }

        if (HasNextLine) {
            Buffer[Line->NumChars] = EDIT_CLIPBOARD_NEWLINE;
        }
    }

    HeapFree(Context->Clipboard);
    Context->Clipboard = Buffer;
    Context->ClipboardSize = Length;

    if (HasNextLine) {
        File->SelStart.X = 0;
        File->SelStart.Y = LineY;
        File->SelEnd.X = 0;
        File->SelEnd.Y = LineY + 1;
        DeleteSelection(File);
        CollapseSelectionToCursor(File);
        return FALSE;
    }

    if (File->Lines->NumItems > 1) {
        LPLISTNODE PrevNode = Node ? Node->Prev : NULL;

        ListEraseItem(File->Lines, Line);
        Modified = TRUE;

        if (PrevNode) {
            MoveCursorToAbsolute(File, 0, LineY - 1);
        } else {
            MoveCursorToAbsolute(File, 0, 0);
        }
    } else {
        Line->NumChars = 0;
        MoveCursorToAbsolute(File, 0, LineY);
        if (Length > 0) {
            Modified = TRUE;
        }
    }

    if (Modified) {
        File->Modified = TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Copy the current selection into the clipboard.
 * @param Context Active editor context.
 * @return FALSE because the editor loop continues running.
 */
static BOOL CommandCopy(LPEDITCONTEXT Context) {
    if (Context == NULL) return FALSE;
    CopySelectionToClipboard(Context);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Paste clipboard content at the cursor position.
 * @param Context Active editor context.
 * @return FALSE because the editor loop continues running.
 */
static BOOL CommandPaste(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    I32 Index;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;
    if (Context->Clipboard == NULL) return FALSE;
    if (Context->ClipboardSize <= 0) return FALSE;

    for (Index = 0; Index < Context->ClipboardSize; Index++) {
        STR Character = Context->Clipboard[Index];
        if (Character == EDIT_CLIPBOARD_NEWLINE) {
            AddLine(File);
        } else {
            AddCharacter(File, Character);
        }
    }

    return FALSE;
}

U32 Edit(U32 NumArguments, LPCSTR* Arguments, BOOL LineNumbers) {
    LPEDITCONTEXT Context;
    LPEDITFILE File;
    U32 Index;

    Context = NewEditContext();

    if (Context == NULL) {
        return DF_RETURN_GENERIC;
    }

    Context->ShowLineNumbers = LineNumbers;

    if (NumArguments && Arguments) {
        for (Index = 0; Index < NumArguments; Index++) {
            OpenTextFile(Context, Arguments[Index]);
        }
    } else {
        File = NewEditFile();
        ListAddItem(Context->Files, File);
        Context->Current = File;
    }

    Loop(Context);

    DeleteEditContext(Context);
    ClearConsole();

    return 0;
}
