
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


    System FS

\************************************************************************/

#ifndef SYSTEMFS_H_INCLUDED
#define SYSTEMFS_H_INCLUDED

/***************************************************************************/

#include "../Base.h"
#include "File-System.h"

/***************************************************************************/
// The file object of SystemFS

typedef struct tag_SYSTEMFSFILE SYSTEMFSFILE, *LPSYSTEMFSFILE;

struct tag_SYSTEMFSFILE {
    LISTNODE_FIELDS
    LPLIST Children;
    LPSYSTEMFSFILE ParentNode;
    LPFILESYSTEM Mounted;
    STR MountPath[MAX_PATH_NAME];
    U32 Attributes;
    DATETIME Creation;
    STR Name[MAX_FILE_NAME];
};

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_SYSTEMFSFILESYSTEM {
    FILESYSTEM Header;
    LPSYSTEMFSFILE Root;
} SYSTEMFSFILESYSTEM, *LPSYSTEMFSFILESYSTEM;

/***************************************************************************/
// The file object created when opening a file

typedef struct tag_SYSFSFILE {
    FILE Header;
    LPSYSTEMFSFILE SystemFile;
    LPSYSTEMFSFILE Parent;
    LPFILE MountedFile;
} SYSFSFILE, *LPSYSFSFILE;

/***************************************************************************/

extern DRIVER SystemFSDriver;
BOOL SystemFSMountFileSystem(LPFILESYSTEM FileSystem);
BOOL SystemFSUnmountFileSystem(LPFILESYSTEM FileSystem);

/***************************************************************************/

#endif
