
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


    Network Manager

\************************************************************************/

#ifndef NETWORKMANAGER_H_INCLUDED
#define NETWORKMANAGER_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "drivers/bus/PCI.h"
#include "network/Network.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_NETWORK_IP_CONFIG {
    U32 LocalIPv4_Be;
    U32 SubnetMask_Be;
    U32 Gateway_Be;
    U32 DNSServer_Be;
} NETWORK_IP_CONFIG, *LPNETWORK_IP_CONFIG;

/************************************************************************/

typedef struct tag_NETWORK_DEVICE_CONTEXT {
    LISTNODE_FIELDS
    LPPCI_DEVICE Device;
    NETWORK_IP_CONFIG ActiveConfig;
    NETWORK_IP_CONFIG StaticConfig;
    BOOL IsInitialized;
    BOOL IsReady;
    NT_RXCB OriginalCallback;
    U8 InterruptSlot;
    BOOL InterruptsEnabled;
    U32 MaintenanceCounter;
} NETWORK_DEVICE_CONTEXT, *LPNETWORK_DEVICE_CONTEXT;

/************************************************************************/

/**
 * @brief Initialize network stack for all network devices in the kernel.
 *
 * This function scans all PCI devices and initializes the network stack
 * (ARP, IPv4, TCP layers) for each network device found.
 */
void InitializeNetwork(void);

/**
 * @brief Initialize network stack for a specific network device.
 *
 * @param Device Pointer to the network PCI device
 * @param LocalIPv4_Be Local IPv4 address in big-endian format
 */
void NetworkManager_InitializeDevice(LPPCI_DEVICE Device, U32 LocalIPv4_Be);

/**
 * @brief Get the primary network device for global protocols like TCP.
 *
 * @return Pointer to the primary network device or NULL if none available
 */
LPPCI_DEVICE NetworkManager_GetPrimaryDevice(void);

/**
 * @brief Check if a specific network device is ready for use.
 *
 * Returns TRUE if static configuration is used or if DHCP has completed.
 *
 * @param Device Pointer to the network device
 * @return TRUE if network device is ready, FALSE otherwise
 */
BOOL NetworkManager_IsDeviceReady(LPDEVICE Device);

/**
 * @brief Get the configured DNS server for a network device.
 *
 * Returns the active configuration first, falling back to the static
 * configuration when DHCP did not provide a server.
 *
 * @param Device Pointer to the network device
 * @return DNS server IPv4 address in big-endian order, or 0 when unset
 */
U32 NetworkManager_GetDNSServer(LPDEVICE Device);

/**
 * @brief Perform periodic maintenance for a network device.
 *
 * This updates ARP, DHCP, TCP and socket state at a low frequency
 * regardless of interrupt delivery mode.
 */
void NetworkManager_MaintenanceTick(LPNETWORK_DEVICE_CONTEXT Context);

/************************************************************************/

#pragma pack(pop)

#endif  // NETWORKMANAGER_H_INCLUDED
