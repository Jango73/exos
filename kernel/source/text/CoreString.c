
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


    String

\************************************************************************/

#include "text/CoreString.h"

#include "Base.h"
#include "VarArg.h"

/***************************************************************************/

/**
 * @brief Fills a memory region with a single byte value.
 *
 * Writes the lower 8 bits of the What parameter into each byte of the
 * destination buffer for the requested size.
 *
 * @param Destination Pointer to the buffer to fill
 * @param What Value whose low byte will be written
 * @param Size Number of bytes to fill
 */
void MemorySet(LPVOID Destination, UINT What, UINT Size) {
    SAFE_USE(Destination) {
        U8* Dst = (U8*)Destination;
        U8 Value = (U8)(What & 0xFF);

        for (UINT Index = 0; Index < Size; Index++) {
            Dst[Index] = Value;
        }
    }
}

/***************************************************************************/

#ifndef __KERNEL__
/**
 * @brief Copies a block of memory from source to destination.
 *
 * Performs a byte-by-byte copy of Size bytes. Behaviour is undefined when
 * buffers overlap; use MemoryMove for overlapping regions.
 *
 * @param Destination Pointer to the destination buffer
 * @param Source Pointer to the source buffer
 * @param Size Number of bytes to copy
 */
void MemoryCopy(LPVOID Destination, LPCVOID Source, UINT Size) {
    SAFE_USE_2(Destination, Source) {
        if (Size == 0 || Destination == Source) {
            return;
        }

        U8* Dst = (U8*)Destination;
        const U8* Src = (const U8*)Source;

        for (UINT Index = 0; Index < Size; Index++) {
            Dst[Index] = Src[Index];
        }
    }
}
#endif

/***************************************************************************/

/**
 * @brief Compares two memory buffers.
 *
 * Compares Size bytes and returns -1, 0 or 1 depending on the lexical order
 * of the buffers.
 *
 * @param First Pointer to the first buffer
 * @param Second Pointer to the second buffer
 * @param Size Number of bytes to compare
 * @return Comparison result (-1, 0, 1)
 */
INT MemoryCompare(LPCVOID First, LPCVOID Second, UINT Size) {
    if (Size == 0 || First == Second) return 0;

    SAFE_USE_2(First, Second) {
        const U8* Ptr1 = (const U8*)First;
        const U8* Ptr2 = (const U8*)Second;

        INT Result = 0;

#ifdef __KERNEL__
        UINT Count = Size;
        #if defined(__EXOS_ARCH_X86_64__)
        __asm__ __volatile__(
            "xor %%eax, %%eax\n"
            "cld\n"
            "repe cmpsb\n"
            "je 2f\n"
            "movzbl -1(%1), %%eax\n"
            "movzbl -1(%2), %%edx\n"
            "cmp %%edx, %%eax\n"
            "ja 0f\n"
            "jb 1f\n"
            "0:\n"
            "mov $1, %%eax\n"
            "jmp 2f\n"
            "1:\n"
            "mov $-1, %%eax\n"
            "2:\n"
            : "=&a"(Result), "+S"(Ptr1), "+D"(Ptr2), "+c"(Count)
            :
            : "rdx", "cc", "memory");
    #else
        __asm__ __volatile__(
            "xor %%eax, %%eax\n"
            "cld\n"
            "repe cmpsb\n"
            "je 2f\n"
            "movzbl -1(%1), %%eax\n"
            "movzbl -1(%2), %%edx\n"
            "cmp %%edx, %%eax\n"
            "ja 0f\n"
            "jb 1f\n"
            "0:\n"
            "mov $1, %%eax\n"
            "jmp 2f\n"
            "1:\n"
            "mov $-1, %%eax\n"
            "2:\n"
            : "=&a"(Result), "+S"(Ptr1), "+D"(Ptr2), "+c"(Count)
            :
            : "edx", "cc", "memory");
    #endif
#else
        for (UINT Index = 0; Index < Size; Index++) {
            if (Ptr1[Index] != Ptr2[Index]) {
                return (Ptr1[Index] < Ptr2[Index]) ? -1 : 1;
            }
        }
#endif

        return Result;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Moves a block of memory, handling overlapping regions.
 *
 * Copies Size bytes from Source to Destination using forward or backward
 * iteration depending on the layout of the buffers to ensure correct results
 * when they overlap.
 *
 * @param Destination Pointer to the destination buffer
 * @param Source Pointer to the source buffer
 * @param Size Number of bytes to move
 */
#ifndef __KERNEL__
void MemoryMove(LPVOID Destination, LPCVOID Source, UINT Size) {
    if (Destination == Source || Size == 0) return;

    SAFE_USE_2(Destination, Source) {
        U8* Dst = (U8*)Destination;
        const U8* Src = (const U8*)Source;

        const U8* SrcEnd = Src + Size;

        if (Dst < Src || Dst >= SrcEnd) {
            for (UINT Index = 0; Index < Size; Index++) {
                Dst[Index] = Src[Index];
            }
        } else {
            UINT Index = Size;
            while (Index > 0) {
                Index--;
                Dst[Index] = Src[Index];
            }
        }
    }
}
#endif

/***************************************************************************/

/**
 * @brief Tests if a character is alphabetic.
 *
 * @param Char Character to test
 * @return TRUE if character is a-z or A-Z, FALSE otherwise
 */
BOOL IsAlpha(STR Char) {
    if ((Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z')) return TRUE;

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Tests if a character is numeric.
 *
 * @param Char Character to test
 * @return TRUE if character is 0-9, FALSE otherwise
 */
BOOL IsNumeric(STR Char) {
    if (Char >= '0' && Char <= '9') return TRUE;

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Tests if a character is alphanumeric.
 *
 * @param Char Character to test
 * @return TRUE if character is a-z, A-Z, or 0-9, FALSE otherwise
 */
BOOL IsAlphaNumeric(STR Char) {
    if (IsAlpha(Char) || IsNumeric(Char)) return TRUE;

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Converts a character to lowercase.
 *
 * @param Char Character to convert
 * @return Lowercase version of character, unchanged if not uppercase
 */
STR CharToLower(STR Char) {
    if (Char >= 'A' && Char <= 'Z') Char = 'a' + (Char - 'A');
    return Char;
}

/***************************************************************************/

/**
 * @brief Converts a character to uppercase.
 *
 * @param Char Character to convert
 * @return Uppercase version of character, unchanged if not lowercase
 */
STR CharToUpper(STR Char) {
    if (Char >= 'a' && Char <= 'z') Char = 'A' + (Char - 'a');
    return Char;
}

/***************************************************************************/

/**
 * @brief Tests if a string is empty or null.
 *
 * @param Src String to test
 * @return TRUE if string is NULL or first character is null terminator
 */
BOOL StringEmpty(LPCSTR Src) {
    if (Src == NULL) return TRUE;
    return Src[0] == STR_NULL;
}

/***************************************************************************/

/**
 * @brief Calculates the length of a null-terminated string.
 *
 * This function has a safety limit of 8192 characters to prevent infinite
 * loops on corrupted strings. Issues warnings if limit is exceeded.
 *
 * @param Src String to measure (can be NULL)
 * @return Length of string in characters, 0 if NULL or empty
 */
UINT StringLength(LPCSTR Src) {
    UINT Index = 0;
    UINT Size = 0;

    SAFE_USE(Src) {
        // Count characters until null terminator or safety limit
        for (Index = 0; Index < 8192; Index++) {
            if (*Src == STR_NULL) break;  // Found end of string
            Src++;                        // Move to next character
            Size++;                       // Count this character
        }
    }

    return Size;
}

/***************************************************************************/

void StringClear(LPSTR Str) {
    SAFE_USE(Str) {
        Str[0] = STR_NULL;
    }
}

/***************************************************************************/

/**
 * @brief Copies a null-terminated string from source to destination.
 *
 * Copies characters one by one including the null terminator.
 * No bounds checking is performed on destination buffer.
 *
 * @param Dst Destination buffer (must be large enough)
 * @param Src Source string to copy (ignored if NULL)
 */
void StringCopy(LPSTR Dst, LPCSTR Src) {
    U32 Index;

    if (Dst && Src) {
        // Copy characters until null terminator is found and copied
        for (Index = 0; Index < MAX_U32; Index++) {
            Dst[Index] = Src[Index];            // Copy current character
            if (Src[Index] == STR_NULL) break;  // Stop after copying null terminator
        }
    }
}

/***************************************************************************/

void StringCopyLimit(LPSTR Dst, LPCSTR Src, UINT MaxLength) {
    UINT Index;

    if (Dst && Src) {
        // Copy characters until null terminator is found and copied
        // or until MaxLength is reached
        for (Index = 0; Index < MAX_U32; Index++) {
            if (Index >= MaxLength - 1) {
                Dst[Index] = STR_NULL;
                break;
            }
            Dst[Index] = Src[Index];            // Copy current character
            if (Src[Index] == STR_NULL) break;  // Stop after copying null terminator
        }
    }
}

/***************************************************************************/

/**
 * @brief Copies a fixed number of characters from source to destination.
 *
 * Does not add null terminator and may not result in a valid string.
 * Useful for copying binary data or fixed-width fields.
 *
 * @param Dst Destination buffer
 * @param Src Source data to copy
 * @param Len Number of characters to copy
 */
void StringCopyNum(LPSTR Dst, LPCSTR Src, UINT Len) {
    UINT Index;

    if (Dst && Src) {
        // Copy exactly Len characters, regardless of null terminators
        for (Index = 0; Index < Len; Index++) {
            Dst[Index] = Src[Index];
        }
    }
}

/***************************************************************************/

/**
 * @brief Concatenates (appends) a source string to a destination string.
 *
 * Finds the end of the destination string and appends the source string there.
 * The destination buffer must be large enough to hold both strings.
 *
 * @param Dst Destination string to append to
 * @param Src Source string to append
 */
void StringConcat(LPSTR Dst, LPCSTR Src) {
    LPSTR DstPtr = NULL;
    LPCSTR SrcPtr = NULL;

    if (Dst && Src) {
        SrcPtr = Src;
        DstPtr = Dst + StringLength(Dst);  // Find end of destination string
        StringCopy(DstPtr, SrcPtr);        // Copy source to end of destination
    }
}

/***************************************************************************/

/**
 * @brief Compares two null-terminated strings lexicographically.
 *
 * Compares strings character by character until a difference is found
 * or one string ends.
 *
 * @param Text1 First string to compare
 * @param Text2 Second string to compare
 * @return 0 if equal, <0 if Text1 < Text2, >0 if Text1 > Text2
 */
INT StringCompare(LPCSTR Text1, LPCSTR Text2) {
    REGISTER I8 Result;

    FOREVER {
        Result = *Text1 - *Text2;                      // Compare current characters
        if (Result != 0 || *Text1 == STR_NULL) break;  // Stop if different or end reached
        Text1++;                                       // Move to next character
        Text2++;
    }

    return (INT)Result;
}

/***************************************************************************/

/**
 * @brief Compares two strings lexicographically, ignoring case.
 *
 * Case-insensitive version of StringCompare. Converts both characters
 * to lowercase before comparison.
 *
 * @param Text1 First string to compare
 * @param Text2 Second string to compare
 * @return 0 if equal, <0 if Text1 < Text2, >0 if Text1 > Text2
 */
INT StringCompareNC(LPCSTR Text1, LPCSTR Text2) {
    REGISTER I8 Result;

    FOREVER {
        Result = CharToLower(*Text1) - CharToLower(*Text2);  // Compare lowercase versions
        if (Result != 0 || *Text1 == STR_NULL) break;        // Stop if different or end reached
        Text1++;                                             // Move to next character
        Text2++;
    }

    return (INT)Result;
}

/***************************************************************************/

/**
 * @brief Determines whether a string contains another string.
 *
 * Performs a case-sensitive substring search and returns TRUE when Search is
 * found in Text.
 *
 * @param Text Source string.
 * @param Search Substring to locate.
 * @return TRUE when Search is found, FALSE otherwise.
 */
BOOL StringContains(LPCSTR Text, LPCSTR Search) {
    if (Text == NULL || Search == NULL) {
        return FALSE;
    }

    if (Search[0] == STR_NULL) {
        return TRUE;
    }

    while (*Text != STR_NULL) {
        U32 Index = 0;

        while (Text[Index] != STR_NULL &&
               Search[Index] != STR_NULL &&
               Text[Index] == Search[Index]) {
            Index++;
        }

        if (Search[Index] == STR_NULL) {
            return TRUE;
        }

        Text++;
    }

    return FALSE;
}

/***************************************************************************/

LPSTR StringToLower(LPSTR Src) {
    LPSTR SrcPtr = Src;

    if (SrcPtr) {
        while (*SrcPtr) {
            *SrcPtr = CharToLower(*SrcPtr);
            SrcPtr++;
        }
    }

    return Src;
}

/***************************************************************************/

LPSTR StringToUpper(LPSTR Src) {
    LPSTR SrcPtr = Src;

    if (SrcPtr) {
        while (*SrcPtr) {
            *SrcPtr = CharToUpper(*SrcPtr);
            SrcPtr++;
        }
    }

    return Src;
}

/***************************************************************************/

/**
 * @brief Finds the first occurrence of a character in a string.
 *
 * Searches the string from left to right for the first occurrence of the
 * specified character. Returns a pointer to the found character or NULL if
 * the character is not found or the string ends before finding it.
 *
 * @param Text String to search in
 * @param Char Character to search for
 * @return Pointer to first occurrence of character, or NULL if not found
 */
LPSTR StringFindChar(LPCSTR Text, STR Char) {
    // Scan through string until we find the character or reach end
    for (; *Text != Char; Text++) {
        if (*Text == STR_NULL) return NULL;  // End of string, not found
    }

    return (LPSTR)Text;  // Found - return pointer to character
}

/***************************************************************************/

/**
 * @brief Finds the last occurrence of a character in a string (reverse search).
 *
 * Searches the string from right to left for the last occurrence of the
 * specified character. Returns a pointer to the found character or NULL if
 * the character is not found.
 *
 * @param Text String to search in
 * @param Char Character to search for
 * @return Pointer to last occurrence of character, or NULL if not found
 */
LPSTR StringFindCharR(LPCSTR Text, STR Char) {
    // Start at end of string and work backwards
    LPCSTR Ptr = Text + StringLength(Text);

    do {
        if (*Ptr == Char) return (LPSTR)Ptr;  // Found character
    } while (--Ptr >= Text);                  // Continue until start of string

    return NULL;  // Character not found
}

/***************************************************************************/

/**
 * @brief Reverses the characters in a string in place.
 *
 * Creates a reversed copy of the input string and copies it back to the
 * original location. Uses a temporary buffer limited to 256 characters.
 *
 * @param Text String to reverse (modified in place)
 */
void StringInvert(LPSTR Text) {
    STR Temp[256];  // Temporary buffer for reversed string
    U32 Length = StringLength(Text);
    U32 Index1 = 0;
    U32 Index2 = Length - 1;

    // Copy characters from end to beginning
    for (Index1 = 0; Index1 < Length;) {
        Temp[Index1++] = Text[Index2--];  // Copy char from end to temp
    }

    Temp[Index1] = STR_NULL;  // Null-terminate reversed string

    StringCopy(Text, Temp);  // Copy reversed string back to original
}

/***************************************************************************/

/**
 * @brief Converts a 32-bit unsigned integer to decimal string representation.
 *
 * Converts the number by extracting digits from right to left (least to most
 * significant), then reverses the result string to get proper order.
 * Special case handling for zero.
 *
 * @param Number 32-bit unsigned integer to convert
 * @param Text Buffer to store resulting decimal string
 */
void U32ToString(U32 Number, LPSTR Text) {
    U32 Index = 0;

    // Special case: zero
    if (Number == 0) {
        Text[0] = '0';
        Text[1] = STR_NULL;
        return;
    }

    // Extract digits from right to left (reverse order)
    while (Number) {
        Text[Index++] = (STR)'0' + (Number % 10);  // Convert digit to ASCII
        Number /= 10;                              // Move to next digit
    }

    Text[Index] = STR_NULL;  // Null-terminate

    StringInvert(Text);  // Reverse to get correct order
}

/***************************************************************************/

static STR HexDigitLo[] = "0123456789abcdef";
static STR HexDigitHi[] = "0123456789ABCDEF";

/***************************************************************************/

#define U32_NUM_BITS 32
#define U32_DIGIT_BITS 4
#define U32_NUM_DIGITS (U32_NUM_BITS / U32_DIGIT_BITS)

/**
 * @brief Converts a 32-bit unsigned integer to uppercase hexadecimal string.
 *
 * Converts the number to 8-character hexadecimal representation using uppercase
 * letters (A-F). Processes 4 bits at a time from most significant to least
 * significant, producing a fixed-width output.
 *
 * @param Number 32-bit unsigned integer to convert
 * @param Text Buffer to store resulting hex string (must be at least 9 chars)
 */
void U32ToHexString(U32 Number, LPSTR Text) {
    U32 Index = 0;
    U32 Value = 0;
    U32 Shift = U32_NUM_DIGITS - 1;  // Start with most significant digit

    if (Text == NULL) return;

    // Process each 4-bit nibble from most to least significant
    for (Index = 0; Index < U32_NUM_DIGITS; Index++) {
        Value = (Number >> (Shift * U32_DIGIT_BITS)) & 0xF;  // Extract 4 bits
        Text[Index] = HexDigitHi[Value];                     // Convert to uppercase hex digit
        Shift--;                                             // Move to next less significant nibble
    }

    Text[Index] = STR_NULL;  // Null-terminate result
}

/***************************************************************************/

/**
 * @brief Converts a hexadecimal string to a 32-bit unsigned integer.
 *
 * Parses hexadecimal strings in "0x" or "0X" format. Supports both uppercase
 * and lowercase hex digits (A-F, a-f). Returns 0 for invalid format or
 * non-hex characters.
 *
 * @param Text Hexadecimal string starting with "0x" or "0X"
 * @return Parsed 32-bit unsigned integer, or 0 if invalid format
 */
U32 HexStringToU32(LPCSTR Text) {
    U32 c, d, Length, Value, Temp, Shift;

    // Must start with "0x" or "0X"
    if (Text[0] != '0') return 0;
    if (Text[1] != 'x' && Text[1] != 'X') return 0;

    Text += 2;  // Skip "0x" prefix
    Length = StringLength(Text);
    if (Length == 0) return 0;  // No hex digits after prefix

    // Process each hex digit from most to least significant
    for (c = 0, Value = 0, Shift = 4 * (Length - 1); c < Length; c++) {
        U32 FoundDigit = 0;

        // Look up character in both lowercase and uppercase hex tables
        for (d = 0; d < 16; d++) {
            if (Text[c] == HexDigitLo[d]) {  // Check lowercase a-f
                Temp = d;
                FoundDigit = 1;
                break;
            }
            if (Text[c] == HexDigitHi[d]) {  // Check uppercase A-F
                Temp = d;
                FoundDigit = 1;
                break;
            }
        }

        if (FoundDigit == 0) return 0;  // Invalid hex character

        Value += (Temp << Shift);  // Add digit value at correct bit position
        Shift -= 4;                // Move to next less significant nibble
    }

    return Value;
}

/***************************************************************************/

/**
 * @brief Converts a decimal string to a 32-bit signed integer.
 *
 * Parses decimal strings by processing digits from right to left (least to
 * most significant). Returns 0 for empty strings or invalid characters.
 * Does not handle negative numbers or signs.
 *
 * @param Text Decimal string to convert
 * @return Parsed 32-bit signed integer, or 0 if invalid format
 */
I32 StringToI32(LPCSTR Text) {
    I32 Value = 0;
    U32 Index = 0;
    U32 Power = 1;  // Decimal place multiplier (1, 10, 100, etc.)
    STR Data = 0;

    if (Text[0] == STR_NULL) return 0;  // Empty string

    Index = StringLength(Text) - 1;  // Start from rightmost digit

    // Process digits from right to left
    FOREVER {
        Data = Text[Index];
        if (IsNumeric(Data) == 0) return 0;  // Invalid character found

        Value += (Data - (STR)'0') * Power;  // Convert ASCII digit and add
        Power *= 10;                         // Move to next decimal place

        if (Index == 0) break;  // Processed all digits
        Index--;                // Move to next digit leftward
    }

    return Value;
}

/***************************************************************************/

/**
 * @brief Converts a string to a 32-bit unsigned integer.
 *
 * Handles both decimal and hexadecimal formats. Hex strings starting with
 * "0x" or "0X" are parsed as hexadecimal. Decimal strings are processed
 * from right to left. Invalid characters stop parsing and return partial result.
 *
 * @param Text String to convert (decimal or "0x" prefixed hex)
 * @return Parsed 32-bit unsigned integer
 */
U32 StringToU32(LPCSTR Text) {
    U32 Value = 0;
    U32 Index = 0;
    U32 Power = 1;  // Decimal place multiplier
    STR Data = 0;

    if (Text[0] == STR_NULL) return 0;  // Empty string

    // Check for hexadecimal format
    if (Text[0] == '0' && Text[1] == 'x') return HexStringToU32(Text);
    if (Text[0] == '0' && Text[1] == 'X') return HexStringToU32(Text);

    Index = StringLength(Text) - 1;  // Start from rightmost digit

    // Process decimal digits from right to left
    FOREVER {
        Data = Text[Index];
        if (IsNumeric(Data) == 0) break;  // Stop at non-numeric character

        Value += (Data - (STR)'0') * Power;  // Convert and accumulate
        Power *= 10;                         // Move to next decimal place

        if (Index == 0) break;  // Processed all digits
        Index--;                // Move to next digit leftward
    }

    return Value;
}

/************************************************************************/

// Helper macro for division with remainder - divides n by base and returns remainder
#ifdef __EXOS_64__
    #define DoDiv(n, base)                     \
        ({                                     \
            int __res;                         \
            __res = (int)((U64)(n) % (U64)(base)); \
            (n) = (UINT)((U64)(n) / (U64)(base));  \
            __res;                             \
        })
#else
    #define DoDiv(n, base)                \
        ({                                \
            int __res;                    \
            __res = ((U32)(n)) % (U32)(base); \
            (n) = (UINT)((U32)(n) / (U32)(base)); \
            __res;                        \
        })
#endif

/************************************************************************/

/**
 * @brief Converts a number to formatted string representation in specified base.
 *
 * This is a comprehensive number-to-string formatter that supports multiple bases,
 * padding, alignment, signs, and special prefixes. Used internally by printf-style
 * functions to format numeric output with various formatting flags.
 *
 * @param Text Buffer to store resulting formatted string
 * @param Number Magnitude of the integer value to convert (always positive)
 * @param Base Number base (2-36) for conversion
 * @param Size Minimum field width for padding
 * @param Precision Minimum number of digits to display
 * @param Type Formatting flags (PF_LARGE, PF_LEFT, PF_SIGN, etc.)
 * @param IsNegative TRUE when the original value was negative
 * @return Pointer to end of formatted string
 */
LPSTR NumberToString(LPSTR Text, UINT Number, I32 Base, I32 Size, I32 Precision, I32 Type, BOOL IsNegative) {
    STR c, Sign, Temp[66];                                         // Temp buffer for digits (max needed for base 2)
    LPCSTR Digits = TEXT("0123456789abcdefghijklmnopqrstuvwxyz");  // Lowercase digits
    INT i;

    // Use uppercase digits if PF_LARGE flag is set
    if (Type & PF_LARGE) Digits = TEXT("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");

    // Left alignment disables zero padding
    if (Type & PF_LEFT) Type &= ~PF_ZEROPAD;

    // Validate base range (binary to base-36)
    if (Base < 2 || Base > 36) return NULL;

    // Choose padding character: '0' for zero-pad, ' ' for space-pad
    c = (Type & PF_ZEROPAD) ? '0' : ' ';
    Sign = 0;

    // Handle sign processing
    if (Type & PF_SIGN) {
        if (IsNegative) {
            Sign = '-';  // Negative number
            Size--;      // Reserve space for sign
        } else if (Type & PF_PLUS) {
            Sign = '+';  // Force + for positive
            Size--;
        } else if (Type & PF_SPACE) {
            Sign = ' ';  // Space for positive
            Size--;
        }
    }

    // Special prefixes ("0" for octal, "0x" for hex)
    if (Type & PF_SPECIAL) {
        if (Base == 8) Size--;  // Reserve space for "0"
    }

    i = 0;

    // Convert number to digits (stored in reverse order)
    if (Number == 0)
        Temp[i++] = '0';  // Special case for zero
    else
        while (Number != 0) Temp[i++] = Digits[DoDiv(Number, Base)];

    // Ensure minimum precision (pad with zeros if needed)
    if (i > Precision) Precision = i;

    Size -= Precision;  // Adjust remaining field width

    // Right-aligned: add leading spaces (if not zero-padded or left-aligned)
    if (!(Type & (PF_ZEROPAD | PF_LEFT))) {
        while (Size-- > 0) *Text++ = ' ';
    }

    // Add sign character
    if (Sign) *Text++ = Sign;

    // Add special base prefixes
    if (Type & PF_SPECIAL) {
        if (Base == 8)
            *Text++ = '0';  // Octal prefix
        else if (Base == 16) {
            *Text++ = '0';  // Hex prefix
            *Text++ = 'x';
        }
    }

    // Zero-padded: add leading zeros
    if (!(Type & PF_LEFT)) {
        while (Size-- > 0) *Text++ = c;
    }

    // Add precision padding (leading zeros)
    while (i < Precision--) *Text++ = '0';

    // Add the actual digits (reverse order from temp buffer)
    while (i-- > 0) *Text++ = Temp[i];

    // Left-aligned: add trailing spaces
    while (Size-- > 0) *Text++ = STR_SPACE;

    *Text++ = STR_NULL;  // Null-terminate result

    return Text;  // Return pointer to end of string
}

/***************************************************************************/

/**
 * @brief Parses decimal number from format string and advances pointer.
 *
 * Helper function for printf-style formatting that extracts numeric field
 * width or precision values from format strings. Advances the format pointer
 * past the parsed digits.
 *
 * @param Format Pointer to format string pointer (modified to skip digits)
 * @return Parsed decimal integer value
 */
static int SkipAToI(LPCSTR* Format) {
    int Result = 0;

    // Parse decimal digits and build number
    while (IsNumeric(**Format)) {
        Result = Result * 10 + (**Format - '0');  // Accumulate digit
        (*Format)++;                              // Move to next character
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Converts a floating-point number to string representation with precision.
 *
 * Converts F32 floating-point values to decimal string format with specified
 * precision. Handles sign, integer part, and fractional part. Uses simple
 * integer arithmetic to avoid complex floating-point operations.
 *
 * @param Text Buffer to store resulting string
 * @param Value F32 floating-point value to convert
 * @param Precision Number of decimal places to display (default 6 if -1)
 * @return Pointer to end of formatted string
 */
LPSTR FloatToString(LPSTR Text, F32 Value, I32 Precision) {
    LPSTR Dst = Text;

    // Set default precision
    if (Precision < 0) Precision = 6;
    if (Precision > 9) Precision = 9;  // Limit to avoid overflow

    // Handle negative numbers
    if (Value < 0.0f) {
        *Dst++ = '-';
        Value = -Value;
    }

    // Extract integer part
    U32 IntegerPart = (U32)Value;
    F32 FractionalPart = Value - (F32)IntegerPart;

    // Convert integer part
    if (IntegerPart == 0) {
        *Dst++ = '0';
    } else {
        STR IntBuffer[16];
        U32ToString(IntegerPart, IntBuffer);
        LPCSTR IntPtr = IntBuffer;
        while (*IntPtr) *Dst++ = *IntPtr++;
    }

    // Add decimal point and fractional part
    if (Precision > 0) {
        *Dst++ = '.';

        // Convert fractional part by multiplying by powers of 10
        for (I32 i = 0; i < Precision; i++) {
            FractionalPart *= 10.0f;
            U32 Digit = (U32)FractionalPart;
            *Dst++ = '0' + (Digit % 10);
            FractionalPart -= (F32)Digit;
        }
    }

    *Dst = STR_NULL;
    return Dst;
}

/***************************************************************************/

/**
 * @brief Core printf-style string formatting function with variable arguments.
 *
 * This is the main formatting engine that processes format strings with
 * specifiers like %d, %s, %x, etc. Supports field width, precision, alignment,
 * padding, and various numeric bases. Used by StringPrintFormat and other
 * printf-style functions throughout the kernel.
 *
 * @param Destination Buffer to store formatted output string
 * @param Format Printf-style format string with % specifiers
 * @param Args Variable argument list containing values to format
 */
void StringPrintFormatArgs(LPSTR Destination, LPCSTR Format, VarArgList Args) {
    LPCSTR Text = NULL;
    UINT NumberValue = 0;
    BOOL NumberIsNegative = FALSE;
    BOOL NumberIsPreloaded = FALSE;
    #if defined(__EXOS_64__)
    BOOL QualifierIsLongLong = FALSE;
    #endif
    int Flags, FieldWidth, Precision, Qualifier, Base, Length, i;
    LPSTR Dst = Destination;  // Output pointer

    // Handle null format string gracefully
    if (Format == NULL) {
        *Dst++ = '<';  // Output "<NULL>" as error indicator
        *Dst++ = 'N';
        *Dst++ = 'U';
        *Dst++ = 'L';
        *Dst++ = 'L';
        *Dst++ = '>';
        *Dst = STR_NULL;
        return;
    }

    // Main formatting loop - process each character in format string
    for (; *Format != STR_NULL; Format++) {
        // Regular characters: copy directly to output
        if (*Format != '%') {
            *Dst++ = *Format;
            continue;
        }

        // Found '%' - begin format specifier parsing
        Flags = 0;
        NumberIsPreloaded = FALSE;
        NumberIsNegative = FALSE;
        NumberValue = 0ull;

        // Parse format flags (-, +, space, #, 0) - can appear in any combination
    Repeat:
        Format++;
        switch (*Format) {
            case '-':
                Flags |= PF_LEFT;  // Left-align output
                goto Repeat;
            case '+':
                Flags |= PF_PLUS;  // Show + for positive numbers
                goto Repeat;
            case ' ':
                Flags |= PF_SPACE;  // Space for positive numbers
                goto Repeat;
            case '#':
                Flags |= PF_SPECIAL;  // Add base prefixes (0, 0x)
                goto Repeat;
            case '0':
                Flags |= PF_ZEROPAD;  // Pad with zeros instead of spaces
                goto Repeat;
            case STR_NULL:
                *Dst = STR_NULL;  // Premature end of format
                return;
        }

        FieldWidth = -1;
        if (IsNumeric(*Format)) {
            FieldWidth = SkipAToI(&Format);
        } else if (*Format == '*') {
            Format++;
            FieldWidth = VarArg(Args, INT);
            if (FieldWidth < 0) {
                FieldWidth = -FieldWidth;
                Flags |= PF_LEFT;
            }
        }

        Precision = -1;
        if (*Format == '.') {
            Format++;
            if (IsNumeric(*Format)) {
                Precision = SkipAToI(&Format);
            } else if (*Format == '*') {
                Format++;
                Precision = VarArg(Args, INT);
            }
            if (Precision < 0) Precision = 0;
        }

        Qualifier = -1;
        #if defined(__EXOS_64__)
        QualifierIsLongLong = FALSE;
        #endif
        if (*Format == 'h' || *Format == 'l' || *Format == 'L') {
            Qualifier = *Format;
            Format++;
            if (Qualifier == 'l' && *Format == 'l') {
                #if defined(__EXOS_64__)
                QualifierIsLongLong = TRUE;
                #endif
                Format++;
            }
        }

        Base = 10;

        switch (*Format) {
            case 'c':
                if (!(Flags & PF_LEFT)) {
                    while (--FieldWidth > 0) *Dst++ = STR_SPACE;
                }
                *Dst++ = (STR)VarArg(Args, INT);
                while (--FieldWidth > 0) *Dst++ = STR_SPACE;
                continue;

            case 's':
                Text = VarArg(Args, LPCSTR);
                if (Text == NULL) Text = TEXT("<NULL>");

                Length = StringLength(Text);
                if (Precision >= 0 && Length > Precision) Length = Precision;

                if (!(Flags & PF_LEFT)) {
                    while (Length < FieldWidth--) *Dst++ = STR_SPACE;
                }
                for (i = 0; i < Length && Text[i] != STR_NULL; i++) {
                    *Dst++ = Text[i];
                }
                while (Length < FieldWidth--) *Dst++ = STR_SPACE;
                continue;

            case 'p': {
                if (FieldWidth == -1) {
                    FieldWidth = 2 * (I32)sizeof(LINEAR);
                    Flags |= PF_ZEROPAD | PF_LARGE;
                }
                Base = 16;
                LINEAR PointerValue = (LINEAR)VarArg(Args, LPVOID);
                NumberValue = (UINT)PointerValue;
                NumberIsPreloaded = TRUE;
                NumberIsNegative = FALSE;
                goto HandleNumber;
            }

            case 'o':
                Flags |= PF_SPECIAL;
                Base = 8;
                break;
            case 'X':
                Flags |= PF_SPECIAL | PF_LARGE;
                Base = 16;
                break;
            case 'x':
                Flags |= PF_SPECIAL;
                Base = 16;
                break;
            case 'b':
                Base = 2;
                break;
            case 'd':
            case 'i':
                Flags |= PF_SIGN;
                break;
            case 'u':
                break;
            case 'f':
                {
                    F32 FloatValue = (F32)VarArg(Args, F64);
                    STR FloatBuffer[64];
                    FloatToString(FloatBuffer, FloatValue, Precision);

                    // Handle field width and alignment
                    Length = StringLength(FloatBuffer);
                    if (!(Flags & PF_LEFT)) {
                        while (Length < FieldWidth--) *Dst++ = STR_SPACE;
                    }
                    for (i = 0; i < Length && FloatBuffer[i] != STR_NULL; i++) {
                        *Dst++ = FloatBuffer[i];
                    }
                    while (Length < FieldWidth--) *Dst++ = STR_SPACE;
                }
                continue;
            default:
                if (*Format != '%') *Dst++ = '%';
                if (*Format) {
                    *Dst++ = *Format;
                } else {
                    Format--;
                }
                continue;
        }

    // Extract and format numeric value based on qualifier and flags
    HandleNumber:
        if (!NumberIsPreloaded) {
            if (Flags & PF_SIGN) {
                #ifdef __EXOS_64__
                if (QualifierIsLongLong) {
                    I64 SignedValue = VarArg(Args, I64);
                    if (SignedValue < 0) {
                        NumberIsNegative = TRUE;
                        NumberValue = (UINT)(-SignedValue);
                    } else {
                        NumberValue = (UINT)SignedValue;
                    }
                } else
                #endif
                if (Qualifier == 'l' || Qualifier == 'L') {
                    I32 SignedValue = VarArg(Args, I32);
                    if (SignedValue < 0) {
                        NumberIsNegative = TRUE;
                        NumberValue = (UINT)(-SignedValue);
                    } else {
                        NumberValue = (UINT)SignedValue;
                    }
                } else if (Qualifier == 'h') {
                    INT RawValue = VarArg(Args, INT);
                    I16 ShortValue = (I16)RawValue;
                    if (ShortValue < 0) {
                        NumberIsNegative = TRUE;
                        NumberValue = (UINT)(-(INT)ShortValue);
                    } else {
                        NumberValue = (UINT)ShortValue;
                    }
                } else {
                    INT SignedValue = VarArg(Args, INT);
                    if (SignedValue < 0) {
                        NumberIsNegative = TRUE;
                        NumberValue = (UINT)(-SignedValue);
                    } else {
                        NumberValue = (UINT)SignedValue;
                    }
                }
            } else {
                #ifdef __EXOS_64__
                if (QualifierIsLongLong) {
                    NumberValue = VarArg(Args, U64);
                } else
                #endif
                if (Qualifier == 'l' || Qualifier == 'L') {
                    NumberValue = (UINT)VarArg(Args, U32);
                } else if (Qualifier == 'h') {
                    NumberValue = (UINT)(U16)VarArg(Args, UINT);
                } else {
                    NumberValue = (UINT)VarArg(Args, UINT);
                }
            }
        }

        // Convert number to formatted string and copy to output
        STR Temp[128];
        NumberToString(Temp, NumberValue, Base, FieldWidth, Precision, Flags, NumberIsNegative);
        NumberIsPreloaded = FALSE;
        NumberIsNegative = FALSE;
        for (i = 0; Temp[i] != STR_NULL; i++) {
            *Dst++ = Temp[i];  // Copy formatted digits
        }
    }

    *Dst = STR_NULL;
}

/***************************************************************************/

/**
 * @brief Printf-style string formatting with variable arguments.
 *
 * Convenient wrapper around StringPrintFormatArgs that handles variable
 * argument list setup and cleanup. This is the main printf-style function
 * used throughout the kernel for formatted string output.
 *
 * @param Destination Buffer to store formatted output string
 * @param Format Printf-style format string with % specifiers
 * @param ... Variable arguments to format according to format string
 */
void StringPrintFormat(LPSTR Destination, LPCSTR Format, ...) {
    VarArgList Args;

    VarArgStart(Args, Format);                         // Initialize argument list
    StringPrintFormatArgs(Destination, Format, Args);  // Do the formatting
    VarArgEnd(Args);                                   // Clean up argument list
}

/***************************************************************************/

/**
 * @brief Parses an IPv4 address string and returns it in big-endian format.
 *
 * @param ipStr IPv4 address string in format "a.b.c.d" (e.g., "192.168.56.10")
 * @return IPv4 address in big-endian format, or 0 if parsing failed
 */
U32 ParseIPAddress(LPCSTR ipStr) {
    U32 octets[4] = {0};
    U32 octetIndex = 0;
    U32 currentOctet = 0;
    U32 i = 0;

    if (ipStr == NULL) return 0;

    // Parse each character
    while (ipStr[i] != '\0' && octetIndex < 4) {
        STR c = ipStr[i];

        if (c >= '0' && c <= '9') {
            // Add digit to current octet
            currentOctet = currentOctet * 10 + (c - '0');
            if (currentOctet > 255) return 0; // Invalid octet value
        } else if (c == '.') {
            // End of current octet
            octets[octetIndex] = currentOctet;
            octetIndex++;
            currentOctet = 0;
        } else {
            // Invalid character
            return 0;
        }
        i++;
    }

    // Store the last octet
    if (octetIndex == 3) {
        octets[3] = currentOctet;
    } else {
        return 0; // Not enough octets
    }

    // Validate all octets are <= 255 and convert to big-endian
    for (i = 0; i < 4; i++) {
        if (octets[i] > 255) return 0;
    }

    return Htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
}
