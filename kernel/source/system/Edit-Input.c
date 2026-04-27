
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
#include "core/Kernel.h"
#include "User.h"
#include "drivers/input/Keyboard.h"
#include "process/Task.h"

/***************************************************************************/
POINT GetAbsoluteCursor(const LPEDITFILE File) {
    POINT Position;

    Position.X = 0;
    Position.Y = 0;
    if (File == NULL) return Position;

    Position.X = File->Left + File->Cursor.X;
    Position.Y = File->Top + File->Cursor.Y;

    return Position;
}

/***************************************************************************/

/**
 * @brief Check whether the current selection spans at least one character.
 * @param File File holding the selection state.
 * @return TRUE if the selection has a non-zero range, FALSE otherwise.
 */
BOOL SelectionHasRange(const LPEDITFILE File) {
    if (File == NULL) return FALSE;
    return (File->SelStart.X != File->SelEnd.X) || (File->SelStart.Y != File->SelEnd.Y);
}

/***************************************************************************/

/**
 * @brief Order selection boundaries so Start is before End.
 * @param File File providing the selection information.
 * @param Start Output start point of the selection.
 * @param End Output end point of the selection.
 */
void NormalizeSelection(const LPEDITFILE File, POINT* Start, POINT* End) {
    POINT Temp;

    if (File == NULL || Start == NULL || End == NULL) return;

    *Start = File->SelStart;
    *End = File->SelEnd;

    if ((Start->Y > End->Y) || (Start->Y == End->Y && Start->X > End->X)) {
        Temp = *Start;
        *Start = *End;
        *End = Temp;
    }
}

/***************************************************************************/

/**
 * @brief Reduce the selection to the current cursor position.
 * @param File File containing the selection.
 */
void CollapseSelectionToCursor(LPEDITFILE File) {
    POINT Position;

    if (File == NULL) return;

    Position = GetAbsoluteCursor(File);
    File->SelStart = Position;
    File->SelEnd = Position;
}

/***************************************************************************/

/**
 * @brief Update selection endpoints after the cursor moves.
 * @param File File being edited.
 * @param Extend TRUE to extend the selection, FALSE to collapse it.
 * @param Previous Cursor position before the move.
 */
void UpdateSelectionAfterMove(LPEDITFILE File, BOOL Extend, POINT Previous) {
    if (File == NULL) return;

    if (Extend) {
        if (SelectionHasRange(File) == FALSE) {
            File->SelStart = Previous;
        }
        File->SelEnd = GetAbsoluteCursor(File);
    } else {
        CollapseSelectionToCursor(File);
    }
}

/***************************************************************************/

/**
 * @brief Move the cursor to absolute coordinates and adjust the viewport.
 * @param File File whose cursor must be moved.
 * @param Column Absolute column target.
 * @param Line Absolute line target.
 */
void MoveCursorToAbsolute(LPEDITFILE File, I32 Column, I32 Line) {
    if (File == NULL) return;

    if (Line < 0) Line = 0;
    if (Column < 0) Column = 0;

    if (Line < File->Top) {
        File->Top = Line;
    } else if (Line >= (File->Top + (I32)MAX_LINES)) {
        File->Top = Line - ((I32)MAX_LINES - 1);
        if (File->Top < 0) File->Top = 0;
    }

    if (Column < File->Left) {
        File->Left = Column;
    } else if (Column >= (File->Left + (I32)MAX_COLUMNS)) {
        File->Left = Column - ((I32)MAX_COLUMNS - 1);
        if (File->Left < 0) File->Left = 0;
    }

    File->Cursor.Y = Line - File->Top;
    File->Cursor.X = Column - File->Left;
    if (File->Cursor.Y < 0) File->Cursor.Y = 0;
    if (File->Cursor.X < 0) File->Cursor.X = 0;

    CollapseSelectionToCursor(File);
}
/***************************************************************************/

/**
 * @brief Ensure a line has enough capacity for a given index.
 * @param Line Line to check.
 * @param Size Desired capacity.
 * @return TRUE on success, FALSE if memory allocation failed.
 */
static BOOL CheckLineSize(LPEDITLINE Line, I32 Size) {
    LPSTR Text;
    I32 NewSize;

    if (Size >= Line->MaxChars) {
        NewSize = ((Size / 8) + 1) * 8;
        Text = (LPSTR)HeapAlloc(NewSize);
        if (Text == NULL) return FALSE;
        StringCopyNum(Text, Line->Chars, Line->NumChars);
        HeapFree(Line->Chars);
        Line->MaxChars = NewSize;
        Line->Chars = Text;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Append characters from a buffer to an edit line expanding tabs.
 * @param Line Destination line.
 * @param Data Source buffer.
 * @param Length Number of characters to append from the buffer.
 */
static void AppendBufferToLine(LPEDITLINE Line, LPCSTR Data, U32 Length) {
    U32 Index;

    if (Line == NULL || Data == NULL) return;

    for (Index = 0; Index < Length; Index++) {
        if (Data[Index] == STR_TAB) {
            if (CheckLineSize(Line, Line->NumChars + 0x04) == FALSE) return;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
            Line->Chars[Line->NumChars++] = STR_SPACE;
        } else {
            if (CheckLineSize(Line, Line->NumChars + 0x01) == FALSE) return;
            Line->Chars[Line->NumChars++] = Data[Index];
        }
    }
}

/***************************************************************************/

/**
 * @brief Fill the current line with spaces up to the cursor position.
 * @param File Active file.
 * @param Line Line to modify.
 */
static void FillToCursor(LPEDITFILE File, LPEDITLINE Line) {
    I32 Index;
    I32 LineX;

    LineX = File->Left + File->Cursor.X;
    if (LineX <= Line->NumChars) return;
    if (CheckLineSize(Line, LineX) == FALSE) return;
    for (Index = Line->NumChars; Index < LineX; Index++) {
        Line->Chars[Index] = STR_SPACE;
    }
    Line->NumChars = LineX;
}

/***************************************************************************/

/**
 * @brief Ensure the file contains a line at the requested index.
 * @param File Active file.
 * @param LineIndex Zero-based line index to retrieve.
 * @return Pointer to the ensured line or NULL on failure.
 */
static LPEDITLINE EnsureLineAt(LPEDITFILE File, I32 LineIndex) {
    LPEDITLINE Line;

    if (File == NULL) return NULL;
    if (LineIndex < 0) return NULL;

    while ((I32)File->Lines->NumItems <= LineIndex) {
        Line = NewEditLine(8);
        if (Line == NULL) return NULL;
        if (ListAddItem(File->Lines, Line) == FALSE) {
            DeleteEditLine(Line);
            return NULL;
        }
    }

    return (LPEDITLINE)ListGetItem(File->Lines, LineIndex);
}

/**
 * @brief Delete the currently selected text range.
 * @param File Active file containing the selection.
 */
void DeleteSelection(LPEDITFILE File) {
    POINT Start;
    POINT End;
    LPEDITLINE StartLine;
    LPEDITLINE EndLine;
    I32 StartColumn;
    I32 EndColumn;
    I32 TailLength;
    I32 LineIndex;
    I32 Remaining;
    I32 Offset;
    BOOL Modified = FALSE;

    if (File == NULL) return;
    if (SelectionHasRange(File) == FALSE) return;

    NormalizeSelection(File, &Start, &End);

    StartLine = ListGetItem(File->Lines, Start.Y);
    if (StartLine == NULL) return;

    if (Start.Y == End.Y) {
        StartColumn = Start.X;
        EndColumn = End.X;

        if (StartColumn > StartLine->NumChars) StartColumn = StartLine->NumChars;
        if (EndColumn > StartLine->NumChars) EndColumn = StartLine->NumChars;
        if (EndColumn <= StartColumn) {
            MoveCursorToAbsolute(File, StartColumn, Start.Y);
            return;
        }

        Remaining = StartLine->NumChars - EndColumn;
        for (Offset = 0; Offset < Remaining; Offset++) {
            StartLine->Chars[StartColumn + Offset] = StartLine->Chars[EndColumn + Offset];
        }
        StartLine->NumChars -= (EndColumn - StartColumn);

        MoveCursorToAbsolute(File, StartColumn, Start.Y);
        Modified = TRUE;
    } else {
        StartColumn = Start.X;
        if (StartColumn > StartLine->NumChars) StartColumn = StartLine->NumChars;

        EndLine = ListGetItem(File->Lines, End.Y);
        EndColumn = End.X;
        TailLength = 0;

        if (EndLine) {
            if (EndColumn > EndLine->NumChars) EndColumn = EndLine->NumChars;
            if (EndColumn < 0) EndColumn = 0;
            TailLength = EndLine->NumChars - EndColumn;
            if (TailLength < 0) TailLength = 0;
            if (TailLength > 0) {
                if (CheckLineSize(StartLine, StartColumn + TailLength) == FALSE) return;
            }
        }

        StartLine->NumChars = StartColumn;
        Modified = TRUE;

        for (LineIndex = End.Y - 1; LineIndex > Start.Y; LineIndex--) {
            LPEDITLINE MiddleLine = ListGetItem(File->Lines, LineIndex);
            if (MiddleLine) {
                ListEraseItem(File->Lines, MiddleLine);
            }
        }

        if (EndLine && EndLine != StartLine) {
            for (Offset = 0; Offset < TailLength; Offset++) {
                StartLine->Chars[StartLine->NumChars++] = EndLine->Chars[EndColumn + Offset];
            }
            ListEraseItem(File->Lines, EndLine);
        }

        MoveCursorToAbsolute(File, StartColumn, Start.Y);
    }

    if (Modified) {
        File->Modified = TRUE;
    }
}

/***************************************************************************/

/**
 * @brief Copy the selected text into the editor clipboard.
 * @param Context Active editor context.
 * @return TRUE if text was copied, FALSE otherwise.
 */
BOOL CopySelectionToClipboard(LPEDITCONTEXT Context) {
    LPEDITFILE File;
    POINT Start;
    POINT End;
    I32 LineIndex;
    I32 SegmentStart;
    I32 SegmentEnd;
    I32 Length;
    I32 Position;
    LPEDITLINE Line;
    LPSTR Buffer;

    if (Context == NULL) return FALSE;

    File = Context->Current;
    if (File == NULL) return FALSE;
    if (SelectionHasRange(File) == FALSE) return FALSE;

    NormalizeSelection(File, &Start, &End);

    Length = 0;

    for (LineIndex = Start.Y; LineIndex <= End.Y; LineIndex++) {
        Line = ListGetItem(File->Lines, LineIndex);
        if (Line == NULL) break;

        SegmentStart = 0;
        SegmentEnd = Line->NumChars;

        if (LineIndex == Start.Y) {
            SegmentStart = Start.X;
            if (SegmentStart > Line->NumChars) SegmentStart = Line->NumChars;
        }

        if (LineIndex == End.Y) {
            SegmentEnd = End.X;
            if (SegmentEnd > Line->NumChars) SegmentEnd = Line->NumChars;
        }

        if (LineIndex == Start.Y && LineIndex != End.Y) {
            SegmentEnd = Line->NumChars;
        }

        if (SegmentEnd < SegmentStart) SegmentEnd = SegmentStart;

        Length += (SegmentEnd - SegmentStart);

        if (LineIndex < End.Y) {
            Length++;
        }
    }

    if (Length <= 0) return FALSE;

    Buffer = (LPSTR)HeapAlloc(Length);
    if (Buffer == NULL) return FALSE;

    Position = 0;

    for (LineIndex = Start.Y; LineIndex <= End.Y && Position < Length; LineIndex++) {
        Line = ListGetItem(File->Lines, LineIndex);
        if (Line == NULL) break;

        SegmentStart = 0;
        SegmentEnd = Line->NumChars;

        if (LineIndex == Start.Y) {
            SegmentStart = Start.X;
            if (SegmentStart > Line->NumChars) SegmentStart = Line->NumChars;
        }

        if (LineIndex == End.Y) {
            SegmentEnd = End.X;
            if (SegmentEnd > Line->NumChars) SegmentEnd = Line->NumChars;
        }

        if (LineIndex == Start.Y && LineIndex != End.Y) {
            SegmentEnd = Line->NumChars;
        }

        if (SegmentEnd < SegmentStart) SegmentEnd = SegmentStart;

        for (; SegmentStart < SegmentEnd && Position < Length; SegmentStart++) {
            Buffer[Position++] = Line->Chars[SegmentStart];
        }

        if (LineIndex < End.Y && Position < Length) {
            Buffer[Position++] = EDIT_CLIPBOARD_NEWLINE;
        }
    }

    HeapFree(Context->Clipboard);
    Context->Clipboard = Buffer;
    Context->ClipboardSize = Position;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Insert a character at the cursor position.
 * @param File Active file.
 * @param ASCIICode Character to insert.
 */
void AddCharacter(LPEDITFILE File, STR ASCIICode) {
    LPEDITLINE Line;
    I32 Index;
    I32 LineX;
    I32 NewLength;
    I32 LineY;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
    }

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    if (LineY < 0) LineY = 0;

    Line = EnsureLineAt(File, LineY);
    if (Line == NULL) return;

    if (LineX <= Line->NumChars) {
        NewLength = Line->NumChars + 1;
    } else {
        NewLength = LineX;
    }

    // Resize the line if too small

    if (CheckLineSize(Line, NewLength) == FALSE) return;

    if (LineX > Line->NumChars) {
        FillToCursor(File, Line);
        Line->Chars[Line->NumChars++] = ASCIICode;
    } else {
        // Insert the character
        for (Index = Line->NumChars + 1; Index > LineX; Index--) {
            Line->Chars[Index] = Line->Chars[Index - 1];
        }
        Line->Chars[Index] = ASCIICode;
        Line->NumChars++;
    }

    // Update the cursor

    File->Cursor.X++;
    if (File->Cursor.X >= (I32)MAX_COLUMNS) {
        File->Left++;
        File->Cursor.X--;
    }
    CollapseSelectionToCursor(File);
    File->Modified = TRUE;
}

/***************************************************************************/

/**
 * @brief Remove a character relative to the cursor.
 * @param File Active file.
 * @param Flag 0 deletes before cursor, non-zero deletes at cursor.
 */
void DeleteCharacter(LPEDITFILE File, I32 Flag) {
    LPLISTNODE Node;
    LPEDITLINE Line;
    LPEDITLINE NextLine;
    LPEDITLINE PrevLine;
    I32 LineX;
    I32 LineY;
    I32 NewLength;
    I32 Index;
    BOOL Modified = FALSE;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
        return;
    }

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return;

    if (Flag == 0) {
        if (LineX > 0) {
            for (Index = LineX; Index < Line->NumChars; Index++) {
                Line->Chars[Index - 1] = Line->Chars[Index];
            }
            Line->NumChars--;
            File->Cursor.X--;
            Modified = TRUE;
        } else {
            Node = (LPLISTNODE)Line;
            Node = Node->Prev;
            if (Node == NULL) return;
            PrevLine = (LPEDITLINE)Node;
            File->Cursor.X = PrevLine->NumChars;
            File->Cursor.Y--;
            NewLength = PrevLine->NumChars + Line->NumChars;
            if (CheckLineSize(PrevLine, NewLength) == FALSE) return;
            for (Index = 0; Index < Line->NumChars; Index++) {
                PrevLine->Chars[PrevLine->NumChars] = Line->Chars[Index];
                PrevLine->NumChars++;
            }
            ListEraseItem(File->Lines, Line);
            Modified = TRUE;
        }
    } else {
        if (Line->NumChars == 0) {
            ListEraseItem(File->Lines, Line);
            Modified = TRUE;
        } else {
            if (LineX >= Line->NumChars) {
                NextLine = ListGetItem(File->Lines, LineY + 1);
                if (NextLine == NULL) return;
                FillToCursor(File, Line);
                if (CheckLineSize(Line, Line->NumChars + NextLine->NumChars) == FALSE) return;
                for (Index = 0; Index < NextLine->NumChars; Index++) {
                    Line->Chars[Line->NumChars++] = NextLine->Chars[Index];
                }
                ListEraseItem(File->Lines, NextLine);
                Modified = TRUE;
            } else {
                for (Index = LineX + 1; Index < Line->NumChars; Index++) {
                    Line->Chars[Index - 1] = Line->Chars[Index];
                }
                Line->NumChars--;
                Modified = TRUE;
            }
        }
    }
    CollapseSelectionToCursor(File);
    if (Modified) {
        File->Modified = TRUE;
    }
}

/***************************************************************************/

/**
 * @brief Split the current line at the cursor position.
 * @param File Active file.
 */
void AddLine(LPEDITFILE File) {
    LPEDITLINE Line;
    LPEDITLINE NewLine;
    LPLISTNODE Node;
    I32 LineX;
    I32 LineY;
    I32 Index;
    BOOL Modified = FALSE;

    if (File == NULL) return;

    if (SelectionHasRange(File)) {
        DeleteSelection(File);
    }

    LineX = File->Left + File->Cursor.X;
    LineY = File->Top + File->Cursor.Y;
    Line = ListGetItem(File->Lines, LineY);
    if (Line == NULL) return;

    if (LineX == 0) {
        NewLine = NewEditLine(8);
        if (NewLine == NULL) return;

        Node = (LPLISTNODE)Line;
        if (Node == NULL || Node->Prev == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node->Prev, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
        Modified = TRUE;
    } else if (LineX >= Line->NumChars) {
        NewLine = NewEditLine(8);
        if (NewLine == NULL) return;

        Node = (LPLISTNODE)Line;
        if (Node == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
        Modified = TRUE;
    } else {
        NewLine = NewEditLine(Line->NumChars);
        if (NewLine == NULL) return;

        for (Index = LineX; Index < Line->NumChars; Index++) {
            NewLine->Chars[Index - LineX] = Line->Chars[Index];
            NewLine->NumChars++;
        }

        Line->NumChars = LineX;

        Node = (LPLISTNODE)Line;
        if (Node == NULL) {
            DeleteEditLine(NewLine);
            return;
        }

        ListAddAfter(File->Lines, Node, NewLine);

        File->Cursor.X = 0;
        File->Cursor.Y++;
        Modified = TRUE;
    }

    CollapseSelectionToCursor(File);
    if (Modified) {
        File->Modified = TRUE;
    }
}

/***************************************************************************/

/**
 * @brief Move cursor to the end of current line.
 * @param File Active file.
 */
void GotoEndOfLine(LPEDITFILE File) {
    LPEDITLINE Line;
    I32 TargetColumn;
    I32 MaxVisible;
    I32 LineIndex;

    if (File == NULL) return;

    LineIndex = File->Top + File->Cursor.Y;
    if (LineIndex < 0) LineIndex = 0;

    Line = ListGetItem(File->Lines, LineIndex);
    if (Line == NULL) {
        File->Left = 0;
        File->Cursor.X = 0;
        return;
    }

    TargetColumn = Line->NumChars;
    if (TargetColumn <= 0) {
        File->Left = 0;
        File->Cursor.X = 0;
        return;
    }

    MaxVisible = MAX_COLUMNS;
    if (MaxVisible < 1) MaxVisible = 1;

    if (TargetColumn <= MaxVisible) {
        File->Left = 0;
    } else if (TargetColumn < File->Left) {
        File->Left = TargetColumn;
    }

    if ((TargetColumn - File->Left) >= MaxVisible) {
        File->Left = TargetColumn - (MaxVisible - 1);
    }

    if (File->Left < 0) File->Left = 0;

    File->Cursor.X = TargetColumn - File->Left;
    if (File->Cursor.X < 0) File->Cursor.X = 0;
    if (File->Cursor.X > MaxVisible) File->Cursor.X = MaxVisible;
}

/***************************************************************************/

/**
 * @brief Move cursor to the beginning of the file.
 * @param File Active file.
 */
void GotoStartOfFile(LPEDITFILE File) {
    if (File == NULL) return;

    File->Left = 0;
    File->Top = 0;
    File->Cursor.X = 0;
    File->Cursor.Y = 0;
}

/***************************************************************************/

/**
 * @brief Move cursor to the start of current line.
 * @param File Active file.
 */
void GotoStartOfLine(LPEDITFILE File) {
    File->Left = 0;
    File->Cursor.X = 0;
}

/***************************************************************************/

/**
 * @brief Move cursor to the end of the file.
 * @param File Active file.
 */
void GotoEndOfFile(LPEDITFILE File) {
    I32 LastLineIndex;
    I32 VisibleRows;

    if (File == NULL || File->Lines == NULL) return;

    if (File->Lines->NumItems == 0) {
        File->Left = 0;
        File->Top = 0;
        File->Cursor.X = 0;
        File->Cursor.Y = 0;
        return;
    }

    LastLineIndex = (I32)File->Lines->NumItems - 1;
    VisibleRows = MAX_LINES;
    if (VisibleRows < 1) VisibleRows = 1;

    if (LastLineIndex < VisibleRows) {
        File->Top = 0;
        File->Cursor.Y = LastLineIndex;
    } else {
        File->Top = LastLineIndex - (VisibleRows - 1);
        if (File->Top < 0) File->Top = 0;
        File->Cursor.Y = LastLineIndex - File->Top;
        if (File->Cursor.Y >= VisibleRows) {
            File->Cursor.Y = VisibleRows - 1;
        }
    }

    File->Left = 0;
    GotoEndOfLine(File);
}

/***************************************************************************/

/**
 * @brief Main editor loop handling user input.
 * @param Context Editor context.
 * @return Exit code.
 */
I32 Loop(LPEDITCONTEXT Context) {
    KEYCODE KeyCode;
    MESSAGE_INFO Message;
    U32 Item;
    BOOL Handled;

    Render(Context);

    FOREVER {
        MemorySet(&Message, 0, sizeof(Message));
        Message.Header.Size = sizeof(Message);
        Message.Header.Version = EXOS_ABI_VERSION;
        Message.Header.Flags = 0;
        Message.Target = NULL;

        if (KernelGetMessage(&Message) == FALSE) {
            continue;
        }

        if (Message.Message != EWM_KEYDOWN) {
            continue;
        }

        KeyCode.VirtualKey = Message.Param1;
        KeyCode.ASCIICode = (STR)Message.Param2;

            Handled = FALSE;
            for (Item = 0; Item < MenuItems; Item++) {
                if (KeyCode.VirtualKey == Menu[Item].Key.VirtualKey) {
                    if (Menu[Item].Modifier.VirtualKey == VK_NONE || GetKeyCodeDown(Menu[Item].Modifier)) {
                        Handled = TRUE;
                        if (Menu[Item].Function(Context)) {
                            return 0;
                        }
                        Render(Context);
                    }
                    break;
                }
            }

            if (Handled) continue;

            if (Context->Current == NULL) continue;

            BOOL ShiftDown = GetKeyCodeDown(ShiftKey);
            POINT PreviousPosition = GetAbsoluteCursor(Context->Current);

            if (KeyCode.VirtualKey == VK_DOWN) {
                Context->Current->Cursor.Y++;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_UP) {
                Context->Current->Cursor.Y--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_RIGHT) {
                Context->Current->Cursor.X++;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_LEFT) {
                Context->Current->Cursor.X--;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_PAGEDOWN) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top += Lines;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_PAGEUP) {
                I32 Lines = (Console.Height * 8) / 10;
                Context->Current->Top -= Lines;
                if (Context->Current->Top < 0) Context->Current->Top = 0;
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_HOME) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoStartOfFile(Context->Current);
                } else {
                    GotoStartOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_END) {
                if (GetKeyCodeDown(ControlKey)) {
                    GotoEndOfFile(Context->Current);
                } else {
                    GotoEndOfLine(Context->Current);
                }
                UpdateSelectionAfterMove(Context->Current, ShiftDown, PreviousPosition);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                DeleteCharacter(Context->Current, 0);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_DELETE) {
                DeleteCharacter(Context->Current, 1);
                Render(Context);
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                AddLine(Context->Current);
                Render(Context);
            } else {
                switch (KeyCode.ASCIICode) {
                    default: {
                        if (KeyCode.ASCIICode >= STR_SPACE) {
                            AddCharacter(Context->Current, KeyCode.ASCIICode);
                            Render(Context);
                        }
                    } break;
                }
            }
        }
    }

 
/***************************************************************************/

/**
 * @brief Load a text file into the editor.
 * @param Context Editor context.
 * @param Name Path of the file to open.
 * @return TRUE on success, FALSE on error.
 */
BOOL OpenTextFile(LPEDITCONTEXT Context, LPCSTR Name) {
    FILE_OPEN_INFO Info;
    FILE_OPERATION FileOperation;
    LPEDITFILE File;
    LPEDITLINE Line;
    HANDLE Handle;
    LPSTR LineStart;
    LPSTR LineData;
    U8* Buffer;
    U32 FileSize;
    U32 LineSize;
    U32 FinalLineSize;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.Name = Name;
    Info.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&Info));

    if (Handle) {
        FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));
        if (FileSize) {
            Buffer = HeapAlloc(FileSize + 1);
            if (Buffer) {
                Buffer[FileSize] = STR_NULL;

                FileOperation.Header.Size = sizeof FileOperation;
                FileOperation.Header.Version = EXOS_ABI_VERSION;
                FileOperation.Header.Flags = 0;
                FileOperation.File = Handle;
                FileOperation.NumBytes = FileSize;
                FileOperation.Buffer = Buffer;

                if (DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation))) {
                    File = NewEditFile();
                    if (File) {
                        File->Name = HeapAlloc(StringLength(Name) + 1);
                        if (File->Name) {
                            StringCopy(File->Name, Name);
                        }
                        ListAddItem(Context->Files, File);
                        Context->Current = File;

                        ListReset(File->Lines);

                        LineData = (LPSTR)Buffer;
                        LineStart = LineData;
                        LineSize = 0;
                        FinalLineSize = 0;

                        while (*LineData) {
                            if (*LineData == 0x0D || *LineData == 0x0A) {
                                Line = NewEditLine(FinalLineSize ? FinalLineSize : 0x01);
                                if (Line) {
                                    AppendBufferToLine(Line, LineStart, LineSize);
                                    ListAddItem(File->Lines, Line);
                                }

                                if (*LineData == 0x0D && LineData[1] == 0x0A) {
                                    LineData += 0x02;
                                } else {
                                    LineData++;
                                }

                                LineStart = LineData;
                                LineSize = 0x00;
                                FinalLineSize = 0x00;
                            } else if (*LineData == STR_TAB) {
                                LineData++;
                                LineSize++;
                                FinalLineSize += 0x04;
                            } else {
                                LineData++;
                                LineSize++;
                                FinalLineSize++;
                            }
                        }

                        if (LineSize > 0 || File->Lines->NumItems == 0) {
                            Line = NewEditLine(FinalLineSize ? FinalLineSize : 0x01);
                            if (Line) {
                                AppendBufferToLine(Line, LineStart, LineSize);
                                ListAddItem(File->Lines, Line);
                            }
                        }
                    }
                }
                HeapFree(Buffer);
            }
        }
        if (File) {
            File->Modified = FALSE;
        }
        DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
    } else {
        File = NewEditFile();
        if (File) {
            if (Name) {
                File->Name = HeapAlloc(StringLength(Name) + 1);
                if (File->Name) {
                    StringCopy(File->Name, Name);
                }
            }
            ListAddItem(Context->Files, File);
            Context->Current = File;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Entry point for the text editor utility.
 * @param NumArguments Number of command line arguments.
 * @param Arguments Array of argument strings.
 * @param LineNumbers TRUE to enable line numbers display.
 * @return 0 on success or error code.
 */
