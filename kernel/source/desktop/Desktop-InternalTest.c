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


    Desktop internal test module

\************************************************************************/

#include "Desktop-InternalTest.h"
#include "Desktop-Private.h"
#include "system/Clock.h"
#include "core/Kernel.h"
#include "log/Log.h"

/***************************************************************************/
// Macros

#define DESKTOP_INTERNAL_TEST_WINDOW_ID_A 0x000085A1
#define DESKTOP_INTERNAL_ON_SCREEN_DEBUG_INFO_WINDOW_ID 0x000085D1

/***************************************************************************/

/**
 * @brief Generate one deterministic pseudo-random value for stress drag motion.
 * @param State In-out generator state.
 * @return Next pseudo-random value.
 */
static U32 DesktopInternalStressNextRandom(U32* State) {
    if (State == NULL) return 0;
    *State = (*State * 1664525) + 1013904223;
    return *State;
}

/***************************************************************************/

/**
 * @brief Resolve the centered half-size test window rectangle in desktop coordinates.
 * @param Desktop Target desktop.
 * @param Rect Receives the resulting rectangle.
 * @return TRUE on success.
 */
static BOOL DesktopInternalResolveCenteredWindowRect(LPDESKTOP Desktop, LPRECT Rect) {
    RECT DesktopRect;
    I32 DesktopWidth;
    I32 DesktopHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Rect == NULL) return FALSE;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return FALSE;

    if (GetDesktopScreenRect(Desktop, &DesktopRect) == FALSE) return FALSE;

    DesktopWidth = DesktopRect.X2 - DesktopRect.X1 + 1;
    DesktopHeight = DesktopRect.Y2 - DesktopRect.Y1 + 1;
    if (DesktopWidth <= 0 || DesktopHeight <= 0) return FALSE;

    WindowWidth = DesktopWidth / 2;
    WindowHeight = DesktopHeight / 2;
    if (WindowWidth <= 0) WindowWidth = 1;
    if (WindowHeight <= 0) WindowHeight = 1;

    Rect->X1 = DesktopRect.X1 + ((DesktopWidth - WindowWidth) / 2);
    Rect->Y1 = DesktopRect.Y1 + ((DesktopHeight - WindowHeight) / 2);
    Rect->X2 = Rect->X1 + WindowWidth - 1;
    Rect->Y2 = Rect->Y1 + WindowHeight - 1;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve the top-left debug-info component rectangle in desktop coordinates.
 * @param Desktop Target desktop.
 * @param Rect Receives the resulting rectangle.
 * @return TRUE on success.
 */
static BOOL DesktopInternalResolveOnScreenDebugInfoRect(LPDESKTOP Desktop, LPRECT Rect) {
    RECT DesktopRect;
    I32 DesktopWidth;
    I32 DesktopHeight;
    I32 Width;
    I32 Height;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Rect == NULL) return FALSE;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return FALSE;

    if (GetDesktopScreenRect(Desktop, &DesktopRect) == FALSE) return FALSE;

    DesktopWidth = DesktopRect.X2 - DesktopRect.X1 + 1;
    DesktopHeight = DesktopRect.Y2 - DesktopRect.Y1 + 1;
    if (DesktopWidth <= 0 || DesktopHeight <= 0) return FALSE;

    Width = DesktopWidth / 2;
    Height = DesktopHeight / 4;
    if (Width <= 0) Width = 1;
    if (Height <= 0) Height = 1;

    Rect->X1 = DesktopRect.X1;
    Rect->Y1 = DesktopRect.Y1;
    Rect->X2 = Rect->X1 + Width - 1;
    Rect->Y2 = Rect->Y1 + Height - 1;
    return TRUE;
}

/**
 * @brief Move one window while preserving its current size.
 * @param Window Target window.
 * @param Position New top-left position in parent coordinates.
 * @return TRUE on success.
 */
static BOOL DesktopInternalMoveWindowToPosition(LPWINDOW Window, LPPOINT Position) {
    RECT CurrentRect;
    RECT NewRect;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Position == NULL) return FALSE;
    if (GetWindowRect((HANDLE)Window, &CurrentRect) == FALSE) return FALSE;

    NewRect.X1 = Position->X;
    NewRect.Y1 = Position->Y;
    NewRect.X2 = Position->X + (CurrentRect.X2 - CurrentRect.X1);
    NewRect.Y2 = Position->Y + (CurrentRect.Y2 - CurrentRect.Y1);

    return MoveWindow((HANDLE)Window, &NewRect);
}

/***************************************************************************/

/**
 * @brief Move one window progressively toward one X target with variable steps.
 * @param Window Target window.
 * @param Position In-out current target position.
 * @param TargetX Destination X coordinate.
 * @param StartY Nominal Y anchor.
 * @param MinY Minimum allowed Y.
 * @param MaxY Maximum allowed Y.
 * @param CurrentY In-out current Y.
 * @param TargetY In-out smoothed Y target.
 * @param RandomState In-out pseudo-random state.
 * @return TRUE on success.
 */
static BOOL DesktopInternalStressMoveTowardX(
    LPWINDOW Window,
    LPPOINT Position,
    I32 TargetX,
    I32 StartY,
    I32 MinY,
    I32 MaxY,
    I32* CurrentY,
    I32* TargetY,
    U32* RandomState
) {
    I32 StepX;
    I32 YOffset;
    U32 RandomValue;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Position == NULL || CurrentY == NULL || TargetY == NULL || RandomState == NULL) return FALSE;

    while (Position->X != TargetX) {
        RandomValue = DesktopInternalStressNextRandom(RandomState);

        if ((RandomValue & 7) == 0) {
            YOffset = (I32)(DesktopInternalStressNextRandom(RandomState) & 0x1FF) - 220;
            *TargetY = StartY + YOffset;
            if (*TargetY < MinY) *TargetY = MinY;
            if (*TargetY > MaxY) *TargetY = MaxY;
        }

        if (*CurrentY < *TargetY)
            *CurrentY += 2;
        else if (*CurrentY > *TargetY)
            *CurrentY -= 2;

        if (*CurrentY < MinY) *CurrentY = MinY;
        if (*CurrentY > MaxY) *CurrentY = MaxY;

        StepX = 2 + (I32)(RandomValue & 0x07);
        if ((RandomValue & 0x10) != 0) {
            StepX += 6 + (I32)((RandomValue >> 5) & 0x0F);
        }
        if ((RandomValue & 0x200) != 0) {
            StepX += 18 + (I32)((RandomValue >> 11) & 0x1F);
        }

        if (Position->X < TargetX) {
            Position->X += StepX;
            if (Position->X > TargetX) Position->X = TargetX;
        } else {
            Position->X -= StepX;
            if (Position->X < TargetX) Position->X = TargetX;
        }

        Position->Y = *CurrentY;
        (void)DesktopInternalMoveWindowToPosition(Window, Position);
        Sleep(20);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Internal test window procedure.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
static U32 DesktopInternalTestWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW This;

    This = (LPWINDOW)Window;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        if (Message == EWM_CREATE) {
        } else if (Message == EWM_DRAW) {
        }
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/

/**
 * @brief Find one direct desktop child window by identifier.
 * @param Desktop Desktop whose root children are inspected.
 * @param WindowID Target window identifier.
 * @return Matching child window pointer or NULL.
 */
static LPWINDOW DesktopInternalFindTestWindow(LPDESKTOP Desktop, U32 WindowID) {
    LPWINDOW RootWindow;
    LPWINDOW Candidate;
    LPWINDOW Found = NULL;
    UINT ChildCount;
    UINT Index;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    RootWindow = Desktop->Window;
    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return NULL;

    ChildCount = GetWindowChildCount((HANDLE)RootWindow);
    for (Index = 0; Index < ChildCount; Index++) {
        Candidate = (LPWINDOW)GetWindowChild((HANDLE)RootWindow, Index);
        if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;
        if (Candidate->WindowID != WindowID) continue;
        Found = Candidate;
        break;
    }
    return Found;
}

/***************************************************************************/

/**
 * @brief Ensure one internal test window exists and is visible.
 * @param Desktop Target desktop.
 * @param WindowID Window identifier.
 * @param Title Internal title tag used for diagnostics.
 * @param Caption Caption applied to the test window, or NULL for none.
 * @param X Left position in desktop coordinates.
 * @param Y Top position in desktop coordinates.
 * @param Width Width in pixels.
 * @param Height Height in pixels.
 * @return TRUE on success.
 */
static BOOL DesktopInternalEnsureSingleWindow(
    LPDESKTOP Desktop,
    U32 WindowID,
    LPCSTR Title,
    LPCSTR Caption,
    LPCSTR WindowClassName,
    WINDOWFUNC WindowFunc,
    U32 WindowStyle,
    I32 X,
    I32 Y,
    I32 Width,
    I32 Height
) {
    LPWINDOW Window;
    WINDOW_INFO WindowInfo;
    RECT WindowRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    Window = DesktopInternalFindTestWindow(Desktop, WindowID);
    WindowRect.X1 = X;
    WindowRect.Y1 = Y;
    WindowRect.X2 = X + Width - 1;
    WindowRect.Y2 = Y + Height - 1;

    if (Window != NULL && Window->TypeID == KOID_WINDOW) {
        (void)MoveWindow((HANDLE)Window, &WindowRect);
        (void)SetWindowCaption((HANDLE)Window, Caption);
        (void)ShowWindow((HANDLE)Window);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = WindowClassName;
    WindowInfo.Function = WindowFunc;
    WindowInfo.Style = WindowStyle;
    WindowInfo.ID = WindowID;
    WindowInfo.WindowPosition.X = X;
    WindowInfo.WindowPosition.Y = Y;
    WindowInfo.WindowSize.X = Width;
    WindowInfo.WindowSize.Y = Height;
    WindowInfo.ShowHide = TRUE;

    Window = (LPWINDOW)CreateWindow(&WindowInfo);
    if (Window == NULL) {
        WARNING(TEXT("Test window creation failed title=%s id=%x"), Title, WindowID);
        return FALSE;
    }

    (void)SetWindowCaption((HANDLE)Window, Caption);
    (void)ShowWindow((HANDLE)Window);
    return TRUE;
}

/***************************************************************************/

BOOL DesktopInternalTestEnsureWindowsVisible(LPDESKTOP Desktop) {
    BOOL FirstCreated;
    BOOL DebugInfoCreated;
    RECT DebugInfoRect;
    RECT WindowRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (DesktopInternalResolveCenteredWindowRect(Desktop, &WindowRect) == FALSE) return FALSE;
    if (DesktopInternalResolveOnScreenDebugInfoRect(Desktop, &DebugInfoRect) == FALSE) return FALSE;

    FirstCreated = DesktopInternalEnsureSingleWindow(
        Desktop,
        DESKTOP_INTERNAL_TEST_WINDOW_ID_A,
        TEXT("Kernel Test Alpha"),
        TEXT("Test"),
        NULL,
        DesktopInternalTestWindowFunc,
        EWS_VISIBLE | EWS_SYSTEM_DECORATED,
        WindowRect.X1,
        WindowRect.Y1,
        WindowRect.X2 - WindowRect.X1 + 1,
        WindowRect.Y2 - WindowRect.Y1 + 1);

    DebugInfoCreated = DesktopInternalEnsureSingleWindow(
        Desktop,
        DESKTOP_INTERNAL_ON_SCREEN_DEBUG_INFO_WINDOW_ID,
        TEXT("DebugInfo"),
        NULL,
        NULL,
        DesktopInternalTestWindowFunc,
        EWS_VISIBLE | EWS_BARE_SURFACE | EWS_ALWAYS_AT_BOTTOM,
        DebugInfoRect.X1,
        DebugInfoRect.Y1,
        DebugInfoRect.X2 - DebugInfoRect.X1 + 1,
        DebugInfoRect.Y2 - DebugInfoRect.Y1 + 1);

    return FirstCreated && DebugInfoCreated;

//    return TRUE;
}

/***************************************************************************/

BOOL DesktopInternalRunStressDrag(LPDESKTOP Desktop, U32 Cycles) {
    LPWINDOW FirstWindow;
    LPWINDOW RootWindow;
    POINT Position;
    I32 CurrentY;
    I32 TargetY;
    I32 MinY;
    I32 MaxY;
    I32 StartY;
    I32 DesktopWidth;
    I32 DesktopHeight;
    I32 LeftOffscreenX;
    I32 RightOffscreenX;
    I32 TargetLeftX;
    I32 TargetRightX;
    I32 RightBaseOffscreen;
    I32 LeftBaseOffscreen;
    I32 ExtraRightAmplitude;
    I32 ExtraLeftAmplitude;
    U32 CycleIndex;
    U32 RandomState;
    const I32 StartX = 48;
    const I32 BaseStartY = 56;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    if (Cycles == 0) {
        Cycles = 12;
    }

    if (DesktopInternalTestEnsureWindowsVisible(Desktop) == FALSE) {
        WARNING(TEXT("Test windows unavailable"));
        return FALSE;
    }

    FirstWindow = DesktopInternalFindTestWindow(Desktop, DESKTOP_INTERNAL_TEST_WINDOW_ID_A);
    if (FirstWindow == NULL || FirstWindow->TypeID != KOID_WINDOW) {
        WARNING(TEXT("Missing test window id=%x"), DESKTOP_INTERNAL_TEST_WINDOW_ID_A);
        return FALSE;
    }

    (void)BringWindowToFront((HANDLE)FirstWindow);

    RootWindow = Desktop->Window;
    DesktopWidth = 1280;
    DesktopHeight = 1024;
    SAFE_USE_VALID_ID(RootWindow, KOID_WINDOW) {
        RECT RootRect;
        if (GetWindowScreenRectSnapshot(RootWindow, &RootRect) != FALSE) {
            DesktopWidth = RootRect.X2 - RootRect.X1 + 1;
            DesktopHeight = RootRect.Y2 - RootRect.Y1 + 1;
        }
    }

    if (DesktopWidth < 640) DesktopWidth = 640;
    if (DesktopHeight < 480) DesktopHeight = 480;

    StartY = BaseStartY;
    LeftOffscreenX = -260;
    RightOffscreenX = DesktopWidth - 40;
    MinY = -180;
    MaxY = DesktopHeight - 40;

    Position.Y = StartY;
    Position.X = StartX;
    (void)DesktopInternalMoveWindowToPosition(FirstWindow, &Position);
    Sleep(40);

    CurrentY = StartY;
    TargetY = StartY;
    RandomState = GetSystemTime() ^ (Cycles << 8);
    if (RandomState == 0) RandomState = 0xA5A5A5A5;

    ExtraRightAmplitude = DesktopWidth / 2;
    if (ExtraRightAmplitude < 120) ExtraRightAmplitude = 120;
    ExtraLeftAmplitude = DesktopWidth / 2;
    if (ExtraLeftAmplitude < 120) ExtraLeftAmplitude = 120;

    RightBaseOffscreen = RightOffscreenX + 120;
    LeftBaseOffscreen = LeftOffscreenX - 120;


    for (CycleIndex = 0; CycleIndex < Cycles; CycleIndex++) {
        TargetRightX =
            RightBaseOffscreen + (I32)(DesktopInternalStressNextRandom(&RandomState) % (U32)ExtraRightAmplitude);
        TargetLeftX =
            LeftBaseOffscreen - (I32)(DesktopInternalStressNextRandom(&RandomState) % (U32)ExtraLeftAmplitude);

        (void)DesktopInternalStressMoveTowardX(
            FirstWindow,
            &Position,
            TargetRightX,
            StartY,
            MinY,
            MaxY,
            &CurrentY,
            &TargetY,
            &RandomState);
        Sleep(130);

        (void)DesktopInternalStressMoveTowardX(
            FirstWindow,
            &Position,
            TargetLeftX,
            StartY,
            MinY,
            MaxY,
            &CurrentY,
            &TargetY,
            &RandomState);
        Sleep(160);
    }

    Position.X = StartX;
    Position.Y = StartY;
    (void)DesktopInternalMoveWindowToPosition(FirstWindow, &Position);
    return TRUE;
}

/***************************************************************************/
