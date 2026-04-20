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


    x86-64 Task segment initialization

\************************************************************************/

#include "arch/x86-64/x86-64.h"

#include "text/CoreString.h"
#include "console/Console.h"
#include "core/Driver.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "text/Text.h"
#include "User.h"

/************************************************************************/

#define TASK_SEGMENTS_VER_MAJOR 1
#define TASK_SEGMENTS_VER_MINOR 0

static UINT TaskSegmentsDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION TaskSegmentsDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_MEMORY,
    .VersionMajor = TASK_SEGMENTS_VER_MAJOR,
    .VersionMinor = TASK_SEGMENTS_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "TaskSegments",
    .Alias = "task_segments",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = TaskSegmentsDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the task segments driver descriptor.
 * @return Pointer to the task segments driver.
 */
LPDRIVER TaskSegmentsGetDriver(void) {
    return &TaskSegmentsDriver;
}

/************************************************************************/

/**
 * @brief Populate the limit fields of a system segment descriptor.
 * @param Descriptor Descriptor to update.
 * @param Limit Segment limit value encoded on 20 bits.
 */
void SetSystemSegmentDescriptorLimit(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U32 Limit) {
    Descriptor->Limit_00_15 = (U16)(Limit & 0xFFFF);
    Descriptor->Limit_16_19 = (U8)((Limit >> 16) & 0x0F);
}

/************************************************************************/

/**
 * @brief Populate the base fields of a system segment descriptor.
 * @param Descriptor Descriptor to update.
 * @param Base 64-bit base address of the segment.
 */
void SetSystemSegmentDescriptorBase(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U64 Base) {
    Descriptor->Base_00_15 = (U16)(Base & 0xFFFF);
    Descriptor->Base_16_23 = (U8)((Base >> 16) & 0xFF);
    Descriptor->Base_24_31 = (U8)((Base >> 24) & 0xFF);
    Descriptor->Base_32_63 = (U32)((Base >> 32) & 0xFFFFFFFF);
}

/***************************************************************************/

/**
 * @brief Allocate and initialize the architecture task-state segment.
 */
void InitializeTaskSegments(void) {
    DEBUG(TEXT("Enter"));

    UINT TssSize = sizeof(X86_64_TASK_STATE_SEGMENT);

    Kernel_x86_32.TSS = (LPX86_64_TASK_STATE_SEGMENT)AllocKernelRegion(
        0, TssSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("TSS"));

    if (Kernel_x86_32.TSS == NULL) {
        ERROR(TEXT("AllocKernelRegion for TSS failed"));
        ConsolePanic(TEXT("AllocKernelRegion for TSS failed"));
    }

    MemorySet(Kernel_x86_32.TSS, 0, TssSize);
    Kernel_x86_32.TSS->IOMapBase = (U16)TssSize;

    LINEAR CurrentRsp;
    GetESP(CurrentRsp);
    Kernel_x86_32.TSS->RSP0 = (U64)CurrentRsp;
    Kernel_x86_32.TSS->IST1 = (U64)CurrentRsp;

    LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor =
        (LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR)((LPSEGMENT_DESCRIPTOR)Kernel_x86_32.GDT + GDT_TSS_INDEX);

    MemorySet(Descriptor, 0, sizeof(X86_64_SYSTEM_SEGMENT_DESCRIPTOR));

    SetSystemSegmentDescriptorLimit(Descriptor, TssSize - 1);
    SetSystemSegmentDescriptorBase(Descriptor, (UINT)Kernel_x86_32.TSS);

    Descriptor->Accessed = 1;
    Descriptor->Code = 1;
    Descriptor->S = 0;
    Descriptor->Privilege = CPU_PRIVILEGE_KERNEL;
    Descriptor->Present = 1;
    Descriptor->Limit_16_19 = (U8)(Descriptor->Limit_16_19 & 0x0F);
    Descriptor->Available = 0;
    Descriptor->LongMode = 0;
    Descriptor->DefaultSize = 0;
    Descriptor->Granularity = 0;
    Descriptor->Reserved = 0;

    DEBUG(TEXT("TSS = %p"), Kernel_x86_32.TSS);
    DEBUG(TEXT("Loading task register"));

    LoadInitialTaskRegister(SELECTOR_TSS);

    DEBUG(TEXT("Exit"));
}

/***************************************************************************/

/**
 * @brief Driver command handler for task segment initialization.
 */
static UINT TaskSegmentsDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((TaskSegmentsDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeTaskSegments();
            TaskSegmentsDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((TaskSegmentsDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            TaskSegmentsDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(TASK_SEGMENTS_VER_MAJOR, TASK_SEGMENTS_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
