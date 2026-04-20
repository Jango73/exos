
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Key codes

\************************************************************************/

#include "Base.h"
#include "drivers/input/Keyboard.h"

#include "system/Clock.h"
#include "text/CoreString.h"
#include "log/Log.h"
#include "input/VKey.h"
#include "utils/KernelPath.h"

/************************************************************************/

typedef struct tag_KEYNAME {
    U8 VirtualKey;
    LPCSTR String;
} KEYNAME, *LPKEYNAME;

/************************************************************************/

static KEYNAME KeyNames[] = {{VK_NONE, TEXT("NONE")},   {VK_F1, TEXT("F1")},        {VK_F2, TEXT("F2")},
                             {VK_F3, TEXT("F3")},       {VK_F4, TEXT("F4")},        {VK_F5, TEXT("F5")},
                             {VK_F6, TEXT("F6")},       {VK_F7, TEXT("F7")},        {VK_F8, TEXT("F8")},
                             {VK_F9, TEXT("F9")},       {VK_F10, TEXT("F10")},      {VK_F11, TEXT("F11")},
                             {VK_F12, TEXT("F12")},     {VK_0, TEXT("0")},          {VK_1, TEXT("1")},
                             {VK_2, TEXT("2")},         {VK_3, TEXT("3")},          {VK_4, TEXT("4")},
                             {VK_5, TEXT("5")},         {VK_6, TEXT("6")},          {VK_7, TEXT("7")},
                             {VK_8, TEXT("8")},         {VK_9, TEXT("9")},          {VK_A, TEXT("A")},
                             {VK_B, TEXT("B")},         {VK_C, TEXT("C")},          {VK_D, TEXT("D")},
                             {VK_E, TEXT("E")},         {VK_F, TEXT("F")},          {VK_G, TEXT("G")},
                             {VK_H, TEXT("H")},         {VK_I, TEXT("I")},          {VK_J, TEXT("J")},
                             {VK_K, TEXT("K")},         {VK_L, TEXT("L")},          {VK_M, TEXT("M")},
                             {VK_N, TEXT("N")},         {VK_O, TEXT("O")},          {VK_P, TEXT("P")},
                             {VK_Q, TEXT("Q")},         {VK_R, TEXT("R")},          {VK_S, TEXT("S")},
                             {VK_T, TEXT("T")},         {VK_U, TEXT("U")},          {VK_V, TEXT("V")},
                             {VK_W, TEXT("W")},         {VK_X, TEXT("X")},          {VK_Y, TEXT("Y")},
                             {VK_Z, TEXT("Z")},         {VK_DOT, TEXT(".")},        {VK_COLON, TEXT(":")},
                             {VK_COMMA, TEXT(",")},     {VK_UNDERSCORE, TEXT("_")}, {VK_STAR, TEXT("*")},
                             {VK_PERCENT, TEXT("%")},   {VK_EQUAL, TEXT("=")},      {VK_PLUS, TEXT("+")},
                             {VK_MINUS, TEXT("-")},     {VK_SLASH, TEXT("/")},      {VK_BACKSLASH, TEXT("\\")},
                             {VK_QUESTION, TEXT("?")},  {VK_EXCL, TEXT("!")},       {VK_DOLLAR, TEXT("$")},
                             {VK_AT, TEXT("@")},        {VK_SPACE, TEXT("SPACE")},  {VK_ENTER, TEXT("ENTER")},
                             {VK_ESCAPE, TEXT("ESC")},  {VK_SHIFT, TEXT("SHFT")},   {VK_LSHIFT, TEXT("LSHF")},
                             {VK_RSHIFT, TEXT("RSHF")}, {VK_CONTROL, TEXT("CTRL")}, {VK_LCTRL, TEXT("LCTL")},
                             {VK_RCTRL, TEXT("RCTL")},  {VK_ALT, TEXT("ALT")},      {VK_LALT, TEXT("LALT")},
                             {VK_RALT, TEXT("RALT")},   {VK_TAB, TEXT("TAB")},      {VK_BACKSPACE, TEXT("BKSP")},
                             {VK_INSERT, TEXT("INS")},  {VK_DELETE, TEXT("DEL")},   {VK_HOME, TEXT("HOME")},
                             {VK_END, TEXT("END")},     {VK_PAGEUP, TEXT("PGUP")},  {VK_PAGEDOWN, TEXT("PGDN")},
                             {VK_UP, TEXT("UP")},       {VK_DOWN, TEXT("DOWN")},    {VK_LEFT, TEXT("LEFT")},
                             {VK_RIGHT, TEXT("RIGHT")}, {VK_NUM, TEXT("NUM")},      {VK_CAPS, TEXT("CAPS")},
                             {VK_SCROLL, TEXT("SCRL")}, {VK_PAUSE, TEXT("PAUS")},
                             {VK_MEDIA_PLAY, TEXT("MPLAY")},
                             {VK_MEDIA_PAUSE, TEXT("MPAUS")},
                             {VK_MEDIA_PLAY_PAUSE, TEXT("MPLPA")},
                             {VK_MEDIA_STOP, TEXT("MSTOP")},
                             {VK_MEDIA_NEXT, TEXT("MNEXT")},
                             {VK_MEDIA_PREV, TEXT("MPREV")},
                             {VK_MEDIA_MUTE, TEXT("MMUTE")},
                             {VK_MEDIA_VOLUME_UP, TEXT("MVUP")},
                             {VK_MEDIA_VOLUME_DOWN, TEXT("MVDN")},
                             {VK_MEDIA_BRIGHTNESS_UP, TEXT("MBRUP")},
                             {VK_MEDIA_BRIGHTNESS_DOWN, TEXT("MBRDN")},
                             {VK_MEDIA_SLEEP, TEXT("MSLP")},
                             {VK_MEDIA_EJECT, TEXT("MEJCT")},
                             {VK_CUT, TEXT("CUT")},
                             {VK_COPY, TEXT("COPY")},
                             {VK_PASTE, TEXT("PASTE")}};

/***************************************************************************/

LPCSTR GetKeyName(U8 VirtualKey) {
    U32 Index;

    for (Index = 0; Index < sizeof(KeyNames) / sizeof(KEYNAME); Index++) {
        if (KeyNames[Index].VirtualKey == VirtualKey) {
            return KeyNames[Index].String;
        }
    }

    return TEXT("");
}

/***************************************************************************/

static BOOL IsUsageModifier(KEY_USAGE Usage) {
    return Usage >= KEY_USAGE_LEFT_CTRL && Usage <= KEY_USAGE_RIGHT_GUI;
}

/***************************************************************************/

static BOOL IsUsageRepeatable(KEY_USAGE Usage) {
    if (Usage < KEY_USAGE_MIN || Usage > KEY_USAGE_MAX) {
        return FALSE;
    }

    if (IsUsageModifier(Usage)) {
        return FALSE;
    }

    if (Usage == KEY_USAGE_CAPS_LOCK || Usage == KEY_USAGE_NUM_LOCK || Usage == KEY_USAGE_SCROLL_LOCK) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

static BOOL IsUsageKeypadDigit(KEY_USAGE Usage) {
    return Usage >= KEY_USAGE_KEYPAD_1 && Usage <= KEY_USAGE_KEYPAD_DOT;
}

/***************************************************************************/

static void ClearKeyCode(LPKEYCODE KeyCode) {
    KeyCode->VirtualKey = 0;
    KeyCode->ASCIICode = 0;
    KeyCode->Unicode = 0;
}

/***************************************************************************/

static BOOL IsKeyCodeEmpty(const KEYCODE *KeyCode) {
    return KeyCode->VirtualKey == 0 && KeyCode->ASCIICode == 0 && KeyCode->Unicode == 0;
}

/***************************************************************************/

static U32 GetKeyCodePoint(const KEYCODE *KeyCode) {
    if (KeyCode->Unicode != 0) return (U32)KeyCode->Unicode;
    if (KeyCode->ASCIICode != 0) return (U32)KeyCode->ASCIICode;
    return 0;
}

/***************************************************************************/

static void SetKeyCodeFromCodePoint(U32 CodePoint, LPKEYCODE KeyCode) {
    ClearKeyCode(KeyCode);
    if (CodePoint == 0) return;

    if (CodePoint <= 0x7F) {
        KeyCode->ASCIICode = (STR)CodePoint;
    } else {
        KeyCode->Unicode = (USTR)CodePoint;
    }
}

/***************************************************************************/

static BOOL FindDeadKeyResult(const KEY_LAYOUT_HID *Layout, U32 DeadKey, U32 BaseKey, U32 *Result) {
    UINT Index;

    if (Layout == NULL || Layout->DeadKeys == NULL || Result == NULL) return FALSE;

    for (Index = 0; Index < Layout->DeadKeyCount; Index++) {
        if (Layout->DeadKeys[Index].DeadKey == DeadKey && Layout->DeadKeys[Index].BaseKey == BaseKey) {
            *Result = Layout->DeadKeys[Index].Result;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

static BOOL IsDeadKey(const KEY_LAYOUT_HID *Layout, U32 CodePoint) {
    UINT Index;

    if (Layout == NULL || Layout->DeadKeys == NULL) return FALSE;

    for (Index = 0; Index < Layout->DeadKeyCount; Index++) {
        if (Layout->DeadKeys[Index].DeadKey == CodePoint) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

static BOOL FindComposeResult(const KEY_LAYOUT_HID *Layout, U32 FirstKey, U32 SecondKey, U32 *Result) {
    UINT Index;

    if (Layout == NULL || Layout->ComposeEntries == NULL || Result == NULL) return FALSE;

    for (Index = 0; Index < Layout->ComposeCount; Index++) {
        if (Layout->ComposeEntries[Index].FirstKey == FirstKey &&
            Layout->ComposeEntries[Index].SecondKey == SecondKey) {
            *Result = Layout->ComposeEntries[Index].Result;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

static BOOL IsComposeStart(const KEY_LAYOUT_HID *Layout, U32 CodePoint) {
    UINT Index;

    if (Layout == NULL || Layout->ComposeEntries == NULL) return FALSE;

    for (Index = 0; Index < Layout->ComposeCount; Index++) {
        if (Layout->ComposeEntries[Index].FirstKey == CodePoint) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

static UINT GetLayoutLevel(void) {
    BOOL Shift = Keyboard.UsageStatus[KEY_USAGE_LEFT_SHIFT] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_SHIFT];
    BOOL Control = Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL];
    BOOL Alt = Keyboard.UsageStatus[KEY_USAGE_LEFT_ALT] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_ALT];

    if (Control) return KEY_LAYOUT_HID_LEVEL_CONTROL;
    if (Alt) return KEY_LAYOUT_HID_LEVEL_ALTGR;
    if (Shift || Keyboard.CapsLock) return KEY_LAYOUT_HID_LEVEL_SHIFT;
    return KEY_LAYOUT_HID_LEVEL_BASE;
}

/***************************************************************************/

static BOOL GetLayoutKeyCode(const KEY_LAYOUT_HID *Layout, KEY_USAGE Usage, UINT Level, LPKEYCODE KeyCode) {
    if (Layout == NULL || Layout->Entries == NULL) return FALSE;
    if (Usage >= Layout->EntryCount) return FALSE;
    if (Level >= Layout->LevelCount) return FALSE;

    *KeyCode = Layout->Entries[Usage].Levels[Level];
    return !IsKeyCodeEmpty(KeyCode);
}

/***************************************************************************/

static BOOL GetFallbackKeyCodeBase(KEY_USAGE Usage, LPKEYCODE KeyCode) {
    switch (Usage) {
        case 0x04: *KeyCode = (KEYCODE){VK_A, 'a', 0}; return TRUE;
        case 0x05: *KeyCode = (KEYCODE){VK_B, 'b', 0}; return TRUE;
        case 0x06: *KeyCode = (KEYCODE){VK_C, 'c', 0}; return TRUE;
        case 0x07: *KeyCode = (KEYCODE){VK_D, 'd', 0}; return TRUE;
        case 0x08: *KeyCode = (KEYCODE){VK_E, 'e', 0}; return TRUE;
        case 0x09: *KeyCode = (KEYCODE){VK_F, 'f', 0}; return TRUE;
        case 0x0A: *KeyCode = (KEYCODE){VK_G, 'g', 0}; return TRUE;
        case 0x0B: *KeyCode = (KEYCODE){VK_H, 'h', 0}; return TRUE;
        case 0x0C: *KeyCode = (KEYCODE){VK_I, 'i', 0}; return TRUE;
        case 0x0D: *KeyCode = (KEYCODE){VK_J, 'j', 0}; return TRUE;
        case 0x0E: *KeyCode = (KEYCODE){VK_K, 'k', 0}; return TRUE;
        case 0x0F: *KeyCode = (KEYCODE){VK_L, 'l', 0}; return TRUE;
        case 0x10: *KeyCode = (KEYCODE){VK_M, 'm', 0}; return TRUE;
        case 0x11: *KeyCode = (KEYCODE){VK_N, 'n', 0}; return TRUE;
        case 0x12: *KeyCode = (KEYCODE){VK_O, 'o', 0}; return TRUE;
        case 0x13: *KeyCode = (KEYCODE){VK_P, 'p', 0}; return TRUE;
        case 0x14: *KeyCode = (KEYCODE){VK_Q, 'q', 0}; return TRUE;
        case 0x15: *KeyCode = (KEYCODE){VK_R, 'r', 0}; return TRUE;
        case 0x16: *KeyCode = (KEYCODE){VK_S, 's', 0}; return TRUE;
        case 0x17: *KeyCode = (KEYCODE){VK_T, 't', 0}; return TRUE;
        case 0x18: *KeyCode = (KEYCODE){VK_U, 'u', 0}; return TRUE;
        case 0x19: *KeyCode = (KEYCODE){VK_V, 'v', 0}; return TRUE;
        case 0x1A: *KeyCode = (KEYCODE){VK_W, 'w', 0}; return TRUE;
        case 0x1B: *KeyCode = (KEYCODE){VK_X, 'x', 0}; return TRUE;
        case 0x1C: *KeyCode = (KEYCODE){VK_Y, 'y', 0}; return TRUE;
        case 0x1D: *KeyCode = (KEYCODE){VK_Z, 'z', 0}; return TRUE;
        case 0x1E: *KeyCode = (KEYCODE){VK_1, '1', 0}; return TRUE;
        case 0x1F: *KeyCode = (KEYCODE){VK_2, '2', 0}; return TRUE;
        case 0x20: *KeyCode = (KEYCODE){VK_3, '3', 0}; return TRUE;
        case 0x21: *KeyCode = (KEYCODE){VK_4, '4', 0}; return TRUE;
        case 0x22: *KeyCode = (KEYCODE){VK_5, '5', 0}; return TRUE;
        case 0x23: *KeyCode = (KEYCODE){VK_6, '6', 0}; return TRUE;
        case 0x24: *KeyCode = (KEYCODE){VK_7, '7', 0}; return TRUE;
        case 0x25: *KeyCode = (KEYCODE){VK_8, '8', 0}; return TRUE;
        case 0x26: *KeyCode = (KEYCODE){VK_9, '9', 0}; return TRUE;
        case 0x27: *KeyCode = (KEYCODE){VK_0, '0', 0}; return TRUE;
        case 0x28: *KeyCode = (KEYCODE){VK_ENTER, STR_NEWLINE, 0}; return TRUE;
        case 0x29: *KeyCode = (KEYCODE){VK_ESCAPE, 0, 0}; return TRUE;
        case 0x2A: *KeyCode = (KEYCODE){VK_BACKSPACE, 0, 0}; return TRUE;
        case 0x2B: *KeyCode = (KEYCODE){VK_TAB, STR_TAB, 0}; return TRUE;
        case 0x2C: *KeyCode = (KEYCODE){VK_SPACE, STR_SPACE, 0}; return TRUE;
        case 0x2D: *KeyCode = (KEYCODE){VK_MINUS, '-', 0}; return TRUE;
        case 0x2E: *KeyCode = (KEYCODE){VK_EQUAL, '=', 0}; return TRUE;
        case 0x2F: *KeyCode = (KEYCODE){VK_NONE, '[', 0}; return TRUE;
        case 0x30: *KeyCode = (KEYCODE){VK_NONE, ']', 0}; return TRUE;
        case 0x31: *KeyCode = (KEYCODE){VK_BACKSLASH, '\\', 0}; return TRUE;
        case 0x33: *KeyCode = (KEYCODE){VK_COLON, ';', 0}; return TRUE;
        case 0x34: *KeyCode = (KEYCODE){VK_NONE, '\'', 0}; return TRUE;
        case 0x35: *KeyCode = (KEYCODE){VK_NONE, '`', 0}; return TRUE;
        case 0x36: *KeyCode = (KEYCODE){VK_COMMA, ',', 0}; return TRUE;
        case 0x37: *KeyCode = (KEYCODE){VK_DOT, '.', 0}; return TRUE;
        case 0x38: *KeyCode = (KEYCODE){VK_SLASH, '/', 0}; return TRUE;
        case 0x54: *KeyCode = (KEYCODE){VK_SLASH, '/', 0}; return TRUE;
        case 0x55: *KeyCode = (KEYCODE){VK_STAR, '*', 0}; return TRUE;
        case 0x56: *KeyCode = (KEYCODE){VK_MINUS, '-', 0}; return TRUE;
        case 0x57: *KeyCode = (KEYCODE){VK_PLUS, '+', 0}; return TRUE;
        case 0x78: *KeyCode = (KEYCODE){VK_MEDIA_STOP, 0, 0}; return TRUE;
        case 0x7B: *KeyCode = (KEYCODE){VK_COPY, 0, 0}; return TRUE;
        case 0x7C: *KeyCode = (KEYCODE){VK_PASTE, 0, 0}; return TRUE;
        case 0x7D: *KeyCode = (KEYCODE){VK_CUT, 0, 0}; return TRUE;
        case 0x7F: *KeyCode = (KEYCODE){VK_MEDIA_MUTE, 0, 0}; return TRUE;
        case 0x80: *KeyCode = (KEYCODE){VK_MEDIA_VOLUME_UP, 0, 0}; return TRUE;
        case 0x81: *KeyCode = (KEYCODE){VK_MEDIA_VOLUME_DOWN, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_ENTER: *KeyCode = (KEYCODE){VK_ENTER, STR_NEWLINE, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_1: *KeyCode = (KEYCODE){VK_1, '1', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_2: *KeyCode = (KEYCODE){VK_2, '2', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_3: *KeyCode = (KEYCODE){VK_3, '3', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_4: *KeyCode = (KEYCODE){VK_4, '4', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_5: *KeyCode = (KEYCODE){VK_5, '5', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_6: *KeyCode = (KEYCODE){VK_6, '6', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_7: *KeyCode = (KEYCODE){VK_7, '7', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_8: *KeyCode = (KEYCODE){VK_8, '8', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_9: *KeyCode = (KEYCODE){VK_9, '9', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_0: *KeyCode = (KEYCODE){VK_0, '0', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_DOT: *KeyCode = (KEYCODE){VK_DOT, '.', 0}; return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

static BOOL GetFallbackKeyCodeShift(KEY_USAGE Usage, LPKEYCODE KeyCode) {
    switch (Usage) {
        case 0x04: *KeyCode = (KEYCODE){VK_A, 'A', 0}; return TRUE;
        case 0x05: *KeyCode = (KEYCODE){VK_B, 'B', 0}; return TRUE;
        case 0x06: *KeyCode = (KEYCODE){VK_C, 'C', 0}; return TRUE;
        case 0x07: *KeyCode = (KEYCODE){VK_D, 'D', 0}; return TRUE;
        case 0x08: *KeyCode = (KEYCODE){VK_E, 'E', 0}; return TRUE;
        case 0x09: *KeyCode = (KEYCODE){VK_F, 'F', 0}; return TRUE;
        case 0x0A: *KeyCode = (KEYCODE){VK_G, 'G', 0}; return TRUE;
        case 0x0B: *KeyCode = (KEYCODE){VK_H, 'H', 0}; return TRUE;
        case 0x0C: *KeyCode = (KEYCODE){VK_I, 'I', 0}; return TRUE;
        case 0x0D: *KeyCode = (KEYCODE){VK_J, 'J', 0}; return TRUE;
        case 0x0E: *KeyCode = (KEYCODE){VK_K, 'K', 0}; return TRUE;
        case 0x0F: *KeyCode = (KEYCODE){VK_L, 'L', 0}; return TRUE;
        case 0x10: *KeyCode = (KEYCODE){VK_M, 'M', 0}; return TRUE;
        case 0x11: *KeyCode = (KEYCODE){VK_N, 'N', 0}; return TRUE;
        case 0x12: *KeyCode = (KEYCODE){VK_O, 'O', 0}; return TRUE;
        case 0x13: *KeyCode = (KEYCODE){VK_P, 'P', 0}; return TRUE;
        case 0x14: *KeyCode = (KEYCODE){VK_Q, 'Q', 0}; return TRUE;
        case 0x15: *KeyCode = (KEYCODE){VK_R, 'R', 0}; return TRUE;
        case 0x16: *KeyCode = (KEYCODE){VK_S, 'S', 0}; return TRUE;
        case 0x17: *KeyCode = (KEYCODE){VK_T, 'T', 0}; return TRUE;
        case 0x18: *KeyCode = (KEYCODE){VK_U, 'U', 0}; return TRUE;
        case 0x19: *KeyCode = (KEYCODE){VK_V, 'V', 0}; return TRUE;
        case 0x1A: *KeyCode = (KEYCODE){VK_W, 'W', 0}; return TRUE;
        case 0x1B: *KeyCode = (KEYCODE){VK_X, 'X', 0}; return TRUE;
        case 0x1C: *KeyCode = (KEYCODE){VK_Y, 'Y', 0}; return TRUE;
        case 0x1D: *KeyCode = (KEYCODE){VK_Z, 'Z', 0}; return TRUE;
        case 0x1E: *KeyCode = (KEYCODE){VK_EXCL, '!', 0}; return TRUE;
        case 0x1F: *KeyCode = (KEYCODE){VK_AT, '@', 0}; return TRUE;
        case 0x20: *KeyCode = (KEYCODE){VK_NONE, '#', 0}; return TRUE;
        case 0x21: *KeyCode = (KEYCODE){VK_DOLLAR, '$', 0}; return TRUE;
        case 0x22: *KeyCode = (KEYCODE){VK_PERCENT, '%', 0}; return TRUE;
        case 0x23: *KeyCode = (KEYCODE){VK_NONE, '^', 0}; return TRUE;
        case 0x24: *KeyCode = (KEYCODE){VK_NONE, '&', 0}; return TRUE;
        case 0x25: *KeyCode = (KEYCODE){VK_STAR, '*', 0}; return TRUE;
        case 0x26: *KeyCode = (KEYCODE){VK_NONE, '(', 0}; return TRUE;
        case 0x27: *KeyCode = (KEYCODE){VK_NONE, ')', 0}; return TRUE;
        case 0x2D: *KeyCode = (KEYCODE){VK_UNDERSCORE, '_', 0}; return TRUE;
        case 0x2E: *KeyCode = (KEYCODE){VK_PLUS, '+', 0}; return TRUE;
        case 0x2F: *KeyCode = (KEYCODE){VK_NONE, '{', 0}; return TRUE;
        case 0x30: *KeyCode = (KEYCODE){VK_NONE, '}', 0}; return TRUE;
        case 0x31: *KeyCode = (KEYCODE){VK_NONE, '|', 0}; return TRUE;
        case 0x33: *KeyCode = (KEYCODE){VK_COLON, ':', 0}; return TRUE;
        case 0x34: *KeyCode = (KEYCODE){VK_NONE, '"', 0}; return TRUE;
        case 0x35: *KeyCode = (KEYCODE){VK_NONE, '~', 0}; return TRUE;
        case 0x36: *KeyCode = (KEYCODE){VK_COMMA, '<', 0}; return TRUE;
        case 0x37: *KeyCode = (KEYCODE){VK_DOT, '>', 0}; return TRUE;
        case 0x38: *KeyCode = (KEYCODE){VK_QUESTION, '?', 0}; return TRUE;
        case 0x54: *KeyCode = (KEYCODE){VK_SLASH, '/', 0}; return TRUE;
        case 0x55: *KeyCode = (KEYCODE){VK_STAR, '*', 0}; return TRUE;
        case 0x56: *KeyCode = (KEYCODE){VK_MINUS, '-', 0}; return TRUE;
        case 0x57: *KeyCode = (KEYCODE){VK_PLUS, '+', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_ENTER: *KeyCode = (KEYCODE){VK_ENTER, STR_NEWLINE, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_1: *KeyCode = (KEYCODE){VK_1, '1', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_2: *KeyCode = (KEYCODE){VK_2, '2', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_3: *KeyCode = (KEYCODE){VK_3, '3', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_4: *KeyCode = (KEYCODE){VK_4, '4', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_5: *KeyCode = (KEYCODE){VK_5, '5', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_6: *KeyCode = (KEYCODE){VK_6, '6', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_7: *KeyCode = (KEYCODE){VK_7, '7', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_8: *KeyCode = (KEYCODE){VK_8, '8', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_9: *KeyCode = (KEYCODE){VK_9, '9', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_0: *KeyCode = (KEYCODE){VK_0, '0', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_DOT: *KeyCode = (KEYCODE){VK_DOT, '.', 0}; return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

static BOOL GetFallbackKeyCode(KEY_USAGE Usage, UINT Level, LPKEYCODE KeyCode) {
    if (Level == KEY_LAYOUT_HID_LEVEL_SHIFT) {
        return GetFallbackKeyCodeShift(Usage, KeyCode);
    }

    return GetFallbackKeyCodeBase(Usage, KeyCode);
}

/***************************************************************************/

static BOOL GetDefaultUsageKeyCode(KEY_USAGE Usage, LPKEYCODE KeyCode) {
    switch (Usage) {
        case 0x28: *KeyCode = (KEYCODE){VK_ENTER, STR_NEWLINE, 0}; return TRUE;
        case 0x29: *KeyCode = (KEYCODE){VK_ESCAPE, 0, 0}; return TRUE;
        case 0x2A: *KeyCode = (KEYCODE){VK_BACKSPACE, 0, 0}; return TRUE;
        case 0x2B: *KeyCode = (KEYCODE){VK_TAB, STR_TAB, 0}; return TRUE;
        case 0x2C: *KeyCode = (KEYCODE){VK_SPACE, STR_SPACE, 0}; return TRUE;
        case 0x39: *KeyCode = (KEYCODE){VK_CAPS, 0, 0}; return TRUE;
        case 0x3A: *KeyCode = (KEYCODE){VK_F1, 0, 0}; return TRUE;
        case 0x3B: *KeyCode = (KEYCODE){VK_F2, 0, 0}; return TRUE;
        case 0x3C: *KeyCode = (KEYCODE){VK_F3, 0, 0}; return TRUE;
        case 0x3D: *KeyCode = (KEYCODE){VK_F4, 0, 0}; return TRUE;
        case 0x3E: *KeyCode = (KEYCODE){VK_F5, 0, 0}; return TRUE;
        case 0x3F: *KeyCode = (KEYCODE){VK_F6, 0, 0}; return TRUE;
        case 0x40: *KeyCode = (KEYCODE){VK_F7, 0, 0}; return TRUE;
        case 0x41: *KeyCode = (KEYCODE){VK_F8, 0, 0}; return TRUE;
        case 0x42: *KeyCode = (KEYCODE){VK_F9, 0, 0}; return TRUE;
        case 0x43: *KeyCode = (KEYCODE){VK_F10, 0, 0}; return TRUE;
        case 0x44: *KeyCode = (KEYCODE){VK_F11, 0, 0}; return TRUE;
        case 0x45: *KeyCode = (KEYCODE){VK_F12, 0, 0}; return TRUE;
        case 0x47: *KeyCode = (KEYCODE){VK_SCROLL, 0, 0}; return TRUE;
        case 0x48: *KeyCode = (KEYCODE){VK_PAUSE, 0, 0}; return TRUE;
        case 0x49: *KeyCode = (KEYCODE){VK_INSERT, 0, 0}; return TRUE;
        case 0x4A: *KeyCode = (KEYCODE){VK_HOME, 0, 0}; return TRUE;
        case 0x4B: *KeyCode = (KEYCODE){VK_PAGEUP, 0, 0}; return TRUE;
        case 0x4C: *KeyCode = (KEYCODE){VK_DELETE, 0, 0}; return TRUE;
        case 0x4D: *KeyCode = (KEYCODE){VK_END, 0, 0}; return TRUE;
        case 0x4E: *KeyCode = (KEYCODE){VK_PAGEDOWN, 0, 0}; return TRUE;
        case 0x4F: *KeyCode = (KEYCODE){VK_RIGHT, 0, 0}; return TRUE;
        case 0x50: *KeyCode = (KEYCODE){VK_LEFT, 0, 0}; return TRUE;
        case 0x51: *KeyCode = (KEYCODE){VK_DOWN, 0, 0}; return TRUE;
        case 0x52: *KeyCode = (KEYCODE){VK_UP, 0, 0}; return TRUE;
        case 0x53: *KeyCode = (KEYCODE){VK_NUM, 0, 0}; return TRUE;
        case 0x54: *KeyCode = (KEYCODE){VK_SLASH, '/', 0}; return TRUE;
        case 0x55: *KeyCode = (KEYCODE){VK_STAR, '*', 0}; return TRUE;
        case 0x56: *KeyCode = (KEYCODE){VK_MINUS, '-', 0}; return TRUE;
        case 0x57: *KeyCode = (KEYCODE){VK_PLUS, '+', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_ENTER: *KeyCode = (KEYCODE){VK_ENTER, STR_NEWLINE, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_1: *KeyCode = (KEYCODE){VK_1, '1', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_2: *KeyCode = (KEYCODE){VK_2, '2', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_3: *KeyCode = (KEYCODE){VK_3, '3', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_4: *KeyCode = (KEYCODE){VK_4, '4', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_5: *KeyCode = (KEYCODE){VK_5, '5', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_6: *KeyCode = (KEYCODE){VK_6, '6', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_7: *KeyCode = (KEYCODE){VK_7, '7', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_8: *KeyCode = (KEYCODE){VK_8, '8', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_9: *KeyCode = (KEYCODE){VK_9, '9', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_0: *KeyCode = (KEYCODE){VK_0, '0', 0}; return TRUE;
        case KEY_USAGE_KEYPAD_DOT: *KeyCode = (KEYCODE){VK_DOT, '.', 0}; return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

static BOOL GetKeypadNavigationKeyCode(KEY_USAGE Usage, LPKEYCODE KeyCode) {
    switch (Usage) {
        case KEY_USAGE_KEYPAD_7: *KeyCode = (KEYCODE){VK_HOME, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_8: *KeyCode = (KEYCODE){VK_UP, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_9: *KeyCode = (KEYCODE){VK_PAGEUP, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_4: *KeyCode = (KEYCODE){VK_LEFT, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_5: return FALSE;
        case KEY_USAGE_KEYPAD_6: *KeyCode = (KEYCODE){VK_RIGHT, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_1: *KeyCode = (KEYCODE){VK_END, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_2: *KeyCode = (KEYCODE){VK_DOWN, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_3: *KeyCode = (KEYCODE){VK_PAGEDOWN, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_0: *KeyCode = (KEYCODE){VK_INSERT, 0, 0}; return TRUE;
        case KEY_USAGE_KEYPAD_DOT: *KeyCode = (KEYCODE){VK_DELETE, 0, 0}; return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

static BOOL GetKeyCodeForUsage(KEY_USAGE Usage, UINT Level, LPKEYCODE KeyCode) {
    const KEY_LAYOUT_HID *Layout = Keyboard.LayoutHid;

    ClearKeyCode(KeyCode);

    if (Keyboard.NumLock == 0 && IsUsageKeypadDigit(Usage)) {
        return GetKeypadNavigationKeyCode(Usage, KeyCode);
    }

    if (Layout != NULL) {
        UINT EffectiveLevel = Level;
        if (EffectiveLevel >= Layout->LevelCount) EffectiveLevel = KEY_LAYOUT_HID_LEVEL_BASE;

        if (GetLayoutKeyCode(Layout, Usage, EffectiveLevel, KeyCode)) return TRUE;
        if (EffectiveLevel != KEY_LAYOUT_HID_LEVEL_BASE && GetLayoutKeyCode(Layout, Usage, KEY_LAYOUT_HID_LEVEL_BASE, KeyCode)) return TRUE;
    }

    if (GetFallbackKeyCode(Usage, Level, KeyCode)) return TRUE;
    return GetDefaultUsageKeyCode(Usage, KeyCode);
}

/***************************************************************************/

static void EmitCodePoint(U32 CodePoint) {
    KEYCODE KeyCode;

    SetKeyCodeFromCodePoint(CodePoint, &KeyCode);
    if (IsKeyCodeEmpty(&KeyCode)) return;
    RouteKeyCode(&KeyCode, FALSE);
}

/***************************************************************************/

void UseKeyboardLayout(LPCSTR Code) {
    STR Path[MAX_PATH_NAME];
    const KEY_LAYOUT_HID *Layout = NULL;

    if (Code == NULL) {
        Keyboard.LayoutHid = NULL;
        return;
    }

    if (KernelPathBuildFile(
            KERNEL_PATH_KEY_KEYBOARD_LAYOUTS,
            KERNEL_PATH_DEFAULT_KEYBOARD_LAYOUTS,
            Code,
            KERNEL_FILE_EXTENSION_KEYBOARD_LAYOUT,
            Path,
            MAX_PATH_NAME) == FALSE) {
        WARNING(TEXT("Invalid keyboard layout path, using embedded en-US layout"));
        Keyboard.LayoutHid = NULL;
        Keyboard.PendingDeadKey = 0;
        Keyboard.PendingComposeKey = 0;
        return;
    }

    DEBUG(TEXT("Loading %s"), Path);

    Layout = LoadKeyboardLayout(Path);
    if (Layout == NULL) {
        WARNING(TEXT("Using embedded en-US layout"));
        Keyboard.LayoutHid = NULL;
    } else {
        Keyboard.LayoutHid = Layout;
    }

    Keyboard.PendingDeadKey = 0;
    Keyboard.PendingComposeKey = 0;
}

/***************************************************************************/

void HandleKeyboardUsage(KEY_USAGE Usage, BOOL Pressed) {
    KEYCODE KeyCode;
    U32 CodePoint;
    U32 Result;
    UINT Level;
    BOOL WasDown;

    if (Usage == 0 || Usage > KEY_USAGE_MAX) return;

    if (Pressed == FALSE) {
        U8 VirtualKey = Keyboard.UsageVirtualKey[Usage];

        Keyboard.UsageStatus[Usage] = 0;
        Keyboard.UsageVirtualKey[Usage] = 0;
        if (Keyboard.SoftwareRepeat && Keyboard.RepeatUsage == Usage) {
            Keyboard.RepeatUsage = 0;
            Keyboard.RepeatStartTick = 0;
            Keyboard.RepeatLastTick = 0;
        }

        if (VirtualKey != 0) {
            Keyboard.VirtualKeyStatus[VirtualKey] = 0;
            RouteKeyUp(VirtualKey);
        }

        return;
    }

    WasDown = (Keyboard.UsageStatus[Usage] != 0);
    Keyboard.UsageStatus[Usage] = 1;

    if (IsUsageModifier(Usage)) {
        return;
    }

    if (Keyboard.SoftwareRepeat && WasDown == FALSE && IsUsageRepeatable(Usage)) {
        Keyboard.RepeatUsage = Usage;
        Keyboard.RepeatStartTick = GetSystemTime();
        Keyboard.RepeatLastTick = Keyboard.RepeatStartTick;
    }

    Level = GetLayoutLevel();
    if (GetKeyCodeForUsage(Usage, Level, &KeyCode) == FALSE) return;
    if (IsKeyCodeEmpty(&KeyCode)) return;
    Keyboard.UsageVirtualKey[Usage] = KeyCode.VirtualKey;
    if (KeyCode.VirtualKey != VK_NONE) {
        Keyboard.VirtualKeyStatus[KeyCode.VirtualKey] = 1;
    }

    CodePoint = GetKeyCodePoint(&KeyCode);
    if (CodePoint == 0) {
        RouteKeyCode(&KeyCode, WasDown);
        return;
    }

    if (Keyboard.PendingComposeKey != 0) {
        if (FindComposeResult(Keyboard.LayoutHid, Keyboard.PendingComposeKey, CodePoint, &Result)) {
            Keyboard.PendingComposeKey = 0;
            EmitCodePoint(Result);
            return;
        }

        EmitCodePoint(Keyboard.PendingComposeKey);
        Keyboard.PendingComposeKey = 0;
    }

    if (Keyboard.PendingDeadKey != 0) {
        if (FindDeadKeyResult(Keyboard.LayoutHid, Keyboard.PendingDeadKey, CodePoint, &Result)) {
            Keyboard.PendingDeadKey = 0;
            EmitCodePoint(Result);
            return;
        }

        EmitCodePoint(Keyboard.PendingDeadKey);
        Keyboard.PendingDeadKey = 0;
    }

    if (IsDeadKey(Keyboard.LayoutHid, CodePoint)) {
        Keyboard.PendingDeadKey = CodePoint;
        Keyboard.PendingComposeKey = 0;
        return;
    }

    if (IsComposeStart(Keyboard.LayoutHid, CodePoint)) {
        Keyboard.PendingComposeKey = CodePoint;
        Keyboard.PendingDeadKey = 0;
        return;
    }

    RouteKeyCode(&KeyCode, WasDown);
}

/***************************************************************************/

void HandleKeyboardVirtualKey(U8 VirtualKey, BOOL Pressed) {
    KEYCODE KeyCode;
    BOOL WasDown;

    if (VirtualKey == VK_NONE) {
        return;
    }

    WasDown = (Keyboard.VirtualKeyStatus[VirtualKey] != 0);
    Keyboard.VirtualKeyStatus[VirtualKey] = Pressed ? 1 : 0;
    if (!Pressed) {
        RouteKeyUp(VirtualKey);
        return;
    }

    KeyCode.VirtualKey = VirtualKey;
    KeyCode.ASCIICode = 0;
    KeyCode.Unicode = 0;
    RouteKeyCode(&KeyCode, WasDown);
}

/***************************************************************************/

U32 GetKeyModifiers(void) {
    U32 Modifiers = 0;

    if (Keyboard.UsageStatus[KEY_USAGE_LEFT_SHIFT] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_SHIFT]) {
        Modifiers |= KEYMOD_SHIFT;
    }
    if (Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL]) {
        Modifiers |= KEYMOD_CONTROL;
    }
    if (Keyboard.UsageStatus[KEY_USAGE_LEFT_ALT] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_ALT]) {
        Modifiers |= KEYMOD_ALT;
    }

    return Modifiers;
}

/***************************************************************************/

BOOL GetKeyCodeDown(KEYCODE KeyCode) {
    UINT Usage;
    UINT Level;
    KEYCODE Temp;
    const KEY_LAYOUT_HID *Layout = Keyboard.LayoutHid;

    switch (KeyCode.VirtualKey) {
        case VK_LSHIFT:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_SHIFT] != 0;
        case VK_RSHIFT:
            return Keyboard.UsageStatus[KEY_USAGE_RIGHT_SHIFT] != 0;
        case VK_LCTRL:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] != 0;
        case VK_RCTRL:
            return Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL] != 0;
        case VK_CONTROL:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] != 0 || Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL] != 0;
        case VK_SHIFT:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_SHIFT] != 0 || Keyboard.UsageStatus[KEY_USAGE_RIGHT_SHIFT] != 0;
        case VK_ALT:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_ALT] != 0 || Keyboard.UsageStatus[KEY_USAGE_RIGHT_ALT] != 0;
        case VK_LALT:
            return Keyboard.UsageStatus[KEY_USAGE_LEFT_ALT] != 0;
        case VK_RALT:
            return Keyboard.UsageStatus[KEY_USAGE_RIGHT_ALT] != 0;
        default:
            break;
    }

    if (Keyboard.VirtualKeyStatus[KeyCode.VirtualKey] != 0) {
        return TRUE;
    }

    for (Usage = 0; Usage <= KEY_USAGE_MAX; Usage++) {
        if (Keyboard.UsageStatus[Usage] == 0) continue;

        if (Layout != NULL && Layout->Entries != NULL) {
            for (Level = 0; Level < Layout->LevelCount; Level++) {
                if (Layout->Entries[Usage].Levels[Level].VirtualKey == KeyCode.VirtualKey) {
                    return TRUE;
                }
            }
        }

        if (GetFallbackKeyCode(Usage, KEY_LAYOUT_HID_LEVEL_BASE, &Temp) && Temp.VirtualKey == KeyCode.VirtualKey) return TRUE;
        if (GetFallbackKeyCode(Usage, KEY_LAYOUT_HID_LEVEL_SHIFT, &Temp) && Temp.VirtualKey == KeyCode.VirtualKey) return TRUE;
        if (GetDefaultUsageKeyCode(Usage, &Temp) && Temp.VirtualKey == KeyCode.VirtualKey) return TRUE;
    }

    return FALSE;
}
