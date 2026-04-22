
/************************************************************************\

    EXOS Runtime HTTP Client
    Copyright (c) 1999-2025 Jango73

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


    HTTP Client API

\************************************************************************/

#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED

#include "../include/exos-runtime.h"
#include "../include/exos.h"
#include "exos-adaptive-delay.h"

/************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// HTTP Error Codes

#define HTTP_SUCCESS                0
#define HTTP_ERROR_INVALID_URL      1
#define HTTP_ERROR_CONNECTION_FAILED 2
#define HTTP_ERROR_TIMEOUT          3
#define HTTP_ERROR_INVALID_RESPONSE 4
#define HTTP_ERROR_MEMORY_ERROR     5
#define HTTP_ERROR_PROTOCOL_ERROR   6
#define HTTP_ERROR_SOCKET_OVERFLOW  7

/***************************************************************************/
// URL Structure

typedef struct tag_URL {
    char Scheme[8];                 // http
    char Host[256];                 // hostname or IP
    unsigned short Port;            // port number (default 80)
    char Path[512];                 // request path
    char Query[256];                // query string
    int Valid;                      // URL validity flag
} URL;

/***************************************************************************/
// HTTP Request Structure

typedef struct tag_HTTP_REQUEST {
    char Method[8];                 // GET, POST, HEAD
    char URI[256];                  // Request URI
    char Version[16];               // HTTP/1.1
    char Headers[1024];             // Request headers
    unsigned char* Body;            // Request body (for POST)
    unsigned int BodyLength;        // Body length
} HTTP_REQUEST;

/***************************************************************************/
// HTTP Response Structure

typedef struct tag_HTTP_RESPONSE {
    char Version[16];               // HTTP version
    unsigned short StatusCode;      // HTTP status code
    char ReasonPhrase[64];          // Status reason phrase
    char Headers[2048];             // Response headers
    unsigned char* Body;            // Response body
    unsigned int BodyLength;        // Body length
    unsigned int ContentLength;     // Content-Length header value
    int ChunkedEncoding;            // Transfer-Encoding: chunked
} HTTP_RESPONSE;

/***************************************************************************/
// HTTP Connection Structure

typedef struct tag_HTTP_CONNECTION {
    SOCKET_HANDLE SocketHandle;  // Berkeley socket descriptor
    unsigned int RemoteIP;          // Server IP address
    unsigned short RemotePort;      // Server port (usually 80)
    int Connected;                  // Connection status
    int KeepAlive;                  // Keep-alive support
    HTTP_REQUEST* CurrentRequest;    // Active request
    HTTP_RESPONSE* CurrentResponse;  // Active response
    unsigned char ReceiveBuffer[4096]; // HTTP receive buffer
    unsigned int ReceiveBufferUsed; // Buffer usage
    unsigned int ReceiveState;      // Parsing state
    ADAPTIVE_DELAY_STATE DelayState; // Exponential backoff for connection attempts
} HTTP_CONNECTION;

/***************************************************************************/
// HTTP Progress Callback Types

typedef void (*HTTP_ResponseProgressCallback)(const HTTP_RESPONSE* Response, void* Context);
typedef void (*HTTP_BodyProgressCallback)(unsigned int Bytes, void* Context);

typedef struct tag_HTTP_PROGRESS_CALLBACKS {
    HTTP_ResponseProgressCallback OnStatusLine;
    HTTP_BodyProgressCallback OnBodyData;
    void* Context;
} HTTP_PROGRESS_CALLBACKS;

/***************************************************************************/
// HTTP API Functions

/**
 * @brief Configure the default receive timeout for HTTP sockets
 * @param TimeoutMs Timeout in milliseconds (0 disables the timeout)
 */
void HTTP_SetDefaultReceiveTimeout(unsigned int TimeoutMs);

/**
 * @brief Retrieve the current default receive timeout for HTTP sockets
 * @return Timeout in milliseconds (0 means no timeout)
 */
unsigned int HTTP_GetDefaultReceiveTimeout(void);

/***************************************************************************/
// HTTP Core Operations

/**
 * @brief Parse a URL string into components
 * @param URLString The URL string to parse
 * @param ParsedURL Output structure to store parsed components
 * @return 1 if parsing was successful, 0 otherwise
 */
int HTTP_ParseURL(const char* URLString, URL* ParsedURL);

/**
 * @brief Create HTTP connection to host
 * @param Host The hostname or IP address
 * @param Port The port number (usually 80 for HTTP)
 * @return Pointer to HTTP_CONNECTION or NULL on failure
 */
HTTP_CONNECTION* HTTP_CreateConnection(const char* Host, unsigned short Port);

/**
 * @brief Destroy HTTP connection
 * @param Connection The connection to destroy
 */
void HTTP_DestroyConnection(HTTP_CONNECTION* Connection);

/**
 * @brief Send HTTP GET request
 * @param Connection The HTTP connection
 * @param Path The request path
 * @param Response The response structure to fill
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_Get(HTTP_CONNECTION* Connection, const char* Path, HTTP_RESPONSE* Response);

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
              unsigned int BodyLength, HTTP_RESPONSE* Response);

/**
 * @brief Receive an HTTP response and stream the body directly to a file
 * @param Connection The HTTP connection
 * @param Filename The destination filename where the body will be written
 * @param ResponseMetadata Optional pointer that receives parsed response metadata
 * @param BytesWritten Optional pointer that receives the number of payload bytes written
 * @param ProgressCallbacks Optional callbacks invoked to report progress while downloading
 * @return HTTP_SUCCESS on success, error code or HTTP status code otherwise
 */
int HTTP_DownloadToFile(HTTP_CONNECTION* Connection, const char* Filename,
                        HTTP_RESPONSE* ResponseMetadata, unsigned int* BytesWritten,
                        const HTTP_PROGRESS_CALLBACKS* ProgressCallbacks);

/**
 * @brief Free response data
 * @param Response The response to free
 */
void HTTP_FreeResponse(HTTP_RESPONSE* Response);

/**
 * @brief Send HTTP request
 * @param Connection The HTTP connection
 * @param Method The HTTP method (GET, POST, etc.)
 * @param Path The request path
 * @param Body The request body data (NULL for GET)
 * @param BodyLength The length of the request body (0 for GET)
 * @return HTTP_SUCCESS on success, error code otherwise
 */
int HTTP_SendRequest(HTTP_CONNECTION* Connection, const char* Method, const char* Path,
                           const unsigned char* Body, unsigned int BodyLength);

/**
 * @brief Get header value from response
 * @param Response The HTTP response
 * @param HeaderName The header name to search for
 * @return Pointer to header value or NULL if not found
 */
const char* HTTP_GetHeader(const HTTP_RESPONSE* Response, const char* HeaderName);

/**
 * @brief Get HTTP status description string
 * @param StatusCode HTTP status code
 * @return Pointer to status description string
 */
const char* HTTP_GetStatusString(unsigned short StatusCode);

/**
 * @brief Retrieve a descriptive string for the last HTTP runtime error
 * @return Pointer to a statically stored error description string
 */
const char* HTTP_GetLastErrorMessage(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // HTTP_H_INCLUDED
