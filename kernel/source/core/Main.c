
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


    Main

\************************************************************************/

#include "console/Console.h"
#include "core/Kernel.h"
#include "Arch.h"
#include "console/Console-EarlyBoot.h"
#include "log/Log.h"
#include "system/System.h"

/************************************************************************/

extern LINEAR __bss_init_start;
extern LINEAR __bss_init_end;

KERNEL_STARTUP_INFO KernelStartup = {
    .IRQMask_21_PM = 0x000000FB, .IRQMask_A1_PM = 0x000000FF, .IRQMask_21_RM = 0, .IRQMask_A1_RM = 0};

/************************************************************************/

#ifndef BOOT_STAGE_MARKERS
#define BOOT_STAGE_MARKERS 0
#endif

/************************************************************************/

enum {
    BOOT_STAGE_KERNEL_ENTRY = 30u,
    BOOT_STAGE_KERNEL_MULTIBOOT_OK = 31u,
    BOOT_STAGE_KERNEL_FRAMEBUFFER_READY = 32u,
    BOOT_STAGE_KERNEL_BOOT_CONFIG_READY = 33u,
    BOOT_STAGE_KERNEL_READY_TO_INIT = 34u
};

/************************************************************************/

#if BOOT_STAGE_MARKERS == 1
static U32 KernelBootMarkerScaleColor(U32 Value, U32 MaskSize) {
    if (MaskSize == 0u) {
        return 0u;
    }

    if (MaskSize >= 8u) {
        return Value & 0xFFu;
    }

    U32 MaxValue = (1u << MaskSize) - 1u;
    return (Value * MaxValue) / 255u;
}

/************************************************************************/

static U32 KernelBootMarkerComposePixel(const multiboot_info_t* MultibootInfo, U32 Red, U32 Green, U32 Blue) {
    if (MultibootInfo == NULL || MultibootInfo->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB) {
        return 0u;
    }

    U32 Pixel = 0u;
    Pixel |= KernelBootMarkerScaleColor(Red, MultibootInfo->color_info[1]) << MultibootInfo->color_info[0];
    Pixel |= KernelBootMarkerScaleColor(Green, MultibootInfo->color_info[3]) << MultibootInfo->color_info[2];
    Pixel |= KernelBootMarkerScaleColor(Blue, MultibootInfo->color_info[5]) << MultibootInfo->color_info[4];
    return Pixel;
}
#endif

/************************************************************************/

/**
 * @brief Import EXOS-specific bootloader configuration from one multiboot pointer.
 * @param MultibootInfo Active multiboot information block.
 */
static void KernelImportBootConfig(multiboot_info_t* MultibootInfo) {
    LPEXOS_MULTIBOOT_CONFIG_TABLE ConfigTable;

    KernelStartup.RsdpPhysical = 0;

    if (MultibootInfo == NULL || (MultibootInfo->flags & MULTIBOOT_INFO_CONFIG_TABLE) == 0u) {
        return;
    }

    ConfigTable = (LPEXOS_MULTIBOOT_CONFIG_TABLE)(UINT)MultibootInfo->config_table;
    if (ConfigTable == NULL) {
        return;
    }

    if (ConfigTable->Signature != EXOS_MULTIBOOT_CONFIG_TABLE_SIGNATURE ||
        ConfigTable->Version != EXOS_MULTIBOOT_CONFIG_TABLE_VERSION) {
        KernelStartup.RsdpPhysical = (PHYSICAL)MultibootInfo->config_table;
        return;
    }

    if ((ConfigTable->Flags & EXOS_MULTIBOOT_CONFIG_HAS_RSDP) != 0u) {
        KernelStartup.RsdpPhysical = (PHYSICAL)ConfigTable->RsdpPhysical;
    }

    if ((ConfigTable->Flags & EXOS_MULTIBOOT_CONFIG_HAS_CONSOLE_CURSOR) != 0u) {
        ConsoleSetBootCursorHandover(ConfigTable->ConsoleCursorX, ConfigTable->ConsoleCursorY);
    }
}

/************************************************************************/

void KernelBootMarkStage(multiboot_info_t* MultibootInfo, U32 StageIndex, U32 Red, U32 Green, U32 Blue) {
#if BOOT_STAGE_MARKERS == 1
    const U32 MarkerBaseX = 2u;
    const U32 MarkerBaseY = 2u;
    const U32 MarkerSize = 8u;
    const U32 MarkerSpacing = 2u;
    const U32 MarkerGroupSize = 10u;
    const U32 MarkerLineStride = MarkerSize + MarkerSpacing;

    if (MultibootInfo == NULL) {
        return;
    }
    if ((MultibootInfo->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) == 0u) {
        return;
    }
    if (MultibootInfo->framebuffer_bpp != 32u || MultibootInfo->framebuffer_pitch == 0u) {
        return;
    }
    if (MultibootInfo->framebuffer_addr_high != 0u || MultibootInfo->framebuffer_addr_low == 0u) {
        return;
    }

    U8* Framebuffer = (U8*)(UINT)MultibootInfo->framebuffer_addr_low;
    if (Framebuffer == NULL) {
        return;
    }

    U32 Pixel = KernelBootMarkerComposePixel(MultibootInfo, Red, Green, Blue);
    U32 GroupIndex = StageIndex / MarkerGroupSize;
    U32 GroupOffset = StageIndex % MarkerGroupSize;
    U32 StartX = MarkerBaseX + GroupOffset * (MarkerSize + MarkerSpacing);
    U32 StartY = MarkerBaseY + GroupIndex * MarkerLineStride;

    if (StartX >= MultibootInfo->framebuffer_width || StartY >= MultibootInfo->framebuffer_height) {
        return;
    }

    U32 DrawWidth = MarkerSize;
    U32 DrawHeight = MarkerSize;
    if (StartX + DrawWidth > MultibootInfo->framebuffer_width) {
        DrawWidth = MultibootInfo->framebuffer_width - StartX;
    }
    if (StartY + DrawHeight > MultibootInfo->framebuffer_height) {
        DrawHeight = MultibootInfo->framebuffer_height - StartY;
    }

    for (U32 Y = 0u; Y < DrawHeight; Y++) {
        U32* Row = (U32*)(Framebuffer + ((StartY + Y) * MultibootInfo->framebuffer_pitch) + (StartX * 4u));
        for (U32 X = 0u; X < DrawWidth; X++) {
            Row[X] = Pixel;
        }
    }
#else
    UNUSED(MultibootInfo);
    UNUSED(StageIndex);
    UNUSED(Red);
    UNUSED(Green);
    UNUSED(Blue);
#endif
}

/**
 * @brief Main entry point for the EXOS kernel in paged protected mode.
 *
 * This function initializes the kernel subsystems, processes memory maps,
 * sets up hardware components, and starts the system. It is called after
 * the bootloader has set up protected mode and paging.
 *
 * The function retrieves startup parameters from Multiboot information structure,
 * initializes all kernel subsystems in proper order, and never returns.
 */
void KernelMain(void) {
    U32 MultibootMagic;
    LINEAR MultibootInfoLinear;

    // No more interrupts
    DisableInterrupts();

    // Retrieve Multiboot parameters from registers
#if defined(__EXOS_ARCH_X86_64__)
    register U64 StartupRax __asm__("rax");
    register U64 StartupRbx __asm__("rbx");

    MultibootMagic = (U32)StartupRax;
    MultibootInfoLinear = (LINEAR)StartupRbx;
#else
    __asm__ __volatile__("movl %%eax, %0" : "=m"(MultibootMagic));
    __asm__ __volatile__("movl %%ebx, %0" : "=m"(MultibootInfoLinear));
#endif


    // Validate Multiboot magic number
    if (MultibootMagic != MULTIBOOT_BOOTLOADER_MAGIC) {
        ConsolePanic(TEXT("Multiboot information not valid"));
    }

    // Map the multiboot info structure to access it
    multiboot_info_t* MultibootInfo = (multiboot_info_t*)(UINT)MultibootInfoLinear;
    KernelBootMarkStage(MultibootInfo, BOOT_STAGE_KERNEL_ENTRY, 255u, 0u, 0u);
    KernelBootMarkStage(MultibootInfo, BOOT_STAGE_KERNEL_MULTIBOOT_OK, 255u, 128u, 0u);

    if ((MultibootInfo->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        PHYSICAL FramebufferPhysical = 0;
#if defined(__EXOS_ARCH_X86_64__)
        U64 FramebufferAddress = U64_Make(
            MultibootInfo->framebuffer_addr_high,
            MultibootInfo->framebuffer_addr_low);
        FramebufferPhysical = (PHYSICAL)FramebufferAddress;
#else
        FramebufferPhysical = (PHYSICAL)MultibootInfo->framebuffer_addr_low;
        if (MultibootInfo->framebuffer_addr_high != 0u) {
            WARNING(TEXT("Framebuffer above 4GB not supported"));
        }
#endif
        ConsoleSetFramebufferInfo(
            FramebufferPhysical,
            MultibootInfo->framebuffer_width,
            MultibootInfo->framebuffer_height,
            MultibootInfo->framebuffer_pitch,
            (U32)MultibootInfo->framebuffer_bpp,
            (U32)MultibootInfo->framebuffer_type,
            (U32)MultibootInfo->color_info[0],
            (U32)MultibootInfo->color_info[1],
            (U32)MultibootInfo->color_info[2],
            (U32)MultibootInfo->color_info[3],
            (U32)MultibootInfo->color_info[4],
            (U32)MultibootInfo->color_info[5]);

        EarlyBootConsoleInitialize(
            FramebufferPhysical,
            MultibootInfo->framebuffer_width,
            MultibootInfo->framebuffer_height,
            MultibootInfo->framebuffer_pitch,
            (U32)MultibootInfo->framebuffer_bpp,
            (U32)MultibootInfo->framebuffer_type,
            (U32)MultibootInfo->color_info[0],
            (U32)MultibootInfo->color_info[1],
            (U32)MultibootInfo->color_info[2],
            (U32)MultibootInfo->color_info[3],
            (U32)MultibootInfo->color_info[4],
            (U32)MultibootInfo->color_info[5]);

    }
    KernelBootMarkStage(MultibootInfo, BOOT_STAGE_KERNEL_FRAMEBUFFER_READY, 255u, 255u, 0u);

    // Extract information from Multiboot structure
    // Get kernel address from first module
    if (MultibootInfo->flags & MULTIBOOT_INFO_MODS && MultibootInfo->mods_count > 0) {
        multiboot_module_t* FirstModule = (multiboot_module_t*)(UINT)(LINEAR)MultibootInfo->mods_addr;
        KernelStartup.KernelPhysicalBase = FirstModule->mod_start;
        KernelStartup.KernelSize = (UINT)(FirstModule->mod_end - FirstModule->mod_start);
        KernelStartup.KernelReservedBytes = (UINT)FirstModule->reserved;
        if (KernelStartup.KernelReservedBytes < KernelStartup.KernelSize) {
            ERROR(TEXT("Invalid kernel reserved span (reserved=%u size=%u)"),
                KernelStartup.KernelReservedBytes,
                KernelStartup.KernelSize);
            ConsolePanic(TEXT("Invalid boot kernel reserved span"));
        }
        // Get the command line
        LPCSTR ModuleCommandLine = (LPCSTR)(UINT)(LINEAR)FirstModule->cmdline;
        StringCopy(KernelStartup.CommandLine, ModuleCommandLine);
    } else {
        // Fallback - should not happen with our bootloader
        KernelStartup.KernelPhysicalBase = 0;
        KernelStartup.KernelSize = 0;
        KernelStartup.KernelReservedBytes = 0;
        StringClear(KernelStartup.CommandLine);
    }

    // Process memory map if available
    if (MultibootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        PHYSICAL MmapCursor = MultibootInfo->mmap_addr;
        PHYSICAL MmapEnd = MultibootInfo->mmap_addr + MultibootInfo->mmap_length;
        U32 EntryCount = 0;

        while (MmapCursor < MmapEnd && EntryCount < (N_4KB / sizeof(MULTIBOOT_MEMORY_ENTRY))) {
            multiboot_memory_map_t* MmapEntry = (multiboot_memory_map_t*)(UINT)MmapCursor;
            // Duplicate Multiboot entry information
            KernelStartup.MultibootMemoryEntries[EntryCount].Base = U64_Make(MmapEntry->addr_high, MmapEntry->addr_low);
            KernelStartup.MultibootMemoryEntries[EntryCount].Length = U64_Make(MmapEntry->len_high, MmapEntry->len_low);
            KernelStartup.MultibootMemoryEntries[EntryCount].Type = MmapEntry->type;
            EntryCount++;

            // Move to next entry (size field is at the beginning and doesn't include itself)
            MmapCursor += MmapEntry->size + sizeof(MmapEntry->size);
        }

        KernelStartup.MultibootMemoryEntryCount = EntryCount;
    }

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.KernelPhysicalBase == 0) {
        ConsolePanic(TEXT("No physical address specified for the kernel"));
    }

    // Clear the BSS

    LINEAR BSSStart = (LINEAR)(&__bss_init_start);
    LINEAR BSSEnd = (LINEAR)(&__bss_init_end);
    U32 BSSSize = BSSEnd - BSSStart;
    MemorySet((LPVOID)BSSStart, 0, BSSSize);

    KernelImportBootConfig(MultibootInfo);
    KernelBootMarkStage(MultibootInfo, BOOT_STAGE_KERNEL_BOOT_CONFIG_READY, 0u, 255u, 0u);

    //--------------------------------------
    // Main initialization routine
    KernelBootMarkStage(MultibootInfo, BOOT_STAGE_KERNEL_READY_TO_INIT, 0u, 255u, 255u);

    InitializeKernel();
}
