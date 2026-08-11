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


    Storage

\************************************************************************/

#include "fs/Storage.h"

#include "core/Kernel.h"
#include "log/Log.h"

/***************************************************************************/

void SectorToBlockParams(LPSTORAGEGEOMETRY Geometry, U32 Sector, LPBLOCKPARAMS Block) {
    U32 Temp1, Temp2;

    Block->Cylinder = 0;
    Block->Head = 0;
    Block->Sector = 0;

    if (Geometry->Heads == 0) return;
    if (Geometry->SectorsPerTrack == 0) return;

    Temp1 = Geometry->Heads * Geometry->SectorsPerTrack;
    Block->Cylinder = Sector / Temp1;
    Temp2 = Sector % Temp1;
    Block->Head = Temp2 / Geometry->SectorsPerTrack;
    Block->Sector = Temp2 % Geometry->SectorsPerTrack + 1;
}

/***************************************************************************/
