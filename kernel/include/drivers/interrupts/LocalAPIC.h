
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


    Local APIC (Advanced Programmable Interrupt Controller)

\************************************************************************/

#ifndef LOCAL_APIC_H_INCLUDED
#define LOCAL_APIC_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Local APIC MSR addresses

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_BSP 0x00000100        // Processor is BSP
#define IA32_APIC_BASE_ENABLE 0x00000800     // APIC Global Enable
#define IA32_APIC_BASE_ADDR_MASK 0xFFFFF000  // APIC Base Address mask

/***************************************************************************/
// Local APIC register offsets (from base address)

#define LOCAL_APIC_ID 0x020            // Local APIC ID Register
#define LOCAL_APIC_VERSION 0x030       // Local APIC Version Register
#define LOCAL_APIC_TPR 0x080           // Task Priority Register
#define LOCAL_APIC_APR 0x090           // Arbitration Priority Register
#define LOCAL_APIC_PPR 0x0A0           // Processor Priority Register
#define LOCAL_APIC_EOI 0x0B0           // End of Interrupt Register
#define LOCAL_APIC_RRD 0x0C0           // Remote Read Register
#define LOCAL_APIC_LDR 0x0D0           // Logical Destination Register
#define LOCAL_APIC_DFR 0x0E0           // Destination Format Register
#define LOCAL_APIC_SPURIOUS_IV 0x0F0   // Spurious Interrupt Vector Register
#define LOCAL_APIC_ISR_BASE 0x100      // In-Service Register (0x100-0x170)
#define LOCAL_APIC_TMR_BASE 0x180      // Trigger Mode Register (0x180-0x1F0)
#define LOCAL_APIC_IRR_BASE 0x200      // Interrupt Request Register (0x200-0x270)
#define LOCAL_APIC_ERROR_STATUS 0x280  // Error Status Register
#define LOCAL_APIC_LVT_CMCI 0x2F0      // LVT Corrected Machine Check Interrupt Register
#define LOCAL_APIC_ICR_LOW 0x300       // Interrupt Command Register (bits 0-31)
#define LOCAL_APIC_ICR_HIGH 0x310      // Interrupt Command Register (bits 32-63)
#define LOCAL_APIC_LVT_TIMER 0x320     // LVT Timer Register
#define LOCAL_APIC_LVT_THERMAL 0x330   // LVT Thermal Sensor Register
#define LOCAL_APIC_LVT_PERF 0x340      // LVT Performance Monitoring Counters Register
#define LOCAL_APIC_LVT_LINT0 0x350     // LVT LINT0 Register
#define LOCAL_APIC_LVT_LINT1 0x360     // LVT LINT1 Register
#define LOCAL_APIC_LVT_ERROR 0x370     // LVT Error Register
#define LOCAL_APIC_TIMER_ICR 0x380     // Timer Initial Count Register
#define LOCAL_APIC_TIMER_CCR 0x390     // Timer Current Count Register
#define LOCAL_APIC_TIMER_DCR 0x3E0     // Timer Divide Configuration Register

/***************************************************************************/
// Local APIC register bits and values

// Spurious Interrupt Vector Register bits
#define LOCAL_APIC_SPURIOUS_ENABLE 0x00000100  // APIC Software Enable
#define LOCAL_APIC_SPURIOUS_VECTOR 0x000000FF  // Spurious Vector mask

// LVT Register bits
#define LOCAL_APIC_LVT_VECTOR_MASK 0x000000FF      // Vector
#define LOCAL_APIC_LVT_DELIVERY_MASK 0x00000700    // Delivery Mode mask
#define LOCAL_APIC_LVT_DELIVERY_FIXED 0x00000000   // Fixed delivery mode
#define LOCAL_APIC_LVT_DELIVERY_SMI 0x00000200     // SMI delivery mode
#define LOCAL_APIC_LVT_DELIVERY_NMI 0x00000400     // NMI delivery mode
#define LOCAL_APIC_LVT_DELIVERY_INIT 0x00000500    // INIT delivery mode
#define LOCAL_APIC_LVT_DELIVERY_EXTINT 0x00000700  // ExtINT delivery mode
#define LOCAL_APIC_LVT_DELIVERY_STATUS 0x00001000  // Delivery Status
#define LOCAL_APIC_LVT_PIN_POLARITY 0x00002000     // Pin Polarity
#define LOCAL_APIC_LVT_REMOTE_IRR 0x00004000       // Remote IRR
#define LOCAL_APIC_LVT_TRIGGER_MODE 0x00008000     // Trigger Mode
#define LOCAL_APIC_LVT_MASK 0x00010000             // Mask

// ICR Register bits
#define LOCAL_APIC_ICR_VECTOR_MASK 0x000000FF        // Vector
#define LOCAL_APIC_ICR_DELIVERY_MASK 0x00000700      // Delivery Mode mask
#define LOCAL_APIC_ICR_DELIVERY_FIXED 0x00000000     // Fixed delivery mode
#define LOCAL_APIC_ICR_DELIVERY_LOWEST 0x00000100    // Lowest Priority delivery mode
#define LOCAL_APIC_ICR_DELIVERY_SMI 0x00000200       // SMI delivery mode
#define LOCAL_APIC_ICR_DELIVERY_NMI 0x00000400       // NMI delivery mode
#define LOCAL_APIC_ICR_DELIVERY_INIT 0x00000500      // INIT delivery mode
#define LOCAL_APIC_ICR_DELIVERY_SIPI 0x00000600      // Startup Inter-Processor Interrupt (SIPI)
#define LOCAL_APIC_ICR_DEST_MODE 0x00000800          // Destination Mode
#define LOCAL_APIC_ICR_DELIVERY_STATUS 0x00001000    // Delivery Status
#define LOCAL_APIC_ICR_LEVEL 0x00004000              // Level
#define LOCAL_APIC_ICR_TRIGGER 0x00008000            // Trigger Mode
#define LOCAL_APIC_ICR_REMOTE_READ 0x00030000        // Remote Read Status mask
#define LOCAL_APIC_ICR_DEST_SHORTHAND 0x000C0000     // Destination Shorthand mask
#define LOCAL_APIC_ICR_DEST_NO_SHORTHAND 0x00000000  // No Shorthand
#define LOCAL_APIC_ICR_DEST_SELF 0x00040000          // Self
#define LOCAL_APIC_ICR_DEST_ALL_INC_SELF 0x00080000  // All Including Self
#define LOCAL_APIC_ICR_DEST_ALL_EXC_SELF 0x000C0000  // All Excluding Self

// Timer Divide Configuration Register values
#define LOCAL_APIC_TIMER_DIVIDE_BY_2 0x00000000
#define LOCAL_APIC_TIMER_DIVIDE_BY_4 0x00000001
#define LOCAL_APIC_TIMER_DIVIDE_BY_8 0x00000002
#define LOCAL_APIC_TIMER_DIVIDE_BY_16 0x00000003
#define LOCAL_APIC_TIMER_DIVIDE_BY_32 0x00000008
#define LOCAL_APIC_TIMER_DIVIDE_BY_64 0x00000009
#define LOCAL_APIC_TIMER_DIVIDE_BY_128 0x0000000A
#define LOCAL_APIC_TIMER_DIVIDE_BY_1 0x0000000B

/***************************************************************************/
// Local APIC configuration structure

typedef struct tag_LOCAL_APIC_CONFIG {
    BOOL Present;          // TRUE if Local APIC is present
    BOOL Enabled;          // TRUE if Local APIC is enabled
    U32 BaseAddress;       // Physical base address of Local APIC
    LINEAR MappedAddress;  // Virtual address where Local APIC is mapped
    U8 ApicId;             // Local APIC ID
    U8 Version;            // Local APIC version
    U8 MaxLvtEntries;      // Maximum LVT entries supported
    U32 SpuriousVector;    // Spurious interrupt vector
} LOCAL_APIC_CONFIG, *LPLOCAL_APIC_CONFIG;

/***************************************************************************/
// Function prototypes

/**
 * Initialize the Local APIC subsystem
 * @return TRUE if initialization successful, FALSE otherwise
 */
BOOL InitializeLocalAPIC(void);

/**
 * Check if Local APIC is present via CPUID
 * @return TRUE if Local APIC is present, FALSE otherwise
 */
BOOL IsLocalAPICPresent(void);

/**
 * Enable the Local APIC via MSR
 * @return TRUE if successfully enabled, FALSE otherwise
 */
BOOL EnableLocalAPIC(void);

/**
 * Disable the Local APIC via MSR
 * @return TRUE if successfully disabled, FALSE otherwise
 */
BOOL DisableLocalAPIC(void);

/**
 * Read Local APIC base address from MSR
 * @return Physical base address of Local APIC
 */
U32 GetLocalAPICBaseAddress(void);

/**
 * Set Local APIC base address via MSR
 * @param BaseAddress Physical base address to set
 * @return TRUE if successfully set, FALSE otherwise
 */
BOOL SetLocalAPICBaseAddress(U32 BaseAddress);

/**
 * Read from a Local APIC register
 * @param Register Register offset
 * @return Register value
 */
U32 ReadLocalAPICRegister(U32 Register);

/**
 * Write to a Local APIC register
 * @param Register Register offset
 * @param Value Value to write
 */
void WriteLocalAPICRegister(U32 Register, U32 Value);

/**
 * Get Local APIC ID
 * @return Local APIC ID
 */
U8 GetLocalAPICId(void);

/**
 * Send End of Interrupt (EOI) signal
 */
void SendLocalAPICEOI(void);

/**
 * Set spurious interrupt vector
 * @param Vector Spurious interrupt vector (0x20-0xFF)
 * @return TRUE if successfully set, FALSE otherwise
 */
BOOL SetSpuriousInterruptVector(U8 Vector);

/**
 * Configure a Local Vector Table (LVT) entry
 * @param LvtRegister LVT register offset
 * @param Vector Interrupt vector
 * @param DeliveryMode Delivery mode
 * @param Masked TRUE to mask the interrupt, FALSE to unmask
 * @return TRUE if successfully configured, FALSE otherwise
 */
BOOL ConfigureLVTEntry(U32 LvtRegister, U8 Vector, U32 DeliveryMode, BOOL Masked);

/**
 * Send an INIT Inter-Processor Interrupt to a target processor
 * @param DestApicId Local APIC ID of the target processor
 * @return TRUE if the INIT was issued, FALSE otherwise
 */
BOOL SendInitIPI(U8 DestApicId);

/**
 * Send a Startup Inter-Processor Interrupt (SIPI) to a target processor
 * @param DestApicId Local APIC ID of the target processor
 * @param TrampolineAddress Physical address of the AP trampoline (page aligned)
 * @return TRUE if the SIPI was issued, FALSE otherwise
 */
BOOL SendStartupIPI(U8 DestApicId, PHYSICAL TrampolineAddress);

/**
 * Get Local APIC configuration
 * @return Pointer to Local APIC configuration structure
 */
LPLOCAL_APIC_CONFIG GetLocalAPICConfig(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // LOCAL_APIC_H_INCLUDED
