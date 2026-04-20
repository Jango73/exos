
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


    Script Exposure Helpers - USB

\************************************************************************/

#include "expose/Exposed.h"

#include "core/DriverEnum.h"
#include "memory/Heap.h"
#include "core/KernelData.h"
#include "drivers/storage/USBStorage.h"
#include "drivers/usb/XHCI.h"

/************************************************************************/

#define EXPOSE_ACCESS_USB_PORTS (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

typedef struct tag_USB_PORT_HANDLE {
    DRIVER_ENUM_XHCI_PORT Data;
} USB_PORT_HANDLE, *LPUSB_PORT_HANDLE;

typedef struct tag_USB_DEVICE_HANDLE {
    DRIVER_ENUM_USB_DEVICE Data;
} USB_DEVICE_HANDLE, *LPUSB_DEVICE_HANDLE;

typedef struct tag_USB_NODE_HANDLE {
    DRIVER_ENUM_USB_NODE Data;
} USB_NODE_HANDLE, *LPUSB_NODE_HANDLE;

/************************************************************************/

static int DATA_SECTION UsbRootSentinel = 0;
static int DATA_SECTION UsbPortArraySentinel = 0;
static int DATA_SECTION UsbDeviceArraySentinel = 0;
static int DATA_SECTION UsbNodeArraySentinel = 0;

SCRIPT_HOST_HANDLE UsbRootHandle = &UsbRootSentinel;

/************************************************************************/

/**
 * @brief Release a USB host handle allocated for script access.
 * @param Context Host callback context (unused for USB exposure)
 * @param Handle Host handle to release
 */
static void UsbHostReleaseHandle(LPVOID Context, SCRIPT_HOST_HANDLE Handle) {
    UNUSED(Context);

    if (Handle != NULL) {
        HeapFree(Handle);
    }
}

/************************************************************************/

static BOOL UsbEnumFetchByIndex(UINT Domain, UINT Index, void* OutData, UINT DataSize) {
    DRIVER_ENUM_QUERY Query;
    DRIVER_ENUM_ITEM Item;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT ProviderIndex = 0;
    UINT MatchIndex = 0;

    if (OutData == NULL || DataSize == 0) {
        return FALSE;
    }

    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = Domain;
    Query.Flags = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (Item.DataSize < DataSize) {
                ProviderIndex++;
                break;
            }

            if (MatchIndex == Index) {
                MemoryCopy(OutData, Item.Data, DataSize);
                return TRUE;
            }

            MatchIndex++;
        }

        ProviderIndex++;
    }

    return FALSE;
}

/************************************************************************/

static UINT UsbEnumGetCount(UINT Domain) {
    DRIVER_ENUM_QUERY Query;
    DRIVER_ENUM_ITEM Item;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT ProviderIndex = 0;
    UINT Count = 0;

    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = Domain;
    Query.Flags = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            Count++;
        }

        ProviderIndex++;
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed USB root object.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    if (STRINGS_EQUAL_NO_CASE(Property, TEXT("port"))) {
        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_USB_PORTS, NULL);
        OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
        OutValue->Value.HostHandle = &UsbPortArraySentinel;
        OutValue->HostDescriptor = &UsbPortArrayDescriptor;
        OutValue->HostContext = NULL;
        OutValue->OwnsValue = FALSE;
        return SCRIPT_OK;
    }

    EXPOSE_BIND_HOST_HANDLE("node", &UsbNodeArraySentinel, &UsbNodeArrayDescriptor, NULL);
    EXPOSE_BIND_HOST_HANDLE("drive", GetUsbStorageList(), &UsbDriveArrayDescriptor, NULL);
    EXPOSE_BIND_HOST_HANDLE("device", &UsbDeviceArraySentinel, &UsbDeviceArrayDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a USB port exposed to the script engine.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB port instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbPortGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_USB_PORTS, NULL);

    LPUSB_PORT_HANDLE Port = (LPUSB_PORT_HANDLE)Parent;
    if (Port == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("bus", Port->Data.Bus);
    EXPOSE_BIND_INTEGER("device", Port->Data.Dev);
    EXPOSE_BIND_INTEGER("function", Port->Data.Func);
    EXPOSE_BIND_INTEGER("port_number", Port->Data.PortNumber);
    EXPOSE_BIND_INTEGER("port_status", Port->Data.PortStatus);
    EXPOSE_BIND_INTEGER("speed_id", Port->Data.SpeedId);
    EXPOSE_BIND_INTEGER("connected", Port->Data.Connected);
    EXPOSE_BIND_INTEGER("enabled", Port->Data.Enabled);
    EXPOSE_BIND_INTEGER("last_enum_error", Port->Data.LastEnumError);
    EXPOSE_BIND_STRING("last_enum_error_text", XHCIEnumErrorToString(Port->Data.LastEnumError));
    EXPOSE_BIND_INTEGER("last_enum_completion", Port->Data.LastEnumCompletion);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed USB port array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB port array exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbPortArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_USB_PORTS, NULL);

    EXPOSE_BIND_INTEGER("count", UsbEnumGetCount(ENUM_DOMAIN_XHCI_PORT));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a USB port from the exposed USB port array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB port array exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting USB port handle
 * @return SCRIPT_OK when the port exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbPortArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_ARRAY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_USB_PORTS, NULL);

    DRIVER_ENUM_XHCI_PORT Data;
    if (!UsbEnumFetchByIndex(ENUM_DOMAIN_XHCI_PORT, Index, &Data, sizeof(Data))) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPUSB_PORT_HANDLE Port = (LPUSB_PORT_HANDLE)HeapAlloc(sizeof(USB_PORT_HANDLE));
    if (Port == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemoryCopy(&Port->Data, &Data, sizeof(Data));
    EXPOSE_SET_HOST_HANDLE(Port, &UsbPortDescriptor, NULL, TRUE);

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a USB device exposed to the script engine.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB device instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    LPUSB_DEVICE_HANDLE Device = (LPUSB_DEVICE_HANDLE)Parent;
    if (Device == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("bus", Device->Data.Bus);
    EXPOSE_BIND_INTEGER("device", Device->Data.Dev);
    EXPOSE_BIND_INTEGER("function", Device->Data.Func);
    EXPOSE_BIND_INTEGER("port_number", Device->Data.PortNumber);
    EXPOSE_BIND_INTEGER("address", Device->Data.Address);
    EXPOSE_BIND_INTEGER("speed_id", Device->Data.SpeedId);
    EXPOSE_BIND_INTEGER("vendor_id", Device->Data.VendorID);
    EXPOSE_BIND_INTEGER("product_id", Device->Data.ProductID);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed USB device array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB device array exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("count", UsbEnumGetCount(ENUM_DOMAIN_USB_DEVICE));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a USB device from the exposed USB device array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB device array exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting USB device handle
 * @return SCRIPT_OK when the device exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_ARRAY_GUARD();

    DRIVER_ENUM_USB_DEVICE Data;
    if (!UsbEnumFetchByIndex(ENUM_DOMAIN_USB_DEVICE, Index, &Data, sizeof(Data))) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPUSB_DEVICE_HANDLE Device = (LPUSB_DEVICE_HANDLE)HeapAlloc(sizeof(USB_DEVICE_HANDLE));
    if (Device == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemoryCopy(&Device->Data, &Data, sizeof(Data));
    EXPOSE_SET_HOST_HANDLE(Device, &UsbDeviceDescriptor, NULL, TRUE);

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a USB drive exposed to the script engine.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB drive instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDriveGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Parent;
    SAFE_USE_VALID_ID(Entry, KOID_USBSTORAGE) {
        EXPOSE_BIND_INTEGER("address", Entry->Address);
        EXPOSE_BIND_INTEGER("vendor_id", Entry->VendorId);
        EXPOSE_BIND_INTEGER("product_id", Entry->ProductId);
        EXPOSE_BIND_INTEGER("block_count", Entry->BlockCount);
        EXPOSE_BIND_INTEGER("block_size", Entry->BlockSize);
        EXPOSE_BIND_INTEGER("present", Entry->Present);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed USB drive array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB drive list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDriveArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST UsbDriveList = (LPLIST)Parent;
    if (UsbDriveList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(UsbDriveList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a USB drive from the exposed USB drive array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB drive list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting USB drive handle
 * @return SCRIPT_OK when the drive exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbDriveArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST UsbDriveList = (LPLIST)Parent;
    if (UsbDriveList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(UsbDriveList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)ListGetItem(UsbDriveList, Index);
    SAFE_USE_VALID_ID(Entry, KOID_USBSTORAGE) {
        EXPOSE_SET_HOST_HANDLE(Entry, &UsbDriveDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a USB tree node exposed to the script engine.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB tree node instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbNodeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    LPUSB_NODE_HANDLE Node = (LPUSB_NODE_HANDLE)Parent;
    if (Node == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("node_type", Node->Data.NodeType);
    EXPOSE_BIND_INTEGER("bus", Node->Data.Bus);
    EXPOSE_BIND_INTEGER("device", Node->Data.Dev);
    EXPOSE_BIND_INTEGER("function", Node->Data.Func);
    EXPOSE_BIND_INTEGER("port_number", Node->Data.PortNumber);
    EXPOSE_BIND_INTEGER("address", Node->Data.Address);
    EXPOSE_BIND_INTEGER("speed_id", Node->Data.SpeedId);
    EXPOSE_BIND_INTEGER("device_class", Node->Data.DeviceClass);
    EXPOSE_BIND_INTEGER("device_sub_class", Node->Data.DeviceSubClass);
    EXPOSE_BIND_INTEGER("device_protocol", Node->Data.DeviceProtocol);
    EXPOSE_BIND_INTEGER("config_value", Node->Data.ConfigValue);
    EXPOSE_BIND_INTEGER("config_attributes", Node->Data.ConfigAttributes);
    EXPOSE_BIND_INTEGER("config_max_power", Node->Data.ConfigMaxPower);
    EXPOSE_BIND_INTEGER("interface_number", Node->Data.InterfaceNumber);
    EXPOSE_BIND_INTEGER("alternate_setting", Node->Data.AlternateSetting);
    EXPOSE_BIND_INTEGER("interface_class", Node->Data.InterfaceClass);
    EXPOSE_BIND_INTEGER("interface_sub_class", Node->Data.InterfaceSubClass);
    EXPOSE_BIND_INTEGER("interface_protocol", Node->Data.InterfaceProtocol);
    EXPOSE_BIND_INTEGER("endpoint_address", Node->Data.EndpointAddress);
    EXPOSE_BIND_INTEGER("endpoint_attributes", Node->Data.EndpointAttributes);
    EXPOSE_BIND_INTEGER("endpoint_max_packet_size", Node->Data.EndpointMaxPacketSize);
    EXPOSE_BIND_INTEGER("endpoint_interval", Node->Data.EndpointInterval);
    EXPOSE_BIND_INTEGER("vendor_id", Node->Data.VendorID);
    EXPOSE_BIND_INTEGER("product_id", Node->Data.ProductID);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed USB tree node array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB tree node array exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbNodeArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("count", UsbEnumGetCount(ENUM_DOMAIN_USB_NODE));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a USB tree node from the exposed USB node array.
 * @param Context Host callback context (unused for USB exposure)
 * @param Parent Handle to the USB node array exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting USB node handle
 * @return SCRIPT_OK when the node exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR UsbNodeArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_ARRAY_GUARD();

    DRIVER_ENUM_USB_NODE Data;
    if (!UsbEnumFetchByIndex(ENUM_DOMAIN_USB_NODE, Index, &Data, sizeof(Data))) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPUSB_NODE_HANDLE Node = (LPUSB_NODE_HANDLE)HeapAlloc(sizeof(USB_NODE_HANDLE));
    if (Node == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemoryCopy(&Node->Data, &Data, sizeof(Data));
    EXPOSE_SET_HOST_HANDLE(Node, &UsbNodeDescriptor, NULL, TRUE);

    return SCRIPT_OK;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR UsbDescriptor = {
    UsbGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbPortDescriptor = {
    UsbPortGetProperty,
    NULL,
    UsbHostReleaseHandle,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbPortArrayDescriptor = {
    UsbPortArrayGetProperty,
    UsbPortArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbDeviceDescriptor = {
    UsbDeviceGetProperty,
    NULL,
    UsbHostReleaseHandle,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbDeviceArrayDescriptor = {
    UsbDeviceArrayGetProperty,
    UsbDeviceArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbDriveDescriptor = {
    UsbDriveGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbDriveArrayDescriptor = {
    UsbDriveArrayGetProperty,
    UsbDriveArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbNodeDescriptor = {
    UsbNodeGetProperty,
    NULL,
    UsbHostReleaseHandle,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR UsbNodeArrayDescriptor = {
    UsbNodeArrayGetProperty,
    UsbNodeArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
