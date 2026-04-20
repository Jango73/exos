
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


    Network Manager

\************************************************************************/

#include "network/NetworkManager.h"

#include "network/ARP.h"
#include "User.h"
#include "network/IPv4.h"
#include "network/UDP.h"
#include "network/DHCP.h"
#include "network/TCP.h"
#include "core/Kernel.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/Network.h"
#include "core/Driver.h"
#include "network/Socket.h"
#include "utils/Helpers.h"
#include "text/CoreString.h"
#include "utils/TOML.h"

/************************************************************************/

#define NETWORK_MANAGER_VER_MAJOR 1
#define NETWORK_MANAGER_VER_MINOR 0

static UINT NetworkManagerDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION NetworkManagerDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = NETWORK_MANAGER_VER_MAJOR,
    .VersionMinor = NETWORK_MANAGER_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "NetworkManager",
    .Alias = "network",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = NetworkManagerDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the network manager driver descriptor.
 * @return Pointer to the network manager driver.
 */
LPDRIVER NetworkManagerGetDriver(void) {
    return &NetworkManagerDriver;
}

/************************************************************************/

/**
 * @brief Get IP value from configuration with fallback.
 *
 * @param configPath Configuration key path
 * @param fallbackValue IP value to use if parsing fails
 * @return Parsed IP in host order or fallback
 */
// Helper function to get network configuration from TOML with fallback
static U32 NetworkManager_GetConfigIP(LPCSTR configPath, U32 fallbackValue) {
    LPCSTR configValue = GetConfigurationValue(configPath);
    if (STRING_EMPTY(configValue) == FALSE) {
        U32 parsedIP = ParseIPAddress(configValue);
        if (parsedIP != 0) {
            return parsedIP;
        }
    }
    return fallbackValue;
}

/************************************************************************/

/**
 * @brief Get per-device network configuration with global fallback.
 *
 * @param deviceName Device identifier to match
 * @param configKey Key name inside the matching interface section
 * @param fallbackGlobalKey Global configuration key to use if missing
 * @param fallbackValue Value to use if no configuration found
 * @return Parsed IP in host order or fallback
 */
// Helper function to get per-device network configuration
static U32 NetworkManager_GetDeviceConfigIP(LPCSTR deviceName, LPCSTR configKey, LPCSTR fallbackGlobalKey, U32 fallbackValue) {
    STR path[128];
    U32 index = 0;

    // Try to find the device in [[NetworkInterface]] sections
    while (index < LOOP_LIMIT) {
        // Check if this NetworkInterface has a DeviceName that matches
        StringPrintFormat(path, TEXT(CONFIG_NETWORK_INTERFACE_DEVICE_NAME_FMT), index);

        LPCSTR configDeviceName = GetConfigurationValue(path);
        if (configDeviceName == NULL) break; // No more NetworkInterface entries

        // Check if this is the device we're looking for
        if (STRINGS_EQUAL(configDeviceName, deviceName)) {
            // Found the device, get the requested config value
            StringPrintFormat(path, TEXT(CONFIG_NETWORK_INTERFACE_CONFIG_FMT), index, configKey);

            LPCSTR configValue = GetConfigurationValue(path);
            if (STRING_EMPTY(configValue) == FALSE) {
                U32 parsedIP = ParseIPAddress(configValue);
                if (parsedIP != 0) {
                    return parsedIP;
                }
            }

            break;
        }
        index++;
    }

    // Fall back to global configuration
    SAFE_USE(fallbackGlobalKey) {
        U32 globalIP = NetworkManager_GetConfigIP(fallbackGlobalKey, fallbackValue);
        return globalIP;
    }

    return fallbackValue;
}

/************************************************************************/

// Forward declaration
static void NetworkManager_RxCallback(const U8 *Frame, U32 Length, LPVOID UserData);

/**
 * @brief Internal frame reception handler that dispatches to protocol layers.
 *
 * @param Frame Pointer to the received ethernet frame
 * @param Length Length of the frame in bytes
 * @param UserData Pointer to the NETWORK_DEVICE_CONTEXT
 */
static void NetworkManager_RxCallback(const U8 *Frame, U32 Length, LPVOID UserData) {
    LPNETWORK_DEVICE_CONTEXT Context = (LPNETWORK_DEVICE_CONTEXT)UserData;
    LPDEVICE Device = NULL;

    SAFE_USE_VALID_ID(Context, KOID_NETWORKDEVICE) {
        Device = (LPDEVICE)Context->Device;
    }

    if (!Device || !Frame || Length < 14U) {
        return;
    }

    U16 EthType = (U16)((Frame[12] << 8) | Frame[13]);

    // Dispatch to protocol layers
    switch (EthType) {
        case ETHTYPE_ARP:
            ARP_OnEthernetFrame(Device, Frame, Length);
            break;
        case ETHTYPE_IPV4:
            IPv4_OnEthernetFrame(Device, Frame, Length);
            break;
        default:
            break;
    }
}

/************************************************************************/

/**
 * @brief Find all network devices in the PCI device list.
 *
 * @return Number of network devices found
 */
static U32 NetworkManager_FindNetworkDevices(void) {
    LPLISTNODE Node;
    U32 Count = 0;
    LPLIST PciDeviceList = GetPCIDeviceList();
    LPLIST NetworkDeviceList = GetNetworkDeviceList();

    SAFE_USE(PciDeviceList) {
        SAFE_USE_VALID_ID(PciDeviceList->First, KOID_PCIDEVICE) {

            for (Node = PciDeviceList->First; Node != NULL; Node = Node->Next) {
                LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;

                SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {

                        if (Device->Driver->Type == DRIVER_TYPE_NETWORK) {
                            // Allocate a new network device context
                            LPNETWORK_DEVICE_CONTEXT Context = (LPNETWORK_DEVICE_CONTEXT)
                                CreateKernelObject(sizeof(NETWORK_DEVICE_CONTEXT), KOID_NETWORKDEVICE);

                            SAFE_USE(Context) {
                                Context->Device = Device;
                                
                                // Generate device name
                                GetDefaultDeviceName(Device->Name, (LPDEVICE)Device, DRIVER_TYPE_NETWORK);

                                // Use per-device configuration with fallback to global config
                                Context->ActiveConfig.LocalIPv4_Be = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("LocalIP"), TEXT(CONFIG_NETWORK_LOCAL_IP), Htonl(NETWORK_FALLBACK_IPV4_BASE + Count));
                                Context->ActiveConfig.SubnetMask_Be = 0;
                                Context->ActiveConfig.Gateway_Be = 0;
                                Context->ActiveConfig.DNSServer_Be = 0;
                                Context->StaticConfig.LocalIPv4_Be = Context->ActiveConfig.LocalIPv4_Be;
                                Context->StaticConfig.SubnetMask_Be = Htonl(NETWORK_FALLBACK_IPV4_NETMASK);
                                Context->StaticConfig.Gateway_Be = Htonl(NETWORK_FALLBACK_IPV4_GATEWAY);
                                Context->StaticConfig.DNSServer_Be = 0;
                                Context->IsInitialized = FALSE;
                                Context->IsReady = FALSE;
                                Context->OriginalCallback = NULL;
                                Context->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
                                Context->InterruptsEnabled = FALSE;
                                Context->MaintenanceCounter = 0;

                                // Add to kernel network device list (thread-safe with MUTEX_KERNEL)
                                LockMutex(MUTEX_KERNEL, INFINITY);
                                ListAddTail(NetworkDeviceList, (LPVOID)Context);
                                UnlockMutex(MUTEX_KERNEL);

                                Count++;
                            } else {
                                ERROR(TEXT("Failed to allocate network device context"));
                            }
                        }
                    }
                }
            }
        } else {
            WARNING(TEXT("No PCI devices available"));
        }
    } else {
        WARNING(TEXT("PCI device list is unavailable"));
    }

    return NetworkDeviceList != NULL ? NetworkDeviceList->NumItems : 0;
}

/************************************************************************/

/**
 * @brief Discover and initialize all network devices.
 *
 * Finds NICs, initializes each device, and leaves network ready when at least
 * one device exists.
 */
void InitializeNetwork(void) {
    // Find all network devices
    LPLIST NetworkDeviceList = GetNetworkDeviceList();

    NetworkManager_FindNetworkDevices();

    if (NetworkDeviceList == NULL || NetworkDeviceList->NumItems == 0) {
        WARNING(TEXT("No network devices found"));
        return;
    }

    // Initialize each network device
    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(Ctx, KOID_NETWORKDEVICE) {
                NetworkManager_InitializeDevice(Ctx->Device, Ctx->ActiveConfig.LocalIPv4_Be);
            }
        }
    }

}

/************************************************************************/

/**
 * @brief Driver command handler for the network manager.
 *
 * DF_LOAD discovers and initializes network devices once; DF_UNLOAD clears
 * readiness only.
 */
static UINT NetworkManagerDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((NetworkManagerDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeNetwork();
            NetworkManagerDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((NetworkManagerDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            NetworkManagerDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(NETWORK_MANAGER_VER_MAJOR, NETWORK_MANAGER_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Initialize a network device and attach protocol layers.
 *
 * Resets the device, gathers information, sets up ARP/IPv4/UDP/TCP,
 * configures static or DHCP addressing, installs RX callbacks, and enables
 * interrupts or polling as available.
 *
 * @param Device PCI network device to initialize
 * @param LocalIPv4_Be Local IPv4 address in big-endian order
 */
void NetworkManager_InitializeDevice(LPPCI_DEVICE Device, U32 LocalIPv4_Be) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {
            if (Device->Driver->Type != DRIVER_TYPE_NETWORK) {
                ERROR(TEXT("Device is not a network device"));
                return;
            }

            // Find device context in the network device list
            LPNETWORK_DEVICE_CONTEXT DeviceContext = NULL;
            LPLIST NetworkDeviceList = GetNetworkDeviceList();
            SAFE_USE(NetworkDeviceList) {
                for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
                    LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
                    SAFE_USE_VALID_ID(Ctx, KOID_NETWORKDEVICE) {
                        if (Ctx->Device == Device) {
                            DeviceContext = Ctx;
                            break;
                        }
                    }
                }
            }

            if (DeviceContext == NULL) {
                ERROR(TEXT("Device %p not found in network device list!"), (LPVOID)Device);
                return;
            }

            // Reset the device
            NETWORK_RESET Reset = {.Device = Device};
            Device->Driver->Command(DF_NT_RESET, (UINT)(LPVOID)&Reset);

            // Get device information
            NETWORK_INFO Info;
            MemorySet(&Info, 0, sizeof(Info));
            NETWORK_GET_INFO GetInfo = {.Device = Device, .Info = &Info};
            Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);

            // Initialize ARP subsystem for this device
            ARP_Initialize((LPDEVICE)Device, LocalIPv4_Be, &Info);

            // Initialize IPv4 subsystem for this device
            IPv4_Initialize((LPDEVICE)Device, LocalIPv4_Be);

            // Initialize UDP subsystem for this device
            UDP_Initialize((LPDEVICE)Device);

            // Initialize DHCP subsystem if enabled in configuration
            LPCSTR UseDHCP = GetConfigurationValue(TEXT(CONFIG_NETWORK_USE_DHCP));
            if (UseDHCP != NULL && STRINGS_EQUAL(UseDHCP, TEXT("1"))) {
                DHCP_Initialize((LPDEVICE)Device);
                DHCP_Start((LPDEVICE)Device);
                // Network will be marked ready when DHCP completes
            } else {
                // Mark network as ready immediately for static configuration
                DeviceContext->IsReady = TRUE;
            }

            // Configure network settings from TOML configuration (per-device with global fallback)
            U32 NetmaskBe = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("Netmask"), TEXT(CONFIG_NETWORK_NETMASK), Htonl(NETWORK_FALLBACK_IPV4_NETMASK));
            U32 GatewayBe = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("Gateway"), TEXT(CONFIG_NETWORK_GATEWAY), Htonl(NETWORK_FALLBACK_IPV4_GATEWAY));
            IPv4_SetNetworkConfig((LPDEVICE)Device, LocalIPv4_Be, NetmaskBe, GatewayBe);
            DeviceContext->ActiveConfig.SubnetMask_Be = NetmaskBe;
            DeviceContext->ActiveConfig.Gateway_Be = GatewayBe;
            DeviceContext->ActiveConfig.LocalIPv4_Be = LocalIPv4_Be;
            DeviceContext->StaticConfig.SubnetMask_Be = NetmaskBe;
            DeviceContext->StaticConfig.Gateway_Be = GatewayBe;
            DeviceContext->StaticConfig.LocalIPv4_Be = LocalIPv4_Be;

            // Initialize TCP subsystem (global for all devices)
            static BOOL DATA_SECTION TCPInitialized = FALSE;
            if (!TCPInitialized) {
                TCP_Initialize();
                TCPInitialized = TRUE;
            }

            // Install RX callback with device context as UserData
            NETWORK_SET_RX_CB SetRxCb = {.Device = Device, .Callback = NetworkManager_RxCallback, .UserData = (LPVOID)DeviceContext};
            U32 Result = Device->Driver->Command(DF_NT_SETRXCB, (UINT)(LPVOID)&SetRxCb);
            UNUSED(Result);

            // Mark device as initialized
            DeviceContext->IsInitialized = TRUE;

            DEVICE_INTERRUPT_CONFIG InterruptConfig;
            MemorySet(&InterruptConfig, 0, sizeof(InterruptConfig));
            InterruptConfig.Device = (LPDEVICE)Device;
            InterruptConfig.LegacyIRQ = Device->Info.IRQLine;
            InterruptConfig.TargetCPU = 0;
            InterruptConfig.VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
            InterruptConfig.InterruptEnabled = FALSE;

            U32 InterruptResult = Device->Driver->Command(DF_DEV_ENABLE_INTERRUPT, (UINT)(LPVOID)&InterruptConfig);
            if (InterruptResult == DF_RETURN_SUCCESS && InterruptConfig.VectorSlot != DEVICE_INTERRUPT_INVALID_SLOT) {
                DeviceContext->InterruptSlot = InterruptConfig.VectorSlot;
                DeviceContext->InterruptsEnabled = InterruptConfig.InterruptEnabled;
                if (!DeviceContext->InterruptsEnabled) {
                    WARNING(TEXT("Hardware interrupts unavailable, using polling on slot %u"),
                            DeviceContext->InterruptSlot);
                }
            } else {
                DeviceContext->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
                DeviceContext->InterruptsEnabled = FALSE;
                WARNING(TEXT("Falling back to polling mode (Result=%u, Slot=%u)"),
                        InterruptResult,
                        InterruptConfig.VectorSlot);
            }

            // Register TCP protocol handler now that device is initialized
            IPv4_RegisterProtocolHandler((LPDEVICE)Device, IPV4_PROTOCOL_TCP, TCP_OnIPv4Packet);
        }
    }
}

/************************************************************************/

/**
 * @brief Get the first initialized network device.
 *
 * @return Pointer to primary initialized PCI network device or NULL
 */
LPPCI_DEVICE NetworkManager_GetPrimaryDevice(void) {
    // Return the first initialized network device
    LPLIST NetworkDeviceList = GetNetworkDeviceList();
    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(Ctx, KOID_NETWORKDEVICE) {
                if (Ctx->IsInitialized) {
                    return Ctx->Device;
                }
            }
        }
    }
    return NULL;
}

/************************************************************************/

/**
 * @brief Determine if a given device is ready for network operations.
 *
 * @param Device Device to query
 * @return TRUE if ready, FALSE otherwise
 */
BOOL NetworkManager_IsDeviceReady(LPDEVICE Device) {
    LPLIST NetworkDeviceList = GetNetworkDeviceList();
    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(Ctx, KOID_NETWORKDEVICE) {
                if ((LPDEVICE)Ctx->Device == Device) {
                    return Ctx->IsReady;
                }
            }
        }
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Periodic maintenance routine for a network device context.
 *
 * Runs ARP/DHCP ticks, TCP update, and socket maintenance every 100 cycles
 * for initialized contexts.
 *
 * @param Context Network device context to service
 */
void NetworkManager_MaintenanceTick(LPNETWORK_DEVICE_CONTEXT Context) {
    SAFE_USE_VALID_ID(Context, KOID_NETWORKDEVICE) {
        if (!Context->IsInitialized) {
            return;
        }

        Context->MaintenanceCounter++;

        if (Context->MaintenanceCounter >= 100U) {
            Context->MaintenanceCounter = 0;

            SAFE_USE_VALID_ID(Context->Device, KOID_PCIDEVICE) {
                ARP_Tick((LPDEVICE)Context->Device);
                DHCP_Tick((LPDEVICE)Context->Device);
            }

            if (NetworkManager_GetPrimaryDevice() == Context->Device) {
                TCP_Update();
                SocketUpdate();
            }
        }
    }
}
