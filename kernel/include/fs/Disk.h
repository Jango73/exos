
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


    Disk

\************************************************************************/

#ifndef DISK_H_INCLUDED
#define DISK_H_INCLUDED

/***************************************************************************/

#include "../Base.h"
#include "core/Driver.h"
#include "core/ID.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

// Functions supplied by a disk driver

#define DF_DISK_RESET (DF_FIRST_FUNCTION + 0)
#define DF_DISK_READ (DF_FIRST_FUNCTION + 1)
#define DF_DISK_WRITE (DF_FIRST_FUNCTION + 2)
#define DF_DISK_GETINFO (DF_FIRST_FUNCTION + 3)
#define DF_DISK_SETACCESS (DF_FIRST_FUNCTION + 4)

/***************************************************************************/

typedef U32 SECTOR;
typedef U32 CLUSTER;

/***************************************************************************/

#define SECTOR_SIZE 512

/***************************************************************************/

typedef struct tag_DISKGEOMETRY {
    U32 Cylinders;
    U32 Heads;
    U32 SectorsPerTrack;
    U32 BytesPerSector;
} DISK_GEOMETRY, *LPDISKGEOMETRY;

/***************************************************************************/

typedef struct tag_DISK_TRANSFER_UNIT DISK_TRANSFER_UNIT, *LPDISK_TRANSFER_UNIT;

typedef struct tag_STORAGE_UNIT {
    LISTNODE_FIELDS
    LPDRIVER Driver;
    LPDISK_TRANSFER_UNIT DiskTransfer;  // Disk transfer layer state, owned by DiskTransferLayer
} STORAGE_UNIT, *LPSTORAGE_UNIT;

/***************************************************************************/

typedef struct tag_IOCONTROL {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Disk;
    U32 SectorLow;
    U32 SectorHigh;
    U32 NumSectors;
    LPVOID Buffer;
    U32 BufferSize;
} IOCONTROL, *LPIOCONTROL;

/***************************************************************************/

typedef struct tag_DISKINFO {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Disk;
    U32 Type;
    U32 Removable;
    U32 BytesPerSector;
    U64 NumSectors;
    U32 Access;
} DISK_INFO, *LPDISKINFO;

/***************************************************************************/

typedef struct tag_DISKACCESS {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Disk;
    U32 Access;
} DISK_ACCESS, *LPDISKACCESS;

/***************************************************************************/

#define DISK_ACCESS_DISABLE 0x0001
#define DISK_ACCESS_READONLY 0x0002

/***************************************************************************/

// Common constants

#define MAX_DISK 4
#define TIMEOUT 10000
#define NUM_BUFFERS 32
#define DISK_CACHE_TTL_MS (5 * 60 * 1000)

/***************************************************************************/
// Disk sector buffer

typedef struct tag_SECTORBUFFER {
    U32 SectorLow;
    U32 SectorHigh;
    U32 Dirty;
    U8 Data[SECTOR_SIZE];
} SECTOR_BUFFER, *LPSECTORBUFFER;

/***************************************************************************/

typedef struct tag_BLOCKPARAMS {
    U32 Cylinder;
    U32 Head;
    U32 Sector;
} BLOCKPARAMS, *LPBLOCKPARAMS;

/***************************************************************************/
// Function prototypes

void SectorToBlockParams(LPDISKGEOMETRY Geometry, U32 Sector, LPBLOCKPARAMS Block);

/***************************************************************************/

#pragma pack(pop)

#endif
