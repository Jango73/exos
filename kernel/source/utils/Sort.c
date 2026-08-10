
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

#include "utils/Sort.h"

#include "text/CoreString.h"
#include "utils/Allocator.h"

/************************************************************************/

/**
 * @brief Sort one array in place with a caller-provided comparator.
 *
 * Insertion sort keeps the ordering stable: equivalent elements keep their
 * relative order, which lets callers build multi-level sorts by running the
 * sort once per key in reverse priority order.
 *
 * @param Array Array of elements to sort in place.
 * @param ElementCount Number of elements in the array.
 * @param ElementSize Size of one element in bytes.
 * @param Comparator Comparator deciding element order.
 */
void SortArray(LPVOID Array, U32 ElementCount, U32 ElementSize, SORT_COMPARATOR Comparator) {
    U8* Elements;
    U8* Temp;
    U32 Index;
    U32 Position;
    ALLOCATOR Allocator;

    if (Array == NULL || ElementCount < 2 || ElementSize == 0 || Comparator == NULL) {
        return;
    }

    AllocatorInitKernel(&Allocator);
    Temp = (U8*)AllocatorAlloc(&Allocator, ElementSize);
    if (Temp == NULL) {
        return;
    }

    Elements = (U8*)Array;

    for (Index = 1; Index < ElementCount; Index++) {
        MemoryCopy(Temp, Elements + (Index * ElementSize), ElementSize);
        Position = Index;
        while (Position > 0 && Comparator(Temp, Elements + ((Position - 1) * ElementSize)) < 0) {
            MemoryCopy(Elements + (Position * ElementSize), Elements + ((Position - 1) * ElementSize), ElementSize);
            Position--;
        }
        MemoryCopy(Elements + (Position * ElementSize), Temp, ElementSize);
    }

    AllocatorFree(&Allocator, Temp);
}

/************************************************************************/
