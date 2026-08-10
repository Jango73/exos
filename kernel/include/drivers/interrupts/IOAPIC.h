
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


    I/O APIC (Advanced Programmable Interrupt Controller)

\************************************************************************/

#ifndef IOAPIC_H_INCLUDED
#define IOAPIC_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// I/O APIC register offsets (indirect access via IOREGSEL/IOWIN)

#define IOAPIC_REGSEL 0x00  // I/O Register Select (Index)
#define IOAPIC_IOWIN 0x10   // I/O Window (Data)

/***************************************************************************/
// I/O APIC register indices (values to write to IOREGSEL)

#define IOAPIC_REG_ID 0x00           // I/O APIC ID
#define IOAPIC_REG_VER 0x01          // I/O APIC Version
#define IOAPIC_REG_ARB 0x02          // I/O APIC Arbitration ID
#define IOAPIC_REG_REDTBL_BASE 0x10  // Redirection Table entries (0x10-0x3F)

/***************************************************************************/
// I/O APIC ID Register bits

#define IOAPIC_ID_MASK 0x0F000000  // APIC ID field mask
#define IOAPIC_ID_SHIFT 24         // APIC ID field shift

/***************************************************************************/
// I/O APIC Version Register bits

#define IOAPIC_VER_VERSION_MASK 0x000000FF  // Version field mask
#define IOAPIC_VER_MRE_MASK 0x00FF0000      // Maximum Redirection Entry mask
#define IOAPIC_VER_MRE_SHIFT 16             // Maximum Redirection Entry shift

/***************************************************************************/
// Redirection Table Entry bits (64-bit entry, split into low/high 32-bit)

// Low 32 bits (REDTBL+0)
#define IOAPIC_REDTBL_VECTOR_MASK 0x000000FF    // Interrupt Vector
#define IOAPIC_REDTBL_DELMOD_MASK 0x00000700    // Delivery Mode mask
#define IOAPIC_REDTBL_DELMOD_FIXED 0x00000000   // Fixed delivery mode
#define IOAPIC_REDTBL_DELMOD_LOWEST 0x00000100  // Lowest Priority delivery mode
#define IOAPIC_REDTBL_DELMOD_SMI 0x00000200     // SMI delivery mode
#define IOAPIC_REDTBL_DELMOD_NMI 0x00000400     // NMI delivery mode
#define IOAPIC_REDTBL_DELMOD_INIT 0x00000500    // INIT delivery mode
#define IOAPIC_REDTBL_DELMOD_EXTINT 0x00000700  // ExtINT delivery mode
#define IOAPIC_REDTBL_DESTMOD 0x00000800        // Destination Mode (0=Physical, 1=Logical)
#define IOAPIC_REDTBL_DELIVS 0x00001000         // Delivery Status (RO)
#define IOAPIC_REDTBL_INTPOL 0x00002000         // Interrupt Input Pin Polarity
#define IOAPIC_REDTBL_REMOTEIRR 0x00004000      // Remote IRR (RO)
#define IOAPIC_REDTBL_TRIGGERMOD 0x00008000     // Trigger Mode (0=Edge, 1=Level)
#define IOAPIC_REDTBL_MASK 0x00010000           // Interrupt Mask

// High 32 bits (REDTBL+1) - contains destination field
#define IOAPIC_REDTBL_DEST_MASK 0xFF000000  // Destination field mask
#define IOAPIC_REDTBL_DEST_SHIFT 24         // Destination field shift

/***************************************************************************/
// I/O APIC constants

#define IOAPIC_MAX_ENTRIES 24        // Maximum redirection entries per I/O APIC
#define IOAPIC_MAX_CONTROLLERS 8     // Maximum number of I/O APIC controllers
#define IOAPIC_IRQ_BASE 0x20         // Base interrupt vector for I/O APIC (avoid PIC conflicts)
#define IOAPIC_SPURIOUS_VECTOR 0xFF  // Spurious interrupt vector

/***************************************************************************/
// Redirection Table Entry structure

typedef struct tag_IOAPIC_REDIRECTION_ENTRY {
    union {
        struct {
            U32 Vector : 8;          // Interrupt vector (0-255)
            U32 DeliveryMode : 3;    // Delivery mode
            U32 DestMode : 1;        // Destination mode (0=Physical, 1=Logical)
            U32 DeliveryStatus : 1;  // Delivery status (read-only)
            U32 IntPolarity : 1;     // Interrupt polarity (0=Active High, 1=Active Low)
            U32 RemoteIRR : 1;       // Remote IRR (read-only)
            U32 TriggerMode : 1;     // Trigger mode (0=Edge, 1=Level)
            U32 Mask : 1;            // Interrupt mask (0=Enabled, 1=Disabled)
            U32 Reserved1 : 15;      // Reserved
        };
        U32 Low;
    };
    union {
        struct {
            U32 Reserved2 : 24;   // Reserved
            U32 Destination : 8;  // Destination field
        };
        U32 High;
    };
} IO_APIC_REDIRECTION_ENTRY, *LPIOAPIC_REDIRECTION_ENTRY;

/***************************************************************************/
// I/O APIC controller information

typedef struct tag_IOAPIC_CONTROLLER {
    U8 IoApicId;              // I/O APIC ID from ACPI
    U32 PhysicalAddress;      // Physical base address
    LINEAR MappedAddress;     // Virtual address where I/O APIC is mapped
    U32 GlobalInterruptBase;  // Global system interrupt base
    U8 Version;               // I/O APIC version
    U8 MaxRedirectionEntry;   // Maximum redirection entry (0-based)
    BOOL Present;             // TRUE if I/O APIC is present and mapped
} IO_APIC_CONTROLLER, *LPIOAPIC_CONTROLLER;

/***************************************************************************/
// I/O APIC configuration

typedef struct tag_IOAPIC_CONFIG {
    BOOL Initialized;                                        // TRUE if I/O APIC subsystem is initialized
    U32 ControllerCount;                                     // Number of I/O APIC controllers
    U32 TotalInterrupts;                                     // Total number of interrupt inputs across all I/O APICs
    U32 NextFreeVector;                                      // Next available interrupt vector
    IO_APIC_CONTROLLER Controllers[IOAPIC_MAX_CONTROLLERS];  // Array of I/O APIC controllers
} IO_APIC_CONFIG, *LPIOAPIC_CONFIG;

/***************************************************************************/
// Function prototypes

/**
 * Initialize the I/O APIC subsystem
 * @return TRUE if initialization successful, FALSE otherwise
 */
BOOL InitializeIOAPIC(void);

/**
 * Shutdown the I/O APIC subsystem
 */
void ShutdownIOAPIC(void);

/**
 * Read from an I/O APIC register
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Register Register index to read
 * @return Register value
 */
U32 ReadIOAPICRegister(U32 ControllerIndex, U8 Register);

/**
 * Write to an I/O APIC register
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Register Register index to write
 * @param Value Value to write
 */
void WriteIOAPICRegister(U32 ControllerIndex, U8 Register, U32 Value);

/**
 * Read a redirection table entry
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Entry Entry number (0-based)
 * @param RedirectionEntry Pointer to structure to fill
 * @return TRUE if successful, FALSE otherwise
 */
BOOL ReadRedirectionEntry(U32 ControllerIndex, U8 Entry, LPIOAPIC_REDIRECTION_ENTRY RedirectionEntry);

/**
 * Write a redirection table entry
 * @param ControllerIndex Index of the I/O APIC controller
 * @param Entry Entry number (0-based)
 * @param RedirectionEntry Pointer to redirection entry structure
 * @return TRUE if successful, FALSE otherwise
 */
BOOL WriteRedirectionEntry(U32 ControllerIndex, U8 Entry, LPIOAPIC_REDIRECTION_ENTRY RedirectionEntry);

/**
 * Configure an I/O APIC interrupt
 * @param IRQ Legacy IRQ number (0-15)
 * @param Vector Interrupt vector to assign
 * @param DeliveryMode Delivery mode (IOAPIC_REDTBL_DELMOD_*)
 * @param TriggerMode Trigger mode (0=Edge, 1=Level)
 * @param Polarity Polarity (0=Active High, 1=Active Low)
 * @param DestCPU Destination CPU APIC ID
 * @return TRUE if successfully configured, FALSE otherwise
 */
BOOL ConfigureIOAPICInterrupt(U8 IRQ, U8 Vector, U32 DeliveryMode, U8 TriggerMode, U8 Polarity, U8 DestCPU);

/**
 * Enable an I/O APIC interrupt
 * @param IRQ Legacy IRQ number to enable
 * @return TRUE if successfully enabled, FALSE otherwise
 */
BOOL EnableIOAPICInterrupt(U8 IRQ);

/**
 * Disable an I/O APIC interrupt
 * @param IRQ Legacy IRQ number to disable
 * @return TRUE if successfully disabled, FALSE otherwise
 */
BOOL DisableIOAPICInterrupt(U8 IRQ);

/**
 * Mask all I/O APIC interrupts
 * @param ControllerIndex Index of the I/O APIC controller
 */
void MaskAllIOAPICInterrupts(U32 ControllerIndex);

/**
 * Get I/O APIC configuration
 * @return Pointer to I/O APIC configuration structure
 */
LPIOAPIC_CONFIG GetIOAPICConfig(void);

/**
 * Get I/O APIC controller information
 * @param ControllerIndex Index of the I/O APIC controller
 * @return Pointer to I/O APIC controller structure, NULL if invalid index
 */
LPIOAPIC_CONTROLLER GetIOAPICController(U32 ControllerIndex);

/**
 * Map IRQ number to I/O APIC controller and entry
 * @param IRQ Legacy IRQ number
 * @param ControllerIndex Pointer to store controller index
 * @param Entry Pointer to store redirection entry index
 * @return TRUE if mapping found, FALSE otherwise
 */
BOOL MapIRQToIOAPIC(U8 IRQ, U32* ControllerIndex, U8* Entry);

/**
 * Allocate next available interrupt vector
 * @return Interrupt vector number, 0 if none available
 */
U8 AllocateInterruptVector(void);

/**
 * Set default I/O APIC configuration for standard PC interrupts
 */
void SetDefaultIOAPICConfiguration(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // IOAPIC_H_INCLUDED
