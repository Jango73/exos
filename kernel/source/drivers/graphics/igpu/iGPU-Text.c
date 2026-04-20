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


    Intel graphics (text operations and cursor runtime)

\************************************************************************/

#include "drivers/graphics/common/Graphics-TextRenderer.h"
#include "iGPU-Internal.h"
#include "log/Log.h"
#include "sync/DeferredWork.h"
#include "system/Clock.h"
#include "utils/Cooldown.h"

/************************************************************************/

typedef struct tag_INTEL_GFX_CURSOR_RUNTIME {
    COOLDOWN ShowCooldown;
    DEFERRED_WORK_TOKEN DeferredToken;
    BOOL PendingShow;
} INTEL_GFX_CURSOR_RUNTIME, *LPINTEL_GFX_CURSOR_RUNTIME;

/************************************************************************/

static INTEL_GFX_CURSOR_RUNTIME IntelGfxCursorRuntime = {
    .DeferredToken = {.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT}};

/************************************************************************/

static void IntelGfxCursorDeferredPoll(LPVOID Context) {
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;
    U32 Now = 0;

    UNUSED(Context);

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0 || IntelGfxState.HasActiveMode == FALSE) {
        return;
    }

    if (IntelGfxCursorRuntime.PendingShow == FALSE) {
        return;
    }

    if (IntelGfxCursorRuntime.ShowCooldown.Initialized == FALSE) {
        return;
    }

    Now = GetSystemTime();
    if (!CooldownReady(&IntelGfxCursorRuntime.ShowCooldown, Now)) {
        return;
    }

    CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = TRUE};

    LockMutex(&(IntelGfxState.Context.Mutex), INFINITY);
    if (GfxTextSetCursorVisible(&IntelGfxState.Context, &CursorVisibleInfo)) {
        IntelGfxCursorRuntime.PendingShow = FALSE;
    }
    UnlockMutex(&(IntelGfxState.Context.Mutex));
}

/************************************************************************/

static void IntelGfxEnsureCursorDeferredRegistration(void) {
    if (DeferredWorkTokenIsValid(IntelGfxCursorRuntime.DeferredToken) != FALSE) {
        return;
    }

    IntelGfxCursorRuntime.DeferredToken =
        DeferredWorkRegisterPollOnly(IntelGfxCursorDeferredPoll, NULL, TEXT("IntelGfxCursor"));

    if (DeferredWorkTokenIsValid(IntelGfxCursorRuntime.DeferredToken) == FALSE) {
        WARNING(TEXT("DeferredWork registration failed"));
    }
}

/************************************************************************/

static void IntelGfxArmCursorShowCooldown(void) {
    U32 Now = 0;

    if (IntelGfxCursorRuntime.ShowCooldown.Initialized == FALSE) {
        if (!CooldownInit(&IntelGfxCursorRuntime.ShowCooldown, INTEL_GFX_CURSOR_SHOW_DELAY_MS)) {
            return;
        }
    } else {
        CooldownSetInterval(&IntelGfxCursorRuntime.ShowCooldown, INTEL_GFX_CURSOR_SHOW_DELAY_MS);
    }

    Now = GetSystemTime();
    IntelGfxCursorRuntime.ShowCooldown.NextAllowedTick = Now + INTEL_GFX_CURSOR_SHOW_DELAY_MS;
    IntelGfxCursorRuntime.PendingShow = TRUE;

    if (DeferredWorkTokenIsValid(IntelGfxCursorRuntime.DeferredToken) != FALSE) {
        DeferredWorkSignal(IntelGfxCursorRuntime.DeferredToken);
    }
}

/************************************************************************/

static void IntelGfxHandleTextWriteActivity(LPGRAPHICSCONTEXT Context) {
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;

    if (Context == NULL) {
        return;
    }

    IntelGfxEnsureCursorDeferredRegistration();
    CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = FALSE};
    (void)GfxTextSetCursorVisible(Context, &CursorVisibleInfo);
    IntelGfxArmCursorShowCooldown();
}

/************************************************************************/

void IntelGfxTextShutdownRuntime(void) {
    if (DeferredWorkTokenIsValid(IntelGfxCursorRuntime.DeferredToken) != FALSE) {
        DeferredWorkUnregister(IntelGfxCursorRuntime.DeferredToken);
    }

    IntelGfxCursorRuntime = (INTEL_GFX_CURSOR_RUNTIME){
        .DeferredToken = {.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT}};
}

/************************************************************************/

UINT IntelGfxTextPutCell(LPGFX_TEXT_CELL_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = GfxTextPutCell(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = GfxTextClearRegion(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    UINT Result = DF_RETURN_GENERIC;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = IntelGfxScrollRegionViaShadow(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return (Result == DF_RETURN_SUCCESS) ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;
    I32 PixelX = 0;
    I32 PixelY = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextSetCursor(Context, Info);
    if (Result) {
        U32 CursorHeight = (Info->CellHeight >= 4) ? 2 : 1;
        PixelX = (I32)(Info->CellX * Info->CellWidth);
        PixelY = (I32)(Info->CellY * Info->CellHeight) + (I32)Info->CellHeight - (I32)CursorHeight;
        (void)IntelGfxFlushContextRegionToScanout(Context, PixelX, PixelY, Info->CellWidth, CursorHeight);
    }
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxEnsureCursorDeferredRegistration();
    if (DeferredWorkTokenIsValid(IntelGfxCursorRuntime.DeferredToken) == FALSE) {
        Result = GfxTextSetCursorVisible(Context, Info);
        UnlockMutex(&(Context->Mutex));
        return Result ? 1 : 0;
    }

    if (Info->IsVisible == FALSE) {
        CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = FALSE};
        (void)GfxTextSetCursorVisible(Context, &CursorVisibleInfo);
        IntelGfxArmCursorShowCooldown();
        UnlockMutex(&(Context->Mutex));
        return 1;
    }

    IntelGfxArmCursorShowCooldown();
    UnlockMutex(&(Context->Mutex));
    return 1;
}

/************************************************************************/

UINT IntelGfxTextDraw(LPGFX_TEXT_DRAW_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_MEASURE_INFO MeasureInfo;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    MeasureInfo = (GFX_TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Info->Text,
        .Font = Info->Font,
        .Width = 0,
        .Height = 0};

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = GfxTextDrawString(Context, Info);
    if (Result && GfxTextMeasure(&MeasureInfo) && MeasureInfo.Width != 0 && MeasureInfo.Height != 0) {
        (void)IntelGfxFlushContextRegionToScanout(Context, Info->X, Info->Y, MeasureInfo.Width, MeasureInfo.Height);
    }
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextMeasure(LPGFX_TEXT_MEASURE_INFO Info) {
    if (Info == NULL) {
        return 0;
    }

    return GfxTextMeasure(Info) ? 1 : 0;
}

/************************************************************************/
