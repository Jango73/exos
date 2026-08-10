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


    Desktop theme parser

\************************************************************************/

#include "Desktop-ThemeParser.h"
#include "Desktop-ThemeTokens.h"
#include "text/CoreString.h"
#include "core/Kernel.h"
#include "utils/TOML.h"

/***************************************************************************/

/**
 * @brief Duplicate a string to kernel heap.
 * @param Source Source string.
 * @return Duplicated string or NULL.
 */
static LPSTR ThemeDuplicateString(LPCSTR Source) {
    LPSTR Text;
    UINT Length;

    if (Source == NULL) return NULL;

    Length = StringLength(Source);
    Text = (LPSTR)KernelHeapAlloc(Length + 1);
    if (Text == NULL) return NULL;

    StringCopy(Text, Source);
    return Text;
}

/***************************************************************************/

/**
 * @brief Free all entries in one runtime table.
 * @param Entries Table pointer.
 * @param Count Number of entries.
 */
static void ThemeFreeTableEntries(LPDESKTOP_THEME_TABLE_ENTRY Entries, U32 Count) {
    U32 Index;

    if (Entries == NULL) return;

    for (Index = 0; Index < Count; Index++) {
        if (Entries[Index].Key) KernelHeapFree(Entries[Index].Key);
        if (Entries[Index].Value) KernelHeapFree(Entries[Index].Value);
    }
}

/***************************************************************************/

void DesktopThemeFreeRuntime(LPDESKTOP_THEME_RUNTIME Runtime) {
    if (Runtime == NULL) return;
    if (Runtime->IsBuiltin) {
        KernelHeapFree(Runtime);
        return;
    }

    ThemeFreeTableEntries(Runtime->Tokens, Runtime->TokenCount);
    ThemeFreeTableEntries(Runtime->ElementProperties, Runtime->ElementPropertyCount);
    ThemeFreeTableEntries(Runtime->Recipes, Runtime->RecipeCount);
    ThemeFreeTableEntries(Runtime->Bindings, Runtime->BindingCount);

    if (Runtime->Tokens) KernelHeapFree(Runtime->Tokens);
    if (Runtime->ElementProperties) KernelHeapFree(Runtime->ElementProperties);
    if (Runtime->Recipes) KernelHeapFree(Runtime->Recipes);
    if (Runtime->Bindings) KernelHeapFree(Runtime->Bindings);
    KernelHeapFree(Runtime);
}

/***************************************************************************/

LPDESKTOP_THEME_RUNTIME DesktopThemeCreateBuiltinRuntime(void) {
    LPDESKTOP_THEME_RUNTIME Runtime = (LPDESKTOP_THEME_RUNTIME)KernelHeapAlloc(sizeof(DESKTOP_THEME_RUNTIME));
    if (Runtime == NULL) return NULL;

    MemorySet(Runtime, 0, sizeof(DESKTOP_THEME_RUNTIME));
    Runtime->IsBuiltin = TRUE;
    return Runtime;
}

/***************************************************************************/

/**
 * @brief Compare start of one string with a prefix.
 * @param Text Full string.
 * @param Prefix Prefix to test.
 * @return TRUE when Text starts with Prefix.
 */
static BOOL ThemeStartsWith(LPCSTR Text, LPCSTR Prefix) {
    UINT Index;

    if (Text == NULL || Prefix == NULL) return FALSE;

    for (Index = 0; Prefix[Index] != STR_NULL; Index++) {
        if (Text[Index] != Prefix[Index]) return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check if one character is hexadecimal.
 * @param Ch Character to test.
 * @return TRUE when hexadecimal.
 */
static BOOL ThemeIsHexDigit(STR Ch) {
    if (Ch >= '0' && Ch <= '9') return TRUE;
    if (Ch >= 'a' && Ch <= 'f') return TRUE;
    if (Ch >= 'A' && Ch <= 'F') return TRUE;
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Return TRUE when a value is a token reference.
 * @param Value Input value.
 * @return TRUE for values prefixed by token:.
 */
static BOOL ThemeIsTokenReference(LPCSTR Value) {
    return ThemeStartsWith(Value, TEXT("token:"));
}

/***************************************************************************/

/**
 * @brief Validate color value syntax.
 * @param Value Input value.
 * @return TRUE when syntax is acceptable.
 */
static BOOL ThemeIsColorValue(LPCSTR Value) {
    UINT Index;
    UINT Length;

    if (Value == NULL || Value[0] == STR_NULL) return FALSE;

    if (ThemeIsTokenReference(Value)) return TRUE;

    if (ThemeStartsWith(Value, TEXT("0x")) || ThemeStartsWith(Value, TEXT("0X"))) {
        Length = StringLength(Value);
        if (Length <= 2) return FALSE;
        for (Index = 2; Index < Length; Index++) {
            if (!ThemeIsHexDigit(Value[Index])) return FALSE;
        }
        return TRUE;
    }

    if (Value[0] == '#') {
        Length = StringLength(Value);
        if (Length != 7 && Length != 9) return FALSE;
        for (Index = 1; Index < Length; Index++) {
            if (!ThemeIsHexDigit(Value[Index])) return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Validate metric value syntax.
 * @param Value Input value.
 * @return TRUE when syntax is acceptable.
 */
static BOOL ThemeIsMetricValue(LPCSTR Value) {
    UINT Index;
    UINT Length;

    if (Value == NULL || Value[0] == STR_NULL) return FALSE;

    if (ThemeIsTokenReference(Value)) return TRUE;

    if (ThemeStartsWith(Value, TEXT("0x")) || ThemeStartsWith(Value, TEXT("0X"))) {
        Length = StringLength(Value);
        if (Length <= 2) return FALSE;
        for (Index = 2; Index < Length; Index++) {
            if (!ThemeIsHexDigit(Value[Index])) return FALSE;
        }
        return TRUE;
    }

    Length = StringLength(Value);
    for (Index = 0; Index < Length; Index++) {
        if (Value[Index] < '0' || Value[Index] > '9') return FALSE;
    }

    return (Length > 0);
}

/***************************************************************************/

/**
 * @brief Validate boolean value syntax.
 * @param Value Input value.
 * @return TRUE when syntax is acceptable.
 */
static BOOL ThemeIsBooleanValue(LPCSTR Value) {
    if (Value == NULL || Value[0] == STR_NULL) return FALSE;
    if (StringCompareNC(Value, TEXT("true")) == 0) return TRUE;
    if (StringCompareNC(Value, TEXT("false")) == 0) return TRUE;
    if (StringCompareNC(Value, TEXT("1")) == 0) return TRUE;
    if (StringCompareNC(Value, TEXT("0")) == 0) return TRUE;
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Validate one property value against schema type.
 * @param PropertyType DESKTOP_THEME_PROPERTY_TYPE_* value.
 * @param Value Property value string.
 * @return TRUE when value matches expected type.
 */
static BOOL ThemeIsValueCompatibleWithType(U32 PropertyType, LPCSTR Value) {
    switch (PropertyType) {
        case DESKTOP_THEME_PROPERTY_TYPE_COLOR:
            return ThemeIsColorValue(Value);
        case DESKTOP_THEME_PROPERTY_TYPE_METRIC:
            return ThemeIsMetricValue(Value);
        case DESKTOP_THEME_PROPERTY_TYPE_BOOLEAN:
            return ThemeIsBooleanValue(Value);
        case DESKTOP_THEME_PROPERTY_TYPE_STRING:
            return (Value != NULL && Value[0] != STR_NULL);
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Split one TOML key into top-level section and local key.
 * @param Key Full key.
 * @param SectionOut Receives section name.
 * @param SectionOutSize Section buffer size.
 * @param LocalOut Receives local key path.
 * @param LocalOutSize Local buffer size.
 * @return TRUE when split succeeds.
 */
static BOOL ThemeSplitTopLevelKey(LPCSTR Key, LPSTR SectionOut, UINT SectionOutSize, LPSTR LocalOut, UINT LocalOutSize) {
    UINT Index;

    if (Key == NULL || SectionOut == NULL || LocalOut == NULL) return FALSE;
    if (SectionOutSize == 0 || LocalOutSize == 0) return FALSE;

    for (Index = 0; Key[Index] != STR_NULL && Key[Index] != '.'; Index++) {
        if (Index + 1 >= SectionOutSize) return FALSE;
        SectionOut[Index] = Key[Index];
    }

    if (Key[Index] != '.') return FALSE;

    SectionOut[Index] = STR_NULL;
    if (Key[Index + 1] == STR_NULL) return FALSE;
    if (StringLength(Key + Index + 1) + 1 > LocalOutSize) return FALSE;
    StringCopy(LocalOut, Key + Index + 1);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Tell whether a full key already appeared in parsed list.
 * @param Current Current item.
 * @param FullKey Full key.
 * @return TRUE when duplicate exists before Current.
 */
static BOOL ThemeHasDuplicateKeyBefore(const TOML_ITEM* Current, LPCSTR FullKey) {
    const TOML_ITEM* Item;

    for (Item = Current; Item != NULL; Item = Item->Next) {
        if (StringCompareNC(Item->Key, FullKey) == 0) return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Parse an elements local key into family/state/property components.
 * @param LocalKey Key path after elements..
 * @param FamilyID Receives family identifier.
 * @param PropertyName Receives property name.
 * @param StateName Receives state name or empty string.
 * @return TRUE when key matches element schema.
 */
static BOOL ThemeParseElementPropertyKey(LPCSTR LocalKey, U32* FamilyID, LPSTR PropertyName, UINT PropertyNameSize, LPSTR StateName, UINT StateNameSize) {
    UINT Length;
    UINT Split;
    STR ElementCandidate[128];
    STR Remainder[160];
    LPSTR Dot1;
    LPSTR Dot2;

    if (LocalKey == NULL || FamilyID == NULL || PropertyName == NULL || StateName == NULL) return FALSE;

    Length = StringLength(LocalKey);
    if (Length == 0 || Length >= sizeof(Remainder)) return FALSE;

    for (Split = Length; Split > 0; Split--) {
        if (LocalKey[Split] != '.') continue;
        if (Split >= sizeof(ElementCandidate)) continue;

        MemoryCopy(ElementCandidate, LocalKey, Split);
        ElementCandidate[Split] = STR_NULL;

        if (DesktopThemeSchemaGetElementFamily(ElementCandidate, FamilyID) == FALSE) continue;

        StringCopy(Remainder, LocalKey + Split + 1);
        if (ThemeStartsWith(Remainder, TEXT("states.")) == FALSE) {
            if (StringLength(Remainder) + 1 > PropertyNameSize) return FALSE;
            StringCopy(PropertyName, Remainder);
            StateName[0] = STR_NULL;
            return TRUE;
        }

        Dot1 = StringFindChar(Remainder + 7, '.');
        if (Dot1 == NULL) return FALSE;
        *Dot1 = STR_NULL;
        Dot2 = Dot1 + 1;
        if (*Dot2 == STR_NULL) return FALSE;
        if (StringLength(Remainder + 7) + 1 > StateNameSize) return FALSE;
        if (StringLength(Dot2) + 1 > PropertyNameSize) return FALSE;
        StringCopy(StateName, Remainder + 7);
        StringCopy(PropertyName, Dot2);
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Check if a token key exists in parsed TOML.
 * @param Toml Parsed TOML.
 * @param TokenName Token key without tokens. prefix.
 * @return TRUE when token exists.
 */
static BOOL ThemeTokenExists(LPTOML Toml, LPCSTR TokenName) {
    STR FullKey[160];

    if (Toml == NULL || TokenName == NULL) return FALSE;
    if (StringLength(TokenName) == 0) return FALSE;
    if (StringLength(TokenName) + 8 >= sizeof(FullKey)) return FALSE;

    StringCopy(FullKey, TEXT("tokens."));
    StringConcat(FullKey, TokenName);
    if (TomlGet(Toml, FullKey) != NULL) return TRUE;
    return DesktopThemeTokenNameExists(TokenName);
}

/***************************************************************************/

/**
 * @brief Check if a recipe identifier exists in parsed TOML.
 * @param Toml Parsed TOML.
 * @param RecipeID Recipe identifier.
 * @return TRUE when recipe exists.
 */
static BOOL ThemeRecipeExists(LPTOML Toml, LPCSTR RecipeID) {
    STR FullKey[160];

    if (Toml == NULL || RecipeID == NULL) return FALSE;
    if (StringLength(RecipeID) == 0) return FALSE;
    if (StringLength(RecipeID) + 14 >= sizeof(FullKey)) return FALSE;

    StringCopy(FullKey, TEXT("recipes."));
    StringConcat(FullKey, RecipeID);
    StringConcat(FullKey, TEXT(".steps"));
    return (TomlGet(Toml, FullKey) != NULL);
}

/***************************************************************************/

/**
 * @brief Count recipe primitives from one textual steps value.
 * @param StepsText Recipe steps value.
 * @return Number of primitive entries.
 */
static U32 ThemeCountRecipePrimitives(LPCSTR StepsText) {
    U32 Count = 0;
    UINT Index;

    if (StepsText == NULL) return 0;

    for (Index = 0; StepsText[Index] != STR_NULL; Index++) {
        if (StepsText[Index] == '{') Count++;
    }

    return Count;
}

/***************************************************************************/

/**
 * @brief Validate every token: reference found in one string value.
 * @param Toml Parsed TOML.
 * @param Value Value to scan.
 * @return TRUE when all token references exist.
 */
static BOOL ThemeValidateTokenReferencesInValue(LPTOML Toml, LPCSTR Value) {
    UINT Index;

    if (Toml == NULL || Value == NULL) return FALSE;

    for (Index = 0; Value[Index] != STR_NULL; Index++) {
        STR Token[160];
        UINT TokenIndex = 0;
        UINT Scan;

        if (!ThemeStartsWith(Value + Index, TEXT("token:"))) continue;

        Scan = Index + 6;
        while (Value[Scan] != STR_NULL) {
            STR Ch = Value[Scan];
            BOOL IsNameChar = ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') || Ch == '_' || Ch == '.' || Ch == '-');
            if (IsNameChar == FALSE) break;
            if (TokenIndex + 1 >= sizeof(Token)) return FALSE;
            Token[TokenIndex++] = Ch;
            Scan++;
        }

        Token[TokenIndex] = STR_NULL;
        if (TokenIndex == 0) return FALSE;
        if (ThemeTokenExists(Toml, Token) == FALSE) return FALSE;
        Index = Scan;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build one runtime entry array from section-prefixed TOML items.
 * @param Toml Parsed TOML.
 * @param Prefix Section prefix (for example "tokens.").
 * @param Entries Destination array.
 * @param EntryCount Number of entries expected.
 * @return TRUE on success.
 */
static BOOL ThemeBuildEntriesFromPrefix(LPTOML Toml, LPCSTR Prefix, LPDESKTOP_THEME_TABLE_ENTRY Entries, U32 EntryCount) {
    LPTOMLITEM Item;
    U32 Index = 0;
    UINT PrefixLength;

    if (Toml == NULL || Prefix == NULL || Entries == NULL) return FALSE;

    PrefixLength = StringLength(Prefix);

    for (Item = Toml->First; Item; Item = Item->Next) {
        if (ThemeStartsWith(Item->Key, Prefix) == FALSE) continue;
        if (Index >= EntryCount) return FALSE;

        Entries[Index].Key = ThemeDuplicateString(Item->Key + PrefixLength);
        Entries[Index].Value = ThemeDuplicateString(Item->Value);
        if (Entries[Index].Key == NULL || Entries[Index].Value == NULL) return FALSE;
        Index++;
    }

    return (Index == EntryCount);
}

/***************************************************************************/

/**
 * @brief Build runtime tables after strict validation succeeds.
 * @param Toml Parsed TOML.
 * @param TokenCount Number of token entries.
 * @param ElementCount Number of element properties.
 * @param RecipeCount Number of recipes.
 * @param BindingCount Number of bindings.
 * @param PrimitiveCount Number of recipe primitives.
 * @param RuntimeOut Receives allocated runtime.
 * @return TRUE on success.
 */
static BOOL ThemeBuildRuntimeTables(
    LPTOML Toml,
    U32 TokenCount,
    U32 ElementCount,
    U32 RecipeCount,
    U32 BindingCount,
    U32 PrimitiveCount,
    LPDESKTOP_THEME_RUNTIME* RuntimeOut
) {
    LPDESKTOP_THEME_RUNTIME Runtime;

    if (RuntimeOut == NULL) return FALSE;
    *RuntimeOut = NULL;

    Runtime = (LPDESKTOP_THEME_RUNTIME)KernelHeapAlloc(sizeof(DESKTOP_THEME_RUNTIME));
    if (Runtime == NULL) return FALSE;
    MemorySet(Runtime, 0, sizeof(DESKTOP_THEME_RUNTIME));

    Runtime->TokenCount = TokenCount;
    Runtime->ElementPropertyCount = ElementCount;
    Runtime->RecipeCount = RecipeCount;
    Runtime->BindingCount = BindingCount;
    Runtime->PrimitiveCount = PrimitiveCount;

    if (TokenCount > 0) {
        Runtime->Tokens = (LPDESKTOP_THEME_TABLE_ENTRY)KernelHeapAlloc(sizeof(DESKTOP_THEME_TABLE_ENTRY) * TokenCount);
        if (Runtime->Tokens == NULL) goto Fail;
        MemorySet(Runtime->Tokens, 0, sizeof(DESKTOP_THEME_TABLE_ENTRY) * TokenCount);
    }

    if (ElementCount > 0) {
        Runtime->ElementProperties = (LPDESKTOP_THEME_TABLE_ENTRY)KernelHeapAlloc(sizeof(DESKTOP_THEME_TABLE_ENTRY) * ElementCount);
        if (Runtime->ElementProperties == NULL) goto Fail;
        MemorySet(Runtime->ElementProperties, 0, sizeof(DESKTOP_THEME_TABLE_ENTRY) * ElementCount);
    }

    if (RecipeCount > 0) {
        Runtime->Recipes = (LPDESKTOP_THEME_TABLE_ENTRY)KernelHeapAlloc(sizeof(DESKTOP_THEME_TABLE_ENTRY) * RecipeCount);
        if (Runtime->Recipes == NULL) goto Fail;
        MemorySet(Runtime->Recipes, 0, sizeof(DESKTOP_THEME_TABLE_ENTRY) * RecipeCount);
    }

    if (BindingCount > 0) {
        Runtime->Bindings = (LPDESKTOP_THEME_TABLE_ENTRY)KernelHeapAlloc(sizeof(DESKTOP_THEME_TABLE_ENTRY) * BindingCount);
        if (Runtime->Bindings == NULL) goto Fail;
        MemorySet(Runtime->Bindings, 0, sizeof(DESKTOP_THEME_TABLE_ENTRY) * BindingCount);
    }

    if (TokenCount > 0 && ThemeBuildEntriesFromPrefix(Toml, TEXT("tokens."), Runtime->Tokens, TokenCount) == FALSE) goto Fail;
    if (ElementCount > 0 && ThemeBuildEntriesFromPrefix(Toml, TEXT("elements."), Runtime->ElementProperties, ElementCount) == FALSE) goto Fail;
    if (RecipeCount > 0 && ThemeBuildEntriesFromPrefix(Toml, TEXT("recipes."), Runtime->Recipes, RecipeCount) == FALSE) goto Fail;
    if (BindingCount > 0 && ThemeBuildEntriesFromPrefix(Toml, TEXT("bindings."), Runtime->Bindings, BindingCount) == FALSE) goto Fail;

    *RuntimeOut = Runtime;
    return TRUE;

Fail:
    DesktopThemeFreeRuntime(Runtime);
    return FALSE;
}

/***************************************************************************/

BOOL DesktopThemeParseStrict(LPCSTR Source, LPDESKTOP_THEME_RUNTIME* Runtime, U32* Status) {
    DESKTOP_THEME_SCHEMA_LIMITS Limits;
    LPTOML Toml;
    LPTOMLITEM Item;
    U32 TokenCount = 0;
    U32 ElementCount = 0;
    U32 RecipeCount = 0;
    U32 BindingCount = 0;
    U32 PrimitiveCount = 0;

    if (Runtime == NULL) return FALSE;
    *Runtime = NULL;

    if (Status) *Status = DESKTOP_THEME_STATUS_SUCCESS;
    if (Source == NULL) {
        if (Status) *Status = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        return FALSE;
    }

    if (DesktopThemeSchemaGetLimits(&Limits) == FALSE) {
        if (Status) *Status = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        return FALSE;
    }

    if (StringLength(Source) > Limits.MaxFileSize) {
        if (Status) *Status = DESKTOP_THEME_STATUS_TOO_LARGE;
        return FALSE;
    }

    Toml = TomlParse(Source);
    if (Toml == NULL) {
        if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_TOML;
        return FALSE;
    }

    for (Item = Toml->First; Item; Item = Item->Next) {
        STR Section[64];
        STR LocalKey[192];
        U32 SectionID;

        if (ThemeHasDuplicateKeyBefore(Item->Next, Item->Key)) {
            if (Status) *Status = DESKTOP_THEME_STATUS_DUPLICATE_KEY;
            goto Fail;
        }

        if (ThemeSplitTopLevelKey(Item->Key, Section, sizeof(Section), LocalKey, sizeof(LocalKey)) == FALSE) {
            if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
            goto Fail;
        }

        if (DesktopThemeSchemaGetTopLevelSectionID(Section, &SectionID) == FALSE) {
            if (Status) *Status = DESKTOP_THEME_STATUS_UNKNOWN_SECTION;
            goto Fail;
        }

        switch (SectionID) {
            case DESKTOP_THEME_SECTION_THEME: {
                if (StringCompareNC(LocalKey, TEXT("name")) != 0 &&
                    StringCompareNC(LocalKey, TEXT("author")) != 0 &&
                    StringCompareNC(LocalKey, TEXT("version")) != 0 &&
                    StringCompareNC(LocalKey, TEXT("description")) != 0) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }
            } break;

            case DESKTOP_THEME_SECTION_TOKENS: {
                if (LocalKey[0] == STR_NULL || Item->Value[0] == STR_NULL) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }
                TokenCount++;
                if (TokenCount > Limits.MaxTokenCount) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_LIMIT_EXCEEDED;
                    goto Fail;
                }
            } break;

            case DESKTOP_THEME_SECTION_ELEMENTS: {
                U32 FamilyID;
                U32 PropertyType;
                STR PropertyName[64];
                STR StateName[64];

                if (ThemeParseElementPropertyKey(LocalKey, &FamilyID, PropertyName, sizeof(PropertyName), StateName, sizeof(StateName)) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_PROPERTY;
                    goto Fail;
                }

                if (StateName[0] != STR_NULL && DesktopThemeSchemaIsStateID(StateName) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_PROPERTY;
                    goto Fail;
                }

                if (DesktopThemeSchemaGetPropertyType(FamilyID, PropertyName, &PropertyType) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_PROPERTY;
                    goto Fail;
                }

                if (ThemeIsValueCompatibleWithType(PropertyType, Item->Value) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_PROPERTY_TYPE;
                    goto Fail;
                }

                ElementCount++;
            } break;

            case DESKTOP_THEME_SECTION_RECIPES: {
                LPSTR Dot = StringFindChar(LocalKey, '.');

                if (Dot == NULL || Dot[1] == STR_NULL) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                if (StringCompareNC(Dot + 1, TEXT("steps")) != 0) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                RecipeCount++;
                if (RecipeCount > Limits.MaxRecipeCount) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_LIMIT_EXCEEDED;
                    goto Fail;
                }

                PrimitiveCount += ThemeCountRecipePrimitives(Item->Value);
                if (PrimitiveCount > Limits.MaxPrimitiveCount) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_LIMIT_EXCEEDED;
                    goto Fail;
                }
            } break;

            case DESKTOP_THEME_SECTION_BINDINGS: {
                LPSTR Dot = StringFindChar(LocalKey, '.');
                STR ElementID[128];
                U32 ElementFamily;

                if (Dot == NULL || Dot[1] == STR_NULL) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                if (StringLength(LocalKey) >= sizeof(ElementID)) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                MemoryCopy(ElementID, LocalKey, Dot - LocalKey);
                ElementID[Dot - LocalKey] = STR_NULL;

                if (DesktopThemeSchemaGetElementFamily(ElementID, &ElementFamily) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                if (DesktopThemeSchemaIsStateID(Dot + 1) == FALSE) {
                    if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
                    goto Fail;
                }

                BindingCount++;
            } break;
        }
    }

    for (Item = Toml->First; Item; Item = Item->Next) {
        STR Section[64];
        STR LocalKey[192];
        U32 SectionID;

        if (ThemeSplitTopLevelKey(Item->Key, Section, sizeof(Section), LocalKey, sizeof(LocalKey)) == FALSE) {
            if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_KEY;
            goto Fail;
        }
        if (DesktopThemeSchemaGetTopLevelSectionID(Section, &SectionID) == FALSE) {
            if (Status) *Status = DESKTOP_THEME_STATUS_UNKNOWN_SECTION;
            goto Fail;
        }

        if (SectionID == DESKTOP_THEME_SECTION_ELEMENTS || SectionID == DESKTOP_THEME_SECTION_RECIPES) {
            if (ThemeValidateTokenReferencesInValue(Toml, Item->Value) == FALSE) {
                if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_REFERENCE;
                goto Fail;
            }
        }

        if (SectionID == DESKTOP_THEME_SECTION_BINDINGS) {
            if (ThemeRecipeExists(Toml, Item->Value) == FALSE) {
                if (Status) *Status = DESKTOP_THEME_STATUS_INVALID_REFERENCE;
                goto Fail;
            }
        }
    }

    if (ThemeBuildRuntimeTables(Toml, TokenCount, ElementCount, RecipeCount, BindingCount, PrimitiveCount, Runtime) == FALSE) {
        if (Status) *Status = DESKTOP_THEME_STATUS_NO_MEMORY;
        goto Fail;
    }

    if (Status) *Status = DESKTOP_THEME_STATUS_SUCCESS;
    TomlFree(Toml);
    return TRUE;

Fail:
    TomlFree(Toml);
    return FALSE;
}

/***************************************************************************/

BOOL DesktopThemeActivateParsed(LPDESKTOP_THEME_RUNTIME Candidate, LPDESKTOP_THEME_RUNTIME Fallback, LPDESKTOP_THEME_RUNTIME* ActiveRuntime) {
    LPDESKTOP_THEME_RUNTIME Previous;

    if (ActiveRuntime == NULL) return FALSE;

    if (Candidate == NULL) {
        if (*ActiveRuntime == NULL && Fallback != NULL) *ActiveRuntime = Fallback;
        return FALSE;
    }

    Previous = *ActiveRuntime;
    *ActiveRuntime = Candidate;

    if (Previous != NULL && Previous != Fallback) {
        DesktopThemeFreeRuntime(Previous);
    }

    return TRUE;
}
