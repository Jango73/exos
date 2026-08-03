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


    Window dockable class helpers

\************************************************************************/

#include "ui/WindowDockable.h"
#include "exos-runtime-main.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>

/************************************************************************/

#define WINDOW_DOCKABLE_PROP_STATE TEXT("windowdockable.state")

/************************************************************************/

static void WindowDockableReadSizeRequestFromProperties(HANDLE Window, LPDOCK_SIZE_REQUEST Request) {
    if (Request == NULL) return;

    Request->Policy = GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_POLICY);
    Request->PreferredPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_PREFERRED);
    Request->MinimumPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MINIMUM);
    Request->MaximumPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MAXIMUM);
    Request->Weight = GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_WEIGHT);

    if (Request->Policy != DOCK_LAYOUT_POLICY_AUTO &&
        Request->Policy != DOCK_LAYOUT_POLICY_FIXED &&
        Request->Policy != DOCK_LAYOUT_POLICY_WEIGHTED) {
        Request->Policy = DOCK_LAYOUT_POLICY_AUTO;
    }

    if (Request->Weight == 0) Request->Weight = 1;
}

/************************************************************************/

static U32 WindowDockableMeasure(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT HostRect, LPDOCK_SIZE_REQUEST Request) {
    UNUSED(Host);
    UNUSED(HostRect);

    if (Dockable == NULL || Request == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    *Request = Dockable->SizeRequest;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

static U32 WindowDockableApplyRect(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT AssignedRect, LPRECT WorkRect) {
    HANDLE Window;
    RECT Rect;

    UNUSED(Host);
    UNUSED(WorkRect);

    if (Dockable == NULL || AssignedRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Window = (HANDLE)Dockable->Context;
    if (Window == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Rect = *AssignedRect;
    if (MoveWindow(Window, &Rect) == FALSE) return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
    (void)InvalidateClientRect(Window, NULL);
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

static void WindowDockableApplyProperties(HANDLE Window) {
    LPWINDOW_DOCKABLE_CLASS_DATA Data;
    HANDLE HostWindow;
    DOCK_SIZE_REQUEST Request;
    U32 Enabled;
    U32 Edge;
    U32 Status;

    Data = WindowDockableClassGetData(Window);
    if (Data == NULL || Data->DockableInitialized == FALSE) {
        debug("[WindowDockableApplyProperties] not initialized window=%x data=%x",
            (UINT)(LINEAR)Window,
            (UINT)(LINEAR)Data);
        return;
    }

    HostWindow = GetWindowParent(Window);
    if (HostWindow == NULL) {
        debug("[WindowDockableApplyProperties] no host parent window=%x", (UINT)(LINEAR)Window);
        return;
    }

    Enabled = GetWindowProp(Window, WINDOW_DOCK_PROP_ENABLED);
    Edge = GetWindowProp(Window, WINDOW_DOCK_PROP_EDGE);
    debug("[WindowDockableApplyProperties] window=%x host=%x enabled=%u edge=%u attached=%u",
        (UINT)(LINEAR)Window,
        (UINT)(LINEAR)HostWindow,
        Enabled,
        Edge,
        Data->DockableAttached);

    if (Enabled == 0 || (Edge != DOCK_EDGE_TOP && Edge != DOCK_EDGE_BOTTOM && Edge != DOCK_EDGE_LEFT && Edge != DOCK_EDGE_RIGHT)) {
        (void)ClearWindowStyle(Window, EWS_EXCLUDE_SIBLING_PLACEMENT);
        if (Data->DockableAttached != FALSE) {
            (void)WindowDockHostDetachDockable(HostWindow, &(Data->Dockable));
            Data->DockableAttached = FALSE;
            (void)WindowDockHostRelayout(HostWindow);
        }
        debug("[WindowDockableApplyProperties] disabled window=%x enabled=%u edge=%u",
            (UINT)(LINEAR)Window,
            Enabled,
            Edge);
        return;
    }

    (void)SetWindowStyle(Window, EWS_EXCLUDE_SIBLING_PLACEMENT);
    (void)DockableSetEdge(&(Data->Dockable), Edge);
    (void)DockableSetOrder(
        &(Data->Dockable),
        (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_PRIORITY),
        (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_ORDER));

    WindowDockableReadSizeRequestFromProperties(Window, &Request);
    debug("[WindowDockableApplyProperties] size window=%x policy=%u pref=%d min=%d max=%d weight=%u",
        (UINT)(LINEAR)Window,
        Request.Policy,
        Request.PreferredPrimarySize,
        Request.MinimumPrimarySize,
        Request.MaximumPrimarySize,
        Request.Weight);
    (void)DockableSetSizeRequest(&(Data->Dockable), &Request);

    (void)WindowDockHostHandleWindowRectChanged(HostWindow);

    if (Data->DockableAttached == FALSE) {
        Status = WindowDockHostAttachDockable(HostWindow, &(Data->Dockable));
        debug("[WindowDockableApplyProperties] attach window=%x host=%x status=%u",
            (UINT)(LINEAR)Window,
            (UINT)(LINEAR)HostWindow,
            Status);
        if (Status == DOCK_LAYOUT_STATUS_SUCCESS || Status == DOCK_LAYOUT_STATUS_ALREADY_ATTACHED) {
            Data->DockableAttached = TRUE;
        } else {
            Data->DockableAttached = FALSE;
            return;
        }
    } else {
        (void)WindowDockHostMarkDirty(HostWindow, DOCK_DIRTY_REASON_DOCKABLE_PROPERTY_CHANGED);
    }

    (void)WindowDockHostRelayout(HostWindow);
}

/************************************************************************/

BOOL WindowDockableClassEnsureRegistered(void) {
    BOOL Result;

    if (FindWindowClass(WINDOW_DOCKABLE_CLASS_NAME) != NULL) {
        debug("[WindowDockableClassEnsureRegistered] already registered");
        return TRUE;
    }
    Result = RegisterWindowClass(WINDOW_DOCKABLE_CLASS_NAME, 0, NULL, WindowDockableWindowFunc, 0) != NULL;
    debug("[WindowDockableClassEnsureRegistered] result=%u", Result);
    return Result;
}

/************************************************************************/

BOOL WindowDockableClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction) {
    HANDLE DockableClass;

    debug("[WindowDockableClassEnsureDerivedRegistered] class=%s", ClassName != NULL ? ClassName : TEXT("(null)"));
    if (ClassName == NULL || WindowFunction == NULL) return FALSE;
    if (WindowDockableClassEnsureRegistered() == FALSE) return FALSE;
    if (FindWindowClass(ClassName) != NULL) {
        debug("[WindowDockableClassEnsureDerivedRegistered] already registered class=%s", ClassName);
        return TRUE;
    }

    DockableClass = FindWindowClass(WINDOW_DOCKABLE_CLASS_NAME);
    if (DockableClass == NULL) return FALSE;

    BOOL Result = RegisterWindowClass(ClassName, DockableClass, NULL, WindowFunction, 0) != NULL;
    debug("[WindowDockableClassEnsureDerivedRegistered] result=%u class=%s base=%x",
        Result,
        ClassName,
        (UINT)(LINEAR)DockableClass);
    return Result;
}

/************************************************************************/

BOOL WindowDockableWindowInheritsDockableClass(HANDLE Window) {
    return WindowInheritsClass(Window, 0, WINDOW_DOCKABLE_CLASS_NAME);
}

/************************************************************************/

LPWINDOW_DOCKABLE_CLASS_DATA WindowDockableClassGetData(HANDLE Window) {
    if (WindowDockableWindowInheritsDockableClass(Window) == FALSE) return NULL;
    return (LPWINDOW_DOCKABLE_CLASS_DATA)(LPVOID)(LINEAR)GetWindowProp(Window, WINDOW_DOCKABLE_PROP_STATE);
}

U32 WindowDockableWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW_DOCKABLE_CLASS_DATA Data;
    DOCKABLE_CALLBACKS Callbacks;
    HANDLE HostWindow;

    debug("[WindowDockableWindowFunc] window=%x message=%x param1=%x param2=%x",
        (UINT)(LINEAR)Window,
        Message,
        Param1,
        Param2);

    if (WindowDockableWindowInheritsDockableClass(Window) == FALSE) {
        debug("[WindowDockableWindowFunc] window does not inherit dockable window=%x", (UINT)(LINEAR)Window);
        return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    switch (Message) {
        case EWM_CREATE:
            Data = WindowDockableClassGetData(Window);
            if (Data == NULL) {
                Data = (LPWINDOW_DOCKABLE_CLASS_DATA)malloc(sizeof(WINDOW_DOCKABLE_CLASS_DATA));
                if (Data == NULL) {
                    debug("[WindowDockableWindowFunc] allocation failed window=%x", (UINT)(LINEAR)Window);
                    return 0;
                }

                memset(Data, 0, sizeof(WINDOW_DOCKABLE_CLASS_DATA));
                (void)SetWindowProp(Window, WINDOW_DOCKABLE_PROP_STATE, (UINT)(LINEAR)Data);
                debug("[WindowDockableWindowFunc] allocated data window=%x data=%x",
                    (UINT)(LINEAR)Window,
                    (UINT)(LINEAR)Data);
            }

            if (Data->DockableInitialized == FALSE) {
                StringCopy(Data->Identifier, TEXT("dock-"));
                U32ToHexString((U32)(LINEAR)Window, Data->Identifier + 5);

                if (DockableInit(&(Data->Dockable), Data->Identifier, (LPVOID)Window) == FALSE) {
                    debug("[WindowDockableWindowFunc] dockable init failed window=%x", (UINT)(LINEAR)Window);
                    return 0;
                }

                Callbacks.Measure = WindowDockableMeasure;
                Callbacks.ApplyRect = WindowDockableApplyRect;
                Callbacks.OnDockChanged = NULL;
                Callbacks.OnHostWorkRectChanged = NULL;
                (void)DockableSetCallbacks(&(Data->Dockable), &Callbacks);

                Data->DockableInitialized = TRUE;
                debug("[WindowDockableWindowFunc] initialized window=%x identifier=%s",
                    (UINT)(LINEAR)Window,
                    Data->Identifier);
            }

            WindowDockableApplyProperties(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_PROPERTY_CHANGED) {
                WindowDockableApplyProperties(Window);
                return 1;
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DELETE:
            Data = WindowDockableClassGetData(Window);
            if (Data != NULL && Data->DockableAttached != FALSE) {
                HostWindow = GetWindowParent(Window);
                if (HostWindow != NULL) {
                    (void)WindowDockHostDetachDockable(HostWindow, &(Data->Dockable));
                    (void)WindowDockHostRelayout(HostWindow);
                }
                Data->DockableAttached = FALSE;
            }

            if (Data != NULL) {
                (void)SetWindowProp(Window, WINDOW_DOCKABLE_PROP_STATE, 0);
                free(Data);
            }

            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
