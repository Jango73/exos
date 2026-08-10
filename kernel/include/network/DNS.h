
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


    Domain Name System (DNS) client

\************************************************************************/

#ifndef DNS_H_INCLUDED
#define DNS_H_INCLUDED

#include "Base.h"
#include "core/Device.h"
#include "network/Network.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// DNS Constants

#define DNS_PORT 53

#define DNS_MAX_NAME_LENGTH 255
#define DNS_MAX_LABEL_LENGTH 63
#define DNS_MAX_COMPRESSION_POINTERS 8
#define DNS_QUERY_BUFFER_SIZE 512
#define DNS_MAX_RETRIES 3
#define DNS_RETRY_TIMEOUT_MILLIS (2 * 1000)
#define DNS_RETRY_BACKOFF_MAX_SHIFT 2
#define DNS_CACHE_MAX_ENTRIES 16
#define DNS_CACHE_MAX_TTL_SECONDS 604800

// Poll interval for the synchronous DNS_Resolve API
#define DNS_RESOLVE_POLL_INTERVAL_MILLISECONDS 100

// DNS header flags (RFC 1035)

#define DNS_FLAG_RESPONSE 0x8000
#define DNS_FLAG_OPCODE_MASK 0x7800
#define DNS_FLAG_AA 0x0400
#define DNS_FLAG_TC 0x0200
#define DNS_FLAG_RD 0x0100
#define DNS_FLAG_RA 0x0080
#define DNS_FLAG_RCODE_MASK 0x000F

// DNS response codes

#define DNS_RCODE_NO_ERROR 0
#define DNS_RCODE_FORMAT_ERROR 1
#define DNS_RCODE_SERVER_FAILURE 2
#define DNS_RCODE_NAME_ERROR 3
#define DNS_RCODE_NOT_IMPLEMENTED 4
#define DNS_RCODE_REFUSED 5

// DNS record types

#define DNS_TYPE_A 1
#define DNS_TYPE_NS 2
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_PTR 12
#define DNS_TYPE_AAAA 28

// DNS classes

#define DNS_CLASS_IN 1

/************************************************************************/

typedef struct tag_DNS_HEADER {
    U16 TransactionID;    // Big-endian
    U16 Flags;            // Big-endian
    U16 QuestionCount;    // Big-endian
    U16 AnswerCount;      // Big-endian
    U16 AuthorityCount;   // Big-endian
    U16 AdditionalCount;  // Big-endian
} DNS_HEADER, *LPDNS_HEADER;

/************************************************************************/

typedef enum tag_DNS_STATUS {
    DNS_STATUS_PENDING = 0,
    DNS_STATUS_SUCCESS = 1,
    DNS_STATUS_TIMEOUT = 2,
    DNS_STATUS_FORMAT_ERROR = 3,
    DNS_STATUS_SERVER_FAILURE = 4,
    DNS_STATUS_NAME_ERROR = 5,
    DNS_STATUS_NOT_IMPLEMENTED = 6,
    DNS_STATUS_REFUSED = 7,
    DNS_STATUS_MALFORMED = 8,
    DNS_STATUS_NO_ANSWER = 9,
    DNS_STATUS_TRUNCATED = 10,
    DNS_STATUS_BAD_PARAMETER = 11,
} DNS_STATUS;

/************************************************************************/

typedef struct tag_DNS_RESOLUTION {
    U16 TransactionID;
    U16 IsPending;
    U32 StartMillis;
    U32 RetryCount;
    U32 QueryLength;
    STR Name[DNS_MAX_NAME_LENGTH + 1];
    U8 Query[DNS_QUERY_BUFFER_SIZE];
} DNS_RESOLUTION, *LPDNS_RESOLUTION;

typedef struct tag_DNS_CACHE_ENTRY {
    STR Name[DNS_MAX_NAME_LENGTH + 1];
    U32 IP_Be;
    U32 ExpiresMillis;
} DNS_CACHE_ENTRY, *LPDNS_CACHE_ENTRY;

typedef struct tag_DNS_CACHE {
    DNS_CACHE_ENTRY Entries[DNS_CACHE_MAX_ENTRIES];
    U32 Count;
    U32 NextSlot;
} DNS_CACHE, *LPDNS_CACHE;

typedef struct tag_DNS_CONTEXT {
    LPDEVICE Device;
    U32 Server_Be;
    DNS_CACHE Cache;
    DNS_RESOLUTION PendingResolution;
    U16 ResponseCode;
    U16 ResponseReceived;
    U16 ResponseTimedOut;
    U16 ResponseHasAnswer;
    U16 ResponseTruncated;
    U32 ResponseIP_Be;
} DNS_CONTEXT, *LPDNS_CONTEXT;

/************************************************************************/

LPDNS_CONTEXT DNS_GetContext(LPDEVICE Device);
void DNS_Initialize(LPDEVICE Device);
void DNS_Destroy(LPDEVICE Device);
U32 DNS_GetServerAddress(LPDEVICE Device);
UINT DNS_EncodeName(LPCSTR Name, U8* Buffer, UINT BufferLength);
UINT DNS_EncodeQuery(U16 TransactionID, LPCSTR Name, U8* Buffer, UINT BufferLength);
UINT DNS_ReadName(const U8* Payload, U32 PayloadLength, U32 Offset, STR* OutName, UINT OutNameLength);
DNS_STATUS DNS_ParseResponse(
    const U8* Payload, U32 PayloadLength, LPCSTR ExpectedName, U32* OutResolvedIP_Be, U32* OutTTLSeconds);
void DNS_CacheReset(LPDNS_CACHE Cache);
BOOL DNS_CacheStore(LPDNS_CACHE Cache, LPCSTR Name, U32 IP_Be, U32 TTLSeconds, U32 CurrentMillis);
BOOL DNS_CacheLookup(LPDNS_CACHE Cache, LPCSTR Name, U32 CurrentMillis, U32* OutIP_Be);
BOOL DNS_StartResolution(LPDEVICE Device, LPCSTR Name);
DNS_STATUS DNS_CheckResolution(LPDEVICE Device, U32* OutResolvedIP_Be);
DNS_STATUS DNS_Resolve(LPDEVICE Device, LPCSTR Name, U32 TimeoutMillis, U32* OutResolvedIP_Be);
void DNS_CancelResolution(LPDEVICE Device);
void DNS_Tick(LPDEVICE Device);
void DNS_OnUDPPacket(
    U32 SourceIP, U16 SourcePort, U16 DestinationPort, U32 DestinationIP, const U8* Payload, U32 PayloadLength);

/************************************************************************/

#pragma pack(pop)

#endif  // DNS_H_INCLUDED
