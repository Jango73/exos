
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


    Portal - Desktop manager & file system display

\************************************************************************/

#include "../../../runtime/include/exos/exos.h"

/************************************************************************/

HANDLE MainWindow = NULL;
HANDLE RedPen = NULL;
HANDLE RedBrush = NULL;
HANDLE GreenPen = NULL;
HANDLE GreenBrush = NULL;
HANDLE PortalDesktopHandle = NULL;
HANDLE PortalDesktopWindow = NULL;

int main(int argc, char** argv);

static STR Prop_Over[] = "OVER";
static STR Prop_Down[] = "DOWN";

// static void* TestFuncPtr = (void*)main;

/************************************************************************/

void DrawFrame3D(HANDLE GC, LPRECT Rect, BOOL Invert, BOOL Fill) {
    LINE_INFO LineInfo;

    LineInfo.Header.Size = sizeof(LINE_INFO);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GC;

    if (Fill == TRUE) {
        SelectPen(GC, NULL);
        SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
        Rectangle(GC, Rect->X1, Rect->Y1, Rect->X2, Rect->Y2, 0);
    }
    if (Invert == FALSE) {
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y2;
        LineInfo.X2 = Rect->X1;
        LineInfo.Y2 = Rect->Y1;
        (void)Line(&LineInfo);
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y1;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y1;
        (void)Line(&LineInfo);
        SelectPen(GC, GetSystemPen(SM_COLOR_DARK_SHADOW));
        LineInfo.X1 = Rect->X2;
        LineInfo.Y1 = Rect->Y1;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y2;
        (void)Line(&LineInfo);
        LineInfo.X1 = Rect->X2;
        LineInfo.Y1 = Rect->Y2;
        LineInfo.X2 = Rect->X1;
        LineInfo.Y2 = Rect->Y2;
        (void)Line(&LineInfo);
        SelectPen(GC, GetSystemPen(SM_COLOR_LIGHT_SHADOW));
        LineInfo.X1 = Rect->X2 - 1;
        LineInfo.Y1 = Rect->Y1 + 1;
        LineInfo.X2 = Rect->X2 - 1;
        LineInfo.Y2 = Rect->Y2 - 1;
        (void)Line(&LineInfo);
        LineInfo.X1 = Rect->X2 - 1;
        LineInfo.Y1 = Rect->Y2 - 1;
        LineInfo.X2 = Rect->X1 + 1;
        LineInfo.Y2 = Rect->Y2 - 1;
        (void)Line(&LineInfo);
    } else {
        SelectPen(GC, GetSystemPen(SM_COLOR_DARK_SHADOW));
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y2;
        LineInfo.X2 = Rect->X1;
        LineInfo.Y2 = Rect->Y1;
        (void)Line(&LineInfo);
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y1;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y1;
        (void)Line(&LineInfo);
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
        LineInfo.X1 = Rect->X2;
        LineInfo.Y1 = Rect->Y1;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y2;
        (void)Line(&LineInfo);
        LineInfo.X1 = Rect->X2;
        LineInfo.Y1 = Rect->Y2;
        LineInfo.X2 = Rect->X1;
        LineInfo.Y2 = Rect->Y2;
        (void)Line(&LineInfo);
    }
}

/************************************************************************/

U32 OnButtonCreate(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    SetWindowProp(Window, Prop_Down, 0);
    SetWindowProp(Window, Prop_Over, 0);

    return 0;
}

/***************************************************************************/

U32 OnButtonLeftButtonDown(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    SetWindowProp(Window, Prop_Down, 1);
    InvalidateClientRect(Window, NULL);

    return 0;
}

/***************************************************************************/

U32 OnButtonLeftButtonUp(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    InvalidateClientRect(Window, NULL);
    SetWindowProp(Window, Prop_Down, 0);

    /*
      if (GetProp(Window, Prop_Over))
      {
    U32 ID = GetWindowID(Window);
    PostMessage(GetWindowParent(Window), EWM_COMMAND, MAKEU32(ID,
      BN_CLICKED), (U32) Window);
      }
    */

    SetWindowProp(Window, Prop_Over, 0);
    ReleaseMouse();

    return 0;
}

/***************************************************************************/

U32 OnButtonMouseMove(HANDLE Window, U32 Param1, U32 Param2) {
    RECT Rect;
    POINT Size;
    POINT Mouse;

    GetWindowRect(Window, &Rect);

    Size.X = (Rect.X2 - Rect.X1) + 1;
    Size.Y = (Rect.Y2 - Rect.Y1) + 1;
    Mouse.X = SIGNED(Param1);
    Mouse.Y = SIGNED(Param2);

    if (Mouse.X >= 0 && Mouse.Y >= 0 && Mouse.X <= Size.X && Mouse.Y <= Size.Y) {
        if (!GetWindowProp(Window, Prop_Over)) {
            InvalidateClientRect(Window, NULL);
            SetWindowProp(Window, Prop_Over, 1);
            CaptureMouse(Window);
        }
    } else {
        if (GetWindowProp(Window, Prop_Over)) {
            InvalidateClientRect(Window, NULL);
            SetWindowProp(Window, Prop_Over, 0);
            if (!GetWindowProp(Window, Prop_Down)) ReleaseMouse();
        }
    }

    return 0;
}

/***************************************************************************/

U32 OnButtonDraw(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    RECT Rect;

    HANDLE GC = GetWindowGC(Window);

    // if (GC = BeginWindowDraw(Window))
    if (GC != NULL) {
        GetWindowRect(Window, &Rect);

        if (GetWindowProp(Window, Prop_Down)) {
            DrawFrame3D(GC, &Rect, 1, TRUE);
        } else {
            DrawFrame3D(GC, &Rect, 0, TRUE);
        }

        ReleaseWindowGC(Window);
    }

    return 0;
}

/***************************************************************************/

U32 ButtonFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            return OnButtonCreate(Window, Param1, Param2);
        case EWM_DRAW:
            return OnButtonDraw(Window, Param1, Param2);

        case EWM_MOUSEDOWN: {
            switch (Param1) {
                case MB_LEFT:
                    return OnButtonLeftButtonDown(Window, Param1, Param2);
            }
        } break;

        case EWM_MOUSEUP: {
            switch (Param1) {
                case MB_LEFT:
                    return OnButtonLeftButtonUp(Window, Param1, Param2);
            }
        } break;

        default:
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}

/***************************************************************************/

U32 MainWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE: {
        } break;

        case EWM_DELETE: {
        } break;

        case EWM_DRAW: {
            RECT Rect;
            HANDLE GC = GetWindowGC(Window);

            if (GC != NULL) {
                GetWindowRect(Window, &Rect);

                DrawFrame3D(GC, &Rect, 0, FALSE);

                Rect.X1++;
                Rect.Y1++;
                Rect.X2--;
                Rect.Y2--;

                SelectPen(GC, NULL);

                SelectBrush(GC, GetSystemBrush(SM_COLOR_TITLE_BAR));
                Rectangle(GC, Rect.X1, Rect.Y1, Rect.X2, Rect.Y1 + 19, 0);

                SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
                Rectangle(GC, Rect.X1, Rect.Y1 + 20, Rect.X2, Rect.Y2, 0);

                ReleaseWindowGC(GC);
            } else {
            }
        } break;

        default:
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}

/************************************************************************/

U32 DesktopTask(LPVOID Param) {
    UNUSED(Param);

    HANDLE Window;
    POINT MousePos;
    POINT NewMousePos;
    U32 MouseButtons;
    U32 NewMouseButtons;

    PortalDesktopHandle = GetCurrentDesktop();

    if (PortalDesktopHandle == NULL) {
        PortalDesktopHandle = CreateDesktop();
    }

    if (PortalDesktopHandle == NULL) {
        return MAX_U32;
    }

    PortalDesktopWindow = GetDesktopWindow(PortalDesktopHandle);
    if (PortalDesktopWindow == NULL) {
        return MAX_U32;
    }

    if (ShowDesktop(PortalDesktopHandle) == FALSE) {
        return MAX_U32;
    }

    Window = PortalDesktopWindow;

    MousePos.X = 0;
    MousePos.Y = 0;
    MouseButtons = 0;

    while (1) {
        GetMousePosition(&NewMousePos);

        if (NewMousePos.X != MousePos.X || NewMousePos.Y != MousePos.Y) {
            MousePos.X = NewMousePos.X;
            MousePos.Y = NewMousePos.Y;

            SendMessage(Window, EWM_MOUSEMOVE, UNSIGNED(MousePos.X), UNSIGNED(MousePos.Y));
        }

        NewMouseButtons = GetMouseButtons();

        if (NewMouseButtons != MouseButtons) {
            U32 DownButtons = 0;
            U32 UpButtons = 0;

            if ((MouseButtons & MB_LEFT) != (NewMouseButtons & MB_LEFT)) {
                if (NewMouseButtons & MB_LEFT)
                    DownButtons |= MB_LEFT;
                else
                    UpButtons |= MB_LEFT;
            }

            if ((MouseButtons & MB_RIGHT) != (NewMouseButtons & MB_RIGHT)) {
                if (NewMouseButtons & MB_RIGHT)
                    DownButtons |= MB_RIGHT;
                else
                    UpButtons |= MB_RIGHT;
            }

            if ((MouseButtons & MB_MIDDLE) != (NewMouseButtons & MB_MIDDLE)) {
                if (NewMouseButtons & MB_MIDDLE)
                    DownButtons |= MB_MIDDLE;
                else
                    UpButtons |= MB_MIDDLE;
            }

            MouseButtons = NewMouseButtons;

            // if (DownButtons) PostMessage(Window, EWM_MOUSEDOWN, DownButtons,
            // 0); if (UpButtons)   PostMessage(Window, EWM_MOUSEUP, UpButtons,
            // 0);

            if (DownButtons) SendMessage(Window, EWM_MOUSEDOWN, DownButtons, 0);
            if (UpButtons) SendMessage(Window, EWM_MOUSEUP, UpButtons, 0);
        }

        /*
            if (GC = GetWindowGC(Window))
            {
              Sequence = 1 - Sequence;
              SelectBrush(GC, Sequence ? RedBrush : GreenBrush);
              Rectangle(GC, 20, 20, 40, 40, 0);
              ReleaseWindowGC(GC);
            }
        */
    }

    // DeleteObject(Desktop);
}

/************************************************************************/

BOOL InitApplication(void) {
    TASK_INFO TaskInfo;
    PEN_INFO PenInfo;
    BRUSH_INFO BrushInfo;

    TaskInfo.Header.Size = sizeof(TASK_INFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = DesktopTask;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = 65536;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;
    TaskInfo.Task = NULL;

    if (!ExosIsSuccess(CreateTask(&TaskInfo)) || TaskInfo.Task == NULL) return FALSE;

    Sleep(500);

    PenInfo.Header.Size = sizeof(PEN_INFO);
    PenInfo.Header.Version = EXOS_ABI_VERSION;
    PenInfo.Header.Flags = 0;
    PenInfo.Color = MAKERGB(255, 0, 0);
    PenInfo.Pattern = 0xFFFFFFFF;
    PenInfo.Flags = 0;
    RedPen = CreatePen(&PenInfo);

    BrushInfo.Header.Size = sizeof(BRUSH_INFO);
    BrushInfo.Header.Version = EXOS_ABI_VERSION;
    BrushInfo.Header.Flags = 0;
    BrushInfo.Color = MAKERGB(255, 0, 0);
    BrushInfo.Pattern = 0xFFFFFFFF;
    BrushInfo.Flags = 0;
    RedBrush = CreateBrush(&BrushInfo);

    PenInfo.Color = MAKERGB(0, 255, 0);
    GreenPen = CreatePen(&PenInfo);

    BrushInfo.Color = MAKERGB(0, 255, 0);
    GreenBrush = CreateBrush(&BrushInfo);

    MainWindow = CreateWindowWithClass(NULL, 0, NULL, MainWindowFunc, 0, 0, 100, 100, 400, 300);

    if (MainWindow == NULL) return FALSE;

    CreateWindowWithClass(MainWindow, 0, NULL, ButtonFunc, EWS_VISIBLE, 0, 400 - 90, 300 - 60, 80, 20);
    CreateWindowWithClass(MainWindow, 0, NULL, ButtonFunc, EWS_VISIBLE, 0, 400 - 90, 300 - 30, 80, 20);

    ShowWindow(MainWindow);

    return TRUE;
}

/************************************************************************/

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    MESSAGE Message;

    if (InitApplication() == FALSE) {
        return MAX_U32;
    }

    while (GetMessage(NULL, &Message, 0, 0)) {
        DispatchMessage(&Message);
    }

    return 0;
}
