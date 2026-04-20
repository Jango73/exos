
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

#include "drivers/input/Keyboard.h"

#include "Base.h"
#include "Arch.h"
#include "console/Console.h"
#include "drivers/interrupts/InterruptController.h"
#include "DisplaySession.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "input/VKey.h"
#include "User.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT StdKeyboardCommands(UINT, UINT);

DRIVER DATA_SECTION StdKeyboardDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_KEYBOARD,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "IBM PC and compatibles",
    .Product = "Standard IBM PC Keyboard - 102 keys",
    .Alias = "ps2_keyboard",
    .Flags = 0,
    .Command = StdKeyboardCommands};

/***************************************************************************/

/**
 * @brief Retrieves the standard keyboard driver descriptor.
 * @return Pointer to the standard keyboard driver.
 */
LPDRIVER StdKeyboardGetDriver(void) {
    return &StdKeyboardDriver;
}

/***************************************************************************/
// Standard scan codes

#define SCAN_ESCAPE 0x01
#define SCAN_BACK 0x0E
#define SCAN_TAB 0x0F
#define SCAN_ENTER 0x1C
#define SCAN_CONTROL 0x1D
#define SCAN_LEFT_SHIFT 0x2A
#define SCAN_RIGHT_SHIFT 0x36
#define SCAN_START 0x37
#define SCAN_ALT 0x38
#define SCAN_SPACE 0x39
#define SCAN_CAPS_LOCK 0x3A
#define SCAN_F1 0x3B
#define SCAN_F2 0x3C
#define SCAN_F3 0x3D
#define SCAN_F4 0x3E
#define SCAN_F5 0x3F
#define SCAN_F6 0x40
#define SCAN_F7 0x41
#define SCAN_F8 0x42
#define SCAN_F9 0x43
#define SCAN_F10 0x44
#define SCAN_NUM_LOCK 0x45
#define SCAN_SCROLL_LOCK 0x46
#define SCAN_PAD_7 0x47
#define SCAN_PAD_8 0x48
#define SCAN_PAD_9 0x49
#define SCAN_PAD_4 0x4B
#define SCAN_PAD_5 0x4C
#define SCAN_PAD_6 0x4D
#define SCAN_PAD_1 0x4F
#define SCAN_PAD_2 0x50
#define SCAN_PAD_3 0x51
#define SCAN_PAD_0 0x52
#define SCAN_PAD_DOT 0x53
#define SCAN_PAD_MINUS 0x4A
#define SCAN_PAD_PLUS 0x4E
#define SCAN_F11 0x57
#define SCAN_F12 0x58

/***************************************************************************/
// Extended scan codes
// Starting with 0xE0

#define SCAN_PAD_ENTER 0x1C
#define SCAN_RIGHT_CONTROL 0x1D
#define SCAN_PAD_SLASH 0x35
#define SCAN_PAD_STAR 0x37
#define SCAN_RIGHT_ALT 0x38
#define SCAN_HOME 0x47
#define SCAN_UP 0x48
#define SCAN_PAGEUP 0x49
#define SCAN_LEFT 0x4B
#define SCAN_RIGHT 0x4D
#define SCAN_END 0x4F
#define SCAN_DOWN 0x50
#define SCAN_PAGEDOWN 0x51
#define SCAN_INSERT 0x52
#define SCAN_DELETE 0x53
#define SCAN_MEDIA_PREV 0x10
#define SCAN_MEDIA_NEXT 0x19
#define SCAN_MEDIA_PLAY_PAUSE 0x22
#define SCAN_MEDIA_STOP 0x24
#define SCAN_MEDIA_MUTE 0x20
#define SCAN_MEDIA_VOLUME_UP 0x30
#define SCAN_MEDIA_VOLUME_DOWN 0x2E
#define SCAN_MEDIA_SLEEP 0x5F
#define SCAN_MEDIA_EJECT 0x64

/***************************************************************************/
// Extended scan codes
// Starting with 0xE1 - 0x1D

#define SCAN_PAUSE 0x45

/***************************************************************************/


static void KeyboardWait(void) {
    U32 Index;

    for (Index = 0; Index < 0x100000; Index++) {
        if ((InPortByte(KEYBOARD_COMMAND) & KSR_IN_FULL) == 0) {
            return;
        }
    }
}

/***************************************************************************/

static BOOL KeyboardACK(void) {
    U32 Loop;

    for (Loop = 0; Loop < 0x100000; Loop++) {
        if (InPortByte(KEYBOARD_COMMAND) & KSR_OUT_FULL) break;
    }

    if (InPortByte(KEYBOARD_DATA) == KSS_ACK) return TRUE;

    return FALSE;
}

/***************************************************************************/

static void SendKeyboardCommand(U32 Command, U32 Data) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    KeyboardWait();

    OutPortByte(KEYBOARD_DATA, Command);
    if (KeyboardACK() == FALSE) goto Out;
    OutPortByte(KEYBOARD_DATA, Data);
    if (KeyboardACK() == FALSE) goto Out;

Out:

    RestoreFlags(&Flags);
}

/***************************************************************************/

U16 DetectKeyboard(void) {
    U32 Flags;
    U8 Id1;
    U8 Id2;

    SaveFlags(&Flags);
    DisableInterrupts();
    KeyboardWait();
    OutPortByte(KEYBOARD_DATA, 0xF2);
    KeyboardWait();
    Id1 = InPortByte(KEYBOARD_DATA);
    KeyboardWait();
    Id2 = InPortByte(KEYBOARD_DATA);
    RestoreFlags(&Flags);

    return (U16)(Id2 << 8 | Id1);
}

/***************************************************************************/

static const KEY_USAGE ScanCodeToUsageTable[128] = {
    0x00, 0x29, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2D, 0x2E, 0x2A, 0x2B,
    0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30, 0x28, KEY_USAGE_LEFT_CTRL, 0x04, 0x16,
    0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34, 0x35, KEY_USAGE_LEFT_SHIFT, 0x31, 0x1D, 0x1B, 0x06, 0x19,
    0x05, 0x11, 0x10, 0x36, 0x37, 0x38, KEY_USAGE_RIGHT_SHIFT, 0x55, KEY_USAGE_LEFT_ALT, 0x2C, KEY_USAGE_CAPS_LOCK, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E,
    0x3F, 0x40, 0x41, 0x42, 0x43, KEY_USAGE_NUM_LOCK, KEY_USAGE_SCROLL_LOCK, KEY_USAGE_KEYPAD_7,
    KEY_USAGE_KEYPAD_8, KEY_USAGE_KEYPAD_9, 0x56, KEY_USAGE_KEYPAD_4, KEY_USAGE_KEYPAD_5, KEY_USAGE_KEYPAD_6, 0x57, KEY_USAGE_KEYPAD_1,
    KEY_USAGE_KEYPAD_2, KEY_USAGE_KEYPAD_3, KEY_USAGE_KEYPAD_0, KEY_USAGE_KEYPAD_DOT, 0x00, 0x00, 0x64, 0x44,
    0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/***************************************************************************/

static KEY_USAGE ScanCodeToUsage(U32 ScanCode) {
    if (ScanCode >= (sizeof(ScanCodeToUsageTable) / sizeof(ScanCodeToUsageTable[0]))) {
        return 0;
    }

    return ScanCodeToUsageTable[ScanCode];
}

/***************************************************************************/

static KEY_USAGE ScanCodeToUsage_E0(U32 ScanCode) {
    switch (ScanCode) {
        case SCAN_RIGHT_CONTROL:
            return KEY_USAGE_RIGHT_CTRL;
        case SCAN_RIGHT_ALT:
            return KEY_USAGE_RIGHT_ALT;
        case SCAN_HOME:
            return 0x4A;
        case SCAN_UP:
            return 0x52;
        case SCAN_PAGEUP:
            return 0x4B;
        case SCAN_LEFT:
            return 0x50;
        case SCAN_RIGHT:
            return 0x4F;
        case SCAN_END:
            return 0x4D;
        case SCAN_DOWN:
            return 0x51;
        case SCAN_PAGEDOWN:
            return 0x4E;
        case SCAN_INSERT:
            return 0x49;
        case SCAN_DELETE:
            return 0x4C;
        case SCAN_PAD_ENTER:
            return KEY_USAGE_KEYPAD_ENTER;
        case SCAN_PAD_SLASH:
            return 0x54;
        case SCAN_PAD_STAR:
            return 0x46;
    }

    return 0;
}

/***************************************************************************/

static KEY_USAGE ScanCodeToUsage_E1(U32 ScanCode) {
    if (ScanCode == SCAN_PAUSE) {
        return 0x48;
    }

    return 0;
}

/***************************************************************************/

static U8 ScanCodeToVirtualKey_E0(U32 ScanCode) {
    switch (ScanCode) {
        case SCAN_MEDIA_PREV:
            return VK_MEDIA_PREV;
        case SCAN_MEDIA_NEXT:
            return VK_MEDIA_NEXT;
        case SCAN_MEDIA_PLAY_PAUSE:
            return VK_MEDIA_PLAY_PAUSE;
        case SCAN_MEDIA_STOP:
            return VK_MEDIA_STOP;
        case SCAN_MEDIA_MUTE:
            return VK_MEDIA_MUTE;
        case SCAN_MEDIA_VOLUME_UP:
            return VK_MEDIA_VOLUME_UP;
        case SCAN_MEDIA_VOLUME_DOWN:
            return VK_MEDIA_VOLUME_DOWN;
        case SCAN_MEDIA_SLEEP:
            return VK_MEDIA_SLEEP;
        case SCAN_MEDIA_EJECT:
            return VK_MEDIA_EJECT;
    }

    return VK_NONE;
}

/***************************************************************************/

static void UpdateKeyboardLEDs(void) {
    U32 LED = 0;

    if (Keyboard.CapsLock) LED |= KSL_CAPS;
    if (Keyboard.NumLock) LED |= KSL_NUM;
    if (Keyboard.ScrollLock) LED |= KSL_SCROLL;

    SendKeyboardCommand(KSC_SETLEDSTATUS, LED);
}

/***************************************************************************/

static U32 GetKeyboardLEDs(void) {
    U32 LED = 0;

    if (Keyboard.CapsLock) LED |= KSL_CAPS;
    if (Keyboard.NumLock) LED |= KSL_NUM;
    if (Keyboard.ScrollLock) LED |= KSL_SCROLL;

    return LED;
}

/***************************************************************************/

static U32 SetKeyboardLEDs(U32 LED) {
    Keyboard.CapsLock = 0;
    Keyboard.NumLock = 0;
    Keyboard.ScrollLock = 0;

    if (LED & KSL_CAPS) Keyboard.CapsLock = 1;
    if (LED & KSL_NUM) Keyboard.NumLock = 1;
    if (LED & KSL_SCROLL) Keyboard.ScrollLock = 1;

    UpdateKeyboardLEDs();

    return TRUE;
}

/***************************************************************************/

static void HandleScanCode(U32 ScanCode) {
    static U32 DATA_SECTION PreviousCode = 0;
    KEY_USAGE Usage = 0;
    BOOL Pressed = FALSE;

    FINE_DEBUG(TEXT("[HandleScanCode] Enter"));

    if (ScanCode == 0) {
        PreviousCode = 0;
        return;
    }

    if (ScanCode == 0xE0 || ScanCode == 0xE1) {
        PreviousCode = ScanCode;
        return;
    }

    //-------------------------------------
    // Process special keys or translate
    // the scan code to an ASCII code

    if (PreviousCode == 0xE0) {
        U8 VirtualKey = VK_NONE;

        PreviousCode = 0;

        Pressed = (ScanCode & 0x80) == 0;
        Usage = ScanCodeToUsage_E0(ScanCode & 0x7F);
        if (Usage != 0) {
            HandleKeyboardUsage(Usage, Pressed);
        } else {
            VirtualKey = ScanCodeToVirtualKey_E0(ScanCode & 0x7F);
            if (VirtualKey != VK_NONE) {
                HandleKeyboardVirtualKey(VirtualKey, Pressed);
            }
        }
    } else if (PreviousCode == 0xE1) {
        PreviousCode = 0;

        if (ScanCode == 0x1D) PreviousCode = ScanCode;
    } else if (PreviousCode == 0x1D) {
        PreviousCode = 0;

        if ((ScanCode & 0x80) == 0) {
            Usage = ScanCodeToUsage_E1(ScanCode);
            if (Usage != 0) {
                HandleKeyboardUsage(Usage, TRUE);
            }
        }
    } else {
        PreviousCode = 0;

        Pressed = (ScanCode & 0x80) == 0;
        if (Pressed == FALSE) {
            Usage = ScanCodeToUsage(ScanCode & 0x7F);
            if (Usage != 0) {
                HandleKeyboardUsage(Usage, FALSE);
            }

            FINE_DEBUG(TEXT("[HandleScanCode] Exit"));
            return;
        }

        switch (ScanCode) {
            case SCAN_NUM_LOCK: {
                Keyboard.NumLock = 1 - Keyboard.NumLock;
                UpdateKeyboardLEDs();
            } break;

            case SCAN_CAPS_LOCK: {
                Keyboard.CapsLock = 1 - Keyboard.CapsLock;
                UpdateKeyboardLEDs();
            } break;

            case SCAN_SCROLL_LOCK: {
                Keyboard.ScrollLock = 1 - Keyboard.ScrollLock;
                UpdateKeyboardLEDs();
            } break;

            default: {
                Usage = ScanCodeToUsage(ScanCode);
                if (Usage != 0) {
                    HandleKeyboardUsage(Usage, TRUE);
                }

                if (Usage == 0x42) {
                    if (Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL]) {
                        (void)DisplaySwitchToConsole();
                    } else {
                        TASK_INFO TaskInfo;
                        TaskInfo.Header.Size = sizeof(TASK_INFO);
                        TaskInfo.Header.Version = EXOS_ABI_VERSION;
                        TaskInfo.Header.Flags = 0;
                        TaskInfo.Func = Shell;
                        TaskInfo.Parameter = NULL;
                        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
                        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
                        TaskInfo.Flags = 0;
                        KernelCreateTask(&KernelProcess, &TaskInfo);
                    }
                }
            } break;
        }
    }

    FINE_DEBUG(TEXT("[HandleScanCode] Exit"));
}

/***************************************************************************/


void KeyboardHandler(void) {
    static U32 DATA_SECTION Busy = 0;
    U32 Status, Code;

    FINE_DEBUG(TEXT("[KeyboardHandler] Enter"));

    if (Busy) {
        FINE_DEBUG(TEXT("[KeyboardHandler] Busy, exiting"));

        return;
    }

    Busy = 1;

    Status = InPortByte(KEYBOARD_COMMAND);

    do {
        if (Status & KSR_OUT_ERROR) {
            ERROR(TEXT("[KeyboardHandler] Keyboard error detected, breaking"));
            break;
        }

        Code = InPortByte(KEYBOARD_DATA);

        if (Status & KSR_OUT_FULL) {
            HandleScanCode(Code);
        }

        Status = InPortByte(KEYBOARD_COMMAND);
    } while (Status & KSR_OUT_FULL);

    Busy = 0;

    FINE_DEBUG(TEXT("[KeyboardHandler] Exit"));
}

/***************************************************************************/

static U32 InitializeKeyboard(void) {
    //-------------------------------------
    // Initialize the keyboard structure

    KeyboardCommonInitialize();

    Keyboard.NumLock = 1;

    //-------------------------------------
    // Enable the keyboard

    SendKeyboardCommand(KSC_ENABLE, KSC_ENABLE);

    //-------------------------------------
    // Set the LED status

    UpdateKeyboardLEDs();

    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);
    InPortByte(KEYBOARD_COMMAND);

    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);
    InPortByte(KEYBOARD_DATA);

    //-------------------------------------
    // Enable the keyboard's IRQ

    DEBUG(TEXT("Keyboard: About to enable IRQ_KEYBOARD (%d)"), IRQ_KEYBOARD);
    DEBUG(TEXT("Keyboard: Active interrupt controller type: %d"), GetActiveInterruptControllerType());

    if (EnableInterrupt(IRQ_KEYBOARD)) {
        DEBUG(TEXT("Keyboard: IRQ_KEYBOARD enabled successfully"));
    } else {
        DEBUG(TEXT("Keyboard: Failed to enable IRQ_KEYBOARD"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

UINT StdKeyboardCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((StdKeyboardDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeKeyboard() == DF_RETURN_SUCCESS) {
                StdKeyboardDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;
        case DF_UNLOAD:
            if ((StdKeyboardDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            StdKeyboardDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_GET_LAST_FUNCTION:
            return 0;
        case DF_KEY_GETSTATE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_KEY_ISKEY:
            return (UINT)PeekChar();
        case DF_KEY_GETKEY:
            return (UINT)GetKeyCode((LPKEYCODE)Parameter);
        case DF_KEY_GETLED:
            return (UINT)GetKeyboardLEDs();
        case DF_KEY_SETLED:
            return (UINT)SetKeyboardLEDs(Parameter);
        case DF_KEY_GETDELAY:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_KEY_SETDELAY:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_KEY_GETRATE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_KEY_SETRATE:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/***************************************************************************/
