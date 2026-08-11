
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


    Symmetric Multiprocessing (SMP) : CPU discovery and boot policy

\************************************************************************/

#include "smp/APStartup.h"
#include "smp/LAPICTimer.h"
#include "smp/SMP.h"

#include "Base.h"
#include "core/KernelData.h"
#include "drivers/interrupts/LocalAPIC.h"
#include "drivers/platform/ACPI.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

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
 * @brief Retrieve a CPU record by its index.
 * @param Index Index of the CPU record.
 * @return Pointer to the CPU record, NULL if the index is invalid.
 */
LPCPU_INFO GetCPUInfoByIndex(U32 Index) {
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
LPCPU_INFO GetCPUInfoByApicId(U8 ApicId) {
    for (U32 Index = 0; Index < G_SMPConfig.CpuCount; Index++) {
        if (G_SMPConfig.CpuList[Index].ApicId == ApicId) {
            return &G_SMPConfig.CpuList[Index];
        }
    }

    return NULL;
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
        LPCPU_INFO Cpu = &G_SMPConfig.CpuList[Index];
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
        }
        if (Cpu->IsBsp) {
            G_SMPConfig.BspIndex = (U8)Usable;
        }
        Usable++;
    }

    return Usable;
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

    // Calibrate the Local APIC timer against the PIT reference clock
    CalibrateLAPICTimer();

    LPACPI_CONFIG AcpiConfig = GetACPIConfig();
    if (AcpiConfig == NULL || AcpiConfig->Valid == FALSE || AcpiConfig->UseLocalApic == FALSE ||
        AcpiConfig->LocalApicCount == 0) {
        WARNING(TEXT("[InitializeSMP] No usable ACPI Local APIC information, SMP disabled"));
        G_SMPConfig.Valid = TRUE;
        G_SMPConfig.SmpEnabled = FALSE;
        G_SMPConfig.CpuCount = 1;
        return;
    }

    G_SMPConfig.HasLocalApic = TRUE;

    // Validate the Local APIC base reported by the MSR against the ACPI override
    U32 MsrBase = GetLocalAPICBaseAddress();
    PHYSICAL AcpiBase = AcpiConfig->LocalApicAddress;

    if (MsrBase == 0) {
        WARNING(TEXT("[InitializeSMP] Local APIC base not available from MSR, SMP disabled"));
        G_SMPConfig.Valid = TRUE;
        G_SMPConfig.SmpEnabled = FALSE;
        G_SMPConfig.CpuCount = 1;
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
        G_SMPConfig.Valid = TRUE;
        G_SMPConfig.SmpEnabled = FALSE;
        G_SMPConfig.CpuCount = 1;
        return;
    }

    // Apply the configuration mask
    U32 Usable = SelectUsableCpus(Detected, EnabledMask);
    G_SMPConfig.CpuCount = Usable;

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
        LPCPU_INFO Cpu = &G_SMPConfig.CpuList[Index];
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
