
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


    FAT Common Helpers

\************************************************************************/

#include "drivers/filesystems/FAT.h"
#include "fs/Storage-Transfer-Layer.h"

/***************************************************************************/

/**
 * @brief Read the boot sector of a FAT partition and validate the BIOS mark.
 *
 * @param Storage Physical storage hosting the partition.
 * @param Partition Partition descriptor.
 * @param Base Base sector offset.
 * @param Buffer Caller-provided SECTOR_SIZE buffer to fill.
 * @return TRUE on success with valid BIOS mark, FALSE otherwise.
 */
BOOL FATReadBootSector(LPSTORAGE_UNIT Storage, LPBOOT_PARTITION Partition, U32 Base, LPVOID Buffer) {
    IOCONTROL Control;
    U32 Result;
    U16* BiosMark;

    Control.TypeID = KOID_IOCONTROL;
    Control.Storage = Storage;
    Control.SectorLow = Base + Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = StorageTransferLayerRead(&Control);
    if (Result != DF_RETURN_SUCCESS) return FALSE;

    BiosMark = (U16*)((U8*)Buffer + (SECTOR_SIZE - sizeof(U16)));

    if (*BiosMark != 0xAA55) return FALSE;

    return TRUE;
}

/***************************************************************************/
