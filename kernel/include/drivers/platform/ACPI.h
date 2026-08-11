
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


    ACPI (Advanced Configuration and Power Interface)

\************************************************************************/

#ifndef ACPI_H_INCLUDED
#define ACPI_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// ACPI address space identifiers

#define ACPI_ADDRESS_SPACE_SYSTEM_MEMORY 0x00
#define ACPI_ADDRESS_SPACE_SYSTEM_IO 0x01

/***************************************************************************/
// Generic Address Structure

typedef struct tag_ACPI_GENERIC_ADDRESS {
    U8 AddressSpaceId;     // Address space where the register resides
    U8 RegisterBitWidth;   // Size in bits of the register
    U8 RegisterBitOffset;  // Bit offset within the register
    U8 AccessSize;         // Access size (BYTE, WORD, DWORD, QWORD)
    U32 AddressLow;        // Low 32 bits of the address
    U32 AddressHigh;       // High 32 bits of the address
} ACPI_GENERIC_ADDRESS, *LPACPI_GENERIC_ADDRESS;

/***************************************************************************/
// ACPI Table Header

typedef struct tag_ACPI_TABLE_HEADER {
    U8 Signature[4];          // Table signature
    U32 Length;               // Length of table in bytes
    U8 Revision;              // ACPI Specification minor version number
    U8 Checksum;              // To make sum of entire table == 0
    U8 OemId[6];              // OEM identification
    U8 OemTableId[8];         // OEM table identification
    U32 OemRevision;          // OEM revision number
    U8 AslCompilerId[4];      // ASL compiler vendor ID
    U32 AslCompilerRevision;  // ASL compiler version
} ACPI_TABLE_HEADER, *LPACPI_TABLE_HEADER;

/***************************************************************************/
// Root System Description Pointer (RSDP)

typedef struct tag_ACPI_RSDP {
    U8 Signature[8];      // "RSD PTR "
    U8 Checksum;          // Checksum of fields defined in ACPI 1.0
    U8 OemId[6];          // OEM identification
    U8 Revision;          // 0 for ACPI 1.0, 2 for ACPI 2.0+
    U32 RsdtAddress;      // 32-bit physical address of RSDT
    U32 Length;           // Length of table in bytes (ACPI 2.0+)
    U64 XsdtAddress;      // 64-bit physical address of XSDT (ACPI 2.0+)
    U8 ExtendedChecksum;  // Checksum of entire table (ACPI 2.0+)
    U8 Reserved[3];       // Reserved field
} ACPI_RSDP, *LPACPI_RSDP;

/***************************************************************************/
// Root System Description Table (RSDT)

typedef struct tag_ACPI_RSDT {
    ACPI_TABLE_HEADER Header;  // Standard ACPI table header
    U32 Entry[];               // Array of pointers to other ACPI tables
} ACPI_RSDT, *LPACPI_RSDT;

/***************************************************************************/
// Extended System Description Table (XSDT)

typedef struct tag_ACPI_XSDT {
    ACPI_TABLE_HEADER Header;  // Standard ACPI table header
    U64 Entry[];               // Array of 64-bit pointers to other ACPI tables
} ACPI_XSDT, *LPACPI_XSDT;

/***************************************************************************/
// Multiple APIC Description Table (MADT)

typedef struct tag_ACPI_MADT {
    ACPI_TABLE_HEADER Header;  // Standard ACPI table header
    U32 LocalApicAddress;      // Physical address of Local APIC (32-bit per spec)
    U32 Flags;                 // Multiple APIC flags
    U8 InterruptController[];  // Array of interrupt controller entries
} ACPI_MADT, *LPACPI_MADT;

/***************************************************************************/
// MADT Flags

#define ACPI_MADT_PCAT_COMPAT 0x00000001  // PC-AT Compatibility

/***************************************************************************/
// MADT Local APIC flags

#define ACPI_MADT_LOCAL_APIC_ENABLED 0x00000001  // Processor is enabled

/***************************************************************************/
// MADT Entry Types

#define ACPI_MADT_TYPE_LOCAL_APIC 0x00
#define ACPI_MADT_TYPE_IO_APIC 0x01
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE 0x02
#define ACPI_MADT_TYPE_NMI_SOURCE 0x03
#define ACPI_MADT_TYPE_LOCAL_APIC_NMI 0x04
#define ACPI_MADT_TYPE_LOCAL_APIC_OVERRIDE 0x05
#define ACPI_MADT_TYPE_IO_SAPIC 0x06
#define ACPI_MADT_TYPE_LOCAL_SAPIC 0x07
#define ACPI_MADT_TYPE_INTERRUPT_SOURCE 0x08

/***************************************************************************/
// MADT Entry Header

typedef struct tag_ACPI_MADT_ENTRY_HEADER {
    U8 Type;    // Entry type
    U8 Length;  // Length of this entry
} ACPI_MADT_ENTRY_HEADER, *LPACPI_MADT_ENTRY_HEADER;

/***************************************************************************/
// Local APIC Structure

typedef struct tag_ACPI_MADT_LOCAL_APIC {
    ACPI_MADT_ENTRY_HEADER Header;
    U8 ProcessorId;  // ACPI processor ID
    U8 ApicId;       // Processor's local APIC ID
    U32 Flags;       // Local APIC flags
} ACPI_MADT_LOCAL_APIC, *LPACPI_MADT_LOCAL_APIC;

/***************************************************************************/
// I/O APIC Structure

typedef struct tag_ACPI_MADT_IO_APIC {
    ACPI_MADT_ENTRY_HEADER Header;
    U8 IoApicId;                    // I/O APIC ID
    U8 Reserved;                    // Reserved (must be zero)
    U32 IoApicAddress;              // Physical address of I/O APIC (32-bit per spec)
    U32 GlobalSystemInterruptBase;  // Global system interrupt where this I/O APIC's interrupts start
} ACPI_MADT_IO_APIC, *LPACPI_MADT_IO_APIC;

/***************************************************************************/
// Interrupt Source Override Structure

typedef struct tag_ACPI_MADT_INTERRUPT_OVERRIDE {
    ACPI_MADT_ENTRY_HEADER Header;
    U8 Bus;                     // Bus that is overridden (0 = ISA)
    U8 Source;                  // Bus-relative interrupt source (IRQ)
    U32 GlobalSystemInterrupt;  // Global system interrupt that this bus-relative interrupt source will signal
    U16 Flags;                  // MPS INTI flags
} ACPI_MADT_INTERRUPT_OVERRIDE, *LPACPI_MADT_INTERRUPT_OVERRIDE;

/***************************************************************************/
// Local APIC NMI Structure

typedef struct tag_ACPI_MADT_LOCAL_APIC_NMI {
    ACPI_MADT_ENTRY_HEADER Header;
    U8 ProcessorId;    // ACPI processor ID (0xFF means all processors)
    U16 Flags;         // MPS INTI flags
    U8 LocalApicLint;  // Local APIC interrupt input LINTn to which NMI is connected
} ACPI_MADT_LOCAL_APIC_NMI, *LPACPI_MADT_LOCAL_APIC_NMI;

/***************************************************************************/
// ACPI Configuration

typedef struct tag_ACPI_CONFIG {
    BOOL Valid;                  // TRUE if ACPI is available and parsed
    BOOL UseLocalApic;           // TRUE if Local APIC should be used
    BOOL UseIoApic;              // TRUE if I/O APIC should be used
    BOOL S5Available;            // TRUE if S5 sleep type values were parsed
    PHYSICAL LocalApicAddress;   // Physical address of Local APIC
    U32 IoApicCount;             // Number of I/O APICs found
    U32 LocalApicCount;          // Number of Local APICs found
    U32 InterruptOverrideCount;  // Number of interrupt source overrides
    U8 SlpTypS5A;                // Sleep type for S5 (PM1a)
    U8 SlpTypS5B;                // Sleep type for S5 (PM1b)
} ACPI_CONFIG, *LPACPI_CONFIG;

/***************************************************************************/
// I/O APIC Information

typedef struct tag_IO_APIC_INFO {
    U8 IoApicId;                    // I/O APIC ID
    PHYSICAL IoApicAddress;         // Physical address of I/O APIC
    U32 GlobalSystemInterruptBase;  // Global system interrupt base
    U32 MaxRedirectionEntry;        // Maximum redirection entry (read from I/O APIC)
} IO_APIC_INFO, *LPIO_APIC_INFO;

/***************************************************************************/
// Local APIC Information

typedef struct tag_LOCAL_APIC_INFO {
    U8 ProcessorId;  // ACPI processor ID
    U8 ApicId;       // Local APIC ID
    U32 Flags;       // Local APIC flags
} LOCAL_APIC_INFO, *LPLOCAL_APIC_INFO;

/***************************************************************************/
// Interrupt Override Information

typedef struct tag_INTERRUPT_OVERRIDE_INFO {
    U8 Bus;                     // Bus (usually 0 for ISA)
    U8 Source;                  // Source IRQ
    U32 GlobalSystemInterrupt;  // Target global system interrupt
    U16 Flags;                  // MPS INTI flags
} INTERRUPT_OVERRIDE_INFO, *LPINTERRUPT_OVERRIDE_INFO;

/***************************************************************************/
// Fixed ACPI Description Table (FACP/FADT)

typedef struct tag_ACPI_FADT {
    ACPI_TABLE_HEADER Header;            // Standard ACPI table header
    U32 FirmwareCtrl;                    // 32-bit physical address of FACS
    U32 Dsdt;                            // 32-bit physical address of DSDT
    U8 Reserved1;                        // System Interrupt Model (ACPI 1.0)
    U8 PreferredPowerManagementProfile;  // Conveys preferred power management profile
    U16 SciInterrupt;                    // System vector of SCI interrupt
    U32 SmiCommandPort;                  // 32-bit Port address of SMI command port
    U8 AcpiEnable;                       // Value to write to SMI_CMD to enable ACPI
    U8 AcpiDisable;                      // Value to write to SMI_CMD to disable ACPI
    U8 S4BiosReq;                        // Value to write to SMI_CMD to enter S4BIOS state
    U8 PstateControl;                    // Processor performance state control
    U32 Pm1aEventBlock;                  // 32-bit Port address of Power Mgt 1a Event Reg Blk
    U32 Pm1bEventBlock;                  // 32-bit Port address of Power Mgt 1b Event Reg Blk
    U32 Pm1aControlBlock;                // 32-bit Port address of Power Mgt 1a Control Reg Blk
    U32 Pm1bControlBlock;                // 32-bit Port address of Power Mgt 1b Control Reg Blk
    U32 Pm2ControlBlock;                 // 32-bit Port address of Power Mgt 2 Control Reg Blk
    U32 PmTimerBlock;                    // 32-bit Port address of Power Mgt Timer Ctrl Reg Blk
    U32 Gpe0Block;                       // 32-bit Port address of General Purpose Event 0 Reg Blk
    U32 Gpe1Block;                       // 32-bit Port address of General Purpose Event 1 Reg Blk
    U8 Pm1EventLength;                   // Byte Length of Port described by Pm1aEventBlock
    U8 Pm1ControlLength;                 // Byte Length of Port described by Pm1aControlBlock
    U8 Pm2ControlLength;                 // Byte Length of Port described by Pm2ControlBlock
    U8 PmTimerLength;                    // Byte Length of Port described by PmTimerBlock
    U8 Gpe0Length;                       // Byte Length of Port described by Gpe0Block
    U8 Gpe1Length;                       // Byte Length of Port described by Gpe1Block
    U8 Gpe1Base;                         // Offset in GPE number space where GPE1 events start
    U8 CstateControl;                    // Support for the _CST object and C States change notification
    U16 WorstC2Latency;                  // Worst case HW latency to enter/exit C2 state
    U16 WorstC3Latency;                  // Worst case HW latency to enter/exit C3 state
    U16 FlushSize;                       // Processor memory cache line width in units of 1024 bytes
    U16 FlushStride;                     // Processor's memory cache line stride
    U8 DutyOffset;                       // Processor duty cycle index in processor P_CNT reg
    U8 DutyWidth;                        // Processor duty cycle value bit width in P_CNT register
    U8 DayAlarm;                         // Index to day-of-month alarm in RTC CMOS RAM
    U8 MonthAlarm;                       // Index to month alarm in RTC CMOS RAM
    U8 Century;                          // Index to century in RTC CMOS RAM
    U16 BootArchitectureFlags;           // IA-PC Boot Architecture Flags
    U8 Reserved2;                        // Reserved field
    U32 Flags;                           // Fixed feature flags
    ACPI_GENERIC_ADDRESS ResetReg;       // Reset register descriptor (ACPI 2.0+)
    U8 ResetValue;                       // Value to write to reset register
    U8 Reserved3[3];                     // Reserved field
} ACPI_FADT, *LPACPI_FADT;

/***************************************************************************/
// ACPI Function Prototypes

// Initialize ACPI and parse tables
BOOL InitializeACPI(void);

// Find and validate RSDP
LPACPI_RSDP FindRSDP(void);

// Validate ACPI table checksum
BOOL ValidateACPITableChecksum(LPACPI_TABLE_HEADER Table);

// Find an ACPI table by signature
LPACPI_TABLE_HEADER FindACPITable(LPCSTR Signature);

// Parse MADT (Multiple APIC Description Table)
BOOL ParseMADT(void);

// Get ACPI configuration
LPACPI_CONFIG GetACPIConfig(void);

// Get I/O APIC information
LPIO_APIC_INFO GetIOApicInfo(U32 Index);

// Get Local APIC information
LPLOCAL_APIC_INFO GetLocalApicInfo(U32 Index);

// Get interrupt override information
LPINTERRUPT_OVERRIDE_INFO GetInterruptOverrideInfo(U32 Index);

// Map an interrupt using override table
U32 MapInterrupt(U8 IRQ);

// Release ACPI resources
void ACPIShutdown(void);

// Power off the system using ACPI
void ACPIPowerOff(void);

// Reboot the system using ACPI
void ACPIReboot(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // ACPI_H_INCLUDED
