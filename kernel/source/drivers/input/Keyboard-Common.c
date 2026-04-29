
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


    Keyboard Common

\************************************************************************/

#include "User.h"
#include "console/Console.h"
#include "drivers/input/Keyboard.h"
#include "input/Hotkey.h"
#include "input/VKey.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process.h"
#include "process/Task.h"
#include "sync/Deferred-Work.h"
#include "system/Clock.h"
#include "text/CoreString.h"

/***************************************************************************/

KEYBOARDSTRUCT Keyboard = {
    .Mutex = EMPTY_MUTEX,
    .Initialized = FALSE,
    .Shift = 1,
    .Control = 0,
    .Alt = 0,
    .CapsLock = 0,
    .NumLock = 0,
    .ScrollLock = 0,
    .Pause = 0,
    .Buffer = {{0}},
    .LayoutHid = NULL,
    .PendingDeadKey = 0,
    .PendingComposeKey = 0,
    .UsageStatus = {0},
    .UsageVirtualKey = {0},
    .VirtualKeyStatus = {0},
    .SoftwareRepeat = FALSE,
    .RepeatUsage = 0,
    .RepeatStartTick = 0,
    .RepeatLastTick = 0,
    .RepeatToken = {.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT}};

/***************************************************************************/

static void KeyboardRepeatPoll(LPVOID Context) {
    UINT Now;

    UNUSED(Context);

    if (Keyboard.SoftwareRepeat == FALSE) {
        return;
    }

    if (Keyboard.RepeatUsage == 0) {
        return;
    }

    if (Keyboard.RepeatUsage > KEY_USAGE_MAX || Keyboard.UsageStatus[Keyboard.RepeatUsage] == 0) {
        Keyboard.RepeatUsage = 0;
        Keyboard.RepeatStartTick = 0;
        Keyboard.RepeatLastTick = 0;
        return;
    }

    Now = GetSystemTime();
    if (Now - Keyboard.RepeatStartTick < 400U) {
        return;
    }

    if (Now - Keyboard.RepeatLastTick < 50U) {
        return;
    }

    Keyboard.RepeatLastTick = Now;
    HandleKeyboardUsage(Keyboard.RepeatUsage, TRUE);
}

/***************************************************************************/

void KeyboardCommonInitialize(void) {
    if (Keyboard.Initialized) {
        return;
    }

    InitMutex(&(Keyboard.Mutex));

    if (DeferredWorkTokenIsValid(Keyboard.RepeatToken) == FALSE) {
        Keyboard.RepeatToken = DeferredWorkRegisterPollOnly(KeyboardRepeatPoll, NULL, TEXT("KeyboardRepeat"));
    }

    if (DeferredWorkTokenIsValid(Keyboard.RepeatToken) == FALSE) {
        ERROR(TEXT("Repeat poll registration failed"));
    }

    Keyboard.Initialized = TRUE;
}

/***************************************************************************/

static void SendKeyCodeToBuffer(LPKEYCODE KeyCode) {
    U32 Index;

    FINE_DEBUG(TEXT("Enter"));

    if (KeyCode->VirtualKey != 0 || KeyCode->ASCIICode != 0) {
        //-------------------------------------
        // Put the key in the buffer

        for (Index = 0; Index < MAXKEYBUFFER; Index++) {
            if (Keyboard.Buffer[Index].VirtualKey == 0 && Keyboard.Buffer[Index].ASCIICode == 0) {
                Keyboard.Buffer[Index] = *KeyCode;
                break;
            }
        }
    }

    FINE_DEBUG(TEXT("Exit"));
}

/***************************************************************************/

static BOOL DispatchKeyMessage(LPKEYCODE KeyCode) {
    if (KeyCode == NULL) return FALSE;
    if (KeyCode->VirtualKey == 0 && KeyCode->ASCIICode == 0) return FALSE;

    return EnqueueInputMessage(EWM_KEYDOWN, KeyCode->VirtualKey, KeyCode->ASCIICode);
}

/***************************************************************************/

static BOOL DispatchKeyUpMessage(U8 VirtualKey) {
    if (VirtualKey == 0) return FALSE;
    return EnqueueInputMessage(EWM_KEYUP, VirtualKey, 0);
}

/***************************************************************************/

void RouteKeyCode(LPKEYCODE KeyCode, BOOL Repeat) {
    if (KeyCode != NULL && KeyCode->VirtualKey != VK_NONE &&
        HotkeyHandleKeyDown(KeyCode->VirtualKey, GetKeyModifiers(), Repeat) == TRUE) {
        return;
    }

    if (DispatchKeyMessage(KeyCode) == FALSE) {
        SendKeyCodeToBuffer(KeyCode);
    }
}

/***************************************************************************/

void RouteKeyUp(U8 VirtualKey) { (void)DispatchKeyUpMessage(VirtualKey); }

/***************************************************************************/

static LPPROCESS LockCurrentProcessMessageQueue(void) {
    LPTASK Task = GetCurrentTask();
    LPPROCESS Process = NULL;

    SAFE_USE_VALID_ID(Task, KOID_TASK) { Process = Task->OwnerProcess; }
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->MessageQueue.MessageBuffer.Entries == NULL || Process->MessageQueue.MessageBuffer.Capacity == 0) {
            return NULL;
        }
    }

    LockMutex(&(Process->Mutex), INFINITY);
    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    return Process;
}

/***************************************************************************/

static void UnlockCurrentProcessMessageQueue(LPPROCESS Process) {
    if (Process == NULL) {
        return;
    }

    UnlockMutex(&(Process->MessageQueue.Mutex));
    UnlockMutex(&(Process->Mutex));
}

/***************************************************************************/

static BOOL FetchKeyFromMessageQueue(BOOL RemoveKeyDown, BOOL PurgeKeyUp, LPKEYCODE KeyCode) {
    LPPROCESS Process;
    BOOL Found = FALSE;
    MESSAGE CurrentMessage;
    UINT Offset = 0;

    if (KeyCode == NULL) return FALSE;

    Process = LockCurrentProcessMessageQueue();
    if (Process == NULL) return FALSE;

    while (Offset < MessageQueueBufferGetCount(&(Process->MessageQueue.MessageBuffer))) {
        if (MessageQueueBufferReadAt(&(Process->MessageQueue.MessageBuffer), Offset, &CurrentMessage) == FALSE) {
            break;
        }

        if (CurrentMessage.Message == EWM_KEYUP) {
            if (PurgeKeyUp) {
                (void)MessageQueueBufferRemoveAt(&(Process->MessageQueue.MessageBuffer), Offset, NULL);
                continue;
            }
        } else if (CurrentMessage.Message == EWM_KEYDOWN && Found == FALSE) {
            KeyCode->VirtualKey = CurrentMessage.Param1;
            KeyCode->ASCIICode = (STR)CurrentMessage.Param2;
            Found = TRUE;
            if (RemoveKeyDown) {
                (void)MessageQueueBufferRemoveAt(&(Process->MessageQueue.MessageBuffer), Offset, NULL);
                continue;
            }
        }

        Offset++;
    }

    UnlockCurrentProcessMessageQueue(Process);

    return Found;
}

/***************************************************************************/

static BOOL PeekKeyInMessageQueue(LPKEYCODE KeyCode) {
    LPPROCESS Process;
    BOOL Found = FALSE;
    MESSAGE CurrentMessage;
    UINT Offset = 0;

    if (KeyCode == NULL) return FALSE;

    Process = LockCurrentProcessMessageQueue();
    if (Process == NULL) return FALSE;

    for (Offset = 0; Offset < MessageQueueBufferGetCount(&(Process->MessageQueue.MessageBuffer)); Offset++) {
        if (MessageQueueBufferReadAt(&(Process->MessageQueue.MessageBuffer), Offset, &CurrentMessage) == FALSE) {
            break;
        }

        if (CurrentMessage.Message == EWM_KEYDOWN) {
            KeyCode->VirtualKey = CurrentMessage.Param1;
            KeyCode->ASCIICode = (STR)CurrentMessage.Param2;
            Found = TRUE;
            break;
        }
    }

    UnlockCurrentProcessMessageQueue(Process);

    return Found;
}

/***************************************************************************/

BOOL PeekChar(void) {
    U32 Result = FALSE;
    KEYCODE KeyCode = {0};

    FINE_DEBUG(TEXT("Enter"));

    if (PeekKeyInMessageQueue(&KeyCode) == TRUE) {
        return TRUE;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    if (Keyboard.Buffer[0].VirtualKey) Result = TRUE;
    if (Keyboard.Buffer[0].ASCIICode) Result = TRUE;

    UnlockMutex(&(Keyboard.Mutex));

    FINE_DEBUG(TEXT("Exit"));

    return Result;
}

/***************************************************************************/

STR GetChar(void) {
    U32 Index;
    STR Char;
    KEYCODE KeyCode = {0};

    if (FetchKeyFromMessageQueue(TRUE, TRUE, &KeyCode) == TRUE) {
        return KeyCode.ASCIICode;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    Char = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return Char;
}

/***************************************************************************/

BOOL GetKeyCode(LPKEYCODE KeyCode) {
    U32 Index;

    if (FetchKeyFromMessageQueue(TRUE, TRUE, KeyCode) == TRUE) {
        return TRUE;
    }

    LockMutex(&(Keyboard.Mutex), INFINITY);

    KeyCode->VirtualKey = Keyboard.Buffer[0].VirtualKey;
    KeyCode->ASCIICode = Keyboard.Buffer[0].ASCIICode;

    //-------------------------------------
    // Roll the keyboard buffer

    for (Index = 1; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index - 1] = Keyboard.Buffer[Index];
    }

    Keyboard.Buffer[MAXKEYBUFFER - 1].VirtualKey = 0;
    Keyboard.Buffer[MAXKEYBUFFER - 1].ASCIICode = 0;

    UnlockMutex(&(Keyboard.Mutex));

    return TRUE;
}

/***************************************************************************/

void WaitKey(void) {
    ConsolePrint(TEXT("Press a key\n"));
    while (!PeekChar()) {
    }
    GetChar();
}

/***************************************************************************/

/**
 * @brief Clear buffered keyboard characters.
 */
void ClearKeyboardBuffer(void) {
    U32 Index;

    LockMutex(&(Keyboard.Mutex), INFINITY);

    for (Index = 0; Index < MAXKEYBUFFER; Index++) {
        Keyboard.Buffer[Index].VirtualKey = 0;
        Keyboard.Buffer[Index].ASCIICode = 0;
    }

    Keyboard.PendingDeadKey = 0;
    Keyboard.PendingComposeKey = 0;
    MemorySet(Keyboard.UsageStatus, 0, sizeof(Keyboard.UsageStatus));
    MemorySet(Keyboard.UsageVirtualKey, 0, sizeof(Keyboard.UsageVirtualKey));
    MemorySet(Keyboard.VirtualKeyStatus, 0, sizeof(Keyboard.VirtualKeyStatus));

    UnlockMutex(&(Keyboard.Mutex));
}

/***************************************************************************/
