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


    Script Exposure Helpers - Memory Map

\************************************************************************/

#include "expose/Exposed.h"

#include "core/KernelData.h"
#include "memory/Memory.h"
#include "process/Process.h"

/************************************************************************/

static int DATA_SECTION MemoryMapRootSentinel = 0;

SCRIPT_HOST_HANDLE MemoryMapRootHandle = &MemoryMapRootSentinel;

/************************************************************************/

/**
 * @brief Return one kernel memory region descriptor by index.
 * @param RegionList Descriptor list.
 * @param Index Zero-based descriptor index.
 * @return Matching descriptor or NULL.
 */
static LPMEMORY_REGION_DESCRIPTOR MemoryRegionGetByIndex(LPMEMORY_REGION_LIST RegionList, UINT Index) {
    UINT CurrentIndex = 0;
    LPMEMORY_REGION_DESCRIPTOR Descriptor = NULL;

    if (RegionList == NULL) {
        return NULL;
    }

    for (Descriptor = RegionList->Head; Descriptor != NULL; Descriptor = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next) {
        if (CurrentIndex == Index) {
            return Descriptor;
        }

        CurrentIndex++;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed memory-map root object.
 * @param Context Host callback context (unused for memory-map exposure)
 * @param Parent Handle to the memory-map root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR MemoryMapRootGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_HOST_HANDLE(
        "kernel_region",
        GetProcessMemoryRegionList(&KernelProcess),
        &MemoryRegionArrayDescriptor,
        NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from an exposed memory region descriptor.
 * @param Context Host callback context (unused for memory-map exposure)
 * @param Parent Handle to the region descriptor requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR MemoryRegionDescriptorGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPMEMORY_REGION_DESCRIPTOR Descriptor = (LPMEMORY_REGION_DESCRIPTOR)Parent;
    SAFE_USE_VALID_ID(Descriptor, KOID_MEMORY_REGION_DESCRIPTOR) {
        EXPOSE_BIND_STRING("tag", (Descriptor->Tag[0] == STR_NULL) ? TEXT("???") : Descriptor->Tag);
#ifdef __EXOS_64__
        EXPOSE_BIND_INTEGER("base_low", (U32)(Descriptor->CanonicalBase & 0xFFFFFFFF));
        EXPOSE_BIND_INTEGER("base_high", (U32)(Descriptor->CanonicalBase >> 32));
        EXPOSE_BIND_INTEGER("physical_low", (U32)(Descriptor->PhysicalBase & 0xFFFFFFFF));
        EXPOSE_BIND_INTEGER("physical_high", (U32)(Descriptor->PhysicalBase >> 32));
#else
        EXPOSE_BIND_INTEGER("base_low", (U32)Descriptor->CanonicalBase);
        EXPOSE_BIND_INTEGER("base_high", 0);
        EXPOSE_BIND_INTEGER("physical_low", (U32)Descriptor->PhysicalBase);
        EXPOSE_BIND_INTEGER("physical_high", 0);
#endif
        EXPOSE_BIND_INTEGER("physical_known", Descriptor->PhysicalBase != 0);
        EXPOSE_BIND_INTEGER("size", Descriptor->Size);
        EXPOSE_BIND_INTEGER("page_count", Descriptor->PageCount);
        EXPOSE_BIND_INTEGER("flags", Descriptor->Flags);
        EXPOSE_BIND_INTEGER("attributes", Descriptor->Attributes);
        EXPOSE_BIND_INTEGER("granularity", Descriptor->Granularity);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed memory region array.
 * @param Context Host callback context (unused for memory-map exposure)
 * @param Parent Handle to the region list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR MemoryRegionArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPMEMORY_REGION_LIST RegionList = (LPMEMORY_REGION_LIST)Parent;
    if (RegionList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", RegionList->Count);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one memory region descriptor from the exposed region array.
 * @param Context Host callback context (unused for memory-map exposure)
 * @param Parent Handle to the region list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting descriptor handle
 * @return SCRIPT_OK when the descriptor exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR MemoryRegionArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPMEMORY_REGION_LIST RegionList = (LPMEMORY_REGION_LIST)Parent;
    LPMEMORY_REGION_DESCRIPTOR Descriptor = NULL;

    if (RegionList == NULL || Index >= RegionList->Count) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    Descriptor = MemoryRegionGetByIndex(RegionList, Index);
    SAFE_USE_VALID_ID(Descriptor, KOID_MEMORY_REGION_DESCRIPTOR) {
        EXPOSE_SET_HOST_HANDLE(Descriptor, &MemoryRegionDescriptorDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR MemoryMapRootDescriptor = {
    MemoryMapRootGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR MemoryRegionDescriptorDescriptor = {
    MemoryRegionDescriptorGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR MemoryRegionArrayDescriptor = {
    MemoryRegionArrayGetProperty,
    MemoryRegionArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
