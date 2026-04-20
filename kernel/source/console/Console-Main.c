
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


    Console

\************************************************************************/

#include "Console-Internal.h"
#include "console/Console-VGATextFallback.h"
#include "DisplaySession.h"
#include "text/font/Font.h"
#include "GFX.h"
#include "core/Kernel.h"
#include "memory/Memory.h"
#include "drivers/graphics/vga/VGA.h"
#include "process/Process.h"
#include "drivers/input/Keyboard.h"
#include "log/Log.h"
#include "sync/Mutex.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "core/DriverGetters.h"
#include "input/VKey.h"
#include "VarArg.h"
#include "log/Profile.h"
#include "system/SerialPort.h"

/***************************************************************************/

#define CONSOLE_VER_MAJOR 1
#define CONSOLE_VER_MINOR 0

static UINT ConsoleDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION ConsoleDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = CONSOLE_VER_MAJOR,
    .VersionMinor = CONSOLE_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "Console",
    .Alias = "console",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = ConsoleDriverCommands};

/***************************************************************************/

/**
 * @brief Retrieves the console driver descriptor.
 * @return Pointer to the console driver.
 */
LPDRIVER ConsoleGetDriver(void) {
    return &ConsoleDriver;
}

/***************************************************************************/

#define CHARATTR (Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07))

#define CGA_REGISTER 0x00
#define CGA_DATA 0x01

/***************************************************************************/

CONSOLE_STRUCT Console = {
    .ScreenWidth = 80,
    .ScreenHeight = 25,
    .Width = 80,
    .Height = 25,
    .CursorX = 0,
    .CursorY = 0,
    .BackColor = 0,
    .ForeColor = 0,
    .Blink = 0,
    .PagingEnabled = TRUE,
    .PagingActive = FALSE,
    .PagingRemaining = 0,
    .RegionCount = 1,
    .ActiveRegion = 0,
    .DebugRegion = 0,
    .Port = 0,
    .Memory = NULL,
    .ShadowBuffer = NULL,
    .ShadowBufferCellCount = 0,
    .FramebufferPhysical = 0,
    .FramebufferLinear = NULL,
    .FramebufferPitch = 0,
    .FramebufferWidth = 0,
    .FramebufferHeight = 0,
    .FramebufferBitsPerPixel = 0,
    .FramebufferType = MULTIBOOT_FRAMEBUFFER_TEXT,
    .FramebufferRedPosition = 0,
    .FramebufferRedMaskSize = 0,
    .FramebufferGreenPosition = 0,
    .FramebufferGreenMaskSize = 0,
    .FramebufferBluePosition = 0,
    .FramebufferBlueMaskSize = 0,
    .FramebufferBytesPerPixel = 0,
    .FontWidth = 8,
    .FontHeight = 16,
    .UseFramebuffer = FALSE,
    .UseTextBackend = TRUE,
    .BootCursorHandoverPending = FALSE,
    .BootCursorHandoverConsumed = FALSE,
    .BootCursorX = 0,
    .BootCursorY = 0};

/***************************************************************************/

/**
 * @brief Move the hardware and logical console cursor under held state mutex.
 * @param CursorX X coordinate of the cursor.
 * @param CursorY Y coordinate of the cursor.
 */
static void SetConsoleCursorPositionLocked(U32 CursorX, U32 CursorY) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        return;
    }

    UNUSED(State);

    ConsoleHideFramebufferCursor();
    Console.CursorX = CursorX;
    Console.CursorY = CursorY;
    ConsoleShowFramebufferCursor();
}

/***************************************************************************/

/**
 * @brief Apply one pending bootloader cursor handover to the standard console.
 *
 * The bootloader passes a logical text cursor through one EXOS config table.
 * Once the first final console backend is activated, this helper imports that
 * position once and clamps it to the active region zero.
 */
static void ConsoleApplyBootCursorHandoverLocked(void) {
    if (Console.BootCursorHandoverPending == FALSE || Console.BootCursorHandoverConsumed != FALSE) {
        return;
    }

    Console.BootCursorHandoverConsumed = TRUE;
    Console.CursorX = Console.BootCursorX;
    Console.CursorY = Console.BootCursorY;
    ConsoleClampCursorToRegionZero();
}

/***************************************************************************/

/**
 * @brief Place one character under held state mutex.
 * @param Char Character to display.
 */
static void SetConsoleCharacterLocked(STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        return;
    }

    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return;
    }

    {
        U32 PixelX = (State.X + Console.CursorX) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Console.CursorY) * ConsoleGetCellHeight();
        ConsoleShadowWriteRegionCell(0, Console.CursorX, Console.CursorY, Char, Console.ForeColor, Console.BackColor, Console.Blink);
        ConsoleHideFramebufferCursor();
        ConsoleDrawGlyph(PixelX, PixelY, Char);
        ConsoleShowFramebufferCursor();
    }
}

/***************************************************************************/

/**
 * @brief Scroll main console region under held state mutex.
 */
static void ScrollConsoleLocked(void) {
    while (Keyboard.ScrollLock) {
    }

    ConsoleHideFramebufferCursor();
    ConsoleScrollRegion(0);
}

/***************************************************************************/

/**
 * @brief Print one character under held state mutex.
 * @param Char Character to print.
 */
static void ConsolePrintCharLocked(STR Char) {
    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return;
    }

    if (Char == STR_NEWLINE) {
        Console.CursorX = 0;
        Console.CursorY++;
        if (Console.CursorY >= Console.Height) {
            ScrollConsoleLocked();
            Console.CursorY = Console.Height - 1;
        }
    } else if (Char == STR_RETURN) {
    } else if (Char == STR_TAB) {
        Console.CursorX += 4;
        if (Console.CursorX >= Console.Width) {
            Console.CursorX = 0;
            Console.CursorY++;
            if (Console.CursorY >= Console.Height) {
                ScrollConsoleLocked();
                Console.CursorY = Console.Height - 1;
            }
        }
    } else {
        SetConsoleCharacterLocked(Char);
        Console.CursorX++;
        if (Console.CursorX >= Console.Width) {
            Console.CursorX = 0;
            Console.CursorY++;
            if (Console.CursorY >= Console.Height) {
                ScrollConsoleLocked();
                Console.CursorY = Console.Height - 1;
            }
        }
    }

    SetConsoleCursorPositionLocked(Console.CursorX, Console.CursorY);
}

/***************************************************************************/

/**
 * @brief Print a null-terminated string under held state mutex.
 * @param Text String to print.
 */
static void ConsolePrintStringLocked(LPCSTR Text) {
    U32 Index = 0;

    SAFE_USE(Text) {
        for (Index = 0; Index < MAX_STRING_BUFFER; Index++) {
            if (Text[Index] == STR_NULL) break;
            ConsolePrintCharLocked(Text[Index]);
        }
    }
}

/***************************************************************************/

/**
 * @brief Move the hardware and logical console cursor.
 * @param CursorX X coordinate of the cursor.
 * @param CursorY Y coordinate of the cursor.
 */
void SetConsoleCursorPosition(U32 CursorX, U32 CursorY) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("SetConsoleCursorPosition"));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    SetConsoleCursorPositionLocked(CursorX, CursorY);
    UnlockMutex(MUTEX_CONSOLE_STATE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Get the current console cursor position from hardware.
 * @param CursorX Pointer to receive X coordinate of the cursor.
 * @param CursorY Pointer to receive Y coordinate of the cursor.
 */
void GetConsoleCursorPosition(U32* CursorX, U32* CursorY) {
    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    SAFE_USE_2(CursorX, CursorY) {
        *CursorX = Console.CursorX;
        *CursorY = Console.CursorY;
    }

    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

U32 GetConsoleWidth(void) {
    U32 Width;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    Width = Console.Width;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return Width;
}

/***************************************************************************/

U32 GetConsoleHeight(void) {
    U32 Height;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    Height = Console.Height;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return Height;
}

/***************************************************************************/

U32 GetConsoleCharHeight(void) {
    U32 CharHeight;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    CharHeight = Console.FontHeight;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return CharHeight;
}

/***************************************************************************/

U32 GetConsoleForeColor(void) {
    U32 Color;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    Color = Console.ForeColor;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return Color;
}

/***************************************************************************/

U32 GetConsoleBackColor(void) {
    U32 Color;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    Color = Console.BackColor;
    UnlockMutex(MUTEX_CONSOLE_STATE);

    return Color;
}

/***************************************************************************/

/**
 * @brief Place a character at the current cursor position.
 * @param Char Character to display.
 */
void SetConsoleCharacter(STR Char) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("SetConsoleCharacter"));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    SetConsoleCharacterLocked(Char);
    UnlockMutex(MUTEX_CONSOLE_STATE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Scroll the console up by one line.
 */
void ScrollConsole(void) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ScrollConsole"));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    ScrollConsoleLocked();
    UnlockMutex(MUTEX_CONSOLE_STATE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Clear the entire console screen.
 */
void ClearConsole(void) {
    U32 Index;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    ConsoleHideFramebufferCursor();

    if (ConsoleIsDebugSplitEnabled() != FALSE) {
        ConsoleClearRegion(0);
    } else {
        for (Index = 0; Index < Console.RegionCount; Index++) {
            ConsoleClearRegion(Index);
        }
    }

    SetConsoleCursorPositionLocked(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Print a single character to the console handling control codes.
 * @param Char Character to print.
 */
void ConsolePrintChar(STR Char) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ConsolePrintChar"));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    ConsolePrintCharLocked(Char);
    UnlockMutex(MUTEX_CONSOLE_STATE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Print a single character to the debug console region.
 * @param Char Character to print.
 */
void ConsolePrintDebugChar(STR Char) {
    if (ConsoleIsDebugSplitEnabled() == FALSE) return;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    ConsolePrintCharRegion(Console.DebugRegion, Char);
    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Handle backspace at the current cursor position.
 */
void ConsoleBackSpace(void) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    if (Console.CursorX == 0 && Console.CursorY == 0) goto Out;

    if (Console.CursorX == 0) {
        Console.CursorX = State.Width - 1;
        Console.CursorY--;
    } else {
        Console.CursorX--;
    }

    if (ConsoleEnsureFramebufferMapped() == TRUE) {
        U32 PixelX = (State.X + Console.CursorX) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Console.CursorY) * ConsoleGetCellHeight();
        ConsoleShadowWriteRegionCell(0, Console.CursorX, Console.CursorY, STR_SPACE, Console.ForeColor, Console.BackColor, Console.Blink);
        ConsoleHideFramebufferCursor();
        ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
    }

Out:

    SetConsoleCursorPositionLocked(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Print a null-terminated string to the console.
 * @param Text String to print.
 */
static void ConsolePrintString(LPCSTR Text) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ConsolePrintString"));

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    ConsolePrintStringLocked(Text);
    UnlockMutex(MUTEX_CONSOLE_STATE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Print a formatted string to the console.
 * @param Format Format string.
 * @return TRUE on success.
 */
void ConsolePrint(LPCSTR Format, ...) {
    STR Text[MAX_STRING_BUFFER];
    VarArgList Args;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    ConsolePrintStringLocked(Text);

    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

void ConsolePrintLine(U32 Row, U32 Column, LPCSTR Text, U32 Length) {
    CONSOLE_REGION_STATE State;
    U32 Index;

    if (Text == NULL) return;

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    if (ConsoleResolveRegionState(0, &State) == FALSE) goto Out;

    if (Row >= State.Height || Column >= State.Width) goto Out;
    if (ConsoleEnsureFramebufferMapped() == FALSE) goto Out;

    for (Index = 0; Index < Length && (Column + Index) < State.Width; Index++) {
        U32 PixelX = (State.X + Column + Index) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleShadowWriteRegionCell(0, Column + Index, Row, Text[Index], Console.ForeColor, Console.BackColor, Console.Blink);
        ConsoleDrawGlyph(PixelX, PixelY, Text[Index]);
    }

Out:
    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Blit a text buffer into the console, with optional per-cell attributes.
 * @param Info Source buffer descriptor.
 * @return 0 when the request has been consumed.
 */
U32 ConsoleBlitBuffer(LPCONSOLE_BLIT_BUFFER Info) {
    CONSOLE_REGION_STATE State;
    U32 SavedFore;
    U32 SavedBack;
    U32 MaxWidth;
    U32 MaxHeight;
    U32 BaseX;
    U32 BaseY;
    U32 Width;
    U32 Height;
    U32 X;
    U32 Y;
    U32 TextPitch;
    U32 AttrPitch;
    U32 Row;
    BOOL UseAttr;
    U32 DefaultFore;
    U32 DefaultBack;
    BOOL UseBackendPath;

    if (Info == NULL || !IsValidMemory((LINEAR)Info) || !IsValidMemory((LINEAR)Info->Text)) {
        return 0;
    }

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        goto Out;
    }

    MaxWidth = State.Width;
    MaxHeight = State.Height;
    BaseX = State.X;
    BaseY = State.Y;
    Width = Info->Width;
    Height = Info->Height;
    X = Info->X;
    Y = Info->Y;
    TextPitch = (Info->TextPitch != 0) ? Info->TextPitch : (Info->Width + 1);
    AttrPitch = (Info->AttrPitch != 0) ? Info->AttrPitch : Info->Width;
    UseAttr = (Info->Attr != NULL) && IsValidMemory((LINEAR)Info->Attr);
    SavedFore = Console.ForeColor;
    SavedBack = Console.BackColor;
    DefaultFore = (Info->ForeColor <= 15) ? Info->ForeColor : SavedFore;
    DefaultBack = (Info->BackColor <= 15) ? Info->BackColor : SavedBack;
    UseBackendPath = (Console.Memory == NULL || ConsoleUsesTextBackend() != FALSE) ? TRUE : FALSE;

    if (Width > MaxWidth) Width = MaxWidth;
    if (Height > MaxHeight) Height = MaxHeight;
    if (X >= MaxWidth || Y >= MaxHeight) goto RestoreColors;
    if (X + Width > MaxWidth) Width = MaxWidth - X;
    if (Y + Height > MaxHeight) Height = MaxHeight - Y;

    if (UseAttr == FALSE) {
        SetConsoleForeColor(DefaultFore);
        SetConsoleBackColor(DefaultBack);
    }

    if (UseBackendPath != FALSE) {
        U32 CellWidth = 0;
        U32 CellHeight = 0;

        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            goto RestoreColors;
        }

        CellWidth = ConsoleGetCellWidth();
        CellHeight = ConsoleGetCellHeight();
        ConsoleHideFramebufferCursor();

        for (Row = 0; Row < Height; Row++) {
            const U8* AttrRow = UseAttr ? (Info->Attr + (Row * AttrPitch)) : NULL;
            U32 Column;

            for (Column = 0; Column < Width; Column++) {
                U32 CellFore = DefaultFore;
                U32 CellBack = DefaultBack;
                U32 PixelX;
                U32 PixelY;
                STR Character;

                if (AttrRow != NULL) {
                    U8 Attr = AttrRow[Column];

                    CellFore = Attr & 0x0F;
                    CellBack = (Attr >> 0x04) & 0x0F;
                }

                PixelX = (BaseX + X + Column) * CellWidth;
                PixelY = (BaseY + Y + Row) * CellHeight;
                Character = Info->Text[(Row * TextPitch) + Column];

                Console.ForeColor = CellFore;
                Console.BackColor = CellBack;
                ConsoleShadowWriteRegionCell(0, X + Column, Y + Row, Character, CellFore, CellBack, Console.Blink);
                ConsoleDrawGlyph(PixelX, PixelY, Character);
            }
        }

        ConsoleShowFramebufferCursor();
        goto RestoreColors;
    }

    for (Row = 0; Row < Height; Row++) {
        const U8* AttrRow = UseAttr ? (Info->Attr + (Row * AttrPitch)) : NULL;
        U32 Column;

        if (AttrRow == NULL) {
            ConsolePrintLine(Y + Row, X, Info->Text + (Row * TextPitch), Width);
            continue;
        }

        for (Column = 0; Column < Width; Column++) {
            U8 Attr = AttrRow[Column];
            U32 CellFore = Attr & 0x0F;
            U32 CellBack = (Attr >> 0x04) & 0x0F;
            U16 Attribute = (U16)(CellFore | (CellBack << 0x04) | (Console.Blink << 0x07));
            UINT Offset;

            Attribute = (U16)(Attribute << 0x08);
            Offset = ((BaseY + Y + Row) * Console.ScreenWidth) + (BaseX + X + Column);
            Console.Memory[Offset] = (U16)Info->Text[(Row * TextPitch) + Column] | Attribute;
        }
    }

RestoreColors:
    Console.ForeColor = SavedFore;
    Console.BackColor = SavedBack;

Out:
    UnlockMutex(MUTEX_CONSOLE_STATE);
    return 0;
}

/***************************************************************************/

int SetConsoleBackColor(U32 Color) {
    Console.BackColor = Color;
    return 1;
}

/***************************************************************************/

int SetConsoleForeColor(U32 Color) {
    Console.ForeColor = Color;
    return 1;
}

/***************************************************************************/

BOOL ConsoleGetString(LPSTR Buffer, U32 Size) {
    KEYCODE KeyCode;
    U32 Index = 0;
    U32 Done = 0;

    DEBUG(TEXT("[ConsoleGetString] Enter"));

    Buffer[0] = STR_NULL;

    while (Done == 0) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                while (Index) {
                    Index--;
                    ConsoleBackSpace();
                }
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                if (Index) {
                    Index--;
                    ConsoleBackSpace();
                }
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                ConsolePrintChar(STR_NEWLINE);
                Done = 1;
            } else {
                if (KeyCode.ASCIICode >= STR_SPACE) {
                    if (Index < Size - 1) {
                        ConsolePrintChar(KeyCode.ASCIICode);
                        Buffer[Index++] = KeyCode.ASCIICode;
                    }
                }
            }
        }

        Sleep(10);
    }

    Buffer[Index] = STR_NULL;

    DEBUG(TEXT("[ConsoleGetString] Exit"));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Print a formatted string to the console.
 * @param Format Format string.
 * @return TRUE on success.
 */
void ConsolePanic(LPCSTR Format, ...) {
    STR Text[0x1000];
    const STR Prefix[] = "[ConsolePanic] ";
    const STR HaltText[] = "[ConsolePanic] >>> Halting system <<<\r\n";
    VarArgList Args;
    UINT Index = 0;

    DisableInterrupts();

    if (DisplaySwitchToConsole() != FALSE) {
        ClearConsole();
        SetConsoleCursorPosition(0, 0);
    }

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    SerialReset(0);
    for (Index = 0; Prefix[Index] != STR_NULL; ++Index) {
        SerialOut(0, Prefix[Index]);
    }
    for (Index = 0; Text[Index] != STR_NULL; ++Index) {
        SerialOut(0, Text[Index]);
    }
    SerialOut(0, '\r');
    SerialOut(0, '\n');
    for (Index = 0; HaltText[Index] != STR_NULL; ++Index) {
        SerialOut(0, HaltText[Index]);
    }

    ConsolePrintString(Text);
    ConsolePrintString(TEXT("\n>>> Halting system <<<"));

    DO_THE_SLEEPING_BEAUTY;
}

/***************************************************************************/

void InitializeConsole(void) {
    const FONT_FACE* Font = FontGetDefaultFace();
    FONT_METRICS Metrics;

    if (FontFaceGetMetrics(Font, &Metrics)) {
        Console.FontWidth = Metrics.CellWidth;
        Console.FontHeight = Metrics.CellHeight;
    }

    if (Console.UseFramebuffer != FALSE) {
        U32 CellWidth = ConsoleGetCellWidth();
        U32 CellHeight = ConsoleGetCellHeight();

        Console.FramebufferBytesPerPixel = Console.FramebufferBitsPerPixel / 8u;
        if (Console.FramebufferBytesPerPixel == 0u) {
            Console.FramebufferBytesPerPixel = 4u;
        }

        Console.ScreenWidth = Console.FramebufferWidth / CellWidth;
        Console.ScreenHeight = Console.FramebufferHeight / CellHeight;
        if (Console.ScreenWidth == 0u || Console.ScreenHeight == 0u) {
            Console.UseFramebuffer = FALSE;
            Console.ScreenWidth = 80;
            Console.ScreenHeight = 25;
        }
    } else if (Console.FramebufferType == MULTIBOOT_FRAMEBUFFER_TEXT &&
               Console.ScreenWidth != 0u && Console.ScreenHeight != 0u) {
    } else {
        Console.ScreenWidth = 80;
        Console.ScreenHeight = 25;
    }

    Console.BackColor = 0;
    Console.ForeColor = 7;
    Console.PagingEnabled = TRUE;
    Console.PagingActive = FALSE;
    Console.PagingRemaining = 0;

    ConsoleApplyLayout();
    ConsoleApplyBootCursorHandoverLocked();
    ConsoleClampCursorToRegionZero();
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);
}

/***************************************************************************/

/**
 * @brief Configure framebuffer metadata for console output.
 *
 * This stores framebuffer parameters for later use during console initialization.
 *
 * @param FramebufferPhysical Physical base address of the framebuffer.
 * @param Width Framebuffer width in pixels or text columns.
 * @param Height Framebuffer height in pixels or text rows.
 * @param Pitch Bytes per scan line.
 * @param BitsPerPixel Bits per pixel.
 * @param Type Multiboot framebuffer type.
 * @param RedPosition Red channel bit position.
 * @param RedMaskSize Red channel bit size.
 * @param GreenPosition Green channel bit position.
 * @param GreenMaskSize Green channel bit size.
 * @param BluePosition Blue channel bit position.
 * @param BlueMaskSize Blue channel bit size.
 */
void ConsoleSetFramebufferInfo(
    PHYSICAL FramebufferPhysical,
    U32 Width,
    U32 Height,
    U32 Pitch,
    U32 BitsPerPixel,
    U32 Type,
    U32 RedPosition,
    U32 RedMaskSize,
    U32 GreenPosition,
    U32 GreenMaskSize,
    U32 BluePosition,
    U32 BlueMaskSize) {
    ConsoleResetFramebufferCursorState();

    Console.FramebufferPhysical = FramebufferPhysical;
    Console.FramebufferLinear = NULL;
    Console.FramebufferBytesPerPixel = 0;
    Console.FramebufferWidth = Width;
    Console.FramebufferHeight = Height;
    Console.FramebufferPitch = Pitch;
    Console.FramebufferBitsPerPixel = BitsPerPixel;
    Console.FramebufferType = Type;
    Console.FramebufferRedPosition = RedPosition;
    Console.FramebufferRedMaskSize = RedMaskSize;
    Console.FramebufferGreenPosition = GreenPosition;
    Console.FramebufferGreenMaskSize = GreenMaskSize;
    Console.FramebufferBluePosition = BluePosition;
    Console.FramebufferBlueMaskSize = BlueMaskSize;

    if (Type == MULTIBOOT_FRAMEBUFFER_RGB && FramebufferPhysical != 0 && Width != 0u && Height != 0u) {
        Console.UseFramebuffer = TRUE;
        Console.UseTextBackend = FALSE;
        Console.Memory = NULL;
        Console.Port = 0;
    } else if (Type == MULTIBOOT_FRAMEBUFFER_TEXT && FramebufferPhysical != 0) {
        Console.UseFramebuffer = FALSE;
        Console.UseTextBackend = TRUE;
        Console.Memory = NULL;
        Console.Port = 0;
        Console.ScreenWidth = (Width != 0u) ? Width : 80u;
        Console.ScreenHeight = (Height != 0u) ? Height : 25u;
        Console.FramebufferBytesPerPixel = sizeof(U16);
    } else {
        Console.UseFramebuffer = FALSE;
        Console.UseTextBackend = FALSE;
    }
}

/***************************************************************************/

/**
 * @brief Route console text rendering through the active graphics backend.
 *
 * This updates console geometry from one graphics mode descriptor and enables
 * framebuffer text dispatch through DF_GFX_TEXT_* commands.
 *
 * @param ModeInfo Active graphics mode descriptor.
 * @return TRUE on success, FALSE on invalid mode geometry.
 */
BOOL ConsoleSetGraphicsTextMode(LPGRAPHICS_MODE_INFO ModeInfo) {
    U32 CellWidth = 0;
    U32 CellHeight = 0;
    U32 Columns = 0;
    U32 Rows = 0;
    U32 BytesPerPixel = 0;

    if (ModeInfo == NULL || ModeInfo->Width == 0 || ModeInfo->Height == 0 || ModeInfo->BitsPerPixel == 0) {
        return FALSE;
    }

    CellWidth = ConsoleGetCellWidth();
    CellHeight = ConsoleGetCellHeight();
    if (CellWidth == 0 || CellHeight == 0) {
        return FALSE;
    }

    Columns = ModeInfo->Width / CellWidth;
    Rows = ModeInfo->Height / CellHeight;
    if (Columns == 0 || Rows == 0) {
        return FALSE;
    }

    BytesPerPixel = ModeInfo->BitsPerPixel / 8;
    if (BytesPerPixel == 0) {
        BytesPerPixel = 4;
    }

    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);

    ConsoleResetFramebufferCursorState();
    Console.UseFramebuffer = TRUE;
    Console.UseTextBackend = FALSE;
    Console.Memory = NULL;
    Console.Port = 0;
    Console.FramebufferPhysical = 0;
    Console.FramebufferLinear = NULL;
    Console.FramebufferWidth = ModeInfo->Width;
    Console.FramebufferHeight = ModeInfo->Height;
    Console.FramebufferBitsPerPixel = ModeInfo->BitsPerPixel;
    Console.FramebufferBytesPerPixel = BytesPerPixel;
    Console.FramebufferPitch = ModeInfo->Width * BytesPerPixel;
    Console.FramebufferType = MULTIBOOT_FRAMEBUFFER_RGB;
    Console.FramebufferRedPosition = 16;
    Console.FramebufferRedMaskSize = 8;
    Console.FramebufferGreenPosition = 8;
    Console.FramebufferGreenMaskSize = 8;
    Console.FramebufferBluePosition = 0;
    Console.FramebufferBlueMaskSize = 8;
    Console.ScreenWidth = Columns;
    Console.ScreenHeight = Rows;
    ConsoleApplyLayout();

    UnlockMutex(MUTEX_CONSOLE_STATE);

#if DEBUG_OUTPUT != 1
    ClearConsole();
#endif

    ConsoleApplyBootCursorHandover();

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Seed one pending bootloader cursor handover before console takeover.
 */
void ConsoleSetBootCursorHandover(U32 CursorX, U32 CursorY) {
    Console.BootCursorHandoverPending = TRUE;
    Console.BootCursorHandoverConsumed = FALSE;
    Console.BootCursorX = CursorX;
    Console.BootCursorY = CursorY;
}

/***************************************************************************/

/**
 * @brief Import the pending bootloader cursor into the active console state once.
 */
void ConsoleApplyBootCursorHandover(void) {
    LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    ConsoleApplyBootCursorHandoverLocked();
    UnlockMutex(MUTEX_CONSOLE_STATE);
}

/***************************************************************************/

/**
 * @brief Repaint the canonical console content on the active backend.
 */
void ConsoleRefreshDisplay(void) {
    ConsoleRepaintRegion(0);
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);
}

/***************************************************************************/

/**
 * @brief Enable or disable console paging.
 * @param Enabled TRUE to enable paging, FALSE to disable.
 */
void ConsoleSetPagingEnabled(BOOL Enabled) {
    Console.PagingEnabled = Enabled ? TRUE : FALSE;
    if (Console.PagingEnabled == FALSE) {
        Console.PagingRemaining = 0;
    }
}

/***************************************************************************/

/**
 * @brief Query whether console paging is enabled.
 * @return TRUE if paging is enabled, FALSE otherwise.
 */
BOOL ConsoleGetPagingEnabled(void) {
    return Console.PagingEnabled ? TRUE : FALSE;
}

/***************************************************************************/

/**
 * @brief Activate or deactivate console paging.
 * @param Active TRUE to allow paging prompts, FALSE to disable them.
 */
void ConsoleSetPagingActive(BOOL Active) {
    Console.PagingActive = Active ? TRUE : FALSE;
    if (Console.PagingActive == FALSE) {
        Console.PagingRemaining = 0;
    } else {
        ConsoleResetPaging();
    }
}

/***************************************************************************/

/**
 * @brief Query whether console paging is active.
 * @return TRUE if paging prompts are active, FALSE otherwise.
 */
BOOL ConsoleGetPagingActive(void) {
    return Console.PagingActive ? TRUE : FALSE;
}

/***************************************************************************/

/**
 * @brief Reset console paging state for the next command.
 */
void ConsoleResetPaging(void) {
    U32 Remaining;

    if (Console.PagingEnabled == FALSE || Console.PagingActive == FALSE) {
        Console.PagingRemaining = 0;
        return;
    }

    Remaining = 0;
    if (Console.Height > 1) {
        if (Console.CursorY > 0) {
            Remaining = Console.CursorY - 1;
        } else {
            Remaining = 0;
        }
    }

    Console.PagingRemaining = Remaining;
}

/***************************************************************************/

/**
 * @brief Set console text mode using a graphics mode descriptor.
 * @param Info Mode description with Width/Height in characters.
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT ConsoleSetMode(LPGRAPHICS_MODE_INFO Info) { return ConsoleDriverCommands(DF_GFX_SETMODE, (UINT)Info); }

/***************************************************************************/

/**
 * @brief Return the number of available VGA console modes.
 * @return Number of console modes.
 */
UINT ConsoleGetModeCount(void) {
    LPDRIVER VGADriver = VGAGetDriver();

    if (VGADriver == NULL || VGADriver->Command == NULL) {
        return 0;
    }

    return VGADriver->Command(DF_GFX_GETMODECOUNT, 0);
}

/***************************************************************************/

/**
 * @brief Query a console mode by index.
 * @param Info Mode request (Index) and output (Columns/Rows/CharHeight).
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT ConsoleGetModeInfo(LPCONSOLE_MODE_INFO Info) {
    VGAMODEINFO VgaInfo;

    if (Info == NULL) return DF_RETURN_GENERIC;

    if (VGAGetModeInfo(Info->Index, &VgaInfo) == FALSE) {
        return DF_RETURN_GENERIC;
    }

    Info->Columns = VgaInfo.Columns;
    Info->Rows = VgaInfo.Rows;
    Info->CharHeight = VgaInfo.CharHeight;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Driver command handler for the console subsystem.
 *
 * DF_LOAD initializes the console once; DF_UNLOAD clears the ready flag
 * as there is no shutdown routine.
 */
static UINT ConsoleDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((ConsoleDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeConsole();
            ConsoleDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((ConsoleDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ConsoleDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(CONSOLE_VER_MAJOR, CONSOLE_VER_MINOR);

        case DF_GFX_GETMODEINFO: {
            LPGRAPHICS_MODE_INFO Info = (LPGRAPHICS_MODE_INFO)Parameter;
            SAFE_USE(Info) {
                Info->Width = Console.Width;
                Info->Height = Console.Height;
                Info->BitsPerPixel = 0;
                return DF_RETURN_SUCCESS;
            }
            return DF_RETURN_GENERIC;
        }

        case DF_GFX_SETMODE: {
            LPGRAPHICS_MODE_INFO Info = (LPGRAPHICS_MODE_INFO)Parameter;
            SAFE_USE(Info) {
                return ConsoleVGATextFallbackActivate(Info->Width, Info->Height, NULL) != FALSE ? DF_RETURN_SUCCESS
                                                                                                : DF_GFX_ERROR_MODEUNAVAIL;
            }
            return DF_RETURN_GENERIC;
        }

        case DF_GFX_GETCONTEXT:
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
        case DF_GFX_ELLIPSE:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
