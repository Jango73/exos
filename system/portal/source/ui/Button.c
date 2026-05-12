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


    Desktop button component

\************************************************************************/

#include "ui/Button.h"
#include "portal-string.h"
#include <stdlib.h>
#include <string.h>


/***************************************************************************/
// Macros

#define DESKTOP_BUTTON_PROP_STATE TEXT("ui.button.state")
#define DESKTOP_BUTTON_CAPTION_BUFFER_SIZE 128

/***************************************************************************/
// Type definitions

typedef struct tag_DESKTOP_BUTTON_STATE {
    U32 Hover;
    U32 Pressed;
    U32 HasPointerPosition;
    I32 PointerX;
    I32 PointerY;
} DESKTOP_BUTTON_STATE, *LPDESKTOP_BUTTON_STATE;

/***************************************************************************/

static U32 ButtonResolveBackgroundToken(HANDLE Window);
static LPDESKTOP_BUTTON_STATE ButtonGetState(HANDLE Window);
static BOOL ButtonEnsureState(HANDLE Window);
static void ButtonDeleteState(HANDLE Window);
static void ButtonSetHoverState(HANDLE Window, U32 Value);
static void ButtonSetPressedState(HANDLE Window, U32 Value);
static BOOL ButtonResolvePointerPosition(HANDLE Window, I32* PointerX, I32* PointerY);

/***************************************************************************/

/**
 * @brief Get one button private state from one window.
 * @param Window Target button window.
 * @return State pointer or NULL.
 */
static LPDESKTOP_BUTTON_STATE ButtonGetState(HANDLE Window) {
    return (LPDESKTOP_BUTTON_STATE)(LPVOID)(LINEAR)GetWindowProp(Window, DESKTOP_BUTTON_PROP_STATE);
}

/***************************************************************************/

/**
 * @brief Allocate button private state when absent.
 * @param Window Target button window.
 * @return TRUE on success.
 */
static BOOL ButtonEnsureState(HANDLE Window) {
    LPDESKTOP_BUTTON_STATE State;

    State = ButtonGetState(Window);
    if (State != NULL) return TRUE;

    State = (LPDESKTOP_BUTTON_STATE)malloc(sizeof(DESKTOP_BUTTON_STATE));
    if (State == NULL) return FALSE;

    memset(State, 0, sizeof(DESKTOP_BUTTON_STATE));
    (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_STATE, (UINT)(LINEAR)State);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release button private state.
 * @param Window Target button window.
 */
static void ButtonDeleteState(HANDLE Window) {
    LPDESKTOP_BUTTON_STATE State;

    State = ButtonGetState(Window);
    if (State == NULL) return;

    free(State);
    (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_STATE, 0);
}

/***************************************************************************/

/**
 * @brief Update button hover state and request redraw when changed.
 * @param Window Target button window.
 * @param Value New hover state.
 */
static void ButtonSetHoverState(HANDLE Window, U32 Value) {
    LPDESKTOP_BUTTON_STATE State;

    if (ButtonEnsureState(Window) == FALSE) return;
    State = ButtonGetState(Window);
    if (State == NULL || State->Hover == Value) return;

    State->Hover = Value;
    (void)InvalidateClientRect(Window, NULL);
}

/***************************************************************************/

/**
 * @brief Update button pressed state and request redraw when changed.
 * @param Window Target button window.
 * @param Value New pressed state.
 */
static void ButtonSetPressedState(HANDLE Window, U32 Value) {
    LPDESKTOP_BUTTON_STATE State;

    if (ButtonEnsureState(Window) == FALSE) return;
    State = ButtonGetState(Window);
    if (State == NULL || State->Pressed == Value) return;

    State->Pressed = Value;
    (void)InvalidateClientRect(Window, NULL);
}

/***************************************************************************/

/**
 * @brief Resolve whether one local point is inside one window rectangle.
 * @param Window Target window handle.
 * @param WindowX Window-relative X coordinate.
 * @param WindowY Window-relative Y coordinate.
 * @return TRUE when the point is inside the window rectangle.
 */
static BOOL ButtonIsPointInside(HANDLE Window, I32 WindowX, I32 WindowY) {
    RECT WindowRect;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Window == NULL) return FALSE;
    if (GetWindowRect(Window, &WindowRect) == FALSE) return FALSE;

    WindowWidth = WindowRect.X2 - WindowRect.X1 + 1;
    WindowHeight = WindowRect.Y2 - WindowRect.Y1 + 1;
    if (WindowWidth <= 0 || WindowHeight <= 0) return FALSE;

    return WindowX >= 0 && WindowX < WindowWidth && WindowY >= 0 && WindowY < WindowHeight;
}

/***************************************************************************/

/**
 * @brief Resolve the latest button-relative pointer coordinates.
 * @param Window Target button window.
 * @param PointerX Receives X coordinate.
 * @param PointerY Receives Y coordinate.
 * @return TRUE when coordinates are available.
 */
static BOOL ButtonResolvePointerPosition(HANDLE Window, I32* PointerX, I32* PointerY) {
    LPDESKTOP_BUTTON_STATE State;

    if (Window == NULL || PointerX == NULL || PointerY == NULL) return FALSE;

    State = ButtonGetState(Window);
    if (State == NULL || State->HasPointerPosition == 0) return FALSE;

    *PointerX = State->PointerX;
    *PointerY = State->PointerY;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve the themed background token for one button state.
 * @param Window Target button window.
 * @return One THEME_TOKEN_WINDOW_BACKGROUND_* identifier.
 */
static U32 ButtonResolveBackgroundToken(HANDLE Window) {
    LPDESKTOP_BUTTON_STATE State;

    State = ButtonGetState(Window);
    if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED;
    if (State != NULL && State->Pressed != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_PRESSED;
    if (State != NULL && State->Hover != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_HOVER;

    return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_NORMAL;
}

/***************************************************************************/

/**
 * @brief Draw centered button caption.
 * @param Window Button window handle.
 * @param GC Target graphics context.
 * @param ClientRect Button client rectangle.
 */
static void ButtonDrawCaption(HANDLE Window, HANDLE GC, LPRECT ClientRect) {
    TEXT_MEASURE_INFO MeasureInfo;
    TEXT_DRAW_INFO DrawInfo;
    STR Caption[DESKTOP_BUTTON_CAPTION_BUFFER_SIZE];
    U32 ThemeToken;
    I32 TextX;
    I32 TextY;

    if (Window == NULL || GC == NULL || ClientRect == NULL) return;
    if (GetWindowCaption(Window, Caption, sizeof(Caption)) == FALSE) return;
    if (Caption[0] == STR_NULL) return;

    MeasureInfo = (TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Caption,
        .Font = NULL,
        .Width = 0,
        .Height = 0};
    (void)MeasureText(&MeasureInfo);

    TextX = ClientRect->X1 + (((ClientRect->X2 - ClientRect->X1 + 1) - (I32)MeasureInfo.Width) / 2);
    TextY = ClientRect->Y1 + (((ClientRect->Y2 - ClientRect->Y1 + 1) - (I32)MeasureInfo.Height) / 2);

    ThemeToken = ButtonResolveBackgroundToken(Window);
    if (ThemeToken == THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED) {
        (void)SelectPen(GC, GetSystemPen(SM_COLOR_NORMAL));
    } else {
        (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    }
    (void)SelectBrush(GC, NULL);

    DrawInfo = (TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GC,
        .X = TextX,
        .Y = TextY,
        .Text = Caption,
        .Font = NULL};
    (void)DrawText(&DrawInfo);
}

/***************************************************************************/

/**
 * @brief Dispatch one button click notify to the parent window.
 * @param Window Source button window.
 */
static void ButtonNotifyClicked(HANDLE Window) {
    HANDLE ParentWindow;

    if (Window == NULL) return;

    ParentWindow = GetWindowParent(Window);
    if (ParentWindow == NULL) return;

    (void)PostMessage(ParentWindow, EWM_CLICKED, GetWindowProp(Window, DESKTOP_BUTTON_PROP_NOTIFY_VALUE), 0);
}

/***************************************************************************/

/**
 * @brief Ensure the button window class is registered.
 * @return TRUE on success.
 */
BOOL ButtonEnsureClassRegistered(void) {
    if (FindWindowClass(DESKTOP_BUTTON_WINDOW_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(DESKTOP_BUTTON_WINDOW_CLASS_NAME, 0, NULL, ButtonWindowFunc, 0) != NULL;
}

/***************************************************************************/

/**
 * @brief Create one button child window.
 * @param ParentWindow Parent window handle.
 * @param WindowID Button window identifier.
 * @param WindowRect Initial window rectangle.
 * @param Caption Optional button caption.
 * @return Button window handle on success, NULL on failure.
 */
HANDLE ButtonCreate(HANDLE ParentWindow, U32 WindowID, LPRECT WindowRect, LPCSTR Caption) {
    WINDOW_INFO WindowInfo;
    HANDLE Window;

    if (ParentWindow == NULL || WindowRect == NULL) return NULL;
    if (ButtonEnsureClassRegistered() == FALSE) return NULL;

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ParentWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_BUTTON_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = WindowID;
    WindowInfo.WindowPosition.X = WindowRect->X1;
    WindowInfo.WindowPosition.Y = WindowRect->Y1;
    WindowInfo.WindowSize.X = WindowRect->X2 - WindowRect->X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect->Y2 - WindowRect->Y1 + 1;
    WindowInfo.ShowHide = TRUE;

    Window = (HANDLE)CreateWindow(&WindowInfo);
    if (Window == NULL) return NULL;

    (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_NOTIFY_VALUE, WindowID);
    if (Caption != NULL) {
        (void)SetWindowCaption(Window, Caption);
    }

    return Window;
}

/***************************************************************************/

/**
 * @brief Button window procedure.
 * @param Window Button window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 ButtonWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    HANDLE GraphicsContext;
    LPDESKTOP_BUTTON_STATE State;
    I32 MouseX;
    I32 MouseY;
    BOOL IsInside;
    BOOL WasPressed;

    switch (Message) {
        case EWM_CREATE:
            return ButtonEnsureState(Window);

        case EWM_DELETE:
            (void)ReleaseMouse();
            ButtonDeleteState(Window);
            return 1;

        case EWM_MOUSEDOWN:
            debug("[ButtonWindowFunc] mousedown window=%x buttons=%x",
                (UINT)(LINEAR)Window,
                Param1);
            if ((Param1 & MB_LEFT) == 0) return 1;
            if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) != 0) return 1;
            if (ButtonResolvePointerPosition(Window, &MouseX, &MouseY) == FALSE) return 1;
            IsInside = ButtonIsPointInside(Window, MouseX, MouseY);
            debug("[ButtonWindowFunc] mousedown window=%x local=%d,%d inside=%u",
                (UINT)(LINEAR)Window,
                MouseX,
                MouseY,
                IsInside);
            if (IsInside == FALSE) return 1;
            (void)CaptureMouse(Window);
            ButtonSetHoverState(Window, 1);
            ButtonSetPressedState(Window, 1);
            return 1;

        case EWM_MOUSEMOVE:
            if (ButtonEnsureState(Window) == FALSE) return 1;
            MouseX = SIGNED(Param1);
            MouseY = SIGNED(Param2);
            IsInside = ButtonIsPointInside(Window, MouseX, MouseY);
            State = ButtonGetState(Window);
            if (State == NULL) return 1;

            State->HasPointerPosition = 1;
            State->PointerX = MouseX;
            State->PointerY = MouseY;

            if (State->Pressed != 0) {
                ButtonSetHoverState(Window, IsInside ? 1 : 0);
                ButtonSetPressedState(Window, IsInside ? 1 : 0);
            } else if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) == 0) {
                ButtonSetHoverState(Window, IsInside ? 1 : 0);
            }
            return 1;

        case EWM_MOUSEUP:
            debug("[ButtonWindowFunc] mouseup window=%x buttons=%x",
                (UINT)(LINEAR)Window,
                Param1);
            if ((Param1 & MB_LEFT) == 0) return 1;
            if (ButtonResolvePointerPosition(Window, &MouseX, &MouseY) == FALSE) return 1;
            IsInside = ButtonIsPointInside(Window, MouseX, MouseY);
            State = ButtonGetState(Window);
            WasPressed = (State != NULL && State->Pressed != 0);
            debug("[ButtonWindowFunc] mouseup window=%x local=%d,%d inside=%u pressed=%u",
                (UINT)(LINEAR)Window,
                MouseX,
                MouseY,
                IsInside,
                WasPressed);

            (void)ReleaseMouse();
            ButtonSetPressedState(Window, 0);
            ButtonSetHoverState(Window, IsInside ? 1 : 0);

            if (WasPressed != FALSE && IsInside != FALSE && GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) == 0) {
                ButtonNotifyClicked(Window);
            }
            return 1;

        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, ButtonResolveBackgroundToken(Window), Param2);

            GraphicsContext = BeginWindowDraw(Window);
            if (GraphicsContext == NULL) return 1;
            if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
                (void)EndWindowDraw(Window);
                return 1;
            }

            ButtonDrawCaption(Window, GraphicsContext, &ClientRect);
            (void)EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
