
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


    VGA

\************************************************************************/

#ifndef VGA_H_INCLUDED
#define VGA_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

typedef struct tag_VGAMODEREGS {
    U8 Regs[64];
} VGA_MODE_REGS, *LPVGAMODEREGS;

/***************************************************************************/

typedef struct tag_VGAMODEINFO {
    U32 Columns;
    U32 Rows;
    U32 CharHeight;
} VGA_MODE_INFO, *LPVGAMODEINFO;

/***************************************************************************/

extern VGA_MODE_REGS VGAModeRegs[];
extern const U32 VGAModeRegsCount;

/***************************************************************************/

LPDRIVER VGAGetDriver(void);
U32 VGAGetModeCount(void);
BOOL VGAGetModeInfo(U32 ModeIndex, LPVGAMODEINFO Info);
BOOL VGAFindTextMode(U32 Columns, U32 Rows, U32* ModeIndex);
BOOL VGASetMode(U32 ModeIndex);

/************************************************************************/

#pragma pack(pop)

#endif  // VGA_H_INCLUDED
