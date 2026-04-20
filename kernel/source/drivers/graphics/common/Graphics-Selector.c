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


    Graphics selector

\************************************************************************/

#include "GFX.h"

#include "text/CoreString.h"
#include "core/DriverGetters.h"
#include "log/Log.h"
#include "log/Profile.h"
#include "system/Clock.h"
#include "utils/BootPath.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define GRAPHICS_SELECTOR_VER_MAJOR 1
#define GRAPHICS_SELECTOR_VER_MINOR 0

/************************************************************************/

typedef struct tag_GRAPHICS_SELECTOR_STATE {
    LPDRIVER Backends[4];
    UINT Scores[4];
    UINT Priorities[4];
    UINT BackendCount;
    UINT ActiveIndex;
    BOOL SelectionFrozen;
} GRAPHICS_SELECTOR_STATE, *LPGRAPHICS_SELECTOR_STATE;

/************************************************************************/

static UINT GraphicsSelectorCommands(UINT Function, UINT Parameter);
static UINT GraphicsSelectorBackendPriority(LPDRIVER Driver);
static UINT GraphicsSelectorScoreDriver(LPDRIVER Driver);
static UINT GraphicsSelectorForwardFrozen(UINT Function, UINT Parameter);
static UINT GraphicsSelectorLoad(void);
static UINT GraphicsSelectorUnload(void);

static DRIVER DATA_SECTION GraphicsSelectorDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = GRAPHICS_SELECTOR_VER_MAJOR,
    .VersionMinor = GRAPHICS_SELECTOR_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "Graphics selector",
    .Alias = "graphics_selector",
    .Flags = 0,
    .Command = GraphicsSelectorCommands
};

static GRAPHICS_SELECTOR_STATE DATA_SECTION GraphicsSelectorState = {0};

/************************************************************************/

/**
 * @brief Check whether one graphics command uses boolean return semantics.
 * @param Function Driver command identifier.
 * @return TRUE for text helper commands returning 0/1.
 */
static BOOL GraphicsSelectorIsBooleanTextCommand(UINT Function) {
    switch (Function) {
        case DF_GFX_TEXT_PUTCELL:
        case DF_GFX_TEXT_CLEAR_REGION:
        case DF_GFX_TEXT_SCROLL_REGION:
        case DF_GFX_TEXT_SET_CURSOR:
        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
        case DF_GFX_TEXT_DRAW:
        case DF_GFX_TEXT_MEASURE:
            return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether one graphics command is related to cursor management.
 * @param Function Driver command identifier.
 * @return TRUE for cursor shape/position/visibility commands.
 */
static BOOL GraphicsSelectorIsCursorCommand(UINT Function) {
    switch (Function) {
        case DF_GFX_CURSOR_SET_SHAPE:
        case DF_GFX_CURSOR_SET_POSITION:
        case DF_GFX_CURSOR_SET_VISIBLE:
            return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Retrieve graphics selector driver descriptor.
 * @return Pointer to graphics selector driver.
 */
LPDRIVER GraphicsSelectorGetDriver(void) {
    return &GraphicsSelectorDriver;
}

/************************************************************************/

/**
 * @brief Match one backend against a shell-facing backend selector name.
 * @param Driver Candidate backend.
 * @param Name Requested backend name.
 * @return TRUE if this backend matches the requested name.
 */
static BOOL GraphicsSelectorBackendMatchesName(LPDRIVER Driver, LPCSTR Name) {
    if (Driver == NULL || Name == NULL || StringLength(Name) == 0) {
        return FALSE;
    }

    if (StringLength(Driver->Alias) == 0) {
        return FALSE;
    }

    return StringCompareNC(Name, Driver->Alias) == 0;
}

/************************************************************************/

/**
 * @brief Find a graphics backend candidate by alias.
 * @param Name Requested backend alias.
 * @return Candidate backend driver or NULL when missing.
 */
static LPDRIVER GraphicsSelectorFindCandidateByName(LPCSTR Name) {
    LPDRIVER Candidates[3];
    UINT Index = 0;

    if (Name == NULL || StringLength(Name) == 0) {
        return NULL;
    }

    Candidates[0] = IntelGfxGetDriver();
    Candidates[1] = GOPGetDriver();
    Candidates[2] = VESAGetDriver();

    for (Index = 0; Index < sizeof(Candidates) / sizeof(Candidates[0]); Index++) {
        LPDRIVER Driver = Candidates[Index];
        if (!GraphicsSelectorBackendMatchesName(Driver, Name)) {
            continue;
        }

        return Driver;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Force active backend by name among loaded selector candidates.
 * @param Name Backend selector name matching one graphics driver alias.
 * @return TRUE when backend exists and becomes active.
 */
BOOL GraphicsSelectorForceBackendByName(LPCSTR Name) {
    LPDRIVER Driver = NULL;
    UINT LoadResult = 0;
    UINT Score = 0;
    UINT Priority = 0;

    if (Name == NULL || StringLength(Name) == 0) {
        return FALSE;
    }

    Driver = GraphicsSelectorFindCandidateByName(Name);
    if (Driver == NULL || Driver->Command == NULL) {
        return FALSE;
    }

    if ((GraphicsSelectorDriver.Flags & DRIVER_FLAG_READY) != 0) {
        (void)GraphicsSelectorUnload();
    }

    GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};

    LoadResult = Driver->Command(DF_LOAD, 0);
    if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
        WARNING(TEXT("Rejecting forced backend %s (load_result=%u, ready=0)"),
            Driver->Product,
            LoadResult);
        return FALSE;
    }

    Score = GraphicsSelectorScoreDriver(Driver);
    Priority = GraphicsSelectorBackendPriority(Driver);

    GraphicsSelectorState.Backends[0] = Driver;
    GraphicsSelectorState.Scores[0] = Score;
    GraphicsSelectorState.Priorities[0] = Priority;
    GraphicsSelectorState.BackendCount = 1;
    GraphicsSelectorState.ActiveIndex = 0;
    GraphicsSelectorDriver.Flags |= DRIVER_FLAG_READY;

    DEBUG(TEXT("Forced backend %s loaded (score=%u priority=%u)"),
        Driver->Product,
        Score,
        Priority);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Return active backend product name.
 * @return Active backend product name, or empty string if unavailable.
 */
LPCSTR GraphicsSelectorGetActiveBackendName(void) {
    if ((GraphicsSelectorDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return TEXT("");
    }

    if (GraphicsSelectorState.BackendCount == 0) {
        return TEXT("");
    }

    if (GraphicsSelectorState.ActiveIndex >= GraphicsSelectorState.BackendCount) {
        return TEXT("");
    }

    SAFE_USE(GraphicsSelectorState.Backends[GraphicsSelectorState.ActiveIndex]) {
        return GraphicsSelectorState.Backends[GraphicsSelectorState.ActiveIndex]->Product;
    }

    return TEXT("");
}

/************************************************************************/

/**
 * @brief Return active backend driver descriptor.
 * @return Active backend driver, or NULL when unavailable.
 */
LPDRIVER GraphicsSelectorGetActiveBackendDriver(void) {
    if ((GraphicsSelectorDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return NULL;
    }

    if (GraphicsSelectorState.BackendCount == 0) {
        return NULL;
    }

    if (GraphicsSelectorState.ActiveIndex >= GraphicsSelectorState.BackendCount) {
        return NULL;
    }

    SAFE_USE(GraphicsSelectorState.Backends[GraphicsSelectorState.ActiveIndex]) {
        return GraphicsSelectorState.Backends[GraphicsSelectorState.ActiveIndex];
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Score a graphics backend by exposed capabilities.
 * @param Driver Candidate driver.
 * @return Score value. Higher means more capable.
 */
static UINT GraphicsSelectorScoreDriver(LPDRIVER Driver) {
    GFX_CAPABILITIES Capabilities;
    UINT Score = 0;

    if (Driver == NULL || Driver->Command == NULL) {
        return 0;
    }

    if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
        return 0;
    }

    Capabilities = (GFX_CAPABILITIES){
        .Header = {.Size = sizeof(GFX_CAPABILITIES), .Version = EXOS_ABI_VERSION, .Flags = 0}
    };

    if (Driver->Command(DF_GFX_GETCAPABILITIES, (UINT)(LPVOID)&Capabilities) != DF_RETURN_SUCCESS) {
        return 1;
    }

    Score = 10;
    if (Capabilities.HasHardwareModeset) Score += 10;
    if (Capabilities.HasPageFlip) Score += 5;
    if (Capabilities.HasVBlankInterrupt) Score += 3;
    if (Capabilities.HasCursorPlane) Score += 2;
    if (Capabilities.SupportsTiledSurface) Score += 2;
    if (Capabilities.MaxWidth >= 1920 && Capabilities.MaxHeight >= 1080) Score += 1;

    return Score;
}

/************************************************************************/

/**
 * @brief Return deterministic backend priority for tie-breaking.
 * @param Driver Candidate backend.
 * @return Priority value. Higher value wins on equal score.
 */
static UINT GraphicsSelectorBackendPriority(LPDRIVER Driver) {
    if (Driver == IntelGfxGetDriver()) {
        return 300;
    }

    if (Driver == GOPGetDriver()) {
        return 200;
    }

    if (Driver == VESAGetDriver()) {
        return 100;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Load candidate graphics backends and select the most capable active one.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_UNEXPECTED otherwise.
 */
static UINT GraphicsSelectorLoad(void) {
    LPDRIVER Candidates[3];
    UINT Index = 0;
    UINT CandidateCount = sizeof(Candidates) / sizeof(Candidates[0]);

    if ((GraphicsSelectorDriver.Flags & DRIVER_FLAG_READY) != 0) {
        return DF_RETURN_SUCCESS;
    }

    GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};

    Candidates[0] = IntelGfxGetDriver();
    Candidates[1] = GOPGetDriver();
    Candidates[2] = VESAGetDriver();

    for (Index = 0; Index < CandidateCount; Index++) {
        LPDRIVER Driver = Candidates[Index];
        UINT LoadResult = 0;
        UINT Score = 0;
        UINT Priority = 0;
        UINT InsertIndex = 0;

        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }

        if (Driver == VESAGetDriver() && VesaIsSupportedOnCurrentBootPath() == FALSE) {
            continue;
        }

        LoadResult = Driver->Command(DF_LOAD, 0);
        DEBUG(TEXT("Probe backend %s load_result=%u ready=%u"),
            Driver->Product,
            LoadResult,
            (Driver->Flags & DRIVER_FLAG_READY) != 0 ? 1 : 0);

        if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
            WARNING(TEXT("Rejecting backend %s (load_result=%u, ready=0)"),
                Driver->Product,
                LoadResult);
            continue;
        }

        Score = GraphicsSelectorScoreDriver(Driver);
        Priority = GraphicsSelectorBackendPriority(Driver);
        DEBUG(TEXT("Candidate backend %s score=%u priority=%u"),
            Driver->Product, Score, Priority);

        if (GraphicsSelectorState.BackendCount >= sizeof(GraphicsSelectorState.Backends) / sizeof(GraphicsSelectorState.Backends[0])) {
            WARNING(TEXT("Backend table full, skipping %s"), Driver->Product);
            continue;
        }

        InsertIndex = GraphicsSelectorState.BackendCount;
        while (InsertIndex > 0 &&
               (Score > GraphicsSelectorState.Scores[InsertIndex - 1] ||
                (Score == GraphicsSelectorState.Scores[InsertIndex - 1] &&
                 Priority > GraphicsSelectorState.Priorities[InsertIndex - 1]))) {
            GraphicsSelectorState.Backends[InsertIndex] = GraphicsSelectorState.Backends[InsertIndex - 1];
            GraphicsSelectorState.Scores[InsertIndex] = GraphicsSelectorState.Scores[InsertIndex - 1];
            GraphicsSelectorState.Priorities[InsertIndex] = GraphicsSelectorState.Priorities[InsertIndex - 1];
            InsertIndex--;
        }

        GraphicsSelectorState.Backends[InsertIndex] = Driver;
        GraphicsSelectorState.Scores[InsertIndex] = Score;
        GraphicsSelectorState.Priorities[InsertIndex] = Priority;
        GraphicsSelectorState.BackendCount++;
    }

    if (GraphicsSelectorState.BackendCount == 0) {
        WARNING(TEXT("No active graphics backend"));
        GraphicsSelectorDriver.Flags |= DRIVER_FLAG_READY;
        return DF_RETURN_SUCCESS;
    }

    GraphicsSelectorState.ActiveIndex = 0;
    GraphicsSelectorDriver.Flags |= DRIVER_FLAG_READY;

    DEBUG(TEXT("Selected backend: %s (score=%u)"),
        GraphicsSelectorState.Backends[0]->Product, GraphicsSelectorState.Scores[0]);
    for (Index = 1; Index < GraphicsSelectorState.BackendCount; Index++) {
        DEBUG(TEXT("Fallback backend[%u]: %s (score=%u)"),
            Index,
            GraphicsSelectorState.Backends[Index]->Product,
            GraphicsSelectorState.Scores[Index]);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unload graphics selector and all candidate backends.
 * @return DF_RETURN_SUCCESS always.
 */
static UINT GraphicsSelectorUnload(void) {
    LPDRIVER Candidates[3];
    UINT Index = 0;

    Candidates[0] = IntelGfxGetDriver();
    Candidates[1] = GOPGetDriver();
    Candidates[2] = VESAGetDriver();

    for (Index = 0; Index < sizeof(Candidates) / sizeof(Candidates[0]); Index++) {
        LPDRIVER Driver = Candidates[Index];
        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }
        (void)Driver->Command(DF_UNLOAD, 0);
    }

    GraphicsSelectorState = (GRAPHICS_SELECTOR_STATE){0};
    GraphicsSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Forward one command only to the frozen active backend.
 * @param Function Driver command identifier.
 * @param Parameter Driver command parameter.
 * @return Result from the active backend, or DF_RETURN_NOT_IMPLEMENTED when missing.
 */
static UINT GraphicsSelectorForwardFrozen(UINT Function, UINT Parameter) {
    LPDRIVER Driver = NULL;

    if (GraphicsSelectorState.BackendCount == 0) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (GraphicsSelectorState.ActiveIndex >= GraphicsSelectorState.BackendCount) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    Driver = GraphicsSelectorState.Backends[GraphicsSelectorState.ActiveIndex];
    if (Driver == NULL || Driver->Command == NULL) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return Driver->Command(Function, Parameter);
}

/************************************************************************/

/**
 * @brief Forward a command to the selected backend.
 * @param Function Command identifier.
 * @param Parameter Command parameter.
 * @return Result from selected backend.
 */
static UINT GraphicsSelectorForward(UINT Function, UINT Parameter) {
    static RATE_LIMITER CursorForwardLimiter;
    static BOOL CursorForwardLimiterReady = FALSE;
    UINT Index = 0;
    BOOL IsBooleanTextCommand = FALSE;
    BOOL IsCursorCommand = FALSE;
    U32 Suppressed = 0;
    U32 Now = GetSystemTime();

    if (GraphicsSelectorState.BackendCount == 0) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (GraphicsSelectorState.SelectionFrozen != FALSE) {
        return GraphicsSelectorForwardFrozen(Function, Parameter);
    }

    IsBooleanTextCommand = GraphicsSelectorIsBooleanTextCommand(Function);
    IsCursorCommand = GraphicsSelectorIsCursorCommand(Function);

    if (IsCursorCommand != FALSE && CursorForwardLimiterReady == FALSE) {
        if (RateLimiterInit(&CursorForwardLimiter, 24, 1000) != FALSE) {
            CursorForwardLimiterReady = TRUE;
        }
    }

    for (Index = GraphicsSelectorState.ActiveIndex; Index < GraphicsSelectorState.BackendCount; Index++) {
        LPDRIVER Driver = GraphicsSelectorState.Backends[Index];
        UINT Result = 0;
        PROFILE_SCOPE RectangleForwardScope;

        if (Driver == NULL || Driver->Command == NULL) {
            continue;
        }

        if (Function == DF_GFX_RECTANGLE) {
            ProfileStart(&RectangleForwardScope, TEXT("GraphicsSelector.RectangleForward"));
        }
        Result = Driver->Command(Function, Parameter);
        if (Function == DF_GFX_RECTANGLE) {
            ProfileStop(&RectangleForwardScope);
        }

        if (IsCursorCommand != FALSE &&
            CursorForwardLimiterReady != FALSE &&
            RateLimiterShouldTrigger(&CursorForwardLimiter, Now, &Suppressed) != FALSE) {
            DEBUG(TEXT("cursor fn=%u backend=%s index=%u result=%u active=%u suppressed=%u"),
                Function,
                Driver->Product,
                Index,
                Result,
                GraphicsSelectorState.ActiveIndex,
                Suppressed);
        }

        if (IsBooleanTextCommand != FALSE) {
            if (Result != 0) {
                GraphicsSelectorState.ActiveIndex = Index;
                return Result;
            }
            continue;
        }

        if (Function == DF_GFX_GETCONTEXT) {
            if (IS_VALID_KERNEL_POINTER((LPVOID)(UINT)Result)) {
                return Result;
            }
            continue;
        }

        if (Result == DF_RETURN_NOT_IMPLEMENTED || Result == DF_RETURN_UNEXPECTED) {
            continue;
        }

        GraphicsSelectorState.ActiveIndex = Index;
        if (Function == DF_GFX_SETMODE && Result == DF_RETURN_SUCCESS) {
            GraphicsSelectorState.SelectionFrozen = TRUE;
        }
        return Result;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Graphics selector driver entry point.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver result.
 */
static UINT GraphicsSelectorCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return GraphicsSelectorLoad();
        case DF_UNLOAD:
            return GraphicsSelectorUnload();
        case DF_GET_VERSION:
            return MAKE_VERSION(GRAPHICS_SELECTOR_VER_MAJOR, GRAPHICS_SELECTOR_VER_MINOR);
        case DF_DEBUG_INFO:
            return GraphicsSelectorForward(Function, Parameter);
        case DF_GFX_GETMODECOUNT:
        case DF_GFX_GETMODEINFO:
        case DF_GFX_SETMODE:
        case DF_GFX_GETCONTEXT:
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
        case DF_GFX_ELLIPSE:
        case DF_GFX_GETCAPABILITIES:
        case DF_GFX_ENUMOUTPUTS:
        case DF_GFX_GETOUTPUTINFO:
        case DF_GFX_PRESENT:
        case DF_GFX_WAITVBLANK:
        case DF_GFX_ALLOCSURFACE:
        case DF_GFX_FREESURFACE:
        case DF_GFX_SETSCANOUT:
        case DF_GFX_TEXT_PUTCELL:
        case DF_GFX_TEXT_CLEAR_REGION:
        case DF_GFX_TEXT_SCROLL_REGION:
        case DF_GFX_TEXT_SET_CURSOR:
        case DF_GFX_TEXT_SET_CURSOR_VISIBLE:
        case DF_GFX_CURSOR_SET_SHAPE:
        case DF_GFX_CURSOR_SET_POSITION:
        case DF_GFX_CURSOR_SET_VISIBLE:
        case DF_GFX_ARC:
        case DF_GFX_TRIANGLE:
        case DF_GFX_TEXT_DRAW:
        case DF_GFX_TEXT_MEASURE:
            return GraphicsSelectorForward(Function, Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
