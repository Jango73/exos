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


    Desktop graphics and drawing

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-Cursor.h"
#include "Desktop-NonClient.h"
#include "Desktop-OverlayInvalidation.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "input/MouseDispatcher.h"
#include "process/Task-Messaging.h"
#include "utils/Graphics-Utils.h"
#include "utils/LineRasterizer.h"

/***************************************************************************/

#define DESKTOP_USE_TEMPORARY_FAST_ROOT_FILL 0

/***************************************************************************/

typedef struct tag_SYSTEM_DRAW_OBJECT_ENTRY {
    U32 SystemColor;
    LPBRUSH Brush;
    LPPEN Pen;
} SYSTEM_DRAW_OBJECT_ENTRY, *LPSYSTEM_DRAW_OBJECT_ENTRY;

/***************************************************************************/

static SYSTEM_DRAW_OBJECT_ENTRY SystemDrawObjects[] = {
    {SM_COLOR_DESKTOP, &Brush_Desktop, &Pen_Desktop},
    {SM_COLOR_HIGHLIGHT, &Brush_High, &Pen_High},
    {SM_COLOR_NORMAL, &Brush_Normal, &Pen_Normal},
    {SM_COLOR_LIGHT_SHADOW, &Brush_HiShadow, &Pen_HiShadow},
    {SM_COLOR_DARK_SHADOW, &Brush_LoShadow, &Pen_LoShadow},
    {SM_COLOR_CLIENT, &Brush_Client, &Pen_Client},
    {SM_COLOR_TEXT_NORMAL, &Brush_Text_Normal, &Pen_Text_Normal},
    {SM_COLOR_TEXT_SELECTED, &Brush_Text_Select, &Pen_Text_Select},
    {SM_COLOR_SELECTION, &Brush_Selection, &Pen_Selection},
    {SM_COLOR_TITLE_BAR, &Brush_Title_Bar, &Pen_Title_Bar},
    {SM_COLOR_TITLE_BAR_2, &Brush_Title_Bar_2, &Pen_Title_Bar_2},
    {SM_COLOR_TITLE_TEXT, &Brush_Title_Text, &Pen_Title_Text},
};

/**
 * @brief Convert one screen rectangle into coordinates relative to one window.
 * @param Window Window used as origin.
 * @param ScreenRect Rectangle in screen coordinates.
 * @param WindowRect Receives rectangle in window coordinates.
 * @return TRUE on success.
 */
static BOOL ConvertScreenRectToWindowRect(LPWINDOW Window, LPRECT ScreenRect, LPRECT WindowRect) {
    RECT WindowScreenRect;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL || WindowRect == NULL) return FALSE;

    if (GetWindowScreenRectSnapshot(Window, &WindowScreenRect) == FALSE) return FALSE;

    GraphicsScreenRectToWindowRect(&WindowScreenRect, ScreenRect, WindowRect);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Invalidate visible sibling windows intersecting one uncovered screen rectangle.
 * @param Window Moved window.
 * @param Parent Parent window containing sibling list.
 * @param UncoveredRect Screen rectangle uncovered by the move.
 */
static void InvalidateSiblingWindowsOnUncoveredRect(LPWINDOW Window, LPWINDOW Parent, LPRECT UncoveredRect) {
    LPWINDOW* Siblings;
    RECT SiblingScreenRect;
    RECT Intersection;
    RECT SiblingLocalRect;
    LPWINDOW Sibling;
    UINT Count;
    UINT Index;
    I32 WindowOrder;
    I32 SiblingOrder;
    BOOL IsVisible;
    WINDOW_STATE_SNAPSHOT SiblingSnapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;
    if (UncoveredRect == NULL) return;

    Count = 0;
    Siblings = NULL;
    WindowOrder = 0;
    (void)GetWindowOrderSnapshot(Window, &WindowOrder);
    (void)DesktopSnapshotWindowChildren(Parent, &Siblings, &Count);

    for (Index = 0; Index < Count; Index++) {
        Sibling = Siblings[Index];

        if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW || Sibling == Window) continue;

        if (GetWindowStateSnapshot(Sibling, &SiblingSnapshot) == FALSE) continue;
        IsVisible = ((SiblingSnapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
        SiblingOrder = SiblingSnapshot.Order;
        SiblingScreenRect = SiblingSnapshot.ScreenRect;

        if (IsVisible == FALSE) continue;
        if (SiblingOrder <= WindowOrder) continue;
        if (IntersectRect(&SiblingScreenRect, UncoveredRect, &Intersection) == FALSE) continue;

        GraphicsScreenRectToWindowRect(&SiblingScreenRect, &Intersection, &SiblingLocalRect);
        (void)InvalidateWindowRect((HANDLE)Sibling, &SiblingLocalRect);
    }

    if (Siblings != NULL) {
        KernelHeapFree(Siblings);
    }
}

/***************************************************************************/

/**
 * @brief Apply effective visibility recomputation and redraw side effects for one subtree root.
 * @param Window Window whose subtree visibility changed.
 * @param PreviousScreenRect Root screen rectangle before the visibility change.
 * @param WasVisible Previous effective visibility of the root window.
 */
static void DesktopHandleWindowEffectiveVisibilityChange(
    LPWINDOW Window,
    LPRECT PreviousScreenRect,
    BOOL WasVisible) {
    LPDESKTOP Desktop;
    LPWINDOW RootWindow;
    RECT CurrentScreenRect;
    BOOL IsVisible;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (PreviousScreenRect == NULL) return;
    if (DesktopRefreshWindowEffectiveVisibilityTree(Window) == FALSE) return;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return;

    CurrentScreenRect = Snapshot.ScreenRect;
    IsVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);

    if (IsVisible != FALSE && WasVisible == FALSE) {
        DesktopOverlayInvalidateWindowTreeRect(Window, &CurrentScreenRect, FALSE);
        return;
    }

    if (IsVisible == FALSE && WasVisible != FALSE) {
        Desktop = DesktopGetWindowDesktop(Window);
        if (Desktop != NULL && DesktopGetRootWindow(Desktop, &RootWindow) != FALSE && RootWindow != NULL) {
            DesktopOverlayInvalidateWindowTreeThenRootRect(RootWindow, PreviousScreenRect);
        }
        return;
    }

    if (IsVisible != FALSE) {
        (void)RequestWindowDraw((HANDLE)Window);
    }
}

/***************************************************************************/

/**
 * @brief Plot one pixel through generic software composition helpers.
 * @param Context Graphics context.
 * @param X X coordinate.
 * @param Y Y coordinate.
 * @param Color Pixel color.
 * @return TRUE on success.
 */
static BOOL DesktopPlotSoftwarePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color) {
    COLOR PreviousColor = 0;

    if (Context == NULL || Color == NULL) {
        return FALSE;
    }

    (void)GraphicsReadPixel((LPGRAPHICSCONTEXT)Context, X, Y, &PreviousColor);
    if (GraphicsWritePixel((LPGRAPHICSCONTEXT)Context, X, Y, *Color) == FALSE) {
        return FALSE;
    }

    *Color = PreviousColor;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set whether one window bypasses its parent work rectangle clamp.
 * @param Window Target window.
 * @param Enabled TRUE to bypass parent work rect clamping.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowBypassParentWorkRectState(LPWINDOW Window, BOOL Enabled) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Enabled != FALSE)
        Window->Status |= WINDOW_STATUS_BYPASS_PARENT_WORK_RECT;
    else
        Window->Status &= ~WINDOW_STATUS_BYPASS_PARENT_WORK_RECT;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build one window rectangle preserving current size at one position.
 * @param Window Target window.
 * @param Position New top-left position relative to parent.
 * @param Rect Receives full window rectangle in parent coordinates.
 * @return TRUE on success.
 */
BOOL BuildWindowRectAtPosition(LPWINDOW Window, LPPOINT Position, LPRECT Rect) {
    I32 Width;
    I32 Height;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Position == NULL || Rect == NULL) return FALSE;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    Width = Snapshot.Rect.X2 - Snapshot.Rect.X1 + 1;
    Height = Snapshot.Rect.Y2 - Snapshot.Rect.Y1 + 1;

    if (Width <= 0 || Height <= 0) return FALSE;

    Rect->X1 = Position->X;
    Rect->Y1 = Position->Y;
    Rect->X2 = Position->X + Width - 1;
    Rect->Y2 = Position->Y + Height - 1;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply default move/resize behavior and enqueue bounded damage.
 * @param Window Target window.
 * @param WindowRect New window rectangle relative to parent.
 * @return TRUE on success.
 */
BOOL DefaultSetWindowRect(LPWINDOW Window, LPRECT WindowRect) {
    LPWINDOW Parent;
    RECT OldScreenRect;
    RECT NewScreenRect;
    RECT FullWindowRect;
    RECT ParentOldRect;
    RECT ParentNewRect;
    RECT OldRect;
    RECT ParentScreenRect;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL) return FALSE;
    if (WindowRect->X1 > WindowRect->X2 || WindowRect->Y1 > WindowRect->Y2) return FALSE;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    Parent = Snapshot.ParentWindow;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;

    OldScreenRect = Snapshot.ScreenRect;
    OldRect = Snapshot.Rect;
    if (DesktopResolveWindowPlacementRect(Window, WindowRect) == FALSE) return FALSE;
    if (WindowRect->X1 == OldRect.X1 && WindowRect->Y1 == OldRect.Y1 && WindowRect->X2 == OldRect.X2 &&
        WindowRect->Y2 == OldRect.Y2) {
        return TRUE;
    }
    if (GetWindowScreenRectSnapshot(Parent, &ParentScreenRect) == FALSE) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Window->Rect = *WindowRect;
    GraphicsWindowRectToScreenRect(&ParentScreenRect, &(Window->Rect), &(Window->ScreenRect));
    NewScreenRect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));
    (void)DesktopRefreshWindowChildScreenRects(Window);

    FullWindowRect.X1 = 0;
    FullWindowRect.Y1 = 0;
    FullWindowRect.X2 = WindowRect->X2 - WindowRect->X1;
    FullWindowRect.Y2 = WindowRect->Y2 - WindowRect->Y1;

    if (ConvertScreenRectToWindowRect(Parent, &OldScreenRect, &ParentOldRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentOldRect);
    }

    InvalidateSiblingWindowsOnUncoveredRect(Window, Parent, &OldScreenRect);

    if (ConvertScreenRectToWindowRect(Parent, &NewScreenRect, &ParentNewRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentNewRect);
    }

    (void)InvalidateWindowRect((HANDLE)Window, &FullWindowRect);
    (void)PostMessage((HANDLE)Window, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, 0);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Broadcast one rectangle-change notify to all direct desktop children.
 * @param DesktopWindow Desktop root window.
 */
static void DesktopBroadcastRectChangedNotifyToDirectChildren(HANDLE DesktopWindow) {
    UINT ChildCount;
    UINT ChildIndex;
    HANDLE ChildWindow;

    if (DesktopWindow == NULL) return;

    ChildCount = GetWindowChildCount(DesktopWindow);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = GetWindowChild(DesktopWindow, ChildIndex);
        if (ChildWindow == NULL) continue;
        (void)PostMessage(ChildWindow, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, (U32)(UINT)(LINEAR)DesktopWindow);
    }
}

/***************************************************************************/

/**
 * @brief Set one graphics-context clip rectangle in screen coordinates.
 * @param GC Graphics context handle.
 * @param ClipRect Clip rectangle in screen coordinates.
 * @return TRUE on success.
 */
BOOL SetGraphicsContextClipScreenRect(HANDLE GC, LPRECT ClipRect) {
    LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)GC;
    RECT ClampedClip;
    I32 MaxX;
    I32 MaxY;

    if (Context == NULL || ClipRect == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (ClipRect->X1 > ClipRect->X2 || ClipRect->Y1 > ClipRect->Y2) return FALSE;

    MaxX = Context->Width - 1;
    MaxY = Context->Height - 1;
    if (MaxX < 0 || MaxY < 0) return FALSE;

    ClampedClip = *ClipRect;
    if (ClampedClip.X1 < 0) ClampedClip.X1 = 0;
    if (ClampedClip.Y1 < 0) ClampedClip.Y1 = 0;
    if (ClampedClip.X2 > MaxX) ClampedClip.X2 = MaxX;
    if (ClampedClip.Y2 > MaxY) ClampedClip.Y2 = MaxY;
    if (ClampedClip.X1 > ClampedClip.X2 || ClampedClip.Y1 > ClampedClip.Y2) return FALSE;

    LockMutex(&(Context->Mutex), INFINITY);
    Context->LoClip.X = ClampedClip.X1;
    Context->LoClip.Y = ClampedClip.Y1;
    Context->HiClip.X = ClampedClip.X2;
    Context->HiClip.Y = ClampedClip.Y2;
    UnlockMutex(&(Context->Mutex));

    return TRUE;
}

/**
 * @brief Build and consume one window clip region from accumulated dirty rectangles.
 * @param This Window whose dirty region is consumed.
 * @param ClipRegion Destination clip region.
 * @param ClipStorage Backing storage for destination clip region.
 * @param ClipCapacity Clip storage capacity.
 * @return TRUE on success.
 */
BOOL BuildWindowDrawClipRegion(
    LPWINDOW This,
    LPRECT_REGION ClipRegion,
    LPRECT ClipStorage,
    UINT ClipCapacity
) {
    RECT DirtyStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT VisibleStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DirtyRegion;
    RECT_REGION VisibleRegion;
    RECT DirtyRect;
    RECT VisibleRect;
    RECT WindowScreenRect;
    I32 ThisOrder;
    LPWINDOW ParentWindow = NULL;
    UINT DirtyCount;
    UINT DirtyIndex;
    UINT VisibleCount;
    UINT VisibleIndex;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRegion == NULL || ClipStorage == NULL || ClipCapacity == 0) return FALSE;
    if (RectRegionInit(ClipRegion, ClipStorage, ClipCapacity) == FALSE) return FALSE;
    RectRegionReset(ClipRegion);
    if (RectRegionInit(&DirtyRegion, DirtyStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) return FALSE;

    if (DesktopConsumeWindowDirtyRegionSnapshot(
            This, &DirtyRegion, DirtyStorage, WINDOW_DIRTY_REGION_CAPACITY, &WindowScreenRect, &ThisOrder, &ParentWindow) ==
        FALSE) {
        return FALSE;
    }
    UNUSED(ThisOrder);
    UNUSED(ParentWindow);

    DirtyCount = RectRegionGetCount(&DirtyRegion);
    for (DirtyIndex = 0; DirtyIndex < DirtyCount; DirtyIndex++) {
        if (RectRegionGetRect(&DirtyRegion, DirtyIndex, &DirtyRect) == FALSE) continue;
        if (DesktopBuildWindowVisibleRegion(
                This, &DirtyRect, TRUE, &VisibleRegion, VisibleStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
            RectRegionReset(ClipRegion);
            (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
            return TRUE;
        }

        VisibleCount = RectRegionGetCount(&VisibleRegion);
        for (VisibleIndex = 0; VisibleIndex < VisibleCount; VisibleIndex++) {
            if (RectRegionGetRect(&VisibleRegion, VisibleIndex, &VisibleRect) == FALSE) continue;
            if (RectRegionAddRect(ClipRegion, &VisibleRect) == FALSE) {
                RectRegionReset(ClipRegion);
                (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
                return TRUE;
            }
        }
    }

    if (RectRegionIsOverflowed(&DirtyRegion) || RectRegionIsOverflowed(ClipRegion)) {
        RectRegionReset(ClipRegion);
        (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
    }

    DesktopPipelineTraceRegion(This, ClipRegion);
    return TRUE;
}

/**
 * @brief Resolve shared brush and pen objects for a system color index.
 * @param Index SM_COLOR_* identifier.
 * @param Brush Receives brush object pointer.
 * @param Pen Receives pen object pointer.
 * @return TRUE when mapping exists.
 */
static BOOL ResolveSystemDrawObjects(U32 Index, LPBRUSH* Brush, LPPEN* Pen) {
    UINT EntryIndex;

    if (Brush == NULL || Pen == NULL) return FALSE;

    *Brush = NULL;
    *Pen = NULL;

    for (EntryIndex = 0; EntryIndex < (sizeof(SystemDrawObjects) / sizeof(SystemDrawObjects[0])); EntryIndex++) {
        if (SystemDrawObjects[EntryIndex].SystemColor == Index) {
            *Brush = SystemDrawObjects[EntryIndex].Brush;
            *Pen = SystemDrawObjects[EntryIndex].Pen;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Show or hide a window and its visible children.
 * @param Handle Window handle.
 * @param ShowHide TRUE to show, FALSE to hide.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowVisibility(HANDLE Handle, BOOL ShowHide) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT PreviousScreenRect;
    BOOL WasVisible = FALSE;
    WINDOW_STATE_SNAPSHOT Snapshot;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    PreviousScreenRect = Snapshot.ScreenRect;
    WasVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);

    //-------------------------------------
    // Send appropriate messages to the window

    (void)DesktopSetWindowVisibleState(This, ShowHide);
    (void)DesktopRevalidateSiblingPlacementConstraints(This);
    DesktopHandleWindowEffectiveVisibilityChange(This, &PreviousScreenRect, WasVisible);

    PostMessage(Handle, EWM_SHOW, 0, 0);

    return TRUE;
}

/***************************************************************************/

BOOL ShowWindow(HANDLE Handle) {
    return DesktopSetWindowVisibility(Handle, TRUE);
}

/***************************************************************************/

BOOL HideWindow(HANDLE Handle) {
    return DesktopSetWindowVisibility(Handle, FALSE);
}

/***************************************************************************/

BOOL SetWindowStyle(HANDLE Handle, U32 StyleMask) {
    return SetWindowStyleState(Handle, StyleMask, TRUE);
}

/***************************************************************************/

BOOL ClearWindowStyle(HANDLE Handle, U32 StyleMask) {
    return SetWindowStyleState(Handle, StyleMask, FALSE);
}

/***************************************************************************/

/**
 * @brief Obtain the size of a window in its own coordinates.
 * @param Handle Window handle.
 * @param Rect Destination rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowRect(HANDLE Handle, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Rect == NULL) return FALSE;

    //-------------------------------------
    // Lock access to the window

    LockMutex(&(This->Mutex), INFINITY);

    Rect->X1 = 0;
    Rect->Y1 = 0;
    Rect->X2 = This->Rect.X2 - This->Rect.X1;
    Rect->Y2 = This->Rect.Y2 - This->Rect.Y1;

    //-------------------------------------
    // Unlock access to the window

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Move and/or resize one window.
 * @param Handle Window handle.
 * @param Rect New window rectangle in parent coordinates.
 * @return TRUE on success.
 */
BOOL MoveWindow(HANDLE Handle, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Rect == NULL) return FALSE;

    return DefaultSetWindowRect(This, Rect);
}

/***************************************************************************/

/**
 * @brief Resize a window.
 * @param Handle Window handle.
 * @param Size New size.
 * @return TRUE on success.
 */
BOOL SizeWindow(HANDLE Handle, LPPOINT Size) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT NewRect;
    I32 Width;
    I32 Height;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Size == NULL) return FALSE;
    if (Size->X <= 0 || Size->Y <= 0) return FALSE;

    Width = Size->X;
    Height = Size->Y;

    LockMutex(&(This->Mutex), INFINITY);
    NewRect.X1 = This->Rect.X1;
    NewRect.Y1 = This->Rect.Y1;
    NewRect.X2 = NewRect.X1 + Width - 1;
    NewRect.Y2 = NewRect.Y1 + Height - 1;
    UnlockMutex(&(This->Mutex));

    return DefaultSetWindowRect(This, &NewRect);
}

/***************************************************************************/

/**
 * @brief Set or clear one masked style on one window.
 * @param Handle Window handle.
 * @param StyleMask Style bits to update.
 * @param Enabled TRUE to set bits, FALSE to clear bits.
 * @return TRUE on success.
 */
BOOL SetWindowStyleState(HANDLE Handle, U32 StyleMask, BOOL Enabled) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT PreviousScreenRect;
    BOOL WasVisible;
    BOOL Result;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (StyleMask == 0) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    PreviousScreenRect = Snapshot.ScreenRect;
    WasVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);

    Result = DesktopSetWindowStyleState(This, StyleMask, Enabled);
    if (Result == FALSE) return FALSE;

    if ((StyleMask & (EWS_EXCLUDE_SIBLING_PLACEMENT | EWS_VISIBLE)) != 0) {
        (void)DesktopRevalidateSiblingPlacementConstraints(This);
    }
    if ((StyleMask & EWS_VISIBLE) != 0) {
        DesktopHandleWindowEffectiveVisibilityChange(This, &PreviousScreenRect, WasVisible);
    }
    if ((StyleMask & (EWS_ALWAYS_IN_FRONT | EWS_ALWAYS_AT_BOTTOM)) != 0) {
        (void)DesktopRefreshWindowZOrder(This);
    }

    return TRUE;
}

/**
 * @brief Retrieve raw style bits from one window.
 * @param Handle Window handle.
 * @param Style Receives raw style bits.
 * @return TRUE on success.
 */
BOOL GetWindowStyle(HANDLE Handle, U32* Style) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (Style == NULL) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    *Style = Snapshot.Style;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one window caption string.
 * @param Handle Window handle.
 * @param Caption New caption text, or NULL for an empty caption.
 * @return TRUE on success.
 */
BOOL SetWindowCaption(HANDLE Handle, LPCSTR Caption) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (DesktopSetWindowCaption(This, Caption) == FALSE) return FALSE;

    return RequestWindowDraw(Handle);
}

/***************************************************************************/

/**
 * @brief Retrieve one window caption string.
 * @param Handle Window handle.
 * @param Caption Receives a null-terminated caption string.
 * @param CaptionLength Destination buffer length.
 * @return TRUE on success.
 */
BOOL GetWindowCaption(HANDLE Handle, LPSTR Caption, UINT CaptionLength) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (Caption == NULL || CaptionLength == 0) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    StringCopyLimit(Caption, Snapshot.Caption, CaptionLength);
    return TRUE;
}

/**
 * @brief Set a custom property on a window.
 * @param Handle Window handle.
 * @param Name Property name.
 * @param Value Property value.
 * @return Previous property value or 0.
 */
UINT SetWindowProp(HANDLE Handle, LPCSTR Name, UINT Value) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node;
    LPPROPERTY Prop;
    UINT OldValue = 0;
    BOOL HasChanged = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return 0;
    if (This->TypeID != KOID_WINDOW) return 0;
    if (Name == NULL || *Name == STR_NULL) return 0;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    for (Node = This->Properties->First; Node; Node = Node->Next) {
        Prop = (LPPROPERTY)Node;
        if (StringCompareNC(Prop->Name, Name) == 0) {
            OldValue = Prop->Value;
            if (OldValue != Value) {
                Prop->Value = Value;
                HasChanged = TRUE;
            }
            goto Out;
        }
    }

    //-------------------------------------
    // Add the property to the window

    Prop = (LPPROPERTY)KernelHeapAlloc(sizeof(PROPERTY));

    SAFE_USE(Prop) {
        StringCopy(Prop->Name, Name);
        Prop->Value = Value;
        ListAddItem(This->Properties, Prop);
        HasChanged = TRUE;
    }

Out:
    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    if (HasChanged != FALSE) {
        (void)PostMessage(Handle, EWM_NOTIFY, EWN_WINDOW_PROPERTY_CHANGED, 0);
    }

    return OldValue;
}

/***************************************************************************/

/**
 * @brief Retrieve a custom property from a window.
 * @param Handle Window handle.
 * @param Name Property name.
 * @return Property value or 0 if not found.
 */
UINT GetWindowProp(HANDLE Handle, LPCSTR Name) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node = NULL;
    LPPROPERTY Prop = NULL;
    UINT Value = 0;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Search the list of properties

    for (Node = This->Properties->First; Node; Node = Node->Next) {
        Prop = (LPPROPERTY)Node;
        if (StringCompareNC(Prop->Name, Name) == 0) {
            Value = Prop->Value;
            goto Out;
        }
    }

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return Value;
}

/***************************************************************************/

/**
 * @brief Resolve one graphics context for a window.
 * @param Window Target window.
 * @param UseScanoutContext TRUE to bypass the desktop shadow context.
 * @param ContextOut Receives the prepared context.
 * @return TRUE on success.
 */
BOOL DesktopGetWindowGraphicsContext(LPWINDOW This, BOOL UseScanoutContext, LPGRAPHICSCONTEXT* ContextOut) {
    LPDESKTOP Desktop;
    LPDRIVER GraphicsDriver;
    LPGRAPHICSCONTEXT Context = NULL;
    UINT ContextPointer;
    WINDOW_STATE_SNAPSHOT WindowSnapshot;
    WINDOW_DRAW_CONTEXT_SNAPSHOT DrawSnapshot;

    if (ContextOut == NULL) return FALSE;
    *ContextOut = NULL;
    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;

    Desktop = DesktopGetWindowDesktop(This);
    if (Desktop != NULL && Desktop->TypeID == KOID_DESKTOP &&
        Desktop->Mode == DESKTOP_MODE_GRAPHICS &&
        Desktop->GraphicsContext != NULL &&
        Desktop->GraphicsContext->TypeID == KOID_GRAPHICSCONTEXT &&
        Desktop->GraphicsContext->MemoryBase != NULL &&
        UseScanoutContext == FALSE) {
        Context = Desktop->GraphicsContext;
    }

    if (Context == NULL) {
        GraphicsDriver = GetGraphicsDriver();
        if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) return FALSE;

        ContextPointer = GraphicsDriver->Command(DF_GFX_GETCONTEXT, 0);
        if (ContextPointer == 0) return FALSE;

        Context = (LPGRAPHICSCONTEXT)(LPVOID)ContextPointer;
        if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    }

    ResetGraphicsContext(Context);
    if (GetWindowStateSnapshot(This, &WindowSnapshot) == FALSE) return FALSE;
    if (GetWindowDrawContextSnapshot(This, &DrawSnapshot) == FALSE) {
        MemorySet(&DrawSnapshot, 0, sizeof(DrawSnapshot));
    }

    //-------------------------------------
    // Set the origin of the context

    LockMutex(&(Context->Mutex), INFINITY);

    Context->Origin.X = WindowSnapshot.ScreenRect.X1;
    Context->Origin.Y = WindowSnapshot.ScreenRect.Y1;

    if ((DrawSnapshot.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) != 0) {
        Context->Origin.X = DrawSnapshot.Origin.X;
        Context->Origin.Y = DrawSnapshot.Origin.Y;
        Context->LoClip.X = DrawSnapshot.ClipRect.X1;
        Context->LoClip.Y = DrawSnapshot.ClipRect.Y1;
        Context->HiClip.X = DrawSnapshot.ClipRect.X2;
        Context->HiClip.Y = DrawSnapshot.ClipRect.Y2;
    }

    /*
      Context->LoClip.X = This->ScreenRect.X1;
      Context->LoClip.Y = This->ScreenRect.Y1;
      Context->HiClip.X = This->ScreenRect.X2;
      Context->HiClip.Y = This->ScreenRect.Y2;
    */

    UnlockMutex(&(Context->Mutex));

    *ContextOut = Context;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Obtain a graphics context for a window.
 * @param Handle Window handle.
 * @return Handle to a graphics context or NULL.
 */
HANDLE GetWindowGC(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPGRAPHICSCONTEXT Context = NULL;

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;
    if (DesktopGetWindowGraphicsContext(This, FALSE, &Context) == FALSE) return NULL;

    return (HANDLE)Context;
}

/***************************************************************************/

/**
 * @brief Release a previously obtained graphics context.
 * @param Handle Graphics context handle.
 * @return TRUE on success.
 */
BOOL ReleaseWindowGC(HANDLE Handle) {
    LPGRAPHICSCONTEXT This = (LPGRAPHICSCONTEXT)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepare a window for drawing and return its graphics context.
 * @param Handle Window handle.
 * @return Graphics context or NULL on failure.
 */
HANDLE BeginWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    HANDLE GC = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    GC = GetWindowGC(Handle);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return GC;
}

/***************************************************************************/

/**
 * @brief Finish drawing operations on a window.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL EndWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Present one screen rectangle from the desktop shadow buffer.
 * @param Window Any window on the target desktop.
 * @param ClipRect Screen-space rectangle to present.
 * @return TRUE on success.
 */
BOOL DesktopPresentScreenRect(LPWINDOW Window, LPRECT ClipRect) {
    LPDESKTOP Desktop;
    GFX_PRESENT_INFO PresentInfo;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRect == NULL) return FALSE;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) return TRUE;
    if (Desktop->Graphics == NULL || Desktop->Graphics->Command == NULL) return FALSE;
    if (Desktop->GraphicsContext == NULL || Desktop->GraphicsContext->TypeID != KOID_GRAPHICSCONTEXT ||
        Desktop->GraphicsContext->MemoryBase == NULL) return FALSE;

    PresentInfo = (GFX_PRESENT_INFO){
        .Header = {.Size = sizeof(GFX_PRESENT_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Desktop->GraphicsContext,
        .SurfaceId = 0,
        .DirtyRect = *ClipRect,
        .Flags = 0
    };

    return Desktop->Graphics->Command(DF_GFX_PRESENT, (UINT)(LPVOID)&PresentInfo) == DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Retrieve a system brush by index.
 * @param Index Brush identifier.
 * @return Handle to the brush.
 */
HANDLE GetSystemBrush(U32 Index) {
    LPBRUSH Brush;
    LPPEN Pen;
    COLOR Color;

    if (ResolveSystemDrawObjects(Index, &Brush, &Pen) == FALSE) return NULL;

    if (DesktopThemeResolveSystemColor(Index, &Color)) {
        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) { Brush->Color = Color; }
        SAFE_USE_VALID_ID(Pen, KOID_PEN) { Pen->Color = Color; }
    }

    return (HANDLE)Brush;
}

/***************************************************************************/

/**
 * @brief Retrieve a system pen by index.
 * @param Index Pen identifier.
 * @return Handle to the pen.
 */
HANDLE GetSystemPen(U32 Index) {
    LPBRUSH Brush;
    LPPEN Pen;
    COLOR Color;

    if (ResolveSystemDrawObjects(Index, &Brush, &Pen) == FALSE) return NULL;

    if (DesktopThemeResolveSystemColor(Index, &Color)) {
        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) { Brush->Color = Color; }
        SAFE_USE_VALID_ID(Pen, KOID_PEN) { Pen->Color = Color; }
    }

    return (HANDLE)Pen;
}

/***************************************************************************/

/**
 * @brief Select a brush into a graphics context.
 * @param GC Graphics context handle.
 * @param Brush Brush handle to select.
 * @return Previous brush handle.
 */
HANDLE SelectBrush(HANDLE GC, HANDLE Brush) {
    LPGRAPHICSCONTEXT Context;
    LPBRUSH NewBrush;
    LPBRUSH OldBrush;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewBrush = (LPBRUSH)Brush;

    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldBrush = Context->Brush;
    Context->Brush = NewBrush;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldBrush;
}

/***************************************************************************/

/**
 * @brief Select a pen into a graphics context.
 * @param GC Graphics context handle.
 * @param Pen Pen handle to select.
 * @return Previous pen handle.
 */
HANDLE SelectPen(HANDLE GC, HANDLE Pen) {
    LPGRAPHICSCONTEXT Context;
    LPPEN NewPen;
    LPPEN OldPen;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewPen = (LPPEN)Pen;

    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldPen = Context->Pen;
    Context->Pen = NewPen;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldPen;
}

/***************************************************************************/

/**
 * @brief Create a brush from brush information.
 * @param BrushInfo Brush parameters.
 * @return Handle to the created brush or NULL.
 */
HANDLE CreateBrush(LPBRUSH_INFO BrushInfo) {
    LPBRUSH Brush = NULL;

    if (BrushInfo == NULL) return NULL;

    Brush = (LPBRUSH)KernelHeapAlloc(sizeof(BRUSH));
    if (Brush == NULL) return NULL;

    MemorySet(Brush, 0, sizeof(BRUSH));

    Brush->TypeID = KOID_BRUSH;
    Brush->References = 1;
    Brush->OwnerProcess = GetCurrentProcess();
    Brush->Color = BrushInfo->Color;
    Brush->Pattern = BrushInfo->Pattern;

    return (HANDLE)Brush;
}

/***************************************************************************/

/**
 * @brief Create a pen from pen information.
 * @param PenInfo Pen parameters.
 * @return Handle to the created pen or NULL.
 */
HANDLE CreatePen(LPPEN_INFO PenInfo) {
    LPPEN Pen = NULL;

    if (PenInfo == NULL) return NULL;

    Pen = (LPPEN)KernelHeapAlloc(sizeof(PEN));
    if (Pen == NULL) return NULL;

    MemorySet(Pen, 0, sizeof(PEN));

    Pen->TypeID = KOID_PEN;
    Pen->References = 1;
    Pen->OwnerProcess = GetCurrentProcess();
    Pen->Color = PenInfo->Color;
    Pen->Pattern = PenInfo->Pattern;
    Pen->Width = PenInfo->Width != 0 ? PenInfo->Width : 1;

    return (HANDLE)Pen;
}

/***************************************************************************/

/**
 * @brief Set a pixel in a graphics context.
 * @param PixelInfo Pixel parameters.
 * @return TRUE on success.
 */
BOOL KernelSetPixel(LPPIXEL_INFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;
    PIXEL_INFO Pixel;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Pixel = *PixelInfo;
    Pixel.X = Context->Origin.X + Pixel.X;
    Pixel.Y = Context->Origin.Y + Pixel.Y;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        return GraphicsWritePixel(Context, Pixel.X, Pixel.Y, Pixel.Color);
    }

    Context->Driver->Command(DF_GFX_SETPIXEL, (UINT)&Pixel);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve a pixel from a graphics context.
 * @param PixelInfo Pixel parameters.
 * @return TRUE on success.
 */
BOOL KernelGetPixel(LPPIXEL_INFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;
    PIXEL_INFO Pixel;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Pixel = *PixelInfo;
    Pixel.X = Context->Origin.X + Pixel.X;
    Pixel.Y = Context->Origin.Y + Pixel.Y;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        COLOR PixelColor = 0;

        if (GraphicsReadPixel(Context, Pixel.X, Pixel.Y, &PixelColor) == FALSE) {
            return FALSE;
        }
        Pixel.Color = PixelColor;
    } else {
        Context->Driver->Command(DF_GFX_GETPIXEL, (UINT)&Pixel);
    }
    PixelInfo->Color = Pixel.Color;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a line using the current pen.
 * @param LineInfo Line parameters.
 * @return TRUE on success.
 */
BOOL Line(LPLINE_INFO LineInfo) {
    LPGRAPHICSCONTEXT Context;
    LINE_INFO Line;

    //-------------------------------------
    // Check validity of parameters

    if (LineInfo == NULL) return FALSE;
    if (LineInfo->Header.Size < sizeof(LINE_INFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)LineInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Line = *LineInfo;
    Line.X1 = Context->Origin.X + Line.X1;
    Line.Y1 = Context->Origin.Y + Line.Y1;
    Line.X2 = Context->Origin.X + Line.X2;
    Line.Y2 = Context->Origin.Y + Line.Y2;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        COLOR LineColor = 0;
        U32 Pattern = MAX_U32;
        U32 Width = 1;

        if (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN) {
            LineColor = Context->Pen->Color;
            Pattern = Context->Pen->Pattern;
            Width = Context->Pen->Width != 0 ? Context->Pen->Width : 1;
        }

        LineRasterizerDraw(Context, Line.X1, Line.Y1, Line.X2, Line.Y2, LineColor, Pattern, Width, DesktopPlotSoftwarePixel);
        return TRUE;
    }

    Context->Driver->Command(DF_GFX_LINE, (UINT)&Line);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a rectangle using current pen and brush.
 * @param RectInfo Rectangle parameters.
 * @return TRUE on success.
 */
BOOL KernelRectangle(LPRECT_INFO RectInfo) {
    LPGRAPHICSCONTEXT Context;
    RECT_INFO RectangleInfo;

    //-------------------------------------
    // Check validity of parameters

    if (RectInfo == NULL) return FALSE;
    if (RectInfo->Header.Size < sizeof(RECT_INFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)RectInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    RectangleInfo = *RectInfo;
    RectangleInfo.X1 = Context->Origin.X + RectangleInfo.X1;
    RectangleInfo.Y1 = Context->Origin.Y + RectangleInfo.Y1;
    RectangleInfo.X2 = Context->Origin.X + RectangleInfo.X2;
    RectangleInfo.Y2 = Context->Origin.Y + RectangleInfo.Y2;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        return GraphicsDrawRectangleFromDescriptor(Context, &RectangleInfo);
    }

    Context->Driver->Command(DF_GFX_RECTANGLE, (UINT)&RectangleInfo);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw an arc using current pen.
 * @param ArcInfo Arc parameters.
 * @return TRUE on success.
 */
BOOL Arc(LPARC_INFO ArcInfo) {
    LPGRAPHICSCONTEXT Context;
    ARC_INFO Arc;

    if (ArcInfo == NULL) return FALSE;
    if (ArcInfo->Header.Size < sizeof(ARC_INFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)ArcInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Arc = *ArcInfo;
    Arc.CenterX = Context->Origin.X + Arc.CenterX;
    Arc.CenterY = Context->Origin.Y + Arc.CenterY;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        return GraphicsDrawArcFromDescriptor(Context, &Arc);
    }

    Context->Driver->Command(DF_GFX_ARC, (UINT)&Arc);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a triangle using current pen and brush.
 * @param TriangleInfo Triangle parameters.
 * @return TRUE on success.
 */
BOOL Triangle(LPTRIANGLE_INFO TriangleInfo) {
    LPGRAPHICSCONTEXT Context;
    TRIANGLE_INFO Triangle;

    if (TriangleInfo == NULL) return FALSE;
    if (TriangleInfo->Header.Size < sizeof(TRIANGLE_INFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)TriangleInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Triangle = *TriangleInfo;
    Triangle.P1.X = Context->Origin.X + Triangle.P1.X;
    Triangle.P1.Y = Context->Origin.Y + Triangle.P1.Y;
    Triangle.P2.X = Context->Origin.X + Triangle.P2.X;
    Triangle.P2.Y = Context->Origin.Y + Triangle.P2.Y;
    Triangle.P3.X = Context->Origin.X + Triangle.P3.X;
    Triangle.P3.Y = Context->Origin.Y + Triangle.P3.Y;

    if ((Context->Flags & GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY) != 0) {
        return GraphicsDrawTriangleFromDescriptor(Context, &Triangle);
    }

    Context->Driver->Command(DF_GFX_TRIANGLE, (UINT)&Triangle);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Determine which window is under a given screen position.
 * @param Handle Starting window handle.
 * @param Position Screen coordinates to test.
 * @return Handle to the window or NULL.
 */
HANDLE WindowHitTest(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Target = NULL;
    LPWINDOW* Children = NULL;
    UINT ChildCount = 0;
    UINT ChildIndex;
    WINDOW_STATE_SNAPSHOT Snapshot;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) goto Out;
    if ((Snapshot.Status & WINDOW_STATUS_VISIBLE) == 0) goto Out;

    (void)DesktopSnapshotWindowChildren(This, &Children, &ChildCount);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Target = (LPWINDOW)WindowHitTest((HANDLE)Children[ChildIndex], Position);
        if (Target != NULL) goto Out;
    }

    //-------------------------------------
    // Test if this window passes hit test

    Target = NULL;

    if (Position->X >= Snapshot.ScreenRect.X1 && Position->X <= Snapshot.ScreenRect.X2 &&
        Position->Y >= Snapshot.ScreenRect.Y1 && Position->Y <= Snapshot.ScreenRect.Y2) {
        Target = This;
    }

Out:
    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    return (HANDLE)Target;
}

/***************************************************************************/

/**
 * @brief Copy one window screen rectangle under mutex.
 * @param Window Source window.
 * @param Rect Receives screen rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    *Rect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve current mouse position.
 * @param Point Receives current screen coordinates.
 * @return TRUE on success.
 */
BOOL GetMousePosition(LPPOINT Point) {
    I32 MouseX;
    I32 MouseY;

    if (Point == NULL) return FALSE;
    if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) return FALSE;

    Point->X = MouseX;
    Point->Y = MouseY;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Get desktop capture state for one window desktop.
 * @param Window Any window on the target desktop.
 * @param CaptureWindow Receives captured window (optional).
 * @param OffsetX Receives drag offset X (optional).
 * @param OffsetY Receives drag offset Y (optional).
 * @return TRUE on success.
 */
BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY) {
    LPDESKTOP Desktop;

    if (CaptureWindow != NULL) *CaptureWindow = NULL;
    if (OffsetX != NULL) *OffsetX = 0;
    if (OffsetY != NULL) *OffsetY = 0;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    if (CaptureWindow != NULL) *CaptureWindow = Desktop->Capture;
    if (OffsetX != NULL) *OffsetX = Desktop->CaptureOffsetX;
    if (OffsetY != NULL) *OffsetY = Desktop->CaptureOffsetY;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set desktop capture state for one window desktop.
 * @param Window Any window on the target desktop.
 * @param CaptureWindow Captured window or NULL.
 * @param OffsetX Drag offset X in captured window coordinates.
 * @param OffsetY Drag offset Y in captured window coordinates.
 * @return TRUE on success.
 */
BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY) {
    LPDESKTOP Desktop;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->Capture = CaptureWindow;
    Desktop->CaptureOffsetX = OffsetX;
    Desktop->CaptureOffsetY = OffsetY;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Capture mouse input for one window.
 * @param Window Target window handle.
 * @return Captured window handle on success, NULL on failure.
 */
HANDLE CaptureMouse(HANDLE Window) {
    LPWINDOW This = (LPWINDOW)Window;

    if (This == NULL || This->TypeID != KOID_WINDOW) return NULL;
    if (SetDesktopCaptureState(This, This, 0, 0) == FALSE) return NULL;

    return Window;
}

/***************************************************************************/

/**
 * @brief Release current mouse capture.
 * @return TRUE on success.
 */
BOOL ReleaseMouse(void) {
    LPDESKTOP ActiveDesktop;
    LPWINDOW CaptureWindow = NULL;

    ActiveDesktop = GetActiveDesktop();
    if (ActiveDesktop == NULL || ActiveDesktop->TypeID != KOID_DESKTOP) return FALSE;
    if (ActiveDesktop->Window == NULL || ActiveDesktop->Window->TypeID != KOID_WINDOW) return FALSE;
    if (GetDesktopCaptureState(ActiveDesktop->Window, &CaptureWindow, NULL, NULL) == FALSE) return FALSE;
    if (CaptureWindow == NULL || CaptureWindow->TypeID != KOID_WINDOW) return FALSE;

    return SetDesktopCaptureState(CaptureWindow, NULL, 0, 0);
}

/***************************************************************************/

/**
 * @brief Post one mouse move to one target window from one screen position.
 * @param Target Destination window.
 * @param ScreenPosition Mouse position in screen coordinates.
 * @return TRUE when the message was posted.
 */
static BOOL DesktopPostMouseMoveFromScreenPoint(LPWINDOW Target, LPPOINT ScreenPosition) {
    RECT WindowScreenRect;
    POINT LocalPosition;

    if (Target == NULL || Target->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenPosition == NULL) return FALSE;
    if (GetWindowScreenRectSnapshot(Target, &WindowScreenRect) == FALSE) return FALSE;

    GraphicsScreenPointToWindowPoint(&WindowScreenRect, ScreenPosition, &LocalPosition);
    return PostMessage((HANDLE)Target, EWM_MOUSEMOVE, UNSIGNED(LocalPosition.X), UNSIGNED(LocalPosition.Y));
}

/***************************************************************************/

/*
static U32 DrawMouseCursor(HANDLE GC, I32 X, I32 Y, BOOL OnOff) {
    LINE_INFO LineInfo;

    if (OnOff) {
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
    } else {
        SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    }

    LineInfo.GC = GC;

    LineInfo.X1 = X - 4;
    LineInfo.Y1 = Y;
    LineInfo.X2 = X + 4;
    LineInfo.Y2 = Y;
    Line(&LineInfo);

    LineInfo.X1 = X;
    LineInfo.Y1 = Y - 4;
    LineInfo.X2 = X;
    LineInfo.Y2 = Y + 4;
    Line(&LineInfo);

    return 0;
}
*/

/***************************************************************************/

/*
static U32 DrawButtons(HANDLE GC) {
    LINE_INFO LineInfo;
    U32 Buttons = GetMouseDriver().Command(DF_MOUSE_GETBUTTONS, 0);

    if (Buttons & MB_LEFT) {
        SelectPen(GC, GetSystemPen(SM_COLOR_TITLE_BAR_2));

        LineInfo.GC = GC;

        LineInfo.X1 = 10;
        LineInfo.Y1 = 0;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 0;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 1;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 1;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 2;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 2;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 3;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 3;
        Line(&LineInfo);
    }

    return 1;
}
*/

/***************************************************************************/

/**
 * @brief Window procedure for the desktop window.
 * @param Window Desktop window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 DesktopWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                DesktopBroadcastRectChangedNotifyToDirectChildren(Window);
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DRAW: {
            HANDLE GC;
            RECT Rect;

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowClientRect(Window, &Rect);
                (void)DrawWindowBackground(Window, GC, &Rect, THEME_TOKEN_WINDOW_BACKGROUND_DESKTOP);

                EndWindowDraw(Window);
            }
        } break;

        case EWM_MOUSEMOVE: {
            POINT Position;
            LPWINDOW Target;
            LPWINDOW PreviousTarget = NULL;
            LPWINDOW CaptureWindow = NULL;

            Position.X = SIGNED(Param1);
            Position.Y = SIGNED(Param2);

            if (GetDesktopCaptureState((LPWINDOW)Window, &CaptureWindow, NULL, NULL) != FALSE) {
                SAFE_USE_VALID_ID(CaptureWindow, KOID_WINDOW) {
                    if (CaptureWindow != (LPWINDOW)Window) {
                        (void)DesktopPostMouseMoveFromScreenPoint(CaptureWindow, &Position);
                        break;
                    }
                }
            }

            (void)GetDesktopLastMouseMoveTarget((LPWINDOW)Window, &PreviousTarget);
            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target == (LPWINDOW)Window) {
                Target = NULL;
            }

            if (PreviousTarget != NULL && PreviousTarget != Target) {
                SAFE_USE_VALID_ID(PreviousTarget, KOID_WINDOW) {
                    (void)DesktopPostMouseMoveFromScreenPoint(PreviousTarget, &Position);
                }
            }

            if (Target != NULL) {
                (void)DesktopPostMouseMoveFromScreenPoint(Target, &Position);
            }

            (void)SetDesktopLastMouseMoveTarget((LPWINDOW)Window, Target);
        } break;

        case EWM_MOUSEDOWN: {
            POINT Position;
            LPWINDOW Target;
            LPWINDOW CaptureWindow = NULL;
            I32 MouseX;
            I32 MouseY;

            if (GetDesktopCaptureState((LPWINDOW)Window, &CaptureWindow, NULL, NULL) != FALSE) {
                SAFE_USE_VALID_ID(CaptureWindow, KOID_WINDOW) {
                    if (CaptureWindow != (LPWINDOW)Window) {
                        (void)PostMessage((HANDLE)CaptureWindow, EWM_MOUSEDOWN, Param1, Param2);
                        break;
                    }
                }
            }

            if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) {
                break;
            }

            Position.X = MouseX;
            Position.Y = MouseY;
            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target != NULL && Target != (LPWINDOW)Window) {
                (void)DesktopSetFocusWindow(Target);
                (void)PostMessage((HANDLE)Target, EWM_MOUSEDOWN, Param1, Param2);
            }
        } break;

        case EWM_MOUSEUP: {
            POINT Position;
            LPWINDOW Target;
            LPWINDOW CaptureWindow = NULL;
            I32 MouseX;
            I32 MouseY;

            if (GetDesktopCaptureState((LPWINDOW)Window, &CaptureWindow, NULL, NULL) != FALSE) {
                SAFE_USE_VALID_ID(CaptureWindow, KOID_WINDOW) {
                    if (CaptureWindow != (LPWINDOW)Window) {
                        (void)PostMessage((HANDLE)CaptureWindow, EWM_MOUSEUP, Param1, Param2);
                        break;
                    }
                }
            }

            if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) {
                break;
            }

            Position.X = MouseX;
            Position.Y = MouseY;
            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target != NULL && Target != (LPWINDOW)Window) {
                (void)PostMessage((HANDLE)Target, EWM_MOUSEUP, Param1, Param2);
            }
        } break;

        default:
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}
