
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


    Domain Name System (DNS) client - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "network/DNS.h"
#include "text/CoreString.h"

/************************************************************************/

#define DNSTEST_RESOLVED_IP 0x0A000203  // 10.0.2.3 read from the wire

// Header flags for a valid response with the RD and RA bits set (RFC 1035).

#define DNSTEST_FLAGS_RESPONSE (DNS_FLAG_RESPONSE | DNS_FLAG_RD | DNS_FLAG_RA)

/************************************************************************/

/**
 * @brief Appends a DNS header to a packet buffer.
 *
 * @param Packet        Packet buffer.
 * @param TransactionID Transaction identifier.
 * @param Flags         Header flags.
 * @param QuestionCount Question count.
 * @param AnswerCount   Answer count.
 * @return Offset just past the header.
 */

static UINT DNSTest_WriteHeader(U8* Packet, U16 TransactionID, U16 Flags, U16 QuestionCount, U16 AnswerCount) {
    LPDNS_HEADER Header = (LPDNS_HEADER)Packet;

    Header->TransactionID = Htons(TransactionID);
    Header->Flags = Htons(Flags);
    Header->QuestionCount = Htons(QuestionCount);
    Header->AnswerCount = Htons(AnswerCount);
    Header->AuthorityCount = 0;
    Header->AdditionalCount = 0;

    return sizeof(DNS_HEADER);
}

/************************************************************************/

/**
 * @brief Appends a DNS question to a packet buffer.
 *
 * @param Packet Packet buffer.
 * @param Offset Offset to append at.
 * @param Name   Question name.
 * @param Type   Question type.
 * @param Class  Question class.
 * @return Offset just past the question, or 0 on overflow.
 */

static UINT DNSTest_WriteQuestion(U8* Packet, UINT Offset, LPCSTR Name, U16 Type, U16 Class) {
    UINT Length = DNS_EncodeName(Name, Packet + Offset, DNS_QUERY_BUFFER_SIZE - Offset);

    if (Length == 0) {
        return 0;
    }
    Offset += Length;

    if (Offset + 4 > DNS_QUERY_BUFFER_SIZE) {
        return 0;
    }

    Packet[Offset] = (U8)(Type >> 8);
    Packet[Offset + 1] = (U8)Type;
    Packet[Offset + 2] = (U8)(Class >> 8);
    Packet[Offset + 3] = (U8)Class;

    return Offset + 4;
}

/************************************************************************/

/**
 * @brief Appends a DNS answer resource record to a packet buffer.
 *
 * @param Packet     Packet buffer.
 * @param Offset     Offset to append at.
 * @param UsePointer When TRUE the name is a compression pointer to the
 *                   question name at offset 12, otherwise it is encoded.
 * @param Name       Answer name (unused with a compression pointer).
 * @param Type       Record type.
 * @param Class      Record class.
 * @param TTL        Time to live.
 * @param RDLength   RDATA length.
 * @param RData      RDATA bytes.
 * @return Offset just past the answer, or 0 on overflow.
 */

static UINT DNSTest_WriteAnswer(
    U8* Packet,
    UINT Offset,
    BOOL UsePointer,
    LPCSTR Name,
    U16 Type,
    U16 Class,
    U32 TTL,
    UINT RDLength,
    const U8* RData) {
    if (UsePointer) {
        if (Offset + 2 > DNS_QUERY_BUFFER_SIZE) {
            return 0;
        }
        Packet[Offset] = 0xC0;
        Packet[Offset + 1] = sizeof(DNS_HEADER);
        Offset += 2;
    } else {
        UINT Length = DNS_EncodeName(Name, Packet + Offset, DNS_QUERY_BUFFER_SIZE - Offset);

        if (Length == 0) {
            return 0;
        }
        Offset += Length;
    }

    if (Offset + 10 + RDLength > DNS_QUERY_BUFFER_SIZE) {
        return 0;
    }

    Packet[Offset] = (U8)(Type >> 8);
    Packet[Offset + 1] = (U8)Type;
    Packet[Offset + 2] = (U8)(Class >> 8);
    Packet[Offset + 3] = (U8)Class;
    Packet[Offset + 4] = (U8)(TTL >> 24);
    Packet[Offset + 5] = (U8)(TTL >> 16);
    Packet[Offset + 6] = (U8)(TTL >> 8);
    Packet[Offset + 7] = (U8)TTL;
    Packet[Offset + 8] = (U8)(RDLength >> 8);
    Packet[Offset + 9] = (U8)RDLength;
    if (RDLength > 0) {
        MemoryCopy(Packet + Offset + 10, RData, RDLength);
    }

    return Offset + 10 + RDLength;
}

/************************************************************************/

/**
 * @brief Builds a DNS response packet with one question and a single answer.
 *
 * @param Packet      Packet buffer.
 * @param OutLength   Receives the total packet length.
 * @param Flags       Header flags.
 * @param UsePointer  When TRUE the answer name is a compression pointer.
 * @param AnswerType  Answer record type.
 * @param AnswerClass Answer record class.
 * @param RDLength    Answer RDATA length.
 * @param RData       Answer RDATA bytes.
 * @return TRUE on success, FALSE otherwise.
 */

static BOOL DNSTest_BuildResponse(
    U8* Packet,
    UINT* OutLength,
    U16 Flags,
    BOOL UsePointer,
    U16 AnswerType,
    U16 AnswerClass,
    UINT RDLength,
    const U8* RData) {
    UINT Offset;
    const U8 ZeroTTL[4] = {0, 0, 0, 0};

    Offset = DNSTest_WriteHeader(Packet, 0x1234, Flags, 1, 1);
    if (Offset == 0) {
        return FALSE;
    }

    Offset = DNSTest_WriteQuestion(Packet, Offset, "example.com", DNS_TYPE_A, DNS_CLASS_IN);
    if (Offset == 0) {
        return FALSE;
    }

    Offset =
        DNSTest_WriteAnswer(Packet, Offset, UsePointer, "example.com", AnswerType, AnswerClass, 300, RDLength, RData);
    if (Offset == 0) {
        return FALSE;
    }

    *OutLength = Offset;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Tests DNS host name encoding.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNSEncodeName(TEST_RESULTS* Results) {
    U8 Buffer[DNS_QUERY_BUFFER_SIZE];
    const U8 Expected[] = {7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0};
    STR LongLabel[65];
    STR LongName[257];
    UINT Index;
    UINT Length;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: regular name encoded in wire format.
    Results->TestsRun++;
    Length = DNS_EncodeName("example.com", Buffer, sizeof(Buffer));
    if (Length == sizeof(Expected) && MemoryCompare(Buffer, Expected, sizeof(Expected)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName produced %u bytes, expected %u"), Length, sizeof(Expected));
    }

    // Test 2: NULL name rejected.
    Results->TestsRun++;
    if (DNS_EncodeName(NULL, Buffer, sizeof(Buffer)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName accepted a NULL name"));
    }

    // Test 3: empty name rejected.
    Results->TestsRun++;
    if (DNS_EncodeName("", Buffer, sizeof(Buffer)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName accepted an empty name"));
    }

    // Test 4: label longer than 63 bytes rejected.
    Results->TestsRun++;
    for (Index = 0; Index < 64; Index++) {
        LongLabel[Index] = 'a';
    }
    LongLabel[64] = STR_NULL;
    if (DNS_EncodeName(LongLabel, Buffer, sizeof(Buffer)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName accepted a label longer than 63 bytes"));
    }

    // Test 5: name longer than 255 bytes rejected.
    Results->TestsRun++;
    for (Index = 0; Index < 256; Index++) {
        LongName[Index] = ((Index % 64) == 63) ? (STR)'.' : (STR)'a';
    }
    LongName[256] = STR_NULL;
    if (DNS_EncodeName(LongName, Buffer, sizeof(Buffer)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName accepted a name longer than 255 bytes"));
    }

    // Test 6: too small output buffer rejected.
    Results->TestsRun++;
    if (DNS_EncodeName("example.com", Buffer, 4) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeName accepted an overflowing buffer"));
    }
}

/************************************************************************/

/**
 * @brief Tests DNS query encoding.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNSEncodeQuery(TEST_RESULTS* Results) {
    U8 Buffer[DNS_QUERY_BUFFER_SIZE];
    LPDNS_HEADER Header;
    UINT Length;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: header fields and total length.
    Results->TestsRun++;
    Length = DNS_EncodeQuery(0x1234, "example.com", Buffer, sizeof(Buffer));
    if (Length == sizeof(DNS_HEADER) + 13 + 4) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeQuery produced %u bytes, expected %u"), Length, sizeof(DNS_HEADER) + 13 + 4);
    }

    // Test 2: header content.
    Results->TestsRun++;
    Header = (LPDNS_HEADER)Buffer;
    if (Ntohs(Header->TransactionID) == 0x1234 && Ntohs(Header->Flags) == DNS_FLAG_RD &&
        Ntohs(Header->QuestionCount) == 1 && Ntohs(Header->AnswerCount) == 0 && Ntohs(Header->AuthorityCount) == 0 &&
        Ntohs(Header->AdditionalCount) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeQuery produced an incorrect DNS header"));
    }

    // Test 3: too small output buffer rejected.
    Results->TestsRun++;
    if (DNS_EncodeQuery(0x1234, "example.com", Buffer, 10) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("EncodeQuery accepted an overflowing buffer"));
    }
}

/************************************************************************/

/**
 * @brief Tests DNS wire-format name reading.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNSReadName(TEST_RESULTS* Results) {
    U8 Buffer[DNS_QUERY_BUFFER_SIZE];
    STR Name[DNS_MAX_NAME_LENGTH + 1];
    UINT Offset;
    UINT Index;
    UINT Length;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: plain name decoded and offset returned.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    Length = DNS_EncodeName("example.com", Buffer, sizeof(Buffer));
    if (DNS_ReadName(Buffer, sizeof(Buffer), 0, Name, sizeof(Name)) == Length &&
        StringCompareNC(Name, "example.com") == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName failed to decode a plain name"));
    }

    // Test 2: compression pointer followed to an earlier name.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    Buffer[0] = 3;
    MemoryCopy(Buffer + 1, "com", 3);
    Buffer[4] = 0;
    Buffer[5] = 7;
    MemoryCopy(Buffer + 6, "example", 7);
    Buffer[13] = 0xC0;
    Buffer[14] = 0;
    if (DNS_ReadName(Buffer, sizeof(Buffer), 5, Name, sizeof(Name)) == 15 &&
        StringCompareNC(Name, "example.com") == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName failed to follow a compression pointer"));
    }

    // Test 3: offset beyond the payload rejected.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    Buffer[0] = 1;
    Buffer[1] = 'x';
    Buffer[2] = 0;
    if (DNS_ReadName(Buffer, 3, 4, Name, sizeof(Name)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName accepted an offset beyond the payload"));
    }

    // Test 4: label running past the payload rejected.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    Buffer[0] = 5;
    MemoryCopy(Buffer + 1, "xy", 2);
    if (DNS_ReadName(Buffer, 3, 0, Name, sizeof(Name)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName accepted a truncated label"));
    }

    // Test 5: output buffer too small rejected.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    DNS_EncodeName("example.com", Buffer, sizeof(Buffer));
    if (DNS_ReadName(Buffer, sizeof(Buffer), 0, Name, 6) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName accepted a too small output buffer"));
    }

    // Test 6: too many compression pointers rejected.
    Results->TestsRun++;
    MemorySet(Buffer, 0, sizeof(Buffer));
    Buffer[0] = 1;
    Buffer[1] = 'x';
    Buffer[2] = 0;
    Buffer[3] = 0xC0;
    Buffer[4] = 0;
    for (Index = 0, Offset = 5; Index < 8; Index++, Offset += 2) {
        Buffer[Offset] = 0xC0;
        Buffer[Offset + 1] = (U8)(Offset - 2);
    }
    if (DNS_ReadName(Buffer, sizeof(Buffer), 19, Name, sizeof(Name)) == 0) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ReadName accepted too many compression pointers"));
    }
}

/************************************************************************/

/**
 * @brief Tests DNS response packet parsing.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNSParseResponse(TEST_RESULTS* Results) {
    U8 Packet[DNS_QUERY_BUFFER_SIZE];
    U8 CNameData[DNS_QUERY_BUFFER_SIZE];
    U8 IpData[4] = {10, 0, 2, 3};
    U32 ResolvedIP_Be = 0;
    U32 ResponseTTL = 0;
    UINT CNameLength;
    UINT Offset;
    UINT Length;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: valid A record response yields the resolved address.
    Results->TestsRun++;
    if (DNSTest_BuildResponse(Packet, &Length, DNSTEST_FLAGS_RESPONSE, TRUE, DNS_TYPE_A, DNS_CLASS_IN, 4, IpData) &&
        DNS_ParseResponse(Packet, Length, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_SUCCESS &&
        ResolvedIP_Be == DNSTEST_RESOLVED_IP) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed on a valid A record response"));
    }

    // Test 2: NULL expected name skips the question echo validation.
    Results->TestsRun++;
    ResolvedIP_Be = 0;
    if (DNS_ParseResponse(Packet, Length, NULL, &ResolvedIP_Be, NULL) == DNS_STATUS_SUCCESS &&
        ResolvedIP_Be == DNSTEST_RESOLVED_IP) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse rejected a response with NULL expected name"));
    }

    // Test 3: question echo mismatch rejected.
    Results->TestsRun++;
    if (DNS_ParseResponse(Packet, Length, "other.example", &ResolvedIP_Be, NULL) == DNS_STATUS_MALFORMED) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse accepted a mismatching question echo"));
    }

    // Test 4: NAME ERROR response code mapped.
    Results->TestsRun++;
    if (DNSTest_BuildResponse(
            Packet,
            &Length,
            DNSTEST_FLAGS_RESPONSE | DNS_RCODE_NAME_ERROR,
            TRUE,
            DNS_TYPE_A,
            DNS_CLASS_IN,
            4,
            IpData) &&
        DNS_ParseResponse(Packet, Length, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_NAME_ERROR) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed to map the NAME ERROR response code"));
    }

    // Test 5: truncation flag mapped.
    Results->TestsRun++;
    if (DNSTest_BuildResponse(
            Packet, &Length, DNSTEST_FLAGS_RESPONSE | DNS_FLAG_TC, TRUE, DNS_TYPE_A, DNS_CLASS_IN, 4, IpData) &&
        DNS_ParseResponse(Packet, Length, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_TRUNCATED) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed to map the truncation flag"));
    }

    // Test 6: CNAME only answer yields NO_ANSWER.
    Results->TestsRun++;
    CNameLength = DNS_EncodeName("example.net", CNameData, sizeof(CNameData));
    if (DNSTest_BuildResponse(
            Packet, &Length, DNSTEST_FLAGS_RESPONSE, TRUE, DNS_TYPE_CNAME, DNS_CLASS_IN, CNameLength, CNameData) &&
        DNS_ParseResponse(Packet, Length, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_NO_ANSWER) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed to skip a CNAME-only answer"));
    }

    // Test 7: CNAME then A record resolves to the A address.
    Results->TestsRun++;
    MemorySet(Packet, 0, sizeof(Packet));
    Offset = DNSTest_WriteHeader(Packet, 0x1234, DNSTEST_FLAGS_RESPONSE, 1, 2);
    Offset = DNSTest_WriteQuestion(Packet, Offset, "example.com", DNS_TYPE_A, DNS_CLASS_IN);
    Offset = DNSTest_WriteAnswer(
        Packet, Offset, TRUE, "example.com", DNS_TYPE_CNAME, DNS_CLASS_IN, 300, CNameLength, CNameData);
    Offset = DNSTest_WriteAnswer(Packet, Offset, TRUE, "example.com", DNS_TYPE_A, DNS_CLASS_IN, 300, 4, IpData);
    ResolvedIP_Be = 0;
    if (DNS_ParseResponse(Packet, Offset, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_SUCCESS &&
        ResolvedIP_Be == DNSTEST_RESOLVED_IP) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed to skip a leading CNAME record"));
    }

    // Test 8: short packet rejected.
    Results->TestsRun++;
    if (DNS_ParseResponse(Packet, sizeof(DNS_HEADER) - 1, "example.com", &ResolvedIP_Be, NULL) ==
        DNS_STATUS_MALFORMED) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse accepted a short packet"));
    }

    // Test 9: answer RDATA running past the payload rejected.
    Results->TestsRun++;
    if (DNSTest_BuildResponse(Packet, &Length, DNSTEST_FLAGS_RESPONSE, TRUE, DNS_TYPE_A, DNS_CLASS_IN, 4, IpData) &&
        DNS_ParseResponse(Packet, Length - 2, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_MALFORMED) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse accepted an overrunning answer"));
    }

    // Test 10: NULL payload rejected.
    Results->TestsRun++;
    if (DNS_ParseResponse(NULL, 0, "example.com", &ResolvedIP_Be, NULL) == DNS_STATUS_MALFORMED) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse accepted a NULL payload"));
    }

    // Test 11: answer record TTL is reported.
    Results->TestsRun++;
    ResponseTTL = 0;
    if (DNSTest_BuildResponse(Packet, &Length, DNSTEST_FLAGS_RESPONSE, TRUE, DNS_TYPE_A, DNS_CLASS_IN, 4, IpData) &&
        DNS_ParseResponse(Packet, Length, "example.com", &ResolvedIP_Be, &ResponseTTL) == DNS_STATUS_SUCCESS &&
        ResponseTTL == 300) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("ParseResponse failed to report the answer TTL"));
    }
}

/************************************************************************/

/**
 * @brief Tests the DNS response cache.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNSCache(TEST_RESULTS* Results) {
    DNS_CACHE Cache;
    U32 ResolvedIP_Be = 0;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: store then lookup a matching entry.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "example.com", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheLookup(&Cache, "example.com", 1000, &ResolvedIP_Be) && ResolvedIP_Be == DNSTEST_RESOLVED_IP) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache store followed by lookup failed"));
    }

    // Test 2: unknown name yields a miss.
    Results->TestsRun++;
    ResolvedIP_Be = 0;
    if (!DNS_CacheLookup(&Cache, "other.example", 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache lookup matched an unknown name"));
    }

    // Test 3: name comparison is case-insensitive.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "Example.COM", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheLookup(&Cache, "example.com", 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache lookup failed to match a name case-insensitively"));
    }

    // Test 4: entry at the TTL boundary is a miss.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "example.com", DNSTEST_RESOLVED_IP, 300, 1000) &&
        !DNS_CacheLookup(&Cache, "example.com", 1000 + 300 * 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache lookup accepted an expired entry"));
    }

    // Test 5: entry just before the TTL boundary is a hit.
    Results->TestsRun++;
    if (DNS_CacheLookup(&Cache, "example.com", 1000 + 300 * 1000 - 1, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache lookup rejected a valid entry"));
    }

    // Test 6: TTL is capped at the configured maximum.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "example.com", DNSTEST_RESOLVED_IP, 0xFFFFFFFF, 1000) &&
        DNS_CacheLookup(&Cache, "example.com", 1000 + DNS_CACHE_MAX_TTL_SECONDS * 1000 - 1, &ResolvedIP_Be) &&
        !DNS_CacheLookup(&Cache, "example.com", 1000 + DNS_CACHE_MAX_TTL_SECONDS * 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache TTL cap is not applied"));
    }

    // Test 7: oldest entry evicted when the cache is full.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "h0.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h1.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h2.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h3.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h4.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h5.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h6.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h7.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h8.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h9.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h10.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h11.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h12.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h13.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h14.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h15.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h16.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        !DNS_CacheLookup(&Cache, "h0.example", 1000, &ResolvedIP_Be) &&
        DNS_CacheLookup(&Cache, "h1.example", 1000, &ResolvedIP_Be) &&
        DNS_CacheLookup(&Cache, "h16.example", 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache failed to evict the oldest entry when full"));
    }

    // Test 8: an expired entry is reused before eviction when the cache is full.
    Results->TestsRun++;
    DNS_CacheReset(&Cache);
    if (DNS_CacheStore(&Cache, "h0.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h1.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h2.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h3.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h4.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h5.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h6.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h7.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h8.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h9.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h10.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h11.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h12.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h13.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h14.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "h15.example", DNSTEST_RESOLVED_IP, 300, 1000) &&
        DNS_CacheStore(&Cache, "fresh.example", DNSTEST_RESOLVED_IP, 300, 1000 + 300 * 1000) &&
        DNS_CacheLookup(&Cache, "fresh.example", 1000 + 300 * 1000, &ResolvedIP_Be)) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Cache failed to reuse an expired entry when full"));
    }
}

/************************************************************************/

/**
 * @brief Main DNS test function that runs all DNS unit tests.
 *
 * @param Results Pointer to TEST_RESULTS structure.
 */

void TestDNS(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Expected ERROR paths exercised by the tests must not fail the smoke
    // test; they are wrapped in the autotest error scope.
    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_BEGIN"));

    TestDNSEncodeName(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    TestDNSEncodeQuery(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    TestDNSReadName(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    TestDNSParseResponse(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    TestDNSCache(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_END"));

    TEST(TEXT("DNS tests passed %u/%u"), Results->TestsPassed, Results->TestsRun);
}
