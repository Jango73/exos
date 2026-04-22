
/************************************************************************\

    EXOS Runtime
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


    EXOS STD C API

\************************************************************************/

#ifndef EXOS_RUNTIME_H_INCLUDED
#define EXOS_RUNTIME_H_INCLUDED

#include "../../kernel/include/User.h"
#include "stdarg.h"

#ifdef __cplusplus
extern "C" {
#endif

// ANSI required limits

#define CHAR_BIT 8

#ifdef __CHAR_SIGNED__
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#else
#define CHAR_MIN 0
#define CHAR_MAX 255
#endif

#define MB_LEN_MAX 2
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255U

#define SHRT_MIN (-32767 - 1)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U
#define LONG_MAX 2147483647L
#define LONG_MIN (-2147483647L - 1)
#define ULONG_MAX 4294967295UL

#define INT_MIN (-2147483647 - 1)
#define INT_MAX 2147483647
#define UINT_MAX 4294967295U

#define TZNAME_MAX 30

#define EOF (-1)

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define ENOSYS 38
#define ERANGE 34

#ifndef NULL
#define NULL 0L
#endif

#define PI 3.1415926535f
#define TWO_PI 6.283185307179586f

/************************************************************************/
// Types

#if defined(_MSC_VER)
    typedef unsigned __int8 uint8_t;
    typedef signed __int8 int8_t;
    typedef unsigned __int16 uint16_t;
    typedef signed __int16 int16_t;
    typedef unsigned __int32 uint32_t;
    typedef signed __int32 int32_t;
    typedef unsigned __int64 uint64_t;
    typedef signed __int64 int64_t;
    typedef unsigned int uint_t;
    typedef signed int int_t;
    typedef unsigned int size_t;
    typedef unsigned int fpos_t;
    typedef unsigned int uintptr_t;
    typedef signed int intptr_t;
#elif defined(__GNUC__) || defined(__clang__)
    typedef unsigned char uint8_t;
    typedef signed char int8_t;
    typedef unsigned short uint16_t;
    typedef signed short int16_t;
    typedef unsigned int uint32_t;
    typedef signed int int32_t;
    typedef unsigned long long uint64_t;
    typedef signed long long int64_t;
    typedef unsigned long uint_t;
    typedef signed long int_t;
    typedef unsigned long size_t;
    typedef unsigned long fpos_t;
    typedef unsigned long uintptr_t;
    typedef signed long intptr_t;
#else
    #error "Unsupported compiler for Base.h"
#endif

/************************************************************************/
// POSIX Socket types

typedef uint32_t socklen_t;

typedef int (*qsort_comparator)(const void* Left, const void* Right);

#pragma pack(push, 1)
struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    unsigned char sin_zero[8];
};
#pragma pack(pop)

// Socket option constants
#define SOL_SOCKET                1
#define SO_RCVTIMEO               20

/************************************************************************/
// Byte order inline functions

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline unsigned short htons(unsigned short Value) { return Value; }

static inline unsigned short ntohs(unsigned short Value) { return Value; }

static inline unsigned long htonl(unsigned long Value) { return Value; }

static inline unsigned long ntohl(unsigned long Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline unsigned short htons(unsigned short Value) { return (unsigned short)((Value << 8) | (Value >> 8)); }

static inline unsigned short ntohs(unsigned short Value) { return htons(Value); }

static inline unsigned long htonl(unsigned long Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}

static inline unsigned long ntohl(unsigned long Value) { return htonl(Value); }

#else
    #error "Endianness not defined"
#endif

/************************************************************************/

extern void debug(const char* format, ...);

/************************************************************************/
// Error reporting

extern int* __errno_location(void);
#define errno (*__errno_location())

/************************************************************************/

// Command line arguments
extern int _argc;
extern char** _argv;
extern void _SetupArguments(void);

extern uint_t exoscall(uint_t function, uint_t parameter);
extern void __exit__(int_t code);

/************************************************************************/

extern unsigned strcmp(const char*, const char*);
extern int strncmp(const char*, const char*, unsigned);
extern char* strstr(const char* haystack, const char* needle);
extern char* strchr(const char* string, int character);
extern char* strrchr(const char* string, int character);
extern char* strpbrk(const char* text, const char* accept);
extern void memset(void*, int, size_t);
extern void memcpy(void*, const void*, size_t);
extern void* memmove(void*, const void*, size_t);
extern int memcmp(const void* s1, const void* s2, size_t n);
extern unsigned strlen(const char*);
extern char* strcpy(char*, const char*);
extern char* strcat(char* dest, const char* src);
extern char* strncat(char* dest, const char* src, size_t n);
extern char* getcwd(char* buffer, size_t size);

/************************************************************************/
/* fseek constants                                                      */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/************************************************************************/

extern void exit(int code);
extern void* malloc(size_t size);
extern void* realloc(void* pointer, size_t size);
extern void free(void* pointer);
extern int peekch(void);
extern int getch(void);
extern int getkey(void);
extern unsigned getkeymodifiers(void);
extern int sprintf(char* str, const char* fmt, ...);
extern int snprintf(char* str, size_t size, const char* fmt, ...);
extern int sscanf(const char* str, const char* fmt, ...);
extern int printf(const char* format, ...);
extern int _beginthread(void (*function)(void*), unsigned stack_size, void* arg_list);
extern void _endthread(void);
extern int system(const char*);
extern void sleep(unsigned ms);
extern int atoi(const char* str);
extern long strtol(const char* nptr, char** endptr, int base);
extern unsigned long strtoul(const char* nptr, char** endptr, int base);
extern long long strtoll(const char* nptr, char** endptr, int base);
extern unsigned long long strtoull(const char* nptr, char** endptr, int base);
extern float strtof(const char* nptr, char** endptr);
extern double strtod(const char* nptr, char** endptr);
extern long double strtold(const char* nptr, char** endptr);
extern void qsort(void* base, size_t num, size_t size, qsort_comparator compare);
extern int remove(const char* path);
extern int rename(const char* old_path, const char* new_path);
extern char* getenv(const char* name);
extern char* realpath(const char* path, char* resolved_path);
extern char* strerror(int error);
extern long double ldexpl(long double value, int exponent);

/************************************************************************/
// Character classification

extern int isalnum(int character);
extern int isalpha(int character);
extern int isascii(int character);
extern int isblank(int character);
extern int iscntrl(int character);
extern int isdigit(int character);
extern int isgraph(int character);
extern int islower(int character);
extern int isprint(int character);
extern int ispunct(int character);
extern int isspace(int character);
extern int isupper(int character);
extern int isxdigit(int character);
extern int toascii(int character);
extern int tolower(int character);
extern int toupper(int character);

/************************************************************************/

typedef struct __iobuf {
    unsigned char* _ptr;     /* next character position */
    int _cnt;                /* number of characters left */
    unsigned char* _base;    /* location of buffer */
    unsigned _flag;          /* mode of file access */
    unsigned _handle;        /* file handle */
    unsigned _bufsize;       /* size of buffer */
    unsigned char _ungotten; /* character placed here by ungetc */
    unsigned char _tmpfchar; /* tmpfile number */
} FILE;

/************************************************************************/

/* FILE_FIND_INFO comes from kernel/include/User.h via exos.h */

/************************************************************************/

extern FILE* fopen(const char*, const char*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fprintf(FILE* fp, const char* fmt, ...);
extern int vfprintf(FILE* fp, const char* fmt, va_list args);
extern int vsnprintf(char* buffer, size_t size, const char* fmt, va_list args);
extern int fputs(const char* text, FILE* fp);
extern int fputc(int character, FILE* fp);
extern int fseek(FILE*, long int, int);
extern long int ftell(FILE*);
extern int fclose(FILE*);
extern int feof(FILE*);
extern int fflush(FILE*);
extern int fgetc(FILE*);
extern int fgets(char* str, int num, FILE* fp);
extern FILE* fdopen(int file_descriptor, const char* mode);
extern FILE* freopen(const char* path, const char* mode, FILE* fp);
extern int open(const char* path, int flags, ...);
extern int close(int file_descriptor);
extern int read(int file_descriptor, void* buffer, unsigned count);
extern int write(int file_descriptor, const void* buffer, unsigned count);
extern long lseek(int file_descriptor, long offset, int whence);
extern int unlink(const char* path);
extern int execvp(const char* file, char* const arguments[]);

/************************************************************************/
// Math

extern float cos(float code);

/************************************************************************/
// POSIX Socket interface

int     socket(int domain, int type, int protocol);
int     bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     listen(int sockfd, int backlog);
int     accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
size_t  send(int sockfd, const void *buf, size_t len, int flags);
size_t  recv(int sockfd, void *buf, size_t len, int flags);
size_t  sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
size_t  recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int     shutdown(int sockfd, int how);
int     getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int     setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int     getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif
