
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


    Mouse common

\************************************************************************/

#ifndef MOUSECOMMON_H_INCLUDED
#define MOUSECOMMON_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "input/Mouse.h"
#include "sync/DeferredWork.h"
#include "sync/Mutex.h"

/************************************************************************/
// Typedefs

typedef struct tag_MOUSE_PACKET_BUFFER {
    I32 DeltaX;
    I32 DeltaY;
    U32 Buttons;
    BOOL Pending;
} MOUSE_PACKET_BUFFER, *LPMOUSE_PACKET_BUFFER;

typedef struct tag_MOUSE_COMMON_CONTEXT {
    BOOL Initialized;
    MUTEX Mutex;
    I32 DeltaX;
    I32 DeltaY;
    U32 Buttons;
    MOUSE_PACKET_BUFFER Packet;
    DEFERRED_WORK_TOKEN DeferredWorkToken;
} MOUSE_COMMON_CONTEXT, *LPMOUSE_COMMON_CONTEXT;

/************************************************************************/
// External symbols

BOOL MouseCommonInitialize(LPMOUSE_COMMON_CONTEXT Context);
void MouseCommonQueuePacket(LPMOUSE_COMMON_CONTEXT Context, I32 DeltaX, I32 DeltaY, U32 Buttons);
U32 MouseCommonGetDeltaX(LPMOUSE_COMMON_CONTEXT Context);
U32 MouseCommonGetDeltaY(LPMOUSE_COMMON_CONTEXT Context);
U32 MouseCommonGetButtons(LPMOUSE_COMMON_CONTEXT Context);

/************************************************************************/

#endif  // MOUSECOMMON_H_INCLUDED
