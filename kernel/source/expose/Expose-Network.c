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


    Script Exposure Helpers - Network

\************************************************************************/

#include "expose/Exposed.h"

#include "core/KernelData.h"
#include "network/NetworkManager.h"

/************************************************************************/

static int DATA_SECTION NetworkRootSentinel = 0;
SCRIPT_HOST_HANDLE NetworkRootHandle = &NetworkRootSentinel;

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed network root object.
 * @param Context Host callback context (unused for network exposure)
 * @param Parent Handle to the network root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR NetworkGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_HOST_HANDLE("device", GetNetworkDeviceList(), &NetworkDeviceArrayDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from an exposed network device.
 * @param Context Host callback context (unused for network exposure)
 * @param Parent Handle to the network device requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR NetworkDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    NETWORK_INFO Info;
    NETWORK_GET_INFO GetInfo;
    U32 IpHost = 0;
    LPPCI_DEVICE Device = NULL;

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Parent;
    SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
        Device = NetContext->Device;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            MemorySet(&Info, 0, sizeof(Info));
            MemorySet(&GetInfo, 0, sizeof(GetInfo));
            GetInfo.Device = Device;
            GetInfo.Info = &Info;

            if (Device->Driver != NULL) {
                Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);
            }

            IpHost = Ntohl(NetContext->ActiveConfig.LocalIPv4_Be);

            EXPOSE_BIND_STRING("name", Device->Name);
            EXPOSE_BIND_STRING("manufacturer", Device->Driver != NULL ? Device->Driver->Manufacturer : TEXT(""));
            EXPOSE_BIND_STRING("product", Device->Driver != NULL ? Device->Driver->Product : TEXT(""));
            EXPOSE_BIND_INTEGER("mac_0", Info.MAC[0]);
            EXPOSE_BIND_INTEGER("mac_1", Info.MAC[1]);
            EXPOSE_BIND_INTEGER("mac_2", Info.MAC[2]);
            EXPOSE_BIND_INTEGER("mac_3", Info.MAC[3]);
            EXPOSE_BIND_INTEGER("mac_4", Info.MAC[4]);
            EXPOSE_BIND_INTEGER("mac_5", Info.MAC[5]);
            EXPOSE_BIND_INTEGER("ip_0", (IpHost >> 24) & 0xFF);
            EXPOSE_BIND_INTEGER("ip_1", (IpHost >> 16) & 0xFF);
            EXPOSE_BIND_INTEGER("ip_2", (IpHost >> 8) & 0xFF);
            EXPOSE_BIND_INTEGER("ip_3", IpHost & 0xFF);
            EXPOSE_BIND_INTEGER("link_up", Info.LinkUp);
            EXPOSE_BIND_INTEGER("speed_mbps", Info.SpeedMbps);
            EXPOSE_BIND_INTEGER("duplex_full", Info.DuplexFull);
            EXPOSE_BIND_INTEGER("mtu", Info.MTU);
            EXPOSE_BIND_INTEGER("initialized", NetContext->IsInitialized);

            return SCRIPT_ERROR_UNDEFINED_VAR;
        }
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed network device array.
 * @param Context Host callback context (unused for network exposure)
 * @param Parent Handle to the network device list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR NetworkDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST NetworkDeviceList = (LPLIST)Parent;
    if (NetworkDeviceList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(NetworkDeviceList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one network device from the exposed network device array.
 * @param Context Host callback context (unused for network exposure)
 * @param Parent Handle to the network device list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting network device handle
 * @return SCRIPT_OK when the device exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR NetworkDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST NetworkDeviceList = (LPLIST)Parent;
    if (NetworkDeviceList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(NetworkDeviceList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)ListGetItem(NetworkDeviceList, Index);
    SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
        EXPOSE_SET_HOST_HANDLE(NetContext, &NetworkDeviceDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR NetworkDescriptor = {
    NetworkGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR NetworkDeviceDescriptor = {
    NetworkDeviceGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR NetworkDeviceArrayDescriptor = {
    NetworkDeviceArrayGetProperty,
    NetworkDeviceArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
