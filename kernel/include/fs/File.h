
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


    File

\************************************************************************/
#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

/***************************************************************************/

#include "../Base.h"
#include "File-System.h"
#include "User.h"
#include "core/Driver.h"

/***************************************************************************/

// Functions supplied by a file driver

#define DF_FILE_OPEN (DF_FIRST_FUNCTION + 0)
#define DF_FILE_CLOSE (DF_FIRST_FUNCTION + 1)
#define DF_FILE_READ (DF_FIRST_FUNCTION + 2)
#define DF_FILE_WRITE (DF_FIRST_FUNCTION + 3)
#define DF_FILE_GETPOS (DF_FIRST_FUNCTION + 4)
#define DF_FILE_SETPOS (DF_FIRST_FUNCTION + 5)

/***************************************************************************/

LPFILE OpenFile(LPFILE_OPEN_INFO FileOpenInfo);
UINT CloseFile(LPFILE File);
UINT GetFilePosition(LPFILE File);
UINT SetFilePosition(LPFILE_OPERATION Operation);
UINT ReadFile(LPFILE_OPERATION Operation);
UINT WriteFile(LPFILE_OPERATION Operation);
UINT GetFileSize(LPFILE File);
UINT DeleteFile(LPFILE_OPEN_INFO FileOpenInfo);
UINT CreateFolder(LPFILE_OPEN_INFO FileOpenInfo);
UINT DeleteFolder(LPFILE_OPEN_INFO FileOpenInfo);

LPVOID FileReadAll(LPCSTR, UINT *);
UINT FileWriteAll(LPCSTR, LPCVOID, UINT);

/***************************************************************************/

#endif
