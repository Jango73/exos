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


    Script Exposure Helpers - PCI

\************************************************************************/

#include "expose/Exposed.h"

#include "core/DriverEnum.h"
#include "memory/Heap.h"

/************************************************************************/

typedef struct tag_PCI_BUS_HANDLE {
    U8 Number;
} PCI_BUS_HANDLE, *LPPCI_BUS_HANDLE;

typedef struct tag_PCI_DEVICE_HANDLE {
    DRIVER_ENUM_PCI_DEVICE Data;
} PCI_DEVICE_HANDLE, *LPPCI_DEVICE_HANDLE;

/************************************************************************/

/**
 * @brief Release a PCI host handle allocated for script access.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Handle Host handle to release
 */
static void PciHostReleaseHandle(LPVOID Context, SCRIPT_HOST_HANDLE Handle) {
    UNUSED(Context);

    if (Handle != NULL) {
        HeapFree(Handle);
    }
}

/************************************************************************/

static BOOL PciEnumFetchByIndex(UINT Index, DRIVER_ENUM_PCI_DEVICE* Data) {
    DRIVER_ENUM_QUERY Query;
    DRIVER_ENUM_ITEM Item;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT ProviderIndex = 0;
    UINT MatchIndex = 0;

    if (Data == NULL) {
        return FALSE;
    }

    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = ENUM_DOMAIN_PCI_DEVICE;
    Query.Flags = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (Item.DataSize < sizeof(DRIVER_ENUM_PCI_DEVICE)) {
                ProviderIndex++;
                break;
            }

            if (MatchIndex == Index) {
                MemoryCopy(Data, Item.Data, sizeof(DRIVER_ENUM_PCI_DEVICE));
                return TRUE;
            }

            MatchIndex++;
        }

        ProviderIndex++;
    }

    return FALSE;
}

/************************************************************************/

static UINT PciEnumGetCount(void) {
    DRIVER_ENUM_QUERY Query;
    DRIVER_ENUM_ITEM Item;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT ProviderIndex = 0;
    UINT Count = 0;

    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = ENUM_DOMAIN_PCI_DEVICE;
    Query.Flags = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (Item.DataSize >= sizeof(DRIVER_ENUM_PCI_DEVICE)) {
                Count++;
            }
        }

        ProviderIndex++;
    }

    return Count;
}

/************************************************************************/

static UINT PciBusGetCount(void) {
    DRIVER_ENUM_PCI_DEVICE Device;
    U8 SeenBus[256];
    UINT DeviceCount = PciEnumGetCount();
    UINT UniqueCount = 0;
    UINT Index;

    MemorySet(SeenBus, 0, sizeof(SeenBus));

    for (Index = 0; Index < DeviceCount; Index++) {
        if (!PciEnumFetchByIndex(Index, &Device)) {
            continue;
        }
        if (SeenBus[Device.Bus] == 0) {
            SeenBus[Device.Bus] = 1;
            UniqueCount++;
        }
    }

    return UniqueCount;
}

/************************************************************************/

static BOOL PciBusGetByIndex(UINT Index, U8* OutBus) {
    DRIVER_ENUM_PCI_DEVICE Device;
    U8 SeenBus[256];
    UINT DeviceCount = PciEnumGetCount();
    UINT MatchIndex = 0;
    UINT DeviceIndex;

    if (OutBus == NULL) {
        return FALSE;
    }

    MemorySet(SeenBus, 0, sizeof(SeenBus));

    for (DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++) {
        if (!PciEnumFetchByIndex(DeviceIndex, &Device)) {
            continue;
        }
        if (SeenBus[Device.Bus] != 0) {
            continue;
        }

        SeenBus[Device.Bus] = 1;
        if (MatchIndex == Index) {
            *OutBus = Device.Bus;
            return TRUE;
        }

        MatchIndex++;
    }

    return FALSE;
}

/************************************************************************/

static UINT PciBusGetDeviceCount(U8 Bus) {
    DRIVER_ENUM_PCI_DEVICE Device;
    UINT DeviceCount = PciEnumGetCount();
    UINT Count = 0;
    UINT Index;

    for (Index = 0; Index < DeviceCount; Index++) {
        if (!PciEnumFetchByIndex(Index, &Device)) {
            continue;
        }
        if (Device.Bus == Bus) {
            Count++;
        }
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a PCI bus exposed to the script engine.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI bus instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciBusGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPPCI_BUS_HANDLE Bus = (LPPCI_BUS_HANDLE)Parent;
    if (Bus == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("number", Bus->Number);
    EXPOSE_BIND_INTEGER("deviceCount", PciBusGetDeviceCount(Bus->Number));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed PCI bus array.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI bus array exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciBusArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("count", PciBusGetCount());

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a PCI bus from the exposed PCI bus array.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI bus array exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting PCI bus handle
 * @return SCRIPT_OK when the bus exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciBusArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_ARRAY_GUARD();

    U8 BusNumber = 0;
    if (!PciBusGetByIndex(Index, &BusNumber)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPPCI_BUS_HANDLE Bus = (LPPCI_BUS_HANDLE)HeapAlloc(sizeof(PCI_BUS_HANDLE));
    if (Bus == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    Bus->Number = BusNumber;
    EXPOSE_SET_HOST_HANDLE(Bus, &PciBusDescriptor, NULL, TRUE);

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a PCI device exposed to the script engine.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI device instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPPCI_DEVICE_HANDLE Device = (LPPCI_DEVICE_HANDLE)Parent;
    if (Device == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("bus", Device->Data.Bus);
    EXPOSE_BIND_INTEGER("device", Device->Data.Dev);
    EXPOSE_BIND_INTEGER("function", Device->Data.Func);
    EXPOSE_BIND_INTEGER("vendorId", Device->Data.VendorID);
    EXPOSE_BIND_INTEGER("deviceId", Device->Data.DeviceID);
    EXPOSE_BIND_INTEGER("baseClass", Device->Data.BaseClass);
    EXPOSE_BIND_INTEGER("subClass", Device->Data.SubClass);
    EXPOSE_BIND_INTEGER("progIf", Device->Data.ProgIF);
    EXPOSE_BIND_INTEGER("revision", Device->Data.Revision);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed PCI device array.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI device array exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("count", PciEnumGetCount());

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a PCI device from the exposed PCI device array.
 * @param Context Host callback context (unused for PCI exposure)
 * @param Parent Handle to the PCI device array exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting PCI device handle
 * @return SCRIPT_OK when the device exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR PciDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_ARRAY_GUARD();

    DRIVER_ENUM_PCI_DEVICE Data;
    if (!PciEnumFetchByIndex(Index, &Data)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPPCI_DEVICE_HANDLE Device = (LPPCI_DEVICE_HANDLE)HeapAlloc(sizeof(PCI_DEVICE_HANDLE));
    if (Device == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemoryCopy(&Device->Data, &Data, sizeof(Data));
    EXPOSE_SET_HOST_HANDLE(Device, &PciDeviceDescriptor, NULL, TRUE);

    return SCRIPT_OK;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR PciBusDescriptor = {
    PciBusGetProperty,
    NULL,
    PciHostReleaseHandle,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR PciBusArrayDescriptor = {
    PciBusArrayGetProperty,
    PciBusArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR PciDeviceDescriptor = {
    PciDeviceGetProperty,
    NULL,
    PciHostReleaseHandle,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR PciDeviceArrayDescriptor = {
    PciDeviceArrayGetProperty,
    PciDeviceArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
