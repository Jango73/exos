
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


    Path utilities

\************************************************************************/

#ifndef PATH_H_INCLUDED
#define PATH_H_INCLUDED

/***************************************************************************/

#include "fs/File-System.h"
#include "utils/Allocator.h"
#include "utils/StringArray.h"

/***************************************************************************/

LPLIST DecomposePath(LPCSTR Path);

/***************************************************************************/

typedef struct tag_PATHCOMPLETION {
    LPFILESYSTEM FileSystem;
    STR Base[MAX_PATH_NAME];
    STRINGARRAY Matches;
    U32 Index;
} PATHCOMPLETION, *LPPATHCOMPLETION;

/***************************************************************************/

BOOL PathCompletionInit(LPPATHCOMPLETION Context, LPFILESYSTEM FileSystem);
BOOL PathCompletionInitA(LPPATHCOMPLETION Context, LPFILESYSTEM FileSystem, LPCALLOCATOR Allocator);
void PathCompletionDeinit(LPPATHCOMPLETION Context);
BOOL PathCompletionNext(LPPATHCOMPLETION Context, LPCSTR Path, LPSTR Output);

/***************************************************************************/

#endif
