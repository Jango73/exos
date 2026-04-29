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


    Shell commands - graphics

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "DisplaySession.h"
#include "Desktop.h"
#include "process/Process.h"
#include "system/System.h"

/***************************************************************************/

/**
 * @brief Retrieve or create the desktop handle associated with the shell process.
 * @return Desktop handle or 0 on failure.
 */
static HANDLE GetOrCreateShellDesktopHandle(void) {
    HANDLE Desktop = 0;

    Desktop = DoSystemCall(SYSCALL_GetCurrentDesktop, SYSCALL_PARAM(0));
    if (Desktop != 0) {
        return Desktop;
    }

    return DoSystemCall(SYSCALL_CreateDesktop, SYSCALL_PARAM(0));
}

/************************************************************************/

/**
 * @brief Apply one desktop theme target through the desktop syscall boundary.
 * @param Target Path or alias to apply.
 * @return TRUE on success.
 */
static BOOL ShellApplyDesktopTheme(LPCSTR Target) {
    DESKTOP_THEME_INFO ApplyInfo;

    if (Target == NULL || StringLength(Target) == 0) {
        return FALSE;
    }

    ApplyInfo.Header.Size = sizeof(ApplyInfo);
    ApplyInfo.Header.Version = EXOS_ABI_VERSION;
    ApplyInfo.Header.Flags = 0;
    ApplyInfo.Target = Target;

    return DoSystemCall(SYSCALL_ApplyDesktopTheme, SYSCALL_PARAM(&ApplyInfo)) != FALSE;
}

/**
 * @brief Convert display front-end identifier to text.
 * @param FrontEnd Display front-end value.
 * @return Constant text description.
 */
static LPCSTR DesktopFrontEndToText(U32 FrontEnd) {
    switch (FrontEnd) {
        case DISPLAY_FRONTEND_CONSOLE:
            return TEXT("console");
        case DISPLAY_FRONTEND_DESKTOP:
            return TEXT("desktop");
        default:
            return TEXT("none");
    }
}

/************************************************************************/

/**
 * @brief Launch the portal application that owns desktop userland components.
 * @return TRUE when the process was created.
 */
static BOOL ShellLaunchPortal(void) {
    PROCESS_INFO ProcessInfo;

    MemorySet(&ProcessInfo, 0, sizeof(PROCESS_INFO));
    ProcessInfo.Header.Size = sizeof(PROCESS_INFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    StringCopy(ProcessInfo.CommandLine, TEXT("/system/apps/portal"));
    StringCopy(ProcessInfo.WorkFolder, TEXT("/system"));

    return CreateProcess(&ProcessInfo);
}

/************************************************************************/

/**
 * @brief Convert desktop mode identifier to text.
 * @param Mode Desktop mode value.
 * @return Constant text description.
 */
static LPCSTR DesktopModeToText(U32 Mode) {
    switch (Mode) {
        case DESKTOP_MODE_GRAPHICS:
            return TEXT("graphics");
        case DESKTOP_MODE_CONSOLE:
            return TEXT("console");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Convert theme fallback reason identifier to text.
 * @param Reason Theme fallback reason.
 * @return Constant text description.
 */
static LPCSTR ThemeFallbackReasonToText(U32 Reason) {
    switch (Reason) {
        case DESKTOP_THEME_FALLBACK_REASON_NONE:
            return TEXT("none");
        case DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED:
            return TEXT("file_read_failed");
        case DESKTOP_THEME_FALLBACK_REASON_PARSE_FAILED:
            return TEXT("parse_failed");
        case DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED:
            return TEXT("activation_failed");
        case DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME:
            return TEXT("no_staged_theme");
        case DESKTOP_THEME_FALLBACK_REASON_RESET_TO_DEFAULT:
            return TEXT("reset_to_default");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Convert desktop cursor path identifier to text.
 * @param Path Cursor path value.
 * @return Constant text description.
 */
static LPCSTR CursorPathToText(U32 Path) {
    switch (Path) {
        case DESKTOP_CURSOR_PATH_HARDWARE:
            return TEXT("hardware");
        case DESKTOP_CURSOR_PATH_SOFTWARE:
            return TEXT("software");
        default:
            return TEXT("unset");
    }
}

/************************************************************************/

/**
 * @brief Convert desktop cursor fallback reason to text.
 * @param Reason Cursor fallback reason.
 * @return Constant text description.
 */
static LPCSTR CursorFallbackReasonToText(U32 Reason) {
    switch (Reason) {
        case DESKTOP_CURSOR_FALLBACK_NONE:
            return TEXT("none");
        case DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS:
            return TEXT("not_graphics");
        case DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES:
            return TEXT("no_capabilities");
        case DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE:
            return TEXT("no_cursor_plane");
        case DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED:
            return TEXT("set_shape_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED:
            return TEXT("set_position_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED:
            return TEXT("set_visible_failed");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Print desktop/theme runtime status.
 */
static void PrintDesktopStatus(void) {
    U32 FrontEnd;
    LPDESKTOP ActiveDesktop;
    DESKTOP_THEME_RUNTIME_INFO ThemeInfo;

    FrontEnd = DisplaySessionGetActiveFrontEnd();
    ActiveDesktop = GetActiveDesktop();

    ConsolePrint(TEXT("desktop: front_end=%s\n"), DesktopFrontEndToText(FrontEnd));

    if (ActiveDesktop != NULL) {
        ConsolePrint(TEXT("desktop: mode=%s\n"), DesktopModeToText(ActiveDesktop->Mode));
        ConsolePrint(TEXT("desktop: cursor_path=%s visible=%u pos=(%x,%x)\n"),
            CursorPathToText(ActiveDesktop->Cursor.RenderPath),
            ActiveDesktop->Cursor.Visible ? 1 : 0,
            UNSIGNED(ActiveDesktop->Cursor.X),
            UNSIGNED(ActiveDesktop->Cursor.Y));
        ConsolePrint(TEXT("desktop: cursor_size=%ux%u\n"),
            ActiveDesktop->Cursor.Width,
            ActiveDesktop->Cursor.Height);
        ConsolePrint(TEXT("desktop: cursor_fallback=%s\n"),
            CursorFallbackReasonToText(ActiveDesktop->Cursor.FallbackReason));
    } else {
        ConsolePrint(TEXT("desktop: mode=unknown\n"));
    }

    if (GetActiveThemeInfo(&ThemeInfo) == FALSE) {
        ConsolePrint(TEXT("desktop: theme=unavailable\n"));
        return;
    }

    ConsolePrint(TEXT("desktop: theme_source=%s\n"), ThemeInfo.IsLoadedActive ? TEXT("loaded") : TEXT("built-in"));
    if (ThemeInfo.ActiveThemePath[0] != STR_NULL) {
        ConsolePrint(TEXT("desktop: active_theme=%s\n"), ThemeInfo.ActiveThemePath);
    }
    if (ThemeInfo.HasStagedTheme != FALSE && ThemeInfo.StagedThemePath[0] != STR_NULL) {
        ConsolePrint(TEXT("desktop: staged_theme=%s\n"), ThemeInfo.StagedThemePath);
    }
    ConsolePrint(TEXT("desktop: theme_last_status=%x fallback_reason=%s\n"),
        ThemeInfo.LastStatus,
        ThemeFallbackReasonToText(ThemeInfo.LastFallbackReason));
}

/************************************************************************/

/**
 * @brief Show main desktop from shell and optionally apply config-selected theme.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 ShowMainDesktopFromShell(void) {
    HANDLE Desktop;
    LPCSTR ConfiguredThemePath;

    Desktop = GetOrCreateShellDesktopHandle();
    if (Desktop == 0) {
        ConsolePrint(TEXT("desktop show: desktop creation failed\n"));
        return DF_RETURN_SUCCESS;
    }

    if (DoSystemCall(SYSCALL_ShowDesktop, SYSCALL_PARAM(Desktop)) == FALSE) {
        ConsolePrint(TEXT("desktop show: unable to switch to desktop\n"));
        return DF_RETURN_SUCCESS;
    }

    if (ShellLaunchPortal() == FALSE) {
        ConsolePrint(TEXT("desktop show: portal launch failed\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("desktop show: portal launched\n"));

    // A configured theme path is optional and must never block desktop activation.
    ConfiguredThemePath = GetConfigurationValue(TEXT("Desktop.ThemePath"));
    if (ConfiguredThemePath != NULL && StringLength(ConfiguredThemePath) != 0) {
        if (ShellApplyDesktopTheme(ConfiguredThemePath) != FALSE) {
            ConsolePrint(TEXT("desktop show: theme activated from config (%s)\n"), ConfiguredThemePath);
        } else {
            ConsolePrint(TEXT("desktop show: theme config failed, keeping current theme (%s)\n"), ConfiguredThemePath);
        }
    }

    return DF_RETURN_SUCCESS;
}

/**
 * @brief Desktop command dispatcher.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_desktop(LPSHELLCONTEXT Context) {
    STR Action[64];

    ParseNextCommandLineComponent(Context);
    StringCopy(Action, Context->Command);

    if (StringLength(Action) == 0 || StringCompareNC(Action, TEXT("status")) == 0) {
        PrintDesktopStatus();
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Action, TEXT("show")) == 0) {
        return ShowMainDesktopFromShell();
    }

    if (StringCompareNC(Action, TEXT("theme")) == 0) {
        ParseNextCommandLineComponent(Context);
        if (StringLength(Context->Command) == 0) {
            ConsolePrint(TEXT("Usage: desktop theme <path-or-name>\n"));
            return DF_RETURN_SUCCESS;
        }

        if (ShellApplyDesktopTheme(Context->Command) != FALSE) {
            ConsolePrint(TEXT("desktop theme: applied %s\n"), Context->Command);
        } else {
            ConsolePrint(TEXT("desktop theme: failed (%s)\n"), Context->Command);
        }
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Usage: desktop show\n"));
    ConsolePrint(TEXT("       desktop status\n"));
    ConsolePrint(TEXT("       desktop theme <path-or-name>\n"));
    return DF_RETURN_SUCCESS;
}
