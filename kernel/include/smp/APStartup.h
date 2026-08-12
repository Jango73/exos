
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


    SMP : application processor bootstrap

\************************************************************************/

#ifndef AP_STARTUP_H_INCLUDED
#define AP_STARTUP_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define AP_PARAMETER_BLOCK_OFFSET 0x400  // Parameter block location inside the trampoline page

/***************************************************************************/
// Parameter block shared between the BSP and the trampoline.
// Pointer fields use UINT (register width), so their size and offsets differ
// between architectures. MUST stay in sync with the equates in
// arch/x86-32/asm/ApBootstrap.asm and arch/x86-64/asm/ApBootstrap.asm.

typedef struct tag_AP_PARAMETER_BLOCK {
    UINT CEntry;    // Linear address of the AP C entry point
    UINT StackTop;  // Linear address of the per-AP kernel stack top
    UINT Cr3;       // Page directory pointer to activate on the AP
    UINT GdtBase;   // Linear address of the GDT
    UINT IdtBase;   // Linear address of the IDT
    U32 GdtLimit;   // GDT limit
    U32 IdtLimit;   // IDT limit
    U32 Status;     // CPU_STATUS value, updated by the AP
    U32 ApicId;     // Local APIC ID of the target AP
    UINT CpuArea;   // Linear address of the per-CPU record (GS base on x86-64)
} AP_PARAMETER_BLOCK, *LPAP_PARAMETER_BLOCK;

/***************************************************************************/
// Function prototypes

/**
 * Copy the AP trampoline into low memory and patch its relocations.
 * Architecture-specific (implemented in assembly).
 */
void ApBootstrapPrepare(void);

/**
 * Bring all enabled application processors online.
 */
void StartupApplicationProcessors(void);

/**
 * C entry point reached by every application processor.
 * @param Param Pointer to the AP parameter block
 */
void APEntryPoint(LPAP_PARAMETER_BLOCK Param);

/***************************************************************************/

#endif  // AP_STARTUP_H_INCLUDED
