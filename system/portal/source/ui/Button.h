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


    Desktop button component

\************************************************************************/

#ifndef DESKTOP_COMPONENTS_BUTTON_H_INCLUDED
#define DESKTOP_COMPONENTS_BUTTON_H_INCLUDED

/***************************************************************************/

#include "exos.h"

/***************************************************************************/

#define DESKTOP_BUTTON_WINDOW_CLASS_NAME TEXT("DesktopButtonWindowClass")
#define DESKTOP_BUTTON_PROP_DISABLED TEXT("ui.button.disabled")
#define DESKTOP_BUTTON_PROP_NOTIFY_VALUE TEXT("ui.button.notify_value")

/***************************************************************************/

BOOL ButtonEnsureClassRegistered(void);
HANDLE ButtonCreate(HANDLE ParentWindow, U32 WindowID, LPRECT WindowRect, LPCSTR Caption);
U32 ButtonWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/***************************************************************************/

#endif
