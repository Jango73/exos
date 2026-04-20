
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


    Keyboard HID

\************************************************************************/

#include "drivers/input/Keyboard.h"

#include "text/CoreString.h"
#include "fs/File.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "utf8-hoehrmann/utf8-hoehrmann.h"

/***************************************************************************/

#define EKM1_HEADER_SIZE 4
#define EKM1_MAX_TOKENS 8
#define EKM1_TOKEN_MAX 64

/***************************************************************************/

typedef struct tag_UTF8_CURSOR {
    const U8 *Bytes;
    UINT Size;
    UINT Offset;
    UINT Line;
    UINT Column;
    UINT DecodeErrors;
} UTF8_CURSOR, *LPUTF8_CURSOR;

/***************************************************************************/

static BOOL ReadLineTokens(LPUTF8_CURSOR Cursor, STR Tokens[][EKM1_TOKEN_MAX], UINT *TokenCount, UINT *LineNumber,
                           UINT *LineOffset, BOOL *EndOfFile) {
    UINT Count = 0;
    UINT Length = 0;
    BOOL InToken = FALSE;

    if (TokenCount == NULL || LineNumber == NULL || LineOffset == NULL || EndOfFile == NULL) return FALSE;

    *TokenCount = 0;
    *EndOfFile = FALSE;

    if (Cursor->Offset >= Cursor->Size) {
        *EndOfFile = TRUE;
        return TRUE;
    }

    *LineNumber = Cursor->Line;
    *LineOffset = Cursor->Offset;
    while (Cursor->Offset < Cursor->Size) {
        U8 Byte = Cursor->Bytes[Cursor->Offset];
        Cursor->Offset++;

        if (Byte == '\n') {
            Cursor->Line++;
            Cursor->Column = 0;
            if (InToken) {
                Tokens[Count][Length] = STR_NULL;
                Count++;
                InToken = FALSE;
            }
            break;
        }

        if (Byte == '\r') {
            continue;
        }

        if (Byte == STR_NULL) {
            if (InToken) {
                Tokens[Count][Length] = STR_NULL;
                Count++;
                InToken = FALSE;
            }
            Cursor->Offset = Cursor->Size;
            *EndOfFile = TRUE;
            break;
        }

        Cursor->Column++;

        if (Byte == '#') {
            if (InToken) {
                Tokens[Count][Length] = STR_NULL;
                Count++;
                InToken = FALSE;
                Length = 0;
            }

            while (Cursor->Offset < Cursor->Size) {
                Byte = Cursor->Bytes[Cursor->Offset];
                Cursor->Offset++;
                if (Byte == '\n') {
                    Cursor->Line++;
                    Cursor->Column = 0;
                    break;
                }
            }
            break;
        }

        if (Byte == ' ' || Byte == '\t') {
            if (InToken) {
                Tokens[Count][Length] = STR_NULL;
                Count++;
                InToken = FALSE;
                Length = 0;
            }
            continue;
        }

        if (Byte >= 0x80) {
            Cursor->DecodeErrors++;
            if (InToken) {
                Tokens[Count][Length] = STR_NULL;
                Count++;
                InToken = FALSE;
                Length = 0;
            }
            continue;
        }

        if (InToken == FALSE) {
            if (Count >= EKM1_MAX_TOKENS) {
                ERROR(TEXT("Too many tokens at line %u"), Cursor->Line);
                return FALSE;
            }
            InToken = TRUE;
            Length = 0;
        }

        if (Length + 1 >= EKM1_TOKEN_MAX) {
            ERROR(TEXT("Token too long at line %u"), Cursor->Line);
            return FALSE;
        }

        Tokens[Count][Length] = (STR)Byte;
        Length++;
    }

    if (InToken) {
        Tokens[Count][Length] = STR_NULL;
        Count++;
    }

    if (Cursor->Offset >= Cursor->Size) {
        *EndOfFile = TRUE;
    }

    *TokenCount = Count;
    return TRUE;
}

/***************************************************************************/

static BOOL ParseHexToken(LPCSTR Token, U32 *Value) {
    U32 Result = 0;
    UINT Index = 0;

    if (Token == NULL || Value == NULL) return FALSE;

    if (Token[0] == '0' && (Token[1] == 'x' || Token[1] == 'X')) {
        Index = 2;
    }

    if (Token[Index] == STR_NULL) return FALSE;

    for (; Token[Index] != STR_NULL; Index++) {
        U32 Digit = 0;
        STR Ch = Token[Index];

        if (Ch >= '0' && Ch <= '9') {
            Digit = (U32)(Ch - '0');
        } else if (Ch >= 'a' && Ch <= 'f') {
            Digit = (U32)(Ch - 'a' + 10);
        } else if (Ch >= 'A' && Ch <= 'F') {
            Digit = (U32)(Ch - 'A' + 10);
        } else {
            return FALSE;
        }

        Result = (Result << 4) | Digit;
    }

    *Value = Result;
    return TRUE;
}

/***************************************************************************/

static BOOL ParseDecToken(LPCSTR Token, U32 *Value) {
    U32 Result = 0;
    UINT Index = 0;

    if (Token == NULL || Value == NULL) return FALSE;

    if (Token[0] == STR_NULL) return FALSE;

    for (; Token[Index] != STR_NULL; Index++) {
        STR Ch = Token[Index];
        if (Ch < '0' || Ch > '9') {
            return FALSE;
        }
        Result = (Result * 10) + (U32)(Ch - '0');
    }

    *Value = Result;
    return TRUE;
}

/***************************************************************************/

static void FreeKeyboardLayoutData(LPKEY_LAYOUT_HID Layout) {
    if (Layout == NULL) return;

    if (Layout->Entries != NULL) KernelHeapFree((LPVOID)Layout->Entries);
    if (Layout->DeadKeys != NULL) KernelHeapFree((LPVOID)Layout->DeadKeys);
    if (Layout->ComposeEntries != NULL) KernelHeapFree((LPVOID)Layout->ComposeEntries);
    if (Layout->Code != NULL) KernelHeapFree((LPVOID)Layout->Code);

    KernelHeapFree(Layout);
}

/***************************************************************************/

/**
 * @brief Loads a HID keyboard layout from an EKM1 file (UTF-8 tolerant decode).
 * @param Path Path to the layout file.
 * @return Parsed layout or NULL when loading fails.
 */
const KEY_LAYOUT_HID *LoadKeyboardLayout(LPCSTR Path) {
    UINT Size = 0;
    U8 *Buffer = NULL;
    KEY_LAYOUT_HID *Layout = NULL;
    KEY_LAYOUT_HID_ENTRY *Entries = NULL;
    KEY_HID_DEAD_KEY *DeadKeys = NULL;
    KEY_HID_COMPOSE_ENTRY *ComposeEntries = NULL;
    U8 MapSeen[(KEY_USAGE_MAX + 1) * KEY_LAYOUT_HID_MAX_LEVELS];
    UTF8_CURSOR Cursor;
    STR Tokens[EKM1_MAX_TOKENS][EKM1_TOKEN_MAX];
    UINT TokenCount = 0;
    UINT LineNumber = 0;
    UINT LineOffset = 0;
    BOOL EndOfFile = FALSE;
    BOOL LayoutHasCode = FALSE;
    BOOL LayoutHasLevels = FALSE;
    BOOL MapSeenAny = FALSE;

    DEBUG(TEXT("Path = %s"), Path);

    Buffer = (U8 *)FileReadAll(Path, &Size);
    if (Buffer == NULL) {
        WARNING(TEXT("Layout file not found"));
        return NULL;
    }

    if (Size < EKM1_HEADER_SIZE) {
        WARNING(TEXT("Layout file too small"));
        goto Out;
    }

    {
        const U8 Header[EKM1_HEADER_SIZE] = {'E', 'K', 'M', '1'};
        if (MemoryCompare(Buffer, Header, EKM1_HEADER_SIZE) != 0) {
            WARNING(TEXT("Invalid layout header"));
            goto Out;
        }
    }

    Layout = (KEY_LAYOUT_HID *)KernelHeapAlloc(sizeof(KEY_LAYOUT_HID));
    if (Layout == NULL) goto Out;

    MemorySet(Layout, 0, sizeof(KEY_LAYOUT_HID));

    Entries = (KEY_LAYOUT_HID_ENTRY *)KernelHeapAlloc(sizeof(KEY_LAYOUT_HID_ENTRY) * (KEY_USAGE_MAX + 1));
    if (Entries == NULL) goto Out_Error;
    MemorySet(Entries, 0, sizeof(KEY_LAYOUT_HID_ENTRY) * (KEY_USAGE_MAX + 1));

    DeadKeys = (KEY_HID_DEAD_KEY *)KernelHeapAlloc(sizeof(KEY_HID_DEAD_KEY) * KEY_LAYOUT_HID_MAX_DEAD_KEYS);
    if (DeadKeys == NULL) goto Out_Error;
    MemorySet(DeadKeys, 0, sizeof(KEY_HID_DEAD_KEY) * KEY_LAYOUT_HID_MAX_DEAD_KEYS);

    ComposeEntries = (KEY_HID_COMPOSE_ENTRY *)KernelHeapAlloc(sizeof(KEY_HID_COMPOSE_ENTRY) * KEY_LAYOUT_HID_MAX_COMPOSE);
    if (ComposeEntries == NULL) goto Out_Error;
    MemorySet(ComposeEntries, 0, sizeof(KEY_HID_COMPOSE_ENTRY) * KEY_LAYOUT_HID_MAX_COMPOSE);

    MemorySet(MapSeen, 0, sizeof(MapSeen));

    Layout->Code = NULL;
    Layout->LevelCount = 1;
    Layout->Entries = Entries;
    Layout->EntryCount = KEY_USAGE_MAX + 1;
    Layout->DeadKeys = DeadKeys;
    Layout->DeadKeyCount = 0;
    Layout->ComposeEntries = ComposeEntries;
    Layout->ComposeCount = 0;

    Cursor.Bytes = Buffer;
    Cursor.Size = Size;
    Cursor.Offset = EKM1_HEADER_SIZE;
    Cursor.Line = 1;
    Cursor.Column = 0;
    Cursor.DecodeErrors = 0;

    while (ReadLineTokens(&Cursor, Tokens, &TokenCount, &LineNumber, &LineOffset, &EndOfFile) == TRUE) {
        if (TokenCount == 0) {
            if (EndOfFile) break;
            continue;
        }

        if (StringCompare(Tokens[0], TEXT("code")) == 0) {
            UINT Length = 0;
            LPSTR CodeCopy = NULL;

            if (TokenCount != 2) {
                ERROR(TEXT("Line %u: Invalid code directive"), LineNumber);
                goto Out_Error;
            }

            if (LayoutHasCode) {
                ERROR(TEXT("Line %u: Duplicate code directive"), LineNumber);
                goto Out_Error;
            }

            Length = StringLength(Tokens[1]);
            if (Length == 0 || Length >= EKM1_TOKEN_MAX) {
                ERROR(TEXT("Line %u: Invalid layout code"), LineNumber);
                goto Out_Error;
            }

            CodeCopy = (LPSTR)KernelHeapAlloc(Length + 1);
            if (CodeCopy == NULL) goto Out_Error;
            StringCopy(CodeCopy, Tokens[1]);
            Layout->Code = CodeCopy;
            LayoutHasCode = TRUE;
        } else if (StringCompare(Tokens[0], TEXT("levels")) == 0) {
            U32 Levels = 0;

            if (TokenCount != 2) {
                ERROR(TEXT("Line %u: Invalid levels directive"), LineNumber);
                goto Out_Error;
            }

            if (MapSeenAny) {
                ERROR(TEXT("Line %u: Levels must appear before map entries"), LineNumber);
                goto Out_Error;
            }

            if (ParseDecToken(Tokens[1], &Levels) == FALSE) {
                ERROR(TEXT("Line %u: Invalid levels value"), LineNumber);
                goto Out_Error;
            }

            if (Levels == 0 || Levels > KEY_LAYOUT_HID_MAX_LEVELS) {
                ERROR(TEXT("Line %u: Levels out of range"), LineNumber);
                goto Out_Error;
            }

            Layout->LevelCount = (UINT)Levels;
            LayoutHasLevels = TRUE;
        } else if (StringCompare(Tokens[0], TEXT("map")) == 0) {
            U32 Usage = 0;
            U32 Level = 0;
            U32 VirtualKey = 0;
            U32 Ascii = 0;
            U32 Unicode = 0;
            UINT Index = 0;

            if (TokenCount != 6) {
                ERROR(TEXT("Line %u: Invalid map directive"), LineNumber);
                goto Out_Error;
            }

            if (ParseHexToken(Tokens[1], &Usage) == FALSE ||
                ParseDecToken(Tokens[2], &Level) == FALSE ||
                ParseHexToken(Tokens[3], &VirtualKey) == FALSE ||
                ParseHexToken(Tokens[4], &Ascii) == FALSE ||
                ParseHexToken(Tokens[5], &Unicode) == FALSE) {
                ERROR(TEXT("Line %u: Invalid map values"), LineNumber);
                goto Out_Error;
            }

            if (Usage < KEY_USAGE_MIN || Usage > KEY_USAGE_MAX) {
                ERROR(TEXT("Line %u: Usage out of range"), LineNumber);
                goto Out_Error;
            }

            if (Level >= Layout->LevelCount) {
                ERROR(TEXT("Line %u: Level out of range"), LineNumber);
                goto Out_Error;
            }

            if (VirtualKey > 0xFF || Ascii > 0xFF || Unicode > 0xFFFF) {
                ERROR(TEXT("Line %u: Keycode out of range"), LineNumber);
                goto Out_Error;
            }

            Index = (UINT)((Usage * KEY_LAYOUT_HID_MAX_LEVELS) + Level);
            if (MapSeen[Index] != 0) {
                ERROR(TEXT("Line %u: Duplicate map entry"), LineNumber);
                goto Out_Error;
            }

            Entries[Usage].Levels[Level].VirtualKey = (U8)VirtualKey;
            Entries[Usage].Levels[Level].ASCIICode = (STR)Ascii;
            Entries[Usage].Levels[Level].Unicode = (USTR)Unicode;
            MapSeen[Index] = 1;
            MapSeenAny = TRUE;
        } else if (StringCompare(Tokens[0], TEXT("dead")) == 0) {
            U32 DeadKey = 0;
            U32 BaseKey = 0;
            U32 Result = 0;

            if (TokenCount != 4) {
                ERROR(TEXT("Line %u: Invalid dead directive"), LineNumber);
                goto Out_Error;
            }

            if (ParseHexToken(Tokens[1], &DeadKey) == FALSE ||
                ParseHexToken(Tokens[2], &BaseKey) == FALSE ||
                ParseHexToken(Tokens[3], &Result) == FALSE) {
                ERROR(TEXT("Line %u: Invalid dead values"), LineNumber);
                goto Out_Error;
            }

            if (Layout->DeadKeyCount >= KEY_LAYOUT_HID_MAX_DEAD_KEYS) {
                ERROR(TEXT("Line %u: Dead key table full"), LineNumber);
                goto Out_Error;
            }

            DeadKeys[Layout->DeadKeyCount].DeadKey = DeadKey;
            DeadKeys[Layout->DeadKeyCount].BaseKey = BaseKey;
            DeadKeys[Layout->DeadKeyCount].Result = Result;
            Layout->DeadKeyCount++;
        } else if (StringCompare(Tokens[0], TEXT("compose")) == 0) {
            U32 FirstKey = 0;
            U32 SecondKey = 0;
            U32 Result = 0;

            if (TokenCount != 4) {
                ERROR(TEXT("Line %u: Invalid compose directive"), LineNumber);
                goto Out_Error;
            }

            if (ParseHexToken(Tokens[1], &FirstKey) == FALSE ||
                ParseHexToken(Tokens[2], &SecondKey) == FALSE ||
                ParseHexToken(Tokens[3], &Result) == FALSE) {
                ERROR(TEXT("Line %u: Invalid compose values"), LineNumber);
                goto Out_Error;
            }

            if (Layout->ComposeCount >= KEY_LAYOUT_HID_MAX_COMPOSE) {
                ERROR(TEXT("Line %u: Compose table full"), LineNumber);
                goto Out_Error;
            }

            ComposeEntries[Layout->ComposeCount].FirstKey = FirstKey;
            ComposeEntries[Layout->ComposeCount].SecondKey = SecondKey;
            ComposeEntries[Layout->ComposeCount].Result = Result;
            Layout->ComposeCount++;
        } else {
            ERROR(TEXT("Line %u: Unknown directive %s"), LineNumber, Tokens[0]);
            goto Out_Error;
        }

        if (EndOfFile) break;
    }

    if (LayoutHasCode == FALSE) {
        ERROR(TEXT("Missing code directive"));
        goto Out_Error;
    }

    if (LayoutHasLevels == FALSE) {
        WARNING(TEXT("Missing levels directive, using default"));
    }

    if (Cursor.DecodeErrors != 0) {
        WARNING(TEXT("UTF-8 replacements: %u"), Cursor.DecodeErrors);
    }

    KernelHeapFree(Buffer);
    return Layout;

Out_Error:
    FreeKeyboardLayoutData(Layout);
Out:
    KernelHeapFree(Buffer);
    return NULL;
}

/***************************************************************************/
