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


    Window docking host class helpers

\************************************************************************/

#include "ui/WindowDockHost.h"
#include "exos-runtime-main.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

/************************************************************************/

#define WINDOW_DOCK_HOST_PROP_STATE TEXT("windowdockhost.state")

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostAllocateData(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = (LPWINDOW_DOCK_HOST_CLASS_DATA)malloc(sizeof(WINDOW_DOCK_HOST_CLASS_DATA));
    if (Data == NULL) {
        debug("[WindowDockHostAllocateData] allocation failed window=%x", (UINT)(LINEAR)Window);
        return NULL;
    }

    memset(Data, 0, sizeof(WINDOW_DOCK_HOST_CLASS_DATA));
    (void)SetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE, (UINT)(LINEAR)Data);
    debug("[WindowDockHostAllocateData] window=%x data=%x", (UINT)(LINEAR)Window, (UINT)(LINEAR)Data);
    return Data;
}

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostEnsureData(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data != NULL) return Data;

    if (WindowDockHostWindowInheritsDockHostClass(Window) == FALSE) return NULL;
    return WindowDockHostAllocateData(Window);
}

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostEnsureState(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;

    Data = WindowDockHostEnsureData(Window);
    if (Data == NULL) {
        debug("[WindowDockHostEnsureState] no data window=%x", (UINT)(LINEAR)Window);
        return NULL;
    }
    if (Data->DockHostInitialized != FALSE) return Data;

    if (DockHostInit(&(Data->DockHost), TEXT("WindowDockHost"), (LPVOID)Window) == FALSE) {
        debug("[WindowDockHostEnsureState] dock host init failed window=%x", (UINT)(LINEAR)Window);
        return NULL;
    }
    if (GetWindowRect(Window, &HostRect) != FALSE) {
        debug("[WindowDockHostEnsureState] initial rect window=%x rect=%d,%d,%d,%d",
            (UINT)(LINEAR)Window,
            HostRect.X1,
            HostRect.Y1,
            HostRect.X2,
            HostRect.Y2);
        (void)DockHostSetHostRect(&(Data->DockHost), &HostRect);
    }

    Data->DockHostInitialized = TRUE;
    return Data;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureRegistered(void) {
    BOOL Result;

    if (FindWindowClass(WINDOW_DOCK_HOST_CLASS_NAME) != NULL) {
        debug("[WindowDockHostClassEnsureRegistered] already registered");
        return TRUE;
    }
    Result = RegisterWindowClass(WINDOW_DOCK_HOST_CLASS_NAME, 0, NULL, WindowDockHostWindowFunc, 0) != NULL;
    debug("[WindowDockHostClassEnsureRegistered] result=%u", Result);
    return Result;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction) {
    HANDLE DockHostClass;

    debug("[WindowDockHostClassEnsureDerivedRegistered] class=%s", ClassName != NULL ? ClassName : TEXT("(null)"));
    if (ClassName == NULL || WindowFunction == NULL) return FALSE;
    if (WindowDockHostClassEnsureRegistered() == FALSE) return FALSE;
    if (FindWindowClass(ClassName) != NULL) {
        debug("[WindowDockHostClassEnsureDerivedRegistered] already registered class=%s", ClassName);
        return TRUE;
    }

    DockHostClass = FindWindowClass(WINDOW_DOCK_HOST_CLASS_NAME);
    if (DockHostClass == NULL) return FALSE;

    BOOL Result = RegisterWindowClass(ClassName, DockHostClass, NULL, WindowFunction, 0) != NULL;
    debug("[WindowDockHostClassEnsureDerivedRegistered] result=%u class=%s base=%x",
        Result,
        ClassName,
        (UINT)(LINEAR)DockHostClass);
    return Result;
}

/************************************************************************/

BOOL WindowDockHostWindowInheritsDockHostClass(HANDLE Window) {
    return WindowInheritsClass(Window, 0, WINDOW_DOCK_HOST_CLASS_NAME);
}

/************************************************************************/

LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostClassGetData(HANDLE Window) {
    if (WindowDockHostWindowInheritsDockHostClass(Window) == FALSE) return NULL;
    return (LPWINDOW_DOCK_HOST_CLASS_DATA)(LPVOID)(LINEAR)GetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE);
}

/************************************************************************/

void WindowDockHostShutdownWindow(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL) return;

    if (Data->DockHostInitialized != FALSE) {
        (void)DockHostReset(&(Data->DockHost));
        Data->DockHostInitialized = FALSE;
    }

    (void)SetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE, 0);
    free(Data);
}

/************************************************************************/

U32 WindowDockHostAttachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) {
        debug("[WindowDockHostAttachDockable] invalid dockable window=%x", (UINT)(LINEAR)Window);
        return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    }

    Data = WindowDockHostEnsureState(Window);
    if (Data == NULL) {
        debug("[WindowDockHostAttachDockable] no state window=%x", (UINT)(LINEAR)Window);
        return DOCK_LAYOUT_STATUS_NOT_ATTACHED;
    }

    U32 Status = DockHostAttachDockable(&(Data->DockHost), Dockable);
    debug("[WindowDockHostAttachDockable] window=%x dockable_context=%x status=%u item_count=%u",
        (UINT)(LINEAR)Window,
        (UINT)(LINEAR)Dockable->Context,
        Status,
        Data->DockHost.ItemCount);
    return Status;
}

/************************************************************************/

U32 WindowDockHostDetachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostDetachDockable(&(Data->DockHost), Dockable);
}

/************************************************************************/

U32 WindowDockHostMarkDirty(HANDLE Window, U32 Reason) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostMarkDirty(&(Data->DockHost), Reason);
}

/************************************************************************/

U32 WindowDockHostHandleWindowRectChanged(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;
    U32 Status;

    Data = WindowDockHostEnsureState(Window);
    if (Data == NULL) {
        debug("[WindowDockHostHandleWindowRectChanged] no state window=%x", (UINT)(LINEAR)Window);
        return DOCK_LAYOUT_STATUS_NOT_ATTACHED;
    }

    if (GetWindowRect(Window, &HostRect) == FALSE) {
        debug("[WindowDockHostHandleWindowRectChanged] rect failed window=%x", (UINT)(LINEAR)Window);
        return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;
    }

    debug("[WindowDockHostHandleWindowRectChanged] window=%x rect=%d,%d,%d,%d",
        (UINT)(LINEAR)Window,
        HostRect.X1,
        HostRect.Y1,
        HostRect.X2,
        HostRect.Y2);
    Status = DockHostSetHostRect(&(Data->DockHost), &HostRect);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    return WindowDockHostRelayout(Window);
}

/************************************************************************/

U32 WindowDockHostRelayout(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    DOCK_LAYOUT_FRAME Frame;
    DOCK_LAYOUT_RESULT Result;
    HANDLE DockableWindow;
    U32 DockableStyle;
    BOOL ReservedPlacementStates[DOCK_HOST_MAX_ITEMS];
    UINT Index;
    U32 Status;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) {
        debug("[WindowDockHostRelayout] not attached window=%x data=%x",
            (UINT)(LINEAR)Window,
            (UINT)(LINEAR)Data);
        return DOCK_LAYOUT_STATUS_NOT_ATTACHED;
    }

    Status = DockHostBuildLayoutFrame(&(Data->DockHost), &Frame);
    debug("[WindowDockHostRelayout] build window=%x status=%u frame_status=%u items=%u assignments=%u host=%d,%d,%d,%d work=%d,%d,%d,%d",
        (UINT)(LINEAR)Window,
        Status,
        Frame.Status,
        Data->DockHost.ItemCount,
        Frame.AssignmentCount,
        Frame.HostRect.X1,
        Frame.HostRect.Y1,
        Frame.HostRect.X2,
        Frame.HostRect.Y2,
        Frame.WorkRect.X1,
        Frame.WorkRect.Y1,
        Frame.WorkRect.X2,
        Frame.WorkRect.Y2);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame.Status == DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame.Status = Status;
    }

    for (Index = 0; Index < Data->DockHost.ItemCount && Index < DOCK_HOST_MAX_ITEMS; Index++) {
        ReservedPlacementStates[Index] = FALSE;

        if (Data->DockHost.Items[Index] == NULL) continue;
        DockableWindow = (HANDLE)Data->DockHost.Items[Index]->Context;
        if (DockableWindow == NULL) continue;
        if (GetWindowStyle(DockableWindow, &DockableStyle) == FALSE) continue;

        ReservedPlacementStates[Index] = ((DockableStyle & EWS_EXCLUDE_SIBLING_PLACEMENT) != 0);
        if (ReservedPlacementStates[Index] != FALSE) {
            (void)ClearWindowStyle(DockableWindow, EWS_EXCLUDE_SIBLING_PLACEMENT);
        }
    }

    Status = DockHostApplyLayoutFrame(&(Data->DockHost), &Frame, &Result);
    debug("[WindowDockHostRelayout] apply window=%x status=%u result_status=%u applied=%u rejected=%u work=%d,%d,%d,%d",
        (UINT)(LINEAR)Window,
        Status,
        Result.Status,
        Result.AppliedCount,
        Result.RejectedCount,
        Result.WorkRect.X1,
        Result.WorkRect.Y1,
        Result.WorkRect.X2,
        Result.WorkRect.Y2);

    for (Index = 0; Index < Data->DockHost.ItemCount && Index < DOCK_HOST_MAX_ITEMS; Index++) {
        if (ReservedPlacementStates[Index] == FALSE) continue;
        if (Data->DockHost.Items[Index] == NULL) continue;

        DockableWindow = (HANDLE)Data->DockHost.Items[Index]->Context;
        if (DockableWindow == NULL) continue;
        (void)SetWindowStyle(DockableWindow, EWS_EXCLUDE_SIBLING_PLACEMENT);
    }

    return Status;
}

/************************************************************************/

U32 WindowDockHostWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    debug("[WindowDockHostWindowFunc] window=%x message=%x param1=%x param2=%x",
        (UINT)(LINEAR)Window,
        Message,
        Param1,
        Param2);

    switch (Message) {
        case EWM_CREATE:
            if (WindowDockHostEnsureData(Window) == NULL) return 0;
            (void)WindowDockHostHandleWindowRectChanged(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                (void)WindowDockHostHandleWindowRectChanged(Window);
                return 1;
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DELETE:
            WindowDockHostShutdownWindow(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
