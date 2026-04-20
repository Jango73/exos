
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


    Base

\************************************************************************/

#ifndef BASE_H_INCLUDED
#define BASE_H_INCLUDED

#include "Types.h"

/************************************************************************/

#pragma pack(push, 1)

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************/
// Some machine constants

#define N_1B ((UINT)0x00000001)
#define N_2B ((UINT)0x00000002)
#define N_4B ((UINT)0x00000004)
#define N_8B ((UINT)0x00000008)
#define N_1KB ((UINT)0x00000400)
#define N_2KB ((UINT)0x00000800)
#define N_4KB ((UINT)0x00001000)
#define N_8KB ((UINT)0x00002000)
#define N_16KB ((UINT)0x00004000)
#define N_32KB ((UINT)0x00008000)
#define N_64KB ((UINT)0x00010000)
#define N_128KB ((UINT)0x00020000)
#define N_256KB ((UINT)0x00040000)
#define N_512KB ((UINT)0x00080000)
#define N_1MB ((UINT)0x00100000)
#define N_2MB ((UINT)0x00200000)
#define N_3MB ((UINT)0x00300000)
#define N_4MB ((UINT)0x00400000)
#define N_8MB ((UINT)0x00800000)
#define N_16MB ((UINT)0x01000000)
#define N_32MB ((UINT)0x02000000)
#define N_64MB ((UINT)0x04000000)
#define N_128MB ((UINT)0x08000000)
#define N_1GB ((UINT)0x40000000)
#define N_2GB ((UINT)0x80000000)
#define N_4GB ((UINT)0xFFFFFFFF)

#define N_1KB_M1 (N_1KB - 1)
#define N_4KB_M1 (N_4KB - 1)
#define N_1MB_M1 (N_1MB - 1)
#define N_4MB_M1 (N_4MB - 1)
#define N_1GB_M1 (N_1GB - 1)
#define N_2GB_M1 (N_2GB - 1)

#ifdef __EXOS_32__
    #define N_HalfMemory (MAX_U32 / 2)
    #define N_FullMemory (MAX_U32)
#endif

#ifdef __EXOS_64__
    #define N_HalfMemory (MAX_U64 / 2)
    #define N_FullMemory (MAX_U64)
#endif

/************************************************************************/
// Bitwise shift multipliers

#define MUL_2 1
#define MUL_4 2
#define MUL_8 3
#define MUL_16 4
#define MUL_32 5
#define MUL_64 6
#define MUL_128 7
#define MUL_256 8
#define MUL_512 9
#define MUL_1KB 10
#define MUL_2KB 11
#define MUL_4KB 12
#define MUL_8KB 13
#define MUL_16KB 14
#define MUL_32KB 15
#define MUL_64KB 16
#define MUL_128KB 17
#define MUL_256KB 18
#define MUL_512KB 19
#define MUL_1MB 20
#define MUL_2MB 21
#define MUL_4MB 22
#define MUL_8MB 23
#define MUL_16MB 24
#define MUL_32MB 25
#define MUL_64MB 26
#define MUL_128MB 27
#define MUL_256MB 28
#define MUL_512MB 29
#define MUL_1GB 30
#define MUL_2GB 31
#define MUL_4GB 32

/************************************************************************/
// Bit values

#define BIT_0 0x0001
#define BIT_1 0x0002
#define BIT_2 0x0004
#define BIT_3 0x0008
#define BIT_4 0x0010
#define BIT_5 0x0020
#define BIT_6 0x0040
#define BIT_7 0x0080
#define BIT_8 0x0100
#define BIT_9 0x0200
#define BIT_10 0x0400
#define BIT_11 0x0800
#define BIT_12 0x1000
#define BIT_13 0x2000
#define BIT_14 0x4000
#define BIT_15 0x8000

/************************************************************************/

#define BIT_0_VALUE(a) (((a) >> 0) & 1)
#define BIT_1_VALUE(a) (((a) >> 1) & 1)
#define BIT_2_VALUE(a) (((a) >> 2) & 1)
#define BIT_3_VALUE(a) (((a) >> 3) & 1)
#define BIT_4_VALUE(a) (((a) >> 4) & 1)
#define BIT_5_VALUE(a) (((a) >> 5) & 1)
#define BIT_6_VALUE(a) (((a) >> 6) & 1)
#define BIT_7_VALUE(a) (((a) >> 7) & 1)

#define BIT_8_VALUE(a) (((a) >> 8) & 1)
#define BIT_9_VALUE(a) (((a) >> 9) & 1)
#define BIT_10_VALUE(a) (((a) >> 10) & 1)
#define BIT_11_VALUE(a) (((a) >> 11) & 1)
#define BIT_12_VALUE(a) (((a) >> 12) & 1)
#define BIT_13_VALUE(a) (((a) >> 13) & 1)
#define BIT_14_VALUE(a) (((a) >> 14) & 1)
#define BIT_15_VALUE(a) (((a) >> 15) & 1)

/************************************************************************/

#ifdef __KERNEL__
extern void ConsolePrint(LPCSTR Format, ...);
#define CONSOLE_DEBUG(a, ...) { STR __Buf[128]; StringPrintFormat(__Buf, a, ##__VA_ARGS__); ConsolePrint(__Buf); }
#endif

/************************************************************************/
// Common ASCII character values

#define STR_NULL ((STR)'\0')
#define STR_RETURN ((STR)'\r')
#define STR_NEWLINE ((STR)'\n')
#define STR_TAB ((STR)'\t')
#define STR_SPACE ((STR)' ')
#define STR_QUOTE ((STR)'"')
#define STR_SCORE ((STR)'_')
#define STR_DOT ((STR)'.')
#define STR_COLON ((STR)':')
#define STR_SLASH ((STR)'/')
#define STR_BACKSLASH ((STR)'\\')
#define STR_PLUS ((STR)'+')
#define STR_MINUS ((STR)'-')

/************************************************************************/
// Common Unicode character values

#define USTR_NULL ((USTR)'\0')
#define USTR_RETURN ((USTR)'\r')
#define USTR_NEWLINE ((USTR)'\n')
#define USTR_TAB ((USTR)'\t')
#define USTR_SPACE ((USTR)' ')
#define USTR_QUOTE ((USTR)'"')
#define USTR_SCORE ((USTR)'_')
#define USTR_DOT ((USTR)'.')
#define USTR_COLON ((USTR)':')
#define USTR_SLASH ((USTR)'/')
#define USTR_BACKSLASH ((USTR)'\\')
#define USTR_PLUS ((USTR)'+')
#define USTR_MINUS ((USTR)'-')

/************************************************************************/
// Common color values

#define COLOR_BLACK ((COLOR)0x00000000)
#define COLOR_GRAY15 ((COLOR)0x00262626)
#define COLOR_GRAY20 ((COLOR)0x00333333)
#define COLOR_GRAY25 ((COLOR)0x00404040)
#define COLOR_GRAY30 ((COLOR)0x004D4D4D)
#define COLOR_GRAY35 ((COLOR)0x00595959)
#define COLOR_GRAY40 ((COLOR)0x00666666)
#define COLOR_GRAY50 ((COLOR)0x00808080)
#define COLOR_GRAY75 ((COLOR)0x00C0C0C0)
#define COLOR_GRAY80 ((COLOR)0x00CCCCCC)
#define COLOR_GRAY90 ((COLOR)0x00E6E6E6)
#define COLOR_WHITE ((COLOR)0x00FFFFFF)
#define COLOR_RED ((COLOR)0x000000FF)
#define COLOR_GREEN ((COLOR)0x0000FF00)
#define COLOR_BLUE ((COLOR)0x00FF0000)
#define COLOR_DARK_RED ((COLOR)0x00000080)
#define COLOR_DARK_GREEN ((COLOR)0x00008000)
#define COLOR_DARK_BLUE ((COLOR)0x00800000)
#define COLOR_LIGHT_RED ((COLOR)0x008080FF)
#define COLOR_LIGHT_GREEN ((COLOR)0x0080FF80)
#define COLOR_LIGHT_BLUE ((COLOR)0x00FF8080)
#define COLOR_YELLOW ((COLOR)0x0000FFFF)
#define COLOR_CYAN ((COLOR)0x00FFFF00)
#define COLOR_PURPLE ((COLOR)0x00FF00FF)
#define COLOR_BROWN ((COLOR)0x00008080)
#define COLOR_DARK_CYAN ((COLOR)0x00808000)

/************************************************************************/
// Utility macros

#define SAFE_USE(a) if ((a) != NULL)
#define SAFE_USE_2(a, b) if ((a) != NULL && (b) != NULL)
#define SAFE_USE_3(a, b, c) if ((a) != NULL && (b) != NULL && (c) != NULL)
#define SAFE_USE_ID(a, i) if ((a) != NULL && (a->TypeID == i))
#define SAFE_USE_ID_2(a, b, i) if ((a) != NULL && (a->TypeID == i) && (b) != NULL && (b->TypeID == i))
#define SAFE_USE_VALID(a) if ((a) != NULL && IsValidMemory((LINEAR)a))
#define SAFE_USE_VALID_2(a, b) if ((a) != NULL && IsValidMemory((LINEAR)a) && (b) != NULL && IsValidMemory((LINEAR)b))
#define SAFE_USE_VALID_ID(a, i) if ((a) != NULL && IsValidMemory((LINEAR)a) && ((a)->TypeID == i))
#define SAFE_USE_VALID_ID_2(a, b, i) if ((a) != NULL && IsValidMemory((LINEAR)a) && ((a)->TypeID == i) \
        && ((b) != NULL && IsValidMemory((LINEAR)b) && ((b)->TypeID == i)))

// This is called before dereferencing a user-provided pointer to a parameter structure
#define SAFE_USE_INPUT_POINTER(p, s) if ((p) != NULL && IsValidMemory((LINEAR)p) && (p)->Header.Size >= sizeof(s))

#ifdef CONFIG_VMA_KERNEL
    #define IS_VALID_KERNEL_POINTER(Value) (((UINT)(Value) != 0) && ((UINT)(Value) >= (UINT)(CONFIG_VMA_KERNEL)))
#else
    #define IS_VALID_KERNEL_POINTER(Value) ((UINT)(Value) != 0)
#endif

// Do an infinite loop
#define FOREVER while(1)

// Put CPU to sleep forever: disable IRQs, halt, and loop.
#define DO_THE_SLEEPING_BEAUTY \
    do {                       \
        __asm__ __volatile__(  \
            "1:\n\t"           \
            "cli\n\t"          \
            "hlt\n\t"          \
            "jmp 1b\n\t"       \
            :                  \
            :                  \
            : "memory");       \
    } while (0)

#define STRINGS_EQUAL(a,b) (StringCompare(a,b)==0)
#define STRINGS_EQUAL_NO_CASE(a,b) (StringCompareNC(a,b)==0)

/************************************************************************/
// Forward declaration to avoid circular dependencies

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef void (*OBJECTDESTRUCTOR)(LPVOID);

/************************************************************************/
// A kernel object header

#define OBJECT_FIELDS       \
    UINT TypeID;            \
    UINT References;        \
    U64 InstanceID;         \
    LPPROCESS OwnerProcess; \
    OBJECTDESTRUCTOR Destructor; \

typedef struct tag_OBJECT {
    OBJECT_FIELDS
} OBJECT, *LPOBJECT;

/************************************************************************/
// 64 bits math

#ifdef __EXOS_32__

// Make U64 from hi/lo
static inline U64 U64_Make(U32 hi, U32 lo) {
    U64 v;
    v.HI = hi;
    v.LO = lo;
    return v;
}

// Add two U64
static inline U64 U64_Add(U64 a, U64 b) {
    U64 r;
    U32 lo = a.LO + b.LO;
    U32 carry = (lo < a.LO) ? 1 : 0;
    r.LO = lo;
    r.HI = a.HI + b.HI + carry;
    return r;
}

// Subtract b from a
static inline U64 U64_Sub(U64 a, U64 b) {
    U64 r;
    U32 borrow = (a.LO < b.LO) ? 1 : 0;
    r.LO = a.LO - b.LO;
    r.HI = a.HI - b.HI - borrow;
    return r;
}

// Multiply two U32 values and return U64
static inline U64 U64_MultiplyU32(U32 Left, U32 Right) {
    U64 Result = U64_0;
    U64 Addend;

    Addend.LO = Left;
    Addend.HI = 0;

    while (Right != 0) {
        if ((Right & 1) != 0) {
            Result = U64_Add(Result, Addend);
        }

        Right >>= 1;
        if (Right != 0) {
            U32 AddendHigh = Addend.HI;
            U32 AddendLow = Addend.LO;
            Addend = U64_Make((AddendHigh << 1) | (AddendLow >> 31), AddendLow << 1);
        }
    }

    return Result;
}

// Divide U64 by U32 and return U64 quotient
static inline U64 U64_DivideByU32(U64 Dividend, U32 Divisor, U32* Remainder) {
    U64 Quotient = U64_0;
    U32 CurrentRemainder = 0;
    UINT BitIndex;

    if (Divisor == 0) {
        if (Remainder != NULL) {
            *Remainder = 0;
        }
        return Quotient;
    }

    for (BitIndex = 0; BitIndex < 64; BitIndex++) {
        U32 Shift = (U32)(63 - BitIndex);
        U32 NextBit;

        if (Shift >= 32) {
            NextBit = (Dividend.HI >> (Shift - 32)) & 1;
        } else {
            NextBit = (Dividend.LO >> Shift) & 1;
        }

        CurrentRemainder = (CurrentRemainder << 1) | NextBit;
        if (CurrentRemainder >= Divisor) {
            CurrentRemainder -= Divisor;
            if (Shift >= 32) {
                Quotient.HI |= 1 << (Shift - 32);
            } else {
                Quotient.LO |= 1 << Shift;
            }
        }
    }

    if (Remainder != NULL) {
        *Remainder = CurrentRemainder;
    }

    return Quotient;
}

// Compare: return -1 if a<b, 0 if a==b, 1 if a>b
static inline int U64_Cmp(U64 a, U64 b) {
    if (a.HI < b.HI) return -1;
    if (a.HI > b.HI) return 1;
    if (a.LO < b.LO) return -1;
    if (a.LO > b.LO) return 1;
    return 0;
}

// Convert U64 to 32-bit if <= 0xFFFFFFFF, else clip
static inline U32 U64_ToU32_Clip(U64 v) {
    if (v.HI != 0) return 0xFFFFFFFF;
    return v.LO;
}

// Helper functions for U64 operations in CRC64
static inline U64 U64_ShiftRight1(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 1) | ((Value.HI & 1) << 31);
    Result.HI = Value.HI >> 1;
    return Result;
}

static inline U64 U64_Xor(U64 A, U64 B) {
    U64 Result;
    Result.LO = A.LO ^ B.LO;
    Result.HI = A.HI ^ B.HI;
    return Result;
}

static inline BOOL U64_IsOdd(U64 Value) { return (Value.LO & 1) != 0; }

static inline U64 U64_FromU32(U32 Value) {
    U64 Result;
    Result.LO = Value;
    Result.HI = 0;
    return Result;
}

static inline U64 U64_FromUINT(UINT Value) {
    U64 Result;
    Result.LO = Value;
    Result.HI = 0;
    return Result;
}

static inline UINT U64_ToUINT(U64 Value) {
    return (UINT)Value.LO;
}

static inline U64 U64_ShiftRight8(U64 Value) {
    U64 Result;
    Result.LO = (Value.LO >> 8) | ((Value.HI & 0xFF) << 24);
    Result.HI = Value.HI >> 8;
    return Result;
}

static inline U32 U64_High32(U64 Value) {
    return Value.HI;
}

static inline U32 U64_Low32(U64 Value) {
    return Value.LO;
}

#else

// Make U64 from hi/lo
static inline U64 U64_Make(U32 hi, U32 lo) {
    return ((U64)hi << 32) | (U64)lo;
}

// Add two U64
static inline U64 U64_Add(U64 a, U64 b) {
    return a + b;
}

// Subtract b from a
static inline U64 U64_Sub(U64 a, U64 b) {
    return a - b;
}

// Multiply two U32 values and return U64
static inline U64 U64_MultiplyU32(U32 Left, U32 Right) {
    return (U64)Left * (U64)Right;
}

// Divide U64 by U32 and return U64 quotient
static inline U64 U64_DivideByU32(U64 Dividend, U32 Divisor, U32* Remainder) {
    U64 Quotient = U64_0;

    if (Divisor == 0) {
        if (Remainder != NULL) {
            *Remainder = 0;
        }
        return Quotient;
    }

    Quotient = Dividend / (U64)Divisor;
    if (Remainder != NULL) {
        *Remainder = (U32)(Dividend % (U64)Divisor);
    }
    return Quotient;
}

// Compare: return -1 if a<b, 0 if a==b, 1 if a>b
static inline int U64_Cmp(U64 a, U64 b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

// Convert U64 to 32-bit if <= 0xFFFFFFFF, else clip
static inline U32 U64_ToU32_Clip(U64 v) {
    return (v > 0xFFFFFFFF) ? 0xFFFFFFFF : (U32)v;
}

// Helper functions for U64 operations in CRC64
static inline U64 U64_ShiftRight1(U64 Value) {
    return Value >> 1;
}

static inline U64 U64_Xor(U64 A, U64 B) {
    return A ^ B;
}

static inline BOOL U64_IsOdd(U64 Value) { return (Value & 1) != 0; }

static inline U64 U64_FromU32(U32 Value) {
    return (U64)Value;
}

static inline U64 U64_FromUINT(UINT Value) {
    return (U64)Value;
}

static inline UINT U64_ToUINT(U64 Value) {
    return (UINT)Value;
}

static inline U64 U64_ShiftRight8(U64 Value) {
    return Value >> 8;
}

static inline U32 U64_High32(U64 Value) {
    return (U32)(Value >> 32);
}

static inline U32 U64_Low32(U64 Value) {
    return (U32)(Value & 0xFFFFFFFF);
}

#endif

#define U64_MUL_U32(Left, Right) U64_MultiplyU32((Left), (Right))
#define U64_DIV_U32(Dividend, Divisor, Remainder) U64_DivideByU32((Dividend), (Divisor), (Remainder))

/************************************************************************/
// Logging macros

#ifdef __KERNEL__

#if DEBUG_OUTPUT == 1
    #define DEBUG(a, ...) KernelLogText(LOG_DEBUG, (a), ##__VA_ARGS__)
    #define TEST(a, ...) KernelLogText(LOG_TEST, (a), ##__VA_ARGS__)
#else
    #define DEBUG(a, ...)
    #define TEST(a, ...)
#endif

#if SCHEDULING_DEBUG_OUTPUT == 1
    #define FINE_DEBUG(a, ...) DEBUG(a, ##__VA_ARGS__)
#else
    #define FINE_DEBUG(a, ...)
#endif

#define VERBOSE(a, ...) KernelLogText(LOG_VERBOSE, (a), ##__VA_ARGS__)
#define WARNING(a, ...) KernelLogText(LOG_WARNING, (a), ##__VA_ARGS__)
#define ERROR(a, ...) KernelLogText(LOG_ERROR, (a), ##__VA_ARGS__)

#else   // __KERNEL__

#if DEBUG_OUTPUT == 1
    #define DEBUG(a, ...) debug((a), ##__VA_ARGS__)
    #define TEST(a, ...)
#else
    #define DEBUG(a, ...)
    #define TEST(a, ...)
#endif

#if SCHEDULING_DEBUG_OUTPUT == 1
    #define FINE_DEBUG(a, ...) DEBUG(a, ##__VA_ARGS__)
#else
    #define FINE_DEBUG(a, ...)
#endif

#define VERBOSE(a, ...)
#define WARNING(a, ...)
#define ERROR(a, ...)

#endif  // __KERNEL__

/************************************************************************/

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline U16 Htons(U16 Value) { return Value; }

static inline U16 Ntohs(U16 Value) { return Value; }

static inline U32 Htonl(U32 Value) { return Value; }

static inline U32 Ntohl(U32 Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline U16 Htons(U16 Value) { return (U16)((Value << 8) | (Value >> 8)); }

static inline U16 Ntohs(U16 Value) { return Htons(Value); }

static inline U32 Htonl(U32 Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}

static inline U32 Ntohl(U32 Value) { return Htonl(Value); }

#else
    #error "Endianness not defined"
#endif  // defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

/************************************************************************/

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif  // BASE_H_INCLUDED
