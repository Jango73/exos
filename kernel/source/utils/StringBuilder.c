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


    String builder

\************************************************************************/

#include "utils/StringBuilder.h"

#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Appends a fixed-length text fragment to the builder.
 *
 * Refuses the append when the resulting text would not fit in the target
 * buffer, while preserving the existing text and tracking the required size.
 *
 * @param Builder Target string builder.
 * @param Text Text fragment to append.
 * @param TextLength Fragment length without the null terminator.
 * @return TRUE when the append succeeded.
 */
static BOOL StringBuilderAppendLength(LPSTRINGBUILDER Builder, LPCSTR Text, UINT TextLength) {
    UINT Index;
    UINT RequiredLength;

    if (Builder == NULL || Builder->Buffer == NULL || Text == NULL) {
        return FALSE;
    }

    RequiredLength = Builder->Length + TextLength;
    if (RequiredLength >= Builder->Capacity) {
        Builder->Overflowed = TRUE;
        if (RequiredLength > Builder->RequiredLength) {
            Builder->RequiredLength = RequiredLength;
        }
        return FALSE;
    }

    for (Index = 0; Index < TextLength; Index++) {
        Builder->Buffer[Builder->Length + Index] = Text[Index];
    }

    Builder->Length = RequiredLength;
    Builder->RequiredLength = Builder->Length;
    Builder->Buffer[Builder->Length] = STR_NULL;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initializes a string builder on top of a caller-owned buffer.
 * @param Builder Builder to initialize.
 * @param Buffer Destination text buffer.
 * @param Capacity Total buffer capacity including the null terminator.
 * @return TRUE on success.
 */
BOOL StringBuilderInit(LPSTRINGBUILDER Builder, LPSTR Buffer, UINT Capacity) {
    if (Builder == NULL || Buffer == NULL || Capacity == 0) {
        return FALSE;
    }

    Builder->Buffer = Buffer;
    Builder->Capacity = Capacity;
    Builder->Length = 0;
    Builder->RequiredLength = 0;
    Builder->Overflowed = FALSE;
    Builder->Buffer[0] = STR_NULL;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resets a builder to an empty string.
 * @param Builder Builder to reset.
 */
void StringBuilderReset(LPSTRINGBUILDER Builder) {
    if (Builder == NULL || Builder->Buffer == NULL || Builder->Capacity == 0) {
        return;
    }

    Builder->Length = 0;
    Builder->RequiredLength = 0;
    Builder->Overflowed = FALSE;
    Builder->Buffer[0] = STR_NULL;
}

/************************************************************************/

/**
 * @brief Replaces the builder contents with one text string.
 * @param Builder Destination builder.
 * @param Text Source text.
 * @return TRUE on success.
 */
BOOL StringBuilderSet(LPSTRINGBUILDER Builder, LPCSTR Text) {
    if (Builder == NULL || Text == NULL) {
        return FALSE;
    }

    StringBuilderReset(Builder);
    return StringBuilderAppend(Builder, Text);
}

/************************************************************************/

/**
 * @brief Appends one null-terminated text string.
 * @param Builder Destination builder.
 * @param Text Source text.
 * @return TRUE on success.
 */
BOOL StringBuilderAppend(LPSTRINGBUILDER Builder, LPCSTR Text) {
    if (Builder == NULL || Text == NULL) {
        return FALSE;
    }

    return StringBuilderAppendLength(Builder, Text, StringLength(Text));
}

/************************************************************************/

/**
 * @brief Appends one character to the builder.
 * @param Builder Destination builder.
 * @param Character Character to append.
 * @return TRUE on success.
 */
BOOL StringBuilderAppendChar(LPSTRINGBUILDER Builder, STR Character) {
    STR Text[2] = {Character, STR_NULL};

    return StringBuilderAppendLength(Builder, Text, 1);
}

/************************************************************************/

/**
 * @brief Appends one path segment, inserting a separator when needed.
 *
 * The original builder contents are preserved when the full path segment does
 * not fit in the destination buffer.
 *
 * @param Builder Destination builder.
 * @param Segment Path segment to append.
 * @param Separator Path separator to inject between segments.
 * @return TRUE on success.
 */
BOOL StringBuilderAppendPathSegment(LPSTRINGBUILDER Builder, LPCSTR Segment, STR Separator) {
    UINT OriginalLength;
    UINT SegmentLength;
    UINT RequiredLength;
    BOOL NeedsSeparator;

    if (Builder == NULL || Segment == NULL) {
        return FALSE;
    }

    OriginalLength = Builder->Length;
    SegmentLength = StringLength(Segment);
    NeedsSeparator = (Builder->Length > 0 && Builder->Buffer[Builder->Length - 1] != Separator);
    RequiredLength = OriginalLength + SegmentLength + (NeedsSeparator ? 1 : 0);

    if (RequiredLength >= Builder->Capacity) {
        Builder->Overflowed = TRUE;
        if (RequiredLength > Builder->RequiredLength) {
            Builder->RequiredLength = RequiredLength;
        }
        return FALSE;
    }

    if (NeedsSeparator) {
        Builder->Buffer[Builder->Length++] = Separator;
    }

    if (StringBuilderAppendLength(Builder, Segment, SegmentLength) == FALSE) {
        Builder->Length = OriginalLength;
        Builder->RequiredLength = RequiredLength;
        Builder->Overflowed = TRUE;
        Builder->Buffer[Builder->Length] = STR_NULL;
    }

    return (Builder->Length == RequiredLength);
}

/************************************************************************/

/**
 * @brief Returns the current builder text.
 * @param Builder Source builder.
 * @return Pointer to the current text, or NULL.
 */
LPCSTR StringBuilderGetText(LPSTRINGBUILDER Builder) {
    if (Builder == NULL || Builder->Buffer == NULL) {
        return NULL;
    }

    return Builder->Buffer;
}

/************************************************************************/

/**
 * @brief Returns the current text length.
 * @param Builder Source builder.
 * @return Current text length.
 */
UINT StringBuilderGetLength(LPSTRINGBUILDER Builder) {
    if (Builder == NULL) {
        return 0;
    }

    return Builder->Length;
}

/************************************************************************/

/**
 * @brief Returns the length required by the most recent append attempt.
 * @param Builder Source builder.
 * @return Required text length excluding the null terminator.
 */
UINT StringBuilderGetRequiredLength(LPSTRINGBUILDER Builder) {
    if (Builder == NULL) {
        return 0;
    }

    return Builder->RequiredLength;
}

/************************************************************************/

/**
 * @brief Reports whether one append attempt overflowed the destination.
 * @param Builder Source builder.
 * @return TRUE when one append did not fit.
 */
BOOL StringBuilderHasOverflowed(LPSTRINGBUILDER Builder) {
    if (Builder == NULL) {
        return FALSE;
    }

    return Builder->Overflowed;
}
