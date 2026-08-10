
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


    User Datagram Protocol (UDP)

\************************************************************************/

#ifndef UDP_H_INCLUDED
#define UDP_H_INCLUDED

#include "Base.h"
#include "core/Device.h"
#include "network/Network.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_UDP_HEADER {
    U16 SourcePort;       // Big-endian
    U16 DestinationPort;  // Big-endian
    U16 Length;           // Big-endian (header + data)
    U16 Checksum;         // Big-endian (0 = disabled)
} UDP_HEADER, *LPUDP_HEADER;

/************************************************************************/
// UDP Callback type for port handlers

typedef void (*UDP_PortHandler)(
    U32 SourceIP, U16 SourcePort, U16 DestinationPort, U32 DestinationIP, const U8* Payload, U32 PayloadLength);

/************************************************************************/
// Per-device UDP API
#include "network/UDPContext.h"

// Utility functions
U16 UDP_CalculateChecksum(
    U32 SourceIP, U32 DestinationIP, const UDP_HEADER* Header, const U8* Payload, U32 PayloadLength);

/************************************************************************/

#pragma pack(pop)

#endif  // UDP_H_INCLUDED
