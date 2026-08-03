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

#ifndef WINDOW_DOCK_HOST_H_INCLUDED
#define WINDOW_DOCK_HOST_H_INCLUDED

/************************************************************************/

#include "exos.h"
#include "ui/DockHost.h"

typedef struct tag_WINDOW_DOCK_HOST_CLASS_DATA {
    DOCK_HOST DockHost;
    BOOL DockHostInitialized;
} WINDOW_DOCK_HOST_CLASS_DATA, *LPWINDOW_DOCK_HOST_CLASS_DATA;

/************************************************************************/

BOOL WindowDockHostClassEnsureRegistered(void);
BOOL WindowDockHostClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction);
BOOL WindowDockHostWindowInheritsDockHostClass(HANDLE Window);
LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostClassGetData(HANDLE Window);
void WindowDockHostShutdownWindow(HANDLE Window);
U32 WindowDockHostAttachDockable(HANDLE Window, LPDOCKABLE Dockable);
U32 WindowDockHostDetachDockable(HANDLE Window, LPDOCKABLE Dockable);
U32 WindowDockHostMarkDirty(HANDLE Window, U32 Reason);
U32 WindowDockHostHandleWindowRectChanged(HANDLE Window);
U32 WindowDockHostRelayout(HANDLE Window);
U32 WindowDockHostWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/************************************************************************/

#endif
