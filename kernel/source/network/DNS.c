
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

#include "network/DNS.h"
#include "network/UDP.h"
#include "network/UDPContext.h"
#include "network/IPv4.h"
#include "network/NetworkManager.h"
#include "core/Device.h"
#include "core/ID.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "system/Clock.h"
#include "process/Task.h"

/************************************************************************/
// Global device pointer (single network device assumption)

static LPDEVICE DATA_SECTION g_DNSDevice = NULL;

/************************************************************************/

LPDNS_CONTEXT DNS_GetContext(LPDEVICE Device) {
    return (LPDNS_CONTEXT)GetDeviceContext(Device, KOID_DNS);
}

/************************************************************************/

/**
 * @brief Generates a pseudo-random DNS transaction ID.
 *
 * @return Transaction ID.
 */

static U16 DNS_GenerateTransactionID(void) {
    static U32 DATA_SECTION Counter = 0x2468ACE0;
    Counter = (Counter * 1103515245 + 12345) & 0x7FFFFFFF;
    return (U16)Counter;
}

/************************************************************************/

/**
 * @brief Calculates the retry timeout with capped exponential backoff.
 *
 * @param RetryCount Number of retries already attempted.
 * @return Timeout in milliseconds.
 */

static U32 DNS_GetRetryTimeout(U32 RetryCount) {
    U32 Shift = RetryCount;

    if (Shift > DNS_RETRY_BACKOFF_MAX_SHIFT) {
        Shift = DNS_RETRY_BACKOFF_MAX_SHIFT;
    }

    return DNS_RETRY_TIMEOUT_MILLIS << Shift;
}

/************************************************************************/

/**
 * @brief Sends the encoded query held in the pending resolution.
 *
 * @param Device  Network device.
 * @param Context DNS context.
 * @return TRUE on success, FALSE otherwise.
 */

static BOOL DNS_SendPendingQuery(LPDEVICE Device, LPDNS_CONTEXT Context) {
    if (Device == NULL || Context == NULL) {
        return FALSE;
    }

    if (Context->Server_Be == 0 || Context->PendingResolution.QueryLength == 0) {
        ERROR(TEXT("No DNS server or query payload available"));
        return FALSE;
    }

    return UDP_Send(
        Device,
        Context->Server_Be,
        DNS_PORT,
        DNS_PORT,
        Context->PendingResolution.Query,
        Context->PendingResolution.QueryLength);
}

/************************************************************************/

/**
 * @brief Retrieves the configured DNS server address for a device.
 *
 * @param Device Network device.
 * @return DNS server IPv4 address in big-endian order, or 0 when unset.
 */

U32 DNS_GetServerAddress(LPDEVICE Device) {
    if (Device == NULL) {
        return 0;
    }

    return NetworkManager_GetDNSServer(Device);
}

/************************************************************************/

/**
 * @brief Encodes a host name into DNS wire format.
 *
 * Each dot-separated label is prefixed with its length and the name is
 * terminated by a zero byte.
 *
 * @param Name         Host name (for example "www.example.com").
 * @param Buffer       Output buffer.
 * @param BufferLength Size of the output buffer in bytes.
 * @return Number of bytes written, or 0 on failure.
 */

UINT DNS_EncodeName(LPCSTR Name, U8* Buffer, UINT BufferLength) {
    UINT Offset = 0;
    UINT LabelStart = 0;
    UINT Index = 0;
    UINT NameLength;

    if (Name == NULL || Buffer == NULL) {
        return 0;
    }

    NameLength = StringLength(Name);
    if (NameLength == 0 || NameLength > DNS_MAX_NAME_LENGTH) {
        return 0;
    }

    while (Index <= NameLength) {
        STR Character = (Index < NameLength) ? Name[Index] : STR_NULL;
        UINT LabelLength;

        if (Character == (STR)'.' || Character == STR_NULL) {
            LabelLength = Index - LabelStart;

            if (LabelLength == 0 && Index < NameLength) {
                ERROR(TEXT("Empty label in name"));
                return 0;
            }

            if (LabelLength > DNS_MAX_LABEL_LENGTH) {
                ERROR(TEXT("Label too long: %u bytes"), LabelLength);
                return 0;
            }

            if (Offset + LabelLength + 1 > BufferLength) {
                ERROR(TEXT("Name buffer overflow"));
                return 0;
            }

            Buffer[Offset] = (U8)LabelLength;
            if (LabelLength > 0) {
                MemoryCopy(Buffer + Offset + 1, Name + LabelStart, LabelLength);
            }
            Offset += LabelLength + 1;

            if (Index == NameLength) {
                if (Offset >= BufferLength) {
                    ERROR(TEXT("Name buffer overflow"));
                    return 0;
                }
                Buffer[Offset] = 0;
                Offset++;
                break;
            }

            LabelStart = Index + 1;
        }

        Index++;
    }

    return Offset;
}

/************************************************************************/

/**
 * @brief Encodes a DNS query (header + single A question).
 *
 * @param TransactionID Transaction identifier.
 * @param Name          Host name to resolve.
 * @param Buffer        Output buffer.
 * @param BufferLength  Size of the output buffer in bytes.
 * @return Total query length in bytes, or 0 on failure.
 */

UINT DNS_EncodeQuery(U16 TransactionID, LPCSTR Name, U8* Buffer, UINT BufferLength) {
    LPDNS_HEADER Header;
    UINT Offset;
    UINT NameLength;

    if (Buffer == NULL || BufferLength < (sizeof(DNS_HEADER) + 1 + 4)) {
        ERROR(TEXT("Query buffer too small"));
        return 0;
    }

    Header = (LPDNS_HEADER)Buffer;
    Header->TransactionID = Htons(TransactionID);
    Header->Flags = Htons(DNS_FLAG_RD);
    Header->QuestionCount = Htons(1);
    Header->AnswerCount = 0;
    Header->AuthorityCount = 0;
    Header->AdditionalCount = 0;

    Offset = sizeof(DNS_HEADER);

    NameLength = DNS_EncodeName(Name, Buffer + Offset, BufferLength - Offset);
    if (NameLength == 0) {
        ERROR(TEXT("Failed to encode name"));
        return 0;
    }
    Offset += NameLength;

    if (Offset + 4 > BufferLength) {
        ERROR(TEXT("Query buffer overflow"));
        return 0;
    }

    Buffer[Offset] = 0;
    Buffer[Offset + 1] = DNS_TYPE_A;
    Buffer[Offset + 2] = 0;
    Buffer[Offset + 3] = DNS_CLASS_IN;
    Offset += 4;

    return Offset;
}

/************************************************************************/

/**
 * @brief Reads a DNS wire-format name from a packet.
 *
 * Handles label sequences and compression pointers. When the name is
 * compressed, the returned offset is the position just past the pointer on
 * the wire; label bytes followed through pointers are not counted. Decodes
 * the name into the output buffer when one is provided.
 *
 * @param Payload        Packet payload.
 * @param PayloadLength  Payload length.
 * @param Offset         Offset of the name in the payload.
 * @param OutName        Output buffer for the decoded name, or NULL.
 * @param OutNameLength  Size of the output buffer.
 * @return Offset just past the name on the wire, or 0 on malformed input.
 */

UINT DNS_ReadName(const U8* Payload, U32 PayloadLength, U32 Offset, STR* OutName, UINT OutNameLength) {
    U32 ReadOffset = Offset;
    U32 FollowOffset = Offset;
    U32 FollowCount = 0;
    UINT OutOffset = 0;
    BOOL Jumped = FALSE;

    while (TRUE) {
        U8 LengthByte;
        U32 Next;

        if (FollowOffset >= PayloadLength) {
            return 0;
        }

        LengthByte = Payload[FollowOffset];

        if (LengthByte == 0) {
            if (!Jumped) {
                ReadOffset = FollowOffset + 1;
            }
            break;
        }

        if ((LengthByte & 0xC0) == 0xC0) {
            if (FollowOffset + 1 >= PayloadLength) {
                return 0;
            }
            if (FollowCount >= DNS_MAX_COMPRESSION_POINTERS) {
                return 0;
            }
            FollowCount++;
            Next = ((U32)(LengthByte & 0x3F) << 8) | Payload[FollowOffset + 1];
            if (Next >= FollowOffset) {
                return 0;
            }
            if (!Jumped) {
                ReadOffset = FollowOffset + 2;
                Jumped = TRUE;
            }
            FollowOffset = Next;
            continue;
        }

        if ((LengthByte & 0xC0) != 0) {
            return 0;
        }

        if (FollowOffset + 1 + LengthByte > PayloadLength) {
            return 0;
        }

        if (OutName != NULL) {
            if (OutOffset > 0) {
                if (OutOffset >= OutNameLength) {
                    return 0;
                }
                OutName[OutOffset] = (STR)'.';
                OutOffset++;
            }
            if (OutOffset + LengthByte >= OutNameLength) {
                return 0;
            }
            MemoryCopy(OutName + OutOffset, Payload + FollowOffset + 1, LengthByte);
            OutOffset += LengthByte;
        }

        FollowOffset += 1 + LengthByte;
    }

    if (OutName != NULL) {
        if (OutOffset >= OutNameLength) {
            return 0;
        }
        OutName[OutOffset] = STR_NULL;
    }

    return ReadOffset;
}

/************************************************************************/

/**
 * @brief Clears all entries of a DNS cache.
 *
 * @param Cache DNS cache.
 */

void DNS_CacheReset(LPDNS_CACHE Cache) {
    if (Cache == NULL) {
        return;
    }

    MemorySet(Cache, 0, sizeof(DNS_CACHE));
}

/************************************************************************/

/**
 * @brief Checks whether a cache entry has expired.
 *
 * The comparison uses unsigned wrap-around arithmetic so that a system time
 * rollover does not keep entries alive forever.
 *
 * @param ExpiresMillis Entry expiration time in milliseconds.
 * @param CurrentMillis Current time in milliseconds.
 * @return TRUE when the entry is expired.
 */

static BOOL DNS_CacheIsExpired(U32 ExpiresMillis, U32 CurrentMillis) {
    return (U32)(CurrentMillis - ExpiresMillis) < 0x80000000;
}

/************************************************************************/

/**
 * @brief Stores a name-to-address mapping in a DNS cache.
 *
 * The record time to live is capped at DNS_CACHE_MAX_TTL_SECONDS. When the
 * cache is full, an expired entry is replaced first, otherwise the oldest
 * entry is evicted round-robin.
 *
 * @param Cache         DNS cache.
 * @param Name          Host name.
 * @param IP_Be         Resolved IPv4 address in big-endian order.
 * @param TTLSeconds    Record time to live in seconds.
 * @param CurrentMillis Current time in milliseconds.
 * @return TRUE on success, FALSE otherwise.
 */

BOOL DNS_CacheStore(LPDNS_CACHE Cache, LPCSTR Name, U32 IP_Be, U32 TTLSeconds, U32 CurrentMillis) {
    LPDNS_CACHE_ENTRY Entry;
    U32 Index;

    if (Cache == NULL || Name == NULL || IP_Be == 0) {
        return FALSE;
    }

    if (TTLSeconds > DNS_CACHE_MAX_TTL_SECONDS) {
        TTLSeconds = DNS_CACHE_MAX_TTL_SECONDS;
    }

    if (Cache->Count < DNS_CACHE_MAX_ENTRIES) {
        Entry = &Cache->Entries[Cache->Count];
        Cache->Count++;
    } else {
        Entry = NULL;
        for (Index = 0; Index < DNS_CACHE_MAX_ENTRIES; Index++) {
            if (DNS_CacheIsExpired(Cache->Entries[Index].ExpiresMillis, CurrentMillis)) {
                Entry = &Cache->Entries[Index];
                break;
            }
        }
        if (Entry == NULL) {
            Entry = &Cache->Entries[Cache->NextSlot];
            Cache->NextSlot = (Cache->NextSlot + 1) % DNS_CACHE_MAX_ENTRIES;
        }
    }

    MemorySet(Entry, 0, sizeof(DNS_CACHE_ENTRY));
    StringCopyLimit(Entry->Name, Name, DNS_MAX_NAME_LENGTH);
    Entry->IP_Be = IP_Be;
    Entry->ExpiresMillis = CurrentMillis + TTLSeconds * 1000;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Looks up a host name in a DNS cache.
 *
 * Expired entries are treated as misses. The name comparison is
 * case-insensitive.
 *
 * @param Cache         DNS cache.
 * @param Name          Host name to look up.
 * @param CurrentMillis Current time in milliseconds.
 * @param OutIP_Be      Receives the resolved IPv4 address (big-endian) on a
 *                      hit, or NULL when not needed.
 * @return TRUE on a cache hit, FALSE otherwise.
 */

BOOL DNS_CacheLookup(LPDNS_CACHE Cache, LPCSTR Name, U32 CurrentMillis, U32* OutIP_Be) {
    U32 Index;

    if (Cache == NULL || Name == NULL) {
        return FALSE;
    }

    for (Index = 0; Index < Cache->Count; Index++) {
        LPDNS_CACHE_ENTRY Entry = &Cache->Entries[Index];

        if (StringCompareNC(Entry->Name, Name) == 0 && !DNS_CacheIsExpired(Entry->ExpiresMillis, CurrentMillis)) {
            if (OutIP_Be != NULL) {
                *OutIP_Be = Entry->IP_Be;
            }
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Starts an asynchronous resolution of a host name.
 *
 * The query is encoded and sent over UDP to the configured DNS server. The
 * caller polls the outcome with DNS_CheckResolution.
 *
 * @param Device Network device.
 * @param Name   Host name to resolve.
 * @return TRUE when the query was sent, FALSE otherwise.
 */

BOOL DNS_StartResolution(LPDEVICE Device, LPCSTR Name) {
    LPDNS_CONTEXT Context;
    LPIPV4_CONTEXT IPv4Context;
    U16 TransactionID;
    UINT QueryLength;
    U32 Server_Be;
    U32 CachedIP_Be;

    if (Device == NULL || Name == NULL) {
        return FALSE;
    }

    Context = DNS_GetContext(Device);
    SAFE_USE(Context) {
        if (Context->PendingResolution.IsPending) {
            WARNING(TEXT("A DNS resolution is already pending"));
            return FALSE;
        }

        Server_Be = DNS_GetServerAddress(Device);
        if (Server_Be == 0) {
            WARNING(TEXT("No DNS server configured"));
            return FALSE;
        }
        Context->Server_Be = Server_Be;

        IPv4Context = IPv4_GetContext(Device);
        SAFE_USE(IPv4Context) {
            if (IPv4Context->LocalIPv4_Be == 0) {
                WARNING(TEXT("Network is not ready"));
                return FALSE;
            }
        }

        // Serve the name from the cache when an unexpired entry exists,
        // avoiding a network round-trip.
        if (DNS_CacheLookup(&Context->Cache, Name, GetSystemTime(), &CachedIP_Be)) {
            DEBUG(TEXT("DNS cache hit for %s"), Name);

            Context->PendingResolution.IsPending = TRUE;
            StringCopyLimit(Context->PendingResolution.Name, Name, DNS_MAX_NAME_LENGTH);
            Context->ResponseReceived = TRUE;
            Context->ResponseTimedOut = FALSE;
            Context->ResponseCode = DNS_RCODE_NO_ERROR;
            Context->ResponseHasAnswer = TRUE;
            Context->ResponseTruncated = FALSE;
            Context->ResponseIP_Be = CachedIP_Be;
            return TRUE;
        }

        TransactionID = DNS_GenerateTransactionID();
        QueryLength = DNS_EncodeQuery(TransactionID, Name, Context->PendingResolution.Query, DNS_QUERY_BUFFER_SIZE);
        if (QueryLength == 0) {
            ERROR(TEXT("Failed to encode DNS query"));
            return FALSE;
        }

        Context->PendingResolution.TransactionID = TransactionID;
        Context->PendingResolution.QueryLength = QueryLength;
        Context->PendingResolution.RetryCount = 0;
        Context->PendingResolution.StartMillis = GetSystemTime();
        Context->PendingResolution.IsPending = TRUE;
        StringCopyLimit(Context->PendingResolution.Name, Name, DNS_MAX_NAME_LENGTH);
        Context->ResponseReceived = FALSE;
        Context->ResponseTimedOut = FALSE;
        Context->ResponseCode = DNS_RCODE_NO_ERROR;
        Context->ResponseHasAnswer = FALSE;
        Context->ResponseTruncated = FALSE;
        Context->ResponseIP_Be = 0;

        if (!DNS_SendPendingQuery(Device, Context)) {
            WARNING(TEXT("Failed to send DNS query"));
            Context->PendingResolution.IsPending = FALSE;
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Maps a DNS response code to a resolution status.
 *
 * @param ResponseCode DNS response code from the packet flags.
 * @return Corresponding DNS_STATUS value.
 */

static DNS_STATUS DNS_MapResponseCode(U16 ResponseCode) {
    switch (ResponseCode) {
        case DNS_RCODE_NO_ERROR:
            return DNS_STATUS_SUCCESS;
        case DNS_RCODE_FORMAT_ERROR:
            return DNS_STATUS_FORMAT_ERROR;
        case DNS_RCODE_NAME_ERROR:
            return DNS_STATUS_NAME_ERROR;
        case DNS_RCODE_NOT_IMPLEMENTED:
            return DNS_STATUS_NOT_IMPLEMENTED;
        case DNS_RCODE_REFUSED:
            return DNS_STATUS_REFUSED;
        case DNS_RCODE_SERVER_FAILURE:
        default:
            return DNS_STATUS_SERVER_FAILURE;
    }
}

/************************************************************************/

/**
 * @brief Polls the outcome of the pending resolution.
 *
 * @param Device          Network device.
 * @param OutResolvedIP_Be Output IPv4 address in big-endian order (A record).
 * @return DNS_STATUS describing the resolution outcome.
 */

DNS_STATUS DNS_CheckResolution(LPDEVICE Device, U32* OutResolvedIP_Be) {
    LPDNS_CONTEXT Context;

    if (Device == NULL) {
        return DNS_STATUS_TIMEOUT;
    }

    Context = DNS_GetContext(Device);
    SAFE_USE(Context) {
        if (!Context->PendingResolution.IsPending) {
            return DNS_STATUS_TIMEOUT;
        }

        if (Context->ResponseReceived) {
            Context->PendingResolution.IsPending = FALSE;

            if (OutResolvedIP_Be != NULL) {
                *OutResolvedIP_Be = Context->ResponseIP_Be;
            }

            if (Context->ResponseTruncated) {
                return DNS_STATUS_TRUNCATED;
            }

            if (Context->ResponseCode == DNS_RCODE_NO_ERROR) {
                return Context->ResponseHasAnswer ? DNS_STATUS_SUCCESS : DNS_STATUS_NO_ANSWER;
            }

            return DNS_MapResponseCode(Context->ResponseCode);
        }

        if (Context->ResponseTimedOut) {
            Context->PendingResolution.IsPending = FALSE;
            return DNS_STATUS_TIMEOUT;
        }

        return DNS_STATUS_PENDING;
    }

    return DNS_STATUS_TIMEOUT;
}

/************************************************************************/

/**
 * @brief Aborts a pending resolution without waiting for a reply.
 *
 * @param Device Network device.
 */

void DNS_CancelResolution(LPDEVICE Device) {
    LPDNS_CONTEXT Context;

    if (Device == NULL) {
        return;
    }

    Context = DNS_GetContext(Device);
    SAFE_USE(Context) {
        Context->PendingResolution.IsPending = FALSE;
        Context->ResponseReceived = FALSE;
        Context->ResponseTimedOut = FALSE;
        Context->ResponseHasAnswer = FALSE;
        Context->ResponseTruncated = FALSE;
        Context->ResponseIP_Be = 0;
    }
}

/************************************************************************/

/**
 * @brief Resolves a host name synchronously.
 *
 * Starts the asynchronous resolution and blocks the caller until the outcome
 * is known or the timeout expires. The caller task yields through Sleep while
 * the network update path drives the retry and timeout policy through
 * DNS_Tick. On any outcome other than success, the pending resolution is
 * cancelled so a late response cannot pollute the context.
 *
 * @param Device           Network device.
 * @param Name             Host name to resolve.
 * @param TimeoutMillis    Maximum time to wait in milliseconds.
 * @param OutResolvedIP_Be Receives the resolved IPv4 address (big-endian).
 * @return DNS_STATUS describing the resolution outcome.
 */

DNS_STATUS DNS_Resolve(LPDEVICE Device, LPCSTR Name, U32 TimeoutMillis, U32* OutResolvedIP_Be) {
    U32 StartMillis;
    DNS_STATUS Status;

    if (Device == NULL || Name == NULL || Name[0] == STR_NULL || TimeoutMillis == 0) {
        return DNS_STATUS_BAD_PARAMETER;
    }

    if (!DNS_StartResolution(Device, Name)) {
        return DNS_STATUS_TIMEOUT;
    }

    StartMillis = GetSystemTime();
    while (TRUE) {
        Status = DNS_CheckResolution(Device, OutResolvedIP_Be);
        if (Status != DNS_STATUS_PENDING) {
            break;
        }

        if ((U32)(GetSystemTime() - StartMillis) >= TimeoutMillis) {
            Status = DNS_STATUS_TIMEOUT;
            break;
        }

        Sleep(DNS_RESOLVE_POLL_INTERVAL_MILLISECONDS);
    }

    if (Status != DNS_STATUS_SUCCESS) {
        DNS_CancelResolution(Device);
    }

    return Status;
}

/************************************************************************/

/**
 * @brief Handles retry and timeout for a pending resolution.
 *
 * @param Device Network device.
 */

void DNS_Tick(LPDEVICE Device) {
    LPDNS_CONTEXT Context;
    UINT CurrentMillis;
    UINT ElapsedMillis;
    UINT TimeoutMillis;

    if (Device == NULL) {
        return;
    }

    Context = DNS_GetContext(Device);
    SAFE_USE(Context) {
        if (!Context->PendingResolution.IsPending || Context->ResponseReceived) {
            return;
        }

        CurrentMillis = GetSystemTime();
        ElapsedMillis = CurrentMillis - Context->PendingResolution.StartMillis;
        TimeoutMillis = DNS_GetRetryTimeout(Context->PendingResolution.RetryCount);

        if (ElapsedMillis < TimeoutMillis) {
            return;
        }

        if (Context->PendingResolution.RetryCount >= DNS_MAX_RETRIES) {
            WARNING(TEXT("DNS resolution timed out after %u retries"), Context->PendingResolution.RetryCount);
            Context->ResponseTimedOut = TRUE;
            return;
        }

        Context->PendingResolution.RetryCount++;
        WARNING(
            TEXT("DNS query timeout after %u ms (backoff %u ms), retry %u/%u"),
            ElapsedMillis,
            TimeoutMillis,
            Context->PendingResolution.RetryCount,
            DNS_MAX_RETRIES);

        if (!DNS_SendPendingQuery(Device, Context)) {
            Context->ResponseTimedOut = TRUE;
        }
    }
}

/************************************************************************/

/**
 * @brief Parses a DNS response packet.
 *
 * Maps the response code, checks the truncation flag, validates the question
 * echo and scans the answer section for the first A record. Malformed packets
 * and question echoes that do not match the expected name yield
 * DNS_STATUS_MALFORMED so that callers can discard the packet and keep the
 * retry/timeout policy active.
 *
 * @param Payload          DNS packet payload.
 * @param PayloadLength    Payload length.
 * @param ExpectedName     Name that must be echoed in the question section,
 *                         or NULL to skip the echo validation.
 * @param OutResolvedIP_Be Receives the A record IPv4 address (big-endian).
 * @param OutTTLSeconds    Receives the A record time to live in seconds, or
 *                         NULL when not needed.
 * @return DNS_STATUS describing the parsed response.
 */

DNS_STATUS DNS_ParseResponse(
    const U8* Payload, U32 PayloadLength, LPCSTR ExpectedName, U32* OutResolvedIP_Be, U32* OutTTLSeconds) {
    LPDNS_HEADER Header;
    U16 Flags;
    U16 ResponseCode;
    U16 QuestionCount;
    U16 AnswerCount;
    U32 Offset;
    U32 Index;
    U32 ResponseIP_Be = 0;
    STR ResponseName[DNS_MAX_NAME_LENGTH + 1];

    if (Payload == NULL || PayloadLength < sizeof(DNS_HEADER)) {
        return DNS_STATUS_MALFORMED;
    }

    Header = (LPDNS_HEADER)Payload;
    Flags = Ntohs(Header->Flags);
    ResponseCode = (U16)(Flags & DNS_FLAG_RCODE_MASK);

    if (ResponseCode != DNS_RCODE_NO_ERROR) {
        return DNS_MapResponseCode(ResponseCode);
    }

    if ((Flags & DNS_FLAG_TC) != 0) {
        return DNS_STATUS_TRUNCATED;
    }

    QuestionCount = Ntohs(Header->QuestionCount);
    AnswerCount = Ntohs(Header->AnswerCount);
    Offset = sizeof(DNS_HEADER);

    // Skip the question section and validate the question echo.
    for (Index = 0; Index < QuestionCount; Index++) {
        UINT Next = DNS_ReadName(Payload, PayloadLength, Offset, ResponseName, sizeof(ResponseName));

        if (Next == 0 || Next + 4 > PayloadLength) {
            return DNS_STATUS_MALFORMED;
        }

        if (Index == 0 && ExpectedName != NULL && StringCompareNC(ResponseName, ExpectedName) != 0) {
            return DNS_STATUS_MALFORMED;
        }

        Offset = Next + 4;
    }

    // Scan the answer section for an A record.
    for (Index = 0; Index < AnswerCount; Index++) {
        U16 Type;
        U16 Class;
        U16 RDLength;
        UINT Next = DNS_ReadName(Payload, PayloadLength, Offset, NULL, 0);

        if (Next == 0 || Next + 10 > PayloadLength) {
            return DNS_STATUS_MALFORMED;
        }

        Type = (U16)((Payload[Next] << 8) | Payload[Next + 1]);
        Class = (U16)((Payload[Next + 2] << 8) | Payload[Next + 3]);
        RDLength = (U16)((Payload[Next + 8] << 8) | Payload[Next + 9]);

        if (Next + 10 + RDLength > PayloadLength) {
            return DNS_STATUS_MALFORMED;
        }

        if (Type == DNS_TYPE_A && Class == DNS_CLASS_IN && RDLength == 4) {
            ResponseIP_Be = ((U32)Payload[Next + 10] << 24) | ((U32)Payload[Next + 11] << 16) |
                            ((U32)Payload[Next + 12] << 8) | (U32)Payload[Next + 13];
            if (OutResolvedIP_Be != NULL) {
                *OutResolvedIP_Be = ResponseIP_Be;
            }
            if (OutTTLSeconds != NULL) {
                *OutTTLSeconds = ((U32)Payload[Next + 4] << 24) | ((U32)Payload[Next + 5] << 16) |
                                 ((U32)Payload[Next + 6] << 8) | (U32)Payload[Next + 7];
            }
            return DNS_STATUS_SUCCESS;
        }

        Offset = Next + 10 + RDLength;
    }

    return DNS_STATUS_NO_ANSWER;
}

/************************************************************************/

/**
 * @brief Handles incoming DNS responses from the UDP layer.
 *
 * Validates the source address, transaction identifier and response flag,
 * then delegates the packet parsing to DNS_ParseResponse. Malformed packets
 * are discarded and the pending resolution is left untouched so that the
 * retry/timeout policy applies.
 *
 * @param SourceIP        Source IPv4 address (big-endian).
 * @param SourcePort      Source port (host byte order).
 * @param DestinationPort Destination port (host byte order).
 * @param DestinationIP   Destination IPv4 address (big-endian).
 * @param Payload         UDP payload.
 * @param PayloadLength   Payload length.
 */

void DNS_OnUDPPacket(
    U32 SourceIP, U16 SourcePort, U16 DestinationPort, U32 DestinationIP, const U8* Payload, U32 PayloadLength) {
    LPDNS_CONTEXT Context;
    LPDNS_HEADER Header;
    U16 TransactionID;
    U16 Flags;
    DNS_STATUS Status;
    U32 ResponseIP_Be = 0;
    U32 ResponseTTLSeconds = 0;

    UNUSED(SourcePort);
    UNUSED(DestinationPort);
    UNUSED(DestinationIP);

    if (g_DNSDevice == NULL) {
        return;
    }

    Context = DNS_GetContext(g_DNSDevice);
    SAFE_USE(Context) {
        if (Payload == NULL || PayloadLength < sizeof(DNS_HEADER)) {
            ERROR(TEXT("Short DNS packet: %u bytes"), PayloadLength);
            return;
        }

        if (SourceIP != Context->Server_Be) {
            return;
        }

        Header = (LPDNS_HEADER)Payload;
        Flags = Ntohs(Header->Flags);

        if ((Flags & DNS_FLAG_RESPONSE) == 0) {
            return;
        }

        TransactionID = Ntohs(Header->TransactionID);
        if (!Context->PendingResolution.IsPending || TransactionID != Context->PendingResolution.TransactionID) {
            return;
        }

        Status = DNS_ParseResponse(
            Payload, PayloadLength, Context->PendingResolution.Name, &ResponseIP_Be, &ResponseTTLSeconds);

        if (Status == DNS_STATUS_MALFORMED) {
            return;
        }

        Context->ResponseReceived = TRUE;
        Context->ResponseCode = (U16)(Flags & DNS_FLAG_RCODE_MASK);
        Context->ResponseHasAnswer = (Status == DNS_STATUS_SUCCESS);
        Context->ResponseTruncated = (Status == DNS_STATUS_TRUNCATED);
        Context->ResponseIP_Be = ResponseIP_Be;

        if (Status == DNS_STATUS_SUCCESS) {
            DEBUG(TEXT("Caching %s with TTL %u seconds"), Context->PendingResolution.Name, ResponseTTLSeconds);
            DNS_CacheStore(
                &Context->Cache, Context->PendingResolution.Name, ResponseIP_Be, ResponseTTLSeconds, GetSystemTime());
        }
    }
}

/************************************************************************/

/**
 * @brief Initializes the DNS context for a device.
 *
 * @param Device Network device.
 */

void DNS_Initialize(LPDEVICE Device) {
    LPDNS_CONTEXT Context;

    if (Device == NULL) {
        return;
    }

    Context = (LPDNS_CONTEXT)KernelHeapAlloc(sizeof(DNS_CONTEXT));
    if (Context == NULL) {
        ERROR(TEXT("Failed to allocate DNS context"));
        return;
    }

    MemorySet(Context, 0, sizeof(DNS_CONTEXT));
    Context->Device = Device;

    SetDeviceContext(Device, KOID_DNS, (LPVOID)Context);

    // Store global device reference
    g_DNSDevice = Device;

    // Register UDP port handler for DNS responses
    UDP_RegisterPortHandler(Device, DNS_PORT, DNS_OnUDPPacket);
}

/************************************************************************/

/**
 * @brief Destroys the DNS context for a device.
 *
 * @param Device Network device.
 */

void DNS_Destroy(LPDEVICE Device) {
    LPDNS_CONTEXT Context;

    if (Device == NULL) {
        return;
    }

    Context = DNS_GetContext(Device);
    SAFE_USE(Context) {
        UDP_UnregisterPortHandler(Device, DNS_PORT);
        KernelHeapFree(Context);
        SetDeviceContext(Device, KOID_DNS, NULL);
    }
}
