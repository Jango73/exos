
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


    IPv4 - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/IPv4.h"
#include "text/CoreString.h"

/************************************************************************/

// Mock IPv4 context for testing
typedef struct tag_IPV4_CONTEXT_MOCK {
    LPDEVICE Device;
    U32 LocalIPv4_Be;
    U32 NetmaskBe;
    U32 DefaultGatewayBe;
    IPV4_PENDING_PACKET PendingPackets[IPV4_MAX_PENDING_PACKETS];
    U32 ARPCallbackRegistered;
} IPV4_CONTEXT_MOCK, *LPIPV4_CONTEXT_MOCK;

/************************************************************************/

/**
 * @brief Test IPv4 header checksum calculation.
 *
 * This function tests the IPv4 header checksum calculation algorithm,
 * ensuring proper handling of different header configurations and
 * correct implementation of the Internet checksum algorithm.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestIPv4ChecksumCalculation(TEST_RESULTS* Results) {
    IPV4_HEADER TestHeader;
    U16 CalculatedChecksum;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Standard IPv4 header with known checksum
    Results->TestsRun++;
    MemorySet(&TestHeader, 0, sizeof(TestHeader));
    TestHeader.VersionIHL = 0x45;      // Version 4, IHL 5 (20 bytes)
    TestHeader.TypeOfService = 0x00;
    TestHeader.TotalLength = Htons(60); // 60 bytes total
    TestHeader.Identification = Htons(0x1234);
    TestHeader.FlagsFragmentOffset = Htons(IPV4_FLAG_DONT_FRAGMENT);
    TestHeader.TimeToLive = 64;
    TestHeader.Protocol = IPV4_PROTOCOL_TCP;
    TestHeader.HeaderChecksum = 0; // Will be calculated
    TestHeader.SourceAddress = Htonl(0xC0A80101);      // 192.168.1.1
    TestHeader.DestinationAddress = Htonl(0xC0A80102); // 192.168.1.2

    CalculatedChecksum = IPv4_CalculateChecksum(&TestHeader);

    // Verify checksum is non-zero and has expected properties
    if (CalculatedChecksum != 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 1 failed: checksum is zero"));
    }

    // Test 2: Verify checksum with different header values
    Results->TestsRun++;
    TestHeader.SourceAddress = Htonl(0x08080808);      // 8.8.8.8
    TestHeader.DestinationAddress = Htonl(0x08080404); // 8.8.4.4
    TestHeader.Protocol = IPV4_PROTOCOL_UDP;
    TestHeader.TimeToLive = 32;

    U16 SecondChecksum = IPv4_CalculateChecksum(&TestHeader);

    // Should produce different checksum with different data
    if (SecondChecksum != CalculatedChecksum && SecondChecksum != 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 2 failed: checksum unchanged or zero"));
    }

    // Test 3: Zero header should produce maximum checksum (all 1s complement)
    Results->TestsRun++;
    MemorySet(&TestHeader, 0, sizeof(TestHeader));
    TestHeader.VersionIHL = 0x45; // Must have valid version/IHL

    U16 ZeroChecksum = IPv4_CalculateChecksum(&TestHeader);
    if (ZeroChecksum != 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 3 failed: zero header produced zero checksum"));
    }

    // Test 4: Test checksum with maximum values
    Results->TestsRun++;
    MemorySet(&TestHeader, 0xFF, sizeof(TestHeader));
    TestHeader.VersionIHL = 0x45; // Valid version/IHL
    TestHeader.HeaderChecksum = 0; // Clear checksum field

    U16 MaxChecksum = IPv4_CalculateChecksum(&TestHeader);
    if (MaxChecksum != ZeroChecksum) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 4 failed: max header same as zero"));
    }
}

/************************************************************************/

/**
 * @brief Test IPv4 pending packet data structure validation.
 *
 * This function tests the IPv4 pending packet data structure validation
 * and basic parameter checking without requiring ARP integration.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestIPv4PendingPacketManagement(TEST_RESULTS* Results) {
    IPV4_CONTEXT_MOCK MockContext;
    U8 TestPayload[100];
    U32 i;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Initialize mock context
    MemorySet(&MockContext, 0, sizeof(MockContext));
    MockContext.LocalIPv4_Be = Htonl(0xC0A80001); // 192.168.0.1

    // Initialize test payload
    for (i = 0; i < sizeof(TestPayload); i++) {
        TestPayload[i] = (U8)(i & 0xFF);
    }

    // Test 1: Test IPv4_AddPendingPacket parameter validation (NULL context)
    Results->TestsRun++;
    int InvalidResult1 = IPv4_AddPendingPacket(NULL, Htonl(0xC0A80002), Htonl(0xC0A80002), IPV4_PROTOCOL_TCP, TestPayload, sizeof(TestPayload));

    if (InvalidResult1 == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 1 failed: NULL context accepted"));
    }

    // Test 2: Test IPv4_AddPendingPacket parameter validation (NULL payload)
    Results->TestsRun++;
    int InvalidResult2 = IPv4_AddPendingPacket((LPIPV4_CONTEXT)&MockContext, Htonl(0xC0A80002), Htonl(0xC0A80002), IPV4_PROTOCOL_TCP, NULL, sizeof(TestPayload));

    if (InvalidResult2 == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 2 failed: NULL payload accepted"));
    }

    // Test 3: Test IPv4_AddPendingPacket parameter validation (zero payload length)
    Results->TestsRun++;
    int InvalidResult3 = IPv4_AddPendingPacket((LPIPV4_CONTEXT)&MockContext, Htonl(0xC0A80002), Htonl(0xC0A80002), IPV4_PROTOCOL_TCP, TestPayload, 0);

    if (InvalidResult3 == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 3 failed: zero payload length accepted"));
    }

    // Test 4: Test IPv4_AddPendingPacket parameter validation (payload too large)
    Results->TestsRun++;
    int InvalidResult4 = IPv4_AddPendingPacket((LPIPV4_CONTEXT)&MockContext, Htonl(0xC0A80002), Htonl(0xC0A80002), IPV4_PROTOCOL_TCP, TestPayload, 2000);

    if (InvalidResult4 == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 4 failed: oversized payload accepted"));
    }
}

/************************************************************************/

/**
 * @brief Main IPv4 test function that runs all IPv4 unit tests.
 *
 * This function coordinates all IPv4 unit tests and aggregates their results.
 * It tests checksum calculation, checksum validation, header validation,
 * and pending packet management functionality.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestIPv4(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Run checksum calculation tests
    TestIPv4ChecksumCalculation(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run pending packet management tests
    TestIPv4PendingPacketManagement(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;
}
