
/************************************************************************\

    EXOS Runtime HTTP Client
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    HTTP Client Implementation

\************************************************************************/

#include "../include/exos.h"
#include "../include/http.h"
#include "../../kernel/include/User.h"

static unsigned int HTTPDefaultReceiveTimeoutMs = 10000; // 10 seconds by default
static char HTTP_LastErrorMessage[128] = "Success";

/***************************************************************************/
#define HTTP_RECEIVE_BUFFER_DEFAULT_SIZE 16384
#define HTTP_DOWNLOAD_BUFFER_MAXIMUM_SIZE 65536

static unsigned int HTTP_SelectDownloadBufferSize(unsigned int ContentLength) {
    unsigned int Size = HTTP_RECEIVE_BUFFER_DEFAULT_SIZE;

    if (ContentLength > 0) {
        Size = ContentLength;
        if (Size > HTTP_DOWNLOAD_BUFFER_MAXIMUM_SIZE) {
            Size = HTTP_DOWNLOAD_BUFFER_MAXIMUM_SIZE;
        }
    }

    return Size;
}

/***************************************************************************/
static void HTTP_SetLastErrorMessage(const char* Message) {
    unsigned int Index = 0;

    if (!Message) {
        HTTP_LastErrorMessage[0] = '\0';
        return;
    }

    while (Message[Index] != '\0' && Index < (sizeof(HTTP_LastErrorMessage) - 1U)) {
        HTTP_LastErrorMessage[Index] = Message[Index];
        Index++;
    }

    HTTP_LastErrorMessage[Index] = '\0';
}

/***************************************************************************/
const char* HTTP_GetLastErrorMessage(void) {
    return HTTP_LastErrorMessage;
}

/***************************************************************************/
typedef enum {
    HTTP_CHUNK_STATE_READ_SIZE = 0,
    HTTP_CHUNK_STATE_READ_DATA,
    HTTP_CHUNK_STATE_READ_DATA_CRLF,
    HTTP_CHUNK_STATE_READ_TRAILERS,
    HTTP_CHUNK_STATE_FINISHED
} HTTP_CHUNK_STATE;

typedef struct tag_HTTP_CHUNK_PARSER {
    HTTP_CHUNK_STATE State;
    unsigned int CurrentChunkSize;
    unsigned int BytesRemainingInChunk;
    unsigned int TotalBytesWritten;
    char SizeBuffer[32];
    unsigned int SizeBufferUsed;
    unsigned int CrLfBytesNeeded;
    int PendingCR;
    int TrailerLineHasData;
} HTTP_CHUNK_PARSER;

/***************************************************************************/
static void HTTP_ChunkParserInit(HTTP_CHUNK_PARSER* Parser) {
    if (!Parser) {
        return;
    }

    Parser->State = HTTP_CHUNK_STATE_READ_SIZE;
    Parser->CurrentChunkSize = 0;
    Parser->BytesRemainingInChunk = 0;
    Parser->TotalBytesWritten = 0;
    Parser->SizeBufferUsed = 0;
    Parser->CrLfBytesNeeded = 0;
    Parser->PendingCR = 0;
    Parser->TrailerLineHasData = 0;
}

/***************************************************************************/
static unsigned int HTTP_ParseChunkSizeValue(const char* Value) {
    unsigned int Result = 0;
    const char* Current = Value;

    while (*Current != '\0') {
        char Character = *Current;
        unsigned int Digit;

        if (Character >= '0' && Character <= '9') {
            Digit = (unsigned int)(Character - '0');
        } else if (Character >= 'a' && Character <= 'f') {
            Digit = (unsigned int)(Character - 'a' + 10U);
        } else if (Character >= 'A' && Character <= 'F') {
            Digit = (unsigned int)(Character - 'A' + 10U);
        } else {
            break;
        }

        Result = (Result << 4) | Digit;
        Current++;
    }

    return Result;
}

/***************************************************************************/
static int HTTP_WriteBodyData(FILE* File, const unsigned char* Data, unsigned int Length) {
    size_t Written;

    if (!File || !Data || Length == 0) {
        return HTTP_SUCCESS;
    }

    Written = fwrite(Data, 1, Length, File);
    if (Written != Length) {
        return HTTP_ERROR_MEMORY_ERROR;
    }

    return HTTP_SUCCESS;
}

/***************************************************************************/
static int HTTP_ChunkParserProcess(HTTP_CHUNK_PARSER* Parser, const unsigned char* Data,
                                   unsigned int Length, FILE* File, unsigned int* BytesWritten) {
    unsigned int Offset = 0;
    unsigned int WrittenThisCall = 0;

    if (!Parser || !Data || Length == 0 || !File) {
        if (BytesWritten) {
            *BytesWritten = 0;
        }
        return HTTP_SUCCESS;
    }

    while (Offset < Length && Parser->State != HTTP_CHUNK_STATE_FINISHED) {
        switch (Parser->State) {
            case HTTP_CHUNK_STATE_READ_SIZE: {
                unsigned char Character = Data[Offset++];

                if (Character == '\n') {
                    if (Parser->SizeBufferUsed > 0 &&
                        Parser->SizeBuffer[Parser->SizeBufferUsed - 1] == '\r') {
                        Parser->SizeBufferUsed--;
                    }

                    Parser->SizeBuffer[Parser->SizeBufferUsed] = '\0';

                    if (Parser->SizeBufferUsed == 0) {
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }

                    char* Semicolon = strchr(Parser->SizeBuffer, ';');
                    if (Semicolon) {
                        *Semicolon = '\0';
                    }

                    Parser->CurrentChunkSize = HTTP_ParseChunkSizeValue(Parser->SizeBuffer);
                    Parser->BytesRemainingInChunk = Parser->CurrentChunkSize;
                    Parser->SizeBufferUsed = 0;

                    if (Parser->CurrentChunkSize == 0U) {
                        Parser->State = HTTP_CHUNK_STATE_READ_TRAILERS;
                        Parser->PendingCR = 0;
                        Parser->TrailerLineHasData = 0;
                    } else {
                        Parser->State = HTTP_CHUNK_STATE_READ_DATA;
                    }
                } else {
                    if (Parser->SizeBufferUsed >= sizeof(Parser->SizeBuffer) - 1U) {
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }
                    Parser->SizeBuffer[Parser->SizeBufferUsed++] = (char)Character;
                }
                break;
            }

            case HTTP_CHUNK_STATE_READ_DATA: {
                unsigned int Available = Length - Offset;
                unsigned int ToWrite = Parser->BytesRemainingInChunk;

                if (ToWrite > Available) {
                    ToWrite = Available;
                }

                if (ToWrite > 0U) {
                    int WriteResult = HTTP_WriteBodyData(File, Data + Offset, ToWrite);
                    if (WriteResult != HTTP_SUCCESS) {
                        return WriteResult;
                    }

                    Offset += ToWrite;
                    Parser->BytesRemainingInChunk -= ToWrite;
                    Parser->TotalBytesWritten += ToWrite;
                    WrittenThisCall += ToWrite;
                }

                if (Parser->BytesRemainingInChunk == 0U) {
                    Parser->State = HTTP_CHUNK_STATE_READ_DATA_CRLF;
                    Parser->CrLfBytesNeeded = 2U;
                }
                break;
            }

            case HTTP_CHUNK_STATE_READ_DATA_CRLF: {
                unsigned char Character = Data[Offset++];

                if ((Parser->CrLfBytesNeeded == 2U && Character != '\r') ||
                    (Parser->CrLfBytesNeeded == 1U && Character != '\n')) {
                    return HTTP_ERROR_PROTOCOL_ERROR;
                }

                Parser->CrLfBytesNeeded--;
                if (Parser->CrLfBytesNeeded == 0U) {
                    Parser->State = HTTP_CHUNK_STATE_READ_SIZE;
                }
                break;
            }

            case HTTP_CHUNK_STATE_READ_TRAILERS: {
                unsigned char Character = Data[Offset++];

                if (Parser->PendingCR) {
                    if (Character != '\n') {
                        return HTTP_ERROR_PROTOCOL_ERROR;
                    }

                    Parser->PendingCR = 0;
                    if (Parser->TrailerLineHasData == 0) {
                        Parser->State = HTTP_CHUNK_STATE_FINISHED;
                    } else {
                        Parser->TrailerLineHasData = 0;
                    }
                    break;
                }

                if (Character == '\r') {
                    Parser->PendingCR = 1;
                } else {
                    Parser->TrailerLineHasData = 1;
                }
                break;
            }

            case HTTP_CHUNK_STATE_FINISHED:
            default:
                break;
        }
    }

    if (BytesWritten) {
        *BytesWritten = WrittenThisCall;
    }

    return HTTP_SUCCESS;
}

/***************************************************************************/
static char HTTP_ToLowerChar(char Character) {
    if (Character >= 'A' && Character <= 'Z') {
        return (char)(Character + ('a' - 'A'));
    }
    return Character;
}

/***************************************************************************/
static int HTTP_HeaderValueContainsToken(const char* Value, const char* Token) {
    unsigned int TokenLength;
    const char* Current;

    if (!Value || !Token) {
        return 0;
    }

    TokenLength = (unsigned int)strlen(Token);
    if (TokenLength == 0U) {
        return 0;
    }

    Current = Value;
    while (*Current) {
        while (*Current == ' ' || *Current == '\t' || *Current == ',') {
            Current++;
        }

        if (*Current == '\0') {
            break;
        }

        unsigned int Matched = 0;
        const char* Segment = Current;
        while (Segment[Matched] && Segment[Matched] != ',' && Segment[Matched] != ';' &&
               Matched < TokenLength &&
               HTTP_ToLowerChar(Segment[Matched]) == HTTP_ToLowerChar(Token[Matched])) {
            Matched++;
        }

        if (Matched == TokenLength) {
            char Terminator = Segment[Matched];
            if (Terminator == '\0' || Terminator == ',' || Terminator == ';' || Terminator == ' ' ||
                Terminator == '\t') {
                return 1;
            }
        }

        while (*Current && *Current != ',') {
            Current++;
        }

        if (*Current == ',') {
            Current++;
        }
    }

    return 0;
}

/***************************************************************************/

void HTTP_SetDefaultReceiveTimeout(unsigned int TimeoutMs) {
    HTTPDefaultReceiveTimeoutMs = TimeoutMs;
}

/***************************************************************************/

unsigned int HTTP_GetDefaultReceiveTimeout(void) {
    return HTTPDefaultReceiveTimeoutMs;
}

/***************************************************************************/

/**
 * @brief Parse a URL string into components
 * @param URLString The URL string to parse
 * @param ParsedURL Output structure to store parsed components
 * @return 1 if parsing was successful, 0 otherwise
 */
int HTTP_ParseURL(const char* URLString, URL* ParsedURL) {
    const char* current;
    const char* schemeEnd;
    const char* hostStart;
    const char* hostEnd;
    const char* pathStart;
    const char* queryStart;
    unsigned int portValue;

    if (!URLString || !ParsedURL) {
        HTTP_SetLastErrorMessage("URL parser received invalid parameters");
        return 0;
    }

    // Initialize the structure
    ParsedURL->Scheme[0] = '\0';
    ParsedURL->Host[0] = '\0';
    ParsedURL->Path[0] = '\0';
    ParsedURL->Query[0] = '\0';
    ParsedURL->Port = 0;
    ParsedURL->Valid = 0;

    current = URLString;

    // Find scheme (e.g., "http://")
    schemeEnd = strstr(current, "://");
    if (!schemeEnd) {
        HTTP_SetLastErrorMessage("URL missing scheme separator");
        return 0;
    }

    // Extract scheme
    if ((unsigned long)(schemeEnd - current) >= (unsigned long)sizeof(ParsedURL->Scheme)) {
        HTTP_SetLastErrorMessage("URL scheme is too long");
        return 0;
    }

    memcpy(ParsedURL->Scheme, current, schemeEnd - current);
    ParsedURL->Scheme[schemeEnd - current] = '\0';

    // Move past "://"
    hostStart = schemeEnd + 3;

    // Find end of host (could be ':', '/', '?', or end of string)
    hostEnd = hostStart;
    while (*hostEnd && *hostEnd != ':' && *hostEnd != '/' && *hostEnd != '?') {
        hostEnd++;
    }

    // Extract host
    if ((unsigned long)(hostEnd - hostStart) >= (unsigned long)sizeof(ParsedURL->Host)) {
        HTTP_SetLastErrorMessage("URL host component is too long");
        return 0;
    }
    if (hostEnd == hostStart) {
        HTTP_SetLastErrorMessage("URL host component is empty");
        return 0;
    }
    memcpy(ParsedURL->Host, hostStart, hostEnd - hostStart);
    ParsedURL->Host[hostEnd - hostStart] = '\0';

    current = hostEnd;

    // Check for port
    if (*current == ':') {
        current++; // Skip ':'
        portValue = 0;
        while (*current >= '0' && *current <= '9' && *current != '/' && *current != '?') {
            portValue = portValue * 10 + (*current - '0');
            if (portValue > 65535) {
                HTTP_SetLastErrorMessage("URL port value exceeds 65535");
                return 0;
            }
            current++;
        }
        ParsedURL->Port = (unsigned short)portValue;
    }

    // Find path start
    pathStart = current;
    if (*pathStart != '/') {
        // Default path if none specified
        strcpy(ParsedURL->Path, "/");
    } else {
        // Find query string
        queryStart = strstr(pathStart, "?");
        if (queryStart) {
            UINT PathLength = (UINT)(queryStart - pathStart);

            // Extract path without query
            if (PathLength >= sizeof(ParsedURL->Path)) {
                HTTP_SetLastErrorMessage("URL path component is too long");
                return 0;
            }
            memcpy(ParsedURL->Path, pathStart, PathLength);
            ParsedURL->Path[PathLength] = '\0';

            // Extract query string
            queryStart++; // Skip '?'
            if (strlen(queryStart) < sizeof(ParsedURL->Query)) {
                strcpy(ParsedURL->Query, queryStart);
            }
        } else {
            // No query string, just path
            if (strlen(pathStart) < sizeof(ParsedURL->Path)) {
                strcpy(ParsedURL->Path, pathStart);
            } else {
                HTTP_SetLastErrorMessage("URL path component exceeds buffer");
                return 0;
            }
        }
    }

    // Set default port if not specified
    if (ParsedURL->Port == 0) {
        if (strcmp(ParsedURL->Scheme, "http") == 0) {
            ParsedURL->Port = 80;
        } else {
            HTTP_SetLastErrorMessage("Unsupported URL scheme");
            return 0; // Only HTTP supported
        }
    }

    // Validate scheme (currently only support HTTP)
    if (strcmp(ParsedURL->Scheme, "http") != 0) {
        HTTP_SetLastErrorMessage("Only HTTP scheme is supported");
        return 0;
    }

    ParsedURL->Valid = 1;
    HTTP_SetLastErrorMessage("Success");
    return 1;
}

/***************************************************************************/

/**
 * @brief Create HTTP connection to host
 * @param Host The hostname or IP address
 * @param Port The port number (usually 80 for HTTP)
 * @return Pointer to HTTP_CONNECTION or NULL on failure
 */
HTTP_CONNECTION* HTTP_CreateConnection(const char* Host, unsigned short Port) {
    HTTP_CONNECTION* connection;
    struct sockaddr_in serverAddr;
    int result;

    HTTP_SetLastErrorMessage("Success");

    if (!Host || Port == 0) {
        HTTP_SetLastErrorMessage("Connection parameters are invalid");
        return NULL;
    }

    // Allocate connection structure
    connection = (HTTP_CONNECTION*)malloc(sizeof(HTTP_CONNECTION));
    if (!connection) {
        HTTP_SetLastErrorMessage("Out of memory while creating connection");
        return NULL;
    }

    // Initialize connection
    memset(connection, 0, sizeof(HTTP_CONNECTION));
    connection->RemotePort = Port;
    connection->Connected = 0;
    connection->KeepAlive = 0;

    // Create socket
    connection->SocketHandle = socket(SOCKET_AF_INET, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
    if (connection->SocketHandle == 0) {
        free(connection);
        HTTP_SetLastErrorMessage("Failed to create TCP socket");
        return NULL;
    }

    // Parse IP address if it's in dotted decimal notation
    connection->RemoteIP = InternetAddressFromString((LPCSTR)Host);

    // Simple hardcoded test for "52.204.95.73"
    /*
    if (strcmp(Host, "52.204.95.73") == 0) {
        connection->RemoteIP = (52 << 24) | (204 << 16) | (95 << 8) | 73;
    } else if (strcmp(Host, "192.168.56.1") == 0) {
        connection->RemoteIP = (192 << 24) | (168 << 16) | (56 << 8) | 1;
    } else if (strcmp(Host, "10.0.2.2") == 0) {
        connection->RemoteIP = (10 << 24) | (0 << 16) | (2 << 8) | 2;
    } else {
        // For now, only support these specific IPs
        connection->RemoteIP = 0;
    }
    */

    if (connection->RemoteIP == 0) {
        // For now, we don't support hostname resolution
        free(connection);
        HTTP_SetLastErrorMessage("Failed to parse remote IP address");
        return NULL;
    }

    // Setup server address
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = SOCKET_AF_INET;
    serverAddr.sin_port = htons(Port);
    serverAddr.sin_addr = htonl(connection->RemoteIP);

    // Apply configured receive timeout (0 disables the timeout)
    unsigned int timeoutMs = HTTPDefaultReceiveTimeoutMs;
    if (timeoutMs > 0) {
        if (setsockopt(connection->SocketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeoutMs, sizeof(timeoutMs)) != 0) {
        }
    }

    // Connect to server
    result = connect(connection->SocketHandle, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    if (result != 0) {
        shutdown(connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
        free(connection);
        HTTP_SetLastErrorMessage("connect() failed to initiate handshake");
        return NULL;
    }

    // Wait for connection to be established using adaptive delay
    // In EXOS, connect() is non-blocking, so we need to wait for TCP handshake
    AdaptiveDelay_Initialize(&(connection->DelayState));

    // Override defaults for HTTP connection timeout
    connection->DelayState.MinDelay = 50;      // 50 ticks minimum
    connection->DelayState.MaxDelay = 2000;    // 2000 ticks maximum
    connection->DelayState.MaxAttempts = 10;   // 10 attempts max

    while (AdaptiveDelay_ShouldContinue(&(connection->DelayState))) {
        struct sockaddr_in peerAddr;
        socklen_t peerAddrLen = sizeof(peerAddr);

        // Try to get peer address - this succeeds only if connection is established
        if (getpeername(connection->SocketHandle, (struct sockaddr*)&peerAddr, &peerAddrLen) == 0) {
            connection->Connected = 1;
            AdaptiveDelay_OnSuccess(&(connection->DelayState));
            HTTP_SetLastErrorMessage("Success");
            return connection;
        }

        // Get next delay and sleep
        U32 delayTicks = AdaptiveDelay_GetNextDelay(&connection->DelayState);
        if (delayTicks > 0) {
            sleep(delayTicks);
            AdaptiveDelay_OnFailure(&(connection->DelayState));
        }
    }

    shutdown(connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
    free(connection);
    HTTP_SetLastErrorMessage("Timed out waiting for TCP handshake");
    return NULL;
}

/***************************************************************************/

/**
 * @brief Destroy HTTP connection
 * @param Connection The connection to destroy
 */
void HTTP_DestroyConnection(HTTP_CONNECTION* Connection) {
    if (!Connection) {
        return;
    }

    if (Connection->Connected && Connection->SocketHandle != 0) {
        shutdown(Connection->SocketHandle, SOCKET_SHUTDOWN_BOTH);
    }

    if (Connection->CurrentRequest) {
        if (Connection->CurrentRequest->Body) {
            free(Connection->CurrentRequest->Body);
        }
        free(Connection->CurrentRequest);
    }

    if (Connection->CurrentResponse) {
        HTTP_FreeResponse(Connection->CurrentResponse);
        free(Connection->CurrentResponse);
    }

    free(Connection);
}

/***************************************************************************/

int HTTP_SendRequest(HTTP_CONNECTION* Connection, const char* Method, const char* Path,
                           const unsigned char* Body, unsigned int BodyLength) {
    char request[2048];
    int requestLen;
    int sent;

    HTTP_SetLastErrorMessage("Success");

    if (!Connection || !Connection->Connected || !Method || !Path) {
        HTTP_SetLastErrorMessage("HTTP_SendRequest received invalid parameters");
        return HTTP_ERROR_INVALID_URL;
    }

    // Build HTTP request
    if (Body && BodyLength > 0) {
        requestLen = sprintf(request,
            "%s %s HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "User-Agent: EXOS/1.0\r\n"
            "Connection: close\r\n"
            "Content-Length: %u\r\n"
            "\r\n",
            Method, Path,
            (Connection->RemoteIP >> 24) & 0xFF,
            (Connection->RemoteIP >> 16) & 0xFF,
            (Connection->RemoteIP >> 8) & 0xFF,
            Connection->RemoteIP & 0xFF,
            Connection->RemotePort,
            BodyLength);
    } else {
        requestLen = sprintf(request,
            "%s %s HTTP/1.1\r\n"
            "Host: %d.%d.%d.%d:%d\r\n"
            "User-Agent: EXOS/1.0\r\n"
            "Connection: close\r\n"
            "\r\n",
            Method, Path,
            (Connection->RemoteIP >> 24) & 0xFF,
            (Connection->RemoteIP >> 16) & 0xFF,
            (Connection->RemoteIP >> 8) & 0xFF,
            Connection->RemoteIP & 0xFF,
            Connection->RemotePort);
    }

    // Send request headers
    sent = send(Connection->SocketHandle, request, requestLen, 0);
    if (sent != requestLen) {
        HTTP_SetLastErrorMessage("Failed to transmit HTTP request headers");
        return HTTP_ERROR_CONNECTION_FAILED;
    }

    // Send body if present
    if (Body && BodyLength > 0) {
        sent = send(Connection->SocketHandle, Body, BodyLength, 0);
        if (sent != (int)BodyLength) {
            HTTP_SetLastErrorMessage("Failed to transmit HTTP request body");
            return HTTP_ERROR_CONNECTION_FAILED;
        }
    }

    HTTP_SetLastErrorMessage("Success");
    return HTTP_SUCCESS;
}

/***************************************************************************/

int HTTP_ReceiveResponse(HTTP_CONNECTION* Connection, HTTP_RESPONSE* Response) {
    char buffer[HTTP_RECEIVE_BUFFER_DEFAULT_SIZE];
    int received;
    int totalReceived = 0;
    char* headerEnd;
    char* statusLine;
    int retryCount = 0;
    int timeoutCount = 0;
    const int maxRetries = 50; // Allow up to 50 attempts with small delays
    const int maxTimeoutsBeforeStateCheck = 3;

    char* contentLengthStr;
    unsigned int contentLength = 0;
    int headersParsed = 0;
    unsigned int savedHeaderLength = 0;

    // Dynamic buffer to accumulate all response data
    unsigned char* allData = NULL;
    unsigned int allDataSize = 0;
    unsigned int allDataCapacity = 4096;

    if (!Connection || !Response) {
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Initialize response
    memset(Response, 0, sizeof(HTTP_RESPONSE));
    Response->StatusCode = 0;
    Response->ContentLength = 0;
    Response->ChunkedEncoding = 0;

    // Allocate initial buffer for all data
    allData = (unsigned char*)malloc(allDataCapacity);
    if (!allData) {
        return HTTP_ERROR_MEMORY_ERROR;
    }

    // Reset receive buffer
    Connection->ReceiveBufferUsed = 0;

    // Receive response with retry logic
    FOREVER {  // Continue until we have complete response
        received = recv(Connection->SocketHandle, buffer, sizeof(buffer), 0);

        if (received >= 0) {
            if (received == 0) {
                break;
            }

            // Reset retry counters on successful receive
            retryCount = 0;
            timeoutCount = 0;

        } else if (received == SOCKET_ERROR_OVERFLOW) {
            free(allData);
            HTTP_SetLastErrorMessage("Socket receive buffer overflow detected");
            return HTTP_ERROR_SOCKET_OVERFLOW;
        } else if (received == SOCKET_ERROR_WOULDBLOCK) {
            retryCount++;
            if (retryCount >= maxRetries) {
                break;
            }

            sleep(1);
            continue;

        } else if (received == SOCKET_ERROR_TIMEOUT) {
            retryCount++;
            timeoutCount++;
            if (timeoutCount >= maxTimeoutsBeforeStateCheck) {
                struct sockaddr_in peerAddr;
                socklen_t peerAddrLen = sizeof(peerAddr);
                int peerStatus = getpeername(Connection->SocketHandle, (struct sockaddr*)&peerAddr, &peerAddrLen);

                if (peerStatus == 0) {
                    timeoutCount = 0;
                } else if (peerStatus == SOCKET_ERROR_NOTCONNECTED) {
                    received = 0;
                    break;
                } else {
                    break;
                }
            }

            if (retryCount >= maxRetries) {
                break;
            }

            sleep(1);
            continue;

        } else {
            break;
        }

        // Expand allData buffer if needed
        if (allDataSize + received > allDataCapacity) {
            allDataCapacity = allDataCapacity * 2;
            if (allDataSize + received > allDataCapacity) {
                allDataCapacity = allDataSize + received + 1024;
            }
            unsigned char* newBuffer = (unsigned char*)malloc(allDataCapacity);
            if (!newBuffer) {
                free(allData);
                return HTTP_ERROR_MEMORY_ERROR;
            }
            memcpy(newBuffer, allData, allDataSize);
            free(allData);
            allData = newBuffer;
        }

        // Copy received data to allData buffer
        memcpy(allData + allDataSize, buffer, received);
        allDataSize += received;
        totalReceived += received;

        // Copy to receive buffer (for header parsing only) up to buffer size
        if (Connection->ReceiveBufferUsed + received < sizeof(Connection->ReceiveBuffer)) {
            memcpy(Connection->ReceiveBuffer + Connection->ReceiveBufferUsed, buffer, received);
            Connection->ReceiveBufferUsed += received;
        }

        // Look for end of headers
        if (!headersParsed) {
            allData[allDataSize] = '\0';
            headerEnd = strstr((char*)allData, "\r\n\r\n");
            if (headerEnd) {
                headersParsed = 1;
                // Parse Content-Length from headers and save header length
                savedHeaderLength = (headerEnd + 4) - (char*)allData;
                char tempHeaders[4096];
                if (savedHeaderLength < sizeof(tempHeaders)) {
                    memcpy(tempHeaders, allData, savedHeaderLength);
                    tempHeaders[savedHeaderLength] = '\0';

                    contentLengthStr = strstr(tempHeaders, "Content-Length:");
                    if (contentLengthStr) {
                        const char* numStart = contentLengthStr + 16; // Skip "Content-Length: "
                        contentLength = 0;
                        while (*numStart >= '0' && *numStart <= '9') {
                            contentLength = contentLength * 10 + (*numStart - '0');
                            numStart++;
                        }
                    }
                }
            }
        }

    }

    if (totalReceived == 0) {
        free(allData);
        return HTTP_ERROR_CONNECTION_FAILED;
    }

    // Null-terminate allData for string operations
    allData[allDataSize] = '\0';

    // Parse status line
    statusLine = (char*)allData;
    if (strstr(statusLine, "HTTP/1.1") == statusLine) {
        strcpy(Response->Version, "HTTP/1.1");
        // Parse status code
        const char* codeStart = statusLine + 9; // Skip "HTTP/1.1 "
        unsigned int code = 0;
        while (*codeStart >= '0' && *codeStart <= '9') {
            code = code * 10 + (*codeStart - '0');
            codeStart++;
        }
        if (code <= 65535) {
            Response->StatusCode = (unsigned short)code;
        } else {
            free(allData);
            return HTTP_ERROR_INVALID_RESPONSE;
        }
    } else if (strstr(statusLine, "HTTP/1.0") == statusLine) {
        strcpy(Response->Version, "HTTP/1.0");
        // Parse status code
        const char* codeStart = statusLine + 9; // Skip "HTTP/1.0 "
        unsigned int code = 0;
        while (*codeStart >= '0' && *codeStart <= '9') {
            code = code * 10 + (*codeStart - '0');
            codeStart++;
        }
        if (code <= 65535) {
            Response->StatusCode = (unsigned short)code;
        } else {
            free(allData);
            return HTTP_ERROR_INVALID_RESPONSE;
        }
    } else {
        free(allData);
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Find headers end in allData
    headerEnd = strstr((char*)allData, "\r\n\r\n");
    if (!headerEnd) {
        free(allData);
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    // Copy headers
    unsigned int headerLength = (headerEnd + 4) - (char*)allData;
    if (headerLength < sizeof(Response->Headers)) {
        memcpy(Response->Headers, allData, headerLength);
        Response->Headers[headerLength] = '\0';
    }

    // Check for Content-Length
    contentLengthStr = strstr(Response->Headers, "Content-Length:");
    if (contentLengthStr) {
        const char* numStart = contentLengthStr + 16; // Skip "Content-Length: "
        contentLength = 0;
        while (*numStart >= '0' && *numStart <= '9') {
            contentLength = contentLength * 10 + (*numStart - '0');
            numStart++;
        }
        Response->ContentLength = contentLength;
    }

    // Check for chunked encoding
    if (strstr(Response->Headers, "Transfer-Encoding: chunked")) {
        Response->ChunkedEncoding = 1;
    }

    // Extract body from allData
    unsigned char* bodyStart = (unsigned char*)(headerEnd + 4);
    unsigned int bodyLength = allDataSize - (bodyStart - allData);

    if (bodyLength > 0) {
        Response->Body = (unsigned char*)malloc(bodyLength + 1);
        if (Response->Body) {
            memcpy(Response->Body, bodyStart, bodyLength);
            Response->Body[bodyLength] = '\0';
            Response->BodyLength = bodyLength;
        } else {
        }
    } else {
    }

    free(allData);
    return HTTP_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Send HTTP GET request
 * @param Connection The HTTP connection
 * @param Path The request path
 * @param Response The response structure to fill
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_Get(HTTP_CONNECTION* Connection, const char* Path, HTTP_RESPONSE* Response) {
    int result;

    result = HTTP_SendRequest(Connection, "GET", Path, NULL, 0);
    if (result != HTTP_SUCCESS) {
        return result;
    }

    result = HTTP_ReceiveResponse(Connection, Response);
    return result;
}

/***************************************************************************/

/**
 * @brief Send HTTP POST request
 * @param Connection The HTTP connection
 * @param Path The request path
 * @param Body The request body data
 * @param BodyLength The length of the request body
 * @param Response The response structure to fill
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_Post(HTTP_CONNECTION* Connection, const char* Path, const unsigned char* Body,
              unsigned int BodyLength, HTTP_RESPONSE* Response) {
    int result;

    result = HTTP_SendRequest(Connection, "POST", Path, Body, BodyLength);
    if (result != HTTP_SUCCESS) {
        return result;
    }

    return HTTP_ReceiveResponse(Connection, Response);
}

/***************************************************************************/

int HTTP_DownloadToFile(HTTP_CONNECTION* Connection, const char* Filename,
                        HTTP_RESPONSE* ResponseMetadata, unsigned int* BytesWritten,
                        const HTTP_PROGRESS_CALLBACKS* ProgressCallbacks) {
    unsigned char* receiveBuffer = NULL;
    unsigned int receiveBufferSize = 0;
    int received;
    char* headerEnd;
    char* statusLine;
    FILE* file = NULL;
    int headersParsed = 0;
    unsigned int contentLength = 0;
    unsigned short statusCode = 0;
    char version[16] = {0};
    unsigned int bodyBytesReceived = 0;
    unsigned int headerLength = 0;
    int isChunked = 0;
    HTTP_CHUNK_PARSER chunkParser;
    unsigned int idleTimeMs = 0;
    unsigned int receiveTimeoutMs;
    const unsigned int pollIntervalMs = 10;
    char headerBuffer[4096];
    unsigned int headerBufferUsed = 0;
    int responseComplete = 0;
    int connectionClosed = 0;
    int result = HTTP_SUCCESS;
    HTTP_RESPONSE localMetadata;
    HTTP_RESPONSE* metadataOut;
    int statusCallbackInvoked = 0;
    int responseStarted = 0;

    if (BytesWritten) {
        *BytesWritten = 0;
    }

    if (ResponseMetadata) {
        memset(ResponseMetadata, 0, sizeof(HTTP_RESPONSE));
        metadataOut = ResponseMetadata;
    } else {
        memset(&localMetadata, 0, sizeof(HTTP_RESPONSE));
        metadataOut = &localMetadata;
    }

    if (!Connection || !Filename) {
        HTTP_SetLastErrorMessage("HTTP_DownloadToFile received invalid parameters");
        return HTTP_ERROR_INVALID_RESPONSE;
    }

    HTTP_SetLastErrorMessage("Success");

    receiveBufferSize = HTTP_SelectDownloadBufferSize(0);
    receiveBuffer = (unsigned char*)malloc(receiveBufferSize);
    if (!receiveBuffer) {
        HTTP_SetLastErrorMessage("Failed to allocate receive buffer");
        return HTTP_ERROR_MEMORY_ERROR;
    }
    receiveTimeoutMs = HTTP_GetDefaultReceiveTimeout();
    if (receiveTimeoutMs == 0U) {
        receiveTimeoutMs = 10000U;
    }

    headerBuffer[0] = '\0';
    HTTP_ChunkParserInit(&chunkParser);

    Connection->ReceiveBufferUsed = 0;

    while (!responseComplete) {
        unsigned int receiveLength = receiveBufferSize;

        if (!headersParsed) {
            unsigned int headerSpace = (sizeof(headerBuffer) - 1) - headerBufferUsed;
            if (headerSpace == 0) {
                result = HTTP_ERROR_INVALID_RESPONSE;
                HTTP_SetLastErrorMessage("HTTP headers exceed internal buffer size");
                goto cleanup;
            }

            if (receiveLength > headerSpace) {
                receiveLength = headerSpace;
            }
        }

        received = recv(Connection->SocketHandle, receiveBuffer, receiveLength, 0);

        if (received < 0) {
            if (received == SOCKET_ERROR_OVERFLOW) {
                result = HTTP_ERROR_SOCKET_OVERFLOW;
                HTTP_SetLastErrorMessage("Socket receive buffer overflow detected");
                goto cleanup;
            }

            if (received == SOCKET_ERROR_WOULDBLOCK) {
                Sleep(pollIntervalMs);

                if (responseStarted) {
                    idleTimeMs += pollIntervalMs;

                    if (idleTimeMs >= receiveTimeoutMs) {
                        result = HTTP_ERROR_TIMEOUT;
                        HTTP_SetLastErrorMessage("Timed out waiting for HTTP response data");
                        goto cleanup;
                    }
                }

                continue;
            }

            if (received == SOCKET_ERROR_TIMEOUT) {
                if (responseStarted) {
                    result = HTTP_ERROR_TIMEOUT;
                    HTTP_SetLastErrorMessage("Socket timeout while waiting for HTTP response data");
                    goto cleanup;
                }

                continue;
            }

            result = HTTP_ERROR_CONNECTION_FAILED;
            HTTP_SetLastErrorMessage("Socket error while receiving HTTP response data");
            goto cleanup;
        }

        if (received == 0) {
            connectionClosed = 1;
            break;
        }

        idleTimeMs = 0;
        responseStarted = 1;

        if (!headersParsed) {
            unsigned int receivedUnsigned = (unsigned int)received;

            if (headerBufferUsed + receivedUnsigned >= sizeof(headerBuffer)) {
                result = HTTP_ERROR_INVALID_RESPONSE;
                HTTP_SetLastErrorMessage("HTTP headers exceed internal buffer size");
                goto cleanup;
            }

            memcpy(headerBuffer + headerBufferUsed, receiveBuffer, receivedUnsigned);
            headerBufferUsed += receivedUnsigned;
            headerBuffer[headerBufferUsed] = '\0';

            headerEnd = strstr(headerBuffer, "\r\n\r\n");
            if (!headerEnd) {
                continue;
            }

            headersParsed = 1;
            headerLength = (unsigned int)((headerEnd + 4) - headerBuffer);
            statusLine = headerBuffer;

            const char* codeStart = NULL;
            if (strncmp(statusLine, "HTTP/1.1", 8) == 0 && statusLine[8] == ' ') {
                strcpy(version, "HTTP/1.1");
                codeStart = statusLine + 9;
            } else if (strncmp(statusLine, "HTTP/1.0", 8) == 0 && statusLine[8] == ' ') {
                strcpy(version, "HTTP/1.0");
                codeStart = statusLine + 9;
            } else {
                result = HTTP_ERROR_INVALID_RESPONSE;
                HTTP_SetLastErrorMessage("Received invalid HTTP status line");
                goto cleanup;
            }

            unsigned int codeValue = 0;
            while (codeStart && *codeStart >= '0' && *codeStart <= '9') {
                codeValue = codeValue * 10U + (unsigned int)(*codeStart - '0');
                codeStart++;
            }

            if (codeValue > 65535U) {
                result = HTTP_ERROR_INVALID_RESPONSE;
                HTTP_SetLastErrorMessage("HTTP status code value is out of range");
                goto cleanup;
            }

            statusCode = (unsigned short)codeValue;

            strcpy(metadataOut->Version, version);
            metadataOut->StatusCode = statusCode;

            const char* reasonStart = codeStart;
            while (reasonStart && *reasonStart == ' ') {
                reasonStart++;
            }
            const char* reasonEnd = strstr(reasonStart, "\r\n");
            if (!reasonEnd) {
                reasonEnd = reasonStart;
            }
            unsigned int reasonLength = (unsigned int)(reasonEnd - reasonStart);
            if (reasonLength > 0U) {
                if (reasonLength >= sizeof(metadataOut->ReasonPhrase)) {
                    reasonLength = sizeof(metadataOut->ReasonPhrase) - 1U;
                }
                memcpy(metadataOut->ReasonPhrase, reasonStart, reasonLength);
                metadataOut->ReasonPhrase[reasonLength] = '\0';
            }

            unsigned int headerCopyLength = headerLength;
            if (headerCopyLength >= sizeof(metadataOut->Headers)) {
                headerCopyLength = sizeof(metadataOut->Headers) - 1U;
            }
            memcpy(metadataOut->Headers, headerBuffer, headerCopyLength);
            metadataOut->Headers[headerCopyLength] = '\0';

            char* transferEncodingStr = strstr(headerBuffer, "Transfer-Encoding:");
            if (transferEncodingStr) {
                const char* valueStart = transferEncodingStr + 18;
                while (*valueStart == ' ' || *valueStart == '\t') {
                    valueStart++;
                }

                char encodingValue[64];
                unsigned int encodingIndex = 0;
                while (*valueStart && *valueStart != '\r' && *valueStart != '\n' &&
                       encodingIndex < (sizeof(encodingValue) - 1U)) {
                    encodingValue[encodingIndex++] = *valueStart;
                    valueStart++;
                }
                encodingValue[encodingIndex] = '\0';

                if (HTTP_HeaderValueContainsToken(encodingValue, "chunked")) {
                    isChunked = 1;
                    HTTP_ChunkParserInit(&chunkParser);
                }
            }

            if (!isChunked) {
                char* contentLengthStr = strstr(headerBuffer, "Content-Length:");
                if (contentLengthStr) {
                    const char* numStart = contentLengthStr + 16;
                    contentLength = 0;
                    while (*numStart >= '0' && *numStart <= '9') {
                        contentLength = contentLength * 10U + (unsigned int)(*numStart - '0');
                        numStart++;
                    }
                }
            }

            if (contentLength > 0) {
                unsigned int targetBufferSize = HTTP_SelectDownloadBufferSize(contentLength);
                if (targetBufferSize != receiveBufferSize) {
                    unsigned char* resizedBuffer = (unsigned char*)malloc(targetBufferSize);
                    if (resizedBuffer) {
                        free(receiveBuffer);
                        receiveBuffer = resizedBuffer;
                        receiveBufferSize = targetBufferSize;
                    }
                }
            }

            metadataOut->ContentLength = contentLength;
            metadataOut->ChunkedEncoding = isChunked;
            if (!statusCallbackInvoked) {
                statusCallbackInvoked = 1;
                if (ProgressCallbacks && ProgressCallbacks->OnStatusLine) {
                    ProgressCallbacks->OnStatusLine(metadataOut, ProgressCallbacks->Context);
                }
            }

            if (statusCode != 200) {
                char statusMessage[64];
                sprintf(statusMessage, "Server responded with HTTP %u", (unsigned int)statusCode);
                HTTP_SetLastErrorMessage(statusMessage);
                result = (int)statusCode;
                goto cleanup;
            }

            file = fopen(Filename, "wb");
            if (!file) {
                result = HTTP_ERROR_MEMORY_ERROR;
                HTTP_SetLastErrorMessage("Failed to open destination file for writing");
                goto cleanup;
            }

            unsigned int bodyDataInBuffer = headerBufferUsed - headerLength;
            if (bodyDataInBuffer > 0U) {
                const unsigned char* bodyStart = (const unsigned char*)(headerBuffer + headerLength);

                if (isChunked) {
                    unsigned int chunkBytesWritten = 0U;
                    result = HTTP_ChunkParserProcess(&chunkParser, bodyStart, bodyDataInBuffer, file, &chunkBytesWritten);
                    if (result != HTTP_SUCCESS) {
                        HTTP_SetLastErrorMessage("Chunk decoder reported an error while processing buffered data");
                        goto cleanup;
                    }

                    if (chunkBytesWritten > 0U && ProgressCallbacks && ProgressCallbacks->OnBodyData) {
                        ProgressCallbacks->OnBodyData(chunkBytesWritten, ProgressCallbacks->Context);
                    }

                    if (chunkParser.State == HTTP_CHUNK_STATE_FINISHED) {
                        responseComplete = 1;
                    }
                } else {
                    result = HTTP_WriteBodyData(file, bodyStart, bodyDataInBuffer);
                    if (result != HTTP_SUCCESS) {
                        HTTP_SetLastErrorMessage("Failed to write buffered response body to file");
                        goto cleanup;
                    }

                    bodyBytesReceived += bodyDataInBuffer;
                    if (bodyDataInBuffer > 0U && ProgressCallbacks && ProgressCallbacks->OnBodyData) {
                        ProgressCallbacks->OnBodyData(bodyDataInBuffer, ProgressCallbacks->Context);
                    }
                    if (contentLength > 0U && bodyBytesReceived >= contentLength) {
                        responseComplete = 1;
                    }
                }
            }

            continue;
        }

        if (isChunked) {
            unsigned int chunkBytesWritten = 0U;
            result = HTTP_ChunkParserProcess(&chunkParser, (const unsigned char*)receiveBuffer,
                                             (unsigned int)received, file, &chunkBytesWritten);
            if (result != HTTP_SUCCESS) {
                HTTP_SetLastErrorMessage("Chunk decoder reported an error while processing response data");
                goto cleanup;
            }

            if (chunkBytesWritten > 0U && ProgressCallbacks && ProgressCallbacks->OnBodyData) {
                ProgressCallbacks->OnBodyData(chunkBytesWritten, ProgressCallbacks->Context);
            }

            if (chunkParser.State == HTTP_CHUNK_STATE_FINISHED) {
                responseComplete = 1;
            }
        } else {
            result = HTTP_WriteBodyData(file, (const unsigned char*)receiveBuffer, (unsigned int)received);
            if (result != HTTP_SUCCESS) {
                HTTP_SetLastErrorMessage("Failed to write response body to file");
                goto cleanup;
            }

            bodyBytesReceived += (unsigned int)received;
            if ((unsigned int)received > 0U && ProgressCallbacks && ProgressCallbacks->OnBodyData) {
                ProgressCallbacks->OnBodyData((unsigned int)received, ProgressCallbacks->Context);
            }
            if (contentLength > 0U && bodyBytesReceived >= contentLength) {
                responseComplete = 1;
            }
        }
    }

    if (result != HTTP_SUCCESS) {
        goto cleanup;
    }

    if (!responseComplete) {
        if (!headersParsed) {
            result = HTTP_ERROR_CONNECTION_FAILED;
            HTTP_SetLastErrorMessage("Connection closed before HTTP headers were received");
            goto cleanup;
        }

        if (isChunked) {
            if (chunkParser.State != HTTP_CHUNK_STATE_FINISHED) {
                result = HTTP_ERROR_CONNECTION_FAILED;
                HTTP_SetLastErrorMessage("Connection closed before final chunk terminator");
                goto cleanup;
            }
        } else if (contentLength > 0U && bodyBytesReceived < contentLength) {
            result = HTTP_ERROR_CONNECTION_FAILED;
            HTTP_SetLastErrorMessage("Connection closed before expected body length was received");
            goto cleanup;
        } else if (!connectionClosed) {
            responseComplete = 1;
        }
    }

cleanup:
    if (file) {
        fclose(file);
    }

    if (receiveBuffer) {
        free(receiveBuffer);
    }

    if (result == HTTP_SUCCESS) {
        if (BytesWritten) {
            if (isChunked) {
                *BytesWritten = chunkParser.TotalBytesWritten;
            } else {
                *BytesWritten = bodyBytesReceived;
            }
        }

        if (ResponseMetadata) {
            strcpy(ResponseMetadata->Version, version);
            ResponseMetadata->StatusCode = statusCode;
            ResponseMetadata->ContentLength = contentLength;
            ResponseMetadata->ChunkedEncoding = isChunked;
        }

        HTTP_SetLastErrorMessage("Success");
    } else {
        if (BytesWritten) {
            *BytesWritten = 0;
        }
    }

    return result;
}

/***************************************************************************/

/**
 * @brief Free response data
 * @param Response The response to free
 */
void HTTP_FreeResponse(HTTP_RESPONSE* Response) {
    if (!Response) {
        return;
    }

    if (Response->Body) {
        free(Response->Body);
        Response->Body = NULL;
    }

    Response->BodyLength = 0;
    Response->ContentLength = 0;
}

/***************************************************************************/

/**
 * @brief Get header value from response
 * @param Response The HTTP response
 * @param HeaderName The header name to search for
 * @return Pointer to header value or NULL if not found
 */
const char* HTTP_GetHeader(const HTTP_RESPONSE* Response, const char* HeaderName) {
    char* headerPos;
    char* valueStart;
    char* valueEnd;
    static char headerValue[256];

    if (!Response || !HeaderName || Response->Headers[0] == 0) {
        return NULL;
    }

    headerPos = strstr(Response->Headers, HeaderName);
    if (!headerPos) {
        return NULL;
    }

    // Find the colon
    valueStart = strstr(headerPos, ":");
    if (!valueStart) {
        return NULL;
    }

    // Skip colon and spaces
    valueStart++;
    while (*valueStart == ' ' || *valueStart == '\t') {
        valueStart++;
    }

    // Find end of line
    valueEnd = strstr(valueStart, "\r\n");
    if (!valueEnd) {
        valueEnd = strstr(valueStart, "\n");
    }

    if (!valueEnd) {
        return NULL;
    }

    // Copy value
    unsigned int valueLength = valueEnd - valueStart;
    if (valueLength >= sizeof(headerValue)) {
        valueLength = sizeof(headerValue) - 1;
    }

    memcpy(headerValue, valueStart, valueLength);
    headerValue[valueLength] = '\0';

    return headerValue;
}

/***************************************************************************/

/**
 * @brief Get HTTP status description string
 * @param StatusCode HTTP status code
 * @return Pointer to status description string
 */
const char* HTTP_GetStatusString(unsigned short StatusCode) {
    switch (StatusCode) {
        // 1xx Informational
        case 100: return "100 - Continue";
        case 101: return "101 - Switching Protocols";

        // 2xx Success
        case 200: return "200 - OK";
        case 201: return "201 - Created";
        case 202: return "202 - Accepted";
        case 204: return "204 - No Content";
        case 206: return "206 - Partial Content";

        // 3xx Redirection
        case 300: return "300 - Multiple Choices";
        case 301: return "301 - Moved Permanently";
        case 302: return "302 - Found";
        case 304: return "304 - Not Modified";
        case 307: return "307 - Temporary Redirect";
        case 308: return "308 - Permanent Redirect";

        // 4xx Client Error
        case 400: return "400 - Bad Request";
        case 401: return "401 - Unauthorized";
        case 403: return "403 - Forbidden";
        case 404: return "404 - Not Found";
        case 405: return "405 - Method Not Allowed";
        case 406: return "406 - Not Acceptable";
        case 408: return "408 - Request Timeout";
        case 409: return "409 - Conflict";
        case 410: return "410 - Gone";
        case 411: return "411 - Length Required";
        case 413: return "413 - Payload Too Large";
        case 414: return "414 - URI Too Long";
        case 415: return "415 - Unsupported Media Type";
        case 416: return "416 - Range Not Satisfiable";
        case 418: return "418 - I'm a teapot";
        case 429: return "429 - Too Many Requests";

        // 5xx Server Error
        case 500: return "500 - Internal Server Error";
        case 501: return "501 - Not Implemented";
        case 502: return "502 - Bad Gateway";
        case 503: return "503 - Service Unavailable";
        case 504: return "504 - Gateway Timeout";
        case 505: return "505 - HTTP Version Not Supported";

        default: return "Unknown Status Code";
    }
}
