
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


    Types

\************************************************************************/

#ifndef TYPES_H_INCLUDED
#define TYPES_H_INCLUDED


/************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************/
// Define __EXOS__

#define __EXOS__

/************************************************************************/
// Target architecture detection

#if defined(__i386__) || defined(_M_IX86)
    #define __EXOS_ARCH_X86_32__
    #define __EXOS_32__
#elif defined(__x86_64__) || defined(_M_X64)
    #define __EXOS_ARCH_X86_64__
    #define __EXOS_64__
#else
    #error "Unsupported target architecture for EXOS"
#endif

/************************************************************************/
// Check __SIZEOF_POINTER__ definition

#ifndef __SIZEOF_POINTER__
    #if defined(__EXOS_ARCH_X86_32__)
        #define __SIZEOF_POINTER__ 4
    #elif defined(__EXOS_ARCH_X86_64__)
        #define __SIZEOF_POINTER__ 8
    #else
        #error "Cannot determine pointer size for this architecture."
    #endif
#endif

/************************************************************************/
// Storage classes

#define CONST const
#define FAR far
#define PACKED __attribute__((packed))
#define NAKEDCALL __declspec(naked)
#define NORETURN __attribute__((noreturn))
#define EXOSAPI
#define APIENTRY
#define REGISTER register

#define SECTION(a) __attribute__((section(a)))
#define DATA_SECTION SECTION(".data")

/************************************************************************/
// Basic types

#if defined(__EXOS_32__)

    #if defined(_MSC_VER)
        typedef unsigned __int8   U8;
        typedef signed __int8     I8;
        typedef unsigned __int16  U16;
        typedef signed __int16    I16;
        typedef unsigned __int32  U32;
        typedef signed __int32    I32;
        typedef unsigned int      UINT;
        typedef signed int        INT;
    #elif defined(__GNUC__) || defined(__clang__)
        typedef unsigned char     U8;
        typedef signed char       I8;
        typedef unsigned short    U16;
        typedef signed short      I16;
        typedef unsigned int      U32;
        typedef signed int        I32;
        typedef unsigned int      UINT;
        typedef signed int        INT;
    #else
        #error "Unsupported compiler for Base.h"
    #endif

    typedef struct PACKED tag_U64 {
        U32 LO;
        U32 HI;
    } U64;

    typedef struct PACKED tag_I64 {
        U32 LO;
        I32 HI;
    } I64;

    #define U64_0           { .LO = 0, .HI = 0 }
    #define U64_EQUAL(a, b) ((a).LO == (b).LO && (a).HI == (b).HI)

#elif defined(__EXOS_64__)

    #if defined(_MSC_VER)
        typedef unsigned __int8       U8;
        typedef signed __int8         I8;
        typedef unsigned __int16      U16;
        typedef signed __int16        I16;
        typedef unsigned __int32      U32;
        typedef signed __int32        I32;
        typedef unsigned long long    U64;
        typedef signed long long      I64;
        typedef unsigned long long    UINT;
        typedef signed long long      INT;
    #elif defined(__GNUC__) || defined(__clang__)
        typedef unsigned char         U8;
        typedef signed char           I8;
        typedef unsigned short        U16;
        typedef signed short          I16;
        typedef unsigned int          U32;
        typedef signed int            I32;
        typedef unsigned long long    U64;
        typedef signed long long      I64;
        typedef unsigned long         UINT;
        typedef signed long           INT;
    #else
        #error "Unsupported compiler for Base.h"
    #endif

    #define U64_0           0
    #define U64_EQUAL(a, b) ((a) == (b))

#else
    #error "Unsupported EXOS target"
#endif

/************************************************************************/
// 48 bit values

typedef struct PACKED tag_U48 {
    U16 LO;
    U32 HI;
} U48;

/************************************************************************/
// 80 bit values

typedef struct PACKED tag_U80 {
    U16 LO;
    U64 HI;
} U80;

/************************************************************************/
// 128 bit values

typedef struct PACKED tag_U128 {
    U64 LO;
    U64 HI;
} U128;

/************************************************************************/
// Capacity for various bit sizes

#define MAX_U8 ((U8)0xFF)
#define MAX_U16 ((U16)0xFFFF)
#define MAX_U32 ((U32)0xFFFFFFFF)

#ifdef __EXOS_16__
    #define MAX_UINT MAX_U16
#endif

#ifdef __EXOS_32__
    #define MAX_UINT MAX_U32
#endif

#ifdef __EXOS_64__
    #define MAX_U64 ((U64)0xFFFFFFFFFFFFFFFF)
    #define MAX_UINT ((UINT)-1)
#endif

/************************************************************************/
// Floating point numbers

typedef float F32;              // 32 bit float
typedef double F64;             // 64 bit float

/************************************************************************/
// Some common types

typedef UINT SIZE;              // Size type
typedef UINT LINEAR;            // Linear virtual address, paged or not
typedef UINT PHYSICAL;          // Physical address

/************************************************************************/

typedef void* LPVOID;
typedef const void* LPCVOID;

/************************************************************************/

typedef void (*VOIDFUNC)(void);
typedef U32 (*TASKFUNC)(LPVOID Param);

/************************************************************************/
// Boolean type

typedef UINT BOOL;

#ifndef FALSE
#define FALSE ((BOOL)0)
#endif

#ifndef TRUE
#define TRUE ((BOOL)1)
#endif

/************************************************************************/
// NULL values

#ifndef NULL
#define NULL 0
#endif

#define NULL8 ((U8)0)
#define NULL16 ((U16)0)
#define NULL32 ((U32)0)
#define NULL64 ((U64)0)
#define NULL128 ((U128)0)

/************************************************************************/
// Time values

#define INFINITY MAX_U32

/************************************************************************/
// Handles - Replace pointers in userland to protect kernel

typedef UINT HANDLE;
typedef UINT SOCKET_HANDLE;

#define HANDLE_MINIMUM 0x10

#define LOOP_LIMIT 512

/************************************************************************/
// These macros give the offset of a structure member and true if a structure
// of a specified size contains the specified member

#define MEMBER_OFFSET(struc, member) ((UINT)(&(((struc*)NULL)->member)))
#define HAS_MEMBER(struc, member, struc_size) (MEMBER_OFFSET(struc, member) < struc_size)
#define ARRAY_COUNT(array) ((UINT)(sizeof(array) / sizeof((array)[0])))

/************************************************************************/
// ASCII string types

typedef U8 STR;
typedef CONST STR CSTR;
typedef STR* LPSTR;
typedef CONST STR* LPCSTR;

#define TEXT(a) ((LPCSTR)a)

/************************************************************************/
// Unicode string types

typedef U16 USTR;
typedef USTR* LPUSTR;
typedef CONST USTR* LPCUSTR;

/************************************************************************/
// Color types

typedef U32 COLOR;

/************************************************************************/
// Maximum string lengths

#define MAX_STRING_BUFFER 1024
#define MAX_COMMAND_LINE MAX_STRING_BUFFER
#define MAX_PATH_NAME MAX_STRING_BUFFER
#define MAX_FS_LOGICAL_NAME 64
#define MAX_FILE_NAME 256
#define MAX_USER_NAME 128
#define MAX_NAME 128
#define MAX_WINDOW_CAPTION 256
#define MAX_PASSWORD 64
#define MAX_COMMAND_NAME 64

/************************************************************************/
// Various macros

#define MAKE_VERSION(maj, min) ((U32)(((((U32)maj) & 0xFFFF) << 16) | (((U32)min) & 0xFFFF)))
#define UNSIGNED(val) *((U32*)(&(val)))
#define SIGNED(val) *((I32*)(&(val)))
#define UNUSED(x) (void)(x)

/************************************************************************/
// Color manipulations

#define MAKERGB(r, g, b) ((((COLOR)r & 0xFF) << 0x00) | (((COLOR)g & 0xFF) << 0x08) | (((COLOR)b & 0xFF) << 0x10))

#define MAKERGBA(r, g, b, a)                                                                   \
    ((((COLOR)r & 0xFF) << 0x00) | (((COLOR)g & 0xFF) << 0x08) | (((COLOR)b & 0xFF) << 0x10) | \
     (((COLOR)a & 0xFF) << 0x18))

#define SETRED(c, r) (((COLOR)c & 0xFFFFFF00) | ((COLOR)r << 0x00))
#define SETGREEN(c, g) (((COLOR)c & 0xFFFF00FF) | ((COLOR)g << 0x08))
#define SETBLUE(c, b) (((COLOR)c & 0xFF00FFFF) | ((COLOR)b << 0x10))
#define SETALPHA(c, a) (((COLOR)c & 0x00FFFFFF) | ((COLOR)a << 0x18))

/************************************************************************/

#ifdef __cplusplus
}
#endif

#endif  // TYPES_H_INCLUDED
