
/************************************************************************\

    EXOS Kernel
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


    TOML

\************************************************************************/

#include "utils/TOML.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "text/CoreString.h"

/***************************************************************************/

typedef struct tag_TOML_SECTION_STATE {
    STR Section[0x80];
    STR SectionBase[0x80];
    U32 SectionIndex;
} TOML_SECTION_STATE, *LPTOML_SECTION_STATE;

/***************************************************************************/

typedef struct tag_TOML_PARSE_COUNT {
    UINT ItemCount;
    UINT KeyBytes;
    UINT ValueBytes;
} TOML_PARSE_COUNT, *LPTOML_PARSE_COUNT;

/***************************************************************************/

/**
 * @brief Extracts the next source line into a local buffer.
 * @param Source TOML-formatted source string.
 * @param Index In/out cursor in the source, advanced past the extracted line.
 * @param Line Output buffer receiving the line.
 * @param LineSize Capacity of the output buffer.
 * @return TRUE when a line was extracted, FALSE at end of source.
 */
static BOOL TomlExtractNextLine(LPCSTR Source, UINT* Index, LPSTR Line, UINT LineSize) {
    UINT LineLength = 0;

    if (Source[*Index] == STR_NULL) {
        return FALSE;
    }

    while (Source[*Index] != STR_NULL && Source[*Index] != '\n') {
        if (LineLength < LineSize - 1) {
            Line[LineLength++] = Source[*Index];
        }
        (*Index)++;
    }

    if (Source[*Index] == '\n') {
        (*Index)++;
    }

    Line[LineLength] = STR_NULL;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parses one TOML line and updates the section tracking state.
 * @param Line Line to parse, mutated in place (comments and quotes removed).
 * @param State Section tracking state, updated for section headers.
 * @param FullKey Output buffer receiving the dotted key for key-value lines.
 * @param Value Output pointer to the value substring inside Line.
 * @return TRUE when the line is a key-value pair, FALSE for section headers,
 *         comments or malformed lines.
 */
static BOOL TomlParseLine(LPSTR Line, LPTOML_SECTION_STATE State, LPSTR FullKey, LPCSTR* Value) {
    LPSTR Ptr = NULL;
    LPSTR Comment = NULL;
    LPSTR Equal = NULL;
    LPSTR Key = NULL;
    LPSTR End = NULL;

    Comment = StringFindChar(Line, '#');
    if (Comment != NULL) {
        *Comment = STR_NULL;
    }

    Ptr = Line;
    while (*Ptr == ' ' || *Ptr == '\t') {
        Ptr++;
    }
    if (*Ptr == STR_NULL) {
        return FALSE;
    }

    if (*Ptr == '[') {
        BOOL Array = FALSE;

        Ptr++;
        if (*Ptr == '[') {
            Array = TRUE;
            Ptr++;
        }

        End = StringFindChar(Ptr, ']');
        if (End != NULL) {
            *End = STR_NULL;
            if (Array && End[1] == ']') {
                End++;
            }
        }

        if (Array) {
            STR IndexText[0x10];

            if (STRINGS_EQUAL(State->SectionBase, Ptr)) {
                State->SectionIndex++;
            } else {
                StringCopy(State->SectionBase, Ptr);
                State->SectionIndex = 0;
            }

            U32ToString(State->SectionIndex, IndexText);
            StringCopy(State->Section, State->SectionBase);
            StringConcat(State->Section, TEXT("."));
            StringConcat(State->Section, IndexText);
        } else {
            StringCopy(State->Section, Ptr);
            State->SectionBase[0] = STR_NULL;
            State->SectionIndex = 0;
        }

        return FALSE;
    }

    Equal = StringFindChar(Ptr, '=');
    if (Equal == NULL) {
        return FALSE;
    }

    *Equal = STR_NULL;
    Key = Ptr;
    *Value = Equal + 1;

    End = Key + StringLength(Key);
    while (End > Key && (End[-1] == ' ' || End[-1] == '\t')) {
        End[-1] = STR_NULL;
        End--;
    }

    while (**Value == ' ' || **Value == '\t') {
        (*Value)++;
    }
    End = (LPSTR)(*Value) + StringLength(*Value);
    while (End > *Value && (End[-1] == ' ' || End[-1] == '\t' || End[-1] == '\r')) {
        End[-1] = STR_NULL;
        End--;
    }

    if (**Value == '\"') {
        (*Value)++;
        End = StringFindChar((LPSTR)*Value, '\"');
        if (End != NULL) {
            *End = STR_NULL;
        }
    }

    FullKey[0] = STR_NULL;
    if (!StringEmpty(State->Section)) {
        StringCopy(FullKey, State->Section);
        StringConcat(FullKey, TEXT("."));
    }
    StringConcat(FullKey, Key);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parses a TOML formatted string into a structured data object.
 *
 * This function implements a basic TOML parser that supports sections,
 * key-value pairs, strings, and arrays. The whole result is allocated as a
 * single contiguous block: the TOML structure, then the TOML_ITEM array, then
 * one contiguous string area holding every key and value. Key and Value
 * pointers reference that block, so TomlFree releases everything with a
 * single free.
 *
 * @param Source TOML-formatted string to parse
 * @return Pointer to TOML structure containing parsed data, or NULL on allocation error
 */
LPTOML TomlParse(LPCSTR Source) {
    TOML_SECTION_STATE State;
    TOML_PARSE_COUNT Count;
    UINT TotalSize = 0;
    UINT SourceIndex = 0;
    U8* Buffer = NULL;
    LPTOML Toml = NULL;
    LPTOMLITEM Items = NULL;
    LPSTR KeyArea = NULL;
    LPSTR ValueArea = NULL;
    UINT ItemIndex = 0;

    MemorySet(&Count, 0, sizeof(Count));
    MemorySet(&State, 0, sizeof(State));

    // Pass 1: count items and string bytes without allocating.
    if (Source != NULL) {
        while (TRUE) {
            STR Line[0x100];
            STR FullKey[0x100];
            LPCSTR Value = NULL;

            if (TomlExtractNextLine(Source, &SourceIndex, Line, sizeof(Line)) == FALSE) {
                break;
            }

            if (TomlParseLine(Line, &State, FullKey, &Value)) {
                Count.ItemCount++;
                Count.KeyBytes += StringLength(FullKey) + 1;
                Count.ValueBytes += StringLength(Value) + 1;
            }
        }
    }

    // Single allocation covering the TOML structure, the items and the strings.
    TotalSize = sizeof(TOML) + Count.ItemCount * sizeof(TOML_ITEM) + Count.KeyBytes + Count.ValueBytes;

    Buffer = (U8*)KernelHeapAlloc(TotalSize);
    if (Buffer == NULL) {
        return NULL;
    }
    MemorySet(Buffer, 0, TotalSize);

    Toml = (LPTOML)Buffer;
    Toml->First = NULL;

    if (Source == NULL) {
        return Toml;
    }

    Items = (LPTOMLITEM)(Buffer + sizeof(TOML));
    KeyArea = (LPSTR)(Buffer + sizeof(TOML) + Count.ItemCount * sizeof(TOML_ITEM));
    ValueArea = KeyArea + Count.KeyBytes;

    // Pass 2: place items and strings into the allocated block.
    MemorySet(&State, 0, sizeof(State));
    SourceIndex = 0;
    ItemIndex = 0;

    while (TRUE) {
        STR Line[0x100];
        STR FullKey[0x100];
        LPCSTR Value = NULL;
        LPTOMLITEM Item = NULL;
        UINT KeyLength = 0;
        UINT ValueLength = 0;

        if (TomlExtractNextLine(Source, &SourceIndex, Line, sizeof(Line)) == FALSE) {
            break;
        }

        if (TomlParseLine(Line, &State, FullKey, &Value) == FALSE) {
            continue;
        }

        Item = &Items[ItemIndex];
        ItemIndex++;

        Item->Next = (ItemIndex < Count.ItemCount) ? &Items[ItemIndex] : NULL;

        KeyLength = StringLength(FullKey);
        Item->Key = KeyArea;
        StringCopy(Item->Key, FullKey);
        KeyArea += KeyLength + 1;

        ValueLength = StringLength(Value);
        Item->Value = ValueArea;
        StringCopy(Item->Value, Value);
        ValueArea += ValueLength + 1;
    }

    if (Count.ItemCount > 0) {
        Toml->First = Items;
    }

    return Toml;
}

/***************************************************************************/

/**
 * @brief Retrieves a value from the TOML structure using a dot-separated path.
 * @param Toml Pointer to TOML structure to search in
 * @param Path Dot-separated path to the desired value (e.g. "server.port")
 * @return Pointer to value string if found, or NULL if not found or on error
 */
LPCSTR TomlGet(LPTOML Toml, LPCSTR Path) {
    LPTOMLITEM Item = NULL;

    // Validate parameters
    if (Toml == NULL) return NULL;
    if (Path == NULL) return NULL;

    // Search through linked list for matching key
    for (Item = Toml->First; Item; Item = Item->Next) {
        if (STRINGS_EQUAL(Item->Key, Path)) {
            return Item->Value;  // Found matching key
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Frees all memory allocated for a TOML structure.
 *
 * The whole structure is a single contiguous block, so one free releases the
 * items and the key/value strings with it.
 *
 * @param Toml Pointer to TOML structure to free (ignored if NULL)
 */
void TomlFree(LPTOML Toml) {
    if (Toml == NULL) {
        return;
    }

    KernelHeapFree(Toml);
}
