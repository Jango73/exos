
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


    EXOS MBR

\************************************************************************/

#include "utils/ExosMbr.h"

#include "fs/Storage-Transfer-Layer.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Fill an EXOS MBR structure for the provided cluster size.
 * @param Master Destination MBR structure to fill.
 * @param SectorsPerCluster Cluster size in sectors.
 */
void ExosMbrFill(LPEXFSMBR Master, U16 SectorsPerCluster) {
    if (Master == NULL) return;

    MemorySet(Master, 0, sizeof(EXFS_MBR));

    Master->OEMName[0] = 'E';
    Master->OEMName[1] = 'X';
    Master->OEMName[2] = 'O';
    Master->OEMName[3] = 'S';
    Master->OEMName[4] = ' ';
    Master->OEMName[5] = ' ';
    Master->OEMName[6] = ' ';
    Master->OEMName[7] = ' ';
    Master->MediaDescriptor = 0xF8;
    Master->LogicalDriveNumber = 0;
    Master->Cylinders = 0;
    Master->Heads = 0;
    Master->SectorsPerTrack = 0;
    Master->BytesPerSector = SECTOR_SIZE;
    Master->SectorsPerCluster = SectorsPerCluster;
    Master->BIOSMark = 0xAA55;
}

/************************************************************************/

/**
 * @brief Write an EXOS MBR to storage.
 * @param Storage Target storage unit.
 * @param StartSector Sector offset for the MBR write.
 * @param SectorsPerCluster Cluster size in sectors.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ExosMbrWrite(LPSTORAGE_UNIT Storage, U32 StartSector, U16 SectorsPerCluster) {
    EXFS_MBR Master;
    IOCONTROL Control;
    U32 Result;

    if (Storage == NULL) return FALSE;

    ExosMbrFill(&Master, SectorsPerCluster);

    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = Storage;
    Control.SectorLow = StartSector;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)&Master;
    Control.BufferSize = sizeof(EXFS_MBR);

    Result = StorageTransferLayerWrite(&Control);

    return Result == DF_RETURN_SUCCESS;
}

/************************************************************************/
