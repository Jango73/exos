/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime Window Functions

\************************************************************************/

#include "exos/exos-adaptive-delay.h"
#include "exos/exos-runtime-main.h"
#include "exos/exos-runtime-string.h"
#include "exos/exos.h"

/***************************************************************************/

HANDLE CreateDesktop(void) { return (HANDLE)exoscall(SYSCALL_CreateDesktop, EXOS_PARAM(0)); }

/***************************************************************************/

BOOL ShowDesktop(HANDLE Desktop) { return (BOOL)exoscall(SYSCALL_ShowDesktop, EXOS_PARAM(Desktop)); }

/***************************************************************************/

HANDLE GetDesktopWindow(HANDLE Desktop) { return (HANDLE)exoscall(SYSCALL_GetDesktopWindow, EXOS_PARAM(Desktop)); }

/***************************************************************************/

HANDLE GetCurrentDesktop(void) { return (HANDLE)exoscall(SYSCALL_GetCurrentDesktop, EXOS_PARAM(0)); }

/***************************************************************************/

BOOL ApplyDesktopTheme(LPCSTR Target) {
    DESKTOP_THEME_INFO ApplyInfo;

    ApplyInfo.Header.Size = sizeof ApplyInfo;
    ApplyInfo.Header.Version = EXOS_ABI_VERSION;
    ApplyInfo.Header.Flags = 0;
    ApplyInfo.Target = Target;

    return (BOOL)exoscall(SYSCALL_ApplyDesktopTheme, EXOS_PARAM(&ApplyInfo));
}

/***************************************************************************/

HANDLE RegisterWindowClass(
    LPCSTR ClassName, HANDLE BaseClass, LPCSTR BaseClassName, WINDOWFUNC Function, U32 ClassDataSize) {
    WINDOW_CLASS_INFO ClassInfo;

    ClassInfo.Header.Size = sizeof ClassInfo;
    ClassInfo.Header.Version = EXOS_ABI_VERSION;
    ClassInfo.Header.Flags = 0;
    ClassInfo.WindowClass = 0;
    ClassInfo.BaseClass = BaseClass;
    ClassInfo.ClassName = ClassName;
    ClassInfo.BaseClassName = BaseClassName;
    ClassInfo.Function = Function;
    ClassInfo.ClassDataSize = ClassDataSize;

    return (HANDLE)exoscall(SYSCALL_RegisterWindowClass, EXOS_PARAM(&ClassInfo));
}

/***************************************************************************/

BOOL UnregisterWindowClass(HANDLE WindowClass, LPCSTR ClassName) {
    WINDOW_CLASS_INFO ClassInfo;

    ClassInfo.Header.Size = sizeof ClassInfo;
    ClassInfo.Header.Version = EXOS_ABI_VERSION;
    ClassInfo.Header.Flags = 0;
    ClassInfo.WindowClass = WindowClass;
    ClassInfo.BaseClass = 0;
    ClassInfo.ClassName = ClassName;
    ClassInfo.BaseClassName = NULL;
    ClassInfo.Function = NULL;
    ClassInfo.ClassDataSize = 0;

    return (BOOL)exoscall(SYSCALL_UnregisterWindowClass, EXOS_PARAM(&ClassInfo));
}

/***************************************************************************/

HANDLE FindWindowClass(LPCSTR ClassName) {
    WINDOW_CLASS_INFO ClassInfo;

    ClassInfo.Header.Size = sizeof ClassInfo;
    ClassInfo.Header.Version = EXOS_ABI_VERSION;
    ClassInfo.Header.Flags = 0;
    ClassInfo.WindowClass = 0;
    ClassInfo.BaseClass = 0;
    ClassInfo.ClassName = ClassName;
    ClassInfo.BaseClassName = NULL;
    ClassInfo.Function = NULL;
    ClassInfo.ClassDataSize = 0;

    return (HANDLE)exoscall(SYSCALL_FindWindowClass, EXOS_PARAM(&ClassInfo));
}

/***************************************************************************/

BOOL WindowInheritsClass(HANDLE Window, HANDLE WindowClass, LPCSTR ClassName) {
    WINDOW_CLASS_QUERY_INFO QueryInfo;

    QueryInfo.Header.Size = sizeof QueryInfo;
    QueryInfo.Header.Version = EXOS_ABI_VERSION;
    QueryInfo.Header.Flags = 0;
    QueryInfo.Window = Window;
    QueryInfo.WindowClass = WindowClass;
    QueryInfo.ClassName = ClassName;

    return (BOOL)exoscall(SYSCALL_WindowInheritsClass, EXOS_PARAM(&QueryInfo));
}

/***************************************************************************/

HANDLE CreateWindowWithClass(
    HANDLE Parent, HANDLE WindowClass, LPCSTR WindowClassName, WINDOWFUNC Func, U32 Style, U32 ID, I32 PosX, I32 PosY,
    I32 SizeX, I32 SizeY) {
    WINDOW_INFO WindowInfo;

    WindowInfo.Header.Size = sizeof WindowInfo;
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = 0;
    WindowInfo.Parent = Parent;
    WindowInfo.WindowClass = WindowClass;
    WindowInfo.WindowClassName = WindowClassName;
    WindowInfo.Function = Func;
    WindowInfo.Style = Style;
    WindowInfo.ID = ID;
    WindowInfo.WindowPosition.X = PosX;
    WindowInfo.WindowPosition.Y = PosY;
    WindowInfo.WindowSize.X = SizeX;
    WindowInfo.WindowSize.Y = SizeY;
    WindowInfo.ShowHide = FALSE;

    return (HANDLE)exoscall(SYSCALL_CreateWindow, EXOS_PARAM(&WindowInfo));
}

/***************************************************************************/

HANDLE CreateWindow(LPWINDOW_INFO WindowInfo) { return (HANDLE)exoscall(SYSCALL_CreateWindow, EXOS_PARAM(WindowInfo)); }

/***************************************************************************/

BOOL DestroyWindow(HANDLE Window) { return (BOOL)exoscall(SYSCALL_DeleteObject, EXOS_PARAM(Window)); }

/***************************************************************************/

BOOL ShowWindow(HANDLE Window) {
    WINDOW_INFO WindowInfo;

    WindowInfo.Header.Size = sizeof WindowInfo;
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = Window;

    return (BOOL)exoscall(SYSCALL_ShowWindow, EXOS_PARAM(&WindowInfo));
}

/***************************************************************************/

BOOL HideWindow(HANDLE Window) {
    WINDOW_INFO WindowInfo;

    WindowInfo.Header.Size = sizeof WindowInfo;
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = Window;

    return (BOOL)exoscall(SYSCALL_HideWindow, EXOS_PARAM(&WindowInfo));
}

/***************************************************************************/

BOOL SetWindowStyle(HANDLE Window, U32 Style) {
    WINDOW_INFO WindowInfo;

    WindowInfo.Header.Size = sizeof WindowInfo;
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = Window;
    WindowInfo.Style = Style;
    WindowInfo.ShowHide = TRUE;

    return (BOOL)exoscall(SYSCALL_SetWindowStyle, EXOS_PARAM(&WindowInfo));
}

/***************************************************************************/

BOOL ClearWindowStyle(HANDLE Window, U32 Style) {
    WINDOW_INFO WindowInfo;

    WindowInfo.Header.Size = sizeof WindowInfo;
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = Window;
    WindowInfo.Style = Style;

    return (BOOL)exoscall(SYSCALL_ClearWindowStyle, EXOS_PARAM(&WindowInfo));
}

/***************************************************************************/

BOOL InvalidateWindowRect(HANDLE Window, LPRECT Rect) {
    WINDOW_RECT WindowRect;

    WindowRect.Header.Size = sizeof WindowRect;
    WindowRect.Header.Version = EXOS_ABI_VERSION;
    WindowRect.Header.Flags = (Rect == NULL) ? WINDOW_RECT_FLAG_ALL : 0;
    WindowRect.Window = Window;

    if (Rect != NULL) {
        WindowRect.Rect.X1 = Rect->X1;
        WindowRect.Rect.Y1 = Rect->Y1;
        WindowRect.Rect.X2 = Rect->X2;
        WindowRect.Rect.Y2 = Rect->Y2;
    } else {
        WindowRect.Rect.X1 = 0;
        WindowRect.Rect.Y1 = 0;
        WindowRect.Rect.X2 = 0;
        WindowRect.Rect.Y2 = 0;
    }

    return (BOOL)exoscall(SYSCALL_InvalidateWindowRect, EXOS_PARAM(&WindowRect));
}

/***************************************************************************/

BOOL InvalidateClientRect(HANDLE Window, LPRECT Rect) {
    WINDOW_RECT WindowRect;

    WindowRect.Header.Size = sizeof WindowRect;
    WindowRect.Header.Version = EXOS_ABI_VERSION;
    WindowRect.Header.Flags = (Rect == NULL) ? WINDOW_RECT_FLAG_ALL : 0;
    WindowRect.Window = Window;

    if (Rect != NULL) {
        WindowRect.Rect.X1 = Rect->X1;
        WindowRect.Rect.Y1 = Rect->Y1;
        WindowRect.Rect.X2 = Rect->X2;
        WindowRect.Rect.Y2 = Rect->Y2;
    } else {
        WindowRect.Rect.X1 = 0;
        WindowRect.Rect.Y1 = 0;
        WindowRect.Rect.X2 = 0;
        WindowRect.Rect.Y2 = 0;
    }

    return (BOOL)exoscall(SYSCALL_InvalidateClientRect, EXOS_PARAM(&WindowRect));
}

/***************************************************************************/

UINT SetWindowProp(HANDLE Window, LPCSTR Name, UINT Value) {
    PROP_INFO PropInfo;

    PropInfo.Header.Size = sizeof PropInfo;
    PropInfo.Header.Version = EXOS_ABI_VERSION;
    PropInfo.Header.Flags = 0;
    PropInfo.Window = Window;
    PropInfo.Name = Name;
    PropInfo.Value = Value;

    return (UINT)exoscall(SYSCALL_SetWindowProp, EXOS_PARAM(&PropInfo));
}

/***************************************************************************/

BOOL SetWindowCaption(HANDLE Window, LPCSTR Caption) {
    WINDOW_CAPTION WindowCaption;
    UINT Index;

    WindowCaption.Header.Size = sizeof WindowCaption;
    WindowCaption.Header.Version = EXOS_ABI_VERSION;
    WindowCaption.Header.Flags = 0;
    WindowCaption.Window = Window;
    for (Index = 0; Index < MAX_WINDOW_CAPTION; Index++) {
        WindowCaption.Text[Index] = (Caption[Index] == 0) ? 0 : (U8)Caption[Index];
        if (Caption[Index] == 0) break;
    }

    return (BOOL)exoscall(SYSCALL_SetWindowCaption, EXOS_PARAM(&WindowCaption));
}

/***************************************************************************/

BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds) {
    TIMER_INFO TimerInfo;

    TimerInfo.Header.Size = sizeof TimerInfo;
    TimerInfo.Header.Version = EXOS_ABI_VERSION;
    TimerInfo.Header.Flags = 0;
    TimerInfo.Window = Window;
    TimerInfo.TimerID = TimerID;
    TimerInfo.Interval = IntervalMilliseconds;

    return (BOOL)exoscall(SYSCALL_SetWindowTimer, EXOS_PARAM(&TimerInfo));
}

/***************************************************************************/

BOOL KillWindowTimer(HANDLE Window, U32 TimerID) { return SetWindowTimer(Window, TimerID, 0); }

/***************************************************************************/

UINT GetWindowProp(HANDLE Window, LPCSTR Name) {
    PROP_INFO PropInfo;

    PropInfo.Header.Size = sizeof PropInfo;
    PropInfo.Header.Version = EXOS_ABI_VERSION;
    PropInfo.Header.Flags = 0;
    PropInfo.Window = Window;
    PropInfo.Name = Name;

    return (UINT)exoscall(SYSCALL_GetWindowProp, EXOS_PARAM(&PropInfo));
}

/***************************************************************************/

HANDLE GetWindowGC(HANDLE Window) { return (HANDLE)exoscall(SYSCALL_GetWindowGC, EXOS_PARAM(Window)); }

/***************************************************************************/

BOOL ReleaseWindowGC(HANDLE GC) { return (BOOL)exoscall(SYSCALL_ReleaseWindowGC, EXOS_PARAM(GC)); }

/***************************************************************************/

BOOL GetGCSurface(HANDLE GC, LPGC_SURFACE_INFO Info) {
    if (GC == NULL || Info == NULL) return FALSE;
    Info->GC = GC;
    return (BOOL)exoscall(SYSCALL_GetGCSurface, EXOS_PARAM(Info));
}

/***************************************************************************/

HANDLE BeginWindowDraw(HANDLE Window) {
    UNUSED(Window);
    return NULL;
}

/***************************************************************************/

BOOL EndWindowDraw(HANDLE Window) {
    UNUSED(Window);
    return NULL;
}

/***************************************************************************/

BOOL GetWindowRect(HANDLE Window, LPRECT Rect) {
    WINDOW_RECT WindowRect;

    if (Window == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    WindowRect.Header.Size = sizeof WindowRect;
    WindowRect.Header.Version = EXOS_ABI_VERSION;
    WindowRect.Header.Flags = 0;
    WindowRect.Window = Window;
    WindowRect.Rect.X1 = 0;
    WindowRect.Rect.Y1 = 0;
    WindowRect.Rect.X2 = 0;
    WindowRect.Rect.Y2 = 0;

    if (exoscall(SYSCALL_GetWindowRect, EXOS_PARAM(&WindowRect)) == FALSE) return FALSE;

    Rect->X1 = WindowRect.Rect.X1;
    Rect->Y1 = WindowRect.Rect.Y1;
    Rect->X2 = WindowRect.Rect.X2;
    Rect->Y2 = WindowRect.Rect.Y2;

    return TRUE;
}

/***************************************************************************/

BOOL GetWindowClientRect(HANDLE Window, LPRECT Rect) {
    WINDOW_RECT WindowRect;

    if (Window == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    WindowRect.Header.Size = sizeof WindowRect;
    WindowRect.Header.Version = EXOS_ABI_VERSION;
    WindowRect.Header.Flags = 0;
    WindowRect.Window = Window;
    WindowRect.Rect.X1 = 0;
    WindowRect.Rect.Y1 = 0;
    WindowRect.Rect.X2 = 0;
    WindowRect.Rect.Y2 = 0;

    if (exoscall(SYSCALL_GetWindowClientRect, EXOS_PARAM(&WindowRect)) == FALSE) return FALSE;

    Rect->X1 = WindowRect.Rect.X1;
    Rect->Y1 = WindowRect.Rect.Y1;
    Rect->X2 = WindowRect.Rect.X2;
    Rect->Y2 = WindowRect.Rect.Y2;

    return TRUE;
}

/***************************************************************************/

BOOL ScreenPointToWindowPoint(HANDLE Window, LPPOINT ScreenPoint, LPPOINT WindowPoint) {
    WINDOW_POINT_INFO WindowPointInfo;

    if (Window == NULL) return FALSE;
    if (ScreenPoint == NULL) return FALSE;
    if (WindowPoint == NULL) return FALSE;

    WindowPointInfo.Header.Size = sizeof WindowPointInfo;
    WindowPointInfo.Header.Version = EXOS_ABI_VERSION;
    WindowPointInfo.Header.Flags = 0;
    WindowPointInfo.Window = Window;
    WindowPointInfo.ScreenPoint = *ScreenPoint;
    WindowPointInfo.WindowPoint.X = 0;
    WindowPointInfo.WindowPoint.Y = 0;

    if (exoscall(SYSCALL_ScreenPointToWindowPoint, EXOS_PARAM(&WindowPointInfo)) == FALSE) return FALSE;

    *WindowPoint = WindowPointInfo.WindowPoint;
    return TRUE;
}

/***************************************************************************/

HANDLE GetWindowParent(HANDLE Window) { return (HANDLE)exoscall(SYSCALL_GetWindowParent, EXOS_PARAM(Window)); }

/***************************************************************************/

U32 GetWindowChildCount(HANDLE Window) { return exoscall(SYSCALL_GetWindowChildCount, EXOS_PARAM(Window)); }

/***************************************************************************/

HANDLE GetWindowChild(HANDLE Window, U32 ChildIndex) {
    WINDOW_CHILD_INFO WindowChildInfo;

    WindowChildInfo.Header.Size = sizeof WindowChildInfo;
    WindowChildInfo.Header.Version = EXOS_ABI_VERSION;
    WindowChildInfo.Header.Flags = 0;
    WindowChildInfo.Window = Window;
    WindowChildInfo.ChildIndex = ChildIndex;

    return (HANDLE)exoscall(SYSCALL_GetWindowChild, EXOS_PARAM(&WindowChildInfo));
}

/***************************************************************************/

HANDLE GetNextWindowSibling(HANDLE Window) {
    return (HANDLE)exoscall(SYSCALL_GetNextWindowSibling, EXOS_PARAM(Window));
}

/***************************************************************************/

HANDLE GetPreviousWindowSibling(HANDLE Window) {
    return (HANDLE)exoscall(SYSCALL_GetPreviousWindowSibling, EXOS_PARAM(Window));
}

/***************************************************************************/

BOOL MoveWindow(HANDLE Window, LPRECT Rect) {
    WINDOW_RECT WindowRect;

    if (Window == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    WindowRect.Header.Size = sizeof WindowRect;
    WindowRect.Header.Version = EXOS_ABI_VERSION;
    WindowRect.Header.Flags = 0;
    WindowRect.Window = Window;
    WindowRect.Rect.X1 = Rect->X1;
    WindowRect.Rect.Y1 = Rect->Y1;
    WindowRect.Rect.X2 = Rect->X2;
    WindowRect.Rect.Y2 = Rect->Y2;

    return (BOOL)exoscall(SYSCALL_MoveWindow, EXOS_PARAM(&WindowRect));
}

/***************************************************************************/

HANDLE GetSystemBrush(U32 Index) { return exoscall(SYSCALL_GetSystemBrush, EXOS_PARAM(Index)); }

/***************************************************************************/

HANDLE GetSystemPen(U32 Index) { return exoscall(SYSCALL_GetSystemPen, EXOS_PARAM(Index)); }

/***************************************************************************/

HANDLE CreateBrush(LPBRUSH_INFO BrushInfo) { return exoscall(SYSCALL_CreateBrush, EXOS_PARAM(BrushInfo)); }

/***************************************************************************/

HANDLE CreatePen(LPPEN_INFO PenInfo) { return exoscall(SYSCALL_CreatePen, EXOS_PARAM(PenInfo)); }

/***************************************************************************/

HANDLE SelectBrush(HANDLE GC, HANDLE Brush) {
    GCSELECT Select;

    Select.Header.Size = sizeof Select;
    Select.Header.Version = EXOS_ABI_VERSION;
    Select.Header.Flags = 0;
    Select.GC = GC;
    Select.Object = Brush;

    return (HANDLE)exoscall(SYSCALL_SelectBrush, EXOS_PARAM(&Select));
}

/***************************************************************************/

HANDLE SelectPen(HANDLE GC, HANDLE Pen) {
    GCSELECT Select;

    Select.Header.Size = sizeof Select;
    Select.Header.Version = EXOS_ABI_VERSION;
    Select.Header.Flags = 0;
    Select.GC = GC;
    Select.Object = Pen;

    return (HANDLE)exoscall(SYSCALL_SelectPen, EXOS_PARAM(&Select));
}

/***************************************************************************/

U32 BaseWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    MESSAGE_INFO MessageInfo;

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Target = Window;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (U32)exoscall(SYSCALL_BaseWindowFunc, EXOS_PARAM(&MessageInfo));
}

/***************************************************************************/

U32 SetPixel(HANDLE GC, U32 X, U32 Y) {
    PIXEL_INFO PixelInfo;

    PixelInfo.Header.Size = sizeof PixelInfo;
    PixelInfo.Header.Version = EXOS_ABI_VERSION;
    PixelInfo.Header.Flags = 0;
    PixelInfo.GC = GC;
    PixelInfo.X = X;
    PixelInfo.Y = Y;

    return (U32)exoscall(SYSCALL_SetPixel, EXOS_PARAM(&PixelInfo));
}

/***************************************************************************/

U32 GetPixel(HANDLE GC, U32 X, U32 Y) {
    PIXEL_INFO PixelInfo;

    PixelInfo.Header.Size = sizeof PixelInfo;
    PixelInfo.Header.Version = EXOS_ABI_VERSION;
    PixelInfo.Header.Flags = 0;
    PixelInfo.GC = GC;
    PixelInfo.X = X;
    PixelInfo.Y = Y;

    return (U32)exoscall(SYSCALL_GetPixel, EXOS_PARAM(&PixelInfo));
}