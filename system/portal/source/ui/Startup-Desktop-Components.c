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


    Startup desktop component composition

\************************************************************************/

#include "ui/Startup-Desktop-Components.h"
#include "exos-runtime-main.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

#include "ui/Button.h"
#include "ui/ClockWidget.h"
#include "ui/Cube3D.h"
#include "ui/LogViewer.h"
#include "ui/OnScreenDebugInfo.h"
#include "ui/ShellBar.h"

/***************************************************************************/

#define DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID 0x5344534F
#define DESKTOP_CUBE3D_WINDOW_TOP 300
#define SHELL_BAR_CLOCK_WINDOW_ID 0x5342434C
#define SHELL_BAR_BUTTON_LOG_VIEWER_WINDOW_ID 0x53424C47
#define SHELL_BAR_BUTTON_CUBE3D_WINDOW_ID 0x53424333
#define SHELL_BAR_COMPONENT_ORDER_BUTTON_CUBE3D 3
#define SHELL_BAR_COMPONENT_ORDER_BUTTON_LOG_VIEWER 2
#define SHELL_BAR_COMPONENT_ORDER_CLOCK 1
#define SHELL_BAR_COMPONENT_WIDTH_CLOCK 64
#define SHELL_BAR_COMPONENT_WIDTH_BUTTON 88

/***************************************************************************/

static BOOL MarkShellBarComponentLayout(HANDLE Window, U32 Order, U32 Width) {
    if (Window == NULL) return FALSE;
    if (Order == 0 || Width == 0) return FALSE;

    (void)SetWindowProp(Window, SHELL_BAR_COMPONENT_PROP_ORDER, Order);
    (void)SetWindowProp(Window, SHELL_BAR_COMPONENT_PROP_WIDTH, Width);
    return TRUE;
}

/***************************************************************************/

static BOOL EnsureShellBarClockWidget(HANDLE ShellBarWindow) {
    HANDLE ComponentsSlotWindow;
    HANDLE ClockWindow;
    WINDOW_INFO WindowInfo;

    debug("[EnsureShellBarClockWidget] enter shellbar=%x", (UINT)(LINEAR)ShellBarWindow);

    if (ShellBarWindow == NULL) {
        debug("[EnsureShellBarClockWidget] missing shellbar");
        return FALSE;
    }
    if (DesktopClockWidgetEnsureClassRegistered() == FALSE) {
        debug("[EnsureShellBarClockWidget] class registration failed");
        return FALSE;
    }

    ComponentsSlotWindow = ShellBarGetSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS);
    debug("[EnsureShellBarClockWidget] components_slot=%x", (UINT)(LINEAR)ComponentsSlotWindow);
    if (ComponentsSlotWindow == NULL) return FALSE;

    ClockWindow = FindWindow(ComponentsSlotWindow, SHELL_BAR_CLOCK_WINDOW_ID);
    if (ClockWindow != NULL) {
        debug("[EnsureShellBarClockWidget] existing clock=%x", (UINT)(LINEAR)ClockWindow);
        return MarkShellBarComponentLayout(
            ClockWindow,
            SHELL_BAR_COMPONENT_ORDER_CLOCK,
            SHELL_BAR_COMPONENT_WIDTH_CLOCK);
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ComponentsSlotWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_CLOCK_WIDGET_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = SHELL_BAR_CLOCK_WINDOW_ID;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    ClockWindow = (HANDLE)CreateWindow(&WindowInfo);
    debug("[EnsureShellBarClockWidget] created clock=%x", (UINT)(LINEAR)ClockWindow);
    if (ClockWindow == NULL) return FALSE;

    return MarkShellBarComponentLayout(
        ClockWindow,
        SHELL_BAR_COMPONENT_ORDER_CLOCK,
        SHELL_BAR_COMPONENT_WIDTH_CLOCK);
}

/***************************************************************************/

static BOOL EnsureShellBarButton(
    HANDLE ShellBarWindow,
    U32 ButtonWindowID,
    U32 TargetWindowID,
    U32 Order,
    LPCSTR Caption
) {
    HANDLE ComponentsSlotWindow;
    HANDLE ButtonWindow;
    RECT ButtonRect;

    debug("[EnsureShellBarButton] enter shellbar=%x button_id=%x target_id=%x",
        (UINT)(LINEAR)ShellBarWindow,
        ButtonWindowID,
        TargetWindowID);

    if (ShellBarWindow == NULL || Caption == NULL) {
        debug("[EnsureShellBarButton] invalid parameter");
        return FALSE;
    }
    if (ButtonEnsureClassRegistered() == FALSE) {
        debug("[EnsureShellBarButton] button class registration failed");
        return FALSE;
    }

    ComponentsSlotWindow = ShellBarGetSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS);
    debug("[EnsureShellBarButton] components_slot=%x", (UINT)(LINEAR)ComponentsSlotWindow);
    if (ComponentsSlotWindow == NULL) return FALSE;

    ButtonWindow = FindWindow(ComponentsSlotWindow, ButtonWindowID);
    if (ButtonWindow == NULL) {
        ButtonRect.X1 = 0;
        ButtonRect.Y1 = 0;
        ButtonRect.X2 = SHELL_BAR_COMPONENT_WIDTH_BUTTON - 1;
        ButtonRect.Y2 = 1;
        ButtonWindow = ButtonCreate(ComponentsSlotWindow, ButtonWindowID, &ButtonRect, Caption);
        debug("[EnsureShellBarButton] created button=%x", (UINT)(LINEAR)ButtonWindow);
        if (ButtonWindow == NULL) return FALSE;
    } else {
        debug("[EnsureShellBarButton] existing button=%x", (UINT)(LINEAR)ButtonWindow);
    }

    (void)SetWindowProp(ButtonWindow, DESKTOP_BUTTON_PROP_NOTIFY_VALUE, TargetWindowID);
    return MarkShellBarComponentLayout(ButtonWindow, Order, SHELL_BAR_COMPONENT_WIDTH_BUTTON);
}

/***************************************************************************/

/**
 * @brief Ensure the floating 3D cube window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureCube3DWindow(HANDLE Desktop) {
    HANDLE RootWindow;
    HANDLE CubeWindow;
    WINDOW_INFO WindowInfo;
    POINT PreferredSize;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL) {
        return FALSE;
    }
    if (Cube3DEnsureClassRegistered() == FALSE) {
        return FALSE;
    }

    RootWindow = GetDesktopWindow((HANDLE)Desktop);
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    if (Cube3DGetPreferredSize(&PreferredSize) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = PreferredSize.X;
    WindowHeight = PreferredSize.Y;
    if (WindowWidth > ScreenWidth - 16) WindowWidth = ScreenWidth - 16;
    if (WindowHeight > ScreenHeight - 16) WindowHeight = ScreenHeight - 16;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = 8;
    WindowRect.Y1 = DESKTOP_CUBE3D_WINDOW_TOP;
    WindowRect.X2 = WindowRect.X1 + WindowWidth - 1;
    WindowRect.Y2 = WindowRect.Y1 + WindowHeight - 1;
    if (WindowRect.X2 >= ScreenWidth) WindowRect.X2 = ScreenWidth - 1;
    if (WindowRect.Y2 >= ScreenHeight) WindowRect.Y2 = ScreenHeight - 1;
    if (WindowRect.X2 < WindowRect.X1) WindowRect.X2 = WindowRect.X1;
    if (WindowRect.Y2 < WindowRect.Y1) WindowRect.Y2 = WindowRect.Y1;

    CubeWindow = FindWindow(RootWindow, DESKTOP_CUBE3D_WINDOW_ID);
    if (CubeWindow != NULL) {
        (void)MoveWindow(CubeWindow, &WindowRect);
        (void)SetWindowCaption(CubeWindow, TEXT("3D Cube"));
        (void)HideWindow(CubeWindow);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_CUBE3D_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_SYSTEM_DECORATED;
    WindowInfo.ID = DESKTOP_CUBE3D_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = FALSE;

    CubeWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (CubeWindow == NULL) {
        return FALSE;
    }

    (void)SetWindowCaption(CubeWindow, TEXT("3D Cube"));
    (void)HideWindow(CubeWindow);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure the floating log viewer window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureLogViewerWindow(HANDLE Desktop) {
    HANDLE RootWindow;
    HANDLE LogViewerWindow;
    WINDOW_INFO WindowInfo;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL) {
        return FALSE;
    }
    if (LogViewerEnsureClassRegistered() == FALSE) {
        return FALSE;
    }

    RootWindow = GetDesktopWindow((HANDLE)Desktop);
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = ScreenWidth / 2;
    WindowHeight = (ScreenHeight * 3) / 4;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = ScreenWidth - WindowWidth;
    WindowRect.Y1 = 0;
    WindowRect.X2 = ScreenWidth - 1;
    WindowRect.Y2 = WindowHeight - 1;

    LogViewerWindow = FindWindow(RootWindow, DESKTOP_LOG_VIEWER_WINDOW_ID);
    if (LogViewerWindow != NULL) {
        (void)MoveWindow(LogViewerWindow, &WindowRect);
        (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
        (void)HideWindow(LogViewerWindow);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_SYSTEM_DECORATED;
    WindowInfo.ID = DESKTOP_LOG_VIEWER_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = FALSE;

    LogViewerWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (LogViewerWindow == NULL) {
        return FALSE;
    }

    (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
    (void)HideWindow(LogViewerWindow);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure the on-screen debug information window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureOnScreenDebugInfoWindow(HANDLE Desktop) {
    HANDLE RootWindow;
    HANDLE DebugInfoWindow;
    WINDOW_INFO WindowInfo;
    POINT PreferredSize;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL) {
        return FALSE;
    }

    RootWindow = GetDesktopWindow((HANDLE)Desktop);
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    if (OnScreenDebugInfoGetPreferredSize(&PreferredSize) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = PreferredSize.X;
    WindowHeight = PreferredSize.Y;
    if (WindowWidth > ScreenWidth) WindowWidth = ScreenWidth;
    if (WindowHeight > ScreenHeight) WindowHeight = ScreenHeight;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = 0;
    WindowRect.Y1 = 0;
    WindowRect.X2 = WindowRect.X1 + WindowWidth - 1;
    WindowRect.Y2 = WindowRect.Y1 + WindowHeight - 1;

    DebugInfoWindow = FindWindow(RootWindow, DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID);
    if (DebugInfoWindow != NULL) {
        (void)MoveWindow(DebugInfoWindow, &WindowRect);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = NULL;
    WindowInfo.Function = OnScreenDebugInfoWindowFunc;
    WindowInfo.Style = EWS_VISIBLE | EWS_BARE_SURFACE | EWS_ALWAYS_AT_BOTTOM;
    WindowInfo.ID = DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = TRUE;

    DebugInfoWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (DebugInfoWindow == NULL) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

BOOL StartupDesktopComponentsInitialize(HANDLE Desktop) {
    HANDLE RootWindow;
    HANDLE ShellBarWindow;
    BOOL ShellBarClockResult = TRUE;
    BOOL ShellBarLogViewerButtonResult = TRUE;
    BOOL ShellBarCube3DButtonResult = TRUE;
    BOOL Cube3DResult;
    BOOL LogViewerResult;
    BOOL OnScreenDebugInfoResult;

    debug("[StartupDesktopComponentsInitialize] enter desktop=%x", (UINT)(LINEAR)Desktop);

    if (Desktop == NULL) {
        debug("[StartupDesktopComponentsInitialize] missing desktop");
        return FALSE;
    }

    RootWindow = GetDesktopWindow((HANDLE)Desktop);
    debug("[StartupDesktopComponentsInitialize] root=%x", (UINT)(LINEAR)RootWindow);
    if (RootWindow == NULL) {
        return FALSE;
    }

    ShellBarWindow = ShellBarGetWindow(RootWindow);
    debug("[StartupDesktopComponentsInitialize] existing shellbar=%x", (UINT)(LINEAR)ShellBarWindow);
    if (ShellBarWindow == NULL) {
        if (ShellBarCreate(RootWindow) == FALSE) {
            debug("[StartupDesktopComponentsInitialize] shellbar creation failed");
            // Shell bar injection is best-effort; continue with other startup components.
        }

        ShellBarWindow = ShellBarGetWindow(RootWindow);
        debug("[StartupDesktopComponentsInitialize] shellbar after create=%x", (UINT)(LINEAR)ShellBarWindow);
    }
    if (ShellBarWindow != NULL) {
        ShellBarClockResult = EnsureShellBarClockWidget(ShellBarWindow);
        ShellBarLogViewerButtonResult = EnsureShellBarButton(
            ShellBarWindow,
            SHELL_BAR_BUTTON_LOG_VIEWER_WINDOW_ID,
            DESKTOP_LOG_VIEWER_WINDOW_ID,
            SHELL_BAR_COMPONENT_ORDER_BUTTON_LOG_VIEWER,
            TEXT("LogViewer"));
        ShellBarCube3DButtonResult = EnsureShellBarButton(
            ShellBarWindow,
            SHELL_BAR_BUTTON_CUBE3D_WINDOW_ID,
            DESKTOP_CUBE3D_WINDOW_ID,
            SHELL_BAR_COMPONENT_ORDER_BUTTON_CUBE3D,
            TEXT("Cube3D"));
    }
    Cube3DResult = EnsureCube3DWindow(Desktop);
    LogViewerResult = EnsureLogViewerWindow(Desktop);
    OnScreenDebugInfoResult = EnsureOnScreenDebugInfoWindow(Desktop);
    debug("[StartupDesktopComponentsInitialize] results shell_clock=%u log_button=%u cube_button=%u cube=%u log=%u debug=%u",
        ShellBarClockResult,
        ShellBarLogViewerButtonResult,
        ShellBarCube3DButtonResult,
        Cube3DResult,
        LogViewerResult,
        OnScreenDebugInfoResult);
    return (ShellBarClockResult != FALSE) &&
           (ShellBarLogViewerButtonResult != FALSE) &&
           (ShellBarCube3DButtonResult != FALSE) &&
           (Cube3DResult != FALSE) &&
           (LogViewerResult != FALSE) &&
           (OnScreenDebugInfoResult != FALSE);
}

/***************************************************************************/
