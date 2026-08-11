
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


    SATA - AHCI Implementation

\************************************************************************/

#include "drivers/storage/SATA.h"

#include "User.h"
#include "core/DriverEnum.h"
#include "core/Kernel.h"
#include "drivers/bus/PCI.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "drivers/storage/ATA.h"
#include "fs/Storage-Transfer-Layer.h"
#include "log/Log.h"
#include "log/Profile.h"
#include "memory/Memory.h"
#include "system/Clock.h"
#include "utils/BufferPool.h"
#include "utils/Storage-ID.h"

/***************************************************************************/
// Version

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/
// Additional error codes not in User.h

#define DF_RETURN_HARDWARE 0x00001001
#define DF_RETURN_TIMEOUT 0x00001002
#define DF_RETURN_BUSY 0x00001003
#define DF_RETURN_NODEVICE 0x00001004

/***************************************************************************/

#define AHCI_MAX_PORTS 32
#define AHCI_CMD_LIST_SIZE 1024  // 32 command headers * 32 bytes each
#define AHCI_FIS_SIZE 256        // FIS receive area size
#define AHCI_CMD_TBL_SIZE 256    // Command table size (including PRDT)
#define AHCI_MAX_PRDT 8          // Max PRDT entries per command

/***************************************************************************/
// Buffer pool configuration

#define SATA_POOL_ALLOC_FLAGS (ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE)
#define SATA_BOUNCE_BUFFER_BYTES (N_4KB + N_4KB)
#define SATA_BOUNCE_BUFFER_OBJECTS_PER_SLAB 8
#define SATA_BOUNCE_BUFFER_INITIAL_SLABS 1
#define SATA_BOUNCE_BUFFER_MIN_FREE 8

// One AHCI command may transfer up to this many sectors. Bounded by the single
// bounce buffer AHCICommand uses (SATA_BOUNCE_BUFFER_BYTES) minus the alignment
// padding it reserves, divided by the sector size.
#define SATA_MAX_DMA_SECTORS ((SATA_BOUNCE_BUFFER_BYTES - N_4KB) / SECTOR_SIZE)

/***************************************************************************/
// AHCI Port Structure

typedef struct tag_AHCI_PORT {
    STORAGE_UNIT Header;
    STORAGE_GEOMETRY Geometry;
    U32 Access;  // Access parameters
    U32 PortNumber;
    LPAHCI_HBA_PORT HBAPort;  // Pointer to HBA port registers
    LPAHCI_HBA_MEM HBAMem;    // Pointer to HBA memory

    // Command structures (must be aligned)
    LPAHCI_CMD_HEADER CommandList;  // Command list (1KB aligned)
    LPAHCI_FIS FISBase;             // FIS receive area (256-byte aligned)
    LPAHCI_CMD_TBL CommandTable;    // Command table

    // Buffer management
    BUFFER_POOL BounceBufferPool;

    volatile U32 PendingInterrupts;
} AHCI_PORT, *LPAHCI_PORT;

/***************************************************************************/
// Global AHCI state

typedef struct tag_AHCI_STATE {
    LPAHCI_HBA_MEM Base;
    U32 PortsImplemented;
    LPPCI_DEVICE Device;
    LPAHCI_PORT Ports[AHCI_MAX_PORTS];
    volatile U32 PendingPortsMask;
    U8 InterruptSlot;
    BOOL InterruptRegistered;
    BOOL InterruptEnabled;
} AHCI_STATE, *LPAHCI_STATE;

static AHCI_STATE AHCIState = { .Base = NULL,
                                .PortsImplemented = 0,
                                .Device = NULL,
                                .Ports = { 0 },
                                .PendingPortsMask = 0,
                                .InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT,
                                .InterruptRegistered = FALSE,
                                .InterruptEnabled = FALSE };

/***************************************************************************/
// AHCI PCI Driver

static UINT AHCIProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE AHCIAttach(LPPCI_DEVICE PciDevice);
static U32 InitializeAHCIController(void);
static BOOL AHCIRegisterInterrupts(void);
static BOOL AHCIInterruptTopHalf(LPDEVICE Device, LPVOID Context);
static void AHCIInterruptBottomHalf(LPDEVICE Device, LPVOID Context);
static void AHCIInterruptPoll(LPDEVICE Device, LPVOID Context);
static U32 SATA_EnumNext(LPDRIVER_ENUM_NEXT Next);
static U32 SATA_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty);
static U32 AHCICommand(LPAHCI_PORT AHCIPort, U8 Command, U32 LBA, U16 SectorCount, LPVOID Buffer, BOOL IsWrite);

static const DRIVER_MATCH AHCIMatches[] = {
    // Match any AHCI controller (Class 01h, Subclass 06h, Programming Interface 01h)
    { PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE, 0x06, 0x01 }
};

/***************************************************************************/

UINT SATAStorageCommands(UINT, UINT);

DRIVER DATA_SECTION SATAStorageDriver = { .TypeID = KOID_DRIVER,
                                       .References = 1,
                                       .Next = NULL,
                                       .Prev = NULL,
                                       .Type = DRIVER_TYPE_SATA_STORAGE,
                                       .VersionMajor = VER_MAJOR,
                                       .VersionMinor = VER_MINOR,
                                       .Designer = "Jango73",
                                       .Manufacturer = "SATA-IO",
                                       .Product = "AHCI SATA Controller",
                                       .Alias = "sata",
                                       .Flags = 0,
                                       .Command = SATAStorageCommands,
                                       .EnumDomainCount = 1,
                                       .EnumDomains = { ENUM_DOMAIN_AHCI_PORT } };

/***************************************************************************/

PCI_DRIVER DATA_SECTION AHCIPCIDriver = { .TypeID = KOID_DRIVER,
                                          .References = 1,
                                          .Next = NULL,
                                          .Prev = NULL,
                                          .Type = DRIVER_TYPE_SATA_STORAGE,
                                          .VersionMajor = VER_MAJOR,
                                          .VersionMinor = VER_MINOR,
                                          .Designer = "Jango73",
                                          .Manufacturer = "",
                                          .Product = "AHCI SATA Controller",
                                          .Alias = "sata",
                                          .Command = AHCIProbe,
                                          .Matches = AHCIMatches,
                                          .MatchCount = 1,
                                          .Attach = AHCIAttach };

/***************************************************************************/

/**
 * @brief Retrieves the AHCI PCI driver descriptor.
 * @return Pointer to the AHCI PCI driver.
 */
LPDRIVER AHCIPCIGetDriver(void) {
    return (LPDRIVER)&AHCIPCIDriver;
}

/***************************************************************************/

/**
 * @brief Retrieves the SATA storage driver descriptor.
 * @return Pointer to the SATA storage driver.
 */
LPDRIVER SATAStorageGetDriver(void) {
    return &SATAStorageDriver;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize an AHCI port structure.
 *
 * @return Pointer to new port or NULL on failure.
 */
static LPAHCI_PORT NewAHCIPort(void) {
    LPAHCI_PORT This;

    This = (LPAHCI_PORT)KernelHeapAlloc(sizeof(AHCI_PORT));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(AHCI_PORT));

    This->Header.TypeID = KOID_STORAGE;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &SATAStorageDriver;
    This->Access = 0;
    This->PendingInterrupts = 0;

    return This;
}

/***************************************************************************/

/**
 * @brief Stop an AHCI port (disable command/FIS engines).
 *
 * Clears ST and FRE then waits for hardware to acknowledge.
 *
 * @param Port Target HBA port.
 */
static void StopPort(LPAHCI_HBA_PORT Port) {
    // Clear ST (Start) bit
    Port->cmd &= ~AHCI_PORT_CMD_ST;

    // Wait until CR (Command List Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_CR) {
        // Wait
    }

    // Clear FRE (FIS Receive Enable)
    Port->cmd &= ~AHCI_PORT_CMD_FRE;

    // Wait until FR (FIS Receive Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_FR) {
        // Wait
    }
}

/***************************************************************************/

/**
 * @brief Start an AHCI port (enable command/FIS engines).
 *
 * @param Port Target HBA port.
 */
static void StartPort(LPAHCI_HBA_PORT Port) {
    // Wait until CR (Command List Running) is cleared
    while (Port->cmd & AHCI_PORT_CMD_CR) {
        // Wait
    }

    // Set FRE (FIS Receive Enable)
    Port->cmd |= AHCI_PORT_CMD_FRE;

    // Set ST (Start) bit
    Port->cmd |= AHCI_PORT_CMD_ST;
}

/***************************************************************************/

/*
static U32 FindFreeCommandSlot(LPAHCI_HBA_PORT Port) {
    // Check which slots are free
    U32 slots = (Port->sact | Port->ci);
    U32 cmdslots = (AHCIState.Base->cap & AHCI_CAP_NCS_MASK) >> 8;

    for (U32 i = 0; i < cmdslots; i++) {
        if ((slots & 1) == 0) {
            return i;
        }
        slots >>= 1;
    }

    return MAX_U32; // No free slot found
}
*/

/***************************************************************************/

/**
 * @brief Reset an AHCI port and ensure device presence.
 *
 * @param Port Target HBA port.
 * @return TRUE if reset sequence succeeds and device present.
 */
static BOOL AHCIPortReset(LPAHCI_HBA_PORT Port) {
    U32 timeout = 1000000;  // 1 second timeout

    // Check if device is present
    if ((Port->ssts & AHCI_PORT_SSTS_DET_MASK) != AHCI_PORT_SSTS_DET_ESTABLISHED) {
        return FALSE;
    }

    // Perform COMRESET
    Port->sctl = (Port->sctl & ~0xF) | 0x1;  // Set DET to 1

    // Wait 1ms
    for (volatile U32 i = 0; i < 10000; i++) {
        // Busy wait
    }

    Port->sctl &= ~0xF;  // Clear DET

    // Wait for device to be ready
    while (timeout--) {
        if ((Port->ssts & AHCI_PORT_SSTS_DET_MASK) == AHCI_PORT_SSTS_DET_ESTABLISHED) {
            break;
        }
    }

    if (timeout == 0) {
        return FALSE;
    }

    // Clear error register
    Port->serr = 0xFFFFFFFF;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Identify a device on an AHCI port and build its stable ID.
 *
 * Issues the ATA IDENTIFY DEVICE command and stores the hardware serial and
 * model strings on the storage unit. When identification fails, the storage
 * unit receives a deterministic synthetic ID instead.
 *
 * @param AHCIPort Target port wrapper structure.
 */
static void AHCIIdeIdentifyDevice(LPAHCI_PORT AHCIPort) {
    U8 Identify[SECTOR_SIZE];

    if (AHCIPort == NULL) return;

    if (AHCICommand(AHCIPort, ATA_CMD_IDENTIFY, 0, 1, Identify, FALSE) == DF_RETURN_SUCCESS) {
        U16* Words = (U16*)Identify;
        STR Serial[STORAGE_ID_SERIAL_MAX_SIZE];
        STR Model[STORAGE_ID_MODEL_MAX_SIZE];

        ATADecodeIdentifyString(Serial, sizeof(Serial), Words, 10, 10);
        ATADecodeIdentifyString(Model, sizeof(Model), Words, 27, 20);

        StorageIdSetIdentity((LPSTORAGE_UNIT)AHCIPort, NULL, Model, Serial);

        DEBUG(TEXT("IDENTIFY port=%u serial=%s model=%s"), AHCIPort->PortNumber, Serial, Model);
    } else {
        DEBUG(TEXT("IDENTIFY failed port=%u, using synthetic ID"), AHCIPort->PortNumber);
        StorageIdSetIdentity((LPSTORAGE_UNIT)AHCIPort, NULL, NULL, NULL);
    }

    StorageIdEnsure((LPSTORAGE_UNIT)AHCIPort);
}

/***************************************************************************/

/**
 * @brief Initialize an AHCI port and allocate per-port structures.
 *
 * Verifies presence, stops the port, allocates command list/FIS/command table,
 * and performs device identification when possible.
 *
 * @param AHCIPort Port wrapper structure.
 * @param PortNum Port index.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL InitializeAHCIPort(LPAHCI_PORT AHCIPort, U32 PortNum) {
    LPAHCI_HBA_PORT Port = &AHCIState.Base->ports[PortNum];

    AHCIState.Ports[PortNum] = NULL;

    // Check if port is implemented
    if ((AHCIState.PortsImplemented & (1 << PortNum)) == 0) {
        return FALSE;
    }

    // Check if device is present
    U32 ssts = Port->ssts;
    U32 det = ssts & AHCI_PORT_SSTS_DET_MASK;

    if (det == AHCI_PORT_SSTS_DET_NONE) {
        return FALSE;
    }

    if (det == AHCI_PORT_SSTS_DET_PRESENT) {
    } else if (det == AHCI_PORT_SSTS_DET_ESTABLISHED) {
    } else {
    }

    // Stop port
    StopPort(Port);

    // Allocate command list (use regular allocation for now)
    AHCIPort->CommandList = (LPAHCI_CMD_HEADER)KernelHeapAlloc(AHCI_CMD_LIST_SIZE);
    if (AHCIPort->CommandList == NULL) {
        return FALSE;
    }
    MemorySet(AHCIPort->CommandList, 0, AHCI_CMD_LIST_SIZE);

    // Allocate FIS receive area (use regular allocation for now)
    AHCIPort->FISBase = (LPAHCI_FIS)KernelHeapAlloc(AHCI_FIS_SIZE);
    if (AHCIPort->FISBase == NULL) {
        return FALSE;
    }
    MemorySet(AHCIPort->FISBase, 0, AHCI_FIS_SIZE);

    // Allocate command table
    AHCIPort->CommandTable = (LPAHCI_CMD_TBL)KernelHeapAlloc(AHCI_CMD_TBL_SIZE);
    if (AHCIPort->CommandTable == NULL) {
        return FALSE;
    }
    MemorySet(AHCIPort->CommandTable, 0, AHCI_CMD_TBL_SIZE);

    if (!BufferPoolInit(
            &AHCIPort->BounceBufferPool,
            SATA_BOUNCE_BUFFER_BYTES,
            SATA_BOUNCE_BUFFER_OBJECTS_PER_SLAB,
            SATA_BOUNCE_BUFFER_INITIAL_SLABS,
            SATA_POOL_ALLOC_FLAGS,
            TEXT("SataBounceBuffer"))) {
        return FALSE;
    }

    if (!BufferPoolReserve(&AHCIPort->BounceBufferPool, SATA_BOUNCE_BUFFER_MIN_FREE)) {
        BufferPoolDeinit(&AHCIPort->BounceBufferPool);
        return FALSE;
    }

    // Set up port registers with physical addresses for DMA
    PHYSICAL CommandListPhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandList);
    PHYSICAL FISBasePhys = MapLinearToPhysical((LINEAR)AHCIPort->FISBase);
    PHYSICAL CommandTablePhys = MapLinearToPhysical((LINEAR)AHCIPort->CommandTable);

    Port->clb = (U32)(CommandListPhys & 0xFFFFFFFF);
    Port->clbu = 0;
    Port->fb = (U32)(FISBasePhys & 0xFFFFFFFF);
    Port->fbu = 0;
#ifdef __EXOS_64__
    Port->clbu = (U32)((CommandListPhys >> 32) & 0xFFFFFFFF);
    Port->fbu = (U32)((FISBasePhys >> 32) & 0xFFFFFFFF);
#endif

    // Set up command header for slot 0
    AHCIPort->CommandList[0].cfl = sizeof(FIS_REG_H2D) / 4;  // FIS length
    AHCIPort->CommandList[0].prdtl = 1;                      // One PRDT entry
    AHCIPort->CommandList[0].ctba = (U32)(CommandTablePhys & 0xFFFFFFFF);
    AHCIPort->CommandList[0].ctbau = 0;
#ifdef __EXOS_64__
    AHCIPort->CommandList[0].ctbau = (U32)((CommandTablePhys >> 32) & 0xFFFFFFFF);
#endif

    // Store references
    AHCIPort->PortNumber = PortNum;
    AHCIPort->HBAPort = Port;
    AHCIPort->HBAMem = AHCIState.Base;
    AHCIPort->PendingInterrupts = 0;
    AHCIState.Ports[PortNum] = AHCIPort;

    // Clear any pending interrupt sources and keep the port masked. AHCI
    // commands are handled synchronously so INTx lines must stay quiet when
    // other devices (E1000) reuse the same legacy IRQ.
    Port->is = 0xFFFFFFFF;
    Port->ie = 0x0;

    // Reset port
    if (!AHCIPortReset(Port)) {
        return FALSE;
    }

    // Start port
    StartPort(Port);

    // Identify the device (stable ID + hardware strings). The stable ID is
    // built before registration so the synthetic index stays deterministic.
    AHCIIdeIdentifyDevice(AHCIPort);

    // Geometry is kept as fixed CHS for now
    AHCIPort->Geometry.Cylinders = 1024;
    AHCIPort->Geometry.Heads = 16;
    AHCIPort->Geometry.SectorsPerTrack = 63;
    AHCIPort->Geometry.BytesPerSector = SECTOR_SIZE;

    // Attach the generic transfer layer (sector cache + chunking). The
    // transfer limit is bounded by the bounce buffer AHCICommand uses.
    if (!StorageTransferLayerInit((LPSTORAGE_UNIT)AHCIPort, SATA_MAX_DMA_SECTORS, NULL)) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief PCI probe entry for the AHCI driver.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return DF_RETURN_SUCCESS on handled, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static UINT AHCIProbe(UINT Function, UINT Parameter) {
    LPPCI_INFO PciInfo = (LPPCI_INFO)Parameter;

    if (Function != DF_PROBE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Attach detected AHCI controller and initialize state.
 *
 * Allocates PCI device copy, maps ABAR, enumerates ports, and registers interrupts.
 *
 * @param PciDevice Detected PCI device descriptor.
 * @return Heap-allocated PCI device pointer or NULL on failure.
 */
static LPPCI_DEVICE AHCIAttach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    // Allocate a new device structure on kernel heap as required by PCI driver specification
    LPPCI_DEVICE Device = (LPPCI_DEVICE)KernelHeapAlloc(sizeof(PCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    // Copy PCI device information to the heap-allocated structure
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->Next = NULL;
    Device->Prev = NULL;
    Device->References = 1;

    // Check if AHCI is already initialized
    SAFE_USE(AHCIState.Base) {
        return Device;  // Return heap-allocated device but don't reinitialize
    }

    // Store the PCI device for interrupt handling
    AHCIState.Device = Device;

    // Get ABAR (AHCI Base Address Register) from BAR5
    U32 ABAR = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 5);
    if (ABAR == 0) {
        KernelHeapFree(Device);
        return NULL;
    }

    // Verify ABAR is in a reasonable range
    if (ABAR < 0x1000 || ABAR > 0xFFFFF000) {
        KernelHeapFree(Device);
        return NULL;
    }

    // Map AHCI registers using MapIOMemory for MMIO
    // AHCI registers typically need 4KB (0x1000) of space
    LINEAR MappedABAR = MapIOMemory(ABAR, N_4KB);
    if (MappedABAR == 0) {
        KernelHeapFree(Device);
        return NULL;
    }

    AHCIState.Base = (LPAHCI_HBA_MEM)MappedABAR;

    // Enable bus mastering
    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    // Initialize AHCI
    if (InitializeAHCIController() != DF_RETURN_SUCCESS) {
        KernelHeapFree(Device);
        return NULL;
    }

    return Device;
}

/***************************************************************************/

/**
 * @brief Initialize AHCI controller, ports, and interrupt handling.
 *
 * Maps HBA registers, enumerates implemented ports, and leaves interrupts
 * masked for polling mode.
 *
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 InitializeAHCIController(void) {
    if (AHCIState.Base == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (!AHCIState.InterruptRegistered) {
        AHCIRegisterInterrupts();
    }

    MemorySet(AHCIState.Ports, 0, sizeof(AHCIState.Ports));
    AHCIState.PendingPortsMask = 0;

    // Get capabilities
    U32 cap = AHCIState.Base->cap;
    U32 nports = (cap & AHCI_CAP_NP_MASK) + 1;

    // Enable AHCI mode
    AHCIState.Base->ghc |= AHCI_GHC_AE;

    // Get ports implemented
    AHCIState.PortsImplemented = AHCIState.Base->pi;

    // Initialize available ports
    for (U32 i = 0; i < nports && i < AHCI_MAX_PORTS; i++) {
        if (AHCIState.PortsImplemented & (1 << i)) {
            LPAHCI_PORT AHCIPort = NewAHCIPort();

            SAFE_USE(AHCIPort) {
                if (InitializeAHCIPort(AHCIPort, i)) {
                    ListAddItem(GetStorageList(), AHCIPort);
                }
            }
        }
    }

    // Leave global interrupts masked. The storage driver uses polling for
    // command completion, so unmasking the HBA would generate useless INTx
    // storms on shared IRQ lines.
    AHCIState.Base->ghc &= ~AHCI_GHC_IE;
    AHCIState.InterruptEnabled = FALSE;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Issue an AHCI command (read/write) on a port.
 *
 * @param AHCIPort Target port.
 * @param Command ATA command byte.
 * @param LBA Logical block address.
 * @param SectorCount Number of sectors to transfer.
 * @param Buffer Data buffer.
 * @param IsWrite TRUE for write, FALSE for read.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 AHCICommand(LPAHCI_PORT AHCIPort, U8 Command, U32 LBA, U16 SectorCount, LPVOID Buffer, BOOL IsWrite) {
    U32 TransferBytes;
    LPVOID EffectiveBuffer;
    LPVOID BounceRaw;
    LPVOID BounceAligned;
    LINEAR BounceBase;
    U32 BounceSize;

    ProfileCountCall(TEXT("AHCICommand"));

    if (AHCIPort == NULL || Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    TransferBytes = (U32)SectorCount * SECTOR_SIZE;
    EffectiveBuffer = Buffer;
    BounceRaw = NULL;
    BounceAligned = NULL;

    if (TransferBytes <= N_4KB) {
        BounceSize = TransferBytes + N_4KB;
        if (BounceSize > SATA_BOUNCE_BUFFER_BYTES) {
            return DF_RETURN_UNEXPECTED;
        }

        BounceRaw = BufferPoolAcquire(&AHCIPort->BounceBufferPool);
        if (BounceRaw == NULL) {
            return DF_RETURN_UNEXPECTED;
        }

        BounceBase = (LINEAR)BounceRaw;
        BounceAligned = (LPVOID)((BounceBase + (N_4KB - 1)) & ~(N_4KB - 1));
        EffectiveBuffer = BounceAligned;

        if (IsWrite) {
            MemoryCopy(EffectiveBuffer, Buffer, TransferBytes);
        } else {
            MemorySet(EffectiveBuffer, 0, TransferBytes);
        }
    }

    LPAHCI_HBA_PORT Port = AHCIPort->HBAPort;
    if (Port == NULL) {
        if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
        return DF_RETURN_HARDWARE;
    }

    // Wait for port to be ready
    U32 timeout = 1000000;
    while ((Port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        ERROR(TEXT("Port ready timeout tfd=%x"), Port->tfd);
        if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
        return DF_RETURN_TIMEOUT;
    }

    // Clear pending interrupts
    Port->is = 0xFFFFFFFF;

    // Set up command header for slot 0
    LPAHCI_CMD_HEADER cmdheader = &AHCIPort->CommandList[0];
    cmdheader->cfl = sizeof(FIS_REG_H2D) / 4;  // FIS length in DWORDs
    cmdheader->w = IsWrite ? 1 : 0;            // Write flag
    cmdheader->prdtl = 1;                      // One PRDT entry

    // Set up command table
    LPAHCI_CMD_TBL cmdtbl = AHCIPort->CommandTable;
    MemorySet(cmdtbl, 0, sizeof(AHCI_CMD_TBL));

    // Set up FIS
    FIS_REG_H2D* cmdfis = (FIS_REG_H2D*)cmdtbl->cfis;
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;  // Command
    cmdfis->command = Command;
    cmdfis->lba0 = LBA & 0xFF;
    cmdfis->lba1 = (LBA >> 8) & 0xFF;
    cmdfis->lba2 = (LBA >> 16) & 0xFF;
    cmdfis->device = 1 << 6;  // LBA mode
    cmdfis->lba3 = (LBA >> 24) & 0xFF;
    cmdfis->lba4 = 0;  // LBA 32-39 (we're using 32-bit LBA)
    cmdfis->lba5 = 0;  // LBA 40-47
    cmdfis->countl = SectorCount & 0xFF;
    cmdfis->counth = (SectorCount >> 8) & 0xFF;

    // Set up PRDT entry
    PHYSICAL bufferPhys = MapLinearToPhysical((LINEAR)EffectiveBuffer);
    if (bufferPhys == 0) {
        ERROR(TEXT("MapLinearToPhysical failed buffer=%p"), EffectiveBuffer);
        if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
        return DF_RETURN_HARDWARE;
    }

    cmdtbl->prdt_entry[0].dba = (U32)(bufferPhys & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = 0;
#ifdef __EXOS_64__
    cmdtbl->prdt_entry[0].dbau = (U32)((bufferPhys >> 32) & 0xFFFFFFFF);
#endif
    cmdtbl->prdt_entry[0].dbc = (SectorCount * 512) - 1;  // Byte count - 1
    cmdtbl->prdt_entry[0].i = 0;                          // No interrupt on completion for this entry

    // Issue command
    Port->ci = 1;  // Issue command slot 0

    // Wait for completion
    timeout = 1000000;
    while ((Port->ci & 1) && timeout > 0) {
        if (Port->is & AHCI_PORT_IS_TFES) {
            ERROR(TEXT("Task file error ci=%x is=%x tfd=%x"), Port->ci, Port->is, Port->tfd);
            if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
            return DF_RETURN_HARDWARE;
        }
        timeout--;
    }

    if (timeout == 0) {
        ERROR(TEXT("Completion timeout ci=%x is=%x tfd=%x"), Port->ci, Port->is, Port->tfd);
        if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
        return DF_RETURN_TIMEOUT;
    }

    // Check for errors
    if (Port->is & AHCI_PORT_IS_TFES) {
        ERROR(TEXT("Task file error after completion ci=%x is=%x tfd=%x"), Port->ci, Port->is, Port->tfd);
        if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);
        return DF_RETURN_HARDWARE;
    }

    if (!IsWrite && BounceRaw != NULL) {
        MemoryCopy(Buffer, EffectiveBuffer, TransferBytes);
    }

    if (BounceRaw != NULL) BufferPoolRelease(&AHCIPort->BounceBufferPool, BounceRaw);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read sectors from a SATA storage using AHCI.
 *
 * Sectors are grouped into chunks of up to SATA_MAX_DMA_SECTORS and each
 * chunk is served by a single AHCI command. Caching and request merging are
 * handled by the generic transfer layer.
 *
 * @param Control IO control structure describing request.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 Read(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Current;
    U32 Result;

    // Check validity of parameters
    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    // Get the physical storage to which operation applies
    AHCIPort = (LPAHCI_PORT)Control->Storage;
    if (AHCIPort == NULL) return DF_RETURN_BAD_PARAMETER;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_STORAGE) return DF_RETURN_BAD_PARAMETER;

    Current = 0;
    while (Current < Control->NumSectors) {
        U32 ChunkSectors = SATA_MAX_DMA_SECTORS;
        if (ChunkSectors > Control->NumSectors - Current) {
            ChunkSectors = Control->NumSectors - Current;
        }

        Result = AHCICommand(
            AHCIPort,
            ATA_CMD_READ_DMA_EXT,
            Control->SectorLow + Current,
            (U16)ChunkSectors,
            (LPVOID)((U8*)Control->Buffer + Current * SECTOR_SIZE),
            FALSE);
        if (Result != DF_RETURN_SUCCESS) {
            return Result;
        }

        Current += ChunkSectors;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Write sectors to a SATA storage using AHCI.
 *
 * Sectors are grouped into chunks of up to SATA_MAX_DMA_SECTORS and each
 * chunk is written with a single AHCI command. Caching and request merging
 * are handled by the generic transfer layer.
 *
 * @param Control IO control structure describing request.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 Write(LPIOCONTROL Control) {
    LPAHCI_PORT AHCIPort;
    U32 Current;
    U32 Result;

    // Check validity of parameters
    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    // Get the physical storage to which operation applies
    AHCIPort = (LPAHCI_PORT)Control->Storage;
    if (AHCIPort == NULL) return DF_RETURN_BAD_PARAMETER;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_STORAGE) return DF_RETURN_BAD_PARAMETER;

    // Check access permissions
    if (AHCIPort->Access & STORAGE_ACCESS_READONLY) return DF_RETURN_NO_PERMISSION;

    Current = 0;
    while (Current < Control->NumSectors) {
        U32 ChunkSectors = SATA_MAX_DMA_SECTORS;
        if (ChunkSectors > Control->NumSectors - Current) {
            ChunkSectors = Control->NumSectors - Current;
        }

        Result = AHCICommand(
            AHCIPort,
            ATA_CMD_WRITE_DMA_EXT,
            Control->SectorLow + Current,
            (U16)ChunkSectors,
            (LPVOID)((U8*)Control->Buffer + Current * SECTOR_SIZE),
            TRUE);
        if (Result != DF_RETURN_SUCCESS) {
            return Result;
        }

        Current += ChunkSectors;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Retrieve storage information for a SATA device.
 *
 * @param Info Output structure to populate.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static U32 GetInfo(LPSTORAGEINFO Info) {
    LPAHCI_PORT AHCIPort;

    if (Info == NULL) return DF_RETURN_BAD_PARAMETER;

    // Get the physical storage to which operation applies
    AHCIPort = (LPAHCI_PORT)Info->Storage;
    if (AHCIPort == NULL) return DF_RETURN_BAD_PARAMETER;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_STORAGE) return DF_RETURN_BAD_PARAMETER;

    Info->Type = DRIVER_TYPE_SATA_STORAGE;
    Info->Removable = 0;
    Info->BytesPerSector = AHCIPort->Geometry.BytesPerSector;
    Info->NumSectors =
        U64_FromU32(AHCIPort->Geometry.Cylinders * AHCIPort->Geometry.Heads * AHCIPort->Geometry.SectorsPerTrack);
    Info->Access = AHCIPort->Access;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Set access parameters for a SATA storage.
 *
 * @param Access Access parameters to store.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static U32 SetAccess(LPSTORAGEACCESS Access) {
    LPAHCI_PORT AHCIPort;

    if (Access == NULL) return DF_RETURN_BAD_PARAMETER;

    // Get the physical storage to which operation applies
    AHCIPort = (LPAHCI_PORT)Access->Storage;
    if (AHCIPort == NULL) return DF_RETURN_BAD_PARAMETER;

    // Check validity of parameters
    if (AHCIPort->Header.TypeID != KOID_STORAGE) return DF_RETURN_BAD_PARAMETER;

    AHCIPort->Access = Access->Access;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Register AHCI interrupt handlers and enable HBA IRQs if possible.
 *
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL AHCIRegisterInterrupts(void) {
    LPPCI_DEVICE Device = AHCIState.Device;

    if (Device == NULL) {
        return FALSE;
    }

    if (AHCIState.InterruptRegistered) {
        return TRUE;
    }

    U8 LegacyIRQ = Device->Info.IRQLine;
    if (LegacyIRQ == 0xFFU) {
        WARNING(TEXT("Controller reports no legacy IRQ line"));
        return FALSE;
    }

    BOOL Registered = FALSE;

    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        DEVICE_INTERRUPT_REGISTRATION Registration = {
            .Device = (LPDEVICE)Device,
            .LegacyIRQ = LegacyIRQ,
            .TargetCPU = 0,
            .InterruptHandler = AHCIInterruptTopHalf,
            .DeferredCallback = AHCIInterruptBottomHalf,
            .PollCallback = AHCIInterruptPoll,
            .Context = (LPVOID)&AHCIState,
            .Name = Device->Driver ? Device->Driver->Product : TEXT("AHCI"),
        };

        if (DeviceInterruptRegister(&Registration, &AHCIState.InterruptSlot)) {
            AHCIState.InterruptRegistered = TRUE;
            AHCIState.InterruptEnabled = DeviceInterruptSlotIsEnabled(AHCIState.InterruptSlot);
            AHCIState.PendingPortsMask = 0;
            Registered = TRUE;
        } else {
            WARNING(TEXT("Failed to register interrupt slot for IRQ %u"), LegacyIRQ);
            AHCIState.InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        }
    }

    return Registered;
}

/***************************************************************************/

/**
 * @brief Top-half interrupt handler for AHCI.
 *
 * Acknowledges interrupts, records pending ports, and defers work.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 * @return TRUE if handled.
 */
static BOOL AHCIInterruptTopHalf(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    LPAHCI_STATE State = (LPAHCI_STATE)Context;
    if (State == NULL || State->Base == NULL) {
        return FALSE;
    }

    U32 GlobalStatus = State->Base->is;
    if (GlobalStatus == 0U) {
        return FALSE;
    }

    State->Base->is = GlobalStatus;

    BOOL ShouldSignal = FALSE;
    for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
        if ((GlobalStatus & (1U << PortIndex)) == 0U) {
            continue;
        }

        LPAHCI_HBA_PORT HwPort = &State->Base->ports[PortIndex];
        U32 PortStatus = HwPort->is;
        HwPort->is = PortStatus;

        if (PortStatus == 0U) {
            continue;
        }

        LPAHCI_PORT Port = State->Ports[PortIndex];
        if (Port == NULL) {
            continue;
        }

        Port->PendingInterrupts |= PortStatus;
        State->PendingPortsMask |= (1U << PortIndex);
        ShouldSignal = TRUE;
    }

    if (!ShouldSignal) {
        static U32 DATA_SECTION SpuriousCount = 0;
        if (SpuriousCount < 4U) {
        }
        SpuriousCount++;
    }

    return ShouldSignal;
}

/***************************************************************************/

/**
 * @brief Bottom-half handler for AHCI interrupts.
 *
 * Clears per-port interrupts that were latched in the top half.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 */
static void AHCIInterruptBottomHalf(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    LPAHCI_STATE State = (LPAHCI_STATE)Context;
    if (State == NULL) {
        return;
    }

    U32 LocalMask;
    U32 LocalStatus[AHCI_MAX_PORTS];
    MemorySet(LocalStatus, 0, sizeof(LocalStatus));

    {
        U32 Flags;
        SaveFlags(&Flags);
        DisableInterrupts();
        LocalMask = State->PendingPortsMask;
        if (LocalMask != 0U) {
            for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
                LPAHCI_PORT Port = State->Ports[PortIndex];
                if (Port != NULL) {
                    LocalStatus[PortIndex] = (U32)Port->PendingInterrupts;
                    Port->PendingInterrupts = 0;
                }
            }
            State->PendingPortsMask = 0;
        }
        RestoreFlags(&Flags);
    }

    if (LocalMask == 0U) {
        return;
    }

    static U32 DATA_SECTION BottomHalfLogCount = 0;

    for (U32 PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
        if ((LocalMask & (1U << PortIndex)) == 0U) {
            continue;
        }

        U32 PortStatus = LocalStatus[PortIndex];
        LPAHCI_PORT Port = State->Ports[PortIndex];
        if (Port == NULL || PortStatus == 0U) {
            continue;
        }

        SAFE_USE_VALID_ID((LPLISTNODE)Port, KOID_STORAGE) {
            if ((PortStatus & (1U << 30)) != 0U) {
                WARNING(TEXT("Port %u reported task file error (status=%x)"), PortIndex, PortStatus);
            } else if (BottomHalfLogCount < 4U) {
            }
        }

        BottomHalfLogCount++;
    }
}

/***************************************************************************/

/**
 * @brief Poll-mode handler to process pending AHCI interrupts.
 *
 * @param Device Interrupt source device.
 * @param Context AHCI_STATE pointer.
 */
static void AHCIInterruptPoll(LPDEVICE Device, LPVOID Context) {
    UNUSED(Device);

    if (AHCIInterruptTopHalf(Device, Context)) {
        AHCIInterruptBottomHalf(Device, Context);
    }
}

/***************************************************************************/

BOOL AHCIIsInitialized(void) {
    return (AHCIState.Base != NULL);
}

/***************************************************************************/

void AHCIInterruptHandler(void) {
    if (AHCIState.Device == NULL || AHCIState.Base == NULL) {
        return;
    }

    if (AHCIInterruptTopHalf((LPDEVICE)AHCIState.Device, (LPVOID)&AHCIState)) {
        AHCIInterruptBottomHalf((LPDEVICE)AHCIState.Device, (LPVOID)&AHCIState);
    }
}

/***************************************************************************/

/**
 * @brief SATA driver command dispatcher.
 *
 * Handles load/unload, version, storage I/O, and access configuration requests.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver-specific status/value.
 */
UINT SATAStorageCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((SATAStorageDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            SATAStorageDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_UNLOAD:
            if ((SATAStorageDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            SATAStorageDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_STORAGE_RESET:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_STORAGE_READ:
            return Read((LPIOCONTROL)Parameter);
        case DF_STORAGE_WRITE:
            return Write((LPIOCONTROL)Parameter);
        case DF_STORAGE_GETINFO:
            return GetInfo((LPSTORAGEINFO)Parameter);
        case DF_STORAGE_SETACCESS:
            return SetAccess((LPSTORAGEACCESS)Parameter);
        case DF_ENUM_NEXT:
            return SATA_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Parameter);
        case DF_ENUM_PRETTY:
            return SATA_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

static LPCSTR SataDetToString(UINT Det) {
    switch (Det) {
        case AHCI_PORT_SSTS_DET_NONE:
            return TEXT("None");
        case AHCI_PORT_SSTS_DET_PRESENT:
            return TEXT("Present");
        case AHCI_PORT_SSTS_DET_ESTABLISHED:
            return TEXT("Established");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/

static U32 SATA_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) || Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_AHCI_PORT) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    UINT MatchIndex = 0;
    for (UINT PortIndex = 0; PortIndex < AHCI_MAX_PORTS; PortIndex++) {
        if ((AHCIState.PortsImplemented & (1U << PortIndex)) == 0U) {
            continue;
        }

        if (MatchIndex == Next->Query->Index) {
            DRIVER_ENUM_AHCI_PORT Data;
            MemorySet(&Data, 0, sizeof(Data));

            Data.PortNumber = PortIndex;
            Data.PortImplemented = 1;

            if (AHCIState.Base != NULL) {
                LPAHCI_HBA_PORT Port = &AHCIState.Base->ports[PortIndex];
                Data.Ssts = Port->ssts;
                Data.Sig = Port->sig;
            }

            MemorySet(Next->Item, 0, sizeof(DRIVER_ENUM_ITEM));
            Next->Item->Header.Size = sizeof(DRIVER_ENUM_ITEM);
            Next->Item->Header.Version = EXOS_ABI_VERSION;
            Next->Item->Domain = ENUM_DOMAIN_AHCI_PORT;
            Next->Item->Index = Next->Query->Index;
            Next->Item->DataSize = sizeof(Data);
            MemoryCopy(Next->Item->Data, &Data, sizeof(Data));

            Next->Query->Index++;
            return DF_RETURN_SUCCESS;
        }

        MatchIndex++;
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

static U32 SATA_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Pretty->Item->Domain != ENUM_DOMAIN_AHCI_PORT || Pretty->Item->DataSize < sizeof(DRIVER_ENUM_AHCI_PORT)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    const DRIVER_ENUM_AHCI_PORT* Data = (const DRIVER_ENUM_AHCI_PORT*)Pretty->Item->Data;
    UINT Det = Data->Ssts & AHCI_PORT_SSTS_DET_MASK;

    StringPrintFormat(
        Pretty->Buffer,
        TEXT("AHCI Port %u: DET=%s SSTS=%x SIG=%x"),
        Data->PortNumber,
        SataDetToString(Det),
        Data->Ssts,
        Data->Sig);

    return DF_RETURN_SUCCESS;
}
