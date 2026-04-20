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


    Desktop

\************************************************************************/

#ifndef DESKTOP_H_INCLUDED
#define DESKTOP_H_INCLUDED

/************************************************************************/

#include "User.h"
#include "process/Task-Messaging.h"
#include "process/Process.h"
#include "Desktop-ThemeRuntime.h"
#include "GFX.h"

/************************************************************************/
// Windowing lock contract
//
// Lock domains used by desktop/windowing code:
// - TaskMessageMutex
// - DesktopTreeMutex
// - DesktopStateMutex
// - DesktopTimerMutex
// - WindowMutex (per window)
//
// Required acquisition order:
// 1. TaskMessageMutex
// 2. DesktopTreeMutex
// 3. DesktopStateMutex
// 4. WindowMutex
// 5. GraphicsContextMutex
//
// Forbidden while holding DesktopTree/DesktopState/Window mutexes:
// - calling PostMessage
// - calling window callbacks (Window->Function)
//
// Structural tree/list traversals that can race with z-order/tree mutations
// must use one stable snapshot and execute callbacks outside structural locks.

/************************************************************************/
// Functions in Desktop.c

LPDESKTOP KernelCreateDesktop(void);
BOOL DeleteDesktop(LPDESKTOP);
BOOL KernelShowDesktop(LPDESKTOP);
HANDLE RegisterWindowClass(LPCSTR ClassName, HANDLE BaseClass, LPCSTR BaseClassName, WINDOWFUNC Function, U32 ClassDataSize);
BOOL UnregisterWindowClass(HANDLE WindowClass, LPCSTR ClassName);
HANDLE FindWindowClass(LPCSTR ClassName);
BOOL WindowInheritsClass(HANDLE Window, HANDLE WindowClass, LPCSTR ClassName);
LPWINDOW DesktopCreateWindow(LPWINDOW_INFO);
HANDLE CreateWindow(LPWINDOW_INFO Info);
BOOL DesktopDeleteWindow(LPWINDOW);
BOOL DeleteWindow(HANDLE Window);
LPWINDOW DesktopFindWindow(LPWINDOW, U32);
HANDLE FindWindow(HANDLE StartWindow, U32 WindowID);
LPWINDOW DesktopContainsWindow(LPWINDOW, LPWINDOW);
HANDLE ContainsWindow(HANDLE StartWindow, HANDLE TargetWindow);
LPDESKTOP DesktopGetWindowDesktop(LPWINDOW);
HANDLE GetWindowDesktop(HANDLE Window);
BOOL DesktopUpdateWindowScreenRectAndDirtyRegion(LPWINDOW Window, LPRECT Rect);
BOOL RequestWindowDraw(HANDLE Handle);
BOOL InvalidateClientRect(HANDLE Window, LPRECT Rect);
BOOL InvalidateWindowRect(HANDLE Window, LPRECT Rect);
UINT SetWindowProp(HANDLE Window, LPCSTR Name, UINT Value);
UINT GetWindowProp(HANDLE Window, LPCSTR Name);
BOOL GetWindowRect(HANDLE Window, LPRECT Rect);
BOOL GetWindowClientRect(HANDLE Window, LPRECT Rect);
BOOL ScreenPointToWindowPoint(HANDLE Window, LPPOINT ScreenPoint, LPPOINT WindowPoint);
BOOL MoveWindow(HANDLE Window, LPRECT Rect);
BOOL DesktopSetWindowVisibility(HANDLE, BOOL);
BOOL ShowWindow(HANDLE Window);
BOOL HideWindow(HANDLE Window);
BOOL BringWindowToFront(HANDLE);
BOOL SizeWindow(HANDLE, LPPOINT);
BOOL SetWindowStyleState(HANDLE, U32, BOOL);
BOOL SetWindowStyle(HANDLE Window, U32 Style);
BOOL ClearWindowStyle(HANDLE Window, U32 Style);
BOOL GetWindowStyle(HANDLE Window, U32* StyleOut);
BOOL SetWindowCaption(HANDLE Window, LPCSTR Caption);
BOOL GetWindowCaption(HANDLE Window, LPSTR Caption, UINT CaptionLength);
BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT WindowRect, LPRECT ScreenRect);
BOOL GetDesktopScreenRect(LPDESKTOP, LPRECT);
HANDLE GetWindowParent(HANDLE Window);
U32 GetWindowChildCount(HANDLE Window);
HANDLE GetWindowChild(HANDLE Window, U32 ChildIndex);
HANDLE GetNextWindowSibling(HANDLE Window);
HANDLE GetPreviousWindowSibling(HANDLE Window);
HANDLE GetWindowGC(HANDLE);
BOOL ReleaseWindowGC(HANDLE);
HANDLE BeginWindowDraw(HANDLE Window);
BOOL EndWindowDraw(HANDLE Window);
HANDLE GetSystemBrush(U32 Index);
HANDLE GetSystemPen(U32 Index);
HANDLE CreateBrush(LPBRUSH_INFO BrushInfo);
HANDLE CreatePen(LPPEN_INFO PenInfo);
HANDLE SelectBrush(HANDLE GC, HANDLE Brush);
HANDLE SelectPen(HANDLE GC, HANDLE Pen);
BOOL KernelSetPixel(LPPIXEL_INFO);
BOOL KernelGetPixel(LPPIXEL_INFO);
BOOL KernelRectangle(LPRECT_INFO);
BOOL Line(LPLINE_INFO LineInfo);
BOOL Arc(LPARC_INFO ArcInfo);
BOOL Triangle(LPTRIANGLE_INFO TriangleInfo);
BOOL DesktopDrawText(LPGFX_TEXT_DRAW_INFO);
BOOL DesktopMeasureText(LPGFX_TEXT_MEASURE_INFO);
BOOL DrawText(LPTEXT_DRAW_INFO DrawInfo);
BOOL MeasureText(LPTEXT_MEASURE_INFO MeasureInfo);
BOOL DrawWindowBackground(HANDLE Window, HANDLE GC, LPRECT Rect, U32 ThemeToken);
HANDLE WindowHitTest(HANDLE, LPPOINT);
BOOL GetMousePosition(LPPOINT Point);
HANDLE CaptureMouse(HANDLE Window);
BOOL ReleaseMouse(void);
BOOL GetGraphicsDebugInfo(LPDRIVER_DEBUG_INFO Info);
BOOL GetMouseDebugInfo(LPDRIVER_DEBUG_INFO Info);
BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds);
BOOL KillWindowTimer(HANDLE Window, U32 TimerID);
U32 BaseWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2);
BOOL DeleteObject(HANDLE Object);
BOOL LoadTheme(LPCSTR Path);
BOOL ActivateTheme(LPCSTR NameOrHandle);
BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info);
BOOL ResetThemeToDefault(void);
BOOL ApplyDesktopTheme(LPCSTR Target);
HANDLE GetDesktopWindow(HANDLE Desktop);

/************************************************************************/

#endif  // DESKTOP_H_INCLUDED
