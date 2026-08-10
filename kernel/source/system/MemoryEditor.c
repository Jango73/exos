
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


    Memory Editor

\************************************************************************/

#include "Base.h"
#include "console/Console.h"
#include "core/Kernel.h"
#include "drivers/input/Keyboard.h"
#include "text/CoreString.h"
#include "input/VKey.h"

/***************************************************************************/

typedef struct tag_MEMEDITCONTEXT {
    U32 Base;
} MEM_EDIT_CONTEXT, *LPMEMEDITCONTEXT;

/***************************************************************************/

static void PrintMemoryLine(U32 Base) {
    STR Num[16];
    STR Addr[16];
    STR Hexa[64];
    STR ASCII[64];
    U8* Pointer = (U8*)(UINT)Base;
    U8 Data = 0;
    U32 Index = 0;

    Addr[0] = STR_NULL;
    Hexa[0] = STR_NULL;
    ASCII[0] = STR_NULL;

    U32ToHexString(Base, Addr);

    if (IsValidMemory(Base)) {
        for (Index = 0; Index < 16; Index++) {
            Data = Pointer[Index];
            U32ToHexString(Data, Num);
            StringConcat(Hexa, Num + 6);
            StringConcat(Hexa, Text_Space);
            if (Index == 7) StringConcat(Hexa, Text_Space);

            if (Data < STR_SPACE) Data = '.';
            if (Data == '%') Data = '.';
            Num[0] = Data;
            Num[1] = STR_NULL;
            StringConcat(ASCII, Num);
        }
    } else {
        StringConcat(ASCII, TEXT("????????"));
    }

    ConsolePrint(Addr);
    ConsolePrint(Text_Space);
    ConsolePrint(Hexa);
    ConsolePrint(Text_Space);
    ConsolePrint(ASCII);
    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

void PrintMemory(U32 Base, U32 Size) {
    U32 End = Base + Size;
    U32 CurrentY = 0;

    for (CurrentY = 0; CurrentY < 24; CurrentY++) {
        PrintMemoryLine(Base);
        Base += 16;
        if (Base >= End) break;
    }
}

/***************************************************************************/

static void PrintMemoryPage(U32 Base, U32 Size) {
    Console.CursorX = 0;
    Console.CursorY = 0;

    PrintMemory(Base, Size);
}

/***************************************************************************/

void MemoryEditor(U32 Base) {
    KEY_CODE KeyCode;

    ClearConsole();
    PrintMemoryPage(Base, 24 * 16);

    FOREVER {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            switch (KeyCode.VirtualKey) {
                case VK_ESCAPE:
                    return;
                case VK_DOWN: {
                    Base += 16;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_UP: {
                    if (Base >= 16)
                        Base -= 16;
                    else
                        Base = 0;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_PAGEDOWN: {
                    Base += 16 * 24;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_PAGEUP: {
                    if (Base >= (16 * 24)) Base -= 16 * 24;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
            }
        }

        Sleep(5);
    }
}
