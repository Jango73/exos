
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


    String Array

\************************************************************************/
#ifndef STRINGARRAY_H_INCLUDED
#define STRINGARRAY_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "utils/Allocator.h"

/***************************************************************************/

typedef struct tag_STRINGARRAY {
    ALLOCATOR Allocator;
    U32 Capacity;
    U32 Count;
    LPSTR *Items;
} STRING_ARRAY, *LPSTRINGARRAY;

/***************************************************************************/

BOOL StringArrayInit(LPSTRINGARRAY Array, U32 Capacity);
BOOL StringArrayInitA(LPSTRINGARRAY Array, U32 Capacity, LPCALLOCATOR Allocator);
void StringArrayDeinit(LPSTRINGARRAY Array);
BOOL StringArrayAddUnique(LPSTRINGARRAY Array, LPCSTR String);
BOOL StringArrayMoveToEnd(LPSTRINGARRAY Array, LPCSTR String);
LPCSTR StringArrayGet(LPSTRINGARRAY Array, U32 Index);

/***************************************************************************/

#endif
