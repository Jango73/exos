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


    PackageFS read-only mount interface

\************************************************************************/

#ifndef PACKAGEFS_H_INCLUDED
#define PACKAGEFS_H_INCLUDED

#include "Base.h"
#include "fs/File-System.h"
#include "package/Epk-Parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************/

extern DRIVER PackageFSDriver;

U32 PackageFSMountFromBuffer(
    LPCVOID PackageBytes, U32 PackageSize, LPCSTR VolumeName, const EPK_PARSER_OPTIONS* Options,
    LPFILESYSTEM* MountedFileSystemOut);

BOOL PackageFSUnmount(LPFILESYSTEM FileSystem);

/***************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
