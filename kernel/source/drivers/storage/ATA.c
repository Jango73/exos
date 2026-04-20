
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


    ATA

\************************************************************************/

#include "drivers/storage/ATA.h"

#include "system/Clock.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "drivers/interrupts/InterruptController.h"
#include "system/System.h"
#include "core/DriverEnum.h"
#include "utils/BufferPool.h"
#include "utils/Cache.h"

/***************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/
// Buffer pool configuration

#define ATA_POOL_ALLOC_FLAGS (ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE)
#define ATA_SECTOR_BUFFER_OBJECTS_PER_SLAB NUM_BUFFERS
#define ATA_SECTOR_BUFFER_INITIAL_SLABS 1
#define ATA_SECTOR_BUFFER_MIN_FREE NUM_BUFFERS

/***************************************************************************/

UINT ATADiskCommands(UINT, UINT);

DRIVER DATA_SECTION ATADiskDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_ATA_STORAGE,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "IBM PC and compatibles",
    .Product = "ATA Disk Controller",
    .Alias = "ata",
    .Flags = 0,
    .Command = ATADiskCommands,
    .EnumDomainCount = 1,
    .EnumDomains = {ENUM_DOMAIN_ATA_DEVICE}};

/***************************************************************************/

/**
 * @brief Retrieves the ATA disk driver descriptor.
 * @return Pointer to the ATA disk driver.
 */
LPDRIVER ATADiskGetDriver(void) {
    return &ATADiskDriver;
}

/***************************************************************************/

// ATA physical disk, derives from STORAGE_UNIT

typedef struct tag_ATADISK {
    STORAGE_UNIT Header;
    DISKGEOMETRY Geometry;
    U32 Access;  // Access parameters
    U32 IOPort;  // 0x01F0 or 0x0170
    U32 IRQ;     // 0x0E
    U32 Drive;   // 0 or 1
    CACHE SectorCache;
    BUFFER_POOL SectorBufferPool;
} ATADISK, *LPATADISK;

/***************************************************************************/

typedef struct tag_SECTOR_CACHE_CONTEXT {
    U32 SectorLow;
    U32 SectorHigh;
} SECTOR_CACHE_CONTEXT, *LPSECTOR_CACHE_CONTEXT;

/***************************************************************************/

static BOOL SectorCacheMatcher(LPVOID Data, LPVOID Context) {
    LPSECTORBUFFER Buffer = (LPSECTORBUFFER)Data;
    LPSECTOR_CACHE_CONTEXT Match = (LPSECTOR_CACHE_CONTEXT)Context;

    if (Buffer == NULL || Match == NULL) {
        return FALSE;
    }

    return Buffer->SectorLow == Match->SectorLow && Buffer->SectorHigh == Match->SectorHigh;
}

/***************************************************************************/

/**
 * @brief Release callback for ATA sector cache entries.
 *
 * @param Data Cache entry payload (LPSECTORBUFFER).
 * @param Dirty Dirty flag from cache entry.
 * @param Context Buffer pool context pointer.
 */
static void ATACacheRelease(LPVOID Data, BOOL Dirty, LPVOID Context) {
    LPBUFFER_POOL Pool = (LPBUFFER_POOL)Context;

    UNUSED(Dirty);

    if (Data == NULL) {
        return;
    }

    if (Pool == NULL) {
        KernelHeapFree(Data);
        return;
    }

    BufferPoolRelease(Pool, Data);
}

/***************************************************************************/

static LPATADISK NewATADisk(void) {
    LPATADISK This;

    This = (LPATADISK)KernelHeapAlloc(sizeof(ATADISK));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(ATADISK));

    This->Header.TypeID = KOID_DISK;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &ATADiskDriver;
    This->Access = 0;

    return This;
}

/***************************************************************************/

static BOOL ATAWaitNotBusy(U32 Port, U32 TimeOut) {
    U32 Status;

    while (TimeOut--) {
        Status = InPortByte(Port + HD_STATUS);
        if ((Status & HD_STATUS_BUSY) == 0) {
            return TRUE;
        }
    }

    DEBUG(TEXT("Time-out in ATA port %x"), Port);

    return FALSE;
}

/***************************************************************************/

static BOOL ATAWaitDataReady(U32 Port, U32 TimeOut) {
    U32 Status;

    while (TimeOut--) {
        Status = InPortByte(Port + HD_STATUS);
        if ((Status & HD_STATUS_BUSY) == 0 && (Status & HD_STATUS_DRQ) != 0) {
            return TRUE;
        }
    }

    DEBUG(TEXT("Time-out in ATA port %x"), Port);

    return FALSE;
}

/***************************************************************************/

static BOOL InitializeATA(void) {
    LPATADISK Disk;
    LPATADRIVEID ATAID;
    U8 Buffer[SECTOR_SIZE];
    U32 Port;
    U32 Drive;
    U32 DisksFound = 0;

    DEBUG(TEXT("Enter"));

    DisableInterrupt(IRQ_ATA);

    //-------------------------------------
    // Identify the drives

    for (Port = 0; Port < 2; Port++) {
        U32 RealPort = 0;

        if (Port == 0) RealPort = ATA_PORT_0;
        if (Port == 1) RealPort = ATA_PORT_1;

        for (Drive = 0; Drive < 2; Drive++) {
            if (ATAWaitNotBusy(RealPort, TIMEOUT) == FALSE) continue;

            OutPortByte(RealPort + HD_CYLINDERLOW, 0);
            OutPortByte(RealPort + HD_CYLINDERHIGH, 0);
            OutPortByte(RealPort + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4));

            // Add delay for drive selection
            for (U32 DelayIndex = 0; DelayIndex < 1000; DelayIndex++);

            // Check for floating bus (no drive present)
            U32 Status = InPortByte(RealPort + HD_STATUS);
            if (Status == 0xFF) continue;  // No drive present

            OutPortByte(RealPort + HD_SECTOR, 0);
            OutPortByte(RealPort + HD_NUMSECTORS, 1);
            OutPortByte(RealPort + HD_COMMAND, HD_COMMAND_IDENTIFY);

            if (ATAWaitDataReady(RealPort, TIMEOUT) == FALSE) continue;

            // Check for error after IDENTIFY command
            Status = InPortByte(RealPort + HD_STATUS);
            if (Status & HD_STATUS_ERROR) continue;
            if (!(Status & HD_STATUS_DRQ)) continue;

            InPortStringWord(RealPort + HD_DATA, Buffer, SECTOR_SIZE / 2);

            ATAID = (LPATADRIVEID)Buffer;

            if (ATAID->PhysicalCylinders != 0 && ATAID->PhysicalHeads != 0 && ATAID->PhysicalSectors != 0) {
                DEBUG(TEXT("port: %x, drive: %x"), (U32)RealPort, (U32)Drive);

                Disk = NewATADisk();
                if (Disk == NULL) continue;

                Disk->Geometry.Cylinders = ATAID->PhysicalCylinders;
                Disk->Geometry.Heads = ATAID->PhysicalHeads;
                Disk->Geometry.SectorsPerTrack = ATAID->PhysicalSectors;
                Disk->Geometry.BytesPerSector = SECTOR_SIZE;
                Disk->IOPort = RealPort;
                Disk->IRQ = IRQ_ATA;
                Disk->Drive = Drive;
                if (!BufferPoolInit(&Disk->SectorBufferPool,
                                    (UINT)sizeof(SECTORBUFFER),
                                    ATA_SECTOR_BUFFER_OBJECTS_PER_SLAB,
                                    ATA_SECTOR_BUFFER_INITIAL_SLABS,
                                    ATA_POOL_ALLOC_FLAGS)) {
                    KernelHeapFree(Disk);
                    continue;
                }

                if (!BufferPoolReserve(&Disk->SectorBufferPool, ATA_SECTOR_BUFFER_MIN_FREE)) {
                    BufferPoolDeinit(&Disk->SectorBufferPool);
                    KernelHeapFree(Disk);
                    continue;
                }

                CacheInit(&Disk->SectorCache, NUM_BUFFERS);

                if (Disk->SectorCache.Entries == NULL) {
                    BufferPoolDeinit(&Disk->SectorBufferPool);
                    KernelHeapFree(Disk);
                    continue;
                }

                CacheSetWritePolicy(
                    &Disk->SectorCache, CACHE_WRITE_POLICY_READ_ONLY, NULL, ATACacheRelease, &Disk->SectorBufferPool);

                ListAddItem(GetDiskList(), Disk);
                DisksFound++;
            }
        }
    }

    // Only enable IRQ if we found at least one disk
    if (DisksFound > 0) {
        EnableInterrupt(IRQ_ATA);
        DEBUG(TEXT("Found %d disk(s), IRQ enabled"), DisksFound);
    } else {
        DEBUG(TEXT("No disks found, IRQ remains disabled"));
    }

    DEBUG(TEXT("Exit"));

    return TRUE;
}

/***************************************************************************/

/*
static U32 ControllerBusy(U32 Port) {
    U32 TimeOut = 100000;
    U32 Status;

    do {
        Status = InPortByte(Port + HD_STATUS);
    } while ((Status & HD_STATUS_BUSY) && TimeOut--);

    return Status;
}
*/

/***************************************************************************/

/*
static BOOL IsStatusOk(U32 Port) {
    U32 Status = InPortByte(Port + HD_STATUS);

    if (Status & HD_STATUS_BUSY) return TRUE;
    if (Status & HD_STATUS_WERROR) return FALSE;
    if (!(Status & HD_STATUS_READY)) return FALSE;
    if (!(Status & HD_STATUS_SEEK)) return FALSE;

    return TRUE;
}
*/

/***************************************************************************/

/*
static BOOL IsControllerReady(U32 Port, U32 Drive, U32 Head) {
    U32 Retry = 100;

    do {
        if (ControllerBusy(Port) & HD_STATUS_BUSY) return FALSE;
        OutPortByte(Port + HD_HEAD, 0xA0 | (Drive << 4) | Head);
        if (IsStatusOk(Port)) return TRUE;
    } while (Retry--);

    return FALSE;
}
*/

/***************************************************************************/

/*
static void ResetController(U32 Port) {
    UNUSED(Port);

    U32 Index;

    OutPortByte(HD_ALTCOMMAND, 4);
    for (Index = 0; Index < 1000; Index++) barrier();
    OutPortByte(HD_ALTCOMMAND, hd_info[0].ctl);
    for (Index = 0; Index < 1000; Index++) barrier();
    if (IsDriveBusy())
    {
    }
    else
    if ((HD_Error = InPortByte(Port + HD_ERROR)) != 1)
    {
    }
}
*/

/***************************************************************************/

static void ATADriveOut(U32 Port, U32 Drive, U32 Command, U8* Buffer, U32 Cylinder, U32 Head, U32 Sector, U32 Count) {
    U32 Flags;

    SaveFlags(&Flags);
    DisableInterrupts();

    if (ATAWaitNotBusy(Port, TIMEOUT) == FALSE) goto Out;

    OutPortByte(Port + HD_CYLINDERLOW, Cylinder & 0xFF);
    OutPortByte(Port + HD_CYLINDERHIGH, (Cylinder >> 8) & 0xFF);
    OutPortByte(Port + HD_HEAD, 0xA0 | ((Drive & 0x01) << 4) | (Head & 0x0F));
    OutPortByte(Port + HD_SECTOR, Sector & 0xFF);
    OutPortByte(Port + HD_NUMSECTORS, Count & 0xFF);
    OutPortByte(Port + HD_COMMAND, Command);

    if (ATAWaitDataReady(Port, TIMEOUT) == FALSE) goto Out;

    if (Command == HD_COMMAND_READ) {
        InPortStringWord(Port + HD_DATA, Buffer, (Count * SECTOR_SIZE) / 2);
    } else if (Command == HD_COMMAND_WRITE) {
        OutPortStringWord(Port + HD_DATA, Buffer, (Count * SECTOR_SIZE) / 2);
    }

Out:

    RestoreFlags(&Flags);
}

/***************************************************************************/

static U32 Read(LPIOCONTROL Control) {
    LPATADISK Disk;
    BLOCKPARAMS Params;
    U32 Current;

    //-------------------------------------
    // Check validity of parameters

    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Control->Disk;
    if (Disk == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.TypeID != KOID_DISK) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IOPort == 0) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IRQ == 0) return DF_RETURN_BAD_PARAMETER;

    CacheCleanup(&Disk->SectorCache, GetSystemTime());

    for (Current = 0; Current < Control->NumSectors; Current++) {
        SECTOR_CACHE_CONTEXT Context = {Control->SectorLow + Current, 0};
        LPSECTORBUFFER Buffer = (LPSECTORBUFFER)CacheFind(&Disk->SectorCache, SectorCacheMatcher, &Context);

        if (Buffer == NULL) {
            Buffer = (LPSECTORBUFFER)BufferPoolAcquire(&Disk->SectorBufferPool);

            if (Buffer == NULL) return DF_RETURN_UNEXPECTED;

            Buffer->SectorLow = Context.SectorLow;
            Buffer->SectorHigh = Context.SectorHigh;
            Buffer->Dirty = 0;

            //-------------------------------------
            // We must now do a physical disk access

            DisableInterrupt(Disk->IRQ);

            SectorToBlockParams(&(Disk->Geometry), Context.SectorLow, &Params);

            ATADriveOut(
                Disk->IOPort, Disk->Drive, HD_COMMAND_READ, Buffer->Data, Params.Cylinder, Params.Head, Params.Sector,
                1);

            EnableInterrupt(Disk->IRQ);

            if (!CacheAdd(&Disk->SectorCache, Buffer, DISK_CACHE_TTL_MS)) {
                BufferPoolRelease(&Disk->SectorBufferPool, Buffer);
                return DF_RETURN_UNEXPECTED;
            }
        }

        MemoryCopy(((U8*)Control->Buffer) + (Current * SECTOR_SIZE), Buffer->Data, SECTOR_SIZE);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 Write(LPIOCONTROL Control) {
    LPATADISK Disk;
    BLOCKPARAMS Params;
    U32 Current;

    //-------------------------------------
    // Check validity of parameters

    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Control->Disk;
    if (Disk == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.TypeID != KOID_DISK) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IOPort == 0) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IRQ == 0) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Check access permissions

    if (Disk->Access & DISK_ACCESS_READONLY) return DF_RETURN_NO_PERMISSION;

    CacheCleanup(&Disk->SectorCache, GetSystemTime());

    for (Current = 0; Current < Control->NumSectors; Current++) {
        SECTOR_CACHE_CONTEXT Context = {Control->SectorLow + Current, 0};
        LPSECTORBUFFER Buffer = (LPSECTORBUFFER)CacheFind(&Disk->SectorCache, SectorCacheMatcher, &Context);
        BOOL AddedToCache = FALSE;

        if (Buffer == NULL) {
            Buffer = (LPSECTORBUFFER)BufferPoolAcquire(&Disk->SectorBufferPool);

            if (Buffer == NULL) return DF_RETURN_UNEXPECTED;

            Buffer->SectorLow = Context.SectorLow;
            Buffer->SectorHigh = Context.SectorHigh;
            Buffer->Dirty = 0;
            AddedToCache = TRUE;
        }

        MemoryCopy(Buffer->Data, ((U8*)Control->Buffer) + (Current * SECTOR_SIZE), SECTOR_SIZE);
        Buffer->Dirty = 1;

        //-------------------------------------
        // Write to physical disk

        DisableInterrupt(Disk->IRQ);

        SectorToBlockParams(&(Disk->Geometry), Context.SectorLow, &Params);

        ATADriveOut(
            Disk->IOPort, Disk->Drive, HD_COMMAND_WRITE, Buffer->Data, Params.Cylinder, Params.Head, Params.Sector, 1);

        EnableInterrupt(Disk->IRQ);

        Buffer->Dirty = 0;

        if (AddedToCache) {
            if (!CacheAdd(&Disk->SectorCache, Buffer, DISK_CACHE_TTL_MS)) {
                BufferPoolRelease(&Disk->SectorBufferPool, Buffer);
                return DF_RETURN_UNEXPECTED;
            }
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 GetInfo(LPDISKINFO Info) {
    LPATADISK Disk;

    if (Info == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Info->Disk;
    if (Disk == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.TypeID != KOID_DISK) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IOPort == 0) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IRQ == 0) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------

    Info->Type = DRIVER_TYPE_ATA_STORAGE;
    Info->Removable = 0;
    Info->BytesPerSector = Disk->Geometry.BytesPerSector;
    Info->NumSectors = U64_FromU32(
        Disk->Geometry.Cylinders * Disk->Geometry.Heads * Disk->Geometry.SectorsPerTrack);
    Info->Access = Disk->Access;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static U32 SetAccess(LPDISKACCESS Access) {
    LPATADISK Disk;

    if (Access == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the physical disk to which operation applies

    Disk = (LPATADISK)Access->Disk;
    if (Disk == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Check validity of parameters

    if (Disk->Header.TypeID != KOID_DISK) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IOPort == 0) return DF_RETURN_BAD_PARAMETER;
    if (Disk->IRQ == 0) return DF_RETURN_BAD_PARAMETER;

    Disk->Access = Access->Access;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

void HardDriveHandler(void) {
    static U32 DATA_SECTION Busy = 0;
    U32 Status0, Status1;
    BOOL RealInterrupt = FALSE;

    if (Busy) return;
    Busy = 1;

    // Check if this is a real ATA interrupt by reading status registers
    Status0 = InPortByte(ATA_PORT_0 + HD_STATUS);
    Status1 = InPortByte(ATA_PORT_1 + HD_STATUS);

    // A real ATA interrupt should have specific status bits set
    // and should not return 0xFF (floating bus)
    if (Status0 != 0xFF && (Status0 & (HD_STATUS_DRQ | HD_STATUS_ERROR))) {
        RealInterrupt = TRUE;
        DEBUG(TEXT("Real interrupt on primary channel, status: %x"), Status0);
    }

    if (Status1 != 0xFF && (Status1 & (HD_STATUS_DRQ | HD_STATUS_ERROR))) {
        RealInterrupt = TRUE;
        DEBUG(TEXT("Real interrupt on secondary channel, status: %x"), Status1);
    }

    // Only process if this is a real interrupt
    if (RealInterrupt) {
        // TODO: Add proper interrupt handling code here
        DEBUG(TEXT("Processing ATA interrupt"));
    }

    Busy = 0;
}

/***************************************************************************/

static U32 ATA_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) ||
        Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_ATA_DEVICE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    LPLIST DiskList = GetDiskList();
    if (DiskList == NULL) {
        return DF_RETURN_NO_MORE;
    }

    UINT MatchIndex = 0;
    for (LPLISTNODE Node = DiskList->First; Node; Node = Node->Next) {
        LPATADISK Disk = (LPATADISK)Node;
        SAFE_USE_VALID(Disk) {
            if (Disk->Header.TypeID != KOID_DISK) {
                continue;
            }
            if (Disk->Header.Driver != &ATADiskDriver) {
                continue;
            }

            if (MatchIndex == Next->Query->Index) {
                DRIVER_ENUM_ATA_DEVICE Data;
                MemorySet(&Data, 0, sizeof(Data));

                Data.IOPort = Disk->IOPort;
                Data.Drive = Disk->Drive;
                Data.IRQ = Disk->IRQ;
                Data.Cylinders = Disk->Geometry.Cylinders;
                Data.Heads = Disk->Geometry.Heads;
                Data.SectorsPerTrack = Disk->Geometry.SectorsPerTrack;

                MemorySet(Next->Item, 0, sizeof(DRIVER_ENUM_ITEM));
                Next->Item->Header.Size = sizeof(DRIVER_ENUM_ITEM);
                Next->Item->Header.Version = EXOS_ABI_VERSION;
                Next->Item->Domain = ENUM_DOMAIN_ATA_DEVICE;
                Next->Item->Index = Next->Query->Index;
                Next->Item->DataSize = sizeof(Data);
                MemoryCopy(Next->Item->Data, &Data, sizeof(Data));

                Next->Query->Index++;
                return DF_RETURN_SUCCESS;
            }

            MatchIndex++;
        }
    }

    return DF_RETURN_NO_MORE;
}

/***************************************************************************/

static U32 ATA_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Pretty->Item->Domain != ENUM_DOMAIN_ATA_DEVICE ||
        Pretty->Item->DataSize < sizeof(DRIVER_ENUM_ATA_DEVICE)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    const DRIVER_ENUM_ATA_DEVICE* Data = (const DRIVER_ENUM_ATA_DEVICE*)Pretty->Item->Data;
    StringPrintFormat(Pretty->Buffer,
                      TEXT("ATA Port %x Drive=%u IRQ=%u CHS=%u/%u/%u"),
                      Data->IOPort,
                      Data->Drive,
                      Data->IRQ,
                      Data->Cylinders,
                      Data->Heads,
                      Data->SectorsPerTrack);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

UINT ATADiskCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((ATADiskDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeATA()) {
                ATADiskDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;
        case DF_UNLOAD:
            if ((ATADiskDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ATADiskDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_DISK_RESET:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_DISK_READ:
            return Read((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return Write((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return GetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return SetAccess((LPDISKACCESS)Parameter);
        case DF_ENUM_NEXT:
            return ATA_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Parameter);
        case DF_ENUM_PRETTY:
            return ATA_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
