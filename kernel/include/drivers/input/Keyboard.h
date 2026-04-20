
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


    Keyboard

\************************************************************************/

#ifndef KEYBOARD_H_INCLUDED
#define KEYBOARD_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "User.h"
#include "core/Driver.h"
#include "process/Process.h"
#include "sync/DeferredWork.h"

/***************************************************************************/

// Functions supplied by a keyboard driver

#define DF_KEY_GETSTATE (DF_FIRST_FUNCTION + 0)
#define DF_KEY_ISKEY (DF_FIRST_FUNCTION + 1)
#define DF_KEY_GETKEY (DF_FIRST_FUNCTION + 2)
#define DF_KEY_GETLED (DF_FIRST_FUNCTION + 3)
#define DF_KEY_SETLED (DF_FIRST_FUNCTION + 4)
#define DF_KEY_GETDELAY (DF_FIRST_FUNCTION + 5)
#define DF_KEY_SETDELAY (DF_FIRST_FUNCTION + 6)
#define DF_KEY_GETRATE (DF_FIRST_FUNCTION + 7)
#define DF_KEY_SETRATE (DF_FIRST_FUNCTION + 8)

/***************************************************************************/

#define KEY_USAGE_PAGE_KEYBOARD 0x07
#define KEY_USAGE_MIN 0x04
#define KEY_USAGE_MAX 0xE7

#define KEYTABSIZE (KEY_USAGE_MAX + 1)
#define MAXKEYBUFFER 128

#define KEY_USAGE_LEFT_CTRL 0xE0
#define KEY_USAGE_LEFT_SHIFT 0xE1
#define KEY_USAGE_LEFT_ALT 0xE2
#define KEY_USAGE_LEFT_GUI 0xE3
#define KEY_USAGE_RIGHT_CTRL 0xE4
#define KEY_USAGE_RIGHT_SHIFT 0xE5
#define KEY_USAGE_RIGHT_ALT 0xE6
#define KEY_USAGE_RIGHT_GUI 0xE7

#define KEY_USAGE_CAPS_LOCK 0x39
#define KEY_USAGE_SCROLL_LOCK 0x47
#define KEY_USAGE_NUM_LOCK 0x53

#define KEY_USAGE_KEYPAD_ENTER 0x58
#define KEY_USAGE_KEYPAD_1 0x59
#define KEY_USAGE_KEYPAD_2 0x5A
#define KEY_USAGE_KEYPAD_3 0x5B
#define KEY_USAGE_KEYPAD_4 0x5C
#define KEY_USAGE_KEYPAD_5 0x5D
#define KEY_USAGE_KEYPAD_6 0x5E
#define KEY_USAGE_KEYPAD_7 0x5F
#define KEY_USAGE_KEYPAD_8 0x60
#define KEY_USAGE_KEYPAD_9 0x61
#define KEY_USAGE_KEYPAD_0 0x62
#define KEY_USAGE_KEYPAD_DOT 0x63

#define KEY_LAYOUT_HID_MAX_LEVELS 4
#define KEY_LAYOUT_HID_MAX_DEAD_KEYS 128
#define KEY_LAYOUT_HID_MAX_COMPOSE 256

#define KEY_LAYOUT_HID_LEVEL_BASE 0x00
#define KEY_LAYOUT_HID_LEVEL_SHIFT 0x01
#define KEY_LAYOUT_HID_LEVEL_ALTGR 0x02
#define KEY_LAYOUT_HID_LEVEL_CONTROL 0x03

#define KEY_LAYOUT_FALLBACK_CODE "en-US"

/***************************************************************************/

typedef struct tag_KEYTRANS {
    KEYCODE Normal;
    KEYCODE Shift;
    KEYCODE Alt;
} KEYTRANS, *LPKEYTRANS;

/***************************************************************************/

typedef UINT KEY_USAGE;

typedef struct tag_KEY_LAYOUT_HID_ENTRY {
    KEYCODE Levels[KEY_LAYOUT_HID_MAX_LEVELS];
} KEY_LAYOUT_HID_ENTRY, *LPKEY_LAYOUT_HID_ENTRY;

/***************************************************************************/

typedef struct tag_KEY_HID_DEAD_KEY {
    U32 DeadKey;
    U32 BaseKey;
    U32 Result;
} KEY_HID_DEAD_KEY, *LPKEY_HID_DEAD_KEY;

/***************************************************************************/

typedef struct tag_KEY_HID_COMPOSE_ENTRY {
    U32 FirstKey;
    U32 SecondKey;
    U32 Result;
} KEY_HID_COMPOSE_ENTRY, *LPKEY_HID_COMPOSE_ENTRY;

/***************************************************************************/

// HID usage page 0x07 layout (separate from legacy PS/2 scan code tables).
typedef struct tag_KEY_LAYOUT_HID {
    LPCSTR Code;
    UINT LevelCount;
    const KEY_LAYOUT_HID_ENTRY *Entries;
    UINT EntryCount;
    const KEY_HID_DEAD_KEY *DeadKeys;
    UINT DeadKeyCount;
    const KEY_HID_COMPOSE_ENTRY *ComposeEntries;
    UINT ComposeCount;
} KEY_LAYOUT_HID, *LPKEY_LAYOUT_HID;

/***************************************************************************/

typedef struct tag_KEYBOARDSTRUCT {
    MUTEX Mutex;
    BOOL Initialized;

    U32 Shift;
    U32 Control;
    U32 Alt;

    U32 CapsLock;
    U32 NumLock;
    U32 ScrollLock;
    U32 Pause;

    KEYCODE Buffer[MAXKEYBUFFER];

    const KEY_LAYOUT_HID *LayoutHid;
    U32 PendingDeadKey;
    U32 PendingComposeKey;
    U8 UsageStatus[KEYTABSIZE];
    U8 UsageVirtualKey[KEYTABSIZE];
    U8 VirtualKeyStatus[0x100];
    BOOL SoftwareRepeat;
    KEY_USAGE RepeatUsage;
    UINT RepeatStartTick;
    UINT RepeatLastTick;
    DEFERRED_WORK_TOKEN RepeatToken;
} KEYBOARDSTRUCT, *LPKEYBOARDSTRUCT;

/***************************************************************************/

extern KEYBOARDSTRUCT Keyboard;

/***************************************************************************/

void RouteKeyCode(LPKEYCODE KeyCode, BOOL Repeat);
void RouteKeyUp(U8 VirtualKey);
void HandleKeyboardUsage(KEY_USAGE Usage, BOOL Pressed);
void HandleKeyboardVirtualKey(U8 VirtualKey, BOOL Pressed);
void KeyboardCommonInitialize(void);
BOOL PeekChar(void);
STR GetChar(void);
BOOL GetKeyCode(LPKEYCODE);
BOOL GetKeyCodeDown(KEYCODE);
U32 GetKeyModifiers(void);
void WaitKey(void);
void KeyboardHandler(void);
LPCSTR GetKeyName(U8);
void UseKeyboardLayout(LPCSTR Code);
const KEY_LAYOUT_HID *LoadKeyboardLayout(LPCSTR Path);
U16 DetectKeyboard(void);
void ClearKeyboardBuffer(void);

/***************************************************************************/

#endif
