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


    Desktop theme runtime API

\************************************************************************/

#include "Desktop-ThemeRuntime.h"
#include "Desktop-Private.h"
#include "text/CoreString.h"
#include "Desktop.h"
#include "Desktop-ThemeTokens.h"
#include "fs/File.h"
#include "core/Kernel.h"
#include "log/Log.h"

/***************************************************************************/

/**
 * @brief Resolve one desktop target only for theme-related redraw operations.
 * @param Desktop Requested desktop or NULL for current process desktop.
 * @return Valid desktop pointer or NULL.
 */
static LPDESKTOP ThemeResolveDesktopForInvalidation(LPDESKTOP Desktop) {
    LPPROCESS Process;

    if (Desktop != NULL && Desktop->TypeID == KOID_DESKTOP) return Desktop;

    Process = GetCurrentProcess();
    if (Process != NULL && Process->TypeID == KOID_PROCESS) {
        if (Process->Desktop != NULL && Process->Desktop->TypeID == KOID_DESKTOP) {
            return Process->Desktop;
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Ensure global theme runtime pointers are initialized.
 * @return TRUE on success.
 */
static BOOL ThemeEnsureRuntimeState(void) {
    LPDESKTOP_THEME Theme;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;

    Theme = GetGlobalThemeState();
    if (Theme == NULL) return FALSE;

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Builtin;

    if (BuiltinRuntime == NULL) {
        BuiltinRuntime = DesktopThemeCreateBuiltinRuntime();
        if (BuiltinRuntime == NULL) {
            Theme->LastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
            Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
            return FALSE;
        }
        Theme->Builtin = BuiltinRuntime;
    }

    if (Theme->Active == NULL) {
        Theme->Active = BuiltinRuntime;
        Theme->ActiveFromFile = FALSE;
        Theme->ActivePath[0] = STR_NULL;
    }

    if (Theme->LastStatus == 0) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_SUCCESS;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Find one runtime table entry value by key.
 * @param Entries Runtime table.
 * @param EntryCount Number of entries.
 * @param Key Entry key.
 * @param Value Receives value pointer.
 * @return TRUE when key exists.
 */
static BOOL ThemeFindRuntimeEntry(
    LPDESKTOP_THEME_TABLE_ENTRY Entries,
    U32 EntryCount,
    LPCSTR Key,
    LPCSTR* Value
) {
    U32 Index;

    if (Entries == NULL || Key == NULL || Value == NULL) return FALSE;

    for (Index = 0; Index < EntryCount; Index++) {
        if (Entries[Index].Key == NULL || Entries[Index].Value == NULL) continue;
        if (StringCompareNC(Entries[Index].Key, Key) != 0) continue;
        *Value = Entries[Index].Value;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Invalidate one window and every child window for full redraw.
 * @param Window Root window of the invalidation traversal.
 */
static void ThemeInvalidateWindowTree(LPWINDOW Window) {
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    UINT ChildCount = 0;
    UINT ChildIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    (void)InvalidateWindowRect((HANDLE)Window, NULL);
    (void)DesktopSnapshotWindowChildren(Window, &Children, &ChildCount);

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Child = Children[ChildIndex];
        if (Child != NULL && Child->TypeID == KOID_WINDOW) {
            ThemeInvalidateWindowTree(Child);
        }
    }
    if (Children != NULL) {
        KernelHeapFree(Children);
    }
}

/***************************************************************************/

/**
 * @brief Invalidate all desktop windows after a theme switch.
 * @param Desktop Target desktop.
 */
static void ThemeInvalidateDesktopWindows(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return;

    ThemeInvalidateWindowTree(Desktop->Window);
}

/***************************************************************************/

BOOL LoadTheme(LPCSTR Path) {
    LPDESKTOP_THEME Theme;
    LPSTR Source;
    UINT SourceSize = 0;
    LPDESKTOP_THEME_RUNTIME Candidate = NULL;
    LPDESKTOP_THEME_RUNTIME PreviousStaged;
    U32 Status = DESKTOP_THEME_STATUS_SUCCESS;

    Theme = GetGlobalThemeState();
    if (Theme == NULL) return FALSE;
    if (ThemeEnsureRuntimeState() == FALSE) return FALSE;

    if (Path == NULL || Path[0] == STR_NULL) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED;
        WARNING(TEXT("Invalid theme path"));
        return FALSE;
    }

    Source = (LPSTR)FileReadAll(Path, &SourceSize);
    if (Source == NULL || SourceSize == 0) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_INVALID_TOML;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED;
        WARNING(TEXT("Cannot read theme file %s"), Path);
        if (Source != NULL) KernelHeapFree(Source);
        return FALSE;
    }

    if (DesktopThemeParseStrict(Source, &Candidate, &Status) == FALSE) {
        Theme->LastStatus = Status;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_PARSE_FAILED;
        WARNING(TEXT("Parse failed status=%x path=%s"), Status, Path);
        KernelHeapFree(Source);
        return FALSE;
    }

    PreviousStaged = (LPDESKTOP_THEME_RUNTIME)Theme->Staged;
    if (PreviousStaged != NULL &&
        PreviousStaged != (LPDESKTOP_THEME_RUNTIME)Theme->Active &&
        PreviousStaged != (LPDESKTOP_THEME_RUNTIME)Theme->Builtin) {
        DesktopThemeFreeRuntime(PreviousStaged);
    }

    Theme->Staged = Candidate;
    StringCopy(Theme->StagedPath, Path);
    Theme->LastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;


    KernelHeapFree(Source);
    return TRUE;
}

/***************************************************************************/

BOOL ActivateTheme(LPCSTR NameOrHandle) {
    LPDESKTOP Desktop;
    LPDESKTOP_THEME Theme;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;
    LPDESKTOP_THEME_RUNTIME StagedRuntime;
    STR PathToActivate[MAX_FILE_NAME];

    Desktop = ThemeResolveDesktopForInvalidation(NULL);
    Theme = GetGlobalThemeState();
    if (Theme == NULL) return FALSE;
    if (ThemeEnsureRuntimeState() == FALSE) return FALSE;

    if (NameOrHandle != NULL && NameOrHandle[0] != STR_NULL) {
        BOOL MatchesPath = (StringCompareNC(NameOrHandle, Theme->StagedPath) == 0);
        BOOL MatchesAlias = (StringCompareNC(NameOrHandle, TEXT("staged")) == 0 || StringCompareNC(NameOrHandle, TEXT("loaded")) == 0);
        if (MatchesPath == FALSE && MatchesAlias == FALSE) {
            Theme->LastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
            Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME;
            WARNING(TEXT("Unknown theme handle %s"), NameOrHandle);
            return FALSE;
        }
    }

    StagedRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Staged;
    if (StagedRuntime == NULL) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_BAD_PARAMETER;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME;
        WARNING(TEXT("No staged theme available"));
        return FALSE;
    }

    StringCopy(PathToActivate, Theme->StagedPath);

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Builtin;
    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Active;

    if (DesktopThemeActivateParsed(StagedRuntime, BuiltinRuntime, &ActiveRuntime) == FALSE) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
        WARNING(TEXT("Activation failed"));
        return FALSE;
    }

    Theme->Active = ActiveRuntime;
    Theme->Staged = NULL;
    Theme->StagedPath[0] = STR_NULL;
    Theme->LastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_NONE;

    if (ActiveRuntime == BuiltinRuntime) {
        Theme->ActiveFromFile = FALSE;
        Theme->ActivePath[0] = STR_NULL;
    } else {
        Theme->ActiveFromFile = TRUE;
        StringCopy(Theme->ActivePath, PathToActivate);
    }

    DesktopThemeSyncSystemObjects();
    ThemeInvalidateDesktopWindows(Desktop);

    return TRUE;
}

/***************************************************************************/

BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info) {
    LPDESKTOP_THEME Theme;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;

    if (Info == NULL) return FALSE;

    Theme = GetGlobalThemeState();
    if (Theme == NULL) return FALSE;
    if (ThemeEnsureRuntimeState() == FALSE) return FALSE;

    MemorySet(Info, 0, sizeof(DESKTOP_THEME_RUNTIME_INFO));

    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Active;
    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Builtin;

    Info->IsBuiltinActive = (ActiveRuntime == BuiltinRuntime);
    Info->IsLoadedActive = Theme->ActiveFromFile;
    Info->HasStagedTheme = (Theme->Staged != NULL);
    StringCopy(Info->ActiveThemePath, Theme->ActivePath);
    StringCopy(Info->StagedThemePath, Theme->StagedPath);
    Info->LastStatus = Theme->LastStatus;
    Info->LastFallbackReason = Theme->LastFallbackReason;

    if (ActiveRuntime != NULL) {
        Info->ActiveTokenCount = ActiveRuntime->TokenCount;
        Info->ActiveElementPropertyCount = ActiveRuntime->ElementPropertyCount;
        Info->ActiveRecipeCount = ActiveRuntime->RecipeCount;
        Info->ActiveBindingCount = ActiveRuntime->BindingCount;
    }

    return TRUE;
}

/***************************************************************************/

BOOL ResetThemeToDefault(void) {
    LPDESKTOP Desktop;
    LPDESKTOP_THEME Theme;
    LPDESKTOP_THEME_RUNTIME BuiltinRuntime;
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    Desktop = ThemeResolveDesktopForInvalidation(NULL);
    Theme = GetGlobalThemeState();
    if (Theme == NULL) return FALSE;
    if (ThemeEnsureRuntimeState() == FALSE) return FALSE;

    BuiltinRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Builtin;
    ActiveRuntime = (LPDESKTOP_THEME_RUNTIME)Theme->Active;

    if (DesktopThemeActivateParsed(NULL, BuiltinRuntime, &ActiveRuntime) == FALSE) {
        Theme->LastStatus = DESKTOP_THEME_STATUS_NO_MEMORY;
        Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED;
        WARNING(TEXT("Reset failed"));
        return FALSE;
    }

    Theme->Active = ActiveRuntime;
    Theme->ActiveFromFile = FALSE;
    Theme->ActivePath[0] = STR_NULL;
    Theme->LastStatus = DESKTOP_THEME_STATUS_SUCCESS;
    Theme->LastFallbackReason = DESKTOP_THEME_FALLBACK_REASON_RESET_TO_DEFAULT;

    DesktopThemeSyncSystemObjects();
    ThemeInvalidateDesktopWindows(Desktop);

    return TRUE;
}

/***************************************************************************/

BOOL ApplyDesktopTheme(LPCSTR Target) {
    if (Target == NULL || Target[0] == STR_NULL) {
        return FALSE;
    }

    if (StringCompareNC(Target, TEXT("default")) == 0 ||
        StringCompareNC(Target, TEXT("builtin")) == 0 ||
        StringCompareNC(Target, TEXT("built-in")) == 0) {
        return ResetThemeToDefault();
    }

    if (ActivateTheme(Target) != FALSE) {
        return TRUE;
    }

    if (LoadTheme(Target) == FALSE) {
        return FALSE;
    }

    return ActivateTheme(TEXT("staged"));
}

/***************************************************************************/

LPDESKTOP_THEME_RUNTIME DesktopThemeGetActiveRuntime(LPDESKTOP Desktop) {
    UNUSED(Desktop);
    if (ThemeEnsureRuntimeState() == FALSE) return NULL;

    return (LPDESKTOP_THEME_RUNTIME)GetGlobalThemeState()->Active;
}

/***************************************************************************/

BOOL DesktopThemeLookupTokenValue(LPDESKTOP Desktop, LPCSTR TokenName, LPCSTR* Value) {
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    if (TokenName == NULL || Value == NULL) return FALSE;

    ActiveRuntime = DesktopThemeGetActiveRuntime(Desktop);
    if (ActiveRuntime == NULL) return FALSE;

    return ThemeFindRuntimeEntry(ActiveRuntime->Tokens, ActiveRuntime->TokenCount, TokenName, Value);
}

/***************************************************************************/

BOOL DesktopThemeLookupElementPropertyValue(LPDESKTOP Desktop, LPCSTR ElementPropertyKey, LPCSTR* Value) {
    LPDESKTOP_THEME_RUNTIME ActiveRuntime;

    if (ElementPropertyKey == NULL || Value == NULL) return FALSE;

    ActiveRuntime = DesktopThemeGetActiveRuntime(Desktop);
    if (ActiveRuntime == NULL) return FALSE;

    return ThemeFindRuntimeEntry(ActiveRuntime->ElementProperties, ActiveRuntime->ElementPropertyCount, ElementPropertyKey, Value);
}

/***************************************************************************/
