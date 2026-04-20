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

#ifndef WINDOW_DOCKABLE_H_INCLUDED
#define WINDOW_DOCKABLE_H_INCLUDED

/************************************************************************/

#include "exos.h"
#include "ui/WindowDockHost.h"

typedef struct tag_WINDOW_DOCKABLE_CLASS_DATA {
    DOCKABLE Dockable;
    BOOL DockableInitialized;
    BOOL DockableAttached;
    STR Identifier[32];
} WINDOW_DOCKABLE_CLASS_DATA, *LPWINDOW_DOCKABLE_CLASS_DATA;

/************************************************************************/

BOOL WindowDockableClassEnsureRegistered(void);
BOOL WindowDockableClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction);
BOOL WindowDockableWindowInheritsDockableClass(HANDLE Window);
LPWINDOW_DOCKABLE_CLASS_DATA WindowDockableClassGetData(HANDLE Window);
U32 WindowDockableWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/************************************************************************/

#endif
