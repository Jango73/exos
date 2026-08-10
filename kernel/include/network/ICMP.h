
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


    Internet Control Message Protocol (ICMP)

\************************************************************************/

#ifndef ICMP_H_INCLUDED
#define ICMP_H_INCLUDED

#include "Base.h"
#include "core/Device.h"
#include "network/Network.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// ICMP message types

#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_DESTINATION_UNREACHABLE 3
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_TIME_EXCEEDED 11

#define ICMP_MAX_PENDING_ECHOES 8
#define ICMP_ECHO_PAYLOAD_SIZE 32

/************************************************************************/

typedef struct tag_ICMP_HEADER {
    U8 Type;             // Message type
    U8 Code;             // Message code
    U16 Checksum;        // Checksum of the whole message (big-endian)
    U16 Identifier;      // Echo identifier (big-endian)
    U16 SequenceNumber;  // Echo sequence number (big-endian)
} ICMP_HEADER, *LPICMP_HEADER;

/************************************************************************/

typedef enum tag_ICMP_ECHO_STATUS {
    ICMP_ECHO_PENDING = 0,
    ICMP_ECHO_RECEIVED = 1,
    ICMP_ECHO_ENTRY_NOT_FOUND = 2
} ICMP_ECHO_STATUS,
    *LPICMP_ECHO_STATUS;

/************************************************************************/

void ICMP_Initialize(LPDEVICE Device);
void ICMP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);
BOOL ICMP_StartEcho(LPDEVICE Device, U32 DestinationIP, U16* OutIdentifier, U16* OutSequenceNumber);
ICMP_ECHO_STATUS ICMP_CheckEcho(U16 Identifier, U16 SequenceNumber, U32* OutSourceIP, U32* OutRoundTripMilliseconds);
void ICMP_CancelEcho(U16 Identifier, U16 SequenceNumber);

/************************************************************************/

#pragma pack(pop)

#endif  // ICMP_H_INCLUDED
