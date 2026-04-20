
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


    Transmission Control Protocol (TCP)

\************************************************************************/

#include "network/TCP.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "system/System.h"
#include "network/IPv4.h"
#include "system/Clock.h"
#include "network/Socket.h"
#include "memory/Memory.h"
#include "memory/Heap.h"
#include "utils/Notification.h"
#include "utils/Helpers.h"
#include "text/CoreString.h"
#include "utils/NetworkChecksum.h"
#include "utils/Hysteresis.h"
#include "core/Device.h"

/************************************************************************/
// Configuration

// Helper to get ephemeral port start from configuration
static U16 TCP_GetEphemeralPortStart(void) {
    LPCSTR configValue = GetConfigurationValue(TEXT(CONFIG_TCP_EPHEMERAL_START));

    if (STRING_EMPTY(configValue) == FALSE) {
        U32 port = StringToU32(configValue);
        if (port > 0 && port <= 65535) {
            return (U16)port;
        }
    }

    return TCP_EPHEMERAL_PORT_START_FALLBACK;
}

/************************************************************************/
// Helper to read buffer sizes from configuration with fallback
static UINT TCP_GetConfiguredBufferSize(LPCSTR configKey, U32 fallback, U32 maxLimit) {
    LPCSTR configValue = GetConfigurationValue(configKey);

    if (STRING_EMPTY(configValue) == FALSE) {
        U32 parsedValue = StringToU32(configValue);
        if (parsedValue > 0) {
            if (parsedValue > maxLimit) {
                WARNING(TEXT("%s=%u exceeds maximum %u, clamping"),
                        configKey, parsedValue, maxLimit);
                return (UINT)maxLimit;
            }
            return (UINT)parsedValue;
        }

        WARNING(TEXT("%s has invalid value '%s', using fallback"),
                configKey, configValue);
    }

    return (UINT)fallback;
}

/************************************************************************/
// Global TCP state

typedef struct tag_TCP_GLOBAL_STATE {
    U16 NextEphemeralPort;
    UINT SendBufferSize;
    UINT ReceiveBufferSize;
} TCP_GLOBAL_STATE, *LPTCP_GLOBAL_STATE;

TCP_GLOBAL_STATE DATA_SECTION GlobalTCP;

/************************************************************************/
// Retransmission/cwnd configuration

#define TCP_CONGESTION_INITIAL_WINDOW       TCP_MAX_RETRANSMIT_PAYLOAD
#define TCP_CONGESTION_INITIAL_SSTHRESH     (TCP_MAX_RETRANSMIT_PAYLOAD * 8)
#define TCP_RETRANSMIT_TIMEOUT_MIN          500
#define TCP_RETRANSMIT_TIMEOUT_MAX          60000
#define TCP_DUPLICATE_ACK_THRESHOLD         3

/************************************************************************/
// State machine definitions

// Forward declarations of state handlers
static void TCP_OnEnterClosed(STATE_MACHINE* SM);
static void TCP_OnEnterListen(STATE_MACHINE* SM);
static void TCP_OnEnterSynSent(STATE_MACHINE* SM);
static void TCP_OnEnterSynReceived(STATE_MACHINE* SM);
static void TCP_OnEnterEstablished(STATE_MACHINE* SM);
static void TCP_OnEnterFinWait1(STATE_MACHINE* SM);
static void TCP_OnEnterFinWait2(STATE_MACHINE* SM);
static void TCP_OnEnterCloseWait(STATE_MACHINE* SM);
static void TCP_OnEnterClosing(STATE_MACHINE* SM);
static void TCP_OnEnterLastAck(STATE_MACHINE* SM);
static void TCP_OnEnterTimeWait(STATE_MACHINE* SM);

// Forward declarations of transition actions
static void TCP_ActionSendSyn(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendSynAck(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendAck(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendFin(STATE_MACHINE* SM, LPVOID EventData);
// static void TCP_ActionSendRst(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_IPv4PacketSentCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData);
static void TCP_ActionProcessData(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionAbortConnection(STATE_MACHINE* SM, LPVOID EventData);

// Forward declarations of transition conditions
static BOOL TCP_ConditionValidAck(STATE_MACHINE* SM, LPVOID EventData);
static BOOL TCP_ConditionValidSyn(STATE_MACHINE* SM, LPVOID EventData);
static int TCP_SendPacket(LPTCP_CONNECTION Conn, U8 Flags, const U8* Payload, U32 PayloadLength);

// Forward declarations of retransmission helpers
static U32 TCP_GetSegmentSequenceLength(U8 Flags, U32 PayloadLength);
static BOOL TCP_ShouldTrackRetransmission(U8 Flags, U32 PayloadLength);
static void TCP_ClearRetransmissionState(LPTCP_CONNECTION Conn);
static void TCP_OnCongestionNewAck(LPTCP_CONNECTION Conn);
static void TCP_OnCongestionTimeoutLoss(LPTCP_CONNECTION Conn);
static void TCP_OnCongestionFastLoss(LPTCP_CONNECTION Conn);
static void TCP_StartTrackedRetransmission(LPTCP_CONNECTION Conn, U8 Flags, const U8* Payload, U32 PayloadLength, U32 SequenceStart);
static BOOL TCP_RetransmitTrackedSegment(LPTCP_CONNECTION Conn, BOOL FastRetransmit);
static void TCP_HandleAcknowledgement(LPTCP_CONNECTION Conn, LPTCP_PACKET_EVENT Event);
static U32 TCP_GetAllowedSendBytes(LPTCP_CONNECTION Conn);

// State definitions
static SM_STATE_DEFINITION TCP_States[] = {
    { TCP_STATE_CLOSED,       TCP_OnEnterClosed,       NULL, NULL },
    { TCP_STATE_LISTEN,       TCP_OnEnterListen,       NULL, NULL },
    { TCP_STATE_SYN_SENT,     TCP_OnEnterSynSent,      NULL, NULL },
    { TCP_STATE_SYN_RECEIVED, TCP_OnEnterSynReceived,  NULL, NULL },
    { TCP_STATE_ESTABLISHED,  TCP_OnEnterEstablished,  NULL, NULL },
    { TCP_STATE_FIN_WAIT_1,   TCP_OnEnterFinWait1,     NULL, NULL },
    { TCP_STATE_FIN_WAIT_2,   TCP_OnEnterFinWait2,     NULL, NULL },
    { TCP_STATE_CLOSE_WAIT,   TCP_OnEnterCloseWait,    NULL, NULL },
    { TCP_STATE_CLOSING,      TCP_OnEnterClosing,      NULL, NULL },
    { TCP_STATE_LAST_ACK,     TCP_OnEnterLastAck,      NULL, NULL },
    { TCP_STATE_TIME_WAIT,    TCP_OnEnterTimeWait,     NULL, NULL }
};

// Transition definitions
static SM_TRANSITION TCP_Transitions[] = {
    // From CLOSED
    { TCP_STATE_CLOSED, TCP_EVENT_CONNECT, TCP_STATE_SYN_SENT, NULL, TCP_ActionSendSyn },
    { TCP_STATE_CLOSED, TCP_EVENT_LISTEN, TCP_STATE_LISTEN, NULL, NULL },

    // From LISTEN
    { TCP_STATE_LISTEN, TCP_EVENT_RCV_SYN, TCP_STATE_SYN_RECEIVED, TCP_ConditionValidSyn, TCP_ActionSendSynAck },
    { TCP_STATE_LISTEN, TCP_EVENT_CLOSE, TCP_STATE_CLOSED, NULL, NULL },

    // From SYN_SENT
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_SYN, TCP_STATE_SYN_RECEIVED, TCP_ConditionValidSyn, TCP_ActionSendAck },
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_SYN_SENT, TCP_EVENT_CLOSE, TCP_STATE_CLOSED, NULL, TCP_ActionAbortConnection },
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, TCP_ActionAbortConnection },

    // From SYN_RECEIVED
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_CLOSE, TCP_STATE_FIN_WAIT_1, NULL, TCP_ActionSendFin },
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_RCV_RST, TCP_STATE_LISTEN, NULL, NULL },

    // From ESTABLISHED
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_DATA, TCP_STATE_ESTABLISHED, NULL, TCP_ActionProcessData },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_CLOSE, TCP_STATE_FIN_WAIT_1, NULL, TCP_ActionSendFin },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_FIN, TCP_STATE_CLOSE_WAIT, NULL, TCP_ActionSendAck },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From FIN_WAIT_1
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_ACK, TCP_STATE_FIN_WAIT_2, TCP_ConditionValidAck, NULL },
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_FIN, TCP_STATE_CLOSING, NULL, TCP_ActionSendAck },
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From FIN_WAIT_2
    { TCP_STATE_FIN_WAIT_2, TCP_EVENT_RCV_FIN, TCP_STATE_TIME_WAIT, NULL, TCP_ActionSendAck },
    { TCP_STATE_FIN_WAIT_2, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From CLOSE_WAIT
    { TCP_STATE_CLOSE_WAIT, TCP_EVENT_CLOSE, TCP_STATE_LAST_ACK, NULL, TCP_ActionSendFin },

    // From CLOSING
    { TCP_STATE_CLOSING, TCP_EVENT_RCV_ACK, TCP_STATE_TIME_WAIT, TCP_ConditionValidAck, NULL },
    { TCP_STATE_CLOSING, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From LAST_ACK
    { TCP_STATE_LAST_ACK, TCP_EVENT_RCV_ACK, TCP_STATE_CLOSED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_LAST_ACK, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From TIME_WAIT
    { TCP_STATE_TIME_WAIT, TCP_EVENT_TIMEOUT, TCP_STATE_CLOSED, NULL, NULL }
};

/************************************************************************/

static BOOL TCP_IsPortInUse(U16 port, U32 localIP) {
    LPLIST ConnectionList = GetTCPConnectionList();
    LPTCP_CONNECTION conn = (LPTCP_CONNECTION)(ConnectionList != NULL ? ConnectionList->First : NULL);
    while (conn != NULL) {
        if (conn->LocalPort == Htons(port) && conn->LocalIP == localIP) {
            return TRUE;
        }
        conn = (LPTCP_CONNECTION)conn->Next;
    }
    return FALSE;
}

/************************************************************************/

static U16 TCP_GetNextEphemeralPort(U32 localIP) {
    U16 startPort = TCP_GetEphemeralPortStart();
    U16 maxPort = 65535;
    U16 attempts = 0;
    U16 maxAttempts = maxPort - startPort + 1;

    // Initialize with a pseudo-random port if not set
    if (GlobalTCP.NextEphemeralPort == 0) {
        // Simple pseudo-random based on system time and IP
        UINT seed = GetSystemTime() ^ (localIP & 0xFFFF);
        GlobalTCP.NextEphemeralPort = startPort + (seed % (maxPort - startPort + 1));
    }

    U16 port = GlobalTCP.NextEphemeralPort;

    // Find next available port, avoiding conflicts
    while (attempts < maxAttempts) {
        if (!TCP_IsPortInUse(port, localIP)) {
            // Update NextEphemeralPort for next allocation
            GlobalTCP.NextEphemeralPort = port + 1;
            if (GlobalTCP.NextEphemeralPort > maxPort) {
                GlobalTCP.NextEphemeralPort = startPort;
            }
            return port;
        }

        port++;
        if (port > maxPort) {
            port = startPort;
        }
        attempts++;
    }

    // If we get here, all ports are in use (very unlikely)
    return startPort; // Return start port as fallback
}

/************************************************************************/

/**
 * @brief Returns the sequence-space length consumed by a segment.
 * @param Flags TCP segment flags.
 * @param PayloadLength Segment payload length.
 * @return Number of sequence values consumed.
 */
static U32 TCP_GetSegmentSequenceLength(U8 Flags, U32 PayloadLength) {
    U32 SequenceLength = PayloadLength;

    if ((Flags & TCP_FLAG_SYN) != 0) {
        SequenceLength++;
    }

    if ((Flags & TCP_FLAG_FIN) != 0) {
        SequenceLength++;
    }

    return SequenceLength;
}

/************************************************************************/

/**
 * @brief Determines if a segment must be tracked for retransmission.
 * @param Flags TCP segment flags.
 * @param PayloadLength Segment payload length.
 * @return TRUE if retransmission tracking is required.
 */
static BOOL TCP_ShouldTrackRetransmission(U8 Flags, U32 PayloadLength) {
    if (PayloadLength > 0) {
        return TRUE;
    }

    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0) {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Clears tracked retransmission metadata for a connection.
 * @param Conn Target TCP connection.
 */
static void TCP_ClearRetransmissionState(LPTCP_CONNECTION Conn) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        Conn->RetransmitPending = FALSE;
        Conn->RetransmitPayloadLength = 0;
        Conn->RetransmitFlags = 0;
        Conn->RetransmitSequenceStart = 0;
        Conn->RetransmitSequenceEnd = 0;
        Conn->RetransmitTimestamp = 0;
        Conn->RetransmitTimer = 0;
        Conn->RetransmitCount = 0;
        Conn->RetransmitWasRetried = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Applies slow-start / congestion-avoidance on a new ACK.
 * @param Conn Target TCP connection.
 */
static void TCP_OnCongestionNewAck(LPTCP_CONNECTION Conn) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        U32 CongestionWindow = Conn->CongestionWindow;

        if (CongestionWindow == 0) {
            CongestionWindow = TCP_CONGESTION_INITIAL_WINDOW;
        }

        if (CongestionWindow < Conn->SlowStartThreshold) {
            CongestionWindow += TCP_MAX_RETRANSMIT_PAYLOAD;
        } else {
            U32 Increment = (TCP_MAX_RETRANSMIT_PAYLOAD * TCP_MAX_RETRANSMIT_PAYLOAD) / CongestionWindow;
            if (Increment == 0) {
                Increment = 1;
            }
            CongestionWindow += Increment;
        }

        if (CongestionWindow > Conn->SendBufferCapacity) {
            CongestionWindow = Conn->SendBufferCapacity;
        }

        Conn->CongestionWindow = CongestionWindow;
    }
}

/************************************************************************/

/**
 * @brief Applies congestion state transition for timeout loss.
 * @param Conn Target TCP connection.
 */
static void TCP_OnCongestionTimeoutLoss(LPTCP_CONNECTION Conn) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        U32 HalfWindow = Conn->CongestionWindow / 2;
        U32 MinimumThreshold = TCP_MAX_RETRANSMIT_PAYLOAD * 2;

        if (HalfWindow < MinimumThreshold) {
            HalfWindow = MinimumThreshold;
        }

        Conn->SlowStartThreshold = HalfWindow;
        Conn->CongestionWindow = TCP_CONGESTION_INITIAL_WINDOW;
        Conn->InFastRecovery = FALSE;
        Conn->FastRecoverySequence = 0;
    }
}

/************************************************************************/

/**
 * @brief Applies congestion state transition for fast retransmit loss.
 * @param Conn Target TCP connection.
 */
static void TCP_OnCongestionFastLoss(LPTCP_CONNECTION Conn) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        U32 HalfWindow = Conn->CongestionWindow / 2;
        U32 MinimumThreshold = TCP_MAX_RETRANSMIT_PAYLOAD * 2;

        if (HalfWindow < MinimumThreshold) {
            HalfWindow = MinimumThreshold;
        }

        Conn->SlowStartThreshold = HalfWindow;
        Conn->CongestionWindow = HalfWindow + (TCP_DUPLICATE_ACK_THRESHOLD * TCP_MAX_RETRANSMIT_PAYLOAD);
        Conn->InFastRecovery = TRUE;
        Conn->FastRecoverySequence = Conn->SendNext;
    }
}

/************************************************************************/

/**
 * @brief Starts retransmission tracking for a freshly transmitted segment.
 * @param Conn Target TCP connection.
 * @param Flags Segment flags.
 * @param Payload Segment payload pointer.
 * @param PayloadLength Segment payload length.
 * @param SequenceStart Segment sequence start number.
 */
static void TCP_StartTrackedRetransmission(LPTCP_CONNECTION Conn, U8 Flags, const U8* Payload, U32 PayloadLength, U32 SequenceStart) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        U32 TrackedLength = PayloadLength;
        U32 SequenceLength = TCP_GetSegmentSequenceLength(Flags, PayloadLength);

        if (TrackedLength > TCP_MAX_RETRANSMIT_PAYLOAD) {
            TrackedLength = TCP_MAX_RETRANSMIT_PAYLOAD;
        }

        Conn->RetransmitFlags = Flags;
        Conn->RetransmitPayloadLength = TrackedLength;
        Conn->RetransmitSequenceStart = SequenceStart;
        Conn->RetransmitSequenceEnd = SequenceStart + SequenceLength;
        Conn->RetransmitTimestamp = GetSystemTime();
        Conn->RetransmitTimer = Conn->RetransmitTimestamp + Conn->RetransmitCurrentTimeout;
        Conn->RetransmitCount = 0;
        Conn->RetransmitPending = TRUE;
        Conn->RetransmitWasRetried = FALSE;

        if (TrackedLength > 0 && Payload != NULL) {
            MemoryCopy(Conn->RetransmitPayload, Payload, TrackedLength);
        }
    }
}

/************************************************************************/

/**
 * @brief Retransmits the tracked segment.
 * @param Conn Target TCP connection.
 * @param FastRetransmit Indicates a duplicate-ACK-triggered retransmit.
 * @return TRUE if retransmission succeeded.
 */
static BOOL TCP_RetransmitTrackedSegment(LPTCP_CONNECTION Conn, BOOL FastRetransmit) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        if (Conn->RetransmitPending == FALSE) {
            return FALSE;
        }

        U32 PreviousSendNext = Conn->SendNext;
        U32 PreviousSendUnacked = Conn->SendUnacked;
        U32 PreviousSequenceStart = Conn->RetransmitSequenceStart;
        U32 PreviousRetransmitCount = Conn->RetransmitCount;
        U32 PreviousRetransmitTimeout = Conn->RetransmitCurrentTimeout;
        U32 PayloadLength = Conn->RetransmitPayloadLength;
        U8 Flags = Conn->RetransmitFlags;
        const U8* Payload = (PayloadLength > 0) ? Conn->RetransmitPayload : NULL;

        Conn->SendNext = PreviousSequenceStart;
        I32 SendResult = TCP_SendPacket(Conn, Flags, Payload, PayloadLength);
        Conn->SendNext = PreviousSendNext;
        Conn->SendUnacked = PreviousSendUnacked;

        if (SendResult < 0) {
            return FALSE;
        }

        Conn->RetransmitWasRetried = TRUE;
        Conn->RetransmitCount = PreviousRetransmitCount;
        Conn->RetransmitCurrentTimeout = PreviousRetransmitTimeout;
        Conn->RetransmitTimestamp = GetSystemTime();

        if (FastRetransmit) {
            Conn->RetransmitTimer = Conn->RetransmitTimestamp + Conn->RetransmitCurrentTimeout;
        } else {
            Conn->RetransmitCount++;

            if (Conn->RetransmitCurrentTimeout < TCP_RETRANSMIT_TIMEOUT_MAX) {
                U32 NextTimeout = Conn->RetransmitCurrentTimeout << 1;
                if (NextTimeout < Conn->RetransmitCurrentTimeout || NextTimeout > TCP_RETRANSMIT_TIMEOUT_MAX) {
                    NextTimeout = TCP_RETRANSMIT_TIMEOUT_MAX;
                }
                Conn->RetransmitCurrentTimeout = NextTimeout;
            }

            Conn->RetransmitTimer = Conn->RetransmitTimestamp + Conn->RetransmitCurrentTimeout;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Processes ACK progression for retransmission and congestion control.
 * @param Conn Target TCP connection.
 * @param Event Packet event carrying ACK information.
 */
static void TCP_HandleAcknowledgement(LPTCP_CONNECTION Conn, LPTCP_PACKET_EVENT Event) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        if (Event == NULL || Event->Header == NULL) {
            return;
        }

        U32 AckNum = Ntohl(Event->Header->AckNumber);
        U32 Now = GetSystemTime();
        BOOL IsDuplicateAck = FALSE;
        BOOL HasNoPayload = (Event->PayloadLength == 0);

        if (AckNum == Conn->LastAckNumber && HasNoPayload) {
            IsDuplicateAck = TRUE;
        }

        if (AckNum > Conn->SendUnacked) {
            Conn->SendUnacked = AckNum;
        }

        if (IsDuplicateAck) {
            Conn->DuplicateAckCount++;
            if (Conn->DuplicateAckCount >= TCP_DUPLICATE_ACK_THRESHOLD &&
                Conn->RetransmitPending &&
                AckNum == Conn->RetransmitSequenceStart) {
                TCP_OnCongestionFastLoss(Conn);
                if (TCP_RetransmitTrackedSegment(Conn, TRUE) == TRUE) {
                }
            }
            return;
        }

        Conn->DuplicateAckCount = 0;
        Conn->LastAckNumber = AckNum;

        if (Conn->InFastRecovery && AckNum >= Conn->FastRecoverySequence) {
            Conn->InFastRecovery = FALSE;
            Conn->CongestionWindow = Conn->SlowStartThreshold;
        }

        if (Conn->RetransmitPending && AckNum >= Conn->RetransmitSequenceEnd) {
            if (Conn->RetransmitWasRetried == FALSE && Conn->RetransmitTimestamp > 0 && Now >= Conn->RetransmitTimestamp) {
                U32 SampleRTT = Now - Conn->RetransmitTimestamp;
                U32 Smoothed = ((Conn->RetransmitBaseTimeout * 7) + SampleRTT) / 8;

                if (Smoothed < TCP_RETRANSMIT_TIMEOUT_MIN) {
                    Smoothed = TCP_RETRANSMIT_TIMEOUT_MIN;
                } else if (Smoothed > TCP_RETRANSMIT_TIMEOUT_MAX) {
                    Smoothed = TCP_RETRANSMIT_TIMEOUT_MAX;
                }

                Conn->RetransmitBaseTimeout = Smoothed;
            }

            Conn->RetransmitCurrentTimeout = Conn->RetransmitBaseTimeout;
            TCP_ClearRetransmissionState(Conn);
            TCP_OnCongestionNewAck(Conn);
        }
    }
}

/************************************************************************/

/**
 * @brief Returns the allowed send bytes according to congestion state.
 * @param Conn Target TCP connection.
 * @return Number of bytes allowed to be sent immediately.
 */
static U32 TCP_GetAllowedSendBytes(LPTCP_CONNECTION Conn) {
    SAFE_USE_VALID_ID(Conn, KOID_TCP) {
        if (Conn->RetransmitPending && Conn->SendNext > Conn->SendUnacked) {
            return 0;
        }

        U32 InFlight = Conn->SendNext - Conn->SendUnacked;

        if (Conn->CongestionWindow <= InFlight) {
            return 0;
        }

        return Conn->CongestionWindow - InFlight;
    }

    return 0;
}

/************************************************************************/

static int TCP_SendPacket(LPTCP_CONNECTION Conn, U8 Flags, const U8* Payload, U32 PayloadLength) {
    TCP_HEADER Header;
    U8 Options[4] = {0}; // MSS option: 4 bytes
    U32 OptionsLength = 0;

    // Add MSS option for SYN packets
    if (Flags & TCP_FLAG_SYN) {
        Options[0] = 2;    // MSS option type
        Options[1] = 4;    // MSS option length
        Options[2] = 0x05; // MSS = 1460 (0x05B4) in network byte order
        Options[3] = 0xB4;
        OptionsLength = 4;
    }

    U32 HeaderLength = sizeof(TCP_HEADER) + OptionsLength;
    U32 TotalLength = HeaderLength + PayloadLength;
    U8 Packet[TotalLength];

    // Fill TCP header (ports already in network byte order)
    Header.SourcePort = Conn->LocalPort;
    Header.DestinationPort = Conn->RemotePort;
    Header.SequenceNumber = Htonl(Conn->SendNext);
    Header.AckNumber = Htonl(Conn->RecvNext);
    Header.DataOffset = ((HeaderLength / 4) << 4); // Data offset in 4-byte words, shifted to upper nibble
    Header.Flags = Flags;
    // Always calculate window based on actual TCP buffer space, not cached value
    UINT AvailableSpace = (Conn->RecvBufferCapacity > Conn->RecvBufferUsed)
                          ? (Conn->RecvBufferCapacity - Conn->RecvBufferUsed)
                          : 0;
    U16 ActualWindow = (AvailableSpace > 0xFFFFU) ? 0xFFFFU : (U16)AvailableSpace;
    Header.WindowSize = Htons(ActualWindow);
    Conn->LastAdvertisedWindow = ActualWindow;
    Header.UrgentPointer = 0;
    Header.Checksum = 0;

    // Copy header, options, and payload to packet
    MemoryCopy(Packet, &Header, sizeof(TCP_HEADER));
    if (OptionsLength > 0) {
        MemoryCopy(Packet + sizeof(TCP_HEADER), Options, OptionsLength);
    }
    if (Payload && PayloadLength > 0) {
        MemoryCopy(Packet + HeaderLength, Payload, PayloadLength);
    }

    // Calculate checksum
    ((LPTCP_HEADER)Packet)->Checksum = TCP_CalculateChecksum((LPTCP_HEADER)Packet,
        Payload, PayloadLength, Conn->LocalIP, Conn->RemoteIP);

    // Debug: Show the actual TCP header being sent (convert from network to host order for display)
    LPTCP_HEADER TcpHdr = (LPTCP_HEADER)Packet;
    UNUSED(TcpHdr);


    // Send via IPv4 through connection's network device
    I32 SendResult = 0;
    U32 SequenceStart = Conn->SendNext;
    U32 SequenceLength = TCP_GetSegmentSequenceLength(Flags, PayloadLength);
    LPDEVICE Device = Conn->Device;

    if (Device == NULL) {
        return 0;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    SendResult = IPv4_Send(Device, Conn->RemoteIP, IPV4_PROTOCOL_TCP, Packet, HeaderLength + PayloadLength);
    UnlockMutex(&(Device->Mutex));

    if (SendResult < 0) {
        return SendResult;
    }

    // Track retransmission only for sequence-bearing segments
    if (TCP_ShouldTrackRetransmission(Flags, PayloadLength)) {
        TCP_StartTrackedRetransmission(Conn, Flags, Payload, PayloadLength, SequenceStart);
        if (Conn->SendUnacked == 0 || Conn->SendUnacked > SequenceStart) {
            Conn->SendUnacked = SequenceStart;
        }
    }

    // Update sequence number if data was sent
    if (SequenceLength > 0) {
        Conn->SendNext += SequenceLength;
    }

    return SendResult;
}

/************************************************************************/
// State handlers

static void TCP_OnEnterClosed(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);

    // Clear all timers and counters to prevent zombie retransmissions
    TCP_ClearRetransmissionState(Conn);
    Conn->DuplicateAckCount = 0;
    Conn->TimeWaitTimer = 0;
    Conn->InFastRecovery = FALSE;

    // Note: We don't need to unregister from global IPv4 notifications
    // as the callback will check the connection state
}

static void TCP_OnEnterListen(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterSynSent(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterSynReceived(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterEstablished(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);

    // Notify upper layers that connection is established
    // Only send notification if we're coming from another state (not a re-entry)
    if (Conn->NotificationContext != NULL && SM->PreviousState != TCP_STATE_ESTABLISHED) {
        Notification_Send(Conn->NotificationContext, NOTIF_EVENT_TCP_CONNECTED, NULL, 0);
    }
}

static void TCP_OnEnterFinWait1(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterFinWait2(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterCloseWait(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterClosing(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterLastAck(STATE_MACHINE* SM) {
    (void)SM;
}

static void TCP_OnEnterTimeWait(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    Conn->TimeWaitTimer = GetSystemTime() + TCP_TIME_WAIT_TIMEOUT;
}

/************************************************************************/
// Transition actions

static void TCP_ActionSendSyn(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);


    Conn->SendNext = 1000; // Initial sequence number
    Conn->SendUnacked = Conn->SendNext;
    Conn->LastAckNumber = Conn->SendUnacked;
    Conn->RetransmitCount = 0;
    Conn->DuplicateAckCount = 0;

    I32 SendResult = TCP_SendPacket(Conn, TCP_FLAG_SYN, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("SYN send failed"));
    }
}

/************************************************************************/

static void TCP_ActionSendSynAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    Conn->SendNext = 2000; // Initial sequence number
    Conn->RecvNext = Ntohl(Event->Header->SequenceNumber) + 1;

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("Failed to send SYN+ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionSendAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (Event && Event->Header) {
        U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
        U8 Flags = Event->Header->Flags;

        // Calculate expected next sequence number
        Conn->RecvNext = SeqNum + Event->PayloadLength;
        if (Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
            Conn->RecvNext++;
        }
    }

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("Failed to send ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionSendFin(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("Failed to send FIN packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

/*
static void TCP_ActionSendRst(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    TCP_SendPacket(Conn, TCP_FLAG_RST, NULL, 0);
}
*/

/************************************************************************/

static void TCP_ActionProcessData(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) {
        return;
    }

    U8 Flags = Event->Header->Flags;
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
    U32 AckTarget = Conn->RecvNext;
    U32 BytesAccepted = 0;
    const U8* PayloadPtr = Event->Payload;
    U32 PayloadLength = Event->PayloadLength;

    if (PayloadLength > 0 && PayloadPtr) {
        if (SeqNum < Conn->RecvNext) {
            U32 AlreadyAcked = Conn->RecvNext - SeqNum;
            if (AlreadyAcked >= PayloadLength) {

                int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
                if (SendResult < 0) {
                    ERROR(TEXT("Failed to send ACK for duplicate segment"));
                    SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
                }
                return;
            }

            SeqNum += AlreadyAcked;
            PayloadPtr += AlreadyAcked;
            PayloadLength -= AlreadyAcked;
        }

        if (SeqNum > Conn->RecvNext) {

            int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
            if (SendResult < 0) {
                ERROR(TEXT("Failed to send ACK for out-of-order segment"));
                SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
            }
            return;
        }


        if (Conn->RecvBufferUsed >= Conn->RecvBufferCapacity) {
            WARNING(TEXT("Receive buffer full, advertising zero window"));

            int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
            if (SendResult < 0) {
                ERROR(TEXT("Failed to send zero window ACK"));
                SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
            }
            return;
        }

        UINT SpaceAvailable = (Conn->RecvBufferCapacity > Conn->RecvBufferUsed)
                              ? (Conn->RecvBufferCapacity - Conn->RecvBufferUsed)
                              : 0;
        U32 CopyLength = (PayloadLength > (U32)SpaceAvailable) ? (U32)SpaceAvailable : PayloadLength;

        if (CopyLength > 0) {
            BytesAccepted = SocketTCPReceiveCallback(Conn, PayloadPtr, CopyLength);

            if (BytesAccepted > 0) {
                MemoryCopy(Conn->RecvBuffer + Conn->RecvBufferUsed, PayloadPtr, BytesAccepted);
                Conn->RecvBufferUsed += BytesAccepted;
            }
        }

        if (BytesAccepted == 0) {
        }
    }

    if (BytesAccepted > 0) {
        U32 Candidate = SeqNum + BytesAccepted;
        if (Candidate > AckTarget) {
            AckTarget = Candidate;
        }
    }

    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0) {
        if (PayloadLength == 0 || BytesAccepted == PayloadLength) {
            AckTarget++;
        }
    }

    if (AckTarget > Conn->RecvNext) {
        Conn->RecvNext = AckTarget;
    }

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("Failed to send ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionAbortConnection(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);

    // Immediately clear all retransmission tracking to stop sending packets
    TCP_ClearRetransmissionState(Conn);
    Conn->DuplicateAckCount = 0;
    Conn->TimeWaitTimer = 0;
}

/************************************************************************/

static void TCP_IPv4PacketSentCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)UserData;
    LPIPV4_PACKET_SENT_DATA PacketData;

    if (!NotificationData || !Conn) return;
    if (NotificationData->EventID != NOTIF_EVENT_IPV4_PACKET_SENT) return;
    if (!NotificationData->Data) return;

    // Check if connection is still active
    if (Conn->StateMachine.CurrentState == TCP_STATE_CLOSED) return;

    PacketData = (LPIPV4_PACKET_SENT_DATA)NotificationData->Data;

    // Check if this packet is for our connection
    if (PacketData->DestinationIP == Conn->RemoteIP && PacketData->Protocol == IPV4_PROTOCOL_TCP) {
        if (Conn->RetransmitPending) {
            U32 Now = GetSystemTime();
            Conn->RetransmitTimestamp = Now;
            Conn->RetransmitTimer = Now + Conn->RetransmitCurrentTimeout;
        }
    }
}

/************************************************************************/
// Helper function to validate sequence numbers within receive window

static BOOL TCP_IsSequenceInWindow(U32 SequenceNumber, U32 WindowStart, U16 WindowSize) {
    // Handle sequence number wrap-around by using modular arithmetic
    U32 WindowEnd = WindowStart + WindowSize;

    // Check if sequence number is within the window
    if (WindowStart <= WindowEnd) {
        // No wrap-around case
        return (SequenceNumber >= WindowStart && SequenceNumber < WindowEnd);
    } else {
        // Wrap-around case
        return (SequenceNumber >= WindowStart || SequenceNumber < WindowEnd);
    }
}

/************************************************************************/
// Transition conditions

static BOOL TCP_ConditionValidAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) return FALSE;

    U32 AckNum = Ntohl(Event->Header->AckNumber);
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
    U8 Flags = Event->Header->Flags;


    // Validate ACK number with cumulative ACK support
    BOOL ValidAck = FALSE;
    if (Conn->SendUnacked == 0 && Conn->SendNext == 0) {
        ValidAck = (AckNum == 0);
    } else if (AckNum >= Conn->SendUnacked && AckNum <= Conn->SendNext) {
        ValidAck = TRUE;
    }

    // For SYN+ACK, accept any sequence number and update RecvNext
    BOOL ValidSeq;
    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        ValidSeq = TRUE;
        Conn->RecvNext = SeqNum + 1;
    } else {
        // Regular ACK - validate sequence number is within receive window
        ValidSeq = TCP_IsSequenceInWindow(SeqNum, Conn->RecvNext, Conn->RecvWindow);
        if (!ValidSeq) {
        }
    }

    BOOL Valid = ValidAck && ValidSeq;

    if (Valid) {
        TCP_HandleAcknowledgement(Conn, Event);
    }

    return Valid;
}

/************************************************************************/

static BOOL TCP_ConditionValidSyn(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) return FALSE;

    // Check SYN flag
    BOOL HasSyn = (Event->Header->Flags & TCP_FLAG_SYN) != 0;
    if (!HasSyn) return FALSE;

    // For SYN packets, additional security checks can be added here
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);

    // In LISTEN state, we accept any valid SYN
    if (SM_GetCurrentState(&Conn->StateMachine) == TCP_STATE_LISTEN) {
        return TRUE;
    }

    // In other states, validate sequence number against receive window
    BOOL ValidSeq = TCP_IsSequenceInWindow(SeqNum, Conn->RecvNext, Conn->RecvWindow);

    if (!ValidSeq) {
    }

    return ValidSeq;
}

/************************************************************************/
// TCP Options parsing

typedef struct tag_TCP_OPTIONS {
    BOOL HasMSS;
    U16 MSS;
    BOOL HasWindowScale;
    U8 WindowScale;
    BOOL HasTimestamp;
    U32 TSVal;
    U32 TSEcr;
} TCP_OPTIONS, *LPTCP_OPTIONS;

static void TCP_ParseOptions(const U8* OptionsData, U32 OptionsLength, LPTCP_OPTIONS ParsedOptions) {
    MemorySet(ParsedOptions, 0, sizeof(TCP_OPTIONS));

    U32 Offset = 0;
    while (Offset < OptionsLength) {
        U8 OptionType = OptionsData[Offset];

        // End of option list
        if (OptionType == 0) {
            break;
        }

        // No-operation (padding)
        if (OptionType == 1) {
            Offset++;
            continue;
        }

        // All other options have a length field
        if (Offset + 1 >= OptionsLength) {
            break;
        }

        U8 OptionLength = OptionsData[Offset + 1];
        if (OptionLength < 2 || Offset + OptionLength > OptionsLength) {
            break;
        }

        switch (OptionType) {
            case 2: // Maximum Segment Size
                if (OptionLength == 4 && Offset + 4 <= OptionsLength) {
                    ParsedOptions->HasMSS = TRUE;
                    ParsedOptions->MSS = (OptionsData[Offset + 2] << 8) | OptionsData[Offset + 3];
                }
                break;

            case 3: // Window Scale
                if (OptionLength == 3 && Offset + 3 <= OptionsLength) {
                    ParsedOptions->HasWindowScale = TRUE;
                    ParsedOptions->WindowScale = OptionsData[Offset + 2];
                }
                break;

            case 8: // Timestamp
                if (OptionLength == 10 && Offset + 10 <= OptionsLength) {
                    ParsedOptions->HasTimestamp = TRUE;
                    ParsedOptions->TSVal = (OptionsData[Offset + 2] << 24) |
                                         (OptionsData[Offset + 3] << 16) |
                                         (OptionsData[Offset + 4] << 8) |
                                         OptionsData[Offset + 5];
                    ParsedOptions->TSEcr = (OptionsData[Offset + 6] << 24) |
                                         (OptionsData[Offset + 7] << 16) |
                                         (OptionsData[Offset + 8] << 8) |
                                         OptionsData[Offset + 9];
                }
                break;

            default:
                break;
        }

        Offset += OptionLength;
    }
}

/************************************************************************/

U16 TCP_CalculateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    U32 HeaderLength = (Header->DataOffset >> 4) * 4;
    U32 TCPTotalLength = HeaderLength + PayloadLength;
    U32 Accumulator = 0;

    // Build IPv4 pseudo-header on stack (12 bytes)
    U8 PseudoHeader[12];
    *((U32*)(PseudoHeader + 0)) = SourceIP;                    // Source IP (already in network order)
    *((U32*)(PseudoHeader + 4)) = DestinationIP;              // Destination IP (already in network order)
    PseudoHeader[8] = 0;                                       // Zero byte
    PseudoHeader[9] = 6;                                       // TCP protocol
    *((U16*)(PseudoHeader + 10)) = Htons((U16)TCPTotalLength); // TCP length

    // Save and clear checksum field
    U16 SavedChecksum = Header->Checksum;
    Header->Checksum = 0;

    // Accumulate pseudo-header
    Accumulator = NetworkChecksum_Calculate_Accumulate(PseudoHeader, 12, Accumulator);

    // Accumulate TCP header
    Accumulator = NetworkChecksum_Calculate_Accumulate((const U8*)Header, HeaderLength, Accumulator);

    // Accumulate payload if present
    if (Payload && PayloadLength > 0) {
        Accumulator = NetworkChecksum_Calculate_Accumulate(Payload, PayloadLength, Accumulator);
    }

    // Restore original checksum
    Header->Checksum = SavedChecksum;

    // Finalize checksum
    return NetworkChecksum_Finalize(Accumulator);
}

/************************************************************************/

int TCP_ValidateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    U16 ReceivedChecksum = Ntohs(Header->Checksum);

    U32 SrcIPHost = Ntohl(SourceIP);
    U32 DstIPHost = Ntohl(DestinationIP);
    UNUSED(SrcIPHost);
    UNUSED(DstIPHost);

    // Calculate expected checksum using the proper TCP checksum function
    U16 CalculatedChecksum = TCP_CalculateChecksum(Header, Payload, PayloadLength, SourceIP, DestinationIP);
    CalculatedChecksum = Ntohs(CalculatedChecksum);

    BOOL IsValid = (CalculatedChecksum == ReceivedChecksum);

    return IsValid ? 1 : 0;
}

/************************************************************************/

/*
static void TCP_SendRstToUnknownConnection(LPDEVICE Device, U32 LocalIP, U16 LocalPort, U32 RemoteIP, U16 RemotePort, U32 AckNumber) {
    TCP_HEADER Header;
    U8 Packet[sizeof(TCP_HEADER)];


    // Fill TCP header for RST response
    Header.SourcePort = LocalPort;       // Already in network byte order
    Header.DestinationPort = RemotePort; // Already in network byte order
    Header.SequenceNumber = 0;          // RST packets typically use seq=0
    Header.AckNumber = Htonl(AckNumber);
    Header.DataOffset = 0x50; // Data offset = 5 (20 bytes), Reserved = 0
    Header.Flags = TCP_FLAG_RST | TCP_FLAG_ACK;
    Header.WindowSize = 0;
    Header.UrgentPointer = 0;
    Header.Checksum = 0; // Calculate later

    // Copy header to packet
    MemoryCopy(Packet, &Header, sizeof(TCP_HEADER));

    // Calculate checksum
    ((LPTCP_HEADER)Packet)->Checksum = TCP_CalculateChecksum((LPTCP_HEADER)Packet,
        NULL, 0, LocalIP, RemoteIP);

    // Send via IPv4 through specified network device
    if (Device == NULL) {
        return;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    IPv4_Send(Device, RemoteIP, IPV4_PROTOCOL_TCP, Packet, sizeof(TCP_HEADER));
    UnlockMutex(&(Device->Mutex));
}
*/

/************************************************************************/

// Public API implementation

void TCP_Initialize(void) {
    MemorySet(&GlobalTCP, 0, sizeof(TCP_GLOBAL_STATE));
    GlobalTCP.NextEphemeralPort = TCP_GetEphemeralPortStart();
    GlobalTCP.SendBufferSize = TCP_GetConfiguredBufferSize(TEXT(CONFIG_TCP_SEND_BUFFER_SIZE),
                                                          TCP_SEND_BUFFER_SIZE,
                                                          TCP_SEND_BUFFER_SIZE);
    GlobalTCP.ReceiveBufferSize = TCP_GetConfiguredBufferSize(TEXT(CONFIG_TCP_RECEIVE_BUFFER_SIZE),
                                                             TCP_RECV_BUFFER_SIZE,
                                                             TCP_RECV_BUFFER_SIZE);


    // TCP protocol handler will be registered later when devices are initialized

}

/************************************************************************/

LPTCP_CONNECTION TCP_CreateConnection(LPDEVICE Device, U32 LocalIP, U16 LocalPort, U32 RemoteIP, U16 RemotePort) {
    if (Device == NULL) {
        return NULL;
    }

    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)CreateKernelObject(sizeof(TCP_CONNECTION), KOID_TCP);
    if (Conn == NULL) {
        return NULL;
    }

    // Initialize TCP-specific fields (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&Conn->Device, 0, sizeof(TCP_CONNECTION) - sizeof(LISTNODE));

    Conn->Device = Device;

    // Set connection parameters - resolve LocalIP if it's 0 (any address)
    if (LocalIP == 0) {
        // Use device's local IP address
        LPIPV4_CONTEXT IPv4Context = IPv4_GetContext(Device);

        SAFE_USE(IPv4Context) {
            Conn->LocalIP = IPv4Context->LocalIPv4_Be;
        } else {
            Conn->LocalIP = 0;
        }
    } else {
        Conn->LocalIP = LocalIP;
    }
    Conn->LocalPort = (LocalPort == 0) ? Htons(TCP_GetNextEphemeralPort(Conn->LocalIP)) : LocalPort;
    Conn->RemoteIP = RemoteIP;
    Conn->RemotePort = RemotePort; // RemotePort should already be in network byte order from socket layer
    Conn->SendBufferCapacity = GlobalTCP.SendBufferSize;
    Conn->RecvBufferCapacity = GlobalTCP.ReceiveBufferSize;
    Conn->SendWindow = (Conn->SendBufferCapacity > 0xFFFFU) ? 0xFFFFU : (U16)Conn->SendBufferCapacity;
    Conn->RecvWindow = (Conn->RecvBufferCapacity > 0xFFFFU) ? 0xFFFFU : (U16)Conn->RecvBufferCapacity;
    Conn->LastAdvertisedWindow = Conn->RecvWindow;
    Conn->RetransmitTimer = 0;
    Conn->RetransmitCount = 0;
    Conn->RetransmitBaseTimeout = TCP_RETRANSMIT_TIMEOUT;
    Conn->RetransmitCurrentTimeout = TCP_RETRANSMIT_TIMEOUT;
    Conn->RetransmitPending = FALSE;
    Conn->RetransmitWasRetried = FALSE;
    Conn->DuplicateAckCount = 0;
    Conn->LastAckNumber = 0;
    Conn->InFastRecovery = FALSE;
    Conn->FastRecoverySequence = 0;
    Conn->CongestionWindow = TCP_CONGESTION_INITIAL_WINDOW;
    Conn->SlowStartThreshold = TCP_CONGESTION_INITIAL_SSTHRESH;

    // Initialize sliding window with hysteresis
    TCP_InitSlidingWindow(Conn);

    // Create notification context for this connection
    Conn->NotificationContext = Notification_CreateContext();
    if (Conn->NotificationContext == NULL) {
        ERROR(TEXT("Failed to create notification context"));
        KernelHeapFree(Conn);
        return NULL;
    }

    // Register for IPv4 packet sent events on the connection's network device
    LockMutex(&(Conn->Device->Mutex), INFINITY);
    IPv4_RegisterNotification(Conn->Device, NOTIF_EVENT_IPV4_PACKET_SENT,
                             TCP_IPv4PacketSentCallback, Conn);
    UnlockMutex(&(Conn->Device->Mutex));

    // Initialize state machine
    SM_Initialize(&Conn->StateMachine, TCP_Transitions,
                  sizeof(TCP_Transitions) / sizeof(SM_TRANSITION),
                  TCP_States, sizeof(TCP_States) / sizeof(SM_STATE_DEFINITION),
                  TCP_STATE_CLOSED, Conn);

    // Add to connections list
    LPLIST ConnectionList = GetTCPConnectionList();
    if (ConnectionList != NULL) {
        ListAddTail(ConnectionList, Conn);
    }

    // Convert to host byte order for debug display
    U32 LocalIPHost = Ntohl(LocalIP);
    U32 RemoteIPHost = Ntohl(RemoteIP);
    UNUSED(LocalIPHost);
    UNUSED(RemoteIPHost);

    return Conn;
}

/************************************************************************/

void TCP_DestroyConnection(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        SM_Destroy(&Connection->StateMachine);

        // Destroy notification context
        SAFE_USE (Connection->NotificationContext) {
            Notification_DestroyContext(Connection->NotificationContext);
            Connection->NotificationContext = NULL;
        }

        // Remove from connections list
        LPLIST ConnectionList = GetTCPConnectionList();
        ListRemove(ConnectionList, Connection);

        // Mark ID
        Connection->TypeID = KOID_NONE;

        // Free the connection memory
        KernelHeapFree(Connection);

    }
}

/************************************************************************/

int TCP_Connect(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_CONNECT, NULL) ? 0 : -1;
    }
    return -1;
}

/************************************************************************/

int TCP_Listen(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_LISTEN, NULL) ? 0 : -1;
    }
    return -1;
}

/************************************************************************/

int TCP_Send(LPTCP_CONNECTION Connection, const U8* Data, U32 Length) {
    if (!Data || Length == 0) return -1;

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        if (SM_GetCurrentState(&Connection->StateMachine) != TCP_STATE_ESTABLISHED) {
            return -1;
        }

        UINT Capacity = Connection->SendBufferCapacity;
        U32 MaxChunk = TCP_MAX_RETRANSMIT_PAYLOAD;
        if (Capacity > 0 && Capacity < MaxChunk) {
            MaxChunk = (U32)Capacity;
        }
        if (MaxChunk == 0) {
            MaxChunk = TCP_MAX_RETRANSMIT_PAYLOAD;
        }

        const U8* CurrentData = Data;
        U32 Remaining = Length;
        U32 TotalSent = 0;

        while (Remaining > 0) {
            U32 Allowed = TCP_GetAllowedSendBytes(Connection);
            if (Allowed == 0) {
                break;
            }

            U32 ChunkSize = (Remaining > MaxChunk) ? MaxChunk : Remaining;
            if (ChunkSize > Allowed) {
                ChunkSize = Allowed;
            }
            if (ChunkSize == 0) {
                break;
            }

            I32 SendResult = TCP_SendPacket(Connection, TCP_FLAG_PSH | TCP_FLAG_ACK, CurrentData, ChunkSize);
            if (SendResult < 0) {
                ERROR(TEXT("Failed to send %u bytes chunk"), ChunkSize);
                return (TotalSent > 0) ? (I32)TotalSent : -1;
            }

            CurrentData += ChunkSize;
            Remaining -= ChunkSize;
            TotalSent += ChunkSize;
        }

        return (I32)TotalSent;
    }
    return -1;
}

/************************************************************************/

int TCP_Receive(LPTCP_CONNECTION Connection, U8* Buffer, U32 BufferSize) {
    if (!Buffer || BufferSize == 0) return -1;

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        if (Connection->RecvBufferUsed == 0) return 0;

        UINT Used = Connection->RecvBufferUsed;
        U32 CopyLength = (Used > BufferSize) ? BufferSize : (U32)Used;
        MemoryCopy(Buffer, Connection->RecvBuffer, CopyLength);

        // Move remaining data to beginning of buffer
        if (CopyLength < Used) {
            MemoryMove(Connection->RecvBuffer, Connection->RecvBuffer + CopyLength, (U32)(Used - CopyLength));
        }

        TCP_HandleApplicationRead(Connection, CopyLength);

        return CopyLength;
    }
    return -1;
}

/************************************************************************/

int TCP_Close(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        BOOL result = SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_CLOSE, NULL);

        return result ? 0 : -1;
    }
    return -1;
}

/************************************************************************/

SM_STATE TCP_GetState(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_GetCurrentState(&Connection->StateMachine);
    }
    return SM_INVALID_STATE;
}

/************************************************************************/

void TCP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    if (PayloadLength < sizeof(TCP_HEADER)) {
        return;
    }

    const TCP_HEADER* Header = (const TCP_HEADER*)Payload;
    U32 HeaderLength = (Header->DataOffset >> 4) * 4;

    // Validate header length
    if (HeaderLength < sizeof(TCP_HEADER) || HeaderLength > PayloadLength) {
        return;
    }

    const U8* Data = Payload + HeaderLength;
    U32 DataLength = PayloadLength - HeaderLength;

    // Parse TCP options if present
    TCP_OPTIONS ParsedOptions;
    if (HeaderLength > sizeof(TCP_HEADER)) {
        U32 OptionsLength = HeaderLength - sizeof(TCP_HEADER);
        const U8* OptionsData = Payload + sizeof(TCP_HEADER);
        TCP_ParseOptions(OptionsData, OptionsLength, &ParsedOptions);
    } else {
        MemorySet(&ParsedOptions, 0, sizeof(TCP_OPTIONS));
    }


    // Validate checksum
    if (!TCP_ValidateChecksum((TCP_HEADER*)Header, Data, DataLength, SourceIP, DestinationIP)) {
        return;
    }

    // Find matching connection
    LPTCP_CONNECTION Conn = NULL;
    LPLIST ConnectionList = GetTCPConnectionList();
    LPTCP_CONNECTION Current =
        (LPTCP_CONNECTION)(ConnectionList != NULL ? ConnectionList->First : NULL);
    while (Current != NULL) {
        if (Current->LocalPort == Header->DestinationPort &&
            Current->RemotePort == Header->SourcePort &&
            Current->RemoteIP == SourceIP &&
            Current->LocalIP == DestinationIP) {
            Conn = Current;
            break;
        }
        Current = (LPTCP_CONNECTION)Current->Next;
    }

    if (Conn == NULL) {

        // Send RST for packets received on unknown connections (except RST packets)
        if (!(Header->Flags & TCP_FLAG_RST)) {
            U32 AckNum = Ntohl(Header->SequenceNumber) + DataLength;
            if (Header->Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
                AckNum++;
            }
            UNUSED(AckNum);
            // TODO: TCP_SendRstToUnknownConnection needs device parameter
            // TCP_SendRstToUnknownConnection(Device, DestinationIP, Header->DestinationPort,
            //                              SourceIP, Header->SourcePort, AckNum);
        }
        return;
    }

    // Create event data
    TCP_PACKET_EVENT Event;
    Event.Header = Header;
    Event.Payload = Data;
    Event.PayloadLength = DataLength;
    Event.SourceIP = SourceIP;
    Event.DestinationIP = DestinationIP;

    // Determine event type based on flags and data length
    U8 Flags = Header->Flags;
    SM_EVENT EventType = TCP_EVENT_RCV_DATA;
    BOOL ProcessResult = FALSE;

    if (DataLength > 0) {
        // Process data
        EventType = TCP_EVENT_RCV_DATA;
        ProcessResult = SM_ProcessEvent(&Conn->StateMachine, EventType, &Event);
    }

    if (Flags & TCP_FLAG_RST) {
        EventType = TCP_EVENT_RCV_RST;
    } else if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        EventType = TCP_EVENT_RCV_ACK;
    } else if (Flags & TCP_FLAG_SYN) {
        EventType = TCP_EVENT_RCV_SYN;
    } else if (Flags & TCP_FLAG_FIN) {
        EventType = TCP_EVENT_RCV_FIN;
    } else if (Flags & TCP_FLAG_ACK) {
        EventType = TCP_EVENT_RCV_ACK;
    }

    ProcessResult = SM_ProcessEvent(&Conn->StateMachine, EventType, &Event);
    UNUSED(ProcessResult);

}

/************************************************************************/

void TCP_Update(void) {
    UINT CurrentTime = GetSystemTime();

    LPLIST ConnectionList = GetTCPConnectionList();
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)(ConnectionList != NULL ? ConnectionList->First : NULL);
    while (Conn != NULL) {
        LPTCP_CONNECTION Next = (LPTCP_CONNECTION)Conn->Next;
        SM_STATE CurrentState = SM_GetCurrentState(&Conn->StateMachine);

        // Check TIME_WAIT timeout
        if (CurrentState == TCP_STATE_TIME_WAIT &&
            Conn->TimeWaitTimer > 0 &&
            CurrentTime >= Conn->TimeWaitTimer) {
            SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_TIMEOUT, NULL);
        }

        // Safety check: if in TIME_WAIT state but timer is invalid, force close
        if (CurrentState == TCP_STATE_TIME_WAIT && Conn->TimeWaitTimer == 0) {
            WARNING(TEXT("TIME_WAIT state with invalid timer, forcing close for connection %p"), (LPVOID)Conn);
            SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_TIMEOUT, NULL);
        }

        if (Conn->RetransmitPending &&
            Conn->RetransmitTimer > 0 &&
            CurrentTime >= Conn->RetransmitTimer) {
            if (Conn->RetransmitCount < TCP_MAX_RETRANSMITS) {
                TCP_OnCongestionTimeoutLoss(Conn);
                if (TCP_RetransmitTrackedSegment(Conn, FALSE) == FALSE) {
                    Conn->RetransmitTimer = CurrentTime + Conn->RetransmitCurrentTimeout;
                }
            } else {
                TCP_ClearRetransmissionState(Conn);

                SAFE_USE(Conn->NotificationContext) {
                    Notification_Send(Conn->NotificationContext, NOTIF_EVENT_TCP_FAILED, NULL, 0);
                }

                SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_RCV_RST, NULL);
            }
        }

        // Update state machine
        SM_Update(&Conn->StateMachine);

        Conn = Next;
    }
}

/************************************************************************/

void TCP_SetNotificationContext(LPTCP_CONNECTION Connection, LPNOTIFICATION_CONTEXT Context) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        Connection->NotificationContext = Context;
    }
}

/************************************************************************/

U32 TCP_RegisterCallback(LPTCP_CONNECTION Connection, U32 Event, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    if (Connection == NULL || Connection->NotificationContext == NULL) {
        ERROR(TEXT("Invalid connection or no notification context"));
        return 1;
    }

    U32 Result = Notification_Register(Connection->NotificationContext, Event, Callback, UserData);
    if (Result != 0) {
        return 0; // Success
    } else {
        ERROR(TEXT("Failed to register callback for event %u on connection %p"), Event, (LPVOID)Connection);
        return 1; // Error
    }
}

/************************************************************************/


/**
 * @brief Initialize sliding window with hysteresis thresholds
 * @param Connection The TCP connection to initialize
 */
void TCP_InitSlidingWindow(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        UINT Capacity = Connection->RecvBufferCapacity;
        U32 MaxWindow = (Capacity > (UINT)MAX_U32) ? MAX_U32 : (U32)Capacity;
        if (MaxWindow == 0) {
            MaxWindow = TCP_RECV_BUFFER_SIZE;
        }
        U32 LowThreshold = MaxWindow / 3;      // 1/3 threshold
        U32 HighThreshold = (MaxWindow * 2) / 3; // 2/3 threshold

        Hysteresis_Initialize(&Connection->WindowHysteresis, LowThreshold, HighThreshold, MaxWindow);

    }
}

/************************************************************************/

/**
 * @brief Process data consumption and update window with hysteresis
 * @param Connection The TCP connection
 * @param DataConsumed Amount of data consumed by application
 */
void TCP_ProcessDataConsumption(LPTCP_CONNECTION Connection, U32 DataConsumed) {
    UNUSED(DataConsumed);
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        // NOTE: RecvBufferUsed is already updated by caller, just calculate window
        UINT AvailableSpace = (Connection->RecvBufferCapacity > Connection->RecvBufferUsed)
                              ? (Connection->RecvBufferCapacity - Connection->RecvBufferUsed)
                              : 0;
        U16 NewWindow = (AvailableSpace > 0xFFFFU) ? 0xFFFFU : (U16)AvailableSpace;

        // Update hysteresis with new window size
        BOOL StateChanged = Hysteresis_Update(&Connection->WindowHysteresis, NewWindow);

        // Note: RecvWindow is no longer used - window is calculated dynamically in TCP_SendPacket


        if (StateChanged) {
        }
    }
}

/************************************************************************/

/**
 * @brief Check if window update ACK should be sent based on hysteresis
 * @param Connection The TCP connection
 * @return TRUE if window update should be sent, FALSE otherwise
 */
BOOL TCP_ShouldSendWindowUpdate(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        BOOL ShouldSend = Hysteresis_IsTransitionPending(&Connection->WindowHysteresis);

        if (ShouldSend) {

            // Clear the transition flag since we're about to send the update
            Hysteresis_ClearTransition(&Connection->WindowHysteresis);
        }

        return ShouldSend;
    }
    return FALSE;
}

/************************************************************************/

void TCP_HandleApplicationRead(LPTCP_CONNECTION Connection, U32 BytesConsumed) {
    if (BytesConsumed == 0) {
        return;
    }

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        UINT PreviousUsed = Connection->RecvBufferUsed;

        if (BytesConsumed > (U32)PreviousUsed) {
            BytesConsumed = (U32)PreviousUsed;
        }

        if (BytesConsumed == 0) {
            return;
        }

        Connection->RecvBufferUsed -= BytesConsumed;

        TCP_ProcessDataConsumption(Connection, BytesConsumed);

        BOOL ShouldSend = TCP_ShouldSendWindowUpdate(Connection);
        UINT AvailableSpace = (Connection->RecvBufferCapacity > Connection->RecvBufferUsed)
                              ? (Connection->RecvBufferCapacity - Connection->RecvBufferUsed)
                              : 0;
        U16 NewWindow = (AvailableSpace > 0xFFFFU) ? 0xFFFFU : (U16)AvailableSpace;
        if (!ShouldSend && NewWindow > Connection->LastAdvertisedWindow) {
            U16 Delta = (U16)(NewWindow - Connection->LastAdvertisedWindow);
            if (Connection->LastAdvertisedWindow == 0 || Delta >= TCP_MAX_RETRANSMIT_PAYLOAD) {
                ShouldSend = TRUE;
            }
        }
        if (!ShouldSend && PreviousUsed == Connection->RecvBufferCapacity &&
            Connection->RecvBufferUsed < Connection->RecvBufferCapacity) {
            ShouldSend = TRUE;
        }

        if (ShouldSend) {
            if (TCP_SendPacket(Connection, TCP_FLAG_ACK, NULL, 0) < 0) {
                ERROR(TEXT("Failed to transmit window update ACK"));
            }
        }
    }
}
