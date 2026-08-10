
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


    Sort

\************************************************************************/
#ifndef SORT_H_INCLUDED
#define SORT_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/
// Comparator returns a negative value when First precedes Second, a positive
// value when Second precedes First, and zero when both are equivalent.

typedef INT (*SORT_COMPARATOR)(LPCVOID First, LPCVOID Second);

/***************************************************************************/

void SortArray(LPVOID Array, U32 ElementCount, U32 ElementSize, SORT_COMPARATOR Comparator);

/***************************************************************************/

#endif
