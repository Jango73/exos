
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


    Dynamic Host Configuration Protocol (DHCP)

\************************************************************************/

#ifndef DHCP_H_INCLUDED
#define DHCP_H_INCLUDED

#include "Base.h"
#include "core/Device.h"
#include "network/Network.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// DHCP Constants

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67

#define DHCP_OP_REQUEST 1
#define DHCP_OP_REPLY 2

#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET 6

#define DHCP_MAGIC_COOKIE 0x63825363

/************************************************************************/
// DHCP Message Types (Option 53)

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7
#define DHCP_INFORM 8

/************************************************************************/
// DHCP Options

#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS_SERVER 6
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_LEASE_TIME 51
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAMETER_LIST 55
#define DHCP_OPTION_RENEWAL_TIME 58
#define DHCP_OPTION_REBIND_TIME 59
#define DHCP_OPTION_CLIENT_ID 61
#define DHCP_OPTION_END 255

/************************************************************************/
// DHCP Client States

#define DHCP_STATE_INIT 0
#define DHCP_STATE_SELECTING 1
#define DHCP_STATE_REQUESTING 2
#define DHCP_STATE_BOUND 3
#define DHCP_STATE_RENEWING 4
#define DHCP_STATE_REBINDING 5
#define DHCP_STATE_FAILED 6

/************************************************************************/
// Configuration

#define DHCP_RETRY_TIMEOUT_MILLIS (30 * 1000)  // 30 seconds for retry
#define DHCP_MAX_RETRIES 5
#define DHCP_RETRY_BACKOFF_MAX_SHIFT 2
#define DHCP_BROADCAST_FLAG 0x8000
#define DHCP_BROADCAST_IP 0xFFFFFFFF
#define DHCP_CLIENT_IDENTIFIER_LENGTH (1 + DHCP_HLEN_ETHERNET)

/************************************************************************/
// DHCP Message Structure

typedef struct tag_DHCP_MESSAGE {
    U8 Op;            // Operation: 1=request, 2=reply
    U8 HType;         // Hardware type: 1=Ethernet
    U8 HLen;          // Hardware address length: 6 for Ethernet
    U8 Hops;          // Client sets to zero
    U32 XID;          // Transaction ID (big-endian)
    U16 Secs;         // Seconds elapsed (big-endian)
    U16 Flags;        // Flags (big-endian)
    U32 CIAddr;       // Client IP address (big-endian)
    U32 YIAddr;       // Your (client) IP address (big-endian)
    U32 SIAddr;       // Server IP address (big-endian)
    U32 GIAddr;       // Gateway IP address (big-endian)
    U8 CHAddr[16];    // Client hardware address
    U8 SName[64];     // Server host name
    U8 File[128];     // Boot file name
    U32 MagicCookie;  // Magic cookie (0x63825363)
    U8 Options[312];  // Options (variable length)
} DHCP_MESSAGE, *LPDHCP_MESSAGE;

// Fixed fields size (up to and including MagicCookie)
#define DHCP_FIXED_FIELDS_SIZE MEMBER_OFFSET(DHCP_MESSAGE, Options)

/************************************************************************/

typedef struct tag_DHCP_CONTEXT {
    LPDEVICE Device;

    U8 LocalMacAddress[6];
    U32 TransactionID;
    U32 State;

    // Timing
    U32 StartMillis;
    U32 RetryCount;

    // Offered/Assigned configuration
    U32 OfferedIP_Be;
    U32 SubnetMask_Be;
    U32 Gateway_Be;
    U32 DNSServer_Be;
    U32 ServerID_Be;
    U32 LeaseTime;    // Seconds
    U32 RenewalTime;  // T1 (seconds)
    U32 RebindTime;   // T2 (seconds)

    // Lease management
    U32 LeaseStartMillis;
} DHCP_CONTEXT, *LPDHCP_CONTEXT;

/************************************************************************/

LPDHCP_CONTEXT DHCP_GetContext(LPDEVICE Device);
void DHCP_Initialize(LPDEVICE Device);
void DHCP_Destroy(LPDEVICE Device);
void DHCP_Start(LPDEVICE Device);
void DHCP_Tick(LPDEVICE Device);
void DHCP_OnUDPPacket(
    U32 SourceIP, U16 SourcePort, U16 DestinationPort, U32 DestinationIP, const U8* Payload, U32 PayloadLength);

/************************************************************************/

#pragma pack(pop)

#endif  // DHCP_H_INCLUDED
