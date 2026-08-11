
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


    SATA - AHCI Implementation

\************************************************************************/

#ifndef SATA_H_INCLUDED
#define SATA_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "fs/Storage.h"
#include "drivers/bus/PCI.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// AHCI PCI Class Codes

#define AHCI_PCI_CLASS 0x01    // Mass Storage Controller
#define AHCI_PCI_SUBCLASS 0x06 // SATA Controller
#define AHCI_PCI_PROG_IF 0x01  // AHCI Interface

/***************************************************************************/
// AHCI HBA (Host Bus Adapter) Memory Registers

// Forward declaration
typedef struct tag_AHCI_HBA_PORT AHCI_HBA_PORT, *LPAHCI_HBA_PORT;

typedef struct tag_AHCI_HBA_PORT {
    U32 clb;       // 0x00, command list base address, 1K-byte aligned
    U32 clbu;      // 0x04, command list base address upper 32 bits
    U32 fb;        // 0x08, FIS base address, 256-byte aligned
    U32 fbu;       // 0x0C, FIS base address upper 32 bits
    U32 is;        // 0x10, interrupt status
    U32 ie;        // 0x14, interrupt enable
    U32 cmd;       // 0x18, command and status
    U32 rsv0;      // 0x1C, Reserved
    U32 tfd;       // 0x20, task file data
    U32 sig;       // 0x24, signature
    U32 ssts;      // 0x28, SATA status (SCR0:SStatus)
    U32 sctl;      // 0x2C, SATA control (SCR2:SControl)
    U32 serr;      // 0x30, SATA error (SCR1:SError)
    U32 sact;      // 0x34, SATA active (SCR3:SActive)
    U32 ci;        // 0x38, command issue
    U32 sntf;      // 0x3C, SATA notification (SCR4:SNotification)
    U32 fbs;       // 0x40, FIS-based switch control
    U32 rsv1[11];  // 0x44 ~ 0x6F, Reserved
    U32 vendor[4]; // 0x70 ~ 0x7F, vendor specific
} AHCI_HBA_PORT, *LPAHCI_HBA_PORT;

/***************************************************************************/
// AHCI HBA (Host Bus Adapter) Memory Registers

typedef struct tag_AHCI_HBA_MEM {
    // 0x00 - 0x2B, Generic Host Control
    U32 cap;     // 0x00, Host Capabilities
    U32 ghc;     // 0x04, Global Host Control
    U32 is;      // 0x08, Interrupt Status
    U32 pi;      // 0x0C, Ports Implemented
    U32 vs;      // 0x10, Version
    U32 ccc_ctl; // 0x14, Command Completion Coalescing Control
    U32 ccc_pts; // 0x18, Command Completion Coalescing Ports
    U32 em_loc;  // 0x1C, Enclosure Management Location
    U32 em_ctl;  // 0x20, Enclosure Management Control
    U32 cap2;    // 0x24, Host Capabilities Extended
    U32 bohc;    // 0x28, BIOS/OS Handoff Control and Status

    // 0x2C - 0x9F, Reserved
    U8 rsv[0xA0 - 0x2C];

    // 0xA0 - 0xFF, Vendor Specific registers
    U8 vendor[0x100 - 0xA0];

    // 0x100 - 0x10FF, Port control registers (0x80 bytes per port, up to 32 ports)
    AHCI_HBA_PORT ports[32];
} AHCI_HBA_MEM, *LPAHCI_HBA_MEM;

/***************************************************************************/
// AHCI Host Capabilities Register bits

#define AHCI_CAP_S64A (1 << 31)   // Supports 64-bit Addressing
#define AHCI_CAP_SNCQ (1 << 30)   // Supports NCQ
#define AHCI_CAP_SSNTF (1 << 29)  // Supports SNotification Register
#define AHCI_CAP_SMPS (1 << 28)   // Supports Mechanical Presence Switch
#define AHCI_CAP_SSS (1 << 27)    // Supports Staggered Spin-up
#define AHCI_CAP_SALP (1 << 26)   // Supports Aggressive Link Power Management
#define AHCI_CAP_SAL (1 << 25)    // Supports Activity LED
#define AHCI_CAP_SCLO (1 << 24)   // Supports Command List Override
#define AHCI_CAP_ISS_MASK (0xF << 20) // Interface Speed Support
#define AHCI_CAP_SAM (1 << 18)    // Supports AHCI mode only
#define AHCI_CAP_SPM (1 << 17)    // Supports Port Multiplier
#define AHCI_CAP_FBSS (1 << 16)   // FIS-based Switching Supported
#define AHCI_CAP_PMD (1 << 15)    // PIO Multiple DRQ Block
#define AHCI_CAP_SSC (1 << 14)    // Slumber State Capable
#define AHCI_CAP_PSC (1 << 13)    // Partial State Capable
#define AHCI_CAP_NCS_MASK (0x1F << 8) // Number of Command Slots
#define AHCI_CAP_CCCS (1 << 7)    // Command Completion Coalescing Supported
#define AHCI_CAP_EMS (1 << 6)     // Enclosure Management Supported
#define AHCI_CAP_SXS (1 << 5)     // Supports External SATA
#define AHCI_CAP_NP_MASK (0x1F)   // Number of Ports

/***************************************************************************/
// AHCI Global Host Control Register bits

#define AHCI_GHC_AE (1 << 31)     // AHCI Enable
#define AHCI_GHC_MRSM (1 << 2)    // MSI Revert to Single Message
#define AHCI_GHC_IE (1 << 1)      // Interrupt Enable
#define AHCI_GHC_HR (1 << 0)      // HBA Reset

/***************************************************************************/
// AHCI Port Command and Status Register bits

#define AHCI_PORT_CMD_ICC_MASK (0xF << 28) // Interface Communication Control
#define AHCI_PORT_CMD_ICC_ACTIVE (0x1 << 28)
#define AHCI_PORT_CMD_ICC_PARTIAL (0x2 << 28)
#define AHCI_PORT_CMD_ICC_SLUMBER (0x6 << 28)
#define AHCI_PORT_CMD_ASP (1 << 27) // Aggressive Slumber/Partial
#define AHCI_PORT_CMD_ALPE (1 << 26) // Aggressive Link Power Management Enable
#define AHCI_PORT_CMD_DLAE (1 << 25) // Drive LED on ATAPI Enable
#define AHCI_PORT_CMD_ATAPI (1 << 24) // Device is ATAPI
#define AHCI_PORT_CMD_APSTE (1 << 23) // Automatic Partial to Slumber Transitions Enabled
#define AHCI_PORT_CMD_FBSCP (1 << 22) // FIS-based Switching Capable Port
#define AHCI_PORT_CMD_ESP (1 << 21) // External SATA Port
#define AHCI_PORT_CMD_CPD (1 << 20) // Cold Presence Detection
#define AHCI_PORT_CMD_MPSP (1 << 19) // Mechanical Presence Switch Attached to Port
#define AHCI_PORT_CMD_HPCP (1 << 18) // Hot Plug Capable Port
#define AHCI_PORT_CMD_PMA (1 << 17) // Port Multiplier Attached
#define AHCI_PORT_CMD_CPS (1 << 16) // Cold Presence State
#define AHCI_PORT_CMD_CR (1 << 15) // Command List Running
#define AHCI_PORT_CMD_FR (1 << 14) // FIS Receive Running
#define AHCI_PORT_CMD_MPSS (1 << 13) // Mechanical Presence Switch State
#define AHCI_PORT_CMD_CCS_MASK (0x1F << 8) // Current Command Slot
#define AHCI_PORT_CMD_FRE (1 << 4) // FIS Receive Enable
#define AHCI_PORT_CMD_CLO (1 << 3) // Command List Override
#define AHCI_PORT_CMD_POD (1 << 2) // Power On Device
#define AHCI_PORT_CMD_SUD (1 << 1) // Spin-Up Device
#define AHCI_PORT_CMD_ST (1 << 0) // Start

/***************************************************************************/
// AHCI Port SATA Status Register bits

#define AHCI_PORT_SSTS_IPM_MASK (0xF << 8) // Interface Power Management
#define AHCI_PORT_SSTS_IPM_ACTIVE (0x1 << 8)
#define AHCI_PORT_SSTS_IPM_PARTIAL (0x2 << 8)
#define AHCI_PORT_SSTS_IPM_SLUMBER (0x6 << 8)
#define AHCI_PORT_SSTS_SPD_MASK (0xF << 4) // Current Interface Speed
#define AHCI_PORT_SSTS_SPD_GEN1 (0x1 << 4) // 1.5 Gbps
#define AHCI_PORT_SSTS_SPD_GEN2 (0x2 << 4) // 3.0 Gbps
#define AHCI_PORT_SSTS_SPD_GEN3 (0x3 << 4) // 6.0 Gbps
#define AHCI_PORT_SSTS_DET_MASK (0xF) // Device Detection
#define AHCI_PORT_SSTS_DET_NONE (0x0) // No device detected
#define AHCI_PORT_SSTS_DET_PRESENT (0x1) // Device detected but no communication
#define AHCI_PORT_SSTS_DET_ESTABLISHED (0x3) // Device detected and communication established

/***************************************************************************/
// FIS (Frame Information Structure) Types

#define FIS_TYPE_REG_H2D 0x27   // Register FIS - host to device
#define FIS_TYPE_REG_D2H 0x34   // Register FIS - device to host
#define FIS_TYPE_DMA_ACT 0x39   // DMA activate FIS - device to host
#define FIS_TYPE_DMA_SETUP 0x41 // DMA setup FIS - bidirectional
#define FIS_TYPE_DATA 0x46      // Data FIS - bidirectional
#define FIS_TYPE_BIST 0x58      // BIST activate FIS - bidirectional
#define FIS_TYPE_PIO_SETUP 0x5F // PIO setup FIS - device to host
#define FIS_TYPE_DEV_BITS 0xA1  // Set device bits FIS - device to host

/***************************************************************************/
// FIS Structures

typedef struct tag_FIS_REG_H2D {
    // DWORD 0
    U8 fis_type; // FIS_TYPE_REG_H2D

    U8 pmport : 4; // Port multiplier
    U8 rsv0 : 3;   // Reserved
    U8 c : 1;      // 1: Command, 0: Control

    U8 command;  // Command register
    U8 featurel; // Feature register, 7:0

    // DWORD 1
    U8 lba0;   // LBA low register, 7:0
    U8 lba1;   // LBA mid register, 15:8
    U8 lba2;   // LBA high register, 23:16
    U8 device; // Device register

    // DWORD 2
    U8 lba3;     // LBA register, 31:24
    U8 lba4;     // LBA register, 39:32
    U8 lba5;     // LBA register, 47:40
    U8 featureh; // Feature register, 15:8

    // DWORD 3
    U8 countl;  // Count register, 7:0
    U8 counth;  // Count register, 15:8
    U8 icc;     // Isochronous command completion
    U8 control; // Control register

    // DWORD 4
    U8 rsv1[4]; // Reserved
} FIS_REG_H2D, *LPFIS_REG_H2D;

typedef struct tag_FIS_REG_D2H {
    // DWORD 0
    U8 fis_type; // FIS_TYPE_REG_D2H

    U8 pmport : 4; // Port multiplier
    U8 rsv0 : 2;   // Reserved
    U8 i : 1;      // Interrupt bit
    U8 rsv1 : 1;   // Reserved

    U8 status; // Status register
    U8 error;  // Error register

    // DWORD 1
    U8 lba0;   // LBA low register, 7:0
    U8 lba1;   // LBA mid register, 15:8
    U8 lba2;   // LBA high register, 23:16
    U8 device; // Device register

    // DWORD 2
    U8 lba3; // LBA register, 31:24
    U8 lba4; // LBA register, 39:32
    U8 lba5; // LBA register, 47:40
    U8 rsv2; // Reserved

    // DWORD 3
    U8 countl; // Count register, 7:0
    U8 counth; // Count register, 15:8
    U8 rsv3[2]; // Reserved

    // DWORD 4
    U8 rsv4[4]; // Reserved
} FIS_REG_D2H, *LPFIS_REG_D2H;

typedef struct tag_FIS_DATA {
    // DWORD 0
    U8 fis_type; // FIS_TYPE_DATA

    U8 pmport : 4; // Port multiplier
    U8 rsv0 : 4;   // Reserved

    U8 rsv1[2]; // Reserved

    // DWORD 1 ~ N
    U32 data[1]; // Payload
} FIS_DATA, *LPFIS_DATA;

typedef struct tag_FIS_PIO_SETUP {
    // DWORD 0
    U8 fis_type; // FIS_TYPE_PIO_SETUP

    U8 pmport : 4; // Port multiplier
    U8 rsv0 : 1;   // Reserved
    U8 d : 1;      // Data transfer direction, 1 - device to host
    U8 i : 1;      // Interrupt bit
    U8 rsv1 : 1;

    U8 status; // Status register
    U8 error;  // Error register

    // DWORD 1
    U8 lba0;   // LBA low register, 7:0
    U8 lba1;   // LBA mid register, 15:8
    U8 lba2;   // LBA high register, 23:16
    U8 device; // Device register

    // DWORD 2
    U8 lba3; // LBA register, 31:24
    U8 lba4; // LBA register, 39:32
    U8 lba5; // LBA register, 47:40
    U8 rsv2; // Reserved

    // DWORD 3
    U8 countl;     // Count register, 7:0
    U8 counth;     // Count register, 15:8
    U8 rsv3;       // Reserved
    U8 e_status;   // New value of status register

    // DWORD 4
    U16 tc;     // Transfer count
    U8 rsv4[2]; // Reserved
} FIS_PIO_SETUP, *LPFIS_PIO_SETUP;

typedef struct tag_FIS_DMA_SETUP {
    // DWORD 0
    U8 fis_type; // FIS_TYPE_DMA_SETUP

    U8 pmport : 4; // Port multiplier
    U8 rsv0 : 1;   // Reserved
    U8 d : 1;      // Data transfer direction, 1 - device to host
    U8 i : 1;      // Interrupt bit
    U8 a : 1;      // Auto-activate. Specifies if DMA Activate FIS is needed

    U8 rsved[2]; // Reserved

    // DWORD 1&2
    U64 DMAbufferID; // DMA Buffer Identifier. Used to Identify DMA buffer in host memory.

    // DWORD 3
    U32 rsvd; // More reserved

    // DWORD 4
    U32 DMAbufOffset; // Byte offset into buffer. First 2 bits must be 0

    // DWORD 5
    U32 TransferCount; // Number of bytes to transfer. Bit 0 must be 0

    // DWORD 6
    U32 resvd; // Reserved
} FIS_DMA_SETUP, *LPFIS_DMA_SETUP;

/***************************************************************************/
// AHCI Command Header

typedef struct tag_AHCI_CMD_HEADER {
    // DW0
    U8 cfl : 5; // Command FIS length in DWORDS, 2 ~ 16
    U8 a : 1;   // ATAPI
    U8 w : 1;   // Write, 1: H2D, 0: D2H
    U8 p : 1;   // Prefetchable

    U8 r : 1;    // Reset
    U8 b : 1;    // BIST
    U8 c : 1;    // Clear busy upon R_OK
    U8 rsv0 : 1; // Reserved
    U8 pmp : 4;  // Port multiplier port

    U16 prdtl; // Physical region descriptor table length in entries

    // DW1
    volatile U32 prdbc; // Physical region descriptor byte count transferred

    // DW2, 3
    U32 ctba;  // Command table descriptor area base address
    U32 ctbau; // Command table descriptor area base address upper 32 bits

    // DW4 - 7
    U32 rsv1[4]; // Reserved
} AHCI_CMD_HEADER, *LPAHCI_CMD_HEADER;

/***************************************************************************/
// AHCI Physical Region Descriptor Table Entry

typedef struct tag_AHCI_PRDT_ENTRY {
    U32 dba;  // Data base address
    U32 dbau; // Data base address upper 32 bits
    U32 rsv0; // Reserved

    // DW3
    U32 dbc : 22; // Byte count, 4M max
    U32 rsv1 : 9; // Reserved
    U32 i : 1;    // Interrupt on completion
} AHCI_PRDT_ENTRY, *LPAHCI_PRDT_ENTRY;

/***************************************************************************/
// AHCI Command Table

typedef struct tag_AHCI_CMD_TBL {
    // 0x00
    U8 cfis[64]; // Command FIS

    // 0x40
    U8 acmd[16]; // ATAPI command, 12 or 16 bytes

    // 0x50
    U8 rsv[48]; // Reserved

    // 0x80
    AHCI_PRDT_ENTRY prdt_entry[1]; // Physical region descriptor table entries, 0 ~ 65535
} AHCI_CMD_TBL, *LPAHCI_CMD_TBL;

/***************************************************************************/
// AHCI Received FIS Structure

typedef struct tag_AHCI_FIS {
    // 0x00
    FIS_DMA_SETUP dsfis; // DMA Setup FIS
    U8 pad0[4];

    // 0x20
    FIS_PIO_SETUP psfis; // PIO Setup FIS
    U8 pad1[12];

    // 0x40
    FIS_REG_D2H rfis; // Register – Device to Host FIS
    U8 pad2[4];

    // 0x58
    U8 sdbfis[8]; // Set Device Bit FIS

    // 0x60
    U8 ufis[64]; // Unknown FIS

    // 0xA0
    U8 rsv[0x100 - 0xA0]; // Reserved
} AHCI_FIS, *LPAHCI_FIS;

/***************************************************************************/
// ATA Device Status Register bits

#define ATA_DEV_BUSY 0x80 // Device busy
#define ATA_DEV_DRQ 0x08  // Data request

/***************************************************************************/
// AHCI Port Interrupt Status bits

#define AHCI_PORT_IS_TFES (1 << 30) // Task File Error Status

/***************************************************************************/
// SATA Commands

#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_IDENTIFY 0xEC

/***************************************************************************/

LPDRIVER AHCIPCIGetDriver(void);
LPDRIVER SATAStorageGetDriver(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // SATA_H_INCLUDED
