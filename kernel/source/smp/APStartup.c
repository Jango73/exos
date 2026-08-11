
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

#include "smp/APStartup.h"
#include "smp/SMP.h"

#include "Arch.h"
#include "Base.h"
#include "User.h"
#include "drivers/interrupts/LocalAPIC.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "system/Clock.h"
#include "system/System.h"
#include "text/CoreString.h"
#include "utils/BusyWait.h"

/***************************************************************************/

#define AP_TRAMPOLINE_ADDRESS LOW_MEMORY_PAGE_4  // SIPI vector 4
#define AP_PARAMETER_BLOCK_ADDRESS (AP_TRAMPOLINE_ADDRESS + AP_PARAMETER_BLOCK_OFFSET)
#define AP_STACK_SIZE 0x4000           // 16 KB per application processor
#define AP_INIT_PAUSE_MILLISECONDS 10  // Spec: wait at least 10 ms after INIT
#define AP_SIPI_PAUSE_MILLISECONDS 1   // Spec: wait ~200 us between SIPIs
#define AP_ONLINE_TIMEOUT_MILLISECONDS 500
#define AP_ONLINE_POLL_LIMIT 1000000

/***************************************************************************/

static LINEAR G_APStacks[SMP_MAX_CPUS] = { 0 };

/***************************************************************************/

/**
 * @brief C entry point executed by every application processor.
 * @param Param Pointer to the AP parameter block
 *
 * Runs with interrupts masked on the per-AP stack provided by the trampoline.
 * Signals online, then parks until further work is scheduled.
 */
void APEntryPoint(LPAP_PARAMETER_BLOCK Param) {
    Param->Status = CPU_STATUS_ONLINE;
    DEBUG(TEXT("[APEntryPoint] APIC ID %u is online"), Param->ApicId);
    for (;;) {
        __asm__ __volatile__("hlt" : : : "memory");
    }
}

/***************************************************************************/

/**
 * @brief Bring all enabled application processors online.
 *
 * Uses the INIT/SIPI/SIPI startup sequence with the trampoline copied to
 * low memory by ApBootstrapPrepare. Runs on the BSP with interrupts masked.
 */
void StartupApplicationProcessors(void) {
    LPSMP_CONFIG Config = GetSMPConfig();
    if (Config->SmpEnabled == FALSE) return;

    for (U32 Index = 0; Index < Config->CpuCount; Index++) {
        LPCPU_INFO Cpu = &Config->CpuList[Index];
        if (Cpu->IsBsp != FALSE || Cpu->Enabled == FALSE) continue;

        LINEAR StackBase =
            AllocKernelRegion(0, AP_STACK_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("APStack"));
        if (StackBase == 0) {
            WARNING(TEXT("[StartupApplicationProcessors] APIC ID %u : AP stack allocation failed"), Cpu->ApicId);
            continue;
        }
        G_APStacks[Index] = StackBase;

        MemorySet((LPVOID)AP_PARAMETER_BLOCK_ADDRESS, 0, sizeof(AP_PARAMETER_BLOCK));
        volatile AP_PARAMETER_BLOCK* Param = (volatile AP_PARAMETER_BLOCK*)AP_PARAMETER_BLOCK_ADDRESS;
        Param->CEntry = (UINT)&APEntryPoint;
        Param->StackTop = StackBase + AP_STACK_SIZE;
        Param->Cr3 = GetPageDirectory();
        Param->GdtBase = (UINT)Kernel_x86_32.GDT;
        Param->IdtBase = (UINT)Kernel_x86_32.IDT;
        Param->GdtLimit = GDT_SIZE - 1;
        Param->IdtLimit = IDT_SIZE - 1;
        Param->Status = CPU_STATUS_BOOTING;
        Param->ApicId = Cpu->ApicId;
        Cpu->Status = CPU_STATUS_BOOTING;

        ApBootstrapPrepare();

        SendInitIPI(Cpu->ApicId);
        BusyWaitMilliseconds(AP_INIT_PAUSE_MILLISECONDS);
        SendStartupIPI(Cpu->ApicId, AP_TRAMPOLINE_ADDRESS);
        BusyWaitMilliseconds(AP_SIPI_PAUSE_MILLISECONDS);
        SendStartupIPI(Cpu->ApicId, AP_TRAMPOLINE_ADDRESS);

        UINT StartTime = GetSystemTime();
        UINT LoopCount = 0;
        while (Param->Status != CPU_STATUS_ONLINE) {
            if (HasOperationTimedOut(StartTime, LoopCount++, AP_ONLINE_POLL_LIMIT, AP_ONLINE_TIMEOUT_MILLISECONDS) !=
                FALSE) {
                WARNING(TEXT("[StartupApplicationProcessors] APIC ID %u failed to come online"), Cpu->ApicId);
                break;
            }
        }

        if (Param->Status == CPU_STATUS_ONLINE) {
            Cpu->Status = CPU_STATUS_ONLINE;
            DEBUG(TEXT("[StartupApplicationProcessors] APIC ID %u is online"), Cpu->ApicId);
        }
    }
}

/***************************************************************************/
