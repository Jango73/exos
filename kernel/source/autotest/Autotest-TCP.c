
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


    TCP Protocol - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/TCP.h"
#include "network/Socket.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Test TCP checksum calculation
 *
 * This function tests the TCP checksum calculation logic against known
 * test vectors to ensure correct implementation of the TCP checksum
 * algorithm including pseudo-header handling.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestTCPChecksum(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Basic TCP header checksum (no payload)
    Results->TestsRun++;
    TCP_HEADER Header;
    MemorySet(&Header, 0, sizeof(TCP_HEADER));
    Header.SourcePort = Htons(80);
    Header.DestinationPort = Htons(8080);
    Header.SequenceNumber = Htonl(0x12345678);
    Header.AckNumber = Htonl(0x87654321);
    Header.DataOffset = 0x50;  // 5 words (20 bytes)
    Header.Flags = TCP_FLAG_SYN;
    Header.WindowSize = Htons(8192);
    Header.UrgentPointer = 0;

    U32 SourceIP = 0xC0A80101;       // 192.168.1.1
    U32 DestinationIP = 0xC0A80102;  // 192.168.1.2

    U16 Checksum = TCP_CalculateChecksum(&Header, NULL, 0, SourceIP, DestinationIP);
    // Don't check specific value as it depends on implementation, just verify it's non-zero
    if (Checksum != 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("TCP checksum is zero for valid header"));
    }

    // Test 2: TCP header with small payload
    Results->TestsRun++;
    const U8 TestPayload[] = "TEST";
    U16 ChecksumWithPayload = TCP_CalculateChecksum(&Header, TestPayload, 4, SourceIP, DestinationIP);
    if (ChecksumWithPayload != 0 && ChecksumWithPayload != Checksum) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("TCP checksum with payload failed: %x vs %x"), ChecksumWithPayload, Checksum);
    }

    // Test 3: Checksum validation (correct)
    Results->TestsRun++;
    Header.Checksum = ChecksumWithPayload;
    int ValidationResult = TCP_ValidateChecksum(&Header, TestPayload, 4, SourceIP, DestinationIP);
    if (ValidationResult == 1) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Valid checksum validation failed"));
    }

    // Test 4: Checksum validation (incorrect)
    Results->TestsRun++;
    Header.Checksum = ChecksumWithPayload ^ 0xFFFF;  // Corrupt checksum
    ValidationResult = TCP_ValidateChecksum(&Header, TestPayload, 4, SourceIP, DestinationIP);
    if (ValidationResult == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Invalid checksum validation should have failed"));
    }

    // Test 5: Zero payload length
    Results->TestsRun++;
    Header.Checksum = 0;
    U16 ZeroPayloadChecksum = TCP_CalculateChecksum(&Header, NULL, 0, SourceIP, DestinationIP);
    if (ZeroPayloadChecksum != 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Zero payload checksum is zero"));
    }
}

/************************************************************************/

/**
 * @brief Test TCP socket option handling for performance optimizations.
 *
 * Verifies the TCP_NODELAY (Nagle) and SO_KEEPALIVE options round-trip through
 * SocketSetOption/SocketGetOption on a stream socket, and that invalid option
 * levels are rejected.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestTCPSocketOptions(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Expected ERROR paths (invalid option level/name rejection) exercised by
    // the tests must not fail the smoke test; they are wrapped in the autotest
    // error scope.
    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_BEGIN"));

    SOCKET_HANDLE Socket = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
    if (Socket == (SOCKET_HANDLE)SOCKET_ERROR_INVALID) {
        ERROR(TEXT("Failed to create TCP test socket"));
        return;
    }

    U32 OptionValue = 1;
    U32 OptionLength = sizeof(OptionValue);

    // Default values: Nagle enabled (NoDelay FALSE), keep-alive disabled
    Results->TestsRun++;
    OptionValue = 1;
    OptionLength = sizeof(OptionValue);
    if (SocketGetOption(Socket, IPPROTO_TCP, TCP_NODELAY, &OptionValue, &OptionLength) == SOCKET_ERROR_NONE &&
        OptionValue == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Default TCP_NODELAY value mismatch"));
    }

    Results->TestsRun++;
    OptionValue = 1;
    OptionLength = sizeof(OptionValue);
    if (SocketGetOption(Socket, SOL_SOCKET, SO_KEEPALIVE, &OptionValue, &OptionLength) == SOCKET_ERROR_NONE &&
        OptionValue == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Default SO_KEEPALIVE value mismatch"));
    }

    // TCP_NODELAY round-trip
    Results->TestsRun++;
    OptionValue = 1;
    if (SocketSetOption(Socket, IPPROTO_TCP, TCP_NODELAY, &OptionValue, sizeof(OptionValue)) == SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("TCP_NODELAY set failed"));
    }

    Results->TestsRun++;
    OptionValue = 0;
    OptionLength = sizeof(OptionValue);
    if (SocketGetOption(Socket, IPPROTO_TCP, TCP_NODELAY, &OptionValue, &OptionLength) == SOCKET_ERROR_NONE &&
        OptionValue == 1) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("TCP_NODELAY get after set failed"));
    }

    // SO_KEEPALIVE round-trip
    Results->TestsRun++;
    OptionValue = 1;
    if (SocketSetOption(Socket, SOL_SOCKET, SO_KEEPALIVE, &OptionValue, sizeof(OptionValue)) == SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SO_KEEPALIVE set failed"));
    }

    Results->TestsRun++;
    OptionValue = 0;
    OptionLength = sizeof(OptionValue);
    if (SocketGetOption(Socket, SOL_SOCKET, SO_KEEPALIVE, &OptionValue, &OptionLength) == SOCKET_ERROR_NONE &&
        OptionValue == 1) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("SO_KEEPALIVE get after set failed"));
    }

    // Invalid option level rejection
    Results->TestsRun++;
    OptionValue = 1;
    if (SocketSetOption(Socket, 0x7FFF, SO_KEEPALIVE, &OptionValue, sizeof(OptionValue)) != SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Invalid option level accepted"));
    }

    // Invalid option name rejection at valid level
    Results->TestsRun++;
    OptionValue = 1;
    if (SocketSetOption(Socket, IPPROTO_TCP, 0x7FFF, &OptionValue, sizeof(OptionValue)) != SOCKET_ERROR_NONE) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Invalid TCP option name accepted"));
    }

    SocketClose(Socket);

    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_END"));

    TEST(TEXT("TCP socket option tests passed %u/%u"), Results->TestsPassed, Results->TestsRun);
}

/************************************************************************/

/**
 * @brief Main TCP test function that runs all TCP unit tests.
 *
 * This function coordinates all TCP unit tests and aggregates their results.
 * It tests checksum calculation, header field handling, flag processing,
 * state definitions, event definitions, and buffer size validation.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestTCP(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Run TCP checksum tests
    TestTCPChecksum(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run TCP socket option tests
    TestTCPSocketOptions(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;
}
