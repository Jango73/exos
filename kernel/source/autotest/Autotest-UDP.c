
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


    UDP Protocol - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/IPv4.h"
#include "network/NetworkManager.h"
#include "network/Socket.h"
#include "network/UDP.h"
#include "network/UDPContext.h"
#include "text/CoreString.h"

/************************************************************************/
// Test addresses and ports (host byte order ports, network byte order IPs)

#define UDPTEST_PEER_IP Htonl(0x0A000201)       // 10.0.2.1
#define UDPTEST_OTHER_IP Htonl(0x0A000202)      // 10.0.2.2
#define UDPTEST_BROADCAST_IP Htonl(0xFFFFFFFF)  // 255.255.255.255
#define UDPTEST_SPECIFIC_IP Htonl(0xC0A84D4D)   // 192.168.77.77
#define UDPTEST_FOREIGN_IP Htonl(0xC0A84D4E)    // 192.168.77.78

#define UDPTEST_PEER_PORT 5000
#define UDPTEST_OTHER_PORT 5001
#define UDPTEST_PORT_REUSE 40000
#define UDPTEST_PORT_TRUNC 40001
#define UDPTEST_PORT_CONNECT 40002
#define UDPTEST_PORT_SPECIFIC 40003

/************************************************************************/

/**
 * @brief Injects a synthetic UDP datagram into the receive path.
 *
 * Bypasses the network device by calling UDP_OnIPv4Packet directly with a
 * crafted UDP packet. A zero checksum disables checksum validation.
 *
 * @param SourceIP Source IPv4 address (network byte order).
 * @param SourcePort Source port (host byte order).
 * @param DestinationPort Destination port (host byte order).
 * @param DestinationIP Destination IPv4 address (network byte order).
 * @param Payload Payload bytes.
 * @param PayloadLength Payload length in bytes.
 */
static void UDPTest_InjectPacket(
    U32 SourceIP, U16 SourcePort, U16 DestinationPort, U32 DestinationIP, const U8* Payload, U32 PayloadLength) {
    U8 Packet[1500];
    LPUDP_HEADER Header;
    U32 TotalLength;

    Header = (LPUDP_HEADER)Packet;
    Header->SourcePort = Htons(SourcePort);
    Header->DestinationPort = Htons(DestinationPort);
    Header->Length = Htons((U16)(sizeof(UDP_HEADER) + PayloadLength));
    Header->Checksum = 0;
    if (PayloadLength > 0) {
        MemoryCopy(Packet + sizeof(UDP_HEADER), Payload, PayloadLength);
    }

    TotalLength = sizeof(UDP_HEADER) + PayloadLength;
    UDP_OnIPv4Packet(Packet, TotalLength, SourceIP, DestinationIP);
}

/************************************************************************/

/**
 * @brief Injects a synthetic UDP datagram with a computed or corrupted checksum.
 *
 * @param SourceIP Source IPv4 address (network byte order).
 * @param SourcePort Source port (host byte order).
 * @param DestinationPort Destination port (host byte order).
 * @param DestinationIP Destination IPv4 address (network byte order).
 * @param Payload Payload bytes.
 * @param PayloadLength Payload length in bytes.
 * @param CorruptChecksum When TRUE, flips all bits of the computed checksum.
 */
static void UDPTest_InjectPacketChecked(
    U32 SourceIP,
    U16 SourcePort,
    U16 DestinationPort,
    U32 DestinationIP,
    const U8* Payload,
    U32 PayloadLength,
    BOOL CorruptChecksum) {
    U8 Packet[1500];
    LPUDP_HEADER Header;
    U16 Checksum;
    U32 TotalLength;

    Header = (LPUDP_HEADER)Packet;
    Header->SourcePort = Htons(SourcePort);
    Header->DestinationPort = Htons(DestinationPort);
    Header->Length = Htons((U16)(sizeof(UDP_HEADER) + PayloadLength));
    Header->Checksum = 0;
    MemoryCopy(Packet + sizeof(UDP_HEADER), Payload, PayloadLength);

    Checksum = UDP_CalculateChecksum(SourceIP, DestinationIP, Header, Payload, PayloadLength);
    if (CorruptChecksum) {
        Checksum ^= 0xFFFF;
    }
    Header->Checksum = Checksum;

    TotalLength = sizeof(UDP_HEADER) + PayloadLength;
    UDP_OnIPv4Packet(Packet, TotalLength, SourceIP, DestinationIP);
}

/************************************************************************/

/**
 * @brief Tests UDP pseudo-header checksum calculation.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestUDPChecksum(TEST_RESULTS* Results) {
    UDP_HEADER Header;
    U16 Checksum;
    U16 ChecksumWithPayload;
    const U8 TestPayload[] = "TEST";

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    MemorySet(&Header, 0, sizeof(UDP_HEADER));
    Header.SourcePort = Htons(1234);
    Header.DestinationPort = Htons(80);
    Header.Length = Htons(sizeof(UDP_HEADER));

    U32 SourceIP = Htonl(0x0A000201);       // 10.0.2.1
    U32 DestinationIP = Htonl(0x0A000215);  // 10.0.2.21

    // Test 1: header-only checksum is non-zero and never the disabled marker
    Results->TestsRun++;
    Checksum = UDP_CalculateChecksum(SourceIP, DestinationIP, &Header, NULL, 0);
    if (Checksum != 0 && Checksum != 0xFFFF) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("UDP header-only checksum is zero or disabled marker"));
    }

    // Test 2: payload changes the checksum
    Results->TestsRun++;
    ChecksumWithPayload = UDP_CalculateChecksum(SourceIP, DestinationIP, &Header, TestPayload, sizeof(TestPayload) - 1);
    if (ChecksumWithPayload != 0 && ChecksumWithPayload != Checksum) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("UDP payload checksum failed: %x vs %x"), ChecksumWithPayload, Checksum);
    }

    // Test 3: different destination changes the checksum
    Results->TestsRun++;
    U16 ChecksumOtherDestination = UDP_CalculateChecksum(SourceIP, Htonl(0x0A000216), &Header, NULL, 0);
    if (ChecksumOtherDestination != 0 && ChecksumOtherDestination != Checksum) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("UDP destination checksum failed: %x vs %x"), ChecksumOtherDestination, Checksum);
    }
}

/************************************************************************/

/**
 * @brief Checks whether a socket creation returned an error handle.
 *
 * SOCKET_HANDLE values are kernel heap pointers for valid sockets and small
 * negative error codes cast to UINT on failure, so handles must be compared
 * against the error codes rather than tested for sign.
 *
 * @param Handle Socket handle returned by SocketCreate.
 * @return TRUE when the handle is an error code.
 */
static BOOL UDPTest_SocketCreationFailed(SOCKET_HANDLE Handle) {
    return Handle == (SOCKET_HANDLE)SOCKET_ERROR_INVALID || Handle == (SOCKET_HANDLE)SOCKET_ERROR_NOMEM;
}

/************************************************************************/

/**
 * @brief Tests UDP socket routing refinement on the receive path.
 *
 * Requires an initialized network device because UDP_OnIPv4Packet dispatches
 * through the per-device UDP context. Covers SO_REUSEADDR sharing, truncation
 * status, connected peer filtering, destination/broadcast filtering, checksum
 * validation, and high-rate overflow behavior.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestUDPSocketDispatch(TEST_RESULTS* Results) {
    SOCKET_HANDLE SocketReuseA;
    SOCKET_HANDLE SocketReuseB;
    SOCKET_HANDLE SocketNoReuse;
    SOCKET_HANDLE SocketConnected;
    SOCKET_HANDLE SocketSpecific;
    SOCKET_ADDRESS_INET Address;
    SOCKET_ADDRESS_INET PeerAddress;
    SOCKET_ADDRESS_INET OtherPeerAddress;
    SOCKET_ADDRESS SourceAddress;
    U32 SourceAddressLength;
    U8 Buffer[128];
    U8 Payload[64];
    U8 LargePayload[1400];
    U32 ReuseOption = 1;
    U32 OptionValue = 0;
    U32 OptionLength;
    I32 Result;
    U32 Index;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    LPDEVICE Device = (LPDEVICE)NetworkManager_GetPrimaryDevice();
    if (Device == NULL || !NetworkManager_IsDeviceReady(Device)) {
        DEBUG(TEXT("Skipping UDP socket dispatch tests (no network device ready)"));
        return;
    }

    for (Index = 0; Index < sizeof(Payload); Index++) {
        Payload[Index] = (U8)(Index & 0xFF);
    }
    for (Index = 0; Index < sizeof(LargePayload); Index++) {
        LargePayload[Index] = (U8)(Index & 0xFF);
    }

    SocketReuseA = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP);
    SocketReuseB = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP);
    SocketNoReuse = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP);
    SocketConnected = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP);
    SocketSpecific = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_DGRAM, SOCKET_PROTOCOL_UDP);

    if (UDPTest_SocketCreationFailed(SocketReuseA) || UDPTest_SocketCreationFailed(SocketReuseB) ||
        UDPTest_SocketCreationFailed(SocketNoReuse) || UDPTest_SocketCreationFailed(SocketConnected) ||
        UDPTest_SocketCreationFailed(SocketSpecific)) {
        ERROR(TEXT("Failed to create UDP test sockets"));
        return;
    }

    // --- SO_REUSEADDR policy and broadcast fan-out ---

    SocketSetOption(SocketReuseA, SOL_SOCKET, SO_REUSEADDR, &ReuseOption, sizeof(ReuseOption));
    SocketSetOption(SocketReuseB, SOL_SOCKET, SO_REUSEADDR, &ReuseOption, sizeof(ReuseOption));

    SocketAddressInetMake(0, Htons(UDPTEST_PORT_REUSE), &Address);

    Results->TestsRun++;
    Result = SocketBind(SocketReuseA, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result == SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SO_REUSEADDR first bind failed: %d"), Result);
    }

    Results->TestsRun++;
    Result = SocketBind(SocketReuseB, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result == SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SO_REUSEADDR second bind failed: %d"), Result);
    }

    Results->TestsRun++;
    Result = SocketBind(SocketNoReuse, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result == SOCKET_ERROR_INUSE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Bind without SO_REUSEADDR returned %d, expected %d"), Result, SOCKET_ERROR_INUSE);
    }

    Results->TestsRun++;
    OptionLength = sizeof(OptionValue);
    if (SocketGetOption(SocketReuseA, SOL_SOCKET, SO_REUSEADDR, &OptionValue, &OptionLength) == SOCKET_ERROR_NONE &&
        OptionValue == 1) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SO_REUSEADDR get option failed"));
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_REUSE, UDPTEST_BROADCAST_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketReuseA, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == 16) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Broadcast fan-out socket A returned %d"), Result);
    }

    Results->TestsRun++;
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketReuseB, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == 16) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Broadcast fan-out socket B returned %d"), Result);
    }

    // --- Truncation status ---

    SocketAddressInetMake(0, Htons(UDPTEST_PORT_TRUNC), &Address);
    Result = SocketBind(SocketNoReuse, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result != SOCKET_ERROR_NONE) {
        ERROR(TEXT("Truncation test bind failed: %d"), Result);
        goto Cleanup;
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_TRUNC, UDPTEST_BROADCAST_IP, Payload, 64);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, 16, 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_MSGSIZE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Truncation returned %d, expected %d"), Result, SOCKET_ERROR_MSGSIZE);
    }

    Results->TestsRun++;
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Consumed datagram not drained, returned %d"), Result);
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_TRUNC, UDPTEST_BROADCAST_IP, Payload, 64);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == 64) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Full buffer receive returned %d, expected 64"), Result);
    }

    // --- Connected peer filter ---

    SocketAddressInetMake(0, Htons(UDPTEST_PORT_CONNECT), &Address);
    Result = SocketBind(SocketConnected, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result != SOCKET_ERROR_NONE) {
        ERROR(TEXT("Connected test bind failed: %d"), Result);
        goto Cleanup;
    }

    SocketAddressInetMake(UDPTEST_PEER_IP, Htons(UDPTEST_PEER_PORT), &PeerAddress);
    Result = SocketConnect(SocketConnected, (LPSOCKET_ADDRESS)&PeerAddress, sizeof(SOCKET_ADDRESS_INET));
    if (Result != SOCKET_ERROR_NONE) {
        ERROR(TEXT("UDP connect failed: %d"), Result);
        goto Cleanup;
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_CONNECT, UDPTEST_BROADCAST_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketConnected, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == 16) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Connected peer receive returned %d"), Result);
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_OTHER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_CONNECT, UDPTEST_BROADCAST_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketConnected, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Foreign source passed connected filter, returned %d"), Result);
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_OTHER_PORT, UDPTEST_PORT_CONNECT, UDPTEST_BROADCAST_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketConnected, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Foreign port passed connected filter, returned %d"), Result);
    }

    Results->TestsRun++;
    SocketAddressInetMake(UDPTEST_OTHER_IP, Htons(UDPTEST_OTHER_PORT), &OtherPeerAddress);
    Result =
        SocketSendTo(SocketConnected, Payload, 16, 0, (LPSOCKET_ADDRESS)&OtherPeerAddress, sizeof(SOCKET_ADDRESS_INET));
    if (Result == SOCKET_ERROR_INVALID) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SendTo foreign peer returned %d, expected %d"), Result, SOCKET_ERROR_INVALID);
    }

    Results->TestsRun++;
    Result = SocketSendTo(SocketConnected, Payload, 16, 0, (LPSOCKET_ADDRESS)&PeerAddress, sizeof(SOCKET_ADDRESS_INET));
    if (Result == 16) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SendTo connected peer returned %d"), Result);
    }

    // --- Destination and broadcast filtering ---

    SocketAddressInetMake(UDPTEST_SPECIFIC_IP, Htons(UDPTEST_PORT_SPECIFIC), &Address);
    Result = SocketBind(SocketSpecific, (LPSOCKET_ADDRESS)&Address, sizeof(SOCKET_ADDRESS_INET));
    if (Result != SOCKET_ERROR_NONE) {
        ERROR(TEXT("Specific address bind failed: %d"), Result);
        goto Cleanup;
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_SPECIFIC, UDPTEST_BROADCAST_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketSpecific, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Broadcast passed specific address filter, returned %d"), Result);
    }

    Results->TestsRun++;
    UDPTest_InjectPacket(UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_SPECIFIC, UDPTEST_FOREIGN_IP, Payload, 16);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketSpecific, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Foreign destination passed UDP layer, returned %d"), Result);
    }

    // --- Receive path checksum validation ---

    Results->TestsRun++;
    UDPTest_InjectPacketChecked(
        UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_TRUNC, UDPTEST_BROADCAST_IP, Payload, 16, FALSE);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == 16) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Valid checksum datagram dropped, returned %d"), Result);
    }

    Results->TestsRun++;
    UDPTest_InjectPacketChecked(
        UDPTEST_PEER_IP, UDPTEST_PEER_PORT, UDPTEST_PORT_TRUNC, UDPTEST_BROADCAST_IP, Payload, 16, TRUE);
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_WOULDBLOCK) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Corrupted checksum datagram accepted, returned %d"), Result);
    }

    // --- High-rate stress and overflow ---

    Results->TestsRun++;
    for (Index = 0; Index < 8; Index++) {
        UDPTest_InjectPacket(
            UDPTEST_PEER_IP,
            UDPTEST_PEER_PORT,
            UDPTEST_PORT_TRUNC,
            UDPTEST_BROADCAST_IP,
            LargePayload,
            sizeof(LargePayload));
    }
    SourceAddressLength = sizeof(SourceAddress);
    Result = SocketReceiveFrom(SocketNoReuse, Buffer, sizeof(Buffer), 0, &SourceAddress, &SourceAddressLength);
    if (Result == SOCKET_ERROR_OVERFLOW) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("High-rate overflow returned %d, expected %d"), Result, SOCKET_ERROR_OVERFLOW);
    }

Cleanup:
    SocketClose(SocketReuseA);
    SocketClose(SocketReuseB);
    SocketClose(SocketNoReuse);
    SocketClose(SocketConnected);
    SocketClose(SocketSpecific);

    TEST(TEXT("UDP socket dispatch tests passed %u/%u"), Results->TestsPassed, Results->TestsRun);
}

/************************************************************************/

/**
 * @brief Main UDP test function that runs all UDP unit tests.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestUDP(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Expected ERROR paths exercised by the dispatch tests must not fail the
    // smoke test; they are wrapped in the autotest error scope.
    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_BEGIN"));

    TestUDPChecksum(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    TestUDPSocketDispatch(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_END"));

    TEST(TEXT("UDP tests passed %u/%u"), Results->TestsPassed, Results->TestsRun);
}
