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


    Hotkey

\************************************************************************/

#include "input/Hotkey.h"

#include "text/CoreString.h"
#include "core/KernelData.h"
#include "log/Log.h"
#include "desktop/DisplaySession.h"
#include "input/VKey.h"
#include "process/Process-Control.h"
#include "process/Task-Messaging.h"

/************************************************************************/

typedef struct tag_HOTKEY_BINDING {
    U32 Modifiers;
    U8 VirtualKey;
} HOTKEY_BINDING, *LPHOTKEY_BINDING;

typedef BOOL (*HOTKEY_ACTION_HANDLER)(LPPROCESS Process);

typedef struct tag_HOTKEY_ACTION {
    LPCSTR Name;
    HOTKEY_ACTION_HANDLER Handler;
} HOTKEY_ACTION, *LPHOTKEY_ACTION;

/************************************************************************/

static BOOL HotkeyActionKillProcess(LPPROCESS Process) {
    return PostProcessMessage(Process, ETM_PROCESS_KILL, 0, 0);
}

/************************************************************************/

static BOOL HotkeyActionPauseProcess(LPPROCESS Process) {
    return PostProcessMessage(Process, ETM_PROCESS_TOGGLE_PAUSE, 0, 0);
}

/************************************************************************/

static BOOL HotkeyActionSwitchToConsole(LPPROCESS Process) {
    UNUSED(Process);
    return DisplaySwitchToConsole();
}

/************************************************************************/

static BOOL HotkeyActionToggleWindowPipelineTrace(LPPROCESS Process) {
    UNUSED(Process);

    SetWindowPipelineTraceEnabled(!GetWindowPipelineTraceEnabled());
    DEBUG(TEXT("Window pipeline trace %s"),
        GetWindowPipelineTraceEnabled() ? TEXT("enabled") : TEXT("disabled"));
    return TRUE;
}

/************************************************************************/

static U8 HotkeyTokenToVirtualKey(LPCSTR Token) {
    if (Token == NULL || Token[0] == STR_NULL) {
        return VK_NONE;
    }

    if (Token[0] != STR_NULL && Token[1] == STR_NULL) {
        if (Token[0] >= 'a' && Token[0] <= 'z') {
            return (U8)(VK_A + (Token[0] - 'a'));
        }

        if (Token[0] >= 'A' && Token[0] <= 'Z') {
            return (U8)(VK_A + (Token[0] - 'A'));
        }

        if (Token[0] >= '0' && Token[0] <= '9') {
            return (U8)(VK_0 + (Token[0] - '0'));
        }
    }

    if (StringCompareNC(Token, TEXT("space")) == 0) return VK_SPACE;
    if (StringCompareNC(Token, TEXT("enter")) == 0) return VK_ENTER;
    if (StringCompareNC(Token, TEXT("escape")) == 0 || StringCompareNC(Token, TEXT("esc")) == 0) return VK_ESCAPE;
    if (StringCompareNC(Token, TEXT("tab")) == 0) return VK_TAB;
    if (StringCompareNC(Token, TEXT("backspace")) == 0) return VK_BACKSPACE;
    if (StringCompareNC(Token, TEXT("insert")) == 0) return VK_INSERT;
    if (StringCompareNC(Token, TEXT("delete")) == 0) return VK_DELETE;
    if (StringCompareNC(Token, TEXT("home")) == 0) return VK_HOME;
    if (StringCompareNC(Token, TEXT("end")) == 0) return VK_END;
    if (StringCompareNC(Token, TEXT("pageup")) == 0) return VK_PAGEUP;
    if (StringCompareNC(Token, TEXT("pagedown")) == 0) return VK_PAGEDOWN;
    if (StringCompareNC(Token, TEXT("up")) == 0) return VK_UP;
    if (StringCompareNC(Token, TEXT("down")) == 0) return VK_DOWN;
    if (StringCompareNC(Token, TEXT("left")) == 0) return VK_LEFT;
    if (StringCompareNC(Token, TEXT("right")) == 0) return VK_RIGHT;

    if (StringCompareNC(Token, TEXT("f1")) == 0) return VK_F1;
    if (StringCompareNC(Token, TEXT("f2")) == 0) return VK_F2;
    if (StringCompareNC(Token, TEXT("f3")) == 0) return VK_F3;
    if (StringCompareNC(Token, TEXT("f4")) == 0) return VK_F4;
    if (StringCompareNC(Token, TEXT("f5")) == 0) return VK_F5;
    if (StringCompareNC(Token, TEXT("f6")) == 0) return VK_F6;
    if (StringCompareNC(Token, TEXT("f7")) == 0) return VK_F7;
    if (StringCompareNC(Token, TEXT("f8")) == 0) return VK_F8;
    if (StringCompareNC(Token, TEXT("f9")) == 0) return VK_F9;
    if (StringCompareNC(Token, TEXT("f10")) == 0) return VK_F10;
    if (StringCompareNC(Token, TEXT("f11")) == 0) return VK_F11;
    if (StringCompareNC(Token, TEXT("f12")) == 0) return VK_F12;

    return VK_NONE;
}

/************************************************************************/

static BOOL HotkeyParseExpression(LPCSTR Expression, LPHOTKEY_BINDING Binding) {
    UINT Length = 0;
    UINT ReadIndex = 0;
    U32 Modifiers = 0;
    U8 VirtualKey = VK_NONE;

    if (Expression == NULL || Binding == NULL) {
        return FALSE;
    }

    Length = StringLength(Expression);
    if (Length == 0 || Length >= MAX_PATH_NAME) {
        return FALSE;
    }

    while (ReadIndex < Length) {
        STR Token[MAX_FILE_NAME];
        UINT TokenLength = 0;

        while (ReadIndex < Length && (Expression[ReadIndex] == '+' || Expression[ReadIndex] == STR_SPACE ||
                                      Expression[ReadIndex] == STR_TAB || Expression[ReadIndex] == '-')) {
            ReadIndex++;
        }

        while (ReadIndex < Length && Expression[ReadIndex] != '+' && Expression[ReadIndex] != '-' &&
               Expression[ReadIndex] != STR_SPACE && Expression[ReadIndex] != STR_TAB) {
            if (TokenLength < MAX_FILE_NAME - 1) {
                Token[TokenLength++] = Expression[ReadIndex];
            }
            ReadIndex++;
        }

        Token[TokenLength] = STR_NULL;
        if (TokenLength == 0) {
            continue;
        }

        if (StringCompareNC(Token, TEXT("control")) == 0 || StringCompareNC(Token, TEXT("ctrl")) == 0) {
            Modifiers |= KEYMOD_CONTROL;
            continue;
        }

        if (StringCompareNC(Token, TEXT("shift")) == 0) {
            Modifiers |= KEYMOD_SHIFT;
            continue;
        }

        if (StringCompareNC(Token, TEXT("alt")) == 0) {
            Modifiers |= KEYMOD_ALT;
            continue;
        }

        if (VirtualKey != VK_NONE) {
            return FALSE;
        }

        VirtualKey = HotkeyTokenToVirtualKey(Token);
        if (VirtualKey == VK_NONE) {
            return FALSE;
        }
    }

    if (VirtualKey == VK_NONE) {
        return FALSE;
    }

    Binding->Modifiers = Modifiers;
    Binding->VirtualKey = VirtualKey;

    return TRUE;
}

/************************************************************************/

static HOTKEY_ACTION_HANDLER HotkeyResolveAction(LPCSTR ActionName) {
    UINT Index;
    HOTKEY_ACTION Actions[] = {
        {TEXT("kill_process"), HotkeyActionKillProcess},
        {TEXT("pause_process"), HotkeyActionPauseProcess},
        {TEXT("switch_to_console"), HotkeyActionSwitchToConsole},
        {TEXT("toggle_window_pipeline_trace"), HotkeyActionToggleWindowPipelineTrace},
        {TEXT("toggle_slow_redraw"), HotkeyActionToggleWindowPipelineTrace},
        {NULL, NULL}
    };

    if (ActionName == NULL || ActionName[0] == STR_NULL) {
        return NULL;
    }

    for (Index = 0; Actions[Index].Name != NULL; Index++) {
        if (StringCompareNC(Actions[Index].Name, ActionName) == 0) {
            return Actions[Index].Handler;
        }
    }

    return NULL;
}

/************************************************************************/

static BOOL HotkeyHandleEntry(
    U8 VirtualKey,
    U32 Modifiers,
    LPCSTR KeyExpression,
    LPCSTR ActionName,
    LPPROCESS Process,
    BOOL LogErrors) {
    HOTKEY_BINDING Binding;
    HOTKEY_ACTION_HANDLER Handler = NULL;

    if (Process == NULL || KeyExpression == NULL || ActionName == NULL) {
        return FALSE;
    }

    if (HotkeyParseExpression(KeyExpression, &Binding) == FALSE) {
        if (LogErrors) {
            WARNING(TEXT("Invalid key expression: %s"), KeyExpression);
        }
        return FALSE;
    }

    if (Binding.VirtualKey != VirtualKey || Binding.Modifiers != Modifiers) {
        return FALSE;
    }

    Handler = HotkeyResolveAction(ActionName);
    if (Handler == NULL) {
        if (LogErrors) {
            WARNING(TEXT("Unknown action: %s"), ActionName);
        }
        return FALSE;
    }

    return Handler(Process);
}

/************************************************************************/

BOOL HotkeyHandleKeyDown(U8 VirtualKey, U32 Modifiers, BOOL Repeat) {
    LPTOML Configuration = NULL;
    LPPROCESS Process = NULL;
    U32 ConfigIndex = 0;
    BOOL HasConfiguredHotkey = FALSE;

    if (VirtualKey == VK_NONE) {
        return FALSE;
    }

    if (Repeat) {
        return FALSE;
    }

    Configuration = GetConfiguration();
    if (Configuration == NULL) {
        return FALSE;
    }

    Process = GetFocusedProcess();
    if (Process == NULL) {
        return FALSE;
    }

    if (HotkeyHandleEntry(
            VirtualKey, Modifiers, TEXT("control+shift+f12"), TEXT("toggle_window_pipeline_trace"), Process, FALSE) == TRUE) {
        return TRUE;
    }

    FOREVER {
        STR KeyPath[MAX_FILE_NAME];
        STR ActionPath[MAX_FILE_NAME];
        LPCSTR KeyExpression = NULL;
        LPCSTR ActionName = NULL;

        StringPrintFormat(KeyPath, TEXT("Hotkey.%u.Key"), ConfigIndex);
        KeyExpression = TomlGet(Configuration, KeyPath);
        if (KeyExpression == NULL) {
            break;
        }
        HasConfiguredHotkey = TRUE;

        StringPrintFormat(ActionPath, TEXT("Hotkey.%u.Action"), ConfigIndex);
        ActionName = TomlGet(Configuration, ActionPath);
        if (ActionName == NULL) {
            ConfigIndex++;
            continue;
        }

        if (HotkeyHandleEntry(VirtualKey, Modifiers, KeyExpression, ActionName, Process, TRUE) == TRUE) {
            return TRUE;
        }

        ConfigIndex++;
    }

    if (HasConfiguredHotkey == FALSE) {
        return HotkeyHandleEntry(VirtualKey, Modifiers, TEXT("control+c"), TEXT("kill_process"), Process, FALSE);
    }

    return FALSE;
}
