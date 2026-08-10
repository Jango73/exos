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

#include "drivers/filesystems/FileSystem-Common.h"
#include "fs/DiskTransferLayer.h"

/************************************************************************/

/**
 * @brief Execute one bounded sector transfer inside a partition.
 *
 * @param Disk Target disk.
 * @param PartitionStart First partition sector.
 * @param PartitionSize Partition size in sectors.
 * @param Sector Starting sector to transfer.
 * @param SectorCount Number of sectors to transfer.
 * @param Buffer Transfer buffer.
 * @param BufferSize Transfer buffer size in bytes.
 * @param Command Disk command to execute.
 * @return TRUE on success, FALSE on failure.
 */
BOOL PartitionTransferSectors(
    LPSTORAGE_UNIT Disk,
    SECTOR PartitionStart,
    U32 PartitionSize,
    SECTOR Sector,
    U32 SectorCount,
    LPVOID Buffer,
    U32 BufferSize,
    UINT Command) {
    IOCONTROL Control;

    if (Disk == NULL || Buffer == NULL || SectorCount == 0 || BufferSize == 0) {
        return FALSE;
    }

    if (Sector < PartitionStart || Sector >= PartitionStart + PartitionSize) {
        return FALSE;
    }

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = SectorCount;
    Control.Buffer = Buffer;
    Control.BufferSize = BufferSize;

    if (Command == DF_DISK_READ) {
        return (DiskTransferLayerRead(&Control) == DF_RETURN_SUCCESS);
    }

    if (Command == DF_DISK_WRITE) {
        return (DiskTransferLayerWrite(&Control) == DF_RETURN_SUCCESS);
    }

    return FALSE;
}
