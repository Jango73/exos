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


    Console regions and layout

\************************************************************************/

#include "Console-Internal.h"
#include "memory/Heap.h"
#include "core/Kernel.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"
#include "text/CoreString.h"
#include "drivers/input/Keyboard.h"
#include "input/VKey.h"
#include "process/Process-Control.h"
#include "process/Process.h"
#include "process/Task.h"
#include "system/System.h"

/***************************************************************************/

typedef struct tag_CONSOLE_ACTIVE_REGION_SNAPSHOT {
    BOOL IsValid;
    U32 RegionIndex;
    U32 RegionX;
    U32 RegionY;
    U32 RegionWidth;
    U32 RegionHeight;
    U32 CursorX;
    U32 CursorY;
    U32 ForeColor;
    U32 BackColor;
    U32 Blink;
    UINT TextCellCount;
    U16* TextBuffer;
} CONSOLE_ACTIVE_REGION_SNAPSHOT, *LPCONSOLE_ACTIVE_REGION_SNAPSHOT;

/***************************************************************************/

static U16 ConsoleComposeShadowCell(STR Char, U32 ForeColor, U32 BackColor, U32 Blink);
static U16 ConsoleComposeShadowBlankCell(U32 ForeColor, U32 BackColor, U32 Blink);
static BOOL ConsoleEnsureShadowBufferLocked(void);
static U16* ConsoleGetShadowCellLocked(U32 ScreenX, U32 ScreenY);

/***************************************************************************/

/**
 * @brief Encode one console cell in the canonical shadow buffer format.
 * @param Char Character stored in the cell.
 * @param ForeColor Foreground color index.
 * @param BackColor Background color index.
 * @param Blink Blink flag.
 * @return Packed cell value.
 */
static U16 ConsoleComposeShadowCell(STR Char, U32 ForeColor, U32 BackColor, U32 Blink) {
    U16 Attribute = (U16)((ForeColor | (BackColor << 0x04) | (Blink << 0x07)) << 0x08);
    return (U16)(U8)Char | Attribute;
}

/***************************************************************************/

/**
 * @brief Encode one blank console cell in the canonical shadow buffer format.
 * @param ForeColor Foreground color index.
 * @param BackColor Background color index.
 * @param Blink Blink flag.
 * @return Packed blank cell value.
 */
static U16 ConsoleComposeShadowBlankCell(U32 ForeColor, U32 BackColor, U32 Blink) {
    return ConsoleComposeShadowCell(STR_SPACE, ForeColor, BackColor, Blink);
}

/***************************************************************************/

/**
 * @brief Ensure the console-owned shadow text buffer matches screen geometry.
 * @return TRUE when the buffer is available.
 */
static BOOL ConsoleEnsureShadowBufferLocked(void) {
    UINT RequiredCellCount;
    UINT RequiredBytes;
    U16* NewBuffer;
    U16 BlankCell;
    UINT Index;

    if (Console.ScreenWidth == 0 || Console.ScreenHeight == 0) {
        return FALSE;
    }

    RequiredCellCount = (UINT)(Console.ScreenWidth * Console.ScreenHeight);
    if (RequiredCellCount == 0) {
        return FALSE;
    }

    if (Console.ShadowBuffer != NULL && Console.ShadowBufferCellCount == RequiredCellCount) {
        return TRUE;
    }

    RequiredBytes = RequiredCellCount * sizeof(U16);
    NewBuffer = (U16*)KernelHeapAlloc(RequiredBytes);
    if (NewBuffer == NULL) {
        return FALSE;
    }

    BlankCell = ConsoleComposeShadowBlankCell(Console.ForeColor, Console.BackColor, Console.Blink);
    for (Index = 0; Index < RequiredCellCount; Index++) {
        NewBuffer[Index] = BlankCell;
    }

    SAFE_USE(Console.ShadowBuffer) { KernelHeapFree(Console.ShadowBuffer); }
    Console.ShadowBuffer = NewBuffer;
    Console.ShadowBufferCellCount = RequiredCellCount;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return one mutable shadow-buffer cell for screen coordinates.
 * @param ScreenX Global console column.
 * @param ScreenY Global console row.
 * @return Pointer to the packed cell, or NULL when unavailable.
 */
static U16* ConsoleGetShadowCellLocked(U32 ScreenX, U32 ScreenY) {
    UINT Offset;

    if (ConsoleEnsureShadowBufferLocked() == FALSE) {
        return NULL;
    }

    if (ScreenX >= Console.ScreenWidth || ScreenY >= Console.ScreenHeight) {
        return NULL;
    }

    Offset = (UINT)((ScreenY * Console.ScreenWidth) + ScreenX);
    if (Offset >= Console.ShadowBufferCellCount) {
        return NULL;
    }

    return &Console.ShadowBuffer[Offset];
}

/**
 * @brief Resolve a console region into a mutable state descriptor.
 *
 * Provides pointers to the mutable fields for either the standard region
 * or a dedicated region, and copies the immutable layout data.
 *
 * @param Index Region index.
 * @param State Output state descriptor.
 * @return TRUE on success, FALSE on invalid index.
 */
BOOL ConsoleResolveRegionState(U32 Index, LPCONSOLE_REGION_STATE State) {
    LPCONSOLE_REGION Region;

    if (Index >= Console.RegionCount) return FALSE;

    Region = &Console.Regions[Index];

    State->X = Region->X;
    State->Y = Region->Y;
    State->Width = Region->Width;
    State->Height = Region->Height;

    if (Index == 0) {
        State->CursorX = &Console.CursorX;
        State->CursorY = &Console.CursorY;
        State->ForeColor = &Console.ForeColor;
        State->BackColor = &Console.BackColor;
        State->Blink = &Console.Blink;
        State->PagingEnabled = &Console.PagingEnabled;
        State->PagingActive = &Console.PagingActive;
        State->PagingRemaining = &Console.PagingRemaining;
    } else {
        State->CursorX = &Region->CursorX;
        State->CursorY = &Region->CursorY;
        State->ForeColor = &Region->ForeColor;
        State->BackColor = &Region->BackColor;
        State->Blink = &Region->Blink;
        State->PagingEnabled = &Region->PagingEnabled;
        State->PagingActive = &Region->PagingActive;
        State->PagingRemaining = &Region->PagingRemaining;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure the console-owned shadow buffer is available.
 * @return TRUE when the buffer is available.
 */
BOOL ConsoleEnsureShadowBuffer(void) {
    BOOL Result;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    Result = ConsoleEnsureShadowBufferLocked();
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return Result;
}

/***************************************************************************/

/**
 * @brief Write one cell into the canonical console shadow buffer.
 * @param RegionIndex Region index.
 * @param CellX Region-local column.
 * @param CellY Region-local row.
 * @param Char Character stored in the cell.
 * @param ForeColor Foreground color index.
 * @param BackColor Background color index.
 * @param Blink Blink flag.
 */
void ConsoleShadowWriteRegionCell(U32 RegionIndex, U32 CellX, U32 CellY, STR Char, U32 ForeColor, U32 BackColor, U32 Blink) {
    CONSOLE_REGION_STATE State;
    U16* Cell;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (CellX >= State.Width || CellY >= State.Height) return;

    Cell = ConsoleGetShadowCellLocked(State.X + CellX, State.Y + CellY);
    if (Cell == NULL) {
        return;
    }

    *Cell = ConsoleComposeShadowCell(Char, ForeColor, BackColor, Blink);
}

/***************************************************************************/

/**
 * @brief Clear one region inside the canonical console shadow buffer.
 * @param RegionIndex Region index.
 * @param ForeColor Foreground color index.
 * @param BackColor Background color index.
 * @param Blink Blink flag.
 */
void ConsoleShadowClearRegion(U32 RegionIndex, U32 ForeColor, U32 BackColor, U32 Blink) {
    CONSOLE_REGION_STATE State;
    U16 BlankCell;
    U32 Row;
    U32 Column;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (ConsoleEnsureShadowBufferLocked() == FALSE) return;

    BlankCell = ConsoleComposeShadowBlankCell(ForeColor, BackColor, Blink);
    for (Row = 0; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            U16* Cell = ConsoleGetShadowCellLocked(State.X + Column, State.Y + Row);
            if (Cell != NULL) {
                *Cell = BlankCell;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Scroll one region inside the canonical console shadow buffer.
 * @param RegionIndex Region index.
 * @param ForeColor Foreground color index for the cleared last row.
 * @param BackColor Background color index for the cleared last row.
 * @param Blink Blink flag for the cleared last row.
 */
void ConsoleShadowScrollRegion(U32 RegionIndex, U32 ForeColor, U32 BackColor, U32 Blink) {
    CONSOLE_REGION_STATE State;
    U16 BlankCell;
    U32 Row;
    U32 Column;
    U16* Destination;
    U16* Source;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Height == 0) return;
    if (ConsoleEnsureShadowBufferLocked() == FALSE) return;

    for (Row = 1; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            Destination = ConsoleGetShadowCellLocked(State.X + Column, State.Y + (Row - 1));
            Source = ConsoleGetShadowCellLocked(State.X + Column, State.Y + Row);
            if (Destination != NULL && Source != NULL) {
                *Destination = *Source;
            }
        }
    }

    BlankCell = ConsoleComposeShadowBlankCell(ForeColor, BackColor, Blink);
    for (Column = 0; Column < State.Width; Column++) {
        Destination = ConsoleGetShadowCellLocked(State.X + Column, State.Y + (State.Height - 1));
        if (Destination != NULL) {
            *Destination = BlankCell;
        }
    }
}

/***************************************************************************/

/**
 * @brief Repaint one region from the canonical shadow buffer to the backend.
 * @param RegionIndex Region index.
 */
void ConsoleRepaintRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    U32 SavedForeColor;
    U32 SavedBackColor;
    U32 SavedBlink;
    U32 Row;
    U32 Column;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (ConsoleEnsureShadowBuffer() == FALSE) return;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        UnlockMutex(MUTEX_CONSOLE_STATE);
        return;
    }

    SavedForeColor = Console.ForeColor;
    SavedBackColor = Console.BackColor;
    SavedBlink = Console.Blink;
    ConsoleHideFramebufferCursor();
    for (Row = 0; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            U16* Cell = ConsoleGetShadowCellLocked(State.X + Column, State.Y + Row);
            U32 PixelX;
            U32 PixelY;

            if (Cell == NULL) {
                continue;
            }

            Console.ForeColor = (U32)(((*Cell) >> 8) & 0x0F);
            Console.BackColor = (U32)(((*Cell) >> 12) & 0x07);
            Console.Blink = (U32)(((*Cell) >> 15) & 0x01);
            PixelX = (State.X + Column) * ConsoleGetCellWidth();
            PixelY = (State.Y + Row) * ConsoleGetCellHeight();
            ConsoleDrawGlyph(PixelX, PixelY, (STR)(U8)((*Cell) & 0xFF));
        }
    }
    Console.ForeColor = SavedForeColor;
    Console.BackColor = SavedBackColor;
    Console.Blink = SavedBlink;
    ConsoleShowFramebufferCursor();
    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Capture the active console region as a reusable snapshot.
 * @param OutSnapshot Receives an opaque snapshot pointer.
 * @return TRUE on success.
 */
BOOL ConsoleCaptureActiveRegionSnapshot(LPVOID* OutSnapshot) {
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT Snapshot;
    CONSOLE_REGION_STATE State;
    U32 RegionIndex;
    U32 Row;
    UINT CopyBytesPerRow;

    if (OutSnapshot == NULL) {
        return FALSE;
    }

    *OutSnapshot = NULL;

    Snapshot = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)KernelHeapAlloc(sizeof(CONSOLE_ACTIVE_REGION_SNAPSHOT));
    if (Snapshot == NULL) {
        return FALSE;
    }

    MemorySet(Snapshot, 0, sizeof(CONSOLE_ACTIVE_REGION_SNAPSHOT));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    RegionIndex = (Console.ActiveRegion < Console.RegionCount) ? Console.ActiveRegion : 0;
    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE || ConsoleEnsureShadowBufferLocked() == FALSE) {
        UnlockMutex(MUTEX_CONSOLE_STATE);
        KernelHeapFree(Snapshot);
        return FALSE;
    }

    Snapshot->RegionIndex = RegionIndex;
    Snapshot->RegionX = State.X;
    Snapshot->RegionY = State.Y;
    Snapshot->RegionWidth = State.Width;
    Snapshot->RegionHeight = State.Height;
    Snapshot->CursorX = *State.CursorX;
    Snapshot->CursorY = *State.CursorY;
    Snapshot->ForeColor = *State.ForeColor;
    Snapshot->BackColor = *State.BackColor;
    Snapshot->Blink = *State.Blink;
    Snapshot->TextCellCount = (UINT)(State.Width * State.Height);

    Snapshot->TextBuffer = (U16*)KernelHeapAlloc(Snapshot->TextCellCount * sizeof(U16));
    if (Snapshot->TextBuffer == NULL) {
        UnlockMutex(MUTEX_CONSOLE_STATE);
        KernelHeapFree(Snapshot);
        return FALSE;
    }

    CopyBytesPerRow = (UINT)(State.Width * sizeof(U16));
    for (Row = 0; Row < State.Height; Row++) {
        MemoryCopy(
            Snapshot->TextBuffer + (Row * State.Width),
            Console.ShadowBuffer + ((State.Y + Row) * Console.ScreenWidth) + State.X,
            CopyBytesPerRow);
    }

    Snapshot->IsValid = TRUE;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    *OutSnapshot = Snapshot;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Restore a previously captured active console region snapshot.
 * @param Snapshot Opaque snapshot pointer.
 * @return TRUE on success.
 */
BOOL ConsoleRestoreActiveRegionSnapshot(LPVOID Snapshot) {
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT StateSnapshot = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)Snapshot;
    CONSOLE_REGION_STATE State;
    U32 Row;
    UINT CopyBytesPerRow;

    if (StateSnapshot == NULL || StateSnapshot->IsValid == FALSE || StateSnapshot->TextBuffer == NULL) {
        return FALSE;
    }

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    if (ConsoleResolveRegionState(StateSnapshot->RegionIndex, &State) == FALSE ||
        ConsoleEnsureShadowBufferLocked() == FALSE ||
        State.Width != StateSnapshot->RegionWidth ||
        State.Height != StateSnapshot->RegionHeight) {
        UnlockMutex(MUTEX_CONSOLE_STATE);
        return FALSE;
    }

    CopyBytesPerRow = (UINT)(State.Width * sizeof(U16));
    for (Row = 0; Row < State.Height; Row++) {
        MemoryCopy(
            Console.ShadowBuffer + ((State.Y + Row) * Console.ScreenWidth) + State.X,
            StateSnapshot->TextBuffer + (Row * State.Width),
            CopyBytesPerRow);
    }

    *State.ForeColor = StateSnapshot->ForeColor;
    *State.BackColor = StateSnapshot->BackColor;
    *State.Blink = StateSnapshot->Blink;
    *State.CursorX = StateSnapshot->CursorX;
    *State.CursorY = StateSnapshot->CursorY;

    UnlockMutex(MUTEX_CONSOLE_STATE);

    ConsoleRepaintRegion(StateSnapshot->RegionIndex);
    SetConsoleCursorPosition(StateSnapshot->CursorX, StateSnapshot->CursorY);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release a snapshot created by ConsoleCaptureActiveRegionSnapshot.
 * @param Snapshot Opaque snapshot pointer.
 */
void ConsoleReleaseActiveRegionSnapshot(LPVOID Snapshot) {
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT StateSnapshot = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)Snapshot;

    if (StateSnapshot == NULL) {
        return;
    }

    SAFE_USE(StateSnapshot->TextBuffer) { KernelHeapFree(StateSnapshot->TextBuffer); }
    KernelHeapFree(StateSnapshot);
}

/***************************************************************************/

/**
 * @brief Initialize a console region layout and runtime fields.
 *
 * @param Index Region index.
 * @param X Left coordinate in screen space.
 * @param Y Top coordinate in screen space.
 * @param Width Region width.
 * @param Height Region height.
 */
static void ConsoleInitializeRegion(U32 Index, U32 X, U32 Y, U32 Width, U32 Height) {
    LPCONSOLE_REGION Region;

    if (Index >= MAX_CONSOLE_REGIONS) return;

    Region = &Console.Regions[Index];
    Region->X = X;
    Region->Y = Y;
    Region->Width = Width;
    Region->Height = Height;
    Region->CursorX = 0;
    Region->CursorY = 0;
    Region->ForeColor = Console.ForeColor;
    Region->BackColor = Console.BackColor;
    Region->Blink = Console.Blink;
    Region->PagingEnabled = FALSE;
    Region->PagingActive = FALSE;
    Region->PagingRemaining = 0;
}

/***************************************************************************/

/**
 * @brief Build a grid of console regions.
 *
 * Regions are laid out in row-major order. The first columns and rows
 * receive any extra width or height if the screen does not divide evenly.
 *
 * @param Columns Number of columns.
 * @param Rows Number of rows.
 */
static void ConsoleConfigureRegions(U32 Columns, U32 Rows) {
    U32 EffectiveColumns;
    U32 EffectiveRows;
    U32 Index;
    U32 Row;
    U32 Column;
    U32 BaseWidth;
    U32 BaseHeight;
    U32 ExtraWidth;
    U32 ExtraHeight;
    U32 CursorX;
    U32 CursorY;

    EffectiveColumns = (Columns == 0) ? 1 : Columns;
    EffectiveRows = (Rows == 0) ? 1 : Rows;

    while ((EffectiveColumns * EffectiveRows) > MAX_CONSOLE_REGIONS) {
        if (EffectiveColumns >= EffectiveRows && EffectiveColumns > 1) {
            EffectiveColumns--;
        } else if (EffectiveRows > 1) {
            EffectiveRows--;
        } else {
            break;
        }
    }

    Console.RegionCount = EffectiveColumns * EffectiveRows;

    BaseWidth = Console.ScreenWidth / EffectiveColumns;
    ExtraWidth = Console.ScreenWidth % EffectiveColumns;
    BaseHeight = Console.ScreenHeight / EffectiveRows;
    ExtraHeight = Console.ScreenHeight % EffectiveRows;

    Index = 0;
    CursorY = 0;
    for (Row = 0; Row < EffectiveRows; Row++) {
        U32 RegionHeight = BaseHeight + ((Row < ExtraHeight) ? 1 : 0);
        CursorX = 0;
        for (Column = 0; Column < EffectiveColumns; Column++) {
            U32 RegionWidth = BaseWidth + ((Column < ExtraWidth) ? 1 : 0);
            ConsoleInitializeRegion(Index, CursorX, CursorY, RegionWidth, RegionHeight);
            CursorX += RegionWidth;
            Index++;
        }
        CursorY += RegionHeight;
    }
}

/***************************************************************************/

/**
 * @brief Apply the console region layout based on build configuration.
 */
void ConsoleApplyLayout(void) {
#if DEBUG_SPLIT == 1
    ConsoleConfigureRegions(2, 1);
    Console.DebugRegion = (Console.RegionCount > 1) ? 1 : 0;
#else
    ConsoleConfigureRegions(1, 1);
    Console.DebugRegion = 0;
#endif

    Console.ActiveRegion = 0;
    Console.Width = Console.Regions[0].Width;
    Console.Height = Console.Regions[0].Height;
}

/***************************************************************************/

/**
 * @brief Clamp the standard console cursor to its region bounds.
 */
void ConsoleClampCursorToRegionZero(void) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) {
        Console.CursorX = 0;
        Console.CursorY = 0;
        return;
    }

    if (Console.CursorY >= State.Height) {
        Console.CursorY = State.Height - 1;
    }

    if (Console.CursorX >= State.Width) {
        Console.CursorX = 0;
        if ((Console.CursorY + 1) < State.Height) {
            Console.CursorY++;
        }
    }
}

/***************************************************************************/

/**
 * @brief Returns TRUE when the debug split is enabled.
 * @return TRUE if the debug region is active, FALSE otherwise.
 */
BOOL ConsoleIsDebugSplitEnabled(void) {
#if DEBUG_SPLIT == 1
    return (Console.RegionCount > 1 && Console.DebugRegion < Console.RegionCount) ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

/***************************************************************************/

/**
 * @brief Determine whether a keycode requests cooperative interruption.
 *
 * @param KeyCode Keycode to evaluate.
 * @return TRUE when keycode corresponds to Control+C, FALSE otherwise.
 */
static BOOL ConsoleIsInterruptKey(LPKEYCODE KeyCode) {
    U32 Modifiers;

    if (KeyCode == NULL) {
        return FALSE;
    }

    if ((U8)KeyCode->ASCIICode == 0x03) {
        return TRUE;
    }

    if (KeyCode->VirtualKey != VK_C) {
        return FALSE;
    }

    Modifiers = GetKeyModifiers();
    return (Modifiers & KEYMOD_CONTROL) != 0;
}

/***************************************************************************/

/**
 * @brief Show the console paging prompt for a specific region.
 * @param RegionIndex Region index.
 */
static void ConsolePagerWaitLockedRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    LPPROCESS CurrentProcess;
    LPTASK CurrentTask;
    KEY_CODE KeyCode;
    U32 WaitLoops;
    U32 ReleasedConsoleLocks;
    BOOL ExitByInterrupt;
    U32 Row;
    U32 Column;
    LPCSTR Prompt = TEXT("-- Press a key --");
    U32 PromptLen;
    U32 Start;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.PagingEnabled) == FALSE || (*State.PagingActive) == FALSE) return;
    if (State.Width == 0 || State.Height < 2) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    Row = State.Height - 1;

    for (Column = 0; Column < State.Width; Column++) {
        U32 PixelX = (State.X + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
    }

    PromptLen = StringLength(Prompt);
    if (PromptLen > State.Width) PromptLen = State.Width;
    Start = (State.Width > PromptLen) ? (State.Width - PromptLen) / 2 : 0;
    for (Column = 0; Column < PromptLen; Column++) {
        U32 PixelX = (State.X + Start + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, Prompt[Column]);
    }

    SetConsoleCursorPosition(0, Row);

    CurrentProcess = GetCurrentProcess();
    CurrentTask = GetCurrentTask();
    WaitLoops = 0;
    ReleasedConsoleLocks = 0;
    ExitByInterrupt = FALSE;

    // Release all recursive console mutex holds while waiting for input.
    while (CurrentTask != NULL && ConsoleStateMutex.Task == CurrentTask && ConsoleStateMutex.Lock > 0) {
        if (UnlockMutex(MUTEX_CONSOLE_STATE) == FALSE) {
            break;
        }

        ReleasedConsoleLocks++;
    }

    while (TRUE) {
        if (CurrentProcess != NULL && ProcessControlIsInterruptRequested(CurrentProcess)) {
            (*State.PagingRemaining) = (State.Height > 0) ? (State.Height - 1) : 0;
            ExitByInterrupt = TRUE;
            break;
        }

        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }

            if (ConsoleIsInterruptKey(&KeyCode)) {
                if (CurrentProcess != NULL) {
                    ProcessControlRequestInterrupt(CurrentProcess);
                }
                (*State.PagingRemaining) = (State.Height > 0) ? (State.Height - 1) : 0;
                ExitByInterrupt = TRUE;
                break;
            }

            (*State.PagingRemaining) = State.Height - 1;
            break;
        }

        Sleep(10);
        WaitLoops++;
    }

    if (ReleasedConsoleLocks == 0) {
        LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    } else {
        for (U32 LockIndex = 0; LockIndex < ReleasedConsoleLocks; LockIndex++) {
            LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
        }
    }

    for (Column = 0; Column < State.Width; Column++) {
        U32 PixelX = (State.X + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
    }

    UNUSED(ExitByInterrupt);
    UNUSED(WaitLoops);
}

/***************************************************************************/

/**
 * @brief Write a character at the current cursor position inside a region.
 *
 * @param RegionIndex Region index.
 * @param Char Character to write.
 */
static void ConsoleSetCharacterRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.CursorX) >= State.Width || (*State.CursorY) >= State.Height) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    {
        U32 PixelX = (State.X + (*State.CursorX)) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + (*State.CursorY)) * ConsoleGetCellHeight();
        ConsoleShadowWriteRegionCell(RegionIndex, *State.CursorX, *State.CursorY, Char, *State.ForeColor, *State.BackColor, *State.Blink);
        ConsoleDrawGlyph(PixelX, PixelY, Char);
    }
}

/***************************************************************************/

/**
 * @brief Scroll a region up by one line.
 * @param RegionIndex Region index.
 */
void ConsoleScrollRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if ((*State.PagingRemaining) == 0) {
        ConsolePagerWaitLockedRegion(RegionIndex);
    } else {
        (*State.PagingRemaining)--;
    }

    ConsoleShadowScrollRegion(RegionIndex, *State.ForeColor, *State.BackColor, *State.Blink);
    ConsoleScrollRegionFramebuffer(RegionIndex);
}

/***************************************************************************/

/**
 * @brief Clear a region and reset its cursor.
 * @param RegionIndex Region index.
 */
void ConsoleClearRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    ConsoleShadowClearRegion(RegionIndex, *State.ForeColor, *State.BackColor, *State.Blink);
    ConsoleClearRegionFramebuffer(RegionIndex);
    (*State.CursorX) = 0;
    (*State.CursorY) = 0;
}

/***************************************************************************/

/**
 * @brief Print a character into a region and update its cursor.
 * @param RegionIndex Region index.
 * @param Char Character to print.
 */
void ConsolePrintCharRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    if (Char == STR_NEWLINE) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    if (Char == STR_RETURN) {
        return;
    }

    if (Char == STR_TAB) {
        (*State.CursorX) += 4;
        if ((*State.CursorX) >= State.Width) {
            (*State.CursorX) = 0;
            (*State.CursorY)++;
            if ((*State.CursorY) >= State.Height) {
                ConsoleScrollRegion(RegionIndex);
                (*State.CursorY) = State.Height - 1;
            }
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    ConsoleSetCharacterRegion(RegionIndex, Char);
    (*State.CursorX)++;
    if ((*State.CursorX) >= State.Width) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
    }

    if (RegionIndex == 0) {
        SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
    }
}
