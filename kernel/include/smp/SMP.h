
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
#include "process/Schedule.h"
#include "smp/LAPICTimer.h"

/***************************************************************************/

#define SMP_MAX_CPUS 32  // Maximum supported processor count

/***************************************************************************/

typedef enum tag_CPU_STATUS {
    CPU_STATUS_OFFLINE = 0,  // CPU not started yet
    CPU_STATUS_BOOTING,      // CPU is being brought up
    CPU_STATUS_ONLINE        // CPU is running
} CPU_STATUS;

/***************************************************************************/
// Per-CPU statistics

typedef struct tag_CPU_STATISTICS {
    U32 ContextSwitchCount;  // Context switches performed on this CPU
    U32 InterruptCount;      // Interrupts serviced on this CPU
    U32 TickCount;           // Scheduler ticks delivered on this CPU
} CPU_STATISTICS, *LPCPU_STATISTICS;

/***************************************************************************/
// Per-CPU record: discovery fields plus runtime state.
// Self is the first member so CurrentCPU() can load it through %gs:0.

typedef struct tag_CPU CPU, *LPCPU;

struct tag_CPU {
    LPCPU Self;                       // Self pointer, read through %gs:0 on x86-64
    U8 ApicId;                        // Local APIC ID
    U8 ProcessorId;                   // ACPI processor ID
    U8 Status;                        // CPU_STATUS: offline, booting, online
    U32 Flags;                        // ACPI MADT Local APIC flags
    BOOL Enabled;                     // Selected as usable by the configuration
    BOOL IsBsp;                       // TRUE for the bootstrap processor
    U32 CpuFlags;                     // Per-CPU runtime flags
    LINEAR StackBase;                 // Per-CPU kernel stack base
    LINEAR StackTop;                  // Per-CPU kernel stack top
    LPVOID Tss;                       // Per-CPU task state segment
    LPTASK CurrentTask;               // Task currently running on this CPU
    LPTASK_RUN_QUEUE RunQueue;        // Per-CPU run queue
    volatile U32 SchedulerFreeze;     // Per-CPU scheduler freeze counter
    volatile BOOL ReschedulePending;  // Reschedule requested for this CPU (IPI delivery is Step 6)
    U32 LocalApicBase;                // Physical Local APIC base address
    LINEAR LocalApicMap;              // Virtual address where the Local APIC is mapped
    LAPICTIMER_CONFIG LAPICTimer;     // Per-CPU Local APIC timer state
    CPU_STATISTICS Statistics;        // Per-CPU counters
};

/***************************************************************************/
// SMP configuration

typedef struct tag_SMP_CONFIG {
    BOOL Valid;                         // TRUE once discovery has run
    BOOL SmpEnabled;                    // TRUE when AP bring-up is allowed
    BOOL NoSMPFlag;                     // "nosmp" present on the boot command line
    BOOL HasLocalApic;                  // TRUE when a Local APIC is available
    U32 EnabledMask;                    // Configuration bitmask (0 = all detected)
    U32 DetectedCount;                  // Enabled processors reported by ACPI
    U32 CpuCount;                       // Usable processors after applying configuration
    U32 OnlineMask;                     // Online CPUs bitmask (bit N = APIC ID N)
    U8 BspIndex;                        // Index of the BSP in CpuList
    U8 BspApicId;                       // Local APIC ID of the BSP
    U8 CpuIndexByApicId[SMP_MAX_CPUS];  // APIC ID to CpuList index map (0xFF = absent)
    CPU CpuList[SMP_MAX_CPUS];
} SMP_CONFIG, *LPSMP_CONFIG;

/***************************************************************************/
// Function prototypes

void InitializeSMP(void);
LPSMP_CONFIG GetSMPConfig(void);
LPCPU CurrentCPU(void);
LPCPU PerCPUGet(U8 ApicId);
BOOL PerCPUSet(U8 ApicId, LPCPU Cpu);
LPCPU GetCPUInfoByIndex(U32 Index);
LPCPU GetCPUInfoByApicId(U8 ApicId);
U32 GetUsableCpuCount(void);
U32 GetOnlineCpuMask(void);
BOOL IsCpuOnline(U8 ApicId);
BOOL SetCpuOnline(U8 ApicId, BOOL Online);
U8 GetBspApicId(void);

/***************************************************************************/

#endif  // SMP_H_INCLUDED
