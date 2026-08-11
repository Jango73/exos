
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

#ifndef STORAGE_H_INCLUDED
#define STORAGE_H_INCLUDED

/***************************************************************************/

#include "../Base.h"
#include "core/Driver.h"
#include "core/ID.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

// Functions supplied by a storage driver

#define DF_STORAGE_RESET (DF_FIRST_FUNCTION + 0)
#define DF_STORAGE_READ (DF_FIRST_FUNCTION + 1)
#define DF_STORAGE_WRITE (DF_FIRST_FUNCTION + 2)
#define DF_STORAGE_GETINFO (DF_FIRST_FUNCTION + 3)
#define DF_STORAGE_SETACCESS (DF_FIRST_FUNCTION + 4)

/***************************************************************************/

typedef U32 SECTOR;
typedef U32 CLUSTER;

/***************************************************************************/

#define SECTOR_SIZE 512

/***************************************************************************/
// Stable storage identity sizes. The ID follows the Linux /dev/disk/by-id model
// (vendor_model_serial) and is independent of enumeration order.

#define STORAGE_ID_VENDOR_MAX_SIZE 17
#define STORAGE_ID_MODEL_MAX_SIZE 41
#define STORAGE_ID_SERIAL_MAX_SIZE 41
#define STORAGE_ID_MAX_SIZE 128

/***************************************************************************/

typedef struct tag_STORAGEGEOMETRY {
    U32 Cylinders;
    U32 Heads;
    U32 SectorsPerTrack;
    U32 BytesPerSector;
} STORAGE_GEOMETRY, *LPSTORAGEGEOMETRY;

/***************************************************************************/

typedef struct tag_STORAGE_TRANSFER_UNIT STORAGE_TRANSFER_UNIT, *LPSTORAGE_TRANSFER_UNIT;

typedef struct tag_STORAGE_UNIT {
    LISTNODE_FIELDS
    LPDRIVER Driver;
    LPSTORAGE_TRANSFER_UNIT StorageTransfer;    // Storage transfer layer state, owned by StorageTransferLayer
    STR Vendor[STORAGE_ID_VENDOR_MAX_SIZE];  // Hardware vendor string (empty when unavailable)
    STR Model[STORAGE_ID_MODEL_MAX_SIZE];    // Hardware model string
    STR Serial[STORAGE_ID_SERIAL_MAX_SIZE];  // Hardware serial string
    STR StorageId[STORAGE_ID_MAX_SIZE];      // Stable ID (vendor_model_serial), see utils/StorageID
} STORAGE_UNIT, *LPSTORAGE_UNIT;

/***************************************************************************/

typedef struct tag_IOCONTROL {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Storage;
    U32 SectorLow;
    U32 SectorHigh;
    U32 NumSectors;
    LPVOID Buffer;
    U32 BufferSize;
} IOCONTROL, *LPIOCONTROL;

/***************************************************************************/

typedef struct tag_STORAGEINFO {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Storage;
    U32 Type;
    U32 Removable;
    U32 BytesPerSector;
    U64 NumSectors;
    U32 Access;
} STORAGE_INFO, *LPSTORAGEINFO;

/***************************************************************************/

typedef struct tag_STORAGEACCESS {
    LISTNODE_FIELDS
    LPSTORAGE_UNIT Storage;
    U32 Access;
} STORAGE_ACCESS, *LPSTORAGEACCESS;

/***************************************************************************/

#define STORAGE_ACCESS_DISABLE 0x0001
#define STORAGE_ACCESS_READONLY 0x0002

/***************************************************************************/

// Common constants

#define MAX_STORAGE 4
#define TIMEOUT 10000
#define NUM_BUFFERS 32
#define STORAGE_CACHE_TTL_MS (5 * 60 * 1000)

/***************************************************************************/
// Storage sector buffer

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

void SectorToBlockParams(LPSTORAGEGEOMETRY Geometry, U32 Sector, LPBLOCKPARAMS Block);

/***************************************************************************/

#pragma pack(pop)

#endif
