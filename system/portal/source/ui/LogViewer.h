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


    Desktop log viewer component

\************************************************************************/

#ifndef DESKTOP_COMPONENTS_LOG_VIEWER_H_INCLUDED
#define DESKTOP_COMPONENTS_LOG_VIEWER_H_INCLUDED

/***************************************************************************/

#include "exos.h"

/***************************************************************************/

#define DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME TEXT("DesktopLogViewerWindowClass")
#define DESKTOP_LOG_VIEWER_WINDOW_ID 0x534C4F47

/***************************************************************************/

BOOL LogViewerEnsureClassRegistered(void);
U32 LogViewerWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);

/***************************************************************************/

#endif
