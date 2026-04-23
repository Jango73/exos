
/************************************************************************\

    EXOS Runtime
    Copyright (c) 1999-2026 Jango73

    SPDX-License-Identifier: MIT
    See runtime/LICENSE for license terms.


    EXOS Runtime

\************************************************************************/

#include "../include/exos.h"
#include "../include/exos-runtime.h"
#include "../include/exos-string.h"

/***************************************************************************/

// Every user structure passed to the kernel begins with an ABI_HDR. Populate
// Header.Size with sizeof(struct), set Header.Version to EXOS_ABI_VERSION, and clear
// Header.Flags before invoking system calls.

UINT CreateTask(LPTASK_INFO TaskInfo) {
    if (TaskInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    TaskInfo->Task = 0;
    return (UINT)exoscall(SYSCALL_CreateTask, EXOS_PARAM(TaskInfo));
}

/***************************************************************************/

BOOL KillTask(HANDLE Task) { return (BOOL)exoscall(SYSCALL_KillTask, EXOS_PARAM(Task)); }

/***************************************************************************/

void Exit(void) { exoscall(SYSCALL_Exit, EXOS_PARAM(0)); }

/***************************************************************************/

void Sleep(U32 MilliSeconds) { exoscall(SYSCALL_Sleep, EXOS_PARAM(MilliSeconds)); }

/***************************************************************************/

U32 Wait(LPWAIT_INFO WaitInfo) { return (U32)exoscall(SYSCALL_Wait, EXOS_PARAM(WaitInfo)); }

/***************************************************************************/

UINT GetSystemTime(void) { return (UINT)exoscall(SYSCALL_GetSystemTime, EXOS_PARAM(0)); }

/***************************************************************************/

BOOL GetLocalTime(LPDATETIME Time) { return (BOOL)exoscall(SYSCALL_GetLocalTime, EXOS_PARAM(Time)); }

/***************************************************************************/

BOOL GetProcessMemoryInfo(LPPROCESS_MEMORY_INFO Info) {
    if (Info == NULL) {
        return FALSE;
    }

    return exoscall(SYSCALL_GetProcessMemoryInfo, EXOS_PARAM(Info)) == DF_RETURN_SUCCESS;
}

/***************************************************************************/

BOOL GetProfileInfo(LPPROFILE_QUERY_INFO Info) {
    if (Info == NULL) {
        return FALSE;
    }

    Info->Header.Size = sizeof(*Info);
    Info->Header.Version = EXOS_ABI_VERSION;
    Info->Header.Flags = 0;

    return exoscall(SYSCALL_GetProfileInfo, EXOS_PARAM(Info)) == DF_RETURN_SUCCESS;
}

/***************************************************************************/

BOOL GetMessage(HANDLE Target, LPMESSAGE Message, U32 First, U32 Last) {
    MESSAGE_INFO MessageInfo;
    BOOL Result;

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Target = Target;
    MessageInfo.First = First;
    MessageInfo.Last = Last;

    Result = (BOOL)exoscall(SYSCALL_GetMessage, EXOS_PARAM(&MessageInfo));

    Message->Time = MessageInfo.Time;
    Message->Target = MessageInfo.Target;
    Message->Message = MessageInfo.Message;
    Message->Param1 = MessageInfo.Param1;
    Message->Param2 = MessageInfo.Param2;

    return Result;
}

/***************************************************************************/

BOOL PeekMessage(HANDLE Target, LPMESSAGE Message, U32 First, U32 Last, U32 Flags) {
    MESSAGE_INFO MessageInfo;
    BOOL Result;

    UNUSED(Flags);

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Target = Target;
    MessageInfo.First = First;
    MessageInfo.Last = Last;

    Result = (BOOL)exoscall(SYSCALL_PeekMessage, EXOS_PARAM(&MessageInfo));

    if (Result && Message != NULL) {
        Message->Time = MessageInfo.Time;
        Message->Target = MessageInfo.Target;
        Message->Message = MessageInfo.Message;
        Message->Param1 = MessageInfo.Param1;
        Message->Param2 = MessageInfo.Param2;
    }

    return Result;
}

/***************************************************************************/

BOOL DispatchMessage(LPMESSAGE Message) {
    MESSAGE_INFO MessageInfo;
    MESSAGE LocalMessage;

    if (Message == NULL) return FALSE;

    LocalMessage = *Message;

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Time = LocalMessage.Time;
    MessageInfo.Target = LocalMessage.Target;
    MessageInfo.Message = LocalMessage.Message;
    MessageInfo.Param1 = LocalMessage.Param1;
    MessageInfo.Param2 = LocalMessage.Param2;

    return (BOOL)exoscall(SYSCALL_DispatchMessage, EXOS_PARAM(&MessageInfo));
}

/***************************************************************************/

BOOL PostMessage(HANDLE Target, U32 Message, U32 Param1, U32 Param2) {
    UNUSED(Target);

    MESSAGE_INFO MessageInfo;

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (BOOL)exoscall(SYSCALL_PostMessage, EXOS_PARAM(&MessageInfo));
}

/***************************************************************************/

U32 SendMessage(HANDLE Target, U32 Message, U32 Param1, U32 Param2) {
    MESSAGE_INFO MessageInfo;

    MessageInfo.Header.Size = sizeof MessageInfo;
    MessageInfo.Header.Version = EXOS_ABI_VERSION;
    MessageInfo.Header.Flags = 0;
    MessageInfo.Target = Target;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (U32)exoscall(SYSCALL_SendMessage, EXOS_PARAM(&MessageInfo));
}

/***************************************************************************/

U32 FindFirstFile(FILE_FIND_INFO* Info) {
    if (Info == NULL) return 0;
    return (U32)exoscall(SYSCALL_FindFirstFile, EXOS_PARAM(Info));
}

/***************************************************************************/

U32 FindNextFile(FILE_FIND_INFO* Info) {
    if (Info == NULL) return 0;
    return (U32)exoscall(SYSCALL_FindNextFile, EXOS_PARAM(Info));
}

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

HANDLE RegisterWindowClass(LPCSTR ClassName, HANDLE BaseClass, LPCSTR BaseClassName, WINDOWFUNC Function, U32 ClassDataSize) {
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
    HANDLE Parent,
    HANDLE WindowClass,
    LPCSTR WindowClassName,
    WINDOWFUNC Func,
    U32 Style,
    U32 ID,
    I32 PosX,
    I32 PosY,
    I32 SizeX,
    I32 SizeY) {
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

HANDLE CreateWindow(LPWINDOW_INFO WindowInfo) {
    return (HANDLE)exoscall(SYSCALL_CreateWindow, EXOS_PARAM(WindowInfo));
}

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

HANDLE BeginWindowDraw(HANDLE Window) {
    UNUSED(Window);
    // return (HANDLE) exoscall(SYSCALL_BeginWindowDraw, EXOS_PARAM( Window));
    return NULL;
}

/***************************************************************************/

BOOL EndWindowDraw(HANDLE Window) {
    UNUSED(Window);
    // return (BOOL) exoscall(SYSCALL_EndWindowDraw, EXOS_PARAM( Window));
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

HANDLE GetNextWindowSibling(HANDLE Window) { return (HANDLE)exoscall(SYSCALL_GetNextWindowSibling, EXOS_PARAM(Window)); }

/***************************************************************************/

HANDLE GetPreviousWindowSibling(HANDLE Window) { return (HANDLE)exoscall(SYSCALL_GetPreviousWindowSibling, EXOS_PARAM(Window)); }

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

HANDLE CreateBrush(LPBRUSH_INFO BrushInfo) {
    return exoscall(SYSCALL_CreateBrush, EXOS_PARAM(BrushInfo));
}

/***************************************************************************/

HANDLE CreatePen(LPPEN_INFO PenInfo) {
    return exoscall(SYSCALL_CreatePen, EXOS_PARAM(PenInfo));
}

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

/***************************************************************************/

BOOL Line(LPLINE_INFO LineInfo) {
    return (BOOL)exoscall(SYSCALL_Line, EXOS_PARAM(LineInfo));
}

/***************************************************************************/

void Rectangle(HANDLE GC, U32 X1, U32 Y1, U32 X2, U32 Y2, U32 CornerRadius) {
    RECT_INFO RectInfo;

    RectInfo.Header.Size = sizeof RectInfo;
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;
    RectInfo.CornerRadius = (I32)CornerRadius;
    RectInfo.CornerStyle = CornerRadius > 0 ? RECT_CORNER_STYLE_ROUNDED : RECT_CORNER_STYLE_SQUARE;

    exoscall(SYSCALL_Rectangle, EXOS_PARAM(&RectInfo));
}

/***************************************************************************/

BOOL DrawText(LPTEXT_DRAW_INFO DrawInfo) {
    if (DrawInfo == NULL) {
        return FALSE;
    }

    return (BOOL)exoscall(SYSCALL_DrawText, EXOS_PARAM(DrawInfo));
}

/***************************************************************************/

BOOL MeasureText(LPTEXT_MEASURE_INFO MeasureInfo) {
    if (MeasureInfo == NULL) {
        return FALSE;
    }

    return (BOOL)exoscall(SYSCALL_MeasureText, EXOS_PARAM(MeasureInfo));
}

/***************************************************************************/

BOOL DrawWindowBackground(HANDLE Window, HANDLE GC, LPRECT Rect, U32 ThemeToken) {
    WINDOW_BACKGROUND_INFO BackgroundInfo;

    if (Rect == NULL) return FALSE;

    BackgroundInfo.Header.Size = sizeof BackgroundInfo;
    BackgroundInfo.Header.Version = EXOS_ABI_VERSION;
    BackgroundInfo.Header.Flags = 0;
    BackgroundInfo.Window = Window;
    BackgroundInfo.GC = GC;
    BackgroundInfo.Rect = *Rect;
    BackgroundInfo.ThemeToken = ThemeToken;

    return (BOOL)exoscall(SYSCALL_DrawWindowBackground, EXOS_PARAM(&BackgroundInfo));
}

/***************************************************************************/

BOOL GetMousePosition(LPPOINT Point) { return (BOOL)exoscall(SYSCALL_GetMousePos, EXOS_PARAM(Point)); }

/***************************************************************************/

U32 GetMouseButtons(void) { return (U32)exoscall(SYSCALL_GetMouseButtons, EXOS_PARAM(0)); }

/***************************************************************************/

HANDLE CaptureMouse(HANDLE Window) {
    return (HANDLE)exoscall(SYSCALL_CaptureMouse, EXOS_PARAM(Window));
}

/***************************************************************************/

BOOL ReleaseMouse(void) { return (BOOL)exoscall(SYSCALL_ReleaseMouse, EXOS_PARAM(0)); }

/***************************************************************************/

U32 GetKeyModifiers(void) {
    U32 modifiers = 0;
    exoscall(SYSCALL_ConsoleGetKeyModifiers, EXOS_PARAM(&modifiers));
    return modifiers;
}

/***************************************************************************/

U32 ConsoleGetKey(LPKEYCODE KeyCode) {
    return exoscall(SYSCALL_ConsoleGetKey, EXOS_PARAM(KeyCode));
}

/***************************************************************************/

U32 ConsoleBlitBuffer(LPCONSOLE_BLIT_BUFFER Buffer) {
    return exoscall(SYSCALL_ConsoleBlitBuffer, EXOS_PARAM(Buffer));
}

/***************************************************************************/

void ConsoleGotoXY(LPPOINT Position) {
    exoscall(SYSCALL_ConsoleGotoXY, EXOS_PARAM(Position));
}

/***************************************************************************/

void ConsoleClear(void) {
    exoscall(SYSCALL_ConsoleClear, EXOS_PARAM(0));
}

/***************************************************************************/

U32 ConsoleSetColumnsRows(U32 Columns, U32 Rows) {
    GRAPHICS_MODE_INFO Info;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.ModeIndex = INFINITY;
    Info.Width = Columns;
    Info.Height = Rows;
    Info.BitsPerPixel = 0;

    return (U32)exoscall(SYSCALL_ConsoleSetMode, EXOS_PARAM(&Info));
}

/***************************************************************************/

BOOL ConsoleGetCurrentMode(LPCONSOLE_MODE_INFO Info) {
    if (Info == NULL) return FALSE;

    Info->Header.Size = sizeof *Info;
    Info->Header.Version = EXOS_ABI_VERSION;
    Info->Header.Flags = 0;
    Info->Index = 0;
    Info->Columns = 0;
    Info->Rows = 0;
    Info->CharHeight = 0;

    return (BOOL)exoscall(SYSCALL_ConsoleGetCurrentMode, EXOS_PARAM(Info));
}

/***************************************************************************/

BOOL DeleteObject(HANDLE Object) {
    return (BOOL)exoscall(SYSCALL_DeleteObject, EXOS_PARAM(Object));
}

/***************************************************************************/

static U32 RandomSeed = 1;

/***************************************************************************/

void srand(U32 Seed) {
    RandomSeed = Seed;
}

/***************************************************************************/

U32 rand(void) {
    RandomSeed = RandomSeed * 1103515245 + 12345;
    return (RandomSeed / 65536) % 32768;
}

/***************************************************************************/
// Berkeley Socket API implementations

SOCKET_HANDLE SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol) {
    SOCKET_CREATE_INFO Info;
    Info.Header.Size = sizeof(SOCKET_CREATE_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.AddressFamily = AddressFamily;
    Info.SocketType = SocketType;
    Info.Protocol = Protocol;
    return (SOCKET_HANDLE)exoscall(SYSCALL_SocketCreate, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketBind(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {
    SOCKET_BIND_INFO Info;
    Info.Header.Size = sizeof(SOCKET_BIND_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    // Copy address data to buffer
    if (AddressLength <= 16) {
        for (U32 i = 0; i < AddressLength; i++) {
            Info.AddressData[i] = ((U8*)Address)[i];
        }
    }
    Info.AddressLength = AddressLength;
    return exoscall(SYSCALL_SocketBind, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketListen(SOCKET_HANDLE SocketHandle, U32 Backlog) {
    SOCKET_LISTEN_INFO Info;
    Info.Header.Size = sizeof(SOCKET_LISTEN_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Backlog = Backlog;
    return exoscall(SYSCALL_SocketListen, EXOS_PARAM(&Info));
}

/***************************************************************************/

SOCKET_HANDLE SocketAccept(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    SOCKET_ACCEPT_INFO Info;
    Info.Header.Size = sizeof(SOCKET_ACCEPT_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.AddressBuffer = (LPVOID)Address;
    Info.AddressLength = AddressLength;
    return (SOCKET_HANDLE)exoscall(SYSCALL_SocketAccept, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketConnect(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {
    SOCKET_CONNECT_INFO Info;
    Info.Header.Size = sizeof(SOCKET_CONNECT_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    // Copy address data to buffer
    if (AddressLength <= 16) {
        for (U32 i = 0; i < AddressLength; i++) {
            Info.AddressData[i] = ((U8*)Address)[i];
        }
    }
    Info.AddressLength = AddressLength;
    return exoscall(SYSCALL_SocketConnect, EXOS_PARAM(&Info));
}

/***************************************************************************/

I32 SocketSend(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags) {
    SOCKET_DATA_INFO Info;
    Info.Header.Size = sizeof(SOCKET_DATA_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Buffer = (LPVOID)Buffer;
    Info.Length = Length;
    Info.Flags = Flags;
    return (I32)exoscall(SYSCALL_SocketSend, EXOS_PARAM(&Info));
}

/***************************************************************************/

I32 SocketReceive(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags) {
    SOCKET_DATA_INFO Info;
    Info.Header.Size = sizeof(SOCKET_DATA_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Buffer = Buffer;
    Info.Length = Length;
    Info.Flags = Flags;
    return (I32)exoscall(SYSCALL_SocketReceive, EXOS_PARAM(&Info));
}

/***************************************************************************/

I32 SocketSendTo(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS DestAddress, U32 AddressLength) {
    SOCKET_DATA_INFO Info;
    Info.Header.Size = sizeof(SOCKET_DATA_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Buffer = (LPVOID)Buffer;
    Info.Length = Length;
    Info.Flags = Flags;
    // Copy address data to buffer
    if (AddressLength <= 16) {
        for (U32 i = 0; i < AddressLength; i++) {
            Info.AddressData[i] = ((U8*)DestAddress)[i];
        }
    }
    Info.AddressLength = AddressLength;
    return (I32)exoscall(SYSCALL_SocketSendTo, EXOS_PARAM(&Info));
}

/***************************************************************************/

I32 SocketReceiveFrom(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS SourceAddress, U32* AddressLength) {
    SOCKET_DATA_INFO Info;
    Info.Header.Size = sizeof(SOCKET_DATA_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Buffer = Buffer;
    Info.Length = Length;
    Info.Flags = Flags;
    Info.AddressLength = *AddressLength;
    I32 Result = (I32)exoscall(SYSCALL_SocketReceiveFrom, EXOS_PARAM(&Info));
    // Copy address data back from buffer
    if (Info.AddressLength <= 16 && Info.AddressLength <= *AddressLength) {
        for (U32 i = 0; i < Info.AddressLength; i++) {
            ((U8*)SourceAddress)[i] = Info.AddressData[i];
        }
    }
    *AddressLength = Info.AddressLength;
    return Result;
}

/***************************************************************************/

U32 SocketClose(SOCKET_HANDLE SocketHandle) {
    return exoscall(SYSCALL_SocketClose, EXOS_PARAM(SocketHandle));
}

/***************************************************************************/

U32 SocketShutdown(SOCKET_HANDLE SocketHandle, U32 How) {
    SOCKET_SHUTDOWN_INFO Info;
    Info.Header.Size = sizeof(SOCKET_SHUTDOWN_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.How = How;
    return exoscall(SYSCALL_SocketShutdown, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketGetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPVOID OptionValue, U32* OptionLength) {
    SOCKET_OPTION_INFO Info;
    Info.Header.Size = sizeof(SOCKET_OPTION_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Level = Level;
    Info.OptionName = OptionName;
    Info.OptionValue = OptionValue;
    Info.OptionLength = *OptionLength;
    U32 Result = exoscall(SYSCALL_SocketGetOption, EXOS_PARAM(&Info));
    *OptionLength = Info.OptionLength;
    return Result;
}

/***************************************************************************/

U32 SocketSetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPCVOID OptionValue, U32 OptionLength) {
    SOCKET_OPTION_INFO Info;
    Info.Header.Size = sizeof(SOCKET_OPTION_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.Level = Level;
    Info.OptionName = OptionName;
    Info.OptionValue = (LPVOID)OptionValue;
    Info.OptionLength = OptionLength;
    return exoscall(SYSCALL_SocketSetOption, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketGetPeerName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    SOCKET_ACCEPT_INFO Info;
    Info.Header.Size = sizeof(SOCKET_ACCEPT_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.AddressBuffer = (LPVOID)Address;
    Info.AddressLength = AddressLength;
    return exoscall(SYSCALL_SocketGetPeerName, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 SocketGetSocketName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    SOCKET_ACCEPT_INFO Info;
    Info.Header.Size = sizeof(SOCKET_ACCEPT_INFO);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.SocketHandle = SocketHandle;
    Info.AddressBuffer = (LPVOID)Address;
    Info.AddressLength = AddressLength;
    return exoscall(SYSCALL_SocketGetSocketName, EXOS_PARAM(&Info));
}

/***************************************************************************/

U32 InternetAddressFromString(LPCSTR IPString) {
    U32 result = 0;
    U32 octet = 0;
    int octet_count = 0;
    LPCSTR p = IPString;

    if (IPString == NULL) return 0;

    while (*p && octet_count < 4) {
        if (*p >= '0' && *p <= '9') {
            octet = octet * 10 + (*p - '0');
            if (octet > 255) return 0;
        } else if (*p == '.') {
            result = (result << 8) | octet;
            octet = 0;
            octet_count++;
        } else {
            return 0;
        }
        p++;
    }

    if (octet_count == 3) {
        result = (result << 8) | octet;

        debug("[InternetAddressFromString] %x", result);
        return result;
    }

    return 0;
}

/***************************************************************************/

LPCSTR InternetAddressToString(U32 IPAddress) {
    static U8 inet_addr_buffer[16];
    static U8 temp_buffer[16];
    U8* bytes = (U8*)&IPAddress;
    U32 len = 0;

    U32ToString(bytes[0], (LPSTR)temp_buffer);
    StringCopy((LPSTR)(inet_addr_buffer + len), (LPCSTR)temp_buffer);
    len += StringLength((LPCSTR)temp_buffer);
    inet_addr_buffer[len++] = '.';

    U32ToString(bytes[1], (LPSTR)temp_buffer);
    StringCopy((LPSTR)(inet_addr_buffer + len), (LPCSTR)temp_buffer);
    len += StringLength((LPCSTR)temp_buffer);
    inet_addr_buffer[len++] = '.';

    U32ToString(bytes[2], (LPSTR)temp_buffer);
    StringCopy((LPSTR)(inet_addr_buffer + len), (LPCSTR)temp_buffer);
    len += StringLength((LPCSTR)temp_buffer);
    inet_addr_buffer[len++] = '.';

    U32ToString(bytes[3], (LPSTR)temp_buffer);
    StringCopy((LPSTR)(inet_addr_buffer + len), (LPCSTR)temp_buffer);

    return (LPCSTR)inet_addr_buffer;
}

/***************************************************************************/

U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress) {
    if (!InetAddress || !GenericAddress) {
        return 1;
    }

    GenericAddress->AddressFamily = InetAddress->AddressFamily;

    // Copy port and address into the data field
    *((U16*)(GenericAddress->Data)) = InetAddress->Port;
    *((U32*)(GenericAddress->Data + 2)) = InetAddress->Address;

    // Zero the remaining padding
    for (int i = 6; i < 14; i++) {
        GenericAddress->Data[i] = 0;
    }

    return 0;
}

/***************************************************************************/
