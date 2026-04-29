
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


    Helper functions definitions

\************************************************************************/

#ifndef HELPERS_H_INCLUDED
#define HELPERS_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "TOML.h"
#include "fs/File-System.h"
#include "fs/SystemFS.h"
#include "user/Account.h"

/***************************************************************************/
// Configuration paths

#define CONFIG_GENERAL_QUANTUM_MS "General.QuantumMS"
#define CONFIG_GENERAL_POLLING "General.Polling"
#define CONFIG_GENERAL_DEFERRED_WORK_WAIT_TIMEOUT_MS "General.DeferredWorkWaitTimeoutMS"
#define CONFIG_GENERAL_DEFERRED_WORK_POLL_DELAY_MS "General.DeferredWorkPollDelayMS"
#define CONFIG_GENERAL_DEVICE_INTERRUPT_SLOTS "General.DeviceInterruptSlots"
#define CONFIG_SESSION_TIMEOUT_SECONDS "Session.TimeoutSeconds"
#define CONFIG_SESSION_TIMEOUT_MINUTES "Session.TimeoutMinutes"
#define CONFIG_NETWORK_LOCAL_IP "Network.LocalIP"
#define CONFIG_NETWORK_NETMASK "Network.Netmask"
#define CONFIG_NETWORK_GATEWAY "Network.Gateway"
#define CONFIG_NETWORK_DEFAULT_PORT "Network.DefaultPort"
#define CONFIG_NETWORK_USE_DHCP "Network.UseDHCP"
#define CONFIG_TCP_EPHEMERAL_START "TCP.EphemeralPortStart"
#define CONFIG_TCP_SEND_BUFFER_SIZE "TCP.SendBufferSize"
#define CONFIG_TCP_RECEIVE_BUFFER_SIZE "TCP.ReceiveBufferSize"
#define CONFIG_TASK_MINIMUM_TASK_STACK_SIZE "Task.MinimumTaskStackSize"
#define CONFIG_TASK_MINIMUM_SYSTEM_STACK_SIZE "Task.MinimumSystemStackSize"

// Per-device network interface configuration (format strings for dynamic paths)
#define CONFIG_NETWORK_INTERFACE_DEVICE_NAME_FMT "NetworkInterface.%u.DeviceName"
#define CONFIG_NETWORK_INTERFACE_CONFIG_FMT "NetworkInterface.%u.%s"

/***************************************************************************/
// Configuration fallbacks

#define DEFERRED_WORK_MAX_ITEMS 16        // Maximum number of deferred work items fallback
#define DEFERRED_WORK_WAIT_TIMEOUT_MS 50  // Deferred work wait timeout fallback (ms)
#define DEFERRED_WORK_POLL_DELAY_MS 5     // Deferred work poll delay fallback (ms)

// Network
#define NETWORK_FALLBACK_IPV4_BASE 0xC0A8380A     // Default IPv4 base address fallback
#define NETWORK_FALLBACK_IPV4_NETMASK 0xFFFFFF00  // Default IPv4 netmask fallback
#define NETWORK_FALLBACK_IPV4_GATEWAY 0xC0A83801  // Default IPv4 gateway fallback

// TCP
#define TCP_EPHEMERAL_PORT_START_FALLBACK N_32KB  // Default TCP ephemeral port start fallback
#define TCP_RETRANSMIT_TIMEOUT 3000               // TCP retransmit timeout fallback (ms)
#define TCP_TIME_WAIT_TIMEOUT 30000               // TCP TIME-WAIT timeout fallback (ms)
#define TCP_MAX_RETRANSMITS 5                     // TCP maximum retransmits fallback
#define TCP_SEND_BUFFER_SIZE N_32KB               // TCP send buffer size fallback
#define TCP_RECV_BUFFER_SIZE N_32KB               // TCP receive buffer size fallback

/***************************************************************************/

LPUSER_ACCOUNT GetCurrentUser(void);
LPTOML GetConfiguration(void);
LPFILESYSTEM GetSystemFS(void);
LPSYSTEMFSFILESYSTEM GetSystemFSFilesystem(void);
LPCSTR GetConfigurationValue(LPCSTR path);

/***************************************************************************/

#endif
