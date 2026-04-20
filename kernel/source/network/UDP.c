
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


    User Datagram Protocol (UDP)

\************************************************************************/

#include "network/UDP.h"
#include "network/IPv4.h"
#include "core/Device.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "utils/NetworkChecksum.h"

/************************************************************************/
// Global device pointer (single network device assumption)

static LPDEVICE DATA_SECTION g_UDPDevice = NULL;

/************************************************************************/

LPUDP_CONTEXT UDP_GetContext(LPDEVICE Device) {
    return (LPUDP_CONTEXT)GetDeviceContext(Device, KOID_UDP);
}

/************************************************************************/

/**
 * @brief Calculates the UDP checksum including pseudo-header.
 *
 * @param SourceIP      Source IPv4 address (big-endian).
 * @param DestinationIP Destination IPv4 address (big-endian).
 * @param Header        Pointer to UDP header.
 * @param Payload       Pointer to UDP payload.
 * @param PayloadLength Length of payload in bytes.
 * @return Calculated checksum in network byte order.
 */

U16 UDP_CalculateChecksum(U32 SourceIP, U32 DestinationIP, const UDP_HEADER* Header, const U8* Payload, U32 PayloadLength) {
    U32 Accumulator = 0;
    U8 PseudoHeader[12];

    // Build pseudo-header: Source IP (4) + Dest IP (4) + Zero (1) + Protocol (1) + UDP Length (2)
    MemoryCopy(PseudoHeader, (const U8*)&SourceIP, 4);
    MemoryCopy(PseudoHeader + 4, (const U8*)&DestinationIP, 4);
    PseudoHeader[8] = 0;
    PseudoHeader[9] = IPV4_PROTOCOL_UDP;
    MemoryCopy(PseudoHeader + 10, (const U8*)&Header->Length, 2);

    // Accumulate pseudo-header
    Accumulator = NetworkChecksum_Calculate_Accumulate(PseudoHeader, 12, 0);

    // Accumulate UDP header (with checksum field set to 0)
    U8 HeaderCopy[sizeof(UDP_HEADER)];
    MemoryCopy(HeaderCopy, (const U8*)Header, sizeof(UDP_HEADER));
    ((LPUDP_HEADER)HeaderCopy)->Checksum = 0;
    Accumulator = NetworkChecksum_Calculate_Accumulate(HeaderCopy, sizeof(UDP_HEADER), Accumulator);

    // Accumulate payload
    if (Payload != NULL && PayloadLength > 0) {
        Accumulator = NetworkChecksum_Calculate_Accumulate(Payload, PayloadLength, Accumulator);
    }

    // Finalize checksum
    U16 Checksum = NetworkChecksum_Finalize(Accumulator);

    // UDP checksum of 0x0000 is special (means checksum disabled), convert to 0xFFFF
    if (Checksum == 0) {
        Checksum = 0xFFFF;
    }

    return Checksum;
}

/************************************************************************/

/**
 * @brief Initializes UDP context for a device.
 *
 * @param Device Network device to initialize UDP for.
 */

void UDP_Initialize(LPDEVICE Device) {
    LPUDP_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = (LPUDP_CONTEXT)KernelHeapAlloc(sizeof(UDP_CONTEXT));
    if (Context == NULL) {
        ERROR(TEXT("Failed to allocate UDP context"));
        return;
    }

    MemorySet(Context, 0, sizeof(UDP_CONTEXT));
    Context->Device = Device;

    // Initialize port bindings
    for (Index = 0; Index < UDP_MAX_PORTS; Index++) {
        Context->PortBindings[Index].IsValid = 0;
    }

    SetDeviceContext(Device, KOID_UDP, (LPVOID)Context);

    // Store global device reference
    g_UDPDevice = Device;

    // Register UDP as IPv4 protocol handler
    IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_UDP, UDP_OnIPv4Packet);

}

/************************************************************************/

/**
 * @brief Destroys UDP context for a device.
 *
 * @param Device Network device to destroy UDP context for.
 */

void UDP_Destroy(LPDEVICE Device) {
    LPUDP_CONTEXT Context;

    if (Device == NULL) return;

    Context = UDP_GetContext(Device);
    SAFE_USE(Context) {
        KernelHeapFree(Context);
        SetDeviceContext(Device, KOID_UDP, NULL);
    }
}

/************************************************************************/

/**
 * @brief Registers a port handler for incoming UDP packets.
 *
 * @param Device  Network device.
 * @param Port    Port number (host byte order).
 * @param Handler Callback function for this port.
 */

void UDP_RegisterPortHandler(LPDEVICE Device, U16 Port, UDP_PortHandler Handler) {
    LPUDP_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = UDP_GetContext(Device);
    SAFE_USE(Context) {
        // Check if port already registered
        for (Index = 0; Index < UDP_MAX_PORTS; Index++) {
            if (Context->PortBindings[Index].IsValid && Context->PortBindings[Index].Port == Port) {
                WARNING(TEXT("Port %u already registered, overwriting"), Port);
                Context->PortBindings[Index].Handler = Handler;
                return;
            }
        }

        // Find free slot
        for (Index = 0; Index < UDP_MAX_PORTS; Index++) {
            if (!Context->PortBindings[Index].IsValid) {
                Context->PortBindings[Index].Port = Port;
                Context->PortBindings[Index].Handler = Handler;
                Context->PortBindings[Index].IsValid = 1;
                return;
            }
        }

        ERROR(TEXT("No free port binding slots"));
    }
}

/************************************************************************/

/**
 * @brief Unregisters a port handler.
 *
 * @param Device Network device.
 * @param Port   Port number (host byte order).
 */

void UDP_UnregisterPortHandler(LPDEVICE Device, U16 Port) {
    LPUDP_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = UDP_GetContext(Device);
    SAFE_USE(Context) {
        for (Index = 0; Index < UDP_MAX_PORTS; Index++) {
            if (Context->PortBindings[Index].IsValid && Context->PortBindings[Index].Port == Port) {
                Context->PortBindings[Index].IsValid = 0;
                return;
            }
        }

        WARNING(TEXT("Port %u not found"), Port);
    }
}

/************************************************************************/

/**
 * @brief Sends a UDP packet.
 *
 * @param Device          Network device.
 * @param DestinationIP   Destination IPv4 address (big-endian).
 * @param SourcePort      Source port (host byte order).
 * @param DestinationPort Destination port (host byte order).
 * @param Payload         Pointer to payload data.
 * @param PayloadLength   Length of payload in bytes.
 * @return 1 on success, 0 on failure.
 */

int UDP_Send(LPDEVICE Device, U32 DestinationIP, U16 SourcePort, U16 DestinationPort, const U8* Payload, U32 PayloadLength) {
    LPUDP_CONTEXT Context;
    LPIPV4_CONTEXT IPv4Context;
    U8 Packet[1500];
    LPUDP_HEADER UDPHeader;
    U16 UDPLength;
    U32 LocalIPv4_Be;
    int Result;

    if (Device == NULL) return 0;

    Context = UDP_GetContext(Device);
    SAFE_USE_2(Context, Payload) {
        IPv4Context = IPv4_GetContext(Device);
        SAFE_USE(IPv4Context) {
            LocalIPv4_Be = IPv4Context->LocalIPv4_Be;

            UDPLength = sizeof(UDP_HEADER) + PayloadLength;
            if (UDPLength > 1500) {
                ERROR(TEXT("Packet too large: %u bytes"), UDPLength);
                return 0;
            }

            // Build UDP header
            UDPHeader = (LPUDP_HEADER)Packet;
            UDPHeader->SourcePort = Htons(SourcePort);
            UDPHeader->DestinationPort = Htons(DestinationPort);
            UDPHeader->Length = Htons(UDPLength);
            UDPHeader->Checksum = 0; // Will be calculated below

            // Copy payload
            if (PayloadLength > 0) {
                MemoryCopy(Packet + sizeof(UDP_HEADER), Payload, PayloadLength);
            }

            // Calculate checksum
            UDPHeader->Checksum = UDP_CalculateChecksum(LocalIPv4_Be, DestinationIP, UDPHeader, Payload, PayloadLength);


            // Send via IPv4
            Result = IPv4_Send(Device, DestinationIP, IPV4_PROTOCOL_UDP, Packet, UDPLength);
            return Result;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Handles incoming UDP packets from IPv4 layer.
 *
 * @param Device        Network device.
 * @param Payload       Pointer to UDP packet (header + data).
 * @param PayloadLength Length of UDP packet in bytes.
 * @param SourceIP      Source IPv4 address (big-endian).
 * @param DestinationIP Destination IPv4 address (big-endian).
 */

void UDP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    LPUDP_CONTEXT Context;
    LPUDP_HEADER UDPHeader;
    U16 SourcePort, DestinationPort, Length, Checksum;
    const U8* UDPPayload;
    U32 UDPPayloadLength;
    U32 Index;
    U32 SrcIP, DstIP;

    UNUSED(DestinationIP);

    if (g_UDPDevice == NULL) return;

    Context = UDP_GetContext(g_UDPDevice);
    SAFE_USE_2(Context, Payload) {
        if (PayloadLength < sizeof(UDP_HEADER)) {
            ERROR(TEXT("Packet too small: %u bytes"), PayloadLength);
            return;
        }

        UDPHeader = (LPUDP_HEADER)Payload;
        SourcePort = Ntohs(UDPHeader->SourcePort);
        DestinationPort = Ntohs(UDPHeader->DestinationPort);
        Length = Ntohs(UDPHeader->Length);
        Checksum = Ntohs(UDPHeader->Checksum);

        SrcIP = Ntohl(SourceIP);
        DstIP = Ntohl(DestinationIP);
        UNUSED(SrcIP);
        UNUSED(DstIP);


        // Validate length
        if (Length < sizeof(UDP_HEADER) || Length > PayloadLength) {
            ERROR(TEXT("Invalid UDP length: %u (packet length: %u)"), Length, PayloadLength);
            return;
        }

        // Checksum validation (skip if checksum is 0 - checksum disabled)
        if (Checksum != 0) {
            U16 CalculatedChecksum = UDP_CalculateChecksum(SourceIP, DestinationIP, UDPHeader,
                                                            Payload + sizeof(UDP_HEADER),
                                                            Length - sizeof(UDP_HEADER));
            CalculatedChecksum = Ntohs(CalculatedChecksum);
            if (CalculatedChecksum != Checksum) {
                ERROR(TEXT("Invalid UDP checksum: expected %x, got %x"),
                      CalculatedChecksum, Checksum);
                return;
            }
        }

        // Extract payload
        UDPPayload = Payload + sizeof(UDP_HEADER);
        UDPPayloadLength = Length - sizeof(UDP_HEADER);

        // Find handler for destination port
        for (Index = 0; Index < UDP_MAX_PORTS; Index++) {
            if (Context->PortBindings[Index].IsValid &&
                Context->PortBindings[Index].Port == DestinationPort) {
                Context->PortBindings[Index].Handler(SourceIP, SourcePort, DestinationPort,
                                                     UDPPayload, UDPPayloadLength);
                return;
            }
        }

    }
}

/************************************************************************/
