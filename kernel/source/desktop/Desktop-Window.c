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


    Desktop window lifecycle and invalidation

\************************************************************************/

#include "Desktop-Dispatcher.h"
#include "Desktop-NonClient.h"
#include "Desktop-OverlayInvalidation.h"
#include "Desktop-Private.h"
#include "Desktop-Timer.h"
#include "Desktop-WindowClass.h"
#include "DisplaySession.h"
#include "Desktop.h"
#include "core/Kernel.h"
#include "process/Process.h"
#include "process/Task-Messaging.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

typedef struct tag_Z_ORDER_CHILD_SNAPSHOT {
    LPWINDOW Window;
    I32 Order;
} Z_ORDER_CHILD_SNAPSHOT, *LPZ_ORDER_CHILD_SNAPSHOT;

/***************************************************************************/

/**
 * @brief Notify one window ancestry that one descendant was appended.
 * @param Window Newly attached descendant window.
 */
static void NotifyWindowChildAppended(LPWINDOW Window) {
    LPWINDOW Current;
    HANDLE ParentWindow;
    U32 ChildWindowID;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    ChildWindowID = Window->WindowID;
    Current = Window;
    FOREVER {
        ParentWindow = GetWindowParent((HANDLE)Current);
        if (ParentWindow == NULL) break;

        (void)PostMessage(ParentWindow, EWM_CHILD_APPENDED, ChildWindowID, 0);
        Current = (LPWINDOW)ParentWindow;
    }
}

/***************************************************************************/

/**
 * @brief Notify one window ancestry that one descendant was removed.
 * @param Parent Parent from which the child was detached.
 * @param ChildWindowID Removed child window identifier.
 */
static void NotifyWindowChildRemoved(LPWINDOW Parent, U32 ChildWindowID) {
    LPWINDOW Current;
    HANDLE ParentWindow;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;

    Current = Parent;
    FOREVER {
        (void)PostMessage((HANDLE)Current, EWM_CHILD_REMOVED, ChildWindowID, 0);

        ParentWindow = GetWindowParent((HANDLE)Current);
        if (ParentWindow == NULL) break;
        Current = (LPWINDOW)ParentWindow;
    }
}

/***************************************************************************/

/**
 * @brief Convert a window-relative rectangle to screen coordinates while the window is already locked.
 * @param This Locked window instance.
 * @param WindowRect Source rectangle in window coordinates.
 * @param ScreenRect Destination rectangle in screen coordinates.
 */
static void WindowRectToScreenRectLocked(LPWINDOW This, LPRECT WindowRect, LPRECT ScreenRect) {
    GraphicsWindowRectToScreenRect(&(This->ScreenRect), WindowRect, ScreenRect);
}

/***************************************************************************/

/**
 * @brief Ensure dirty region storage is initialized for one window.
 * @param This Target window.
 * @return TRUE on success.
 */
static BOOL EnsureWindowDirtyRegionInitialized(LPWINDOW This) {
    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (This->DirtyRegion.Storage == This->DirtyRects && This->DirtyRegion.Capacity == WINDOW_DIRTY_REGION_CAPACITY) {
        return TRUE;
    }

    return RectRegionInit(&This->DirtyRegion, This->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a new window structure.
 * @return Pointer to the created window or NULL on failure.
 */
static LPWINDOW NewWindow(void) {
    LPWINDOW This = (LPWINDOW)CreateKernelObject(sizeof(WINDOW), KOID_WINDOW);
    if (This == NULL) return NULL;

    InitMutexWithDebugInfo(&(This->Mutex), MUTEX_CLASS_WINDOW, TEXT("Window"));

    This->Properties = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    This->Children = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    (void)RectRegionInit(&This->DirtyRegion, This->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);

    return This;
}

/***************************************************************************/

/**
 * @brief Snapshot one parent child order list while the parent mutex is held.
 * @param Parent Parent window whose children are snapshotted.
 * @param Entries Receives allocated child snapshots.
 * @param Count Receives number of child snapshots.
 * @return TRUE on success.
 */
static BOOL SnapshotWindowChildOrderLocked(LPWINDOW Parent, LPZ_ORDER_CHILD_SNAPSHOT* Entries, UINT* Count) {
    LPLISTNODE Node;
    LPZ_ORDER_CHILD_SNAPSHOT Snapshot;
    UINT Capacity = 0;
    UINT Index = 0;

    if (Entries == NULL || Count == NULL) return FALSE;

    *Entries = NULL;
    *Count = 0;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Parent->Children == NULL) return TRUE;

    for (Node = Parent->Children->First; Node != NULL; Node = Node->Next) {
        Capacity++;
    }

    if (Capacity == 0) return TRUE;

    Snapshot = KernelHeapAlloc(sizeof(Z_ORDER_CHILD_SNAPSHOT) * Capacity);
    if (Snapshot == NULL) return FALSE;

    for (Node = Parent->Children->First; Node != NULL; Node = Node->Next) {
        LPWINDOW Child = (LPWINDOW)Node;

        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;

        Snapshot[Index].Window = Child;
        Snapshot[Index].Order = Child->Order;
        Index++;
    }

    *Entries = Snapshot;
    *Count = Index;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Invalidate one window subtree on the intersection with one screen rectangle.
 * @param Window Window whose subtree is invalidated.
 * @param ScreenRect Damage rectangle in screen coordinates.
 * @return TRUE when one intersection was invalidated.
 */
static BOOL InvalidateWindowTreeOnScreenIntersection(LPWINDOW Window, LPRECT ScreenRect) {
    RECT WindowScreenRect;
    RECT Intersection;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL) return FALSE;
    if (GetWindowScreenRectSnapshot(Window, &WindowScreenRect) == FALSE) return FALSE;
    if (IntersectRect(&WindowScreenRect, ScreenRect, &Intersection) == FALSE) return FALSE;

    DesktopOverlayInvalidateWindowTreeRect(Window, &Intersection, FALSE);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Invalidate direct transparent children intersecting one screen rectangle.
 * @param Window Parent window whose child list is inspected.
 * @param ScreenRect Damage rectangle in screen coordinates.
 */
/**
 * @brief Update one window screen rectangle and reset its dirty region to this rectangle.
 * @param Window Target window.
 * @param Rect New screen rectangle.
 * @return TRUE on success.
 */
BOOL DesktopUpdateWindowScreenRectAndDirtyRegion(LPWINDOW Window, LPRECT Rect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);

    Window->Rect = *Rect;
    Window->ScreenRect = *Rect;

    if (EnsureWindowDirtyRegionInitialized(Window) == FALSE) {
        UnlockMutex(&(Window->Mutex));
        return FALSE;
    }

    RectRegionReset(&Window->DirtyRegion);
    (void)RectRegionAddRect(&Window->DirtyRegion, Rect);

    UnlockMutex(&(Window->Mutex));
    (void)PostMessage((HANDLE)Window, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, 0);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Refresh direct and indirect child screen rectangles after one parent move.
 * @param ParentWindow Window whose descendants are refreshed.
 * @return TRUE on success.
 */
BOOL DesktopRefreshWindowChildScreenRects(LPWINDOW ParentWindow) {
    LPWINDOW* Children;
    LPWINDOW ChildWindow;
    RECT ParentScreenRect;
    RECT FullWindowRect;
    RECT ChildScreenRect;
    UINT ChildCount;
    UINT ChildIndex;

    if (ParentWindow == NULL || ParentWindow->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowScreenRectSnapshot(ParentWindow, &ParentScreenRect) == FALSE) return FALSE;

    Children = NULL;
    ChildCount = 0;
    if (DesktopSnapshotWindowChildren(ParentWindow, &Children, &ChildCount) == FALSE) return FALSE;

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = Children[ChildIndex];
        if (ChildWindow == NULL || ChildWindow->TypeID != KOID_WINDOW) continue;

        LockMutex(&(ChildWindow->Mutex), INFINITY);
        GraphicsWindowRectToScreenRect(&ParentScreenRect, &(ChildWindow->Rect), &(ChildWindow->ScreenRect));
        ChildScreenRect = ChildWindow->ScreenRect;
        UnlockMutex(&(ChildWindow->Mutex));

        FullWindowRect.X1 = 0;
        FullWindowRect.Y1 = 0;
        FullWindowRect.X2 = ChildScreenRect.X2 - ChildScreenRect.X1;
        FullWindowRect.Y2 = ChildScreenRect.Y2 - ChildScreenRect.Y1;
        (void)InvalidateWindowRect((HANDLE)ChildWindow, &FullWindowRect);
        (void)DesktopRefreshWindowChildScreenRects(ChildWindow);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Comparison routine for sorting windows by order.
 * @param Item1 First window pointer.
 * @param Item2 Second window pointer.
 * @return Difference of window orders.
 */
I32 SortWindows_Order(LPCVOID Item1, LPCVOID Item2) {
    LPWINDOW* Ptr1 = (LPWINDOW*)Item1;
    LPWINDOW* Ptr2 = (LPWINDOW*)Item2;
    LPWINDOW Win1 = *Ptr1;
    LPWINDOW Win2 = *Ptr2;

    return (Win1->Order - Win2->Order);
}

/***************************************************************************/

/**
 * @brief Delete a window and its children.
 * @param This Window to delete.
 */
BOOL DesktopDeleteWindow(LPWINDOW This) {
    LPPROCESS Process;
    LPTASK Task;
    LPDESKTOP Desktop;
    LPWINDOW ParentWindow;
    LPWINDOW ChildWindow;
    U32 ChildWindowID;

    //-------------------------------------
    // Check validity of parameters

    if (This->TypeID != KOID_WINDOW) return FALSE;
    if (This->ParentWindow == NULL) return FALSE;

    Task = This->Task;
    if (Task == NULL) return FALSE;
    Process = Task->OwnerProcess;
    if (Process == NULL) return FALSE;
    Desktop = Process->Desktop;
    if (Desktop == NULL) return FALSE;

    //-------------------------------------
    // Release desktop related resources

    DesktopTimerRemoveWindowTimers(Desktop, This);
    (void)DesktopClearWindowReferences(Desktop, This);
    (void)SendMessage((HANDLE)This, EWM_DELETE, 0, 0);

    LockMutex(&(This->Mutex), INFINITY);
    ParentWindow = This->ParentWindow;
    ChildWindowID = This->WindowID;
    UnlockMutex(&(This->Mutex));

    FOREVER {
        ChildWindow = (LPWINDOW)GetWindowChild((HANDLE)This, 0);
        if (ChildWindow == NULL || ChildWindow->TypeID != KOID_WINDOW) break;
        DesktopDeleteWindow(ChildWindow);
    }

    LockMutex(&(This->Mutex), INFINITY);

    if (This->ClassData != NULL) {
        KernelHeapFree(This->ClassData);
        This->ClassData = NULL;
    }
    UnlockMutex(&(This->Mutex));

    (void)DesktopDetachWindowChild(ParentWindow, This);
    NotifyWindowChildRemoved(ParentWindow, ChildWindowID);

    ReleaseKernelObject(This);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Search recursively for a window starting from another window.
 * @param Start Window to start searching from.
 * @param WindowID Window identifier to find.
 * @return Pointer to the found window or NULL.
 */
LPWINDOW DesktopFindWindow(LPWINDOW Start, U32 WindowID) {
    LPWINDOW Current = NULL;
    LPWINDOW Child = NULL;
    UINT ChildCount;
    UINT Index;

    if (Start == NULL) return NULL;
    if (Start->TypeID != KOID_WINDOW) return NULL;

    if (Start->WindowID == WindowID) return Start;

    ChildCount = GetWindowChildCount((HANDLE)Start);
    for (Index = 0; Index < ChildCount; Index++) {
        Child = (LPWINDOW)GetWindowChild((HANDLE)Start, Index);
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        Current = DesktopFindWindow(Child, WindowID);
        if (Current != NULL) return Current;
    }

    return Current;
}

/***************************************************************************/

/**
 * @brief Check whether one concrete window handle belongs to one subtree.
 * @param Start Window to start searching from.
 * @param Target Window handle to find.
 * @return Target window pointer when it belongs to the subtree, otherwise NULL.
 */
LPWINDOW DesktopContainsWindow(LPWINDOW Start, LPWINDOW Target) {
    LPWINDOW Current = NULL;
    LPWINDOW Child = NULL;
    UINT ChildCount;
    UINT Index;

    if (Start == NULL) return NULL;
    if (Start->TypeID != KOID_WINDOW) return NULL;

    if (Target == NULL) return NULL;
    if (Target->TypeID != KOID_WINDOW) return NULL;

    if (Start == Target) return Start;

    ChildCount = GetWindowChildCount((HANDLE)Start);
    for (Index = 0; Index < ChildCount; Index++) {
        Child = (LPWINDOW)GetWindowChild((HANDLE)Start, Index);
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        Current = DesktopContainsWindow(Child, Target);
        if (Current != NULL) return Current;
    }

    return Current;
}

/***************************************************************************/

/**
 * @brief Create a window based on provided window information.
 * @param Info Structure describing the window to create.
 * @return Pointer to the created window or NULL on failure.
 */
LPWINDOW DesktopCreateWindow(LPWINDOW_INFO Info) {
    LPWINDOW This;
    LPWINDOW Parent;
    LPDESKTOP Desktop;
    LPTASK OwnerTask;
    RECT InitialRect;
    RECT ParentScreenRect;
    U32 ParentLevel;

    //-------------------------------------
    // Check validity of parameters

    if (Info == NULL) return NULL;

    //-------------------------------------
    // Get the desktop of the current process

    Desktop = GetCurrentProcess()->Desktop;

    //-------------------------------------
    // Check that parent is a valid window
    // and that it belongs to the current desktop

    Parent = (LPWINDOW)Info->Parent;

    SAFE_USE(Parent) {
        if (Parent->TypeID != KOID_WINDOW) return NULL;
    }

    This = NewWindow();

    if (This == NULL) return NULL;

    OwnerTask = DesktopResolveWindowTask(Desktop, GetCurrentTask());
    This->Task = OwnerTask;
    SAFE_USE_VALID_ID(OwnerTask, KOID_TASK) { This->OwnerProcess = OwnerTask->OwnerProcess; }
    This->ParentWindow = Parent;
    This->Function = Info->Function;
    This->WindowID = Info->ID;
    This->Style = Info->Style;
    This->ContentTransparencyHint = WINDOW_CONTENT_TRANSPARENCY_HINT_AUTO;

    if (WindowClassInitializeRegistry() == FALSE) {
        ReleaseKernelObject(This);
        return NULL;
    }

    if (Info->WindowClass != 0) {
        This->Class = WindowClassFindByHandle((U32)Info->WindowClass);
    } else if (Info->WindowClassName != NULL) {
        This->Class = WindowClassFindByName(Info->WindowClassName);
    } else {
        This->Class = WindowClassGetDefault();
    }
    if (This->Class == NULL) {
        ReleaseKernelObject(This);
        return NULL;
    }

    if (This->Class->ClassDataSize > 0) {
        This->ClassData = KernelHeapAlloc(This->Class->ClassDataSize);
        if (This->ClassData == NULL) {
            ReleaseKernelObject(This);
            return NULL;
        }
        MemorySet(This->ClassData, 0, This->Class->ClassDataSize);
    }

    if (This->Function == NULL) {
        This->Function = This->Class->Function;
    }

    if (EnsureAllMessageQueues(OwnerTask, TRUE) == FALSE) {
        if (This->ClassData != NULL) {
            KernelHeapFree(This->ClassData);
            This->ClassData = NULL;
        }
        ReleaseKernelObject(This);
        return NULL;
    }
    if (This->ParentWindow == NULL) {
        SAFE_USE(Desktop) {
            if (Desktop->Window == NULL) {
                Desktop->Window = This;
            } else {
                This->ParentWindow = Desktop->Window;
            }
        }
    }

    InitialRect.X1 = Info->WindowPosition.X;
    InitialRect.Y1 = Info->WindowPosition.Y;
    InitialRect.X2 = Info->WindowPosition.X + (Info->WindowSize.X - 1);
    InitialRect.Y2 = Info->WindowPosition.Y + (Info->WindowSize.Y - 1);

    if (DesktopResolveWindowPlacementRect(This, &InitialRect) == FALSE) {
        if (This->ClassData != NULL) {
            KernelHeapFree(This->ClassData);
            This->ClassData = NULL;
        }
        ReleaseKernelObject(This);
        return NULL;
    }

    This->Rect = InitialRect;
    This->ScreenRect = This->Rect;
    RectRegionReset(&This->DirtyRegion);
    (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);

    SAFE_USE(This->ParentWindow) {
        if (GetWindowScreenRectSnapshot(This->ParentWindow, &ParentScreenRect) != FALSE) {
            GraphicsWindowRectToScreenRect(&ParentScreenRect, &(This->Rect), &(This->ScreenRect));
            RectRegionReset(&This->DirtyRegion);
            (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);
        }

        ParentLevel = 0;
        (void)GetWindowLevelSnapshot(This->ParentWindow, &ParentLevel);
        This->Level = ParentLevel + 1;
        (void)DesktopAttachWindowChild(This->ParentWindow, This);
    }

    NotifyWindowChildAppended(This);

    //-------------------------------------
    // Tell the window it is being created

    PostMessage((HANDLE)This, EWM_CREATE, 0, 0);

    //-------------------------------------
    // Ensure the freshly created window gets a full local draw request

    {
        RECT FullWindowRect;

        FullWindowRect.X1 = 0;
        FullWindowRect.Y1 = 0;
        FullWindowRect.X2 = This->Rect.X2 - This->Rect.X1;
        FullWindowRect.Y2 = This->Rect.Y2 - This->Rect.Y1;
        InvalidateWindowRect((HANDLE)This, &FullWindowRect);
    }

    if (Info->ShowHide != FALSE || (This->Style & EWS_VISIBLE) != 0) {
        (void)ShowWindow((HANDLE)This);
    }

    return This;
}

/***************************************************************************/

/**
 * @brief Retrieve the desktop owning a given window.
 * @param This Window whose desktop is requested.
 * @return Pointer to the desktop or NULL.
 */
LPDESKTOP DesktopGetWindowDesktop(LPWINDOW This) {
    LPPROCESS Process = NULL;
    LPTASK Task = NULL;
    LPDESKTOP Desktop = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Task = This->Task;
    if (Task != NULL && Task->TypeID == KOID_TASK) {
        Process = Task->OwnerProcess;

        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            Desktop = Process->Desktop;

            SAFE_USE(Desktop) {
                if (Desktop->TypeID != KOID_DESKTOP) Desktop = NULL;
            }
        }
    }

    UnlockMutex(&(This->Mutex));

    return Desktop;
}

/***************************************************************************/

HANDLE CreateWindow(LPWINDOW_INFO Info) {
    return (HANDLE)DesktopCreateWindow(Info);
}

/***************************************************************************/

BOOL DeleteWindow(HANDLE Window) {
    return DesktopDeleteWindow((LPWINDOW)Window);
}

/***************************************************************************/

HANDLE FindWindow(HANDLE StartWindow, U32 WindowID) {
    return (HANDLE)DesktopFindWindow((LPWINDOW)StartWindow, WindowID);
}

/***************************************************************************/

HANDLE ContainsWindow(HANDLE StartWindow, HANDLE TargetWindow) {
    return (HANDLE)DesktopContainsWindow((LPWINDOW)StartWindow, (LPWINDOW)TargetWindow);
}

/***************************************************************************/

HANDLE GetWindowDesktop(HANDLE Window) {
    return (HANDLE)DesktopGetWindowDesktop((LPWINDOW)Window);
}

/***************************************************************************/

HANDLE GetDesktopWindow(HANDLE DesktopHandle) {
    LPDESKTOP Desktop;
    LPWINDOW RootWindow;

    Desktop = (LPDESKTOP)DesktopHandle;
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;
    if (DesktopGetRootWindow(Desktop, &RootWindow) == FALSE) return NULL;

    return (HANDLE)RootWindow;
}

/***************************************************************************/

/*
static BOOL ComputeWindowRegions(LPWINDOW This) {
    UNUSED(This);

    return FALSE;
}
*/

/***************************************************************************/

/**
 * @brief Convert a window-relative rectangle to screen coordinates.
 * @param Handle Window handle.
 * @param WindowRect Source rectangle in window coordinates.
 * @param ScreenRect Destination rectangle in screen coordinates.
 * @return TRUE on success.
 */
BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT WindowRect, LPRECT ScreenRect) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (WindowRect == NULL) return FALSE;
    if (ScreenRect == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    WindowRectToScreenRectLocked(This, WindowRect, ScreenRect);

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Convert a screen rectangle to window-relative coordinates.
 * @param Handle Window handle.
 * @param ScreenRect Source screen rectangle.
 * @param WindowRect Destination window rectangle.
 * @return TRUE on success.
 */
BOOL ScreenRectToWindowRect(HANDLE Handle, LPRECT ScreenRect, LPRECT WindowRect) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (ScreenRect == NULL) return FALSE;
    if (WindowRect == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    GraphicsScreenRectToWindowRect(&(This->ScreenRect), ScreenRect, WindowRect);

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Convert one screen point to one window-relative point.
 * @param Handle Window handle.
 * @param ScreenPoint Source screen point.
 * @param WindowPoint Destination window-relative point.
 * @return TRUE on success.
 */
BOOL ScreenPointToWindowPoint(HANDLE Handle, LPPOINT ScreenPoint, LPPOINT WindowPoint) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (ScreenPoint == NULL) return FALSE;
    if (WindowPoint == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);
    GraphicsScreenPointToWindowPoint(&(This->ScreenRect), ScreenPoint, WindowPoint);
    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Add one client rectangle to a window dirty region.
 * @param Handle Window handle.
 * @param Src Client rectangle in client coordinates, or NULL for full client area.
 * @return TRUE on success.
 */
BOOL InvalidateClientRect(HANDLE Handle, LPRECT Src) {
    RECT ClientWindowRect;
    RECT WindowRect;

    if (GetWindowClientRect(Handle, &ClientWindowRect) == FALSE) return FALSE;

    if (Src == NULL) {
        return InvalidateWindowRect(Handle, &ClientWindowRect);
    }

    WindowRect.X1 = ClientWindowRect.X1 + Src->X1;
    WindowRect.Y1 = ClientWindowRect.Y1 + Src->Y1;
    WindowRect.X2 = ClientWindowRect.X1 + Src->X2;
    WindowRect.Y2 = ClientWindowRect.Y1 + Src->Y2;

    return InvalidateWindowRect(Handle, &WindowRect);
}

/***************************************************************************/

/**
 * @brief Add a rectangle to a window's invalid region.
 * @param Handle Window handle.
 * @param Src Rectangle to invalidate.
 * @return TRUE on success.
 */
BOOL InvalidateWindowRect(HANDLE Handle, LPRECT Src) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT Rect;
    RECT WindowRect;
    BOOL IsVisible = FALSE;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    IsVisible = ((This->Status & WINDOW_STATUS_VISIBLE) != 0);
    if (IsVisible == FALSE) {
        UnlockMutex(&(This->Mutex));
        return TRUE;
    }

    if (EnsureWindowDirtyRegionInitialized(This) == FALSE) {
        UnlockMutex(&(This->Mutex));
        return FALSE;
    }

    SAFE_USE(Src) {
        WindowRectToScreenRectLocked(This, Src, &Rect);
        (void)RectRegionAddRect(&This->DirtyRegion, &Rect);
    }
    else {
        // Damage tracking uses the full window surface. Client/non-client split
        // is resolved later during draw dispatch, not during invalidation.
        WindowRect.X1 = 0;
        WindowRect.Y1 = 0;
        WindowRect.X2 = This->Rect.X2 - This->Rect.X1;
        WindowRect.Y2 = This->Rect.Y2 - This->Rect.Y1;
        WindowRectToScreenRectLocked(This, &WindowRect, &Rect);
        RectRegionReset(&This->DirtyRegion);
        (void)RectRegionAddRect(&This->DirtyRegion, &Rect);
    }

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    if (This->WindowID == 0x53484252) {
    }

    return RequestWindowDraw(Handle);
}

/***************************************************************************/

/**
 * @brief Request one coalesced draw message for a window.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL RequestWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    BOOL ShouldPost = FALSE;
    BOOL IsVisible = FALSE;
    U32 FrontEnd = DisplaySessionGetActiveFrontEnd();

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (FrontEnd != DISPLAY_FRONTEND_DESKTOP) {
        LockMutex(&(This->Mutex), INFINITY);
        This->Status &= ~WINDOW_STATUS_NEED_DRAW;
        UnlockMutex(&(This->Mutex));
        return TRUE;
    }

    LockMutex(&(This->Mutex), INFINITY);

    IsVisible = ((This->Status & WINDOW_STATUS_VISIBLE) != 0);
    if (IsVisible == FALSE) {
        This->Status &= ~WINDOW_STATUS_NEED_DRAW;
        UnlockMutex(&(This->Mutex));
        return TRUE;
    }

    if ((This->Status & WINDOW_STATUS_NEED_DRAW) == 0) {
        This->Status |= WINDOW_STATUS_NEED_DRAW;
        ShouldPost = TRUE;
    }

    UnlockMutex(&(This->Mutex));

    if (ShouldPost) {
        PostMessage(Handle, EWM_DRAW, 0, 0);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Raise a window to the front of the Z order.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL BringWindowToFront(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW That;
    LPWINDOW Parent;
    LPZ_ORDER_CHILD_SNAPSHOT AffectedWindows = NULL;
    UINT AffectedWindowCount = 0;
    UINT Index;
    LPLISTNODE Node;
    I32 Order;
    I32 OldOrder;
    RECT DamageScreenRect;
    BOOL IsChild = FALSE;
    BOOL Reordered = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;
    if (This->ParentWindow == NULL) return FALSE;

    Parent = This->ParentWindow;
    if (Parent->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowOrderSnapshot(This, &OldOrder) == FALSE) return FALSE;
    if (OldOrder == 0) return TRUE;
    if (GetWindowScreenRectSnapshot(This, &DamageScreenRect) == FALSE) return FALSE;

    LockMutex(&(Parent->Mutex), INFINITY);

    if (SnapshotWindowChildOrderLocked(Parent, &AffectedWindows, &AffectedWindowCount) == FALSE) {
        UnlockMutex(&(Parent->Mutex));
        return FALSE;
    }

    for (Node = Parent->Children->First, Order = 1; Node; Node = Node->Next) {
        That = (LPWINDOW)Node;
        if (That == This) {
            IsChild = TRUE;
            That->Order = 0;
        } else {
            That->Order = Order++;
        }
    }

    if (IsChild != FALSE) {
        ListSort(Parent->Children, SortWindows_Order);
        Reordered = TRUE;
    }

    UnlockMutex(&(Parent->Mutex));

    if (Reordered == FALSE) {
        if (AffectedWindows != NULL) {
            KernelHeapFree(AffectedWindows);
        }
        return FALSE;
    }

    for (Index = 0; Index < AffectedWindowCount; Index++) {
        LPWINDOW Window = AffectedWindows[Index].Window;

        if (Window == NULL || Window->TypeID != KOID_WINDOW) continue;

        if (Window == This) {
            (void)InvalidateWindowTreeOnScreenIntersection(Window, &DamageScreenRect);
            continue;
        }

        if (AffectedWindows[Index].Order >= OldOrder) continue;
        (void)InvalidateWindowTreeOnScreenIntersection(Window, &DamageScreenRect);
    }

    if (AffectedWindows != NULL) {
        KernelHeapFree(AffectedWindows);
    }

    return TRUE;
}

/***************************************************************************/
