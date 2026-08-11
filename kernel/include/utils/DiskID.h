
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


    Stable disk identification

\************************************************************************/

#ifndef DISKID_H_INCLUDED
#define DISKID_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "fs/Disk.h"

/***************************************************************************/

void DiskIdSetIdentity(LPSTORAGE_UNIT Storage, LPCSTR Vendor, LPCSTR Model, LPCSTR Serial);
LPCSTR DiskIdEnsure(LPSTORAGE_UNIT Storage);
LPCSTR DiskIdGet(LPSTORAGE_UNIT Storage);
LPSTORAGE_UNIT DiskIdFindById(LPCSTR Id);

/***************************************************************************/

#endif
