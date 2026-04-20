
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


    Berkeley Socket Implementation

\************************************************************************/

#include "network/Socket.h"

#include "system/Clock.h"
#include "memory/Heap.h"
#include "core/ID.h"
#include "network/IPv4.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/NetworkManager.h"
#include "system/System.h"
#include "network/TCP.h"
#include "network/UDP.h"
#include "utils/CircularBuffer.h"

/************************************************************************/
// Global socket management

typedef struct tag_SOCKET_UDP_DATAGRAM_HEADER {
    SOCKET_ADDRESS_INET SourceAddress;
    U32 PayloadLength;
} SOCKET_UDP_DATAGRAM_HEADER, *LPSOCKET_UDP_DATAGRAM_HEADER;

/************************************************************************/

static BOOL Socket_IsUDPPortInUseByAnotherSocket(LPSOCKET ExcludedSocket, U16 PortBe) {
    LPLIST SocketList = GetSocketList();
    LPSOCKET Current = (LPSOCKET)(SocketList != NULL ? SocketList->First : NULL);

    while (Current) {
        SAFE_USE(Current) {
            if (Current != ExcludedSocket &&
                Current->SocketType == SOCKET_TYPE_DGRAM &&
                Current->State >= SOCKET_STATE_BOUND &&
                Current->State != SOCKET_STATE_CLOSED &&
                Current->LocalAddress.Port == PortBe) {
                return TRUE;
            }

            Current = (LPSOCKET)Current->Next;
        } else {
            break;
        }
    }

    return FALSE;
}

/************************************************************************/

static U16 Socket_AllocateEphemeralPort(void) {
    U32 Port;

    for (Port = 49152; Port <= 65535; Port++) {
        if (!Socket_IsUDPPortInUseByAnotherSocket(NULL, Htons((U16)Port))) {
            return (U16)Port;
        }
    }

    return 0;
}

/************************************************************************/

static BOOL Socket_QueueUDPDatagram(LPSOCKET Socket, U32 SourceIP_Be, U16 SourcePort, const U8* Payload, U32 PayloadLength) {
    SOCKET_UDP_DATAGRAM_HEADER Header;
    U8* DatagramData;
    U32 DatagramLength;
    U32 AvailableSpace;
    U32 WrittenLength;

    SAFE_USE_2(Socket, Payload) {
        Header.SourceAddress.AddressFamily = SOCKET_AF_INET;
        Header.SourceAddress.Port = Htons(SourcePort);
        Header.SourceAddress.Address = SourceIP_Be;
        MemorySet(Header.SourceAddress.Zero, 0, sizeof(Header.SourceAddress.Zero));
        Header.PayloadLength = PayloadLength;

        DatagramLength = (U32)sizeof(SOCKET_UDP_DATAGRAM_HEADER) + PayloadLength;
        AvailableSpace = CircularBuffer_GetAvailableSpace(&Socket->ReceiveBuffer);

        if (AvailableSpace < DatagramLength) {
            Socket->ReceiveOverflow = TRUE;
            WARNING(TEXT("Dropping UDP datagram on socket %p (need %u, available %u)"),
                    Socket, DatagramLength, AvailableSpace);
            return FALSE;
        }

        DatagramData = (U8*)KernelHeapAlloc(DatagramLength);
        if (DatagramData == NULL) {
            Socket->ReceiveOverflow = TRUE;
            ERROR(TEXT("Failed to allocate datagram buffer (%u bytes)"), DatagramLength);
            return FALSE;
        }

        MemoryCopy(DatagramData, (const U8*)&Header, sizeof(SOCKET_UDP_DATAGRAM_HEADER));
        if (PayloadLength > 0) {
            MemoryCopy(DatagramData + sizeof(SOCKET_UDP_DATAGRAM_HEADER), Payload, PayloadLength);
        }

        WrittenLength = CircularBuffer_Write(&Socket->ReceiveBuffer, DatagramData, DatagramLength);
        KernelHeapFree(DatagramData);

        if (WrittenLength != DatagramLength) {
            Socket->ReceiveOverflow = TRUE;
            WARNING(TEXT("Partial UDP datagram queue on socket %p (%u/%u)"),
                    Socket, WrittenLength, DatagramLength);
            return FALSE;
        }

        Socket->PacketsReceived++;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static void Socket_UDPPortHandler(U32 SourceIP, U16 SourcePort, U16 DestinationPort, const U8* Payload, U32 PayloadLength) {
    LPLIST SocketList = GetSocketList();
    LPSOCKET Socket = (LPSOCKET)(SocketList != NULL ? SocketList->First : NULL);

    while (Socket) {
        SAFE_USE(Socket) {
            if (Socket->SocketType == SOCKET_TYPE_DGRAM &&
                Socket->State >= SOCKET_STATE_BOUND &&
                Socket->State != SOCKET_STATE_CLOSED &&
                Ntohs(Socket->LocalAddress.Port) == DestinationPort) {
                if (!Socket_QueueUDPDatagram(Socket, SourceIP, SourcePort, Payload, PayloadLength)) {
                    WARNING(TEXT("UDP datagram dropped for socket %p on port %u"),
                            Socket, DestinationPort);
                }
                return;
            }

            Socket = (LPSOCKET)Socket->Next;
        } else {
            return;
        }
    }

}

/************************************************************************/

/**
 * @brief Destructor function for socket control blocks
 *
 * This function is called when a socket control block is being destroyed.
 * It cleans up any allocated resources including pending connections list
 * and TCP connections.
 *
 * @param Item Pointer to the socket control block to destroy
 */
void SocketDestructor(LPVOID Item) {
    LPSOCKET Socket = (LPSOCKET)Item;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->PendingConnections) {
            DeleteList(Socket->PendingConnections);
        }

        if (Socket->TCPConnection != NULL && Socket->SocketType == SOCKET_TYPE_STREAM) {
            TCP_DestroyConnection(Socket->TCPConnection);
        }

        if (Socket->ReceiveBuffer.AllocatedData) {
            KernelHeapFree(Socket->ReceiveBuffer.AllocatedData);
            Socket->ReceiveBuffer.AllocatedData = NULL;
            Socket->ReceiveBuffer.Data = Socket->ReceiveBuffer.InitialData;
            Socket->ReceiveBuffer.Size = Socket->ReceiveBuffer.InitialSize;
        }

        if (Socket->SendBuffer.AllocatedData) {
            KernelHeapFree(Socket->SendBuffer.AllocatedData);
            Socket->SendBuffer.AllocatedData = NULL;
            Socket->SendBuffer.Data = Socket->SendBuffer.InitialData;
            Socket->SendBuffer.Size = Socket->SendBuffer.InitialSize;
        }
    }
}

/************************************************************************/

/**
 * @brief Create a new socket
 *
 * This function creates a new socket of the specified type and protocol.
 * Currently supports AF_INET address family with TCP and UDP protocols.
 *
 * @param AddressFamily Address family (SOCKET_AF_INET)
 * @param SocketType Socket type (SOCKET_TYPE_STREAM or SOCKET_TYPE_DGRAM)
 * @param Protocol Protocol (SOCKET_PROTOCOL_TCP or SOCKET_PROTOCOL_UDP)
 * @return Socket descriptor on success, or negative error code on failure
 */
SOCKET_HANDLE SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol) {

    // Validate parameters
    if (AddressFamily != SOCKET_AF_INET) {
        ERROR(TEXT("Unsupported address family: %d"),AddressFamily);
        return (SOCKET_HANDLE)SOCKET_ERROR_INVALID;
    }

    if (SocketType != SOCKET_TYPE_STREAM && SocketType != SOCKET_TYPE_DGRAM) {
        ERROR(TEXT("Unsupported socket type: %d"),SocketType);
        return (SOCKET_HANDLE)SOCKET_ERROR_INVALID;
    }

    // Allocate socket control block
    LPSOCKET Socket = (LPSOCKET)CreateKernelObject(sizeof(SOCKET), KOID_SOCKET);
    if (!Socket) {
        ERROR(TEXT("Failed to allocate socket control block"));
        return (SOCKET_HANDLE)SOCKET_ERROR_NOMEM;
    }

    // Initialize socket-specific fields (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&Socket->AddressFamily, 0, sizeof(SOCKET) - sizeof(LISTNODE));
    Socket->AddressFamily = AddressFamily;
    Socket->SocketType = SocketType;
    Socket->Protocol = Protocol;
    Socket->State = SOCKET_STATE_CREATED;

    // Set default socket options
    Socket->ReuseAddress = FALSE;
    Socket->KeepAlive = FALSE;
    Socket->NoDelay = FALSE;
    Socket->ReceiveTimeout = 0;
    Socket->SendTimeout = 0;
    Socket->ReceiveTimeoutStartTime = 0;

    // Initialize buffers
    CircularBuffer_Initialize(&Socket->ReceiveBuffer, Socket->ReceiveBufferData, SOCKET_BUFFER_SIZE, SOCKET_MAXIMUM_BUFFER_SIZE);
    CircularBuffer_Initialize(&Socket->SendBuffer, Socket->SendBufferData, SOCKET_BUFFER_SIZE, SOCKET_MAXIMUM_BUFFER_SIZE);
    (void)RateLimiterInit(&Socket->ReceiveLogLimiter, 4, 1000);
    Socket->ReceiveOverflow = FALSE;

    // Add to socket list
    LPLIST SocketList = GetSocketList();
    if (SocketList == NULL || ListAddTail(SocketList, Socket) == 0) {
        ERROR(TEXT("Failed to add socket to list"));
        KernelHeapFree(Socket);
        return (SOCKET_HANDLE)SOCKET_ERROR_NOMEM;
    }

    return (SOCKET_HANDLE)Socket;
}

/************************************************************************/

/**
 * @brief Close a socket
 *
 * This function closes a socket and releases all associated resources.
 * Any pending TCP connections are also closed.
 *
 * @param SocketHandle The socket descriptor to close
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketClose(SOCKET_HANDLE SocketHandle) {

    LPSOCKET Socket = (LPSOCKET)SocketHandle;
    LPDEVICE NetworkDevice;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->SocketType == SOCKET_TYPE_DGRAM &&
            Socket->State >= SOCKET_STATE_BOUND &&
            Socket->LocalAddress.Port != 0 &&
            !Socket_IsUDPPortInUseByAnotherSocket(Socket, Socket->LocalAddress.Port)) {
            NetworkDevice = (LPDEVICE)NetworkManager_GetPrimaryDevice();
            SAFE_USE(NetworkDevice) {
                UDP_UnregisterPortHandler(NetworkDevice, Ntohs(Socket->LocalAddress.Port));
            }
        }

        // Close TCP connection if exists
        SAFE_USE_VALID_ID(Socket->TCPConnection, KOID_TCP) {
            if (Socket->SocketType == SOCKET_TYPE_STREAM) {
                TCP_Close(Socket->TCPConnection);
            }
        }

        // Update socket state
        Socket->State = SOCKET_STATE_CLOSED;

        // Remove from list (this will call the destructor)
        LPLIST SocketList = GetSocketList();
        ListErase(SocketList, Socket);

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Shutdown a socket connection
 *
 * This function shuts down part or all of a socket connection. For TCP sockets,
 * it gracefully closes the connection.
 *
 * @param SocketHandle The socket descriptor to shutdown
 * @param How How to shutdown (SOCKET_SHUTDOWN_READ, SOCKET_SHUTDOWN_WRITE, or SOCKET_SHUTDOWN_BOTH)
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketShutdown(SOCKET_HANDLE SocketHandle, U32 How) {
    UNUSED(How);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {

        // Allow shutdown on connecting sockets too (not just connected ones)
        if (Socket->State == SOCKET_STATE_CLOSED) {
            ERROR(TEXT("Socket %p already closed"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        // For TCP sockets, gracefully close the connection
        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            TCP_Close(Socket->TCPConnection);
            Socket->State = SOCKET_STATE_CLOSING;
        } else {
        }

        return SOCKET_ERROR_NONE;
    }

    ERROR(TEXT("SAFE_USE_VALID_ID failed for socket %p"), (LPVOID)SocketHandle);
    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Create an IPv4 socket address structure
 *
 * This function creates and initializes an IPv4 socket address structure
 * with the specified IP address and port.
 *
 * @param IPAddress IPv4 address in network byte order
 * @param Port Port number in network byte order
 * @param Address Pointer to the address structure to initialize
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressInetMake(U32 IPAddress, U16 Port, LPSOCKET_ADDRESS_INET Address) {
    if (!Address) return SOCKET_ERROR_INVALID;

    MemorySet(Address, 0, sizeof(SOCKET_ADDRESS_INET));
    Address->AddressFamily = SOCKET_AF_INET;
    Address->Port = Port;
    Address->Address = IPAddress;

    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Convert IPv4 address to generic socket address
 *
 * This function converts an IPv4-specific socket address structure to
 * a generic socket address structure.
 *
 * @param InetAddress Pointer to the IPv4 address structure
 * @param GenericAddress Pointer to the generic address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress) {
    if (!InetAddress || !GenericAddress) return SOCKET_ERROR_INVALID;

    MemoryCopy(GenericAddress, InetAddress, sizeof(SOCKET_ADDRESS_INET));
    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Convert generic socket address to IPv4 address
 *
 * This function converts a generic socket address structure to an
 * IPv4-specific socket address structure.
 *
 * @param GenericAddress Pointer to the generic address structure
 * @param InetAddress Pointer to the IPv4 address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressGenericToInet(LPSOCKET_ADDRESS GenericAddress, LPSOCKET_ADDRESS_INET InetAddress) {
    if (!GenericAddress || !InetAddress) return SOCKET_ERROR_INVALID;

    if (GenericAddress->AddressFamily != SOCKET_AF_INET) {
        return SOCKET_ERROR_INVALID;
    }

    MemoryCopy(InetAddress, GenericAddress, sizeof(SOCKET_ADDRESS_INET));
    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Update all sockets
 *
 * This function updates all active sockets, checking for timeouts and
 * handling state transitions. Should be called periodically by the system.
 */
void SocketUpdate(void) {
    // Update all sockets (check timeouts, handle state changes, etc.)
    LPLIST SocketList = GetSocketList();
    if (!SocketList) return;

    LPSOCKET Socket = (LPSOCKET)SocketList->First;

    while (Socket) {
        LPSOCKET NextSocket = (LPSOCKET)Socket->Next;

        SAFE_USE(Socket) {
            // Handle timeouts and state transitions
            if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
                SM_STATE TCPState = TCP_GetState(Socket->TCPConnection);

                // Update socket state based on TCP state
                switch (TCPState) {
                    case TCP_STATE_ESTABLISHED:
                        if (Socket->State == SOCKET_STATE_CONNECTING) {
                            Socket->State = SOCKET_STATE_CONNECTED;
                        }
                        break;

                    case TCP_STATE_CLOSED:
                        if (Socket->State != SOCKET_STATE_CLOSED) {
                            Socket->State = SOCKET_STATE_CLOSED;
                        }
                        break;
                }
            }
        }

        Socket = NextSocket;
    }
}

/************************************************************************/

/**
 * @brief Bind a socket to a local address
 *
 * This function binds a socket to a specified local address and port.
 * The socket must be in the created state and the address must not be in use.
 *
 * @param SocketHandle The socket descriptor to bind
 * @param Address Pointer to the local address to bind to
 * @param AddressLength Size of the address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketBind(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {

    LPSOCKET Socket = (LPSOCKET)SocketHandle;
    U16 EphemeralPort;
    LPDEVICE NetworkDevice;

    if (!Address || AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
        ERROR(TEXT("Invalid address or length"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CREATED) {
            ERROR(TEXT("Socket %p already bound or in invalid state"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INUSE;
        }

        // Convert generic address to inet address
        SOCKET_ADDRESS_INET InetAddress;
        if (SocketAddressGenericToInet(Address, &InetAddress) != SOCKET_ERROR_NONE) {
            ERROR(TEXT("Failed to convert address"));
            return SOCKET_ERROR_INVALID;
        }

        if (Socket->SocketType == SOCKET_TYPE_DGRAM && InetAddress.Port == 0) {
            EphemeralPort = Socket_AllocateEphemeralPort();
            if (EphemeralPort == 0) {
                ERROR(TEXT("No ephemeral UDP port available"));
                return SOCKET_ERROR_INUSE;
            }
            InetAddress.Port = Htons(EphemeralPort);
        }

        // Check if address is already in use (simple check)
        LPLIST SocketList = GetSocketList();
        LPSOCKET ExistingSocket = (LPSOCKET)(SocketList != NULL ? SocketList->First : NULL);

        while (ExistingSocket) {
            SAFE_USE(ExistingSocket) {
                if (ExistingSocket != Socket &&
                    ExistingSocket->State >= SOCKET_STATE_BOUND &&
                    ExistingSocket->LocalAddress.Port == InetAddress.Port &&
                    (ExistingSocket->LocalAddress.Address == InetAddress.Address ||
                     ExistingSocket->LocalAddress.Address == 0 ||
                     InetAddress.Address == 0)) {
                    if (!Socket->ReuseAddress) {
                        ERROR(TEXT("Address already in use"));
                        return SOCKET_ERROR_INUSE;
                    }
                }
                ExistingSocket = (LPSOCKET)ExistingSocket->Next;
            } else {
                break;
            }
        }

        // Bind the address
        MemoryCopy(&Socket->LocalAddress, &InetAddress, sizeof(SOCKET_ADDRESS_INET));
        Socket->State = SOCKET_STATE_BOUND;

        if (Socket->SocketType == SOCKET_TYPE_DGRAM) {
            NetworkDevice = (LPDEVICE)NetworkManager_GetPrimaryDevice();
            if (NetworkDevice == NULL) {
                ERROR(TEXT("No network device available for UDP bind"));
                Socket->State = SOCKET_STATE_CREATED;
                MemorySet(&Socket->LocalAddress, 0, sizeof(SOCKET_ADDRESS_INET));
                return SOCKET_ERROR_INVALID;
            }

            UDP_RegisterPortHandler(NetworkDevice, Ntohs(InetAddress.Port), Socket_UDPPortHandler);
        }


        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Set a socket to listen for incoming connections
 *
 * This function configures a TCP socket to listen for incoming connections.
 * The socket must be bound to a local address before calling this function.
 *
 * @param SocketHandle The socket descriptor to set to listen mode
 * @param Backlog Maximum number of pending connections in the queue
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketListen(SOCKET_HANDLE SocketHandle, U32 Backlog) {

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_BOUND) {
            ERROR(TEXT("Socket %p not bound"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        if (Socket->SocketType != SOCKET_TYPE_STREAM) {
            ERROR(TEXT("Socket %p is not a stream socket"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        // Create pending connections queue
        if (!Socket->PendingConnections) {
            Socket->PendingConnections = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
            if (!Socket->PendingConnections) {
                ERROR(TEXT("Failed to create pending connections queue"));
                return SOCKET_ERROR_NOMEM;
            }
        }

        // Create TCP connection for listening
        Socket->TCPConnection = TCP_CreateConnection(
            (LPDEVICE)NetworkManager_GetPrimaryDevice(),
            Socket->LocalAddress.Address,
            Socket->LocalAddress.Port,
            0, 0);

        if (Socket->TCPConnection == NULL) {
            ERROR(TEXT("Failed to create TCP connection for listening"));
            return SOCKET_ERROR_INVALID;
        }

        // Start listening
        if (TCP_Listen(Socket->TCPConnection) != 0) {
            ERROR(TEXT("Failed to start TCP listening"));
            TCP_DestroyConnection(Socket->TCPConnection);
            Socket->TCPConnection = NULL;
            return SOCKET_ERROR_INVALID;
        }

        Socket->ListenBacklog = Backlog;
        Socket->State = SOCKET_STATE_LISTENING;

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Accept an incoming connection
 *
 * This function accepts a pending connection on a listening socket and
 * creates a new socket for the established connection.
 *
 * @param SocketHandle The listening socket descriptor
 * @param Address Pointer to store the remote address of the accepted connection
 * @param AddressLength Pointer to the size of the address buffer
 * @return New socket descriptor for the accepted connection, or error code on failure
 */
SOCKET_HANDLE SocketAccept(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {

    LPSOCKET ListenSocket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(ListenSocket, KOID_SOCKET) {
        if (ListenSocket->State != SOCKET_STATE_LISTENING) {
            ERROR(TEXT("Socket %p not listening"), (LPVOID)SocketHandle);
            return (SOCKET_HANDLE)SOCKET_ERROR_NOTLISTENING;
        }

        // Check for pending connections
        if (!ListenSocket->PendingConnections || ListenSocket->PendingConnections->NumItems == 0) {
            // No pending connections, would block
            return (SOCKET_HANDLE)SOCKET_ERROR_WOULDBLOCK;
        }

        // Get the first pending connection
        LPSOCKET PendingSocket = (LPSOCKET)ListenSocket->PendingConnections->First;
        if (!PendingSocket) {
            ERROR(TEXT("No pending connection found"));
            return (SOCKET_HANDLE)SOCKET_ERROR_WOULDBLOCK;
        }

        // Remove from pending queue
        ListRemove(ListenSocket->PendingConnections, PendingSocket);

        // Create new socket for the accepted connection
        SOCKET_HANDLE NewSocketDescriptor = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
        if (NewSocketDescriptor == (SOCKET_HANDLE)SOCKET_ERROR_INVALID) {
            ERROR(TEXT("Failed to create new socket for accepted connection"));
            KernelHeapFree(PendingSocket);
            return (SOCKET_HANDLE)SOCKET_ERROR_NOMEM;
        }

        LPSOCKET NewSocket = (LPSOCKET)NewSocketDescriptor;

        SAFE_USE_VALID_ID(NewSocket, KOID_SOCKET) {
            // Copy connection information
            MemoryCopy(&NewSocket->LocalAddress, &ListenSocket->LocalAddress, sizeof(SOCKET_ADDRESS_INET));
            MemoryCopy(&NewSocket->RemoteAddress, &PendingSocket->RemoteAddress, sizeof(SOCKET_ADDRESS_INET));
            NewSocket->TCPConnection = PendingSocket->TCPConnection;
            NewSocket->State = SOCKET_STATE_CONNECTED;

            // Return remote address if requested
            if (Address && AddressLength && *AddressLength >= sizeof(SOCKET_ADDRESS_INET)) {
                SocketAddressInetToGeneric(&NewSocket->RemoteAddress, Address);
                *AddressLength = sizeof(SOCKET_ADDRESS_INET);
            }

            KernelHeapFree(PendingSocket);

            return NewSocketDescriptor;
        } else {
            // SAFE_USE_VALID_ID failed, cleanup and return error
            SocketClose(NewSocketDescriptor);
            KernelHeapFree(PendingSocket);
            ERROR(TEXT("Failed to validate new socket"));
            return (SOCKET_HANDLE)SOCKET_ERROR_INVALID;
        }
    }

    return (SOCKET_HANDLE)SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Connect a socket to a remote address
 *
 * This function initiates a connection to a remote address. For TCP sockets,
 * this performs the three-way handshake to establish a connection.
 *
 * @param SocketHandle The socket descriptor to connect
 * @param Address Pointer to the remote address to connect to
 * @param AddressLength Size of the address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketConnect(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
        ERROR(TEXT("Invalid address or length"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CREATED && Socket->State != SOCKET_STATE_BOUND) {
            ERROR(TEXT("Socket %p in invalid state for connect"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        if (Socket->SocketType != SOCKET_TYPE_STREAM) {
            ERROR(TEXT("Socket %p is not a stream socket"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        // Convert generic address to inet address
        SOCKET_ADDRESS_INET RemoteAddress;
        if (SocketAddressGenericToInet(Address, &RemoteAddress) != SOCKET_ERROR_NONE) {
            ERROR(TEXT("Failed to convert remote address"));
            return SOCKET_ERROR_INVALID;
        }

        // If socket is not bound, bind to any local address
        if (Socket->State == SOCKET_STATE_CREATED) {
            SOCKET_ADDRESS_INET LocalAddress;
            SocketAddressInetMake(0, 0, &LocalAddress); // Any address, any port
            MemoryCopy(&Socket->LocalAddress, &LocalAddress, sizeof(SOCKET_ADDRESS_INET));
            Socket->State = SOCKET_STATE_BOUND;
        }

        // Store remote address
        MemoryCopy(&Socket->RemoteAddress, &RemoteAddress, sizeof(SOCKET_ADDRESS_INET));

        // Get network device and check if ready
        LPDEVICE NetworkDevice = (LPDEVICE)NetworkManager_GetPrimaryDevice();
        if (NetworkDevice == NULL) {
            ERROR(TEXT("No network device available"));
            return SOCKET_ERROR_INVALID;
        }

        // Wait for network to be ready with timeout
        UINT WaitStartMillis = GetSystemTime();
        UINT TimeoutMs = 60000; // 60 seconds timeout
        while (!NetworkManager_IsDeviceReady(NetworkDevice)) {
            UINT ElapsedMs = GetSystemTime() - WaitStartMillis;

            if (ElapsedMs > TimeoutMs) {
                ERROR(TEXT("Timeout waiting for network to be ready"));
                return SOCKET_ERROR_TIMEOUT;
            }

            DoSystemCall(SYSCALL_Sleep, SYSCALL_PARAM(1000));
        }

        // Create TCP connection
        Socket->TCPConnection = TCP_CreateConnection(
            NetworkDevice,
            Socket->LocalAddress.Address,
            Socket->LocalAddress.Port,
            RemoteAddress.Address,
            RemoteAddress.Port);

        if (Socket->TCPConnection == NULL) {
            ERROR(TEXT("Failed to create TCP connection"));
            return SOCKET_ERROR_INVALID;
        }

        // Register for TCP connection events
        if (TCP_RegisterCallback(Socket->TCPConnection, NOTIF_EVENT_TCP_CONNECTED, SocketTCPNotificationCallback, Socket) != 0) {
            ERROR(TEXT("Failed to register TCP notification"));
        } else {
        }

        // Initiate TCP connection
        if (TCP_Connect(Socket->TCPConnection) != 0) {
            ERROR(TEXT("Failed to initiate TCP connection"));
            TCP_DestroyConnection(Socket->TCPConnection);
            Socket->TCPConnection = NULL;
            // Reset socket state to allow retry
            Socket->State = SOCKET_STATE_BOUND;
            return SOCKET_ERROR_CONNREFUSED;
        }

        Socket->State = SOCKET_STATE_CONNECTING;


        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Send data on a connected socket
 *
 * This function sends data on a connected socket. For TCP sockets,
 * the data is sent reliably and in order.
 *
 * @param SocketHandle The socket descriptor to send data on
 * @param Buffer Pointer to the data to send
 * @param Length Number of bytes to send
 * @param Flags Send flags (currently unused)
 * @return Number of bytes sent on success, or negative error code on failure
 */
I32 SocketSend(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags) {
    UNUSED(Flags);
    if (!Buffer || Length == 0) {
        ERROR(TEXT("Invalid buffer or length"));
        return SOCKET_ERROR_INVALID;
    }

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED) {
            ERROR(TEXT("Socket %p not connected"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            // Send via TCP
            I32 Result = TCP_Send(Socket->TCPConnection, (const U8*)Buffer, Length);
            if (Result > 0) {
                Socket->BytesSent += Result;
                Socket->PacketsSent++;
            }
            return Result;
        } else {
            ERROR(TEXT("Unsupported socket type for send"));
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Emit a rate-limited receive diagnostic for a socket.
 * @param Socket Socket instance.
 * @param Reason Diagnostic reason.
 * @param ErrorCode Socket error code.
 */
static void SocketReceiveLogRateLimited(LPSOCKET Socket, LPCSTR Reason, I32 ErrorCode) {
    U32 Suppressed = 0;

    if (Socket == NULL || Reason == NULL) {
        return;
    }

    if (!RateLimiterShouldTrigger(&Socket->ReceiveLogLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    WARNING(TEXT("%s socket=%p state=%u error=%x suppressed=%u"),
            Reason,
            (LPVOID)Socket,
            Socket->State,
            (U32)ErrorCode,
            Suppressed);
}

/************************************************************************/

/**
 * @brief Receive data from a connected socket
 *
 * This function receives data from a connected socket. For TCP sockets,
 * data is received from the internal buffer.
 *
 * @param SocketHandle The socket descriptor to receive data from
 * @param Buffer Pointer to the buffer to store received data
 * @param Length Maximum number of bytes to receive
 * @param Flags Receive flags (currently unused)
 * @return Number of bytes received on success, or negative error code on failure
 */
I32 SocketReceive(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags) {
    UNUSED(Flags);
    if (!Buffer || Length == 0) {
        ERROR(TEXT("Invalid buffer or length"));
        return SOCKET_ERROR_INVALID;
    }

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED && Socket->State != SOCKET_STATE_CLOSED) {
            ERROR(TEXT("Socket %p not connected (state=%u)"), (LPVOID)SocketHandle, Socket->State);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            if (Socket->ReceiveOverflow || Socket->ReceiveBuffer.Overflowed) {
                if (Socket->ReceiveOverflow) {
                    WARNING(TEXT("Receive buffer overflow detected on socket %p"), (LPVOID)SocketHandle);
                    Socket->ReceiveOverflow = FALSE;
                }
                return SOCKET_ERROR_OVERFLOW;
            }

            // Check receive buffer first
            U32 AvailableData = CircularBuffer_GetAvailableData(&Socket->ReceiveBuffer);
            if (AvailableData > 0) {
                U32 BytesToCopy = CircularBuffer_Read(&Socket->ReceiveBuffer, (U8*)Buffer, Length);

                Socket->BytesReceived += BytesToCopy;
                Socket->ReceiveTimeoutStartTime = 0; // Reset timeout so user space can continue waiting after new data arrives

                if (Socket->TCPConnection != NULL && BytesToCopy > 0) {
                    TCP_HandleApplicationRead(Socket->TCPConnection, BytesToCopy);
                }

                return BytesToCopy;
            } else {
                // No data available - check timeout
                if (Socket->ReceiveTimeout > 0) {
                    UINT CurrentTime = GetSystemTime();

                    // Initialize timeout start time on first call
                    if (Socket->ReceiveTimeoutStartTime == 0) {
                        Socket->ReceiveTimeoutStartTime = CurrentTime;
                    }

                    // Check if timeout exceeded
                    if ((CurrentTime - Socket->ReceiveTimeoutStartTime) >= Socket->ReceiveTimeout) {
                        Socket->ReceiveTimeoutStartTime = 0; // Reset for next operation
                        SocketReceiveLogRateLimited(Socket, TEXT("receive time out"), SOCKET_ERROR_TIMEOUT);
                        return SOCKET_ERROR_TIMEOUT;
                    }
                }

                // No data available - check if connection is closed
                if (Socket->State == SOCKET_STATE_CLOSED) {
                    return 0; // EOF
                }

                // No data available, would block
                return SOCKET_ERROR_WOULDBLOCK;
            }
        } else {
            ERROR(TEXT("Unsupported socket type for receive"));
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Send data to a specific address (UDP)
 *
 * This function sends data to a specific destination address without
 * establishing a connection. Currently not implemented.
 *
 * @param SocketHandle The socket descriptor to send data on
 * @param Buffer Pointer to the data to send
 * @param Length Number of bytes to send
 * @param Flags Send flags (currently unused)
 * @param DestinationAddress Destination address to send to
 * @param AddressLength Size of the destination address structure
 * @return Number of bytes sent on success, or negative error code on failure
 */
I32 SocketSendTo(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags,
                 LPSOCKET_ADDRESS DestinationAddress, U32 AddressLength) {
    UNUSED(Flags);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;
    SOCKET_ADDRESS_INET DestinationInetAddress;
    SOCKET_ADDRESS_INET LocalInetAddress;
    LPDEVICE NetworkDevice;
    const U8* PayloadData = (const U8*)Buffer;
    U8 EmptyPayload = 0;
    U16 SourcePort;
    U16 DestinationPort;
    int SendResult;

    if ((Buffer == NULL && Length > 0) || DestinationAddress == NULL || AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
        ERROR(TEXT("Invalid parameters"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->SocketType != SOCKET_TYPE_DGRAM) {
            ERROR(TEXT("Socket %p is not datagram"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        if (SocketAddressGenericToInet(DestinationAddress, &DestinationInetAddress) != SOCKET_ERROR_NONE) {
            ERROR(TEXT("Invalid destination address"));
            return SOCKET_ERROR_INVALID;
        }

        if (DestinationInetAddress.Port == 0) {
            ERROR(TEXT("Destination port is zero"));
            return SOCKET_ERROR_INVALID;
        }

        if (Socket->State == SOCKET_STATE_CREATED) {
            SocketAddressInetMake(0, 0, &LocalInetAddress);
            if (SocketBind(SocketHandle, (LPSOCKET_ADDRESS)&LocalInetAddress, sizeof(SOCKET_ADDRESS_INET)) != SOCKET_ERROR_NONE) {
                ERROR(TEXT("Failed to auto-bind UDP socket"));
                return SOCKET_ERROR_INVALID;
            }
        }

        if (Socket->State < SOCKET_STATE_BOUND) {
            ERROR(TEXT("UDP socket %p not bound"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        NetworkDevice = (LPDEVICE)NetworkManager_GetPrimaryDevice();
        if (NetworkDevice == NULL) {
            ERROR(TEXT("No network device available"));
            return SOCKET_ERROR_INVALID;
        }

        if (!NetworkManager_IsDeviceReady(NetworkDevice)) {
            return SOCKET_ERROR_WOULDBLOCK;
        }

        if (Length == 0) {
            PayloadData = &EmptyPayload;
        }

        SourcePort = Ntohs(Socket->LocalAddress.Port);
        DestinationPort = Ntohs(DestinationInetAddress.Port);

        SendResult = UDP_Send(NetworkDevice, DestinationInetAddress.Address, SourcePort, DestinationPort, PayloadData, Length);
        if (SendResult == 0) {
            ERROR(TEXT("UDP_Send failed"));
            return SOCKET_ERROR_INVALID;
        }

        Socket->BytesSent += Length;
        Socket->PacketsSent++;
        MemoryCopy(&Socket->RemoteAddress, &DestinationInetAddress, sizeof(SOCKET_ADDRESS_INET));

        return (I32)Length;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Receive data from any address (UDP)
 *
 * This function receives data from any source address without requiring
 * an established connection. Currently not implemented.
 *
 * @param SocketHandle The socket descriptor to receive data from
 * @param Buffer Pointer to the buffer to store received data
 * @param Length Maximum number of bytes to receive
 * @param Flags Receive flags (currently unused)
 * @param SourceAddress Pointer to store the source address of received data
 * @param AddressLength Pointer to the size of the source address buffer
 * @return Number of bytes received on success, or negative error code on failure
 */
I32 SocketReceiveFrom(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags,
                      LPSOCKET_ADDRESS SourceAddress, U32* AddressLength) {
    UNUSED(Flags);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;
    SOCKET_UDP_DATAGRAM_HEADER DatagramHeader;
    U32 AvailableData;
    U32 SourceAddressLengthRequired = sizeof(SOCKET_ADDRESS_INET);
    U32 BytesToCopy;
    U32 RemainingBytes;
    U8 DiscardBuffer[64];

    if (Buffer == NULL || Length == 0) {
        ERROR(TEXT("Invalid buffer or length"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->SocketType != SOCKET_TYPE_DGRAM) {
            ERROR(TEXT("Socket %p is not datagram"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        if (Socket->State < SOCKET_STATE_BOUND || Socket->State == SOCKET_STATE_CLOSED) {
            ERROR(TEXT("UDP socket %p not bound"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        if (Socket->ReceiveOverflow || Socket->ReceiveBuffer.Overflowed) {
            if (Socket->ReceiveOverflow) {
                WARNING(TEXT("Receive buffer overflow detected on socket %p"), (LPVOID)SocketHandle);
                Socket->ReceiveOverflow = FALSE;
            }
            return SOCKET_ERROR_OVERFLOW;
        }

        AvailableData = CircularBuffer_GetAvailableData(&Socket->ReceiveBuffer);
        if (AvailableData < sizeof(SOCKET_UDP_DATAGRAM_HEADER)) {
            if (AvailableData > 0) {
                ERROR(TEXT("Invalid UDP datagram queue state, resetting buffer"));
                CircularBuffer_Reset(&Socket->ReceiveBuffer);
                return SOCKET_ERROR_INVALID;
            }

            if (Socket->ReceiveTimeout > 0) {
                UINT CurrentTime = GetSystemTime();
                if (Socket->ReceiveTimeoutStartTime == 0) {
                    Socket->ReceiveTimeoutStartTime = CurrentTime;
                }

                if ((CurrentTime - Socket->ReceiveTimeoutStartTime) >= Socket->ReceiveTimeout) {
                    Socket->ReceiveTimeoutStartTime = 0;
                    return SOCKET_ERROR_TIMEOUT;
                }
            }

            return SOCKET_ERROR_WOULDBLOCK;
        }

        if (CircularBuffer_Read(&Socket->ReceiveBuffer, (U8*)&DatagramHeader, sizeof(SOCKET_UDP_DATAGRAM_HEADER)) !=
            sizeof(SOCKET_UDP_DATAGRAM_HEADER)) {
            ERROR(TEXT("Failed to read UDP datagram header"));
            CircularBuffer_Reset(&Socket->ReceiveBuffer);
            return SOCKET_ERROR_INVALID;
        }

        AvailableData = CircularBuffer_GetAvailableData(&Socket->ReceiveBuffer);
        if (DatagramHeader.PayloadLength > AvailableData) {
            ERROR(TEXT("Corrupted UDP datagram queue (%u > %u)"),
                  DatagramHeader.PayloadLength, AvailableData);
            CircularBuffer_Reset(&Socket->ReceiveBuffer);
            return SOCKET_ERROR_INVALID;
        }

        if (AddressLength != NULL) {
            if (SourceAddress == NULL || *AddressLength < SourceAddressLengthRequired) {
                *AddressLength = SourceAddressLengthRequired;
            } else {
                SocketAddressInetToGeneric(&DatagramHeader.SourceAddress, SourceAddress);
                *AddressLength = SourceAddressLengthRequired;
            }
        }

        BytesToCopy = (Length < DatagramHeader.PayloadLength) ? Length : DatagramHeader.PayloadLength;
        if (BytesToCopy > 0) {
            CircularBuffer_Read(&Socket->ReceiveBuffer, (U8*)Buffer, BytesToCopy);
        }

        RemainingBytes = DatagramHeader.PayloadLength - BytesToCopy;
        while (RemainingBytes > 0) {
            U32 ChunkLength = (RemainingBytes > sizeof(DiscardBuffer)) ? sizeof(DiscardBuffer) : RemainingBytes;
            CircularBuffer_Read(&Socket->ReceiveBuffer, DiscardBuffer, ChunkLength);
            RemainingBytes -= ChunkLength;
        }

        Socket->BytesReceived += BytesToCopy;
        Socket->ReceiveTimeoutStartTime = 0;

        if (BytesToCopy < DatagramHeader.PayloadLength) {
            WARNING(TEXT("Datagram truncated on socket %p (%u/%u bytes)"),
                    (LPVOID)SocketHandle, BytesToCopy, DatagramHeader.PayloadLength);
        }

        return (I32)BytesToCopy;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief TCP receive callback function
 *
 * This function is called by the TCP layer when data is received on a
 * TCP connection. It buffers the data in the appropriate socket's receive buffer.
 *
 * @param TCPConnection The TCP connection ID that received data
 * @param Data Pointer to the received data
 * @param DataLength Number of bytes received
 */
void SocketTCPNotificationCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData) {
    LPSOCKET Socket = (LPSOCKET)UserData;

    if (!Socket || !NotificationData) return;


    if (NotificationData->EventID == NOTIF_EVENT_TCP_CONNECTED) {
        Socket->State = SOCKET_STATE_CONNECTED;
    }
}

U32 SocketTCPReceiveCallback(LPTCP_CONNECTION TCPConnection, const U8* Data, U32 DataLength) {
    if (!Data || DataLength == 0) {
        return 0;
    }

    LPLIST SocketList = GetSocketList();
    LPSOCKET Socket = (LPSOCKET)(SocketList != NULL ? SocketList->First : NULL);
    while (Socket) {
        SAFE_USE(Socket) {
            if (Socket->TCPConnection == TCPConnection) {
                // Copy data to receive buffer using CircularBuffer
                U32 BytesToCopy = CircularBuffer_Write(&Socket->ReceiveBuffer, Data, DataLength);

                if (BytesToCopy > 0) {
                    Socket->PacketsReceived++;
                }

                if (BytesToCopy < DataLength) {
                    Socket->ReceiveOverflow = TRUE;
                    WARNING(TEXT("Receive buffer overflow for socket %p (%u/%u bytes stored, size=%u, max=%u)"),
                            Socket,
                            BytesToCopy,
                            DataLength,
                            Socket->ReceiveBuffer.Size,
                            Socket->ReceiveBuffer.MaximumSize);
                }

                // NOTE: TCP window is now calculated automatically based on TCP buffer usage

                return BytesToCopy;
            }
            Socket = (LPSOCKET)Socket->Next;
        } else {
            return 0;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Get socket option
 *
 * This function retrieves the value of a socket option.
 * Currently not implemented.
 *
 * @param SocketHandle The socket descriptor
 * @param Level The protocol level (SOL_SOCKET, IPPROTO_TCP, etc.)
 * @param OptionName The option name to retrieve
 * @param OptionValue Pointer to store the option value
 * @param OptionLength Pointer to the size of the option value buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPVOID OptionValue, U32* OptionLength) {
    UNUSED(Level);
    UNUSED(OptionName);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!OptionValue || !OptionLength) {
        ERROR(TEXT("Invalid option value or length pointers"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        // TODO: Implement socket options
        ERROR(TEXT("SocketGetOption not implemented yet"));
        return SOCKET_ERROR_INVALID;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Set socket option
 *
 * This function sets the value of a socket option.
 * Currently not implemented.
 *
 * @param SocketHandle The socket descriptor
 * @param Level The protocol level (SOL_SOCKET, IPPROTO_TCP, etc.)
 * @param OptionName The option name to set
 * @param OptionValue Pointer to the option value to set
 * @param OptionLength Size of the option value
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketSetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPCVOID OptionValue, U32 OptionLength) {
    UNUSED(Level);
    UNUSED(OptionName);
    UNUSED(OptionLength);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!OptionValue) {
        ERROR(TEXT("Invalid option value pointer"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Level == SOL_SOCKET) {
            switch (OptionName) {
                case SO_RCVTIMEO: {
                    if (OptionLength != sizeof(U32)) {
                        ERROR(TEXT("Invalid option length for SO_RCVTIMEO"));
                        return SOCKET_ERROR_INVALID;
                    }
                    U32 timeoutMs = *(const U32*)OptionValue;
                    Socket->ReceiveTimeout = timeoutMs;
                    return SOCKET_ERROR_NONE;
                }
                default:
                    ERROR(TEXT("Unsupported socket option %u"), OptionName);
                    return SOCKET_ERROR_INVALID;
            }
        } else {
            ERROR(TEXT("Unsupported option level %u"), Level);
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Get the remote address of a connected socket
 *
 * This function retrieves the remote address of a connected socket.
 * The socket must be in connected state.
 *
 * @param SocketHandle The socket descriptor
 * @param Address Pointer to store the remote address
 * @param AddressLength Pointer to the size of the address buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetPeerName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || !AddressLength) {
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED) {
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (*AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
            return SOCKET_ERROR_INVALID;
        }

        SocketAddressInetToGeneric(&Socket->RemoteAddress, Address);
        *AddressLength = sizeof(SOCKET_ADDRESS_INET);

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Get the local address of a socket
 *
 * This function retrieves the local address that a socket is bound to.
 * The socket must be in bound state or higher.
 *
 * @param SocketHandle The socket descriptor
 * @param Address Pointer to store the local address
 * @param AddressLength Pointer to the size of the address buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetSocketName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || !AddressLength) {
        ERROR(TEXT("Invalid address or length pointers"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, KOID_SOCKET) {
        if (Socket->State < SOCKET_STATE_BOUND) {
            ERROR(TEXT("Socket %p not bound"), (LPVOID)SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        if (*AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
            ERROR(TEXT("Address length too small"));
            return SOCKET_ERROR_INVALID;
        }

        SocketAddressInetToGeneric(&Socket->LocalAddress, Address);
        *AddressLength = sizeof(SOCKET_ADDRESS_INET);

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}
