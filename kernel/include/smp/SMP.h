
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


    Symmetric Multiprocessing (SMP)

\************************************************************************/

#ifndef SMP_H_INCLUDED
#define SMP_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define SMP_MAX_CPUS 32  // Maximum supported processor count

/***************************************************************************/

typedef enum tag_CPU_STATUS {
    CPU_STATUS_OFFLINE = 0,  // CPU not started yet
    CPU_STATUS_BOOTING,      // CPU is being brought up
    CPU_STATUS_ONLINE        // CPU is running
} CPU_STATUS;

/***************************************************************************/
// Per-CPU record

typedef struct tag_CPU_INFO {
    U8 ApicId;       // Local APIC ID
    U8 ProcessorId;  // ACPI processor ID
    U32 Flags;       // ACPI MADT Local APIC flags
    BOOL Enabled;    // Selected as usable by the configuration
    BOOL IsBsp;      // TRUE for the bootstrap processor
} CPU_INFO, *LPCPU_INFO;

/***************************************************************************/
// SMP configuration

typedef struct tag_SMP_CONFIG {
    BOOL Valid;         // TRUE once discovery has run
    BOOL SmpEnabled;    // TRUE when AP bring-up is allowed
    BOOL NoSMPFlag;     // "nosmp" present on the boot command line
    BOOL HasLocalApic;  // TRUE when a Local APIC is available
    U32 EnabledMask;    // Configuration bitmask (0 = all detected)
    U32 DetectedCount;  // Enabled processors reported by ACPI
    U32 CpuCount;       // Usable processors after applying configuration
    U8 BspIndex;        // Index of the BSP in CpuList
    U8 BspApicId;       // Local APIC ID of the BSP
    CPU_INFO CpuList[SMP_MAX_CPUS];
} SMP_CONFIG, *LPSMP_CONFIG;

/***************************************************************************/
// Function prototypes

void InitializeSMP(void);
LPSMP_CONFIG GetSMPConfig(void);
LPCPU_INFO GetCPUInfoByIndex(U32 Index);
LPCPU_INFO GetCPUInfoByApicId(U8 ApicId);
U32 GetUsableCpuCount(void);
U8 GetBspApicId(void);

/***************************************************************************/

#endif  // SMP_H_INCLUDED
