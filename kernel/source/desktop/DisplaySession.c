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


    Display session state

\************************************************************************/

#include "DisplaySession.h"

#include "console/Console.h"
#include "console/Console-VGATextFallback.h"
#include "desktop/Desktop-ModeSelector.h"
#include "core/DriverGetters.h"
#include "GFX.h"
#include "core/KernelData.h"
#include "sync/Mutex.h"
#include "Desktop.h"

/************************************************************************/

static BOOL DisplaySessionQueryGraphicsMode(LPDRIVER Driver, LPGRAPHICS_MODE_INFO ModeInfo);
static LPDRIVER DisplaySessionResolveConsoleGraphicsDriver(void);
/**
 * @brief Query active mode information from a graphics backend.
 * @param Driver Graphics driver to query.
 * @param ModeInfo Output mode information.
 * @return TRUE when mode information is valid.
 */
static BOOL DisplaySessionQueryGraphicsMode(LPDRIVER Driver, LPGRAPHICS_MODE_INFO ModeInfo) {
    UINT Result;

    if (Driver == NULL || Driver->Command == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    ModeInfo->Header.Size = sizeof(GRAPHICS_MODE_INFO);
    ModeInfo->Header.Version = EXOS_ABI_VERSION;
    ModeInfo->Header.Flags = 0;
    ModeInfo->ModeIndex = INFINITY;
    ModeInfo->Width = 0;
    ModeInfo->Height = 0;
    ModeInfo->BitsPerPixel = 0;

    Result = Driver->Command(DF_GFX_GETMODEINFO, (UINT)(LPVOID)ModeInfo);
    if (Result != DF_RETURN_SUCCESS || ModeInfo->Width == 0 || ModeInfo->Height == 0) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve the concrete graphics backend to reuse for console text mode.
 * @return Graphics backend driver, or NULL when no reusable backend is active.
 */
static LPDRIVER DisplaySessionResolveConsoleGraphicsDriver(void) {
    LPDRIVER ActiveBackendDriver = GraphicsSelectorGetActiveBackendDriver();
    LPDRIVER SessionGraphicsDriver = DisplaySessionGetActiveGraphicsDriver();

    if (ActiveBackendDriver != NULL && ActiveBackendDriver != ConsoleGetDriver()) {
        return ActiveBackendDriver;
    }

    if (SessionGraphicsDriver != NULL &&
        SessionGraphicsDriver != ConsoleGetDriver() &&
        SessionGraphicsDriver != GetGraphicsDriver()) {
        return SessionGraphicsDriver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initialize display session state once.
 */
void DisplaySessionInitialize(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized != FALSE) {
            return;
        }

        MemorySet(Session, 0, sizeof(DISPLAY_SESSION));
        Session->GraphicsDriver = ConsoleGetDriver();
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->IsInitialized = TRUE;
    }
}

/************************************************************************/

/**
 * @brief Update display session state for console ownership.
 * @param ModeInfo Active console mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetConsoleMode(LPGRAPHICS_MODE_INFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (ModeInfo == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = ConsoleGetDriver();
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->HasValidMode = TRUE;
        SetFocusedProcess(&KernelProcess);
        SetActiveDesktop(NULL);
        ConsoleRefreshDisplay();
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Update console front-end state to render text through a graphics backend.
 * @param GraphicsDriver Active graphics backend.
 * @param ModeInfo Active graphics mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetConsoleGraphicsMode(LPDRIVER GraphicsDriver, LPGRAPHICS_MODE_INFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (GraphicsDriver == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    if (ConsoleSetGraphicsTextMode(ModeInfo) == FALSE) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = GraphicsDriver;
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_CONSOLE;
        Session->HasValidMode = TRUE;
        SetFocusedProcess(&KernelProcess);
        SetActiveDesktop(NULL);
        ConsoleRefreshDisplay();
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Update display session state for desktop ownership.
 * @param Desktop Active desktop.
 * @param GraphicsDriver Selected graphics backend.
 * @param ModeInfo Active graphics mode.
 * @return TRUE on success.
 */
BOOL DisplaySessionSetDesktopMode(LPDESKTOP Desktop, LPDRIVER GraphicsDriver, LPGRAPHICS_MODE_INFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    if (Desktop == NULL || GraphicsDriver == NULL || ModeInfo == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            DisplaySessionInitialize();
        }

        Session->GraphicsDriver = GraphicsDriver;
        Session->ActiveMode = *ModeInfo;
        Session->ActiveFrontEnd = DISPLAY_FRONTEND_DESKTOP;
        Session->HasValidMode = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Force one graphics backend alias and apply one graphics mode to the active display session.
 * @param DriverAlias Requested graphics backend alias.
 * @param RequestedMode Requested graphics mode.
 * @param AppliedMode Optional output receiving the effective applied mode.
 * @return DF_RETURN_SUCCESS on success, or one driver-style error code.
 */
UINT DisplaySessionApplyGraphicsDriverByAlias(
    LPCSTR DriverAlias,
    LPGRAPHICS_MODE_INFO RequestedMode,
    LPGRAPHICS_MODE_INFO AppliedMode) {
    LPDRIVER GraphicsDriver = NULL;
    LPDESKTOP ActiveDesktop = NULL;
    GRAPHICS_MODE_INFO EffectiveMode;
    UINT ModeSetResult = DF_RETURN_GENERIC;

    if (DriverAlias == NULL || StringLength(DriverAlias) == 0 || RequestedMode == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (DesktopIsValidGraphicsModeInfo(RequestedMode) == FALSE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (GraphicsSelectorForceBackendByName(DriverAlias) == FALSE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    GraphicsDriver = GraphicsSelectorGetActiveBackendDriver();
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    EffectiveMode = *RequestedMode;
    ModeSetResult = GraphicsDriver->Command(DF_GFX_SETMODE, (UINT)(LPVOID)&EffectiveMode);
    if (ModeSetResult != DF_RETURN_SUCCESS) {
        return ModeSetResult;
    }

    if (DisplaySessionQueryGraphicsMode(GraphicsDriver, &EffectiveMode) == FALSE) {
        EffectiveMode = *RequestedMode;
    }

    if (DisplaySessionSetConsoleGraphicsMode(GraphicsDriver, &EffectiveMode) == FALSE) {
        ActiveDesktop = GetActiveDesktop();
        if (ActiveDesktop == NULL ||
            DisplaySessionSetDesktopMode(ActiveDesktop, GraphicsDriver, &EffectiveMode) == FALSE) {
            return DF_RETURN_UNEXPECTED;
        }
    }

    if (AppliedMode != NULL) {
        *AppliedMode = EffectiveMode;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Switch display ownership to console front-end.
 * @return TRUE on success.
 */
BOOL DisplaySwitchToConsole(void) {
    GRAPHICS_MODE_INFO ModeInfo;
    GRAPHICS_MODE_INFO GraphicsModeInfo;
    UINT Result;
    LPDRIVER GraphicsDriver;

    ModeInfo.Header.Size = sizeof(ModeInfo);
    ModeInfo.Header.Version = EXOS_ABI_VERSION;
    ModeInfo.Header.Flags = 0;
    ModeInfo.ModeIndex = INFINITY;
    ModeInfo.Width = (Console.Width != 0) ? Console.Width : 80;
    ModeInfo.Height = (Console.Height != 0) ? Console.Height : 25;
    ModeInfo.BitsPerPixel = 0;

    GraphicsDriver = DisplaySessionResolveConsoleGraphicsDriver();
    if (GraphicsDriver != NULL) {
        if (DisplaySessionQueryGraphicsMode(GraphicsDriver, &GraphicsModeInfo) != FALSE) {
            if (DisplaySessionSetConsoleGraphicsMode(GraphicsDriver, &GraphicsModeInfo) != FALSE) {
                return TRUE;
            }
        }
    }

    Result = ConsoleSetMode(&ModeInfo);
    if (Result == DF_RETURN_SUCCESS) {
        return DisplaySessionSetConsoleMode(&ModeInfo);
    }

    if (GraphicsDriver != NULL && DisplaySessionQueryGraphicsMode(GraphicsDriver, &GraphicsModeInfo) != FALSE) {
        if (DisplaySessionSetConsoleGraphicsMode(GraphicsDriver, &GraphicsModeInfo) == FALSE) {
            goto FallbackToVgaText;
        }
        return TRUE;
    }

FallbackToVgaText:
    if (ConsoleVGATextFallbackActivate(80, 25, &ModeInfo) != FALSE) {
        return DisplaySessionSetConsoleMode(&ModeInfo);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Switch display ownership to desktop front-end.
 * @param Desktop Desktop to activate.
 * @return TRUE on success.
 */
BOOL DisplaySwitchToDesktop(LPDESKTOP Desktop) {
    if (Desktop == NULL) {
        return FALSE;
    }

    return KernelShowDesktop(Desktop);
}

/**
 * @brief Retrieve active display front-end.
 * @return One of DISPLAY_FRONTEND_*.
 */
U32 DisplaySessionGetActiveFrontEnd(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            return DISPLAY_FRONTEND_NONE;
        }

        return Session->ActiveFrontEnd;
    }

    return DISPLAY_FRONTEND_NONE;
}

/************************************************************************/

/**
 * @brief Retrieve active graphics driver tracked by session.
 * @return Active driver pointer or NULL.
 */
LPDRIVER DisplaySessionGetActiveGraphicsDriver(void) {
    LPDISPLAY_SESSION Session = GetDisplaySession();

    SAFE_USE(Session) {
        if (Session->IsInitialized == FALSE) {
            return NULL;
        }

        return Session->GraphicsDriver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Retrieve the active graphics mode tracked by the session.
 * @param ModeInfo Output mode information.
 * @return TRUE when a valid active mode is available.
 */
BOOL DisplaySessionGetActiveMode(LPGRAPHICS_MODE_INFO ModeInfo) {
    LPDISPLAY_SESSION Session = GetDisplaySession();
    LPDRIVER GraphicsDriver = NULL;

    if (ModeInfo == NULL) {
        return FALSE;
    }

    SAFE_USE(Session) {
        if (Session->IsInitialized != FALSE && Session->HasValidMode != FALSE) {
            *ModeInfo = Session->ActiveMode;
            return TRUE;
        }
    }

    GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
    if (GraphicsDriver == NULL) {
        GraphicsDriver = GraphicsSelectorGetActiveBackendDriver();
    }

    if (GraphicsDriver != NULL && DisplaySessionQueryGraphicsMode(GraphicsDriver, ModeInfo) != FALSE) {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/
