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


    Desktop shell bar component

\************************************************************************/

#ifndef SHELL_BAR_H_INCLUDED
#define SHELL_BAR_H_INCLUDED

/************************************************************************/

#include "exos.h"

/************************************************************************/

#define SHELL_BAR_WINDOW_CLASS_NAME TEXT("ShellBarWindowClass")
#define SHELL_BAR_SLOT_COMPONENTS_WINDOW_ID 0x53424350
#define SHELL_BAR_COMPONENT_PROP_ORDER TEXT("shellbar.component_order")
#define SHELL_BAR_COMPONENT_PROP_WIDTH TEXT("shellbar.component_width")

/************************************************************************/

typedef enum tag_SHELL_BAR_SLOT_ID {
    SHELL_BAR_SLOT_LEFT = 1,
    SHELL_BAR_SLOT_CENTER = 2,
    SHELL_BAR_SLOT_COMPONENTS = 3
} SHELL_BAR_SLOT_ID, *LPSHELL_BAR_SLOT_ID;

/************************************************************************/

BOOL ShellBarEnsureClassRegistered(void);
BOOL ShellBarCreate(HANDLE ParentWindow);
HANDLE ShellBarGetWindow(HANDLE ParentWindow);
HANDLE ShellBarGetSlotWindow(HANDLE ShellBarWindow, U32 SlotID);
U32 ShellBarWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/************************************************************************/

#endif
