
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

#ifndef CONSOLE_H_INCLUDED
#define CONSOLE_H_INCLUDED

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#include "User.h" /* For console color defines and CONSOLE_BLIT_BUFFER */

/***************************************************************************/

#define MAX_CONSOLE_REGIONS 16

/***************************************************************************/

typedef struct tag_CONSOLE_REGION {
    U32 X;
    U32 Y;
    U32 Width;
    U32 Height;
    U32 CursorX;
    U32 CursorY;
    U32 ForeColor;
    U32 BackColor;
    U32 Blink;
    U32 PagingEnabled;
    U32 PagingActive;
    U32 PagingRemaining;
} CONSOLE_REGION, *LPCONSOLE_REGION;

/***************************************************************************/

typedef struct tag_CONSOLE_STRUCT {
    U32 ScreenWidth;
    U32 ScreenHeight;
    U32 Width;
    U32 Height;
    U32 CursorX;
    U32 CursorY;
    U32 BackColor;
    U32 ForeColor;
    U32 Blink;
    U32 PagingEnabled;
    U32 PagingActive;
    U32 PagingRemaining;
    U32 RegionCount;
    U32 ActiveRegion;
    U32 DebugRegion;
    U32 Port;
    U16* Memory;
    U16* ShadowBuffer;
    UINT ShadowBufferCellCount;
    PHYSICAL FramebufferPhysical;
    U8* FramebufferLinear;
    U32 FramebufferPitch;
    U32 FramebufferWidth;
    U32 FramebufferHeight;
    U32 FramebufferBitsPerPixel;
    U32 FramebufferType;
    U32 FramebufferRedPosition;
    U32 FramebufferRedMaskSize;
    U32 FramebufferGreenPosition;
    U32 FramebufferGreenMaskSize;
    U32 FramebufferBluePosition;
    U32 FramebufferBlueMaskSize;
    U32 FramebufferBytesPerPixel;
    U32 FontWidth;
    U32 FontHeight;
    BOOL UseFramebuffer;
    BOOL UseTextBackend;
    BOOL BootCursorHandoverPending;
    BOOL BootCursorHandoverConsumed;
    U32 BootCursorX;
    U32 BootCursorY;
    CONSOLE_REGION Regions[MAX_CONSOLE_REGIONS];
} CONSOLE_STRUCT, *LPCONSOLE_STRUCT;

/***************************************************************************/

void SetConsoleCursorPosition(U32 CursorX, U32 CursorY);
void GetConsoleCursorPosition(U32* CursorX, U32* CursorY);
U32 GetConsoleWidth(void);
U32 GetConsoleHeight(void);
U32 GetConsoleCharHeight(void);
U32 GetConsoleForeColor(void);
U32 GetConsoleBackColor(void);
void SetConsoleCharacter(STR);
void ScrollConsole(void);
void ClearConsole(void);
void ConsolePrintChar(STR);
void ConsoleBackSpace(void);
void ConsolePrint(LPCSTR Format, ...);
void ConsolePrintDebugChar(STR Char);
BOOL ConsoleIsDebugSplitEnabled(void);
BOOL ConsoleIsFramebufferMappingInProgress(void);
BOOL ConsoleCaptureActiveRegionSnapshot(LPVOID* OutSnapshot);
BOOL ConsoleRestoreActiveRegionSnapshot(LPVOID Snapshot);
void ConsoleReleaseActiveRegionSnapshot(LPVOID Snapshot);
void ConsolePrintLine(U32 Row, U32 Column, LPCSTR Text, U32 Length);
U32 ConsoleBlitBuffer(LPCONSOLE_BLIT_BUFFER Info);
int SetConsoleBackColor(U32 Color);
int SetConsoleForeColor(U32 Color);
BOOL ConsoleGetString(LPSTR, U32);
void ConsolePanic(LPCSTR Format, ...);
void InitializeConsole(void);
void ConsoleInvalidateFramebufferMapping(void);
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
    U32 BlueMaskSize);
BOOL ConsoleSetGraphicsTextMode(LPGRAPHICS_MODE_INFO ModeInfo);
UINT ConsoleSetMode(LPGRAPHICS_MODE_INFO Info);
UINT ConsoleGetModeCount(void);
UINT ConsoleGetModeInfo(LPCONSOLE_MODE_INFO Info);
void ConsoleSetPagingEnabled(BOOL Enabled);
BOOL ConsoleGetPagingEnabled(void);
void ConsoleSetPagingActive(BOOL Active);
BOOL ConsoleGetPagingActive(void);
void ConsoleResetPaging(void);
void ConsoleSetBootCursorHandover(U32 CursorX, U32 CursorY);
void ConsoleRefreshDisplay(void);

// Functions in shell/Shell-Main.c

U32 Shell(LPVOID);

/***************************************************************************/

extern CONSOLE_STRUCT Console;

/***************************************************************************/

#pragma pack(pop)

#endif
