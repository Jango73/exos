
/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime

\************************************************************************/

#include "../../kernel/include/User.h"
#include "../../kernel/include/VarArg.h"
#include "../include/exos-runtime.h"
#include "../include/exos-string.h"
#include "../include/exos.h"

/************************************************************************/

#ifdef __KERNEL__
/* In kernel mode, use kernel heap functions directly */
extern void* KernelHeapAlloc(unsigned long size);
extern void KernelHeapFree(void* ptr);
extern void* KernelHeapRealloc(void* ptr, unsigned long size);
#endif

/************************************************************************/

// Global argc/argv for main function
int _argc = 0;
char** _argv = NULL;

// Static storage for argv to avoid early malloc() issues
char* _static_argv[16];                       // Support up to 16 arguments
char _static_arg_storage[MAX_STRING_BUFFER];  // Storage for argument strings

PROCESS_INFO _ProcessInfo;

void RuntimeInitializeStandardStreams(void);

/************************************************************************/

// Suppress unused warnings for future use
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
static void __attribute__((unused)) suppress_unused_warnings(void) {
    UNUSED(_static_argv);
    UNUSED(_static_arg_storage);
}
#pragma GCC diagnostic pop

/************************************************************************/

char* strncat(char* dest, const char* src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i;

    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }

    dest[dest_len + i] = '\0';
    return dest;
}

/************************************************************************/

char* strrchr(const char* string, int character) {
    const char* last = NULL;
    char target = (char)character;

    if (string == NULL) {
        return NULL;
    }

    while (*string != '\0') {
        if (*string == target) {
            last = string;
        }
        string++;
    }

    if (target == '\0') {
        return (char*)string;
    }

    return (char*)last;
}

/************************************************************************/

#ifndef __KERNEL__
void debug(const char* format, ...) {
    char Buffer[MAX_STRING_BUFFER];
    VarArgList Args;

    VarArgStart(Args, format);
    StringPrintFormatArgs((LPSTR)Buffer, (LPCSTR)format, Args);
    VarArgEnd(Args);

    exoscall(SYSCALL_Debug, EXOS_PARAM(Buffer));
}
#endif

/************************************************************************/

#ifndef __KERNEL__
void exit(int ErrorCode) { __exit__(ErrorCode); }
#endif

/************************************************************************/

#ifdef __KERNEL__
void* malloc(size_t s) { return KernelHeapAlloc(s); }
#else
void* malloc(size_t s) { return (void*)exoscall(SYSCALL_HeapAlloc, EXOS_PARAM(s)); }
#endif

/************************************************************************/

#ifdef __KERNEL__
void free(void* p) { KernelHeapFree(p); }
#else
void free(void* p) { exoscall(SYSCALL_HeapFree, EXOS_PARAM(p)); }
#endif

/************************************************************************/

#ifdef __KERNEL__
void* realloc(void* ptr, size_t size) { return KernelHeapRealloc(ptr, size); }
#else
void* realloc(void* ptr, size_t size) {
    HEAP_REALLOC_INFO info;
    info.Header.Size = sizeof(HEAP_REALLOC_INFO);
    info.Header.Version = EXOS_ABI_VERSION;
    info.Header.Flags = 0;
    info.Pointer = ptr;
    info.Size = (U32)size;
    return (void*)exoscall(SYSCALL_HeapRealloc, EXOS_PARAM(&info));
}
#endif

/************************************************************************/

int memerror(void) {
    /* Memory allocation error handler for bcrypt compatibility */
    /* In user space, we can't do much except return error */
    return -1;
}

/************************************************************************/

int sprintf(char* str, const char* fmt, ...) {
    VarArgList Args;
    VarArgStart(Args, fmt);
    StringPrintFormatArgs((LPSTR)str, (LPCSTR)fmt, Args);
    VarArgEnd(Args);
    return strlen(str);
}

/************************************************************************/

int snprintf(char* str, size_t size, const char* fmt, ...) {
    char Buffer[MAX_STRING_BUFFER];
    VarArgList Args;
    UINT FormattedLength;
    UINT CopyLength;

    VarArgStart(Args, fmt);
    StringPrintFormatArgs((LPSTR)Buffer, (LPCSTR)fmt, Args);
    VarArgEnd(Args);

    FormattedLength = strlen(Buffer);

    if (str != NULL && size > 0) {
        CopyLength = (FormattedLength < (size - 1)) ? FormattedLength : (UINT)(size - 1);
        if (CopyLength > 0) {
            memmove(str, Buffer, CopyLength);
        }
        str[CopyLength] = 0;
    }

    return (int)FormattedLength;
}

/************************************************************************/

char* getcwd(char* buffer, size_t size) {
    size_t length;

    if (buffer == NULL || size == 0) {
        return NULL;
    }

    if (_ProcessInfo.WorkFolder[0] == '\0') {
        buffer[0] = '\0';
        return buffer;
    }

    length = strlen((const char*)_ProcessInfo.WorkFolder);
    if (length + 1 > size) {
        return NULL;
    }

    memmove(buffer, _ProcessInfo.WorkFolder, length);
    buffer[length] = '\0';
    return buffer;
}

/************************************************************************/

#ifndef __KERNEL__
int printf(const char* fmt, ...) {
    char Buffer[MAX_STRING_BUFFER];
    VarArgList Args;

    VarArgStart(Args, fmt);
    StringPrintFormatArgs((LPSTR)Buffer, (LPCSTR)fmt, Args);
    VarArgEnd(Args);

    exoscall(SYSCALL_ConsolePrint, EXOS_PARAM(Buffer));
    return strlen(Buffer);
}
#endif

/************************************************************************/

#ifndef __KERNEL__
int fprintf(FILE* fp, const char* fmt, ...) {
    char Buffer[MAX_STRING_BUFFER];
    VarArgList Args;

    VarArgStart(Args, fmt);
    StringPrintFormatArgs((LPSTR)Buffer, (LPCSTR)fmt, Args);
    VarArgEnd(Args);

    if (!fp) {
        printf("%s", Buffer);
        return strlen(Buffer);
    }

    return fwrite(Buffer, 1, strlen(Buffer), fp);
}
#endif

/************************************************************************/

#ifndef __KERNEL__
int peekch(void) { return exoscall(SYSCALL_ConsolePeekKey, EXOS_PARAM(0)); }
#endif

/************************************************************************/

#ifndef __KERNEL__
int getch(void) {
    KEYCODE KeyCode;

    while (exoscall(SYSCALL_ConsolePeekKey, EXOS_PARAM(0)) == 0) {
        sleep(10);
    }

    exoscall(SYSCALL_ConsoleGetKey, EXOS_PARAM(&KeyCode));

    return (int)KeyCode.ASCIICode;
}
#endif

/************************************************************************/

#ifndef __KERNEL__
int getkey(void) {
    KEYCODE KeyCode;

    while (exoscall(SYSCALL_ConsolePeekKey, EXOS_PARAM(0)) == 0) {
        sleep(10);
    }

    exoscall(SYSCALL_ConsoleGetKey, EXOS_PARAM(&KeyCode));

    return (int)KeyCode.VirtualKey;
}
#endif

/************************************************************************/

#ifndef __KERNEL__
unsigned getkeymodifiers(void) { return (unsigned)GetKeyModifiers(); }
#endif

/************************************************************************/

#ifndef __KERNEL__
int _beginthread(void (*start_address)(void*), unsigned stack_size, void* arg_list) {
    TASK_INFO TaskInfo;
    UINT Result;

    memset(&TaskInfo, 0, sizeof(TaskInfo));

    TaskInfo.Header.Size = sizeof(TASK_INFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    TaskInfo.Func = (TASKFUNC)start_address;
#pragma GCC diagnostic pop
    TaskInfo.Parameter = (LPVOID)arg_list;
    TaskInfo.StackSize = (U32)stack_size;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;
    TaskInfo.Task = 0;

    Result = (UINT)exoscall(SYSCALL_CreateTask, EXOS_PARAM(&TaskInfo));
    if (Result != DF_RETURN_SUCCESS || TaskInfo.Task == NULL) {
        return 0;
    }

    return (int)TaskInfo.Task;
}
#endif

/************************************************************************/

void _endthread(void) {}

/************************************************************************/

#ifndef __KERNEL__
void sleep(unsigned ms) { exoscall(SYSCALL_Sleep, EXOS_PARAM(ms)); }
#endif

/************************************************************************/

#ifndef __KERNEL__
int system(const char* __cmd) {
    PROCESS_INFO ProcessInfo;
    UINT Result;

    memset(&ProcessInfo, 0, sizeof(ProcessInfo));

    ProcessInfo.Header.Size = sizeof(PROCESS_INFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    StringCopyLimit(ProcessInfo.CommandLine, (LPCSTR)__cmd, MAX_PATH_NAME);
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;
    ProcessInfo.Process = NULL;
    ProcessInfo.Task = NULL;

    Result = (UINT)exoscall(SYSCALL_CreateProcess, EXOS_PARAM(&ProcessInfo));
    return (int)Result;
}
#endif

/************************************************************************/

#ifndef __KERNEL__
FILE* fopen(const char* __name, const char* __mode) {
    FILE_OPEN_INFO info;
    FILE* __fp;
    HANDLE handle;

    info.Header.Size = sizeof(FILE_OPEN_INFO);
    info.Header.Version = EXOS_ABI_VERSION;
    info.Header.Flags = 0;
    info.Flags = 0;
    info.Name = (LPCSTR)__name;

    char PrimaryMode = 0;
    int HasPlus = 0;

    for (const char* ModeChar = __mode; ModeChar && *ModeChar; ++ModeChar) {
        switch (*ModeChar) {
            case 'r':
            case 'w':
            case 'a':
                if (PrimaryMode != 0 && PrimaryMode != *ModeChar) {
                    return NULL;
                }
                PrimaryMode = *ModeChar;
                break;
            case '+':
                HasPlus = 1;
                break;
            case 'b':
            case 't':
                break;
            default:
                return NULL;
        }
    }

    if (PrimaryMode == 0) {
        return NULL;
    }

    switch (PrimaryMode) {
        case 'r':
            info.Flags |= FILE_OPEN_READ | FILE_OPEN_EXISTING;
            break;
        case 'w':
            info.Flags |= FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
            break;
        case 'a':
            info.Flags |= FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_SEEK_END;
            break;
        default:
            return NULL;
    }

    if (HasPlus) {
        info.Flags |= FILE_OPEN_READ | FILE_OPEN_WRITE;
    }

    handle = exoscall(SYSCALL_OpenFile, EXOS_PARAM(&info));

    if (handle) {
        __fp = (FILE*)malloc(sizeof(FILE));

        if (__fp == NULL) {
            exoscall(SYSCALL_DeleteObject, EXOS_PARAM(handle));
            return NULL;
        }

        __fp->_ptr = NULL;
        __fp->_cnt = 0;
        __fp->_base = (unsigned char*)malloc(4096);
        __fp->_flag = 0;
        __fp->_handle = handle;
        __fp->_bufsize = 4096;
        __fp->_ungotten = 0;
        __fp->_tmpfchar = 0;

        return __fp;
    }

    return NULL;
}
#endif

/************************************************************************/

#ifndef __KERNEL__
int fclose(FILE* __fp) {
    if (__fp) {
        if (__fp->_handle != 0) {
            exoscall(SYSCALL_DeleteObject, EXOS_PARAM(__fp->_handle));
            __fp->_handle = 0;
        }

        if (__fp == stdin || __fp == stdout || __fp == stderr) {
            return 1;
        }

        if (__fp->_base) free(__fp->_base);
        free(__fp);
        return 1;
    }
    return 0;
}
#endif

/************************************************************************/

#ifndef __KERNEL__
size_t fread(void* buf, size_t elsize, size_t num, FILE* fp) {
    FILE_OPERATION fileop;

    if (!fp) return 0;

    fileop.Header.Size = sizeof fileop;
    fileop.Header.Version = EXOS_ABI_VERSION;
    fileop.Header.Flags = 0;
    fileop.File = (HANDLE)fp->_handle;
    fileop.NumBytes = elsize * num;
    fileop.Buffer = buf;

    return (size_t)exoscall(SYSCALL_ReadFile, EXOS_PARAM(&fileop));
}

/************************************************************************/

size_t fwrite(const void* buf, size_t elsize, size_t num, FILE* fp) {
    FILE_OPERATION fileop;

    if (!fp) return 0;

    fileop.Header.Size = sizeof(fileop);
    fileop.Header.Version = EXOS_ABI_VERSION;
    fileop.Header.Flags = 0;
    fileop.File = (HANDLE)fp->_handle;
    fileop.NumBytes = elsize * num;
    fileop.Buffer = (void*)buf;

    size_t result = (size_t)exoscall(SYSCALL_WriteFile, EXOS_PARAM(&fileop));

    return result;
}

/************************************************************************/

int fseek(FILE* fp, long int pos, int whence) {
    FILE_OPERATION Operation;
    long Target = 0;
    long Current;
    long Size;
    UINT Result;

    if (fp == NULL) return -1;

    Current = (long)exoscall(SYSCALL_GetFilePointer, EXOS_PARAM(fp->_handle));

    switch (whence) {
        case SEEK_SET:
            Target = pos;
            break;
        case SEEK_CUR:
            Target = Current + pos;
            break;
        case SEEK_END:
            Size = (long)exoscall(SYSCALL_GetFileSize, EXOS_PARAM(fp->_handle));
            Target = Size + pos;
            break;
        default:
            return -1;
    }

    if (Target < 0) {
        return -1;
    }

    Operation.Header.Size = sizeof(Operation);
    Operation.Header.Version = EXOS_ABI_VERSION;
    Operation.Header.Flags = 0;
    Operation.File = (HANDLE)fp->_handle;
    Operation.NumBytes = (U32)Target;
    Operation.Buffer = NULL;

    Result = (UINT)exoscall(SYSCALL_SetFilePointer, EXOS_PARAM(&Operation));
    return (Result == DF_RETURN_SUCCESS) ? 0 : -1;
}

/************************************************************************/

long int ftell(FILE* fp) {
    if (fp == NULL) return -1;
    return (long int)exoscall(SYSCALL_GetFilePointer, EXOS_PARAM(fp->_handle));
}

/************************************************************************/

int feof(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/************************************************************************/

int fflush(FILE* fp) {
    UNUSED(fp);
    return 0;
}

/************************************************************************/

/* Input helpers */

int fgetc(FILE* fp) {
    unsigned char c;
    size_t readCount;

    if (!fp) return -1;

    readCount = fread(&c, 1, 1, fp);
    if (readCount == 0) {
        return -1;
    }

    return (int)c;
}

int fgets(char* str, int num, FILE* fp) {
    int count = 0;
    int ch;

    if (str == NULL || num <= 0 || fp == NULL) return 0;

    while (count < num - 1) {
        ch = fgetc(fp);
        if (ch == -1) {
            break;
        }
        str[count++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }

    if (count == 0) {
        return 0;
    }

    str[count] = '\0';
    return (int)count;
}

/************************************************************************/
// Socket API implementations

// TODO : when pointer masking done, remove SocketDescriptorToHandle
static inline SOCKET_HANDLE SocketDescriptorToHandle(int SocketDescriptor) {
    return (SOCKET_HANDLE)(INT)SocketDescriptor;
}

/************************************************************************/

int socket(int domain, int type, int protocol) { return (int)SocketCreate((U16)domain, (U16)type, (U16)protocol); }

/************************************************************************/

int bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    SOCKET_ADDRESS kernelAddr;

    if (!addr || addrlen < sizeof(struct sockaddr)) {
        return -1;
    }

    kernelAddr.AddressFamily = addr->sa_family;
    memcpy(kernelAddr.Data, addr->sa_data, sizeof(kernelAddr.Data));

    return (int)SocketBind(SocketDescriptorToHandle(sockfd), &kernelAddr, (U32)addrlen);
}

/************************************************************************/

int listen(int sockfd, int backlog) { return (int)SocketListen(SocketDescriptorToHandle(sockfd), (U32)backlog); }

/************************************************************************/

int accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    SOCKET_ADDRESS kernelAddr;
    U32 len = sizeof(kernelAddr);
    int result = (int)SocketAccept(SocketDescriptorToHandle(sockfd), &kernelAddr, &len);

    if (result >= 0 && addr && addrlen && *addrlen >= sizeof(struct sockaddr)) {
        addr->sa_family = kernelAddr.AddressFamily;
        memcpy(addr->sa_data, kernelAddr.Data, sizeof(addr->sa_data));
        *addrlen = sizeof(struct sockaddr);
    }

    return result;
}

/************************************************************************/

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
    SOCKET_ADDRESS kernelAddr;

    if (!addr || addrlen < sizeof(struct sockaddr)) {
        return -1;
    }

    kernelAddr.AddressFamily = addr->sa_family;
    memcpy(kernelAddr.Data, addr->sa_data, sizeof(kernelAddr.Data));

    return (int)SocketConnect(SocketDescriptorToHandle(sockfd), &kernelAddr, (U32)addrlen);
}

/************************************************************************/

size_t send(int sockfd, const void* buf, size_t len, int flags) {
    I32 result = SocketSend(SocketDescriptorToHandle(sockfd), (LPCVOID)buf, (U32)len, (U32)flags);
    return (result >= 0) ? (size_t)result : 0;
}

/************************************************************************/

size_t recv(int sockfd, void* buf, size_t len, int flags) {
    return (size_t)SocketReceive(SocketDescriptorToHandle(sockfd), (LPVOID)buf, (U32)len, (U32)flags);
}

/************************************************************************/

size_t sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
    SOCKET_ADDRESS kernelAddr;

    if (!dest_addr || addrlen < sizeof(struct sockaddr)) {
        return 0;
    }

    kernelAddr.AddressFamily = dest_addr->sa_family;
    memcpy(kernelAddr.Data, dest_addr->sa_data, sizeof(kernelAddr.Data));

    I32 result =
        SocketSendTo(SocketDescriptorToHandle(sockfd), (LPCVOID)buf, (U32)len, (U32)flags, &kernelAddr, (U32)addrlen);
    return (result >= 0) ? (size_t)result : 0;
}

/************************************************************************/

size_t recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t* addrlen) {
    SOCKET_ADDRESS kernelAddr;
    U32 addr_len = sizeof(kernelAddr);
    I32 result =
        SocketReceiveFrom(SocketDescriptorToHandle(sockfd), (LPVOID)buf, (U32)len, (U32)flags, &kernelAddr, &addr_len);

    if (result >= 0 && src_addr && addrlen && *addrlen >= sizeof(struct sockaddr)) {
        src_addr->sa_family = kernelAddr.AddressFamily;
        memcpy(src_addr->sa_data, kernelAddr.Data, sizeof(src_addr->sa_data));
        *addrlen = sizeof(struct sockaddr);
    }

    return (result >= 0) ? (size_t)result : 0;
}

/************************************************************************/

int shutdown(int sockfd, int how) { return (int)SocketShutdown(SocketDescriptorToHandle(sockfd), (U32)how); }

/************************************************************************/

int getsockopt(int sockfd, int level, int optname, void* optval, socklen_t* optlen) {
    U32 opt_len = (optlen != NULL) ? *optlen : 0;
    int result =
        (int)SocketGetOption(SocketDescriptorToHandle(sockfd), (U32)level, (U32)optname, (LPVOID)optval, &opt_len);
    if (optlen != NULL) {
        *optlen = opt_len;
    }
    return result;
}

/************************************************************************/

int setsockopt(int sockfd, int level, int optname, const void* optval, socklen_t optlen) {
    return (int)SocketSetOption(
        SocketDescriptorToHandle(sockfd), (U32)level, (U32)optname, (LPCVOID)optval, (U32)optlen);
}

/************************************************************************/

int getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    SOCKET_ADDRESS kernelAddr;
    U32 len = sizeof(kernelAddr);
    int result = (int)SocketGetPeerName(SocketDescriptorToHandle(sockfd), &kernelAddr, &len);

    if (result == 0 && addr && addrlen && *addrlen >= sizeof(struct sockaddr)) {
        addr->sa_family = kernelAddr.AddressFamily;
        memcpy(addr->sa_data, kernelAddr.Data, sizeof(addr->sa_data));
        *addrlen = sizeof(struct sockaddr);
    }

    return result;
}

/************************************************************************/

int getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
    SOCKET_ADDRESS kernelAddr;
    U32 len = sizeof(kernelAddr);
    int result = (int)SocketGetSocketName(SocketDescriptorToHandle(sockfd), &kernelAddr, &len);

    if (result == 0 && addr && addrlen && *addrlen >= sizeof(struct sockaddr)) {
        addr->sa_family = kernelAddr.AddressFamily;
        memcpy(addr->sa_data, kernelAddr.Data, sizeof(addr->sa_data));
        *addrlen = sizeof(struct sockaddr);
    }

    return result;
}

#endif

/************************************************************************/

#ifndef __KERNEL__
void _SetupArguments(void) {
    char* Token;
    int i;
    char* StoragePtr;
    char* p;
    int InQuotes;

    debug("[_SetupArguments] enter");

    _argc = 0;
    _argv = _static_argv;
    StoragePtr = _static_arg_storage;

    // Clear static arrays
    for (i = 0; i < 16; i++) {
        _static_argv[i] = NULL;
    }

    memset(&_ProcessInfo, 0, sizeof(_ProcessInfo));

    // Get process information
    _ProcessInfo.Header.Size = sizeof(_ProcessInfo);
    _ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    _ProcessInfo.Header.Flags = 0;
    _ProcessInfo.Process = 0;

    if (exoscall(SYSCALL_GetProcessInfo, EXOS_PARAM(&_ProcessInfo)) != DF_RETURN_SUCCESS) {
        return;
    }

    RuntimeInitializeStandardStreams();

    int CommandLineLen = strlen((const char*)_ProcessInfo.CommandLine);

    if (CommandLineLen >= MAX_PATH_NAME || CommandLineLen + 16 >= MAX_STRING_BUFFER) {
        // Not enough storage space - fallback
        _argc = 1;
        _static_argv[0] = "prog";
        return;
    }

    // If empty command line, fallback
    if (_ProcessInfo.CommandLine[0] == '\0') {
        _argc = 1;
        _static_argv[0] = "prog";
        return;
    }

    // Parse CommandLine directly into argv
    p = (char*)_ProcessInfo.CommandLine;
    _argc = 0;

    while (*p && _argc < 15) {
        // Skip leading spaces
        while (*p == ' ') p++;
        if (*p == 0) break;

        Token = StoragePtr;
        _static_argv[_argc] = Token;
        InQuotes = 0;

        // Copy argument while parsing
        while (*p && (InQuotes || *p != ' ')) {
            if (*p == '"') {
                InQuotes = !InQuotes;
                p++;  // Skip quote, don't copy it
            } else {
                *StoragePtr++ = *p++;
            }
        }

        // Null-terminate current argument
        *StoragePtr++ = 0;
        _argc++;
    }
}
#endif
