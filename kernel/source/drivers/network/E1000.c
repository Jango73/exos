
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


    E1000

\************************************************************************/

#include "drivers/network/E1000.h"

#include "Base.h"
#include "core/Driver.h"
#include "core/Kernel.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "sync/DeferredWork.h"
#include "drivers/interrupts/InterruptController.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/Network.h"
#include "network/NetworkManager.h"
#include "drivers/bus/PCI.h"
#include "text/CoreString.h"
#include "User.h"

/************************************************************************/

/*
    RX & TX Descriptor Rings (E1000) - Example with 128 entries each
    -----------------------------------------------------------------
    Both rings are arrays of fixed-size descriptors (16 bytes), aligned and DMA-visible.
    The NIC and driver use RDH/RDT (RX) or TDH/TDT (TX) to coordinate ownership.

    =================================================================
    RECEIVE RING (RX) - hardware writes, driver reads
    =================================================================

        +--------------------------------------------------+
        |                                                  |
        v                                                  |
    +---------+    +---------+    +---------+    +---------+
    | Desc 0  | -> | Desc 1  | -> | Desc 2  | -> |  ...     |
    +---------+    +---------+    +---------+    +---------+
       ^                                ^
       |                                |
    RDH (Head)                      RDT (Tail)

    - RDH (Receive Descriptor Head):
        * Maintained by NIC.
        * Points to next descriptor NIC will fill with a received frame.
    - RDT (Receive Descriptor Tail):
        * Maintained by driver.
        * Points to last descriptor available to NIC.
        * Driver advances after processing a descriptor.

    Flow:
        1. NIC writes packet into RDH's buffer, sets DD (Descriptor Done).
        2. Driver polls/IRQ, processes data, clears DD.
        3. Driver advances RDT to give descriptor back to NIC.
        4. Wraps around modulo RX_DESC_COUNT.

    If RDH == RDT:
        Ring is FULL → NIC drops incoming packets.

    =================================================================
    TRANSMIT RING (TX) - driver writes, hardware reads
    =================================================================

        +--------------------------------------------------+
        |                                                  |
        v                                                  |
    +---------+    +---------+    +---------+    +---------+
    | Desc 0  | -> | Desc 1  | -> | Desc 2  | -> |  ...     |
    +---------+    +---------+    +---------+    +---------+
       ^                                ^
       |                                |
    TDH (Head)                      TDT (Tail)

    - TDH (Transmit Descriptor Head):
        * Maintained by NIC.
        * Points to next descriptor NIC will send.
    - TDT (Transmit Descriptor Tail):
        * Maintained by driver.
        * Points to next free descriptor for the driver to fill.
        * Driver advances after writing a packet.

    Flow:
        1. Driver writes packet buffer addr/len into TDT's descriptor.
        2. Driver sets CMD bits (EOP, IFCS, RS).
        3. Driver advances TDT to hand descriptor to NIC.
        4. NIC sends packet, sets DD in status.
        5. Driver checks DD to reclaim descriptor.

    If (TDT + 1) % TX_DESC_COUNT == TDH:
        Ring is FULL → driver must wait before sending more.
*/

/************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

#define E1000_ReadReg32(Base, Off) (*(volatile U32 *)((U8 *)(Base) + (Off)))
#define E1000_WriteReg32(Base, Off, Val) (*(volatile U32 *)((U8 *)(Base) + (Off)) = (U32)(Val))

typedef struct tag_E1000DEVICE E1000DEVICE, *LPE1000DEVICE;

#pragma pack(push, 1)

struct tag_E1000DEVICE {
    PCI_DEVICE_FIELDS

    // MMIO mapping
    LINEAR MmioBase;
    U32 MmioSize;

    // MAC address
    U8 Mac[6];

    // RX ring
    DMA_BUFFER RxRingBuffer;
    U32 RxRingCount;
    U32 RxHead;
    U32 RxTail;

    // TX ring
    DMA_BUFFER TxRingBuffer;
    U32 TxRingCount;
    U32 TxHead;
    U32 TxTail;

    // Pooled DMA areas (one big allocation each)
    DMA_BUFFER RxBufferPool;
    DMA_BUFFER TxBufferPool;

    // RX callback (set via DF_NT_SETRXCB)
    NT_RXCB RxCallback;
    LPVOID RxUserData;

    // Interrupt bookkeeping
    U8 InterruptSlot;
    BOOL InterruptRegistered;
    BOOL InterruptArmed;
    U32 InterruptTraceCount;
    U32 AckTraceCount;
};

#pragma pack(pop)

static UINT E1000Commands(UINT Function, UINT Param);
static BOOL E1000_EnableInterrupts(LPE1000DEVICE Device, U8 LegacyIRQ, U8 TargetCPU);
static BOOL E1000_DisableInterrupts(LPE1000DEVICE Device, U8 LegacyIRQ);
static U32 E1000_OnEnableInterrupts(DEVICE_INTERRUPT_CONFIG *Config);
static U32 E1000_OnDisableInterrupts(DEVICE_INTERRUPT_CONFIG *Config);
static BOOL E1000_AcknowledgeInterrupt(LPE1000DEVICE Device, U32 *Cause);
static BOOL E1000_InterruptTopHalf(LPDEVICE Device, LPVOID Context);
static void E1000_DeferredRoutine(LPDEVICE Device, LPVOID Context);
static void E1000_PollRoutine(LPDEVICE Device, LPVOID Context);
static U32 E1000_ReceivePoll(LPE1000DEVICE Device);
static void E1000_ReleaseDMAResources(LPE1000DEVICE Device);

/************************************************************************/
// Globals and PCI match table

static DRIVER_MATCH E1000_MatchTable[] = {E1000_MATCH_DEFAULT};
static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDev);

PCI_DRIVER DATA_SECTION E1000Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "E1000 (82540EM)",
    .Alias = "e1000",
    .Command = E1000Commands,
    .Matches = E1000_MatchTable,
    .MatchCount = sizeof(E1000_MatchTable) / sizeof(E1000_MatchTable[0]),
    .Attach = E1000_Attach};

/************************************************************************/

/**
 * @brief Retrieves the E1000 PCI driver descriptor.
 * @return Pointer to the E1000 PCI driver.
 */
LPDRIVER E1000GetDriver(void) {
    return (LPDRIVER)&E1000Driver;
}

/************************************************************************/
// Small busy wait

/**
 * @brief Busy-wait loop used for short hardware delays.
 *
 * @param Iterations Number of iterations to spin.
 */
static void E1000_Delay(UINT Iterations) {
    volatile UINT Index;
    for (Index = 0; Index < Iterations; Index++) {
        asm volatile("nop");
    }
}

/************************************************************************/
// EEPROM read and MAC

/**
 * @brief Read a 16-bit word from the device EEPROM.
 * @param Device Target E1000 device.
 * @param Address Word offset within the EEPROM.
 * @return Word value read from EEPROM.
 */
static U16 E1000_EepromReadWord(LPE1000DEVICE Device, U32 Address) {
    U32 Value = 0;
    U32 Count = 0;

    E1000_WriteReg32(Device->MmioBase, E1000_REG_EERD, ((Address & MAX_U8) << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

    while (Count < E1000_RESET_TIMEOUT_ITER) {
        Value = E1000_ReadReg32(Device->MmioBase, E1000_REG_EERD);
        if (Value & E1000_EERD_DONE) {
            // Successfully read, return the data
            return (U16)((Value >> E1000_EERD_DATA_SHIFT) & 0xFFFF);
        }
        Count++;
    }

    // EEPROM read failed/timed out - log error and return 0 as safe default
    ERROR(TEXT("EEPROM read timeout at address %u after %u iterations"), Address, Count);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the MAC address from hardware or EEPROM.
 * @param Device Target E1000 device.
 */
static void E1000_ReadMac(LPE1000DEVICE Device) {
    U32 low = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAL0);
    U32 high = E1000_ReadReg32(Device->MmioBase, E1000_REG_RAH0);


    // Check if RAL/RAH contain a valid, non-zero MAC (AV bit set, not all zeros, not broadcast)
    if ((high & (1u << 31)) && (low != 0) && !((low == MAX_U32) && ((high & 0xFFFF) == 0xFFFF))) {
        // Additional check: ensure it's not a multicast address (first bit of first byte)
        U8 first_byte = (low >> 0) & MAX_U8;
        if ((first_byte & 0x01) == 0) {
            // Valid unicast MAC address found in hardware registers
            Device->Mac[0] = (low >> 0) & MAX_U8;
            Device->Mac[1] = (low >> 8) & MAX_U8;
            Device->Mac[2] = (low >> 16) & MAX_U8;
            Device->Mac[3] = (low >> 24) & MAX_U8;
            Device->Mac[4] = (high >> 0) & MAX_U8;
            Device->Mac[5] = (high >> 8) & MAX_U8;
            return;
        }
    }

    // Fallback: read permanent MAC from EEPROM then program RAL/RAH
    U16 w0 = E1000_EepromReadWord(Device, 0);
    U16 w1 = E1000_EepromReadWord(Device, 1);
    U16 w2 = E1000_EepromReadWord(Device, 2);


    if (w0 == 0 && w1 == 0 && w2 == 0) {
        // EEPROM is empty, use fallback MAC address
        Device->Mac[0] = 0x52;
        Device->Mac[1] = 0x54;
        Device->Mac[2] = 0x00;
        Device->Mac[3] = 0x12;
        Device->Mac[4] = 0x34;
        Device->Mac[5] = 0x56;
    } else {
        Device->Mac[0] = (U8)(w0 & MAX_U8);
        Device->Mac[1] = (U8)(w0 >> 8);
        Device->Mac[2] = (U8)(w1 & MAX_U8);
        Device->Mac[3] = (U8)(w1 >> 8);
        Device->Mac[4] = (U8)(w2 & MAX_U8);
        Device->Mac[5] = (U8)(w2 >> 8);
    }

    low = (U32)Device->Mac[0] | ((U32)Device->Mac[1] << 8) | ((U32)Device->Mac[2] << 16) | ((U32)Device->Mac[3] << 24);
    high = (U32)Device->Mac[4] | ((U32)Device->Mac[5] << 8) | (1u << 31);  // Set AV (Address Valid) bit
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, low);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, high);

}

/************************************************************************/
// Core HW ops

/**
 * @brief Reset the network controller and configure basic settings.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_Reset(LPE1000DEVICE Device) {
    U32 Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl | E1000_CTRL_RST);

    U32 Count = 0;
    while (Count < E1000_RESET_TIMEOUT_ITER) {
        Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
        if ((Ctrl & E1000_CTRL_RST) == 0) break;
        Count++;
    }

    Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
    Ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl);
    // Disable interrupts for polling path
    E1000_WriteReg32(Device->MmioBase, E1000_REG_IMC, MAX_U32);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Setup MAC address filters for packet reception.
 * @param Device Target E1000 device.
 */
static void E1000_SetupMacFilters(LPE1000DEVICE Device) {

    // Program our MAC address into Receive Address Register 0
    U32 RAL = (U32)Device->Mac[0] |
              ((U32)Device->Mac[1] << 8) |
              ((U32)Device->Mac[2] << 16) |
              ((U32)Device->Mac[3] << 24);

    U32 RAH = (U32)Device->Mac[4] |
              ((U32)Device->Mac[5] << 8) |
              (1U << 31);  // Address Valid bit

    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, RAL);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, RAH);


    // Clear multicast table array (accept no multicast by default)
    for (U32 i = 0; i < 128; i++) {
        E1000_WriteReg32(Device->MmioBase, E1000_REG_MTA + (i * 4), 0);
    }

}

/************************************************************************/
// RX/TX rings setup

/**
 * @brief Release DMA resources allocated by the E1000 driver.
 * @param Device Target E1000 device.
 */
static void E1000_ReleaseDMAResources(LPE1000DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    DMABufferRelease(&Device->TxBufferPool);
    DMABufferRelease(&Device->RxBufferPool);
    DMABufferRelease(&Device->TxRingBuffer);
    DMABufferRelease(&Device->RxRingBuffer);
}

/************************************************************************/

/**
 * @brief Initialize the receive descriptor ring and buffers.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_SetupReceive(LPE1000DEVICE Device) {
    UINT Index;
    LPE1000_RXDESC Ring;

    Device->RxRingCount = E1000_RX_DESC_COUNT;

    if (!DMABufferAllocate(
            &Device->RxRingBuffer,
            Device->RxRingCount * sizeof(E1000_RXDESC),
            FALSE,
            TEXT("E1000RxRing"))) {
        ERROR(TEXT("RX ring allocation failed"));
        return FALSE;
    }

    if (!DMABufferAllocate(
            &Device->RxBufferPool,
            Device->RxRingCount * PAGE_SIZE,
            FALSE,
            TEXT("E1000RxPool"))) {
        ERROR(TEXT("RX pool allocation failed"));
        return FALSE;
    }

    Ring = (LPE1000_RXDESC)Device->RxRingBuffer.LinearBase;
    for (Index = 0; Index < Device->RxRingCount; Index++) {
        PHYSICAL BufferPhys = DMABufferGetIndexedPhysical(&Device->RxBufferPool, Index, PAGE_SIZE);
        LINEAR BufferLinear = DMABufferGetIndexedLinear(&Device->RxBufferPool, Index, PAGE_SIZE);

        if (BufferPhys == 0 || BufferLinear == 0) {
            ERROR(TEXT("RX pool address lookup failed at %u"), Index);
            return FALSE;
        }

        if ((BufferPhys & 0xF) != 0) {
            ERROR(TEXT("Invalid/unaligned buffer physical address %p at index %u"),
                  BufferPhys,
                  Index);
            return FALSE;
        }

        Ring[Index].BufferAddrLow = (U32)(BufferPhys & MAX_U32);
        Ring[Index].BufferAddrHigh = 0;
        Ring[Index].Length = 0;
        Ring[Index].Checksum = 0;
        Ring[Index].Status = 0;
        Ring[Index].Errors = 0;
        Ring[Index].Special = 0;

        if (Index < 3) {
            DEBUG(TEXT("RX[%u]: PhysAddr=%x Linear=%x (aligned=%s)"),
                  Index,
                  (U32)BufferPhys,
                  (U32)BufferLinear,
                  ((BufferPhys & 0xF) == 0) ? TEXT("YES") : TEXT("NO"));
        }
    }

    if ((DMABufferGetPhysical(&Device->RxRingBuffer, 0) & 0xF) != 0) {
        ERROR(TEXT("Descriptor ring not 16-byte aligned: %p"),
              DMABufferGetPhysical(&Device->RxRingBuffer, 0));
        return FALSE;
    }

    // Then program NIC registers
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAL, (U32)(DMABufferGetPhysical(&Device->RxRingBuffer, 0) & MAX_U32));
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDBAH, 0);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDLEN, Device->RxRingCount * sizeof(E1000_RXDESC));

    // Initialize head and tail pointers
    // CRITICAL: RDT must point to the last available descriptor for HW to use
    Device->RxHead = 0;
    Device->RxTail = Device->RxRingCount - 1;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDH, 0);
    // Setting RDT to (count-1) tells HW all descriptors 0..count-1 are available
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, Device->RxTail);
    // CRITICAL: Some QEMU versions require TCTL to be set before RCTL for proper link establishment
    // Set TCTL first with basic TX configuration
    U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
               (E1000_TCTL_CT_DEFAULT << E1000_TCTL_CT_SHIFT) |
               (E1000_TCTL_COLD_DEFAULT << E1000_TCTL_COLD_SHIFT);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TCTL, Tctl);

    {
        // Force promiscuous mode to capture all packets
        U32 Rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RCTL, Rctl);

        // Add small delay to let hardware stabilize
        E1000_Delay(100);

        // QEMU E1000 compatibility: Force link up and configure TIPG

        // Force link up without full device reset
        U32 Ctrl = E1000_ReadReg32(Device->MmioBase, E1000_REG_CTRL);
        Ctrl |= E1000_CTRL_SLU | E1000_CTRL_FD;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_CTRL, Ctrl);

        // QEMU-specific TIPG configuration for proper packet timing
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TIPG, E1000_TIPG_QEMU_COMPAT);
    }

    // Clear Multicast Table Array (MTA) - set all to 0 for unicast-only mode
    for (U32 i = 0; i < 128; i += 4) {
        E1000_WriteReg32(Device->MmioBase, E1000_REG_MTA + i, 0);
    }

    // Set MAC address in Receive Address Register (RAL0/RAH0)
    U32 RalValue = (U32)Device->Mac[0] | ((U32)Device->Mac[1] << 8) | ((U32)Device->Mac[2] << 16) | ((U32)Device->Mac[3] << 24);
    U32 RahValue = (U32)Device->Mac[4] | ((U32)Device->Mac[5] << 8) | (1 << 31); // AV=1 (Address Valid)
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAL0, RalValue);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_RAH0, RahValue);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the transmit descriptor ring and buffers.
 * @param Device Target E1000 device.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL E1000_SetupTransmit(LPE1000DEVICE Device) {
    U32 Index;
    LPE1000_TXDESC Ring;

    Device->TxRingCount = E1000_TX_DESC_COUNT;

    if (!DMABufferAllocate(
            &Device->TxRingBuffer,
            Device->TxRingCount * sizeof(E1000_TXDESC),
            FALSE,
            TEXT("E1000TxRing"))) {
        ERROR(TEXT("TX ring allocation failed"));
        return FALSE;
    }

    if (!DMABufferAllocate(
            &Device->TxBufferPool,
            Device->TxRingCount * PAGE_SIZE,
            FALSE,
            TEXT("E1000TxPool"))) {
        ERROR(TEXT("TX pool allocation failed"));
        return FALSE;
    }

    Ring = (LPE1000_TXDESC)Device->TxRingBuffer.LinearBase;
    for (Index = 0; Index < Device->TxRingCount; Index++) {
        PHYSICAL BufferPhys = DMABufferGetIndexedPhysical(&Device->TxBufferPool, Index, PAGE_SIZE);
        if (BufferPhys == 0) {
            ERROR(TEXT("TX pool phys lookup failed at %u"), Index);
            return FALSE;
        }

        if ((BufferPhys & 0xF) != 0) {
            ERROR(TEXT("Invalid/unaligned TX buffer physical address %p at index %u"),
                  BufferPhys,
                  Index);
            return FALSE;
        }

        Ring[Index].BufferAddrLow = (U32)(BufferPhys & MAX_U32);
        Ring[Index].BufferAddrHigh = 0;
        Ring[Index].Length = 0;
        Ring[Index].CSO = 0;
        Ring[Index].CMD = 0;
        Ring[Index].STA = E1000_TX_STA_DD;
        Ring[Index].CSS = 0;
        Ring[Index].Special = 0;
    }

    if ((DMABufferGetPhysical(&Device->TxRingBuffer, 0) & 0xF) != 0) {
        ERROR(TEXT("TX descriptor ring not 16-byte aligned: %p"),
              DMABufferGetPhysical(&Device->TxRingBuffer, 0));
        return FALSE;
    }

    // Program NIC registers
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAL, (U32)(DMABufferGetPhysical(&Device->TxRingBuffer, 0) & MAX_U32));
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDBAH, 0);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDLEN, Device->TxRingCount * sizeof(E1000_TXDESC));

    // Initialize head and tail pointers
    Device->TxHead = 0;
    Device->TxTail = 0;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDH, Device->TxHead);
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, Device->TxTail);

    // Enable TX
    {
        U32 Tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (E1000_TCTL_CT_DEFAULT << E1000_TCTL_CT_SHIFT) |
                   (E1000_TCTL_COLD_DEFAULT << E1000_TCTL_COLD_SHIFT);
        E1000_WriteReg32(Device->MmioBase, E1000_REG_TCTL, Tctl);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDev PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE E1000_Attach(LPPCI_DEVICE PciDevice) {
    LPE1000DEVICE Device = (LPE1000DEVICE)KernelHeapAlloc(sizeof(E1000DEVICE));
    if (Device == NULL) return NULL;

    MemorySet(Device, 0, sizeof(E1000DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->InterruptRegistered = FALSE;
    Device->InterruptArmed = FALSE;


    U32 Bar0Phys = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);


    if (Bar0Phys == NULL || Bar0Size == 0) {
        ERROR(TEXT("Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }


    Device->MmioBase = MapIOMemory(Bar0Phys, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == NULL) {
        ERROR(TEXT("MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }


    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!E1000_Reset(Device)) {
        ERROR(TEXT("Reset failed"));
        KernelHeapFree(Device);
        return NULL;
    }


    E1000_ReadMac(Device);

    // Setup MAC address filters
    E1000_SetupMacFilters(Device);

    if (!E1000_SetupReceive(Device)) {
        ERROR(TEXT("RX setup failed"));
        if (Device->MmioBase) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        }
        E1000_ReleaseDMAResources(Device);
        KernelHeapFree(Device);
        return NULL;
    }


    if (!E1000_SetupTransmit(Device)) {
        ERROR(TEXT("TX setup failed"));
        E1000_ReleaseDMAResources(Device);
        if (Device->MmioBase) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        }
        KernelHeapFree(Device);
        return NULL;
    }

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/
// Interrupt control

/**
 * @brief Register and arm device interrupts (or configure polling).
 *
 * @param Device Target E1000 device.
 * @param LegacyIRQ Optional legacy IRQ line (MAX_U8 to auto-select).
 * @param TargetCPU CPU index for interrupt routing.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL E1000_EnableInterrupts(LPE1000DEVICE Device, U8 LegacyIRQ, U8 TargetCPU) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        if (Device->MmioBase == NULL) {
            WARNING(TEXT("MMIO base is NULL"));
            return FALSE;
        }

        if (LegacyIRQ == MAX_U8) {
            LegacyIRQ = Device->Info.IRQLine;
        }

        if (LegacyIRQ == MAX_U8) {
            WARNING(TEXT("No valid IRQ line available"));
            return FALSE;
        }

        DEVICE_INTERRUPT_REGISTRATION Registration = {
            .Device = (LPDEVICE)Device,
            .LegacyIRQ = LegacyIRQ,
            .TargetCPU = TargetCPU,
            .InterruptHandler = E1000_InterruptTopHalf,
            .DeferredCallback = E1000_DeferredRoutine,
            .PollCallback = E1000_PollRoutine,
            .Context = Device,
            .Name = Device->Driver ? Device->Driver->Product : TEXT("E1000"),
        };

        if (!DeviceInterruptRegister(&Registration, &Device->InterruptSlot)) {
            WARNING(TEXT("Failed to register device interrupt"));
            Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
            Device->InterruptRegistered = FALSE;
            Device->InterruptArmed = FALSE;
            return FALSE;
        }

        Device->InterruptRegistered = TRUE;
        Device->InterruptArmed = DeviceInterruptSlotIsEnabled(Device->InterruptSlot);
        Device->InterruptTraceCount = 0;
        Device->AckTraceCount = 0;

        if (Device->InterruptArmed) {
            // Clear any pending causes and apply the default mask
            E1000_WriteReg32(Device->MmioBase, E1000_REG_IMC, MAX_U32);
            E1000_ReadReg32(Device->MmioBase, E1000_REG_ICR);

            if (!DeferredWorkIsPollingMode()) {
                E1000_WriteReg32(Device->MmioBase, E1000_REG_IMS, E1000_DEFAULT_INTERRUPT_MASK);
            }
        } else {
        }
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Mask and unregister device interrupts.
 *
 * @param Device Target E1000 device.
 * @param LegacyIRQ Optional legacy IRQ line (MAX_U8 to auto-select).
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL E1000_DisableInterrupts(LPE1000DEVICE Device, U8 LegacyIRQ) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        if (Device->MmioBase == NULL) {
            WARNING(TEXT("MMIO base is NULL"));
            return FALSE;
        }

        E1000_WriteReg32(Device->MmioBase, E1000_REG_IMC, MAX_U32);
        E1000_ReadReg32(Device->MmioBase, E1000_REG_ICR);

        if (LegacyIRQ == MAX_U8) {
            LegacyIRQ = Device->Info.IRQLine;
        }

        if (Device->InterruptRegistered) {
            DeviceInterruptUnregister(Device->InterruptSlot);
            Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
            Device->InterruptRegistered = FALSE;
            Device->InterruptArmed = FALSE;
        } else if (LegacyIRQ != MAX_U8) {
            DisableDeviceInterrupt(LegacyIRQ);
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Read and acknowledge the interrupt cause.
 *
 * @param Device Target E1000 device.
 * @param Cause Optional output for the cause register value.
 * @return TRUE if a cause was present, FALSE otherwise.
 */
static BOOL E1000_AcknowledgeInterrupt(LPE1000DEVICE Device, U32 *Cause) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        if (Device->MmioBase == NULL) {
            return FALSE;
        }

        U32 InterruptCause = E1000_ReadReg32(Device->MmioBase, E1000_REG_ICR);
        if (Cause != NULL) {
            *Cause = InterruptCause;
        }

        Device->AckTraceCount++;
        if (Device->AckTraceCount <= E1000_ACK_TRACE_LIMIT) {
            WARNING(TEXT("Cause=%x Armed=%s Polling=%s"),
                    InterruptCause,
                    Device->InterruptArmed ? TEXT("YES") : TEXT("NO"),
                    DeferredWorkIsPollingMode() ? TEXT("YES") : TEXT("NO"));
        }

        if (InterruptCause == 0U) {
            if (Device->AckTraceCount <= E1000_ACK_TRACE_LIMIT) {
                WARNING(TEXT("No pending interrupt cause"));
            }
            return FALSE;
        }

        if (Device->InterruptArmed) {
            if (DeferredWorkIsPollingMode()) {
                if (Device->AckTraceCount <= E1000_ACK_TRACE_LIMIT) {
                    WARNING(TEXT("Polling mode - masking interrupts (IMC=%x)"),
                            MAX_U32);
                }
                E1000_WriteReg32(Device->MmioBase, E1000_REG_IMC, MAX_U32);
            } else {
                if (Device->AckTraceCount <= E1000_ACK_TRACE_LIMIT) {
                    WARNING(TEXT("Re-arming interrupts with mask=%x"),
                            E1000_DEFAULT_INTERRUPT_MASK);
                }
                E1000_WriteReg32(Device->MmioBase, E1000_REG_IMS, E1000_DEFAULT_INTERRUPT_MASK);
            }
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Interrupt top-half handler.
 *
 * Acknowledges interrupts and determines if deferred work is needed.
 *
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (E1000DEVICE).
 * @return TRUE if interrupt was relevant and should schedule bottom half.
 */
static BOOL E1000_InterruptTopHalf(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPE1000DEVICE Device = (LPE1000DEVICE)Context;
    Device->InterruptTraceCount++;
    U32 Cause = 0;

    if (!E1000_AcknowledgeInterrupt(Device, &Cause)) {
        if (Device->InterruptTraceCount <= E1000_INTERRUPT_TRACE_LIMIT) {
            WARNING(TEXT("No cause reported (trace=%u)"),
                    Device->InterruptTraceCount);
        }
        return FALSE;
    }

    if (Device->InterruptTraceCount <= E1000_INTERRUPT_TRACE_LIMIT) {
        WARNING(TEXT("Cause=%x RelevantMask=%x"),
                Cause,
                (E1000_INT_RXT0 | E1000_INT_RXO | E1000_INT_RXDMT0 | E1000_INT_LSC));
    }

    if ((Cause & (E1000_INT_RXT0 | E1000_INT_RXO | E1000_INT_RXDMT0 | E1000_INT_LSC)) == 0) {
        if (Device->InterruptTraceCount <= E1000_INTERRUPT_TRACE_LIMIT) {
            WARNING(TEXT("Ignored cause=%x (no relevant bits)"), Cause);
        }
        return FALSE;
    }

    if ((Cause & E1000_INT_LSC) != 0) {
    }

    if ((Cause & E1000_INT_RXO) != 0) {
        WARNING(TEXT("RX overrun detected (cause=%x)"), Cause);
    }

    if (Device->InterruptTraceCount <= E1000_INTERRUPT_TRACE_LIMIT) {
        WARNING(TEXT("Scheduling deferred work for cause=%x"), Cause);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Deferred (bottom-half) routine for processing RX and maintenance.
 *
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (E1000DEVICE).
 */
static void E1000_DeferredRoutine(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPE1000DEVICE Device = (LPE1000DEVICE)Context;

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        E1000_ReceivePoll(Device);

        LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Device->RxUserData;
        SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
            NetworkManager_MaintenanceTick(NetContext);
        }
    }
}

/************************************************************************/

/**
 * @brief Polling routine when running without interrupts.
 *
 * @param DevicePointer Device pointer from polling context.
 * @param Context Driver context (E1000DEVICE).
 */
static void E1000_PollRoutine(LPDEVICE DevicePointer, LPVOID Context) {
    E1000_DeferredRoutine(DevicePointer, Context);
}

/************************************************************************/
// Receive/Transmit operations

/**
 * @brief Send a frame using the transmit ring.
 * @param Device Target E1000 device.
 * @param Data Pointer to frame data.
 * @param Length Length of frame in bytes.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_TransmitSend(LPE1000DEVICE Device, const U8 *Data, U32 Length) {
    if (Length == 0 || Length > E1000_TX_BUF_SIZE) return DF_RETURN_BAD_PARAMETER;


    U32 Index = Device->TxTail;
    LPE1000_TXDESC Ring = (LPE1000_TXDESC)Device->TxRingBuffer.LinearBase;
    LINEAR BufferLinear = DMABufferGetIndexedLinear(&Device->TxBufferPool, Index, PAGE_SIZE);

    if (BufferLinear == 0) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    // Copy into pre-allocated TX buffer
    MemoryCopy((LPVOID)BufferLinear, (LPVOID)Data, Length);

    Ring[Index].Length = (U16)Length;
    Ring[Index].CMD = (E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS);
    Ring[Index].STA = 0;

    // Advance tail
    U32 NewTail = (Index + 1) % Device->TxRingCount;
    Device->TxTail = NewTail;
    E1000_WriteReg32(Device->MmioBase, E1000_REG_TDT, NewTail);

    // Simple spin for DD
    U32 Wait = 0;
    while (((Ring[Index].STA & E1000_TX_STA_DD) == 0) && (Wait++ < E1000_TX_TIMEOUT_ITER)) {
    }

    if (Wait >= E1000_TX_TIMEOUT_ITER) {
        ERROR(TEXT("TX timeout - packet transmission failed"));
        return DF_RETURN_NT_TX_FAIL;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Poll the receive ring for incoming frames.
 * @param Device Target E1000 device.
 * @return DF_RETURN_SUCCESS after processing frames.
 */
static U32 E1000_ReceivePoll(LPE1000DEVICE Device) {
    LPE1000_RXDESC Ring = (LPE1000_RXDESC)Device->RxRingBuffer.LinearBase;
    U32 Count = 0;
    U32 MaxIterations = Device->RxRingCount * 2; // Safety limit: twice the ring size
    U32 ConsecutiveEmptyChecks = 0;

    while (Count < MaxIterations) {
        U32 NextIndex = (Device->RxHead) % Device->RxRingCount;
        U8 Status = Ring[NextIndex].Status;

        if ((Status & E1000_RX_STA_DD) == 0) {
            ConsecutiveEmptyChecks++;

            // If we've checked multiple times with no new packets, break to prevent spinning
            if (ConsecutiveEmptyChecks >= 3) {

                // No data available - show RX register state every 100 polls
                static U32 DATA_SECTION PollCount = 0;
                if ((PollCount++ % 100) == 0) {
                    DEBUG(TEXT("RDH=%x RDT=%x RCTL=%x"),
                          E1000_ReadReg32(Device->MmioBase, E1000_REG_RDH),
                          E1000_ReadReg32(Device->MmioBase, E1000_REG_RDT),
                          E1000_ReadReg32(Device->MmioBase, E1000_REG_RCTL));
                }
                break;
            }

            // Small delay to let hardware potentially update descriptor
            volatile U32 delay;
            for (delay = 0; delay < 10; delay++) {
                asm volatile("nop");
            }
            continue;
        }

        // Reset consecutive empty checks since we found a packet
        ConsecutiveEmptyChecks = 0;

        if ((Status & E1000_RX_STA_EOP) != 0) {
            U16 Length = Ring[NextIndex].Length;
            const U8 *Frame = (const U8 *)DMABufferGetIndexedLinear(&Device->RxBufferPool, NextIndex, PAGE_SIZE);

            if (Frame != NULL && Device->RxCallback) {
                Device->RxCallback(Frame, (U32)Length, Device->RxUserData);
            } else {
            }
        }

        // Advance head
        Device->RxHead = (NextIndex + 1) % Device->RxRingCount;

        // RDT must point to the last descriptor that the hardware can use
        // Make the processed descriptor available again by updating RDT to point to it
        Device->RxTail = NextIndex;
        E1000_WriteReg32(Device->MmioBase, E1000_REG_RDT, NextIndex);

        // Clear descriptor status AFTER updating RDT to avoid race condition
        Ring[NextIndex].Status = 0;

        Count++;
    }

    if (Count >= MaxIterations) {
        WARNING(TEXT("Hit maximum iteration limit (%u), potential infinite loop prevented"), MaxIterations);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/
// PCI-level helpers (per-function)

/**
 * @brief Verify PCI information matches supported hardware.
 * @param PciInfo PCI configuration to probe.
 * @return DF_RETURN_SUCCESS if supported, otherwise DF_RETURN_NOT_IMPLEMENTED.
 */
static U32 E1000_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo->VendorID != E1000_VENDOR_INTEL) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->DeviceID != E1000_DEVICE_82540EM) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) return DF_RETURN_NOT_IMPLEMENTED;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/
// Network DF_* helpers (per-function)

/**
 * @brief Enable device interrupts via network stack hook.
 *
 * @param Config Interrupt configuration parameters.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnEnableInterrupts(DEVICE_INTERRUPT_CONFIG *Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPE1000DEVICE Device = (LPE1000DEVICE)Config->Device;

    if (!E1000_EnableInterrupts(Device, Config->LegacyIRQ, Config->TargetCPU)) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    Config->VectorSlot = Device->InterruptSlot;
    Config->InterruptEnabled = Device->InterruptArmed;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Disable device interrupts via network stack hook.
 *
 * @param Config Interrupt configuration parameters.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnDisableInterrupts(DEVICE_INTERRUPT_CONFIG *Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPE1000DEVICE Device = (LPE1000DEVICE)Config->Device;

    if (!E1000_DisableInterrupts(Device, Config->LegacyIRQ)) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reset callback invoked by network stack.
 *
 * @param Reset Reset parameters.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_UNEXPECTED on failure.
 */
static U32 E1000_OnReset(const NETWORK_RESET *Reset) {
    if (Reset == NULL || Reset->Device == NULL) return DF_RETURN_BAD_PARAMETER;
    return E1000_Reset((LPE1000DEVICE)Reset->Device) ? DF_RETURN_SUCCESS : DF_RETURN_UNEXPECTED;
}

/************************************************************************/

/**
 * @brief Fill NETWORK_INFO structure with device state.
 * @param Get Query parameters and output buffer.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnGetInfo(const NETWORK_GET_INFO *Get) {
    if (Get == NULL || Get->Device == NULL || Get->Info == NULL) return DF_RETURN_BAD_PARAMETER;
    LPE1000DEVICE Device = (LPE1000DEVICE)Get->Device;
    U32 Status = E1000_ReadReg32(Device->MmioBase, E1000_REG_STATUS);

    Get->Info->MAC[0] = Device->Mac[0];
    Get->Info->MAC[1] = Device->Mac[1];
    Get->Info->MAC[2] = Device->Mac[2];
    Get->Info->MAC[3] = Device->Mac[3];
    Get->Info->MAC[4] = Device->Mac[4];
    Get->Info->MAC[5] = Device->Mac[5];

    Get->Info->LinkUp = (Status & E1000_STATUS_LU) ? 1 : 0;
    Get->Info->SpeedMbps = E1000_LINK_SPEED_MBPS;
    Get->Info->DuplexFull = (Status & E1000_STATUS_FD) ? 1 : 0;
    Get->Info->MTU = E1000_DEFAULT_MTU;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Register a callback for received frames.
 * @param Set Parameters including callback pointer.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnSetReceiveCallback(const NETWORK_SET_RX_CB *Set) {
    if (Set == NULL || Set->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    LPE1000DEVICE Device = (LPE1000DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    Device->RxUserData = Set->UserData;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Send frame through network stack interface.
 * @param Send Parameters describing frame to send.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnSend(const NETWORK_SEND *Send) {
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    U32 result = E1000_TransmitSend((LPE1000DEVICE)Send->Device, Send->Data, Send->Length);
    return result;
}

/************************************************************************/

/**
 * @brief Poll device for received frames through network stack interface.
 * @param Poll Poll parameters.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 E1000_OnPoll(const NETWORK_POLL *Poll) {
    if (Poll == NULL || Poll->Device == NULL) return DF_RETURN_BAD_PARAMETER;
    return E1000_ReceivePoll((LPE1000DEVICE)Poll->Device);
}

/************************************************************************/
// Driver meta helpers

/**
 * @brief Driver load callback.
 * @return DF_RETURN_SUCCESS.
 */
static U32 E1000_OnLoad(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Driver unload callback.
 * @return DF_RETURN_SUCCESS.
 */
static U32 E1000_OnUnload(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Retrieve driver version encoded with MAKE_VERSION.
 * @return Encoded version number.
 */
static U32 E1000_OnGetVersion(void) { return MAKE_VERSION(VER_MAJOR, VER_MINOR); }

/************************************************************************/

/**
 * @brief Report driver capabilities bitmask.
 * @return Capability flags, zero if none.
 */
static U32 E1000_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Return last implemented DF_* function.
 * @return Function identifier used for iteration.
 */
static U32 E1000_OnGetLastFunc(void) { return DF_DEV_DISABLE_INTERRUPT; }

/************************************************************************/
// Driver entry

/**
 * @brief Central dispatch for all driver functions.
 * @param Function Identifier of requested driver operation.
 * @param Param Optional pointer to parameters.
 * @return DF_RETURN_* code depending on operation.
 */
static UINT E1000Commands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return E1000_OnLoad();
        case DF_UNLOAD:
            return E1000_OnUnload();
        case DF_GET_VERSION:
            return E1000_OnGetVersion();
        case DF_GET_CAPS:
            return E1000_OnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return E1000_OnGetLastFunc();

        // PCI binding
        case DF_PROBE:
            return E1000_OnProbe((const PCI_INFO *)(LPVOID)Param);

        // Network DF_* API
        case DF_NT_RESET:
            return E1000_OnReset((const NETWORK_RESET *)(LPVOID)Param);
        case DF_NT_GETINFO:
            return E1000_OnGetInfo((const NETWORK_GET_INFO *)(LPVOID)Param);
        case DF_NT_SETRXCB:
            return E1000_OnSetReceiveCallback((const NETWORK_SET_RX_CB *)(LPVOID)Param);
        case DF_DEV_ENABLE_INTERRUPT:
            return E1000_OnEnableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Param);
        case DF_DEV_DISABLE_INTERRUPT:
            return E1000_OnDisableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Param);
        case DF_NT_SEND:
            return E1000_OnSend((const NETWORK_SEND *)(LPVOID)Param);
        case DF_NT_POLL:
            return E1000_OnPoll((const NETWORK_POLL *)(LPVOID)Param);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
