
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


    Symmetric Multiprocessing (SMP) : CPU discovery, per-CPU records and accessors

\************************************************************************/

#include "smp/APStartup.h"
#include "smp/LAPICTimer.h"
#include "smp/SMP.h"

#include "Arch.h"
#include "Base.h"
#include "core/KernelData.h"
#include "drivers/interrupts/LocalAPIC.h"
#include "drivers/platform/ACPI.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

/***************************************************************************/

#define CPU_INDEX_INVALID 0xFF  // CpuIndexByApicId sentinel for an absent CPU

/***************************************************************************/

static SMP_CONFIG DATA_SECTION G_SMPConfig = { 0 };

/***************************************************************************/

/**
 * @brief Retrieve the SMP configuration.
 * @return Pointer to the SMP configuration structure.
 */
LPSMP_CONFIG GetSMPConfig(void) {
    return &G_SMPConfig;
}

/***************************************************************************/

/**
 * @brief Retrieve the number of usable processors.
 * @return Number of usable processors.
 */
U32 GetUsableCpuCount(void) {
    return G_SMPConfig.CpuCount;
}

/***************************************************************************/

/**
 * @brief Retrieve the Local APIC ID of the bootstrap processor.
 * @return Local APIC ID of the BSP.
 */
U8 GetBspApicId(void) {
    return G_SMPConfig.BspApicId;
}

/***************************************************************************/

/**
 * @brief Retrieve the record of the currently executing processor.
 *
 * On x86-64 the per-CPU record is reached through the GS base (the record
 * self pointer is read through %gs:0). On x86-32 the record is looked up
 * by the current Local APIC ID in the per-CPU array.
 *
 * @return Pointer to the current CPU record, NULL when the record is not
 *         reachable yet. The per-CPU GS base is set by InitializeSMP.
 */
LPCPU CurrentCPU(void) {
#if defined(__EXOS_ARCH_X86_64__)
    LPCPU Cpu;

    __asm__ __volatile__("movq %%gs:0, %0" : "=r"(Cpu) : : "memory");

    return Cpu;
#elif defined(__EXOS_ARCH_X86_32__)
    return PerCPUGet(GetLocalAPICId());
#else
    #error "Unsupported architecture"
#endif
}

/***************************************************************************/

/**
 * @brief Retrieve the CPU record associated with a Local APIC ID.
 * @param ApicId Local APIC ID to look for.
 * @return Pointer to the CPU record, NULL when the ID is unknown.
 */
LPCPU PerCPUGet(U8 ApicId) {
    U8 Index;

    if (ApicId >= SMP_MAX_CPUS) {
        return NULL;
    }

    Index = G_SMPConfig.CpuIndexByApicId[ApicId];
    if (Index == CPU_INDEX_INVALID) {
        return NULL;
    }

    return &G_SMPConfig.CpuList[Index];
}

/***************************************************************************/

/**
 * @brief Store the record of the CPU associated with a Local APIC ID.
 *
 * Only updates records that are already part of the CPU table; the table is
 * populated by InitializeSMP.
 * @param ApicId Local APIC ID of the target CPU.
 * @param Cpu Record to store, or NULL to clear the slot.
 * @return TRUE on success, FALSE when the Local APIC ID is unknown.
 */
BOOL PerCPUSet(U8 ApicId, LPCPU Cpu) {
    U8 Index;

    if (ApicId >= SMP_MAX_CPUS) {
        return FALSE;
    }

    Index = G_SMPConfig.CpuIndexByApicId[ApicId];
    if (Index == CPU_INDEX_INVALID) {
        return FALSE;
    }

    if (Cpu == NULL) {
        MemorySet(&G_SMPConfig.CpuList[Index], 0, sizeof(CPU));
        G_SMPConfig.CpuIndexByApicId[ApicId] = CPU_INDEX_INVALID;
        return TRUE;
    }

    G_SMPConfig.CpuList[Index] = *Cpu;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve a CPU record by its index in the usable CPU list.
 * @param Index Index of the CPU record.
 * @return Pointer to the CPU record, NULL if the index is invalid.
 */
LPCPU GetCPUInfoByIndex(U32 Index) {
    if (Index >= G_SMPConfig.CpuCount) {
        return NULL;
    }

    return &G_SMPConfig.CpuList[Index];
}

/***************************************************************************/

/**
 * @brief Retrieve a CPU record by its Local APIC ID.
 * @param ApicId Local APIC ID to look for.
 * @return Pointer to the CPU record, NULL if not found.
 */
LPCPU GetCPUInfoByApicId(U8 ApicId) {
    return PerCPUGet(ApicId);
}

/***************************************************************************/

/**
 * @brief Retrieve the bitmask of CPUs currently online.
 * @return Bitmask where bit N is set when APIC ID N is online.
 */
U32 GetOnlineCpuMask(void) {
    return G_SMPConfig.OnlineMask;
}

/***************************************************************************/

/**
 * @brief Check whether a CPU is online.
 * @param ApicId Local APIC ID of the target CPU.
 * @return TRUE when the CPU is online, FALSE otherwise.
 */
BOOL IsCpuOnline(U8 ApicId) {
    if (ApicId >= SMP_MAX_CPUS) {
        return FALSE;
    }

    return (G_SMPConfig.OnlineMask & (1 << ApicId)) != 0;
}

/***************************************************************************/

/**
 * @brief Mark a CPU as online or offline.
 * @param ApicId Local APIC ID of the target CPU.
 * @param Online TRUE to mark online, FALSE to mark offline.
 * @return TRUE on success, FALSE when the Local APIC ID is out of range.
 */
BOOL SetCpuOnline(U8 ApicId, BOOL Online) {
    if (ApicId >= SMP_MAX_CPUS) {
        return FALSE;
    }

    if (Online != FALSE) {
        G_SMPConfig.OnlineMask |= (1 << ApicId);
    } else {
        G_SMPConfig.OnlineMask &= ~(1 << ApicId);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Bootstrap the BSP per-CPU record and the per-CPU access path.
 *
 * Runs at the very start of InitializeSMP so CurrentCPU() is valid in every
 * code path, including the monoprocessor fallback.
 */
static void InitializeBspCpuAnchor(void) {
    LPCPU BspCpu = &G_SMPConfig.CpuList[0];

    BspCpu->Self = BspCpu;
    BspCpu->Status = CPU_STATUS_ONLINE;
    BspCpu->LocalApicBase = GetLocalAPICBaseAddress();
#if defined(__EXOS_ARCH_X86_64__)
    SetPerCPUAreaBase((LINEAR)BspCpu);
#endif
}

/***************************************************************************/

/**
 * @brief Finalize the monoprocessor fallback configuration.
 *
 * Leaves a single usable CPU (the BSP) and maps APIC ID 0 so CurrentCPU()
 * resolves on hardware without usable Local APIC information.
 */
static void FinalizeMonoProcessorMode(void) {
    G_SMPConfig.Valid = TRUE;
    G_SMPConfig.SmpEnabled = FALSE;
    G_SMPConfig.CpuCount = 1;
    G_SMPConfig.CpuIndexByApicId[0] = 0;
    G_SMPConfig.OnlineMask |= (1 << 0);
}

/***************************************************************************/

/**
 * @brief Select the usable CPU set from the enabled ACPI entries and the
 * configuration mask. The BSP is always usable.
 * @return Number of usable CPUs.
 */
static U32 SelectUsableCpus(U32 DetectedCount, U32 EnabledMask) {
    U32 Usable = 0;

    for (U32 Index = 0; Index < DetectedCount; Index++) {
        LPCPU Cpu = &G_SMPConfig.CpuList[Index];
        BOOL Allowed = (EnabledMask == 0) || ((EnabledMask >> Cpu->ApicId) & 0x1) != 0;

        if (Cpu->IsBsp) {
            Allowed = TRUE;
        }

        if (Allowed == FALSE) {
            Cpu->Enabled = FALSE;
            continue;
        }

        Cpu->Enabled = TRUE;
        if (Usable != Index) {
            G_SMPConfig.CpuList[Usable] = *Cpu;
            Cpu = &G_SMPConfig.CpuList[Usable];
        }
        G_SMPConfig.CpuIndexByApicId[Cpu->ApicId] = (U8)Usable;
        if (Cpu->IsBsp) {
            G_SMPConfig.BspIndex = (U8)Usable;
        }
        Usable++;
    }

    return Usable;
}

/***************************************************************************/

/**
 * @brief Fill the runtime fields of the BSP per-CPU record and mark it online.
 *
 * Runs after SelectUsableCpus, before the application processors are started.
 */
static void InitializeBspCpuRecord(void) {
    LPCPU BspCpu = &G_SMPConfig.CpuList[G_SMPConfig.BspIndex];
    LPLOCAL_APIC_CONFIG LocalApicConfig = GetLocalAPICConfig();

    BspCpu->Self = BspCpu;
    BspCpu->Status = CPU_STATUS_ONLINE;
    BspCpu->LocalApicBase = GetLocalAPICBaseAddress();
    if (LocalApicConfig != NULL) {
        BspCpu->LocalApicMap = LocalApicConfig->MappedAddress;
    }
    BspCpu->Tss = (LPVOID)Kernel_x86_32.TSS;
    BspCpu->CurrentTask = GetCurrentTask();
    BspCpu->StackTop = KernelStartup.StackTop;
    G_SMPConfig.OnlineMask |= (1 << G_SMPConfig.BspApicId);
#if defined(__EXOS_ARCH_X86_64__)
    SetPerCPUAreaBase((LINEAR)BspCpu);
#endif

    DEBUG(
        TEXT("[InitializeSMP] BSP CPU=%p APIC ID %u CurrentTask=%p"),
        (LPVOID)BspCpu,
        G_SMPConfig.BspApicId,
        (LPVOID)BspCpu->CurrentTask);
}

/***************************************************************************/

/**
 * @brief Initialize the SMP subsystem.
 *
 * Enumerates the enabled processors reported by the ACPI MADT, validates the
 * Local APIC base, applies the configuration (General.SMP, General.EnabledCPUs
 * and the "nosmp" boot flag) and records the usable CPU set. Runs on the BSP
 * after the configuration has been loaded.
 */
void InitializeSMP(void) {
    MemorySet(&G_SMPConfig, 0, sizeof(SMP_CONFIG));
    for (U32 Index = 0; Index < SMP_MAX_CPUS; Index++) {
        G_SMPConfig.CpuIndexByApicId[Index] = CPU_INDEX_INVALID;
    }

    // Make CurrentCPU() valid in every code path
    InitializeBspCpuAnchor();

    // Calibrate the Local APIC timer against the PIT reference clock
    CalibrateLAPICTimer();

    LPACPI_CONFIG AcpiConfig = GetACPIConfig();
    if (AcpiConfig == NULL || AcpiConfig->Valid == FALSE || AcpiConfig->UseLocalApic == FALSE ||
        AcpiConfig->LocalApicCount == 0) {
        WARNING(TEXT("[InitializeSMP] No usable ACPI Local APIC information, SMP disabled"));
        FinalizeMonoProcessorMode();
        return;
    }

    G_SMPConfig.HasLocalApic = TRUE;

    // Validate the Local APIC base reported by the MSR against the ACPI override
    U32 MsrBase = GetLocalAPICBaseAddress();
    PHYSICAL AcpiBase = AcpiConfig->LocalApicAddress;

    if (MsrBase == 0) {
        WARNING(TEXT("[InitializeSMP] Local APIC base not available from MSR, SMP disabled"));
        FinalizeMonoProcessorMode();
        return;
    }

    if (AcpiBase != 0 && MsrBase != AcpiBase) {
        WARNING(TEXT("[InitializeSMP] Local APIC base mismatch (MSR=%p ACPI=%p)"), (LPVOID)MsrBase, (LPVOID)AcpiBase);
    }

    // Boot command line fallback flag
    G_SMPConfig.NoSMPFlag = StringContains(KernelStartup.CommandLine, TEXT("nosmp"));

    // Configuration master switch (default: enabled when hardware supports it)
    BOOL ConfigSmpEnabled = (GetConfigurationUInt(TEXT("General.SMP"), 1, 0, 1) != 0);

    // Configuration CPU mask (bit N = APIC ID N, empty means all detected)
    U32 EnabledMask = 0;
    LPCSTR MaskString = GetConfigurationValue(TEXT("General.EnabledCPUs"));

    if (STRING_EMPTY(MaskString) == FALSE) {
        EnabledMask = StringToU32(MaskString);
        G_SMPConfig.EnabledMask = EnabledMask;
    }

    G_SMPConfig.BspApicId = GetLocalAPICId();

    // Collect the enabled processors reported by the ACPI MADT
    U32 Detected = 0;
    for (U32 Index = 0; Index < AcpiConfig->LocalApicCount && Detected < SMP_MAX_CPUS; Index++) {
        LPLOCAL_APIC_INFO Info = GetLocalApicInfo(Index);

        if (Info == NULL || (Info->Flags & ACPI_MADT_LOCAL_APIC_ENABLED) == 0) {
            continue;
        }

        G_SMPConfig.CpuList[Detected].ApicId = Info->ApicId;
        G_SMPConfig.CpuList[Detected].ProcessorId = Info->ProcessorId;
        G_SMPConfig.CpuList[Detected].Flags = Info->Flags;
        G_SMPConfig.CpuList[Detected].IsBsp = (Info->ApicId == G_SMPConfig.BspApicId);
        G_SMPConfig.CpuList[Detected].Enabled = FALSE;
        Detected++;
    }

    G_SMPConfig.DetectedCount = Detected;

    if (Detected == 0) {
        WARNING(TEXT("[InitializeSMP] No enabled processors in ACPI MADT, SMP disabled"));
        FinalizeMonoProcessorMode();
        return;
    }

    // Apply the configuration mask
    U32 Usable = SelectUsableCpus(Detected, EnabledMask);
    G_SMPConfig.CpuCount = Usable;

    // Fill the BSP runtime record before the application processors are started
    InitializeBspCpuRecord();

    // SMP is only effective with a usable set of at least two processors
    G_SMPConfig.SmpEnabled = (ConfigSmpEnabled != FALSE) && (G_SMPConfig.NoSMPFlag == FALSE) && (Usable >= 2);

    DEBUG(
        TEXT("[InitializeSMP] SMP=%s nosmp=%s EnabledCPUs=%x detected=%u usable=%u BSP APIC ID=%u"),
        G_SMPConfig.SmpEnabled ? "ON" : "OFF",
        G_SMPConfig.NoSMPFlag ? "yes" : "no",
        G_SMPConfig.EnabledMask,
        G_SMPConfig.DetectedCount,
        G_SMPConfig.CpuCount,
        G_SMPConfig.BspApicId);

    for (U32 Index = 0; Index < G_SMPConfig.CpuCount; Index++) {
        LPCPU Cpu = &G_SMPConfig.CpuList[Index];
        DEBUG(
            TEXT("[InitializeSMP] CPU %u : APIC ID %u ProcessorId %u %s%s"),
            Index,
            Cpu->ApicId,
            Cpu->ProcessorId,
            Cpu->IsBsp ? TEXT("BSP") : TEXT("AP"),
            Cpu->Enabled ? TEXT("") : TEXT(" (excluded by configuration)"));
    }

    // Bring up the application processors
    if (G_SMPConfig.SmpEnabled != FALSE) {
        StartupApplicationProcessors();
    }

    G_SMPConfig.Valid = TRUE;
}
