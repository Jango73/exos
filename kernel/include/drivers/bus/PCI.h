
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


    PCI

\************************************************************************/

#ifndef PCI_H_INCLUDED
#define PCI_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "core/Device.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Common PCI constants and helpers

#define PCI_MAX_BUS 256
#define PCI_MAX_DEV 32
#define PCI_MAX_FUNC 8

#define PCI_ANY_ID 0xFFFF
#define PCI_ANY_CLASS 0xFF

// Config space offsets (Type 0 header)
#define PCI_CFG_VENDOR_ID 0x00   /* U16 */
#define PCI_CFG_DEVICE_ID 0x02   /* U16 */
#define PCI_CFG_COMMAND 0x04     /* U16 */
#define PCI_CFG_STATUS 0x06      /* U16 */
#define PCI_CFG_REVISION 0x08    /* U8  */
#define PCI_CFG_PROG_IF 0x09     /* U8  */
#define PCI_CFG_SUBCLASS 0x0A    /* U8  */
#define PCI_CFG_BASECLASS 0x0B   /* U8  */
#define PCI_CFG_CACHELINE 0x0C   /* U8  */
#define PCI_CFG_LAT_TIMER 0x0D   /* U8  */
#define PCI_CFG_HEADER_TYPE 0x0E /* U8  */
#define PCI_CFG_BIST 0x0F        /* U8  */
#define PCI_CFG_BAR0 0x10        /* U32 */
#define PCI_CFG_BAR1 0x14        /* U32 */
#define PCI_CFG_BAR2 0x18        /* U32 */
#define PCI_CFG_BAR3 0x1C        /* U32 */
#define PCI_CFG_BAR4 0x20        /* U32 */
#define PCI_CFG_BAR5 0x24        /* U32 */
#define PCI_CFG_CAP_PTR 0x34     /* U8  */
#define PCI_CFG_IRQ_LINE 0x3C    /* U8  */
#define PCI_CFG_IRQ_PIN 0x3D     /* U8  */

// Command bits
#define PCI_CMD_IO 0x0001
#define PCI_CMD_MEM 0x0002
#define PCI_CMD_BUSMASTER 0x0004
#define PCI_CMD_INT_DISABLE 0x0400

// BAR (Base Address Register) decoding
#define PCI_BAR_IO_MASK 0xFFFFFFFCU
#define PCI_BAR_MEM_MASK 0xFFFFFFF0U
#define PCI_BAR_IS_IO(bar) (((bar)&0x1U) != 0)
#define PCI_BAR_IS_MEM(bar) (!PCI_BAR_IS_IO(bar))

// Header type
#define PCI_HEADER_TYPE_MASK 0x7F
#define PCI_HEADER_MULTI_FN 0x80

// Capability list
#define PCI_CAP_ID_PM 0x01
#define PCI_CAP_ID_AGP 0x02
#define PCI_CAP_ID_VPD 0x03
#define PCI_CAP_ID_MSI 0x05
#define PCI_CAP_ID_PCIX 0x07
#define PCI_CAP_ID_HT 0x08
#define PCI_CAP_ID_VENDOR 0x09
#define PCI_CAP_ID_DEBUGPORT 0x0A
#define PCI_CAP_ID_HOTPLUG 0x0C
#define PCI_CAP_ID_PCIe 0x10
#define PCI_CAP_ID_MSIX 0x11
#define PCI_CAP_ID_SATA 0x12
#define PCI_CAP_ID_AF 0x13
#define PCI_CAP_ID_EA 0x14
#define PCI_CAP_ID_FPB 0x15

#define PCI_MAX_CAPABILITIES 48

// Base classes (subset)
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_DISPLAY 0x03

// Network subclasses
#define PCI_SUBCLASS_ETHERNET 0x00

/***************************************************************************/
// Matching and driver model (PCI layer)

// Matching rule for a PCI driver. Any field set to PCI_ANY_* is a wildcard.
typedef struct tag_DRIVER_MATCH {
    U16 VendorID; /* 0x8086, 0x10EC, ... or PCI_ANY_ID */
    U16 DeviceID; /* specific device id or PCI_ANY_ID   */
    U8 BaseClass; /* e.g., PCI_CLASS_NETWORK or PCI_ANY_CLASS */
    U8 SubClass;  /* e.g., PCI_SUBCLASS_ETHERNET or PCI_ANY_CLASS */
    U8 ProgIF;    /* usually PCI_ANY_CLASS unless needed */
} DRIVER_MATCH, *LPDRIVER_MATCH;

// Minimal snapshot of a PCI function for binding and init.
typedef struct tag_PCI_INFO {
    U8 Bus;
    U8 Dev;
    U8 Func;

    U16 VendorID;
    U16 DeviceID;

    U8 BaseClass;
    U8 SubClass;
    U8 ProgIF;
    U8 Revision;

    // Raw BAR values as read from config space (unmasked).
    U32 BAR[6];

    // Legacy INTx line (0xFF if none/unknown). MSI/MSI-X handled separately.
    U8 IRQLine;
    U8 IRQLegacyPin; /* INTA=1..INTD=4 or 0 if none */
} PCI_INFO, *LPPCI_INFO;

// Runtime description of a PCI device
#define PCI_DEVICE_FIELDS \
    DEVICE_FIELDS         \
    PCI_INFO Info;        \
    U32 BARPhys[6];       \
    volatile LPVOID BARMapped[6];

typedef struct tag_PCI_DEVICE {
    PCI_DEVICE_FIELDS
} PCI_DEVICE, *LPPCI_DEVICE;

// A PCI-aware driver: extends the generic DRIVER with a match table.
typedef struct tag_PCI_DRIVER {
    LISTNODE_FIELDS
    DRIVER_FIELDS
    const DRIVER_MATCH* Matches;  // array of match entries
    U32 MatchCount;               // number of entries
    LPPCI_DEVICE (*Attach)(LPPCI_DEVICE PciDevice);
} PCI_DRIVER, *LPPCI_DRIVER;

/***************************************************************************/
// Public API of the PCI bus manager
// (Implementation provided by the PCI subsystem; drivers do not implement)

// Register a PCI-aware driver. The driver remains owned by caller (static).
void PCI_RegisterDriver(LPPCI_DRIVER drv);

/* Scan the entire PCI hierarchy (bus/dev/func). For each device:
    - Build PCI_INFO / PCI_DEVICE
    - Try registered PCI_DRIVERs (Matches then DF_PROBE)
    - Call the driver's Attach callback on the first driver that accepts the device
*/
void PCI_ScanBus(void);

/* Basic config space accessors (Type 1 cycles via 0xCF8/0xCFC or equivalent). */
U32 PCI_Read32(U8 bus, U8 dev, U8 func, U16 off);
void PCI_Write32(U8 bus, U8 dev, U8 func, U16 off, U32 value);

/* Convenience helpers (optional; may be stubs if not needed). */
U16 PCI_Read16(U8 bus, U8 dev, U8 func, U16 off);
U8 PCI_Read8(U8 bus, U8 dev, U8 func, U16 off);
void PCI_Write16(U8 bus, U8 dev, U8 func, U16 off, U16 value);
void PCI_Write8(U8 bus, U8 dev, U8 func, U16 off, U8 value);

/* Enable/disable Bus Mastering bit in COMMAND register. Returns previous COMMAND. */
U16 PCI_EnableBusMaster(U8 bus, U8 dev, U8 func, BOOL enable);

/* BAR utilities: returns decoded physical base and size. Size is computed by
    writing 0xFFFFFFFF to the BAR then restoring, per PCI spec. */
U32 PCI_GetBARBase(U8 bus, U8 dev, U8 func, U8 barIndex);
U32 PCI_GetBARSize(U8 bus, U8 dev, U8 func, U8 barIndex);

/* Capability traversal: returns offset of the first capability with given ID,
    or 0 if not found (0 is not a valid cap pointer when capabilities are present). */
U8 PCI_FindCapability(U8 bus, U8 dev, U8 func, U8 capId);

/* Capability traversal: scans the capability list of a function. OffsetArray is
    filled with the config-space offset of each capability found (caller provides
    storage for at least MaxCount entries) and the number of capabilities found is
    returned (0 when the function has no capability list). */
U8 PCI_ScanCapabilities(U8 bus, U8 dev, U8 func, U8* OffsetArray, U8 MaxCount);

/***************************************************************************/

#pragma pack(pop)

#endif  // PCI_H_INCLUDED
