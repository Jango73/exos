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


    Mouse dispatcher

\************************************************************************/

#include "input/MouseDispatcher.h"

#include "Arch.h"
#include "system/Clock.h"
#include "console/Console.h"
#include "core/KernelData.h"
#include "log/Log.h"
#include "input/Mouse.h"
#include "Desktop.h"
#include "Desktop-Cursor.h"
#include "process/Process.h"
#include "process/Task.h"
#include "User.h"
#include "drivers/graphics/vga/VGA.h"
#include "utils/Cooldown.h"

/************************************************************************/

#define MOUSE_MOVE_COOLDOWN_MS 10

/************************************************************************/

typedef struct tag_MOUSE_DISPATCH_STATE {
    BOOL Initialized;
    MUTEX Mutex;
    COOLDOWN MoveCooldown;
    I32 PosX;
    I32 PosY;
    I32 ResidualX;
    I32 ResidualY;
    U32 Buttons;
} MOUSE_DISPATCH_STATE, *LPMOUSE_DISPATCH_STATE;

static MOUSE_DISPATCH_STATE g_MouseDispatch = {
    .Initialized = FALSE,
    .Mutex = EMPTY_MUTEX,
    .MoveCooldown = {0, 0, FALSE},
    .PosX = 0,
    .PosY = 0,
    .ResidualX = 0,
    .ResidualY = 0,
    .Buttons = 0};

/************************************************************************/
/**
 * @brief Clamp a mouse position to a rectangle.
 * @param X Pointer to X coordinate.
 * @param Y Pointer to Y coordinate.
 * @param Rect Bounds to clamp against.
 */
static void ClampMousePosition(I32* X, I32* Y, LPRECT Rect) {
    if (X == NULL || Y == NULL || Rect == NULL) return;

    if (*X < Rect->X1) *X = Rect->X1;
    if (*X > Rect->X2) *X = Rect->X2;
    if (*Y < Rect->Y1) *Y = Rect->Y1;
    if (*Y > Rect->Y2) *Y = Rect->Y2;
}

/************************************************************************/
/**
 * @brief Compute a scaled delta with residual accumulation.
 * @param Delta Raw delta.
 * @param Scale Scaling factor (pixels per console cell).
 * @param Residual Residual accumulator (same unit as Delta).
 * @return Scaled delta in console cells.
 */
static I32 ConsumeScaledDelta(I32 Delta, I32 Scale, I32* Residual) {
    I32 Sum;
    I32 Steps;

    if (Residual == NULL) return Delta;
    if (Scale <= 1) {
        *Residual = 0;
        return Delta;
    }

    Sum = *Residual + Delta;
    if (Sum >= 0) {
        Steps = Sum / Scale;
        *Residual = Sum % Scale;
    } else {
        I32 Neg = -Sum;
        Steps = -(Neg / Scale);
        *Residual = -(Neg % Scale);
    }

    return Steps;
}

/************************************************************************/
/**
 * @brief Retrieve console mouse scaling factors.
 * @param ScaleX Output X scale.
 * @param ScaleY Output Y scale.
 */
static void GetConsoleMouseScale(I32* ScaleX, I32* ScaleY) {
    I32 X = 8;
    I32 Y = 16;
    U32 ModeIndex;
    VGAMODEINFO Info;

    if (Console.Width > 0 && Console.Height > 0) {
        if (VGAFindTextMode(Console.Width, Console.Height, &ModeIndex) == TRUE &&
            VGAGetModeInfo(ModeIndex, &Info) == TRUE) {
            if (Info.CharHeight > 0) {
                Y = (I32)Info.CharHeight;
            }
        }
    }

    if (ScaleX != NULL) *ScaleX = X;
    if (ScaleY != NULL) *ScaleY = Y;
}

/************************************************************************/

/**
 * @brief Initialize mouse dispatch state and cooldown.
 *
 * @return TRUE on success, FALSE if the structure could not be initialized.
 */
BOOL InitializeMouseDispatcher(void) {
    if (g_MouseDispatch.Initialized) {
        return TRUE;
    }

    InitMutex(&(g_MouseDispatch.Mutex));

    if (CooldownInit(&(g_MouseDispatch.MoveCooldown), MOUSE_MOVE_COOLDOWN_MS) == FALSE) {
        return FALSE;
    }

    g_MouseDispatch.PosX = 0;
    g_MouseDispatch.PosY = 0;
    g_MouseDispatch.ResidualX = 0;
    g_MouseDispatch.ResidualY = 0;
    g_MouseDispatch.Buttons = 0;
    g_MouseDispatch.Initialized = TRUE;

    {
        RECT Rect;
        LPDESKTOP Desktop = GetActiveDesktop();
        if (GetDesktopScreenRect(Desktop, &Rect) == TRUE) {
            g_MouseDispatch.PosX = Rect.X1 + ((Rect.X2 - Rect.X1) / 2);
            g_MouseDispatch.PosY = Rect.Y1 + ((Rect.Y2 - Rect.Y1) / 2);
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Process a raw mouse delta and broadcast throttled events.
 *
 * The first movement after any idle period is dispatched immediately.
 * Subsequent movement broadcasts are spaced by at least
 * MOUSE_MOVE_COOLDOWN_MS. Button transitions are always broadcast
 * immediately.
 *
 * @param DeltaX Signed X delta.
 * @param DeltaY Signed Y delta.
 * @param Buttons Current button bitmask (MB_*).
 */
void MouseDispatcherOnInput(I32 DeltaX, I32 DeltaY, U32 Buttons) {
    if (g_MouseDispatch.Initialized == FALSE) {
        return;
    }

    UINT Flags;
    RECT ScreenRect;
    BOOL HasRect = FALSE;
    BOOL ConsoleMode = FALSE;
    I32 PosX = 0;
    I32 PosY = 0;
    U32 PreviousButtons;
    U32 DownButtons = 0;
    U32 UpButtons = 0;
    BOOL SendMove = FALSE;
    U32 Now = GetSystemTime();
    I32 OldPosX = 0;
    I32 OldPosY = 0;
    I32 NewPosX = 0;
    I32 NewPosY = 0;
    BOOL PositionChanged = FALSE;
    LPDESKTOP ActiveDesktop = NULL;

    {
        LPDESKTOP Desktop = GetActiveDesktop();
        HasRect = GetDesktopScreenRect(Desktop, &ScreenRect);
        SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
            ConsoleMode = (Desktop->Mode == DESKTOP_MODE_CONSOLE);
        }
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    OldPosX = g_MouseDispatch.PosX;
    OldPosY = g_MouseDispatch.PosY;

    if (ConsoleMode) {
        I32 ScaleX;
        I32 ScaleY;
        GetConsoleMouseScale(&ScaleX, &ScaleY);
        DeltaX = ConsumeScaledDelta(DeltaX, ScaleX, &(g_MouseDispatch.ResidualX));
        DeltaY = ConsumeScaledDelta(DeltaY, ScaleY, &(g_MouseDispatch.ResidualY));
    } else {
        g_MouseDispatch.ResidualX = 0;
        g_MouseDispatch.ResidualY = 0;
    }

    g_MouseDispatch.PosX += DeltaX;
    g_MouseDispatch.PosY += DeltaY;

    if (HasRect) {
        ClampMousePosition(&(g_MouseDispatch.PosX), &(g_MouseDispatch.PosY), &ScreenRect);
    }

    PreviousButtons = g_MouseDispatch.Buttons;
    g_MouseDispatch.Buttons = Buttons;

    DownButtons = (~PreviousButtons) & Buttons;
    UpButtons = PreviousButtons & (~Buttons);

    if ((DeltaX != 0 || DeltaY != 0) && CooldownTryArm(&(g_MouseDispatch.MoveCooldown), Now)) {
        SendMove = TRUE;
        PosX = g_MouseDispatch.PosX;
        PosY = g_MouseDispatch.PosY;
    }

    PositionChanged = (OldPosX != g_MouseDispatch.PosX || OldPosY != g_MouseDispatch.PosY);
    NewPosX = g_MouseDispatch.PosX;
    NewPosY = g_MouseDispatch.PosY;
    ActiveDesktop = GetActiveDesktop();

    RestoreFlags(&Flags);

    if (DownButtons) {
        EnqueueInputMessage(EWM_MOUSEDOWN, DownButtons, 0);
    }

    if (UpButtons) {
        EnqueueInputMessage(EWM_MOUSEUP, UpButtons, 0);
    }

    if (SendMove) {
        EnqueueInputMessage(EWM_MOUSEMOVE, UNSIGNED(PosX), UNSIGNED(PosY));
    }

    if (PositionChanged != FALSE) {
        DesktopCursorOnMousePositionChanged(ActiveDesktop, OldPosX, OldPosY, NewPosX, NewPosY);
    }
}

/************************************************************************/

/**
 * @brief Retrieve the current mouse cursor position.
 * @param X Output X coordinate.
 * @param Y Output Y coordinate.
 * @return TRUE when the dispatcher is initialized and the position is copied.
 */
BOOL GetMouseScreenPosition(I32* X, I32* Y) {
    UINT Flags;
    I32 CurrentX;
    I32 CurrentY;

    if (X == NULL || Y == NULL) {
        return FALSE;
    }

    if (g_MouseDispatch.Initialized == FALSE) {
        return FALSE;
    }

    SaveFlags(&Flags);
    DisableInterrupts();

    CurrentX = g_MouseDispatch.PosX;
    CurrentY = g_MouseDispatch.PosY;

    RestoreFlags(&Flags);

    *X = CurrentX;
    *Y = CurrentY;

    return TRUE;
}

/************************************************************************/
