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


    Desktop default and base window procedures

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-NonClient.h"
#include "Desktop-OverlayInvalidation.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "input/MouseDispatcher.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_WINDOW_FUNC_TRACE_SHELLBAR_WINDOW_ID 0x53484252
#define DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE TEXT("desktop.non_client.pressed_message")

/***************************************************************************/

/**
 * @brief Apply the default close behavior for one window.
 * @param Window Target window.
 * @return TRUE on success.
 */
static BOOL DesktopHandleDefaultClose(HANDLE Window) {
    return DeleteWindow(Window);
}

/***************************************************************************/

/**
 * @brief Apply the default minimize behavior for one window.
 * @param Window Target window.
 * @return TRUE on success.
 */
static BOOL DesktopHandleDefaultMinimize(HANDLE Window) {
    return HideWindow(Window);
}

/***************************************************************************/

/**
 * @brief Apply the default maximize behavior for one window.
 * @param Window Target window.
 * @return TRUE on success.
 */
static BOOL DesktopHandleDefaultMaximize(HANDLE Window) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    RECT WorkRect;
    LPWINDOW This = (LPWINDOW)Window;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;
    if (Snapshot.ParentWindow == NULL || Snapshot.ParentWindow->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowEffectiveWorkRectSnapshot(Snapshot.ParentWindow, &WorkRect) == FALSE) return FALSE;

    return DefaultSetWindowRect(This, &WorkRect);
}

/***************************************************************************/

/**
 * @brief Default window procedure for unhandled messages.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
static U32 DefaultWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE: {
        } break;

        case EWM_DELETE: {
        } break;

        case EWM_CLOSE: {
            return DesktopHandleDefaultClose(Window) != FALSE ? 1 : 0;
        }

        case EWM_MAXIMIZE: {
            return DesktopHandleDefaultMaximize(Window) != FALSE ? 1 : 0;
        }

        case EWM_MINIMIZE: {
            return DesktopHandleDefaultMinimize(Window) != FALSE ? 1 : 0;
        }

        case EWM_MOUSEDOWN: {
            LPWINDOW This = (LPWINDOW)Window;
            POINT MousePosition;
            I32 MouseX;
            I32 MouseY;
            RECT ScreenRect;
            U32 TitleBarButtonMessage;

            if ((Param1 & MB_LEFT) == 0) break;
            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) break;

            MousePosition.X = MouseX;
            MousePosition.Y = MouseY;
            TitleBarButtonMessage = GetWindowTitleBarButtonMessageAtPoint(This, &MousePosition);

            if (TitleBarButtonMessage != EWM_NONE) {
                (void)SetWindowProp(Window, DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE, TitleBarButtonMessage);
                (void)BringWindowToFront(Window);
                (void)RequestWindowDraw(Window);
                return 1;
            }

            if (IsPointInWindowTitleBar(This, &MousePosition) == FALSE) break;

            ScreenRect = This->ScreenRect;
            (void)SetWindowProp(Window, DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE, 0);
            (void)BringWindowToFront(Window);
            (void)SetDesktopCaptureState(This, This, MousePosition.X - ScreenRect.X1, MousePosition.Y - ScreenRect.Y1);
        } break;

        case EWM_MOUSEMOVE: {
            LPWINDOW This = (LPWINDOW)Window;
            LPWINDOW CaptureWindow = NULL;
            RECT ScreenRect;
            RECT ParentScreenRect;
            POINT NewPosition;
            U32 Buttons;
            I32 OffsetX = 0;
            I32 OffsetY = 0;
            BOOL ParentHasRect = FALSE;
            U32 PressedMessage;

            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            PressedMessage = GetWindowProp(Window, DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE);
            if (PressedMessage != 0) return 1;
            if (GetDesktopCaptureState(This, &CaptureWindow, &OffsetX, &OffsetY) == FALSE) break;
            if (CaptureWindow != This) break;

            Buttons = GetMouseDriver()->Command(DF_MOUSE_GETBUTTONS, 0);
            if ((Buttons & MB_LEFT) == 0) {
                (void)SetDesktopCaptureState(This, NULL, 0, 0);
                break;
            }

            if (GetWindowScreenRectSnapshot(This, &ScreenRect) == FALSE) break;

            NewPosition.X = ScreenRect.X1 + SIGNED(Param1) - OffsetX;
            NewPosition.Y = ScreenRect.Y1 + SIGNED(Param2) - OffsetY;

            SAFE_USE_VALID_ID(This->ParentWindow, KOID_WINDOW) {
                ParentHasRect = GetWindowScreenRectSnapshot(This->ParentWindow, &ParentScreenRect);
            }

            if (ParentHasRect != FALSE) {
                NewPosition.X -= ParentScreenRect.X1;
                NewPosition.Y -= ParentScreenRect.Y1;
            }

            {
                RECT WindowRect;
                if (BuildWindowRectAtPosition(This, &NewPosition, &WindowRect) != FALSE) {
                    (void)MoveWindow(Window, &WindowRect);
                }
            }
        } break;

        case EWM_MOUSEUP: {
            LPWINDOW This = (LPWINDOW)Window;
            LPWINDOW CaptureWindow = NULL;
            U32 PressedMessage;
            POINT MousePosition;
            I32 MouseX;
            I32 MouseY;

            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            PressedMessage = GetWindowProp(Window, DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE);
            if (PressedMessage != 0) {
                (void)SetWindowProp(Window, DESKTOP_WINDOW_PROP_NON_CLIENT_PRESSED_MESSAGE, 0);
                (void)RequestWindowDraw(Window);
                if ((Param1 & MB_LEFT) == 0) return 1;
                if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) return 1;

                MousePosition.X = MouseX;
                MousePosition.Y = MouseY;
                if (GetWindowTitleBarButtonMessageAtPoint(This, &MousePosition) == PressedMessage) {
                    (void)PostMessage(Window, PressedMessage, 0, 0);
                }
                return 1;
            }
            if (GetDesktopCaptureState(This, &CaptureWindow, NULL, NULL) == FALSE) break;
            if (CaptureWindow != This) break;

            (void)SetDesktopCaptureState(This, NULL, 0, 0);
        } break;

        case EWM_MOVE: {
            LPWINDOW This = (LPWINDOW)Window;
            POINT Position;
            RECT WindowRect;

            Position.X = SIGNED(Param1);
            Position.Y = SIGNED(Param2);

            if (BuildWindowRectAtPosition(This, &Position, &WindowRect) != FALSE &&
                DefaultSetWindowRect(This, &WindowRect)) {
                return 1;
            }
        } break;

        case EWM_DRAW: {
            return BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);
        }

        case EWM_CLEAR: {
            HANDLE GC;
            RECT SurfaceRect;
            RECT ClipScreenRect;
            RECT ClipLocalRect;
            RECT SurfaceScreenRect;
            LPWINDOW This = (LPWINDOW)Window;
            WINDOW_DRAW_CONTEXT_SNAPSHOT DrawContext;
            WINDOW_STATE_SNAPSHOT Snapshot;
            LPDESKTOP Desktop;
            LPWINDOW RootWindow = NULL;
            RECT DamageScreenRect;
            U32 ThemeToken;
            BOOL PreviousTransparent;
            BOOL ResolvedTransparent = FALSE;
            BOOL EffectiveTransparent = FALSE;

            if (DesktopGetWindowDrawSurfaceRect(This, &SurfaceRect) == FALSE) {
                if (ShouldDrawWindowNonClient(This) != FALSE) {
                    if (GetWindowClientRect((HANDLE)This, &SurfaceRect) == FALSE) break;
                } else {
                    if (GetWindowRect((HANDLE)This, &SurfaceRect) == FALSE) break;
                }
            } else if (GetWindowDrawContextSnapshot(This, &DrawContext) != FALSE &&
                       (DrawContext.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) != 0 &&
                       DesktopGetWindowDrawClipRect(This, &ClipScreenRect) != FALSE) {
                SurfaceScreenRect.X1 = DrawContext.Origin.X + SurfaceRect.X1;
                SurfaceScreenRect.Y1 = DrawContext.Origin.Y + SurfaceRect.Y1;
                SurfaceScreenRect.X2 = DrawContext.Origin.X + SurfaceRect.X2;
                SurfaceScreenRect.Y2 = DrawContext.Origin.Y + SurfaceRect.Y2;
                GraphicsScreenRectToWindowRect(&SurfaceScreenRect, &ClipScreenRect, &ClipLocalRect);

                if (IntersectRect(&SurfaceRect, &ClipLocalRect, &ClipLocalRect) == FALSE) {
                    return 1;
                }
            }

            if (This != NULL && This->TypeID == KOID_WINDOW && This->WindowID == DESKTOP_WINDOW_FUNC_TRACE_SHELLBAR_WINDOW_ID) {
            }

            GC = BeginWindowDraw(Window);
            if (GC == NULL) break;

            ThemeToken = Param1 != 0 ? Param1 : THEME_TOKEN_WINDOW_BACKGROUND_CLIENT;
            if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) {
                EndWindowDraw(Window);
                break;
            }
            PreviousTransparent = ((Snapshot.Status & WINDOW_STATUS_CONTENT_TRANSPARENT) != 0);
            (void)DrawWindowBackgroundResolved(Window, GC, &SurfaceRect, ThemeToken, &ResolvedTransparent);

            EndWindowDraw(Window);

            EffectiveTransparent = ResolvedTransparent;
            if (Snapshot.ContentTransparencyHint == WINDOW_CONTENT_TRANSPARENCY_HINT_TRANSPARENT) {
                EffectiveTransparent = TRUE;
            } else if (Snapshot.ContentTransparencyHint == WINDOW_CONTENT_TRANSPARENCY_HINT_OPAQUE) {
                EffectiveTransparent = FALSE;
            }

            if (EffectiveTransparent != PreviousTransparent) {
                (void)DesktopSetWindowResolvedTransparencyState(This, EffectiveTransparent);

                if (WindowRectToScreenRect((HANDLE)This, &SurfaceRect, &DamageScreenRect) != FALSE) {
                    Desktop = DesktopGetWindowDesktop(This);
                    if (Desktop != NULL && DesktopGetRootWindow(Desktop, &RootWindow) != FALSE && RootWindow != NULL) {
                        DesktopOverlayInvalidateWindowTreeThenRootRect(RootWindow, &DamageScreenRect);
                    }
                }
            }
            return 1;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Resolve one class pointer from one function in one inheritance chain.
 * @param WindowClass Root class.
 * @param Function Function to match.
 * @return Matched class or NULL.
 */
static LPWINDOW_CLASS ResolveClassByFunction(LPWINDOW_CLASS WindowClass, WINDOWFUNC Function) {
    LPWINDOW_CLASS This;

    if (WindowClass == NULL || Function == NULL) return NULL;

    for (This = WindowClass; This != NULL; This = This->BaseClass) {
        if (This->Function == Function) return This;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Call the base class window function for one dispatch context.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Result from base class callback or default behavior.
 */
U32 BaseWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW This = (LPWINDOW)Window;
    LPTASK Task = GetCurrentTask();
    LPWINDOW_CLASS CurrentClass = NULL;
    LPWINDOW_CLASS BaseClass = NULL;
    LPVOID PreviousWindow = NULL;
    LPVOID PreviousClass = NULL;
    WINDOWFUNC PreviousFunction = NULL;
    U32 Result;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Task->WindowDispatchDepth > 0 && Task->WindowDispatchWindow == This) {
                CurrentClass = (LPWINDOW_CLASS)Task->WindowDispatchClass;

                if (CurrentClass == NULL || CurrentClass->TypeID != KOID_WINDOW_CLASS) {
                    CurrentClass = ResolveClassByFunction(This->Class, Task->WindowDispatchFunction);
                }

                SAFE_USE_VALID_ID(CurrentClass, KOID_WINDOW_CLASS) { BaseClass = CurrentClass->BaseClass; }
            }

            if (BaseClass != NULL && BaseClass->TypeID == KOID_WINDOW_CLASS && BaseClass->Function != NULL) {
                HANDLE TargetHandle;
                PreviousWindow = Task->WindowDispatchWindow;
                PreviousClass = Task->WindowDispatchClass;
                PreviousFunction = Task->WindowDispatchFunction;

                Task->WindowDispatchWindow = This;
                Task->WindowDispatchClass = BaseClass;
                Task->WindowDispatchFunction = BaseClass->Function;
                Task->WindowDispatchDepth++;

                TargetHandle = Window;
                SAFE_USE_VALID_ID(BaseClass->OwnerProcess, KOID_PROCESS) {
                    if (BaseClass->OwnerProcess->Privilege == CPU_PRIVILEGE_USER) {
                        TargetHandle = EnsureHandle((LINEAR)This);
                    }
                }

                Result = BaseClass->Function(TargetHandle, Message, Param1, Param2);

                Task->WindowDispatchWindow = PreviousWindow;
                Task->WindowDispatchClass = PreviousClass;
                Task->WindowDispatchFunction = PreviousFunction;
                if (Task->WindowDispatchDepth > 0) Task->WindowDispatchDepth--;

                return Result;
            }
        }
    }

    return DefaultWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
