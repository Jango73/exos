
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


    Mouse dispatcher

\************************************************************************/

#ifndef MOUSEDISPATCHER_H_INCLUDED
#define MOUSEDISPATCHER_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

BOOL InitializeMouseDispatcher(void);
void MouseDispatcherOnInput(I32 DeltaX, I32 DeltaY, U32 Buttons);
BOOL GetMouseScreenPosition(I32* X, I32* Y);
BOOL SetMouseSerpentineMode(BOOL Enabled);

/************************************************************************/

#pragma pack(pop)

#endif
