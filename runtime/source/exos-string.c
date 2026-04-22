/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime String Helpers

\************************************************************************/

#include "../include/exos-string.h"

/************************************************************************/

static const STR ExosHexDigitsLower[] = "0123456789abcdefghijklmnopqrstuvwxyz";
static const STR ExosHexDigitsUpper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/************************************************************************/

/**
 * @brief Checks whether a character is a decimal digit.
 * @param Character Character to test.
 * @return TRUE for decimal digits, FALSE otherwise.
 */
static BOOL exos_string_is_numeric(STR Character) {
    return Character >= '0' && Character <= '9';
}

/************************************************************************/

/**
 * @brief Reverses a null-terminated string in place.
 * @param Text String to reverse.
 */
static void exos_string_reverse(LPSTR Text) {
    UINT Left;
    UINT Right;

    if (Text == NULL) {
        return;
    }

    Left = 0;
    Right = StringLength(Text);
    if (Right == 0) {
        return;
    }
    Right--;

    while (Left < Right) {
        STR Character = Text[Left];
        Text[Left] = Text[Right];
        Text[Right] = Character;
        Left++;
        Right--;
    }
}

/************************************************************************/

/**
 * @brief Calculates the length of a null-terminated string.
 * @param Src String to measure.
 * @return Length of the string in characters, or 0 for NULL.
 */
UINT StringLength(LPCSTR Src) {
    UINT Size;

    if (Src == NULL) {
        return 0;
    }

    Size = 0;
    while (Src[Size] != STR_NULL && Size < 8192) {
        Size++;
    }

    return Size;
}

/************************************************************************/

/**
 * @brief Copies a null-terminated string from source to destination.
 * @param Dst Destination buffer.
 * @param Src Source string.
 */
void StringCopy(LPSTR Dst, LPCSTR Src) {
    UINT Index;

    if (Dst == NULL || Src == NULL) {
        return;
    }

    for (Index = 0; Index < MAX_U32; Index++) {
        Dst[Index] = Src[Index];
        if (Src[Index] == STR_NULL) {
            break;
        }
    }
}

/************************************************************************/

/**
 * @brief Copies a string to a bounded destination buffer.
 * @param Dst Destination buffer.
 * @param Src Source string.
 * @param MaxLength Destination buffer length.
 */
void StringCopyLimit(LPSTR Dst, LPCSTR Src, UINT MaxLength) {
    UINT Index;

    if (Dst == NULL || Src == NULL || MaxLength == 0) {
        return;
    }

    for (Index = 0; Index < MaxLength - 1; Index++) {
        Dst[Index] = Src[Index];
        if (Src[Index] == STR_NULL) {
            return;
        }
    }

    Dst[Index] = STR_NULL;
}

/************************************************************************/

/**
 * @brief Converts a 32-bit unsigned integer to a decimal string.
 * @param Number Number to convert.
 * @param Text Destination buffer.
 */
void U32ToString(U32 Number, LPSTR Text) {
    U32 Index;

    if (Text == NULL) {
        return;
    }

    if (Number == 0) {
        Text[0] = '0';
        Text[1] = STR_NULL;
        return;
    }

    Index = 0;
    while (Number != 0) {
        Text[Index++] = (STR)('0' + (Number % 10));
        Number /= 10;
    }
    Text[Index] = STR_NULL;

    exos_string_reverse(Text);
}

/************************************************************************/

/**
 * @brief Divides a register-sized number by a base and returns the remainder.
 * @param Number Number to divide, updated with the quotient.
 * @param Base Base divisor.
 * @return Division remainder.
 */
static INT exos_string_divide(UINT* Number, I32 Base) {
    INT Remainder;

#ifdef __EXOS_64__
    Remainder = (INT)((U64)(*Number) % (U64)Base);
    *Number = (UINT)((U64)(*Number) / (U64)Base);
#else
    Remainder = (INT)((U32)(*Number) % (U32)Base);
    *Number = (UINT)((U32)(*Number) / (U32)Base);
#endif

    return Remainder;
}

/************************************************************************/

/**
 * @brief Converts an integer to its formatted string representation.
 * @param Text Destination buffer.
 * @param Number Number magnitude to convert.
 * @param Base Numeric base.
 * @param Size Minimum field width.
 * @param Precision Minimum digit count.
 * @param Type Formatting flags.
 * @param IsNegative TRUE when the original value was negative.
 * @return Pointer to the end of the formatted string.
 */
static LPSTR exos_string_number_to_string(
    LPSTR Text,
    UINT Number,
    I32 Base,
    I32 Size,
    I32 Precision,
    I32 Type,
    BOOL IsNegative) {
    STR PaddingCharacter;
    STR Sign;
    STR Temporary[66];
    LPCSTR Digits;
    INT Index;

    if (Base < 2 || Base > 36) {
        return Text;
    }

    Digits = (Type & PF_LARGE) ? ExosHexDigitsUpper : ExosHexDigitsLower;
    if (Type & PF_LEFT) {
        Type &= ~PF_ZEROPAD;
    }

    PaddingCharacter = (Type & PF_ZEROPAD) ? '0' : ' ';
    Sign = 0;

    if (Type & PF_SIGN) {
        if (IsNegative) {
            Sign = '-';
            Size--;
        } else if (Type & PF_PLUS) {
            Sign = '+';
            Size--;
        } else if (Type & PF_SPACE) {
            Sign = ' ';
            Size--;
        }
    }

    if ((Type & PF_SPECIAL) && Base == 8) {
        Size--;
    }

    Index = 0;
    if (Number == 0) {
        Temporary[Index++] = '0';
    } else {
        while (Number != 0) {
            Temporary[Index++] = Digits[exos_string_divide(&Number, Base)];
        }
    }

    if (Index > Precision) {
        Precision = Index;
    }
    Size -= Precision;

    if ((Type & (PF_ZEROPAD | PF_LEFT)) == 0) {
        while (Size-- > 0) {
            *Text++ = ' ';
        }
    }

    if (Sign != 0) {
        *Text++ = Sign;
    }

    if (Type & PF_SPECIAL) {
        if (Base == 8) {
            *Text++ = '0';
        } else if (Base == 16) {
            *Text++ = '0';
            *Text++ = 'x';
        }
    }

    if ((Type & PF_LEFT) == 0) {
        while (Size-- > 0) {
            *Text++ = PaddingCharacter;
        }
    }

    while (Index < Precision--) {
        *Text++ = '0';
    }

    while (Index-- > 0) {
        *Text++ = Temporary[Index];
    }

    while (Size-- > 0) {
        *Text++ = STR_SPACE;
    }

    *Text = STR_NULL;
    return Text;
}

/************************************************************************/

/**
 * @brief Parses a positive decimal integer from a format string.
 * @param Format Format pointer to advance.
 * @return Parsed integer value.
 */
static INT exos_string_skip_atoi(LPCSTR* Format) {
    INT Result;

    Result = 0;
    while (exos_string_is_numeric(**Format)) {
        Result = Result * 10 + (**Format - '0');
        (*Format)++;
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Converts a floating-point value to a decimal string.
 * @param Text Destination buffer.
 * @param Value Value to convert.
 * @param Precision Number of decimal places.
 * @return Pointer to the end of the formatted string.
 */
static LPSTR exos_string_float_to_string(LPSTR Text, F32 Value, I32 Precision) {
    LPSTR Destination;
    U32 IntegerPart;
    F32 FractionalPart;
    I32 Index;

    if (Precision < 0) {
        Precision = 6;
    }
    if (Precision > 9) {
        Precision = 9;
    }

    Destination = Text;
    if (Value < 0.0) {
        *Destination++ = '-';
        Value = -Value;
    }

    IntegerPart = (U32)Value;
    FractionalPart = Value - (F32)IntegerPart;

    if (IntegerPart == 0) {
        *Destination++ = '0';
    } else {
        STR Buffer[16];
        LPCSTR Cursor;

        U32ToString(IntegerPart, Buffer);
        Cursor = Buffer;
        while (*Cursor != STR_NULL) {
            *Destination++ = *Cursor++;
        }
    }

    if (Precision > 0) {
        *Destination++ = '.';
        for (Index = 0; Index < Precision; Index++) {
            U32 Digit;

            FractionalPart *= 10.0;
            Digit = (U32)FractionalPart;
            *Destination++ = (STR)('0' + (Digit % 10));
            FractionalPart -= (F32)Digit;
        }
    }

    *Destination = STR_NULL;
    return Destination;
}

/************************************************************************/

/**
 * @brief Formats a string using a variable argument list.
 * @param Destination Destination buffer.
 * @param Format Printf-style format string.
 * @param Args Variable argument list.
 */
void StringPrintFormatArgs(LPSTR Destination, LPCSTR Format, VarArgList Args) {
    LPCSTR Text;
    UINT NumberValue;
    BOOL NumberIsNegative;
    BOOL NumberIsPreloaded;
#ifdef __EXOS_64__
    BOOL QualifierIsLongLong;
#endif
    INT Flags;
    INT FieldWidth;
    INT Precision;
    INT Qualifier;
    INT Base;
    INT Length;
    INT Index;
    LPSTR Dst;

    if (Destination == NULL) {
        return;
    }

    Dst = Destination;
    if (Format == NULL) {
        StringCopy(Dst, TEXT("<NULL>"));
        return;
    }

    for (; *Format != STR_NULL; Format++) {
        if (*Format != '%') {
            *Dst++ = *Format;
            continue;
        }

        Flags = 0;
        NumberIsPreloaded = FALSE;
        NumberIsNegative = FALSE;
        NumberValue = 0;

    Repeat:
        Format++;
        switch (*Format) {
            case '-':
                Flags |= PF_LEFT;
                goto Repeat;
            case '+':
                Flags |= PF_PLUS;
                goto Repeat;
            case ' ':
                Flags |= PF_SPACE;
                goto Repeat;
            case '#':
                Flags |= PF_SPECIAL;
                goto Repeat;
            case '0':
                Flags |= PF_ZEROPAD;
                goto Repeat;
            case STR_NULL:
                *Dst = STR_NULL;
                return;
        }

        FieldWidth = -1;
        if (exos_string_is_numeric(*Format)) {
            FieldWidth = exos_string_skip_atoi(&Format);
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
            if (exos_string_is_numeric(*Format)) {
                Precision = exos_string_skip_atoi(&Format);
            } else if (*Format == '*') {
                Format++;
                Precision = VarArg(Args, INT);
            }
            if (Precision < 0) {
                Precision = 0;
            }
        }

        Qualifier = -1;
#ifdef __EXOS_64__
        QualifierIsLongLong = FALSE;
#endif
        if (*Format == 'h' || *Format == 'l' || *Format == 'L') {
            Qualifier = *Format;
            Format++;
            if (Qualifier == 'l' && *Format == 'l') {
#ifdef __EXOS_64__
                QualifierIsLongLong = TRUE;
#endif
                Format++;
            }
        }

        Base = 10;
        switch (*Format) {
            case 'c':
                if ((Flags & PF_LEFT) == 0) {
                    while (--FieldWidth > 0) {
                        *Dst++ = STR_SPACE;
                    }
                }
                *Dst++ = (STR)VarArg(Args, INT);
                while (--FieldWidth > 0) {
                    *Dst++ = STR_SPACE;
                }
                continue;

            case 's':
                Text = VarArg(Args, LPCSTR);
                if (Text == NULL) {
                    Text = TEXT("<NULL>");
                }
                Length = (INT)StringLength(Text);
                if (Precision >= 0 && Length > Precision) {
                    Length = Precision;
                }
                if ((Flags & PF_LEFT) == 0) {
                    while (Length < FieldWidth--) {
                        *Dst++ = STR_SPACE;
                    }
                }
                for (Index = 0; Index < Length && Text[Index] != STR_NULL; Index++) {
                    *Dst++ = Text[Index];
                }
                while (Length < FieldWidth--) {
                    *Dst++ = STR_SPACE;
                }
                continue;

            case 'p':
                if (FieldWidth == -1) {
                    FieldWidth = 2 * (I32)sizeof(LINEAR);
                    Flags |= PF_ZEROPAD | PF_LARGE;
                }
                Base = 16;
                NumberValue = (UINT)((LINEAR)VarArg(Args, LPVOID));
                NumberIsPreloaded = TRUE;
                NumberIsNegative = FALSE;
                goto HandleNumber;

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
            case 'f': {
                STR FloatBuffer[64];
                F32 FloatValue;

                FloatValue = (F32)VarArg(Args, F64);
                exos_string_float_to_string(FloatBuffer, FloatValue, Precision);
                Length = (INT)StringLength(FloatBuffer);
                if ((Flags & PF_LEFT) == 0) {
                    while (Length < FieldWidth--) {
                        *Dst++ = STR_SPACE;
                    }
                }
                for (Index = 0; Index < Length && FloatBuffer[Index] != STR_NULL; Index++) {
                    *Dst++ = FloatBuffer[Index];
                }
                while (Length < FieldWidth--) {
                    *Dst++ = STR_SPACE;
                }
                continue;
            }
            default:
                if (*Format != '%') {
                    *Dst++ = '%';
                }
                if (*Format != STR_NULL) {
                    *Dst++ = *Format;
                } else {
                    Format--;
                }
                continue;
        }

    HandleNumber:
        if (NumberIsPreloaded == FALSE) {
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

        {
            STR Temporary[128];

            exos_string_number_to_string(
                Temporary,
                NumberValue,
                Base,
                FieldWidth,
                Precision,
                Flags,
                NumberIsNegative);
            for (Index = 0; Temporary[Index] != STR_NULL; Index++) {
                *Dst++ = Temporary[Index];
            }
        }
    }

    *Dst = STR_NULL;
}
