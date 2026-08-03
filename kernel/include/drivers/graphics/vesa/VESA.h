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


    VESA shared primitive declarations

\************************************************************************/

#ifndef VESA_H_INCLUDED
#define VESA_H_INCLUDED

/************************************************************************/

#include "GFX.h"

/************************************************************************/

typedef struct tag_VESA_CONTEXT VESA_CONTEXT, *LPVESA_CONTEXT;

/************************************************************************/

COLOR SetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color);
COLOR SetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color);
COLOR SetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y, COLOR Color);
COLOR GetPixel8(LPVESA_CONTEXT Context, I32 X, I32 Y);
COLOR GetPixel16(LPVESA_CONTEXT Context, I32 X, I32 Y);
COLOR GetPixel24(LPVESA_CONTEXT Context, I32 X, I32 Y);
U32 Line8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 Line16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 Line24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 Rect8(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 Rect16(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 Rect24(LPVESA_CONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2);
U32 VESAArcPrimitive(LPVESA_CONTEXT Context, LPARC_INFO Info);
U32 VESATrianglePrimitive(LPVESA_CONTEXT Context, LPTRIANGLE_INFO Info);

/************************************************************************/

#endif  // VESA_H_INCLUDED
