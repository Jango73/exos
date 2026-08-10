
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

#include "network/ICMP.h"

#include "log/Log.h"
#include "memory/Memory.h"
#include "network/IPv4.h"
#include "system/Clock.h"
#include "text/CoreString.h"
#include "utils/NetworkChecksum.h"

/************************************************************************/

#define ICMP_MAX_PACKET_SIZE 1500

/************************************************************************/

typedef struct tag_ICMP_ECHO_ENTRY {
    BOOL IsPending;
    BOOL ReplyReceived;
    U16 Identifier;
    U16 SequenceNumber;
    U32 ReplySourceIP;
    U32 SentTick;
    U32 ReplyTick;
} ICMP_ECHO_ENTRY, *LPICMP_ECHO_ENTRY;

/************************************************************************/

static LPDEVICE DATA_SECTION g_ICMPDevice = NULL;
static ICMP_ECHO_ENTRY DATA_SECTION g_EchoEntries[ICMP_MAX_PENDING_ECHOES];
static U16 DATA_SECTION g_NextIdentifier = 0x0100;
static U16 DATA_SECTION g_NextSequenceNumber = 1;

/************************************************************************/

/**
 * @brief Locate the pending echo entry matching an identifier and sequence.
 *
 * @param Identifier Echo identifier in host byte order.
 * @param SequenceNumber Echo sequence number in host byte order.
 * @return Pointer to the matching entry or NULL when not found.
 */
static LPICMP_ECHO_ENTRY ICMP_FindEchoEntry(U16 Identifier, U16 SequenceNumber) {
    U32 Index;

    for (Index = 0; Index < ICMP_MAX_PENDING_ECHOES; Index++) {
        LPICMP_ECHO_ENTRY Entry = &g_EchoEntries[Index];
        if (Entry->IsPending && Entry->Identifier == Identifier && Entry->SequenceNumber == SequenceNumber) {
            return Entry;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a free echo entry slot.
 *
 * @return Pointer to a free entry or NULL when all slots are busy.
 */
static LPICMP_ECHO_ENTRY ICMP_AllocateEchoEntry(void) {
    U32 Index;

    for (Index = 0; Index < ICMP_MAX_PENDING_ECHOES; Index++) {
        if (!g_EchoEntries[Index].IsPending) {
            return &g_EchoEntries[Index];
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Validate the checksum of a received ICMP message.
 *
 * @param Message Pointer to the ICMP message (header + payload).
 * @param MessageLength Length of the message in bytes.
 * @return TRUE when the checksum matches, FALSE otherwise.
 */
static BOOL ICMP_ValidateChecksum(const U8* Message, U32 MessageLength) {
    U8 HeaderCopy[sizeof(ICMP_HEADER)];
    U32 Accumulator;
    U16 ComputedChecksum;

    if (Message == NULL || MessageLength < sizeof(ICMP_HEADER)) {
        return FALSE;
    }

    MemoryCopy(HeaderCopy, Message, sizeof(ICMP_HEADER));
    ((LPICMP_HEADER)HeaderCopy)->Checksum = 0;

    Accumulator = NetworkChecksum_Calculate_Accumulate(HeaderCopy, sizeof(ICMP_HEADER), 0);
    if (MessageLength > sizeof(ICMP_HEADER)) {
        Accumulator = NetworkChecksum_Calculate_Accumulate(
            Message + sizeof(ICMP_HEADER), MessageLength - sizeof(ICMP_HEADER), Accumulator);
    }

    ComputedChecksum = NetworkChecksum_Finalize(Accumulator);
    return ComputedChecksum == ((LPICMP_HEADER)Message)->Checksum;
}

/************************************************************************/

/**
 * @brief Send one ICMP message through the IPv4 layer.
 *
 * @param DestinationIP Destination IPv4 address (big-endian).
 * @param Type ICMP message type.
 * @param Code ICMP message code.
 * @param Identifier Echo identifier in host byte order.
 * @param SequenceNumber Echo sequence number in host byte order.
 * @param Payload Optional message payload.
 * @param PayloadLength Payload length in bytes.
 * @return 1 on success, 0 on failure.
 */
static int ICMP_SendMessage(
    U32 DestinationIP, U8 Type, U8 Code, U16 Identifier, U16 SequenceNumber, const U8* Payload, U32 PayloadLength) {
    U8 Packet[ICMP_MAX_PACKET_SIZE];
    LPICMP_HEADER Header;
    U32 MessageLength;

    if (g_ICMPDevice == NULL) {
        return 0;
    }

    if (PayloadLength > sizeof(Packet) - sizeof(ICMP_HEADER)) {
        ERROR(TEXT("ICMP message too large: %u bytes"), PayloadLength);
        return 0;
    }

    MemorySet(Packet, 0, sizeof(Packet));

    Header = (LPICMP_HEADER)Packet;
    Header->Type = Type;
    Header->Code = Code;
    Header->Checksum = 0;
    Header->Identifier = Htons(Identifier);
    Header->SequenceNumber = Htons(SequenceNumber);

    MessageLength = sizeof(ICMP_HEADER) + PayloadLength;
    if (PayloadLength > 0) {
        MemoryCopy(Packet + sizeof(ICMP_HEADER), Payload, PayloadLength);
    }

    Header->Checksum = NetworkChecksum_Calculate(Packet, MessageLength);

    return IPv4_Send(g_ICMPDevice, DestinationIP, IPV4_PROTOCOL_ICMP, Packet, MessageLength);
}

/************************************************************************/

/**
 * @brief Answer an incoming echo request with an echo reply.
 *
 * @param Message Pointer to the received ICMP echo request.
 * @param MessageLength Length of the received message.
 * @param SourceIP Source IPv4 address of the request (big-endian).
 */
static void ICMP_SendEchoReply(const ICMP_HEADER* Message, U32 MessageLength, U32 SourceIP) {
    const U8* Payload;
    U32 PayloadLength;

    if (MessageLength <= sizeof(ICMP_HEADER)) {
        return;
    }

    Payload = (const U8*)Message + sizeof(ICMP_HEADER);
    PayloadLength = MessageLength - sizeof(ICMP_HEADER);

    if (ICMP_SendMessage(
            SourceIP,
            ICMP_TYPE_ECHO_REPLY,
            0,
            Ntohs(Message->Identifier),
            Ntohs(Message->SequenceNumber),
            Payload,
            PayloadLength) == 0) {
        DEBUG(TEXT("Failed to send echo reply to %x"), SourceIP);
    }
}

/************************************************************************/

/**
 * @brief Mark a matching pending echo entry as answered.
 *
 * @param Message Pointer to the received ICMP echo reply.
 */
static void ICMP_HandleEchoReply(const ICMP_HEADER* Message) {
    LPICMP_ECHO_ENTRY Entry;
    U16 Identifier;
    U16 SequenceNumber;

    Identifier = Ntohs(Message->Identifier);
    SequenceNumber = Ntohs(Message->SequenceNumber);

    Entry = ICMP_FindEchoEntry(Identifier, SequenceNumber);
    if (Entry == NULL) {
        return;
    }

    Entry->ReplyTick = GetSystemTime();
    Entry->ReplyReceived = TRUE;
}

/************************************************************************/

/**
 * @brief Initializes the ICMP subsystem for a device.
 *
 * Registers the ICMP protocol handler with the IPv4 layer.
 *
 * @param Device Network device to initialize ICMP for.
 */
void ICMP_Initialize(LPDEVICE Device) {
    if (Device == NULL) {
        return;
    }

    g_ICMPDevice = Device;
    IPv4_RegisterProtocolHandler(Device, IPV4_PROTOCOL_ICMP, ICMP_OnIPv4Packet);
}

/************************************************************************/

/**
 * @brief Handles incoming ICMP messages from the IPv4 layer.
 *
 * @param Payload Pointer to the ICMP message (header + payload).
 * @param PayloadLength Length of the ICMP message in bytes.
 * @param SourceIP Source IPv4 address (big-endian).
 * @param DestinationIP Destination IPv4 address (big-endian).
 */
void ICMP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    const ICMP_HEADER* Header;

    UNUSED(DestinationIP);

    if (Payload == NULL || PayloadLength < sizeof(ICMP_HEADER)) {
        return;
    }

    if (!ICMP_ValidateChecksum(Payload, PayloadLength)) {
        DEBUG(TEXT("Dropping ICMP message with invalid checksum"));
        return;
    }

    Header = (const ICMP_HEADER*)Payload;

    switch (Header->Type) {
        case ICMP_TYPE_ECHO_REQUEST:
            ICMP_SendEchoReply(Header, PayloadLength, SourceIP);
            break;

        case ICMP_TYPE_ECHO_REPLY:
            ICMP_HandleEchoReply(Header);
            break;

        default:
            break;
    }
}

/************************************************************************/

/**
 * @brief Start one echo request toward a destination.
 *
 * @param Device Network device to use.
 * @param DestinationIP Destination IPv4 address (big-endian).
 * @param OutIdentifier Receives the allocated echo identifier (host order).
 * @param OutSequenceNumber Receives the allocated sequence number (host order).
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ICMP_StartEcho(LPDEVICE Device, U32 DestinationIP, U16* OutIdentifier, U16* OutSequenceNumber) {
    LPICMP_ECHO_ENTRY Entry;
    U8 Payload[ICMP_ECHO_PAYLOAD_SIZE];
    U32 Index;

    if (Device == NULL || OutIdentifier == NULL || OutSequenceNumber == NULL) {
        return FALSE;
    }

    Entry = ICMP_AllocateEchoEntry();
    if (Entry == NULL) {
        ERROR(TEXT("No free echo entry slots"));
        return FALSE;
    }

    Entry->Identifier = g_NextIdentifier;
    Entry->SequenceNumber = g_NextSequenceNumber;
    Entry->IsPending = TRUE;
    Entry->ReplyReceived = FALSE;
    Entry->ReplySourceIP = 0;
    Entry->SentTick = GetSystemTime();
    Entry->ReplyTick = 0;

    for (Index = 0; Index < sizeof(Payload); Index++) {
        Payload[Index] = (U8)(0x41 + (Index % 0x17));
    }

    if (ICMP_SendMessage(
            DestinationIP,
            ICMP_TYPE_ECHO_REQUEST,
            0,
            Entry->Identifier,
            Entry->SequenceNumber,
            Payload,
            sizeof(Payload)) == 0) {
        Entry->IsPending = FALSE;
        ERROR(TEXT("Failed to send echo request to %x"), DestinationIP);
        return FALSE;
    }

    *OutIdentifier = Entry->Identifier;
    *OutSequenceNumber = Entry->SequenceNumber;

    g_NextIdentifier++;
    if (g_NextIdentifier == 0) {
        g_NextIdentifier = 1;
    }
    g_NextSequenceNumber++;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Poll the status of one pending echo request.
 *
 * @param Identifier Echo identifier (host order).
 * @param SequenceNumber Echo sequence number (host order).
 * @param OutSourceIP Receives the reply source IPv4 address (big-endian) when received.
 * @param OutRoundTripMilliseconds Receives the round trip time in milliseconds when received.
 * @return ICMP_ECHO_RECEIVED when the reply arrived, ICMP_ECHO_PENDING while waiting,
 *         ICMP_ECHO_ENTRY_NOT_FOUND when the entry is not registered.
 */
ICMP_ECHO_STATUS ICMP_CheckEcho(U16 Identifier, U16 SequenceNumber, U32* OutSourceIP, U32* OutRoundTripMilliseconds) {
    LPICMP_ECHO_ENTRY Entry;

    Entry = ICMP_FindEchoEntry(Identifier, SequenceNumber);
    if (Entry == NULL) {
        return ICMP_ECHO_ENTRY_NOT_FOUND;
    }

    if (!Entry->ReplyReceived) {
        return ICMP_ECHO_PENDING;
    }

    if (OutSourceIP != NULL) {
        *OutSourceIP = Entry->ReplySourceIP;
    }
    if (OutRoundTripMilliseconds != NULL) {
        *OutRoundTripMilliseconds = Entry->ReplyTick - Entry->SentTick;
    }

    return ICMP_ECHO_RECEIVED;
}

/************************************************************************/

/**
 * @brief Release a pending echo entry.
 *
 * @param Identifier Echo identifier (host order).
 * @param SequenceNumber Echo sequence number (host order).
 */
void ICMP_CancelEcho(U16 Identifier, U16 SequenceNumber) {
    LPICMP_ECHO_ENTRY Entry;

    Entry = ICMP_FindEchoEntry(Identifier, SequenceNumber);
    if (Entry == NULL) {
        return;
    }

    Entry->IsPending = FALSE;
    Entry->ReplyReceived = FALSE;
}

/************************************************************************/
