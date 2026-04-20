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


    Desktop cursor ownership and rendering

\************************************************************************/

#include "Desktop-Cursor.h"

#include "Desktop-Private.h"
#include "Desktop.h"
#include "Desktop-OverlayInvalidation.h"
#include "text/CoreString.h"
#include "DisplaySession.h"
#include "GFX.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "system/Clock.h"
#include "input/MouseDispatcher.h"
#include "utils/Helpers.h"
#include "utils/Graphics-Utils.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define DESKTOP_CURSOR_MIN_SIZE 16
#define DESKTOP_CURSOR_DEFAULT_WIDTH 16
#define DESKTOP_CURSOR_DEFAULT_HEIGHT 16
#define DESKTOP_CURSOR_MAX_SIZE 64

/************************************************************************/

static void DesktopCursorInitializeHeader(ABI_HEADER* Header, U32 Size);

/************************************************************************/

/**
 * @brief Clamp one value to inclusive bounds.
 * @param Value Input value.
 * @param Minimum Minimum bound.
 * @param Maximum Maximum bound.
 * @return Clamped value.
 */
static I32 ClampI32(I32 Value, I32 Minimum, I32 Maximum) {
    if (Value < Minimum) return Minimum;
    if (Value > Maximum) return Maximum;
    return Value;
}

/************************************************************************/

static CONST char DesktopCursorArrowTemplate[16][17] = {
    "BBTTTTTTTTTTTTTT",
    "BWBTTTTTTTTTTTTT",
    "BWWBTTTTTTTTTTTT",
    "BWBWBTTTTTTTTTTT",
    "BWBBWBTTTTTTTTTT",
    "BWBBBWBTTTTTTTTT",
    "BWBBBBWBTTTTTTTT",
    "BWBBBBBWBTTTTTTT",
    "BWBBBBBBWBTTTTTT",
    "BWBBBBWWWWBTTTTT",
    "BWBWWWBBBBTTTTTT",
    "BWWBWWWBTTTTTTTT",
    "BWBTBWWBTTTTTTTT",
    "BBTTBWWWBTTTTTTT",
    "BTTTTBWWBTTTTTTT",
    "TTTTTBBBTTTTTTTT"
};

/************************************************************************/

/**
 * @brief Sample one cursor template pixel for one destination cursor size.
 * @param X Destination X.
 * @param Y Destination Y.
 * @param Width Destination width.
 * @param Height Destination height.
 * @return Template token ('B','W','T').
 */
static char DesktopCursorSampleTemplate(U32 X, U32 Y, U32 Width, U32 Height) {
    U32 SourceX;
    U32 SourceY;

    if (Width == 0 || Height == 0) return 'T';

    SourceX = (X * 16) / Width;
    SourceY = (Y * 16) / Height;

    if (SourceX > 15) SourceX = 15;
    if (SourceY > 15) SourceY = 15;

    return DesktopCursorArrowTemplate[SourceY][SourceX];
}

/************************************************************************/

/**
 * @brief Build one ARGB cursor bitmap from template.
 * @param Pixels Destination pixel buffer.
 * @param Width Cursor width.
 * @param Height Cursor height.
 */
static void DesktopCursorBuildArrowPixels(U32* Pixels, U32 Width, U32 Height) {
    U32 X;
    U32 Y;
    char TemplateToken;

    if (Pixels == NULL || Width == 0 || Height == 0) return;

    for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
            TemplateToken = DesktopCursorSampleTemplate(X, Y, Width, Height);

            if (TemplateToken == 'B') {
                Pixels[(Y * Width) + X] = 0xFF000000;
            } else if (TemplateToken == 'W') {
                Pixels[(Y * Width) + X] = 0xFFFFFFFF;
            } else {
                Pixels[(Y * Width) + X] = 0x00000000;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Draw one cursor template on one graphics context.
 * @param GC Target graphics context.
 * @param OriginX Cursor origin X in window coordinates.
 * @param OriginY Cursor origin Y in window coordinates.
 * @param Width Cursor width.
 * @param Height Cursor height.
 */
static void DesktopCursorDrawTemplate(HANDLE GC, I32 OriginX, I32 OriginY, U32 Width, U32 Height) {
    PIXEL_INFO PixelInfo;
    U32 X;
    U32 Y;
    char TemplateToken;

    MemorySet(&PixelInfo, 0, sizeof(PixelInfo));
    DesktopCursorInitializeHeader(&(PixelInfo.Header), sizeof(PixelInfo));
    PixelInfo.GC = GC;

    for (Y = 0; Y < Height; Y++) {
        for (X = 0; X < Width; X++) {
            TemplateToken = DesktopCursorSampleTemplate(X, Y, Width, Height);
            if (TemplateToken == 'T') continue;

            PixelInfo.X = OriginX + (I32)X;
            PixelInfo.Y = OriginY + (I32)Y;
            PixelInfo.Color = (TemplateToken == 'B') ? 0xFF000000 : 0xFFFFFFFF;
            (void)KernelSetPixel(&PixelInfo);
        }
    }
}

/************************************************************************/

/**
 * @brief Build cursor rectangle in screen coordinates.
 * @param X Cursor X position.
 * @param Y Cursor Y position.
 * @param RectOut Output rectangle.
 */
static void DesktopCursorBuildRect(LPDESKTOP Desktop, I32 X, I32 Y, LPRECT RectOut) {
    U32 CursorWidth = DESKTOP_CURSOR_DEFAULT_WIDTH;
    U32 CursorHeight = DESKTOP_CURSOR_DEFAULT_HEIGHT;

    if (Desktop != NULL && Desktop->TypeID == KOID_DESKTOP) {
        if (Desktop->Cursor.Width >= DESKTOP_CURSOR_MIN_SIZE) CursorWidth = Desktop->Cursor.Width;
        if (Desktop->Cursor.Height >= DESKTOP_CURSOR_MIN_SIZE) CursorHeight = Desktop->Cursor.Height;
    }

    if (RectOut == NULL) return;

    RectOut->X1 = X;
    RectOut->Y1 = Y;
    RectOut->X2 = X + (I32)CursorWidth - 1;
    RectOut->Y2 = Y + (I32)CursorHeight - 1;
}

/************************************************************************/

/**
 * @brief Build the bounding rectangle of two rectangles.
 * @param Left First rectangle.
 * @param Right Second rectangle.
 * @param Result Output bounding rectangle.
 */
static void DesktopCursorUnionRect(LPRECT Left, LPRECT Right, LPRECT Result) {
    if (Left == NULL || Right == NULL || Result == NULL) return;

    Result->X1 = Left->X1 < Right->X1 ? Left->X1 : Right->X1;
    Result->Y1 = Left->Y1 < Right->Y1 ? Left->Y1 : Right->Y1;
    Result->X2 = Left->X2 > Right->X2 ? Left->X2 : Right->X2;
    Result->Y2 = Left->Y2 > Right->Y2 ? Left->Y2 : Right->Y2;
}

/**
 * @brief Build one ABI header.
 * @param Header Output header.
 * @param Size Structure size.
 */
static void DesktopCursorInitializeHeader(ABI_HEADER* Header, U32 Size) {
    if (Header == NULL) return;

    Header->Size = Size;
    Header->Version = EXOS_ABI_VERSION;
    Header->Flags = 0;
}

/************************************************************************/

/**
 * @brief Convert cursor fallback reason to descriptive text.
 * @param Reason Fallback reason identifier.
 * @return Constant reason text.
 */
static LPCSTR DesktopCursorFallbackReasonToText(U32 Reason) {
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
 * @brief Update cursor diagnostics path and fallback reason.
 * @param Desktop Target desktop.
 * @param Path New cursor path value.
 * @param Reason New fallback reason value.
 * @param DriverStatus Driver status associated with fallback.
 */
static void DesktopCursorSetPathState(LPDESKTOP Desktop, U32 Path, U32 Reason, UINT DriverStatus) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    if (Desktop->Cursor.RenderPath == Path && Desktop->Cursor.FallbackReason == Reason) {
        UnlockMutex(&(Desktop->Mutex));
        return;
    }

    Desktop->Cursor.RenderPath = Path;
    Desktop->Cursor.FallbackReason = Reason;

    UnlockMutex(&(Desktop->Mutex));

    if (Path == DESKTOP_CURSOR_PATH_HARDWARE) {
    } else {
        WARNING(TEXT("Cursor path=software reason=%s"),
            DesktopCursorFallbackReasonToText(Reason));
    }
    UNUSED(DriverStatus);
}

/************************************************************************/

/**
 * @brief Request bounded software cursor redraw for old and new cursor rectangles.
 * @param Desktop Target desktop.
 * @param OldX Previous rendered X.
 * @param OldY Previous rendered Y.
 * @param NewX Pending X.
 * @param NewY Pending Y.
 */
static void DesktopCursorRequestSoftwareRedraw(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY) {
    RECT OldRect;
    RECT NewRect;
    RECT DamageRect = {0};
    RECT ClipRect;
    BOOL HasOldRect = FALSE;
    BOOL HasNewRect = FALSE;
    BOOL HasDamageRect = FALSE;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return;

    LockMutex(&(Desktop->Mutex), INFINITY);
    ClipRect = Desktop->Cursor.ClipRect;
    UnlockMutex(&(Desktop->Mutex));

    DesktopCursorBuildRect(Desktop, OldX, OldY, &OldRect);
    DesktopCursorBuildRect(Desktop, NewX, NewY, &NewRect);

    HasOldRect = IntersectRect(&OldRect, &ClipRect, &OldRect);
    HasNewRect = IntersectRect(&NewRect, &ClipRect, &NewRect);

    if (HasOldRect != FALSE) {
        DamageRect = OldRect;
        HasDamageRect = TRUE;
    }

    if (HasNewRect != FALSE) {
        if (HasDamageRect == FALSE) {
            DamageRect = NewRect;
            HasDamageRect = TRUE;
        } else {
            DesktopCursorUnionRect(&DamageRect, &NewRect, &DamageRect);
        }
    }

    if (HasDamageRect != FALSE) {
        // Cursor software damage must invalidate only the intersecting area.
        DesktopOverlayInvalidateWindowTreeRect(Desktop->Window, &DamageRect, TRUE);
        (void)DesktopOverlayInvalidateRootRect(Desktop->Window, &DamageRect);
    }

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->Cursor.PendingX = OldX;
    Desktop->Cursor.PendingY = OldY;
    Desktop->Cursor.SoftwareDirty = (HasOldRect != FALSE);
    UnlockMutex(&(Desktop->Mutex));
}

/************************************************************************/

/**
 * @brief Clamp one cursor size value to accepted bounds.
 * @param Value Requested value.
 * @param DefaultValue Fallback value when requested value is invalid.
 * @return Clamped size value.
 */
static U32 DesktopCursorClampSize(U32 Value, U32 DefaultValue) {
    if (Value == 0) Value = DefaultValue;
    if (Value < DESKTOP_CURSOR_MIN_SIZE) Value = DESKTOP_CURSOR_MIN_SIZE;
    if (Value > DESKTOP_CURSOR_MAX_SIZE) Value = DESKTOP_CURSOR_MAX_SIZE;
    return Value;
}

/************************************************************************/

/**
 * @brief Resolve cursor size from configuration.
 * @param Desktop Target desktop.
 */
static void DesktopCursorResolveConfiguredSize(LPDESKTOP Desktop) {
    LPCSTR WidthText;
    LPCSTR HeightText;
    U32 Width = DESKTOP_CURSOR_DEFAULT_WIDTH;
    U32 Height = DESKTOP_CURSOR_DEFAULT_HEIGHT;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    WidthText = GetConfigurationValue(TEXT("Desktop.CursorWidth"));
    HeightText = GetConfigurationValue(TEXT("Desktop.CursorHeight"));

    if (WidthText != NULL && StringLength(WidthText) != 0) {
        Width = StringToU32(WidthText);
    }

    if (HeightText != NULL && StringLength(HeightText) != 0) {
        Height = StringToU32(HeightText);
    }

    Width = DesktopCursorClampSize(Width, DESKTOP_CURSOR_DEFAULT_WIDTH);
    Height = DesktopCursorClampSize(Height, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->Cursor.Width = Width;
    Desktop->Cursor.Height = Height;
    UnlockMutex(&(Desktop->Mutex));
}

/************************************************************************/

/**
 * @brief Try to enable hardware cursor path on one graphics backend.
 * @param Desktop Target desktop.
 * @param GraphicsDriver Active graphics driver.
 * @return TRUE when hardware path is active.
 */
static BOOL DesktopCursorTryEnableHardware(LPDESKTOP Desktop, LPDRIVER GraphicsDriver) {
    GFX_CAPABILITIES Capabilities;
    GFX_CURSOR_SHAPE_INFO ShapeInfo;
    GFX_CURSOR_POSITION_INFO PositionInfo;
    GFX_CURSOR_VISIBLE_INFO VisibleInfo;
    UINT Status;
    U32 Pixels[DESKTOP_CURSOR_MAX_SIZE * DESKTOP_CURSOR_MAX_SIZE];
    U32 CursorWidth;
    U32 CursorHeight;
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES, DF_RETURN_GENERIC);
        return FALSE;
    }

    MemorySet(&Capabilities, 0, sizeof(Capabilities));
    DesktopCursorInitializeHeader(&(Capabilities.Header), sizeof(Capabilities));

    Status = GraphicsDriver->Command(DF_GFX_GETCAPABILITIES, (UINT)(LPVOID)&Capabilities);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES, Status);
        return FALSE;
    }

    if (Capabilities.HasCursorPlane == FALSE) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE, DF_RETURN_SUCCESS);
        return FALSE;
    }

    LockMutex(&(Desktop->Mutex), INFINITY);
    CursorWidth = Desktop->Cursor.Width;
    CursorHeight = Desktop->Cursor.Height;
    UnlockMutex(&(Desktop->Mutex));

    CursorWidth = DesktopCursorClampSize(CursorWidth, DESKTOP_CURSOR_DEFAULT_WIDTH);
    CursorHeight = DesktopCursorClampSize(CursorHeight, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    DesktopCursorBuildArrowPixels(Pixels, CursorWidth, CursorHeight);

    MemorySet(&ShapeInfo, 0, sizeof(ShapeInfo));
    DesktopCursorInitializeHeader(&(ShapeInfo.Header), sizeof(ShapeInfo));
    ShapeInfo.Width = CursorWidth;
    ShapeInfo.Height = CursorHeight;
    ShapeInfo.HotspotX = 0;
    ShapeInfo.HotspotY = 0;
    ShapeInfo.Format = GFX_CURSOR_FORMAT_ARGB8888;
    ShapeInfo.Pitch = CursorWidth * sizeof(U32);
    ShapeInfo.Pixels = Pixels;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_SHAPE, (UINT)(LPVOID)&ShapeInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED, Status);
        return FALSE;
    }

    MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
    DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
    PositionInfo.X = Desktop->Cursor.X;
    PositionInfo.Y = Desktop->Cursor.Y;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
        return FALSE;
    }

    MemorySet(&VisibleInfo, 0, sizeof(VisibleInfo));
    DesktopCursorInitializeHeader(&(VisibleInfo.Header), sizeof(VisibleInfo));
    VisibleInfo.IsVisible = Desktop->Cursor.Visible;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_VISIBLE, (UINT)(LPVOID)&VisibleInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED, Status);
        return FALSE;
    }

    DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_HARDWARE, DESKTOP_CURSOR_FALLBACK_NONE, DF_RETURN_SUCCESS);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Refresh cursor clipping against desktop bounds and clamp cursor position.
 * @param Desktop Target desktop.
 */
static void DesktopCursorRefreshClipAndPosition(LPDESKTOP Desktop) {
    RECT ScreenRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Desktop->Cursor.ClipRect = ScreenRect;
    Desktop->Cursor.X = ClampI32(Desktop->Cursor.X, ScreenRect.X1, ScreenRect.X2);
    Desktop->Cursor.Y = ClampI32(Desktop->Cursor.Y, ScreenRect.Y1, ScreenRect.Y2);

    UnlockMutex(&(Desktop->Mutex));
}

/************************************************************************/

/**
 * @brief Initialize cursor ownership when one desktop enters graphics mode.
 * @param Desktop Target desktop.
 */
void DesktopCursorOnDesktopActivated(LPDESKTOP Desktop) {
    I32 CurrentX = 0;
    I32 CurrentY = 0;
    I32 CursorX;
    I32 CursorY;
    LPDRIVER GraphicsDriver;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    DesktopCursorResolveConfiguredSize(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);

    Desktop->Cursor.Visible = TRUE;
    if (GetMouseScreenPosition(&CurrentX, &CurrentY) == TRUE) {
        Desktop->Cursor.X = CurrentX;
        Desktop->Cursor.Y = CurrentY;
    }
    Desktop->Cursor.PendingX = Desktop->Cursor.X;
    Desktop->Cursor.PendingY = Desktop->Cursor.Y;
    Desktop->Cursor.SoftwareDirty = FALSE;

    UnlockMutex(&(Desktop->Mutex));

    DesktopCursorRefreshClipAndPosition(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);
    CursorX = Desktop->Cursor.X;
    CursorY = Desktop->Cursor.Y;
    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS, DF_RETURN_SUCCESS);
        return;
    }

    GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
    if (DesktopCursorTryEnableHardware(Desktop, GraphicsDriver) == FALSE) {
        DesktopCursorRequestSoftwareRedraw(Desktop, CursorX, CursorY, CursorX, CursorY);
    }
}

/************************************************************************/

/**
 * @brief Apply one mouse position update to desktop cursor state.
 * @param Desktop Target desktop.
 * @param OldX Previous X position.
 * @param OldY Previous Y position.
 * @param NewX New X position.
 * @param NewY New Y position.
 */
void DesktopCursorOnMousePositionChanged(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY) {
    LPDRIVER GraphicsDriver;
    GFX_CURSOR_POSITION_INFO PositionInfo;
    UINT Status;
    RECT ClipRect;
    BOOL IsVisible;
    U32 CursorPath;
    I32 ClampedOldX;
    I32 ClampedOldY;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    DesktopCursorRefreshClipAndPosition(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);

    ClipRect = Desktop->Cursor.ClipRect;
    NewX = ClampI32(NewX, ClipRect.X1, ClipRect.X2);
    NewY = ClampI32(NewY, ClipRect.Y1, ClipRect.Y2);
    ClampedOldX = ClampI32(OldX, ClipRect.X1, ClipRect.X2);
    ClampedOldY = ClampI32(OldY, ClipRect.Y1, ClipRect.Y2);

    Desktop->Cursor.X = NewX;
    Desktop->Cursor.Y = NewY;
    Desktop->Cursor.PendingX = NewX;
    Desktop->Cursor.PendingY = NewY;
    Desktop->Cursor.SoftwareDirty = FALSE;
    IsVisible = Desktop->Cursor.Visible;
    CursorPath = Desktop->Cursor.RenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS || IsVisible == FALSE) {
        return;
    }

    if (CursorPath == DESKTOP_CURSOR_PATH_HARDWARE) {
        GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
        if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, DF_RETURN_GENERIC);
            DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
            return;
        }

        MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
        DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
        PositionInfo.X = NewX;
        PositionInfo.Y = NewY;

        Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
        if (Status != DF_RETURN_SUCCESS) {
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
            DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
        } else {
            LockMutex(&(Desktop->Mutex), INFINITY);
            Desktop->Cursor.X = NewX;
            Desktop->Cursor.Y = NewY;
            Desktop->Cursor.PendingX = NewX;
            Desktop->Cursor.PendingY = NewY;
            Desktop->Cursor.SoftwareDirty = FALSE;
            UnlockMutex(&(Desktop->Mutex));
        }

        return;
    }

    DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
}

/************************************************************************/

/**
 * @brief Render software cursor overlay on one window as final pass.
 * @param Window Target window.
 */
void DesktopCursorRenderSoftwareOverlayOnWindow(LPWINDOW Window) {
    LPDESKTOP Desktop;
    RECT WindowRect;
    RECT CursorRect;
    RECT ClipRect;
    RECT Intersection;
    RECT DrawClipStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DrawClipRegion;
    RECT DrawClipRect;
    I32 CursorX;
    I32 CursorY;
    U32 CursorWidth;
    U32 CursorHeight;
    BOOL IsVisible;
    U32 CursorPath;
    I32 LocalCursorX;
    I32 LocalCursorY;
    LPGRAPHICSCONTEXT GC = NULL;
    UINT ClipIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    CursorX = Desktop->Cursor.X;
    CursorY = Desktop->Cursor.Y;
    CursorWidth = Desktop->Cursor.Width;
    CursorHeight = Desktop->Cursor.Height;
    ClipRect = Desktop->Cursor.ClipRect;
    IsVisible = Desktop->Cursor.Visible;
    CursorPath = Desktop->Cursor.RenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) return;
    if (IsVisible == FALSE) return;
    if (CursorPath != DESKTOP_CURSOR_PATH_SOFTWARE) return;

    CursorWidth = DesktopCursorClampSize(CursorWidth, DESKTOP_CURSOR_DEFAULT_WIDTH);
    CursorHeight = DesktopCursorClampSize(CursorHeight, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    DesktopCursorBuildRect(Desktop, CursorX, CursorY, &CursorRect);
    if (IntersectRect(&CursorRect, &ClipRect, &CursorRect) == FALSE) {
        return;
    }

    if (GetWindowScreenRectSnapshot(Window, &WindowRect) == FALSE) return;

    if (IntersectRect(&CursorRect, &WindowRect, &Intersection) == FALSE) {
        return;
    }

    if (DesktopBuildWindowVisibleRegion(Window, &Intersection, TRUE, &DrawClipRegion, DrawClipStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
        return;
    }

    if (RectRegionGetCount(&DrawClipRegion) == 0) {
        return;
    }

    if (DesktopGetWindowGraphicsContext(Window, TRUE, &GC) == FALSE) return;
    LocalCursorX = CursorX - WindowRect.X1;
    LocalCursorY = CursorY - WindowRect.Y1;

    for (ClipIndex = 0; ClipIndex < RectRegionGetCount(&DrawClipRegion); ClipIndex++) {
        if (RectRegionGetRect(&DrawClipRegion, ClipIndex, &DrawClipRect) == FALSE) continue;
        (void)SetGraphicsContextClipScreenRect((HANDLE)GC, &DrawClipRect);
        DesktopCursorDrawTemplate((HANDLE)GC, LocalCursorX, LocalCursorY, CursorWidth, CursorHeight);
    }

    (void)ReleaseWindowGC((HANDLE)GC);
}

/************************************************************************/
