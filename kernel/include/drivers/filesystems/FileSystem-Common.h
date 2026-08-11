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


    File System Common

\************************************************************************/

#ifndef DRIVERS_FILESYSTEMS_FILESYSTEM_COMMON_H_INCLUDED
#define DRIVERS_FILESYSTEMS_FILESYSTEM_COMMON_H_INCLUDED

/************************************************************************/

#include "fs/Storage.h"

/************************************************************************/
// External functions

BOOL PartitionTransferSectors(LPSTORAGE_UNIT Storage, SECTOR PartitionStart, U32 PartitionSize, SECTOR Sector,
                              U32 SectorCount, LPVOID Buffer, U32 BufferSize, UINT Command);

/************************************************************************/

#endif  // DRIVERS_FILESYSTEMS_FILESYSTEM_COMMON_H_INCLUDED
