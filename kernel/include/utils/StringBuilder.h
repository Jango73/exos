/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    String builder

\************************************************************************/

#ifndef STRINGBUILDER_H_INCLUDED
#define STRINGBUILDER_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

typedef struct tag_STRINGBUILDER {
    LPSTR Buffer;
    UINT Capacity;
    UINT Length;
    UINT RequiredLength;
    BOOL Overflowed;
} STRINGBUILDER, *LPSTRINGBUILDER;

/***************************************************************************/

BOOL StringBuilderInit(LPSTRINGBUILDER Builder, LPSTR Buffer, UINT Capacity);
void StringBuilderReset(LPSTRINGBUILDER Builder);
BOOL StringBuilderSet(LPSTRINGBUILDER Builder, LPCSTR Text);
BOOL StringBuilderAppend(LPSTRINGBUILDER Builder, LPCSTR Text);
BOOL StringBuilderAppendChar(LPSTRINGBUILDER Builder, STR Character);
BOOL StringBuilderAppendPathSegment(LPSTRINGBUILDER Builder, LPCSTR Segment, STR Separator);
LPCSTR StringBuilderGetText(LPSTRINGBUILDER Builder);
UINT StringBuilderGetLength(LPSTRINGBUILDER Builder);
UINT StringBuilderGetRequiredLength(LPSTRINGBUILDER Builder);
BOOL StringBuilderHasOverflowed(LPSTRINGBUILDER Builder);

/***************************************************************************/

#endif
