/************************************************************************\

    EXOS Portal
    Copyright (c) 1999-2026 Jango73

    Portal string helpers

\************************************************************************/

#include "portal-string.h"

/************************************************************************/

/**
 * @brief Clear one null-terminated text buffer.
 * @param Text Text buffer.
 */
void StringClear(LPSTR Text) {
    if (Text == NULL) return;
    Text[0] = STR_NULL;
}

/************************************************************************/

/**
 * @brief Append one text buffer to another.
 * @param Destination Destination text buffer.
 * @param Source Source text buffer.
 */
void StringConcat(LPSTR Destination, LPCSTR Source) {
    UINT DestinationLength;
    UINT SourceIndex;

    if (Destination == NULL || Source == NULL) return;

    DestinationLength = StringLength(Destination);
    SourceIndex = 0;
    while (Source[SourceIndex] != STR_NULL) {
        Destination[DestinationLength + SourceIndex] = Source[SourceIndex];
        SourceIndex++;
    }
    Destination[DestinationLength + SourceIndex] = STR_NULL;
}

/************************************************************************/

/**
 * @brief Compare two text buffers with case sensitivity.
 * @param Left First text buffer.
 * @param Right Second text buffer.
 * @return Negative, zero, or positive comparison result.
 */
INT StringCompare(LPCSTR Left, LPCSTR Right) {
    UINT Index;

    if (Left == NULL && Right == NULL) return 0;
    if (Left == NULL) return -1;
    if (Right == NULL) return 1;

    Index = 0;
    while (Left[Index] != STR_NULL && Right[Index] != STR_NULL) {
        if (Left[Index] != Right[Index]) {
            return (INT)((U8)Left[Index]) - (INT)((U8)Right[Index]);
        }
        Index++;
    }

    return (INT)((U8)Left[Index]) - (INT)((U8)Right[Index]);
}

/************************************************************************/

/**
 * @brief Convert one 32-bit value to hexadecimal text.
 * @param Value Input value.
 * @param Text Destination text buffer.
 */
void U32ToHexString(U32 Value, LPSTR Text) {
    static const STR Digits[] = "0123456789ABCDEF";
    UINT Shift;
    UINT Index;

    if (Text == NULL) return;

    Index = 0;
    for (Shift = 28; Shift <= 28; Shift -= 4) {
        Text[Index++] = Digits[(Value >> Shift) & 0xF];
        if (Shift == 0) break;
    }
    Text[Index] = STR_NULL;
}

/************************************************************************/
