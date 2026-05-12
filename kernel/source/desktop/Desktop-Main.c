
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


    Desktop

\************************************************************************/

#include "system/Clock.h"
#include "Desktop-Cursor.h"
#include "Desktop-Dispatcher.h"
#include "Desktop-ModeSelector.h"
#include "Desktop-Private.h"
#include "Desktop-ThemeTokens.h"
#include "Desktop.h"
#include "DisplaySession.h"
#include "core/DriverGetters.h"
#include "GFX.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "console/Console.h"
#include "process/Task-Messaging.h"

/***************************************************************************/

extern DRIVER ConsoleDriver;

/***************************************************************************/

#define ROOT_WINDOW_CLASS_NAME TEXT("RootWindowClass")

/***************************************************************************/

U32 DesktopWindowFunc(HANDLE, U32, U32, U32);

/**
 * @brief Update the desktop root window rectangle from a size.
 * @param Desktop Desktop to update.
 * @param Width New width in pixels/cells.
 * @param Height New height in pixels/cells.
 */
static void UpdateDesktopWindowRect(LPDESKTOP Desktop, I32 Width, I32 Height) {
    RECT Rect;

    if (Width <= 0 || Height <= 0) return;

    Rect.X1 = 0;
    Rect.Y1 = 0;
    Rect.X2 = Width - 1;
    Rect.Y2 = Height - 1;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
            (void)DesktopUpdateWindowScreenRectAndDirtyRegion(Desktop->Window, &Rect);
        }
    }
}

/***************************************************************************/

/**
 * @brief Check whether one desktop already owns one persisted display selection.
 * @param Desktop Desktop instance to inspect.
 * @return TRUE when backend alias and mode are valid.
 */
static BOOL DesktopHasDisplaySelection(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    if (Desktop->DisplaySelection.IsAssigned == FALSE) {
        return FALSE;
    }

    if (StringLength(Desktop->DisplaySelection.BackendAlias) == 0) {
        return FALSE;
    }

    return DesktopIsValidGraphicsModeInfo(&Desktop->DisplaySelection.ModeInfo);
}

/***************************************************************************/

/**
 * @brief Persist one backend and mode selection on the desktop.
 * @param Desktop Desktop receiving the persisted selection.
 * @param BackendAlias Selected backend alias.
 * @param ModeInfo Selected graphics mode.
 */
static void DesktopSetDisplaySelection(LPDESKTOP Desktop, LPCSTR BackendAlias, LPGRAPHICS_MODE_INFO ModeInfo) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP || BackendAlias == NULL || ModeInfo == NULL) {
        return;
    }

    if (StringLength(BackendAlias) == 0 || DesktopIsValidGraphicsModeInfo(ModeInfo) == FALSE) {
        return;
    }

    MemorySet(&(Desktop->DisplaySelection), 0, sizeof(Desktop->DisplaySelection));
    StringCopy(Desktop->DisplaySelection.BackendAlias, BackendAlias);
    Desktop->DisplaySelection.ModeInfo = *ModeInfo;
    Desktop->DisplaySelection.ModeInfo.ModeIndex = INFINITY;
    Desktop->DisplaySelection.IsAssigned = TRUE;
}

/***************************************************************************/

/**
 * @brief Apply the desktop persisted backend and mode selection.
 * @param Desktop Desktop owning the selection.
 * @param AppliedModeInfo Receives the effective graphics mode.
 * @return TRUE when the stored backend and mode were applied successfully.
 */
static BOOL DesktopApplyDisplaySelection(LPDESKTOP Desktop, LPGRAPHICS_MODE_INFO AppliedModeInfo) {
    GRAPHICS_MODE_INFO RequestedModeInfo;
    GRAPHICS_MODE_INFO QueriedModeInfo;
    UINT ModeSetResult;

    if (DesktopHasDisplaySelection(Desktop) == FALSE || AppliedModeInfo == NULL) {
        return FALSE;
    }

    if (GraphicsSelectorForceBackendByName(Desktop->DisplaySelection.BackendAlias) == FALSE) {
        WARNING(TEXT("Stored backend unavailable (%s)"),
            Desktop->DisplaySelection.BackendAlias);
        return FALSE;
    }

    RequestedModeInfo = Desktop->DisplaySelection.ModeInfo;
    ModeSetResult = GetGraphicsDriver()->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
    if (ModeSetResult != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Stored mode apply failed (%u)"), ModeSetResult);
        return FALSE;
    }

    DesktopInitializeGraphicsModeInfo(&QueriedModeInfo, INFINITY, 0, 0, 0);
    if (GetGraphicsDriver()->Command(DF_GFX_GETMODEINFO, (UINT)&QueriedModeInfo) == DF_RETURN_SUCCESS &&
        DesktopIsValidGraphicsModeInfo(&QueriedModeInfo) != FALSE) {
        *AppliedModeInfo = QueriedModeInfo;
    } else {
        *AppliedModeInfo = RequestedModeInfo;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate that the active graphics backend can provide one drawing context.
 * @param GraphicsDriver Active graphics driver.
 * @return TRUE when the backend exposes one usable graphics context.
 */
static BOOL DesktopEnsureGraphicsContextAvailable(LPDRIVER GraphicsDriver) {
    UINT ContextPointer = 0;
    LPGRAPHICSCONTEXT Context = NULL;

    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        return FALSE;
    }

    ContextPointer = GraphicsDriver->Command(DF_GFX_GETCONTEXT, 0);
    if (!IS_VALID_KERNEL_POINTER((LPVOID)(UINT)ContextPointer)) {
        return FALSE;
    }

    Context = (LPGRAPHICSCONTEXT)(LPVOID)ContextPointer;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release the desktop-owned graphics shadow buffer and context.
 * @param Desktop Target desktop.
 */
static void DesktopReleaseGraphicsShadowBuffer(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return;
    }

    if (Desktop->GraphicsShadowBufferLinear != 0 && Desktop->GraphicsShadowBufferSize != 0) {
        FreeRegion(Desktop->GraphicsShadowBufferLinear, Desktop->GraphicsShadowBufferSize);
    }
    if (Desktop->GraphicsContext != NULL) {
        KernelHeapFree(Desktop->GraphicsContext);
    }

    Desktop->GraphicsShadowBufferLinear = 0;
    Desktop->GraphicsShadowBufferSize = 0;
    Desktop->GraphicsContext = NULL;
}

/***************************************************************************/

/**
 * @brief Create or resize the desktop-owned graphics shadow buffer.
 * @param Desktop Target desktop.
 * @param DriverContext Active backend scanout context.
 * @return TRUE on success.
 */
static BOOL DesktopEnsureGraphicsShadowBuffer(LPDESKTOP Desktop, LPGRAPHICSCONTEXT DriverContext) {
    UINT RequiredSize = 0;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    if (DriverContext == NULL || DriverContext->TypeID != KOID_GRAPHICSCONTEXT || DriverContext->MemoryBase == NULL) {
        return FALSE;
    }

    RequiredSize = (UINT)(DriverContext->BytesPerScanLine * (U32)DriverContext->Height);
    if (RequiredSize == 0) {
        return FALSE;
    }

    if (Desktop->GraphicsShadowBufferLinear != 0 && Desktop->GraphicsShadowBufferSize != RequiredSize) {
        DesktopReleaseGraphicsShadowBuffer(Desktop);
    }

    if (Desktop->GraphicsShadowBufferLinear == 0) {
        Desktop->GraphicsShadowBufferLinear = AllocRegion(
            VMA_KERNEL,
            0,
            RequiredSize,
            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
            TEXT("DesktopGraphicsShadow"));
        if (Desktop->GraphicsShadowBufferLinear == 0) {
            ERROR(TEXT("AllocRegion failed size=%u"), RequiredSize);
            return FALSE;
        }

        Desktop->GraphicsShadowBufferSize = RequiredSize;
    }

    if (Desktop->GraphicsContext == NULL) {
        Desktop->GraphicsContext = (LPGRAPHICSCONTEXT)KernelHeapAlloc(sizeof(GRAPHICSCONTEXT));
        if (Desktop->GraphicsContext == NULL) {
            DesktopReleaseGraphicsShadowBuffer(Desktop);
            return FALSE;
        }
    }

    *(Desktop->GraphicsContext) = *DriverContext;
    Desktop->GraphicsContext->Flags |= GRAPHICS_CONTEXT_FLAG_SOFTWARE_ONLY;
    Desktop->GraphicsContext->MemoryBase = (U8*)(LINEAR)Desktop->GraphicsShadowBufferLinear;
    Desktop->GraphicsContext->Driver = Desktop->Graphics;
    Desktop->GraphicsContext->References = 1;
    Desktop->GraphicsContext->OwnerProcess = Desktop->OwnerProcess;
    InitMutex(&(Desktop->GraphicsContext->Mutex));
    MemorySet(Desktop->GraphicsContext->MemoryBase, 0, RequiredSize);
    return TRUE;
}

/***************************************************************************/

BRUSH Brush_Desktop = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_High = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Normal = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_HiShadow = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_LoShadow = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Client = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Text_Normal = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Text_Select = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Selection = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Bar = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Bar_2 = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Text = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };

/***************************************************************************/

PEN Pen_Desktop = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_High = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Normal = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_HiShadow = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_LoShadow = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Client = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Text_Normal = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Text_Select = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Selection = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Bar = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Bar_2 = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Text = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };

/***************************************************************************/

/**
 * @brief Reset a graphics context to its default state.
 * @param This Graphics context to reset.
 * @return TRUE on success.
 */
BOOL ResetGraphicsContext(LPGRAPHICSCONTEXT This) {
    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    //-------------------------------------
    // Lock access to the context

    LockMutex(&(This->Mutex), INFINITY);

    DesktopThemeSyncSystemObjects();

    This->LoClip.X = 0;
    This->LoClip.Y = 0;
    This->HiClip.X = This->Width - 1;
    This->HiClip.Y = This->Height - 1;

    This->Brush = &Brush_Normal;
    This->Pen = &Pen_Text_Normal;
    This->Font = NULL;
    This->Bitmap = NULL;

    //-------------------------------------
    // Unlock access to the context

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Comparison routine for sorting desktops by order.
 * @param Item1 First desktop pointer.
 * @param Item2 Second desktop pointer.
 * @return Difference of desktop orders.
 */
static I32 SortDesktops_Order(LPCVOID Item1, LPCVOID Item2) {
    LPDESKTOP* Ptr1 = (LPDESKTOP*)Item1;
    LPDESKTOP* Ptr2 = (LPDESKTOP*)Item2;
    LPDESKTOP Dsk1 = *Ptr1;
    LPDESKTOP Dsk2 = *Ptr2;

    return (Dsk1->Order - Dsk2->Order);
}

/***************************************************************************/

/**
 * @brief Resolve the process that should receive focus when one desktop activates.
 * @param Desktop Desktop being shown.
 * @return User process owning the desktop task, or NULL when desktop activation
 *         should not change focused process.
 */
static LPPROCESS DesktopResolveActivationFocusProcess(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    SAFE_USE_VALID_ID(Desktop->Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Desktop->Task->OwnerProcess, KOID_PROCESS) {
            if (Desktop->Task->OwnerProcess->Privilege == CPU_PRIVILEGE_USER) {
                return Desktop->Task->OwnerProcess;
            }
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Create a new desktop and its main window.
 * @return Pointer to the created desktop or NULL on failure.
 */
LPDESKTOP KernelCreateDesktop(LPWINDOW RootWindow) {
    LPDESKTOP This;
    WINDOW_INFO WindowInfo;
    LPDESKTOP PreviousDesktop;

    This = (LPDESKTOP)CreateKernelObject(sizeof(DESKTOP), KOID_DESKTOP);
    if (This == NULL) return NULL;

    InitMutex(&(This->Mutex));
    InitMutex(&(This->TimerMutex));
    This->Timers = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (This->Timers == NULL) {
        ReleaseKernelObject(This);
        return NULL;
    }

    This->Task = GetCurrentTask();
    SAFE_USE_VALID_ID(This->Task, KOID_TASK) { This->OwnerProcess = This->Task->OwnerProcess; }
    if (EnsureAllMessageQueues(This->Task, TRUE) == FALSE) {
        DeleteList(This->Timers);
        ReleaseKernelObject(This);
        return NULL;
    }
    This->Graphics = &ConsoleDriver;
    This->Mode = DESKTOP_MODE_CONSOLE;

    if (RootWindow == NULL && DesktopEnsureDispatcherTask(This) == FALSE) {
        DeleteList(This->Timers);
        ReleaseKernelObject(This);
        return NULL;
    }

    PreviousDesktop = GetCurrentProcess()->Desktop;
    GetCurrentProcess()->Desktop = This;

    if (RootWindow != NULL) {
        This->Window = RootWindow;
        (void)DesktopSetWindowTask(RootWindow, This->Task);

        LockMutex(&(RootWindow->Mutex), INFINITY);
        RootWindow->ParentWindow = NULL;
        UnlockMutex(&(RootWindow->Mutex));
    } else {
        WindowInfo.Header.Size = sizeof(WINDOW_INFO);
        WindowInfo.Header.Version = EXOS_ABI_VERSION;
        WindowInfo.Header.Flags = 0;
        WindowInfo.Window = NULL;
        WindowInfo.Parent = NULL;
        if (FindWindowClass(ROOT_WINDOW_CLASS_NAME) == NULL &&
            RegisterWindowClass(ROOT_WINDOW_CLASS_NAME, 0, NULL, DesktopWindowFunc, 0) == NULL) {
            GetCurrentProcess()->Desktop = PreviousDesktop;
            DeleteList(This->Timers);
            ReleaseKernelObject(This);
            return NULL;
        }

        WindowInfo.WindowClass = 0;
        WindowInfo.WindowClassName = ROOT_WINDOW_CLASS_NAME;
        WindowInfo.Function = NULL;
        WindowInfo.Style = EWS_BARE_SURFACE;
        WindowInfo.ID = 0;
        WindowInfo.WindowPosition.X = 0;
        WindowInfo.WindowPosition.Y = 0;
        WindowInfo.WindowSize.X = (I32)Console.Width;
        WindowInfo.WindowSize.Y = (I32)Console.Height;
        WindowInfo.ShowHide = TRUE;

        This->Window = DesktopCreateWindow(&WindowInfo);
    }

    if (This->Window == NULL) {
        GetCurrentProcess()->Desktop = PreviousDesktop;
        ReleaseKernelObject(This);
        return NULL;
    }

    // A desktop and its root window are shared session anchors, not private
    // resources of the shell process that created them.
    This->OwnerProcess = NULL;
    if (RootWindow == NULL) {
        This->Window->OwnerProcess = NULL;
    }

    UpdateDesktopWindowRect(This, (I32)Console.Width, (I32)Console.Height);

    //-------------------------------------
    // Add the desktop to the kernel's list

    LockMutex(MUTEX_KERNEL, INFINITY);

    LPLIST DesktopList = GetDesktopList();
    ListAddHead(DesktopList, This);

    // Process already points to this desktop

    UnlockMutex(MUTEX_KERNEL);

    return This;
}

/***************************************************************************/

/**
 * @brief Delete a desktop and release its resources.
 * @param This Desktop to delete.
 */
BOOL DeleteDesktop(LPDESKTOP This) {
    if (This == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    DesktopReleaseGraphicsShadowBuffer(This);

    SAFE_USE(This->Timers) {
        DeleteList(This->Timers);
        This->Timers = NULL;
    }

    SAFE_USE_VALID_ID(This->Window, KOID_WINDOW) { DesktopDeleteWindow(This->Window); }

    ReleaseKernelObject(This);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Display a desktop by setting the graphics mode and ordering.
 * @param This Desktop to show.
 * @return TRUE on success.
 */
BOOL KernelShowDesktop(LPDESKTOP This) {
    GRAPHICS_MODE_INFO ModeInfo;
    GRAPHICS_MODE_INFO QueriedModeInfo;
    GRAPHICS_MODE_INFO SelectedModeInfo;
    GRAPHICS_MODE_INFO RequestedModeInfo;
    UINT ModeSetResult;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    I32 Order;
    LPDRIVER SelectedBackendDriver;
    LPGRAPHICSCONTEXT DriverContext;
    UINT ContextPointer;
    BOOL ModeReady;
    BOOL HasSelectedMode;
    BOOL UsedLegacyAutoSelect;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_DESKTOP) return FALSE;

    (void)DesktopEnsureDispatcherTask(This);

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_KERNEL, INFINITY);
    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Sort the kernel's desktop list

    LPLIST DesktopList = GetDesktopList();
    for (Node = DesktopList->First, Order = 1; Node; Node = Node->Next) {
        Desktop = (LPDESKTOP)Node;
        if (Desktop == This)
            Desktop->Order = 0;
        else
            Desktop->Order = Order++;
    }

    ListSort(DesktopList, SortDesktops_Order);

    This->Graphics = GetGraphicsDriver();
    if (This->Graphics == NULL || This->Graphics->Command == NULL) {
        WARNING(TEXT("Graphics driver unavailable"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    if (DesktopApplyDisplaySelection(This, &ModeInfo) != FALSE) {
        This->Graphics = GetGraphicsDriver();
        DEBUG(TEXT("Applied stored display selection backend=%s mode=%ux%ux%u"),
            GraphicsSelectorGetActiveBackendName(),
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
    } else {
        HasSelectedMode = DesktopSelectGraphicsMode(This->Graphics, &SelectedModeInfo);
        UsedLegacyAutoSelect = FALSE;

        if (HasSelectedMode != FALSE) {
            RequestedModeInfo = SelectedModeInfo;
            ModeSetResult = This->Graphics->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
            if (ModeSetResult == DF_RETURN_SUCCESS) {
                ModeInfo = RequestedModeInfo;
                DEBUG(TEXT("Selected mode applied backend=%s mode=%ux%ux%u"),
                    GraphicsSelectorGetActiveBackendName(),
                    ModeInfo.Width,
                    ModeInfo.Height,
                    ModeInfo.BitsPerPixel);
            } else {
                WARNING(TEXT("DF_GFX_SETMODE selected mode failed (%u)"), ModeSetResult);
            }
        }

        if (HasSelectedMode == FALSE || ModeSetResult != DF_RETURN_SUCCESS) {
            // Legacy fallback while all backends migrate to full mode enumeration.
            DesktopInitializeGraphicsModeInfo(&RequestedModeInfo, INFINITY, 0, 0, 0);
            ModeSetResult = This->Graphics->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
            if (ModeSetResult != DF_RETURN_SUCCESS) {
                WARNING(TEXT("DF_GFX_SETMODE legacy auto-select failed (%u)"), ModeSetResult);
            } else {
                ModeInfo = RequestedModeInfo;
                UsedLegacyAutoSelect = TRUE;
                DEBUG(TEXT("Legacy mode applied backend=%s mode=%ux%ux%u"),
                    GraphicsSelectorGetActiveBackendName(),
                    ModeInfo.Width,
                    ModeInfo.Height,
                    ModeInfo.BitsPerPixel);
            }
        }

        if (UsedLegacyAutoSelect != FALSE) {
        }
    }

    DesktopInitializeGraphicsModeInfo(&QueriedModeInfo, INFINITY, 0, 0, 0);

    if (This->Graphics->Command(DF_GFX_GETMODEINFO, (UINT)&QueriedModeInfo) == DF_RETURN_SUCCESS &&
        QueriedModeInfo.Width != 0 && QueriedModeInfo.Height != 0) {
        ModeInfo = QueriedModeInfo;
    }

    ModeReady = DesktopIsValidGraphicsModeInfo(&ModeInfo);
    if (ModeReady == FALSE) {
        WARNING(TEXT("No valid graphics mode available after selection"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    if (DesktopEnsureGraphicsContextAvailable(This->Graphics) == FALSE) {
        WARNING(TEXT("Active graphics backend cannot provide a drawing context"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    ContextPointer = This->Graphics->Command(DF_GFX_GETCONTEXT, 0);
    DriverContext = (LPGRAPHICSCONTEXT)(LPVOID)ContextPointer;
    if (!IS_VALID_KERNEL_POINTER((LPVOID)DriverContext) || DriverContext->TypeID != KOID_GRAPHICSCONTEXT ||
        DesktopEnsureGraphicsShadowBuffer(This, DriverContext) == FALSE) {
        WARNING(TEXT("Unable to prepare desktop shadow buffer"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    SelectedBackendDriver = GraphicsSelectorGetActiveBackendDriver();
    if (SelectedBackendDriver != NULL && StringLength(SelectedBackendDriver->Alias) != 0) {
        DEBUG(TEXT("Final backend=%s alias=%s mode=%ux%ux%u"),
            SelectedBackendDriver->Product,
            SelectedBackendDriver->Alias,
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
        DesktopSetDisplaySelection(This, SelectedBackendDriver->Alias, &ModeInfo);
    }

    if (DisplaySessionSetDesktopMode(
            This,
            (SelectedBackendDriver != NULL) ? SelectedBackendDriver : This->Graphics,
            &ModeInfo) == FALSE) {
        WARNING(TEXT("Unable to activate desktop display session"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    This->Mode = DESKTOP_MODE_GRAPHICS;
    UpdateDesktopWindowRect(This, (I32)ModeInfo.Width, (I32)ModeInfo.Height);
    // PostMessage((HANDLE) This->Window, EWM_DRAW, 0, 0);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    UnlockMutex(MUTEX_KERNEL);

    {
        LPPROCESS FocusProcess;

        FocusProcess = DesktopResolveActivationFocusProcess(This);
        if (FocusProcess != NULL) {
            SetFocusedProcess(FocusProcess);
        } else {
            SetActiveDesktop(This);
        }
    }

    DesktopCursorOnDesktopActivated(This);
    //-------------------------------------
    // Force the desktop root window to repaint

    SAFE_USE_VALID_ID(This->Window, KOID_WINDOW) { InvalidateWindowRect((HANDLE)This->Window, NULL); }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve the desktop screen rectangle for the current mode.
 * @param Desktop Desktop to query.
 * @param Rect Output rectangle.
 * @return TRUE if the rectangle is returned, FALSE otherwise.
 */
BOOL GetDesktopScreenRect(LPDESKTOP Desktop, LPRECT Rect) {
    LPWINDOW RootWindow;

    if (Rect == NULL) return FALSE;
    if (DesktopGetRootWindow(Desktop, &RootWindow) == FALSE) return FALSE;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        LockMutex(&(Desktop->Mutex), INFINITY);

        if (Desktop->Mode == DESKTOP_MODE_CONSOLE) {
            if (Console.Width == 0 || Console.Height == 0) {
                UnlockMutex(&(Desktop->Mutex));
                return FALSE;
            }
            Rect->X1 = 0;
            Rect->Y1 = 0;
            Rect->X2 = (I32)Console.Width - 1;
            Rect->Y2 = (I32)Console.Height - 1;
            UnlockMutex(&(Desktop->Mutex));
            return TRUE;
        }

        UnlockMutex(&(Desktop->Mutex));
    }

    return GetWindowScreenRectSnapshot(RootWindow, Rect);
}
/***************************************************************************/
