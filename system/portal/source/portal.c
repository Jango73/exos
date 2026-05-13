/************************************************************************\

    EXOS Portal
    Copyright (c) 1999-2026 Jango73

    Desktop portal

\************************************************************************/

#include "exos.h"
#include "exos-runtime-main.h"
#include "ui/Startup-Desktop-Components.h"
#include "ui/WindowDockHost.h"

/************************************************************************/

#define PORTAL_ROOT_WINDOW_CLASS_NAME TEXT("PortalRootWindowClass")

/************************************************************************/

static U32 PortalRootWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_DRAW:
            return BaseWindowFunc(Window, EWM_CLEAR, THEME_TOKEN_WINDOW_BACKGROUND_DESKTOP, Param2);
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/

static HANDLE PortalCreateRootWindow(void) {
    WINDOW_INFO WindowInfo;

    if (WindowDockHostClassEnsureDerivedRegistered(PORTAL_ROOT_WINDOW_CLASS_NAME, PortalRootWindowFunc) == FALSE) {
        debug("[PortalCreateRootWindow] root class registration failed");
        return NULL;
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = NULL;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = PORTAL_ROOT_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_BARE_SURFACE;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    return CreateWindow(&WindowInfo);
}

/************************************************************************/

/**
 * @brief Retrieve or create the desktop associated with this process.
 * @return Desktop handle or NULL on failure.
 */
static HANDLE PortalGetOrCreateDesktop(void) {
    HANDLE Desktop;
    HANDLE RootWindow;

    Desktop = GetCurrentDesktop();
    if (Desktop != NULL) return Desktop;

    RootWindow = PortalCreateRootWindow();
    if (RootWindow == NULL) {
        debug("[PortalGetOrCreateDesktop] root window creation failed");
        return NULL;
    }

    return CreateDesktop(RootWindow);
}

/************************************************************************/

/**
 * @brief Initialize and show the desktop owned by portal.
 * @return TRUE on success.
 */
static BOOL PortalShowDesktop(void) {
    HANDLE Desktop;

    Desktop = PortalGetOrCreateDesktop();
    if (Desktop == NULL) {
        debug("[PortalShowDesktop] desktop creation failed");
        return FALSE;
    }

    if (ShowDesktop(Desktop) == FALSE) {
        debug("[PortalShowDesktop] unable to switch to desktop");
        return FALSE;
    }

    if (StartupDesktopComponentsInitialize(Desktop) == FALSE) {
        debug("[PortalShowDesktop] startup desktop components initialization failed");
        return FALSE;
    }

    debug("[PortalShowDesktop] desktop ready");

    return TRUE;
}

/************************************************************************/
/**
 * @brief Check whether portal should enable the diagnostic mouse serpentine mode.
 * @param ArgCount Command-line argument count.
 * @param Arguments Command-line argument array.
 * @return TRUE when the matching flag is present.
 */
static BOOL PortalShouldEnableMouseSerpentine(int ArgCount, char** Arguments) {
    int ArgIndex;

    if (Arguments == NULL) {
        return FALSE;
    }

    for (ArgIndex = 1; ArgIndex < ArgCount; ArgIndex++) {
        if (strcmp(Arguments[ArgIndex], "--mouse-serpentine") == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

int main(int argc, char** argv) {
    MESSAGE Message;
    BOOL EnableMouseSerpentine;

    EnableMouseSerpentine = PortalShouldEnableMouseSerpentine(argc, argv);

    if (PortalShowDesktop() == FALSE) {
        return (int)MAX_U32;
    }

    if (EnableMouseSerpentine != FALSE) {
        if (SetMouseSerpentineMode(TRUE) == FALSE) {
            debug("[main] unable to enable mouse serpentine mode");
        } else {
            debug("[main] mouse serpentine mode enabled");
        }
    }

    while (GetMessage(NULL, &Message, 0, 0)) {
        DispatchMessage(&Message);
    }

    return 0;
}

/************************************************************************/
