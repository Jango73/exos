
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


    System call

\************************************************************************/

#include "system/SYSCall.h"

/************************************************************************/

SYSCALL_ENTRY DATA_SECTION SysCallTable[SYSCALL_Last];

/************************************************************************/

void InitializeSystemCallTable(void) {
    U32 Index;

    for (Index = 0; Index < SYSCALL_Last; Index++) {
        SysCallTable[Index].Function = NULL;
        SysCallTable[Index].Privilege = EXOS_PRIVILEGE_USER;
    }

    // Base Services
    SysCallTable[SYSCALL_GetVersion] = (SYSCALL_ENTRY){SysCall_GetVersion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemInfo] = (SYSCALL_ENTRY){SysCall_GetSystemInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetLastError] = (SYSCALL_ENTRY){SysCall_GetLastError, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetLastError] = (SYSCALL_ENTRY){SysCall_SetLastError, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Debug] = (SYSCALL_ENTRY){SysCall_Debug, EXOS_PRIVILEGE_USER};

    // Socket syscalls
    SysCallTable[SYSCALL_SocketCreate] = (SYSCALL_ENTRY){SysCall_SocketCreate, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketShutdown] = (SYSCALL_ENTRY){SysCall_SocketShutdown, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketBind] = (SYSCALL_ENTRY){SysCall_SocketBind, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketListen] = (SYSCALL_ENTRY){SysCall_SocketListen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketAccept] = (SYSCALL_ENTRY){SysCall_SocketAccept, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketConnect] = (SYSCALL_ENTRY){SysCall_SocketConnect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSend] = (SYSCALL_ENTRY){SysCall_SocketSend, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketReceive] = (SYSCALL_ENTRY){SysCall_SocketReceive, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSendTo] = (SYSCALL_ENTRY){SysCall_SocketSendTo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketReceiveFrom] = (SYSCALL_ENTRY){SysCall_SocketReceiveFrom, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketClose] = (SYSCALL_ENTRY){SysCall_SocketClose, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetOption] = (SYSCALL_ENTRY){SysCall_SocketGetOption, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSetOption] = (SYSCALL_ENTRY){SysCall_SocketSetOption, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetPeerName] = (SYSCALL_ENTRY){SysCall_SocketGetPeerName, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetSocketName] = (SYSCALL_ENTRY){SysCall_SocketGetSocketName, EXOS_PRIVILEGE_USER};

    // Time Services
    SysCallTable[SYSCALL_GetSystemTime] = (SYSCALL_ENTRY){SysCall_GetSystemTime, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetLocalTime] = (SYSCALL_ENTRY){SysCall_GetLocalTime, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetLocalTime] = (SYSCALL_ENTRY){SysCall_SetLocalTime, EXOS_PRIVILEGE_USER};

    // Process Services
    SysCallTable[SYSCALL_DeleteObject] = (SYSCALL_ENTRY){SysCall_DeleteObject, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateProcess] = (SYSCALL_ENTRY){SysCall_CreateProcess, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_KillProcess] = (SYSCALL_ENTRY){SysCall_KillProcess, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProcessInfo] = (SYSCALL_ENTRY){SysCall_GetProcessInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProcessMemoryInfo] = (SYSCALL_ENTRY){SysCall_GetProcessMemoryInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProfileInfo] = (SYSCALL_ENTRY){SysCall_GetProfileInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_LoadModule] = (SYSCALL_ENTRY){SysCall_LoadModule, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetModuleSymbol] = (SYSCALL_ENTRY){SysCall_GetModuleSymbol, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReleaseModule] = (SYSCALL_ENTRY){SysCall_ReleaseModule, EXOS_PRIVILEGE_USER};

    // Threading Services
    SysCallTable[SYSCALL_CreateTask] = (SYSCALL_ENTRY){SysCall_CreateTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_KillTask] = (SYSCALL_ENTRY){SysCall_KillTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Exit] = (SYSCALL_ENTRY){SysCall_Exit, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SuspendTask] = (SYSCALL_ENTRY){SysCall_SuspendTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ResumeTask] = (SYSCALL_ENTRY){SysCall_ResumeTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Sleep] = (SYSCALL_ENTRY){SysCall_Sleep, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Wait] = (SYSCALL_ENTRY){SysCall_Wait, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_PostMessage] = (SYSCALL_ENTRY){SysCall_PostMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SendMessage] = (SYSCALL_ENTRY){SysCall_SendMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_PeekMessage] = (SYSCALL_ENTRY){SysCall_PeekMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetMessage] = (SYSCALL_ENTRY){SysCall_GetMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DispatchMessage] = (SYSCALL_ENTRY){SysCall_DispatchMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateMutex] = (SYSCALL_ENTRY){SysCall_CreateMutex, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_LockMutex] = (SYSCALL_ENTRY){SysCall_LockMutex, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_UnlockMutex] = (SYSCALL_ENTRY){SysCall_UnlockMutex, EXOS_PRIVILEGE_USER};

    // Memory Services
    SysCallTable[SYSCALL_AllocRegion] = (SYSCALL_ENTRY){SysCall_AllocRegion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FreeRegion] = (SYSCALL_ENTRY){SysCall_FreeRegion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_IsMemoryValid] = (SYSCALL_ENTRY){SysCall_IsMemoryValid, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProcessHeap] = (SYSCALL_ENTRY){SysCall_GetProcessHeap, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapAlloc] = (SYSCALL_ENTRY){SysCall_HeapAlloc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapFree] = (SYSCALL_ENTRY){SysCall_HeapFree, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapRealloc] = (SYSCALL_ENTRY){SysCall_HeapRealloc, EXOS_PRIVILEGE_USER};

    // File Services
    SysCallTable[SYSCALL_EnumVolumes] = (SYSCALL_ENTRY){SysCall_EnumVolumes, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetVolumeInfo] = (SYSCALL_ENTRY){SysCall_GetVolumeInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_OpenFile] = (SYSCALL_ENTRY){SysCall_OpenFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreatePipe] = (SYSCALL_ENTRY){SysCall_CreatePipe, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReadFile] = (SYSCALL_ENTRY){SysCall_ReadFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_WriteFile] = (SYSCALL_ENTRY){SysCall_WriteFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetFileSize] = (SYSCALL_ENTRY){SysCall_GetFileSize, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetFilePointer] = (SYSCALL_ENTRY){SysCall_GetFilePosition, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetFilePointer] = (SYSCALL_ENTRY){SysCall_SetFilePosition, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FindFirstFile] = (SYSCALL_ENTRY){SysCall_FindFirstFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FindNextFile] = (SYSCALL_ENTRY){SysCall_FindNextFile, EXOS_PRIVILEGE_USER};

    // Console Services
    SysCallTable[SYSCALL_ConsolePeekKey] = (SYSCALL_ENTRY){SysCall_ConsolePeekKey, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetKey] = (SYSCALL_ENTRY){SysCall_ConsoleGetKey, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetKeyModifiers] = (SYSCALL_ENTRY){SysCall_ConsoleGetKeyModifiers, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsolePrint] = (SYSCALL_ENTRY){SysCall_ConsolePrint, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetString] = (SYSCALL_ENTRY){SysCall_ConsoleGetString, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGotoXY] = (SYSCALL_ENTRY){SysCall_ConsoleGotoXY, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleClear] = (SYSCALL_ENTRY){SysCall_ConsoleClear, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleBlitBuffer] = (SYSCALL_ENTRY){SysCall_ConsoleBlitBuffer, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleSetMode] = (SYSCALL_ENTRY){SysCall_ConsoleSetMode, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetModeCount] = (SYSCALL_ENTRY){SysCall_ConsoleGetModeCount, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetModeInfo] = (SYSCALL_ENTRY){SysCall_ConsoleGetModeInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetCurrentMode] = (SYSCALL_ENTRY){SysCall_ConsoleGetCurrentMode, EXOS_PRIVILEGE_USER};

    // Authentication Services
    SysCallTable[SYSCALL_Login] = (SYSCALL_ENTRY){SysCall_Login, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Logout] = (SYSCALL_ENTRY){SysCall_Logout, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetCurrentUser] = (SYSCALL_ENTRY){SysCall_GetCurrentUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ChangePassword] = (SYSCALL_ENTRY){SysCall_ChangePassword, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateUser] = (SYSCALL_ENTRY){SysCall_CreateUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DeleteUser] = (SYSCALL_ENTRY){SysCall_DeleteUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ListUsers] = (SYSCALL_ENTRY){SysCall_ListUsers, EXOS_PRIVILEGE_USER};

    // Mouse Services
    SysCallTable[SYSCALL_GetMousePos] = (SYSCALL_ENTRY){SysCall_GetMousePos, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetMousePos] = (SYSCALL_ENTRY){SysCall_SetMousePos, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetMouseButtons] = (SYSCALL_ENTRY){SysCall_GetMouseButtons, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowMouse] = (SYSCALL_ENTRY){SysCall_ShowMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HideMouse] = (SYSCALL_ENTRY){SysCall_HideMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ClipMouse] = (SYSCALL_ENTRY){SysCall_ClipMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CaptureMouse] = (SYSCALL_ENTRY){SysCall_CaptureMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReleaseMouse] = (SYSCALL_ENTRY){SysCall_ReleaseMouse, EXOS_PRIVILEGE_USER};

    // Windowing Services
    SysCallTable[SYSCALL_CreateDesktop] = (SYSCALL_ENTRY){SysCall_CreateDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowDesktop] = (SYSCALL_ENTRY){SysCall_ShowDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetDesktopWindow] = (SYSCALL_ENTRY){SysCall_GetDesktopWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetCurrentDesktop] = (SYSCALL_ENTRY){SysCall_GetCurrentDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ApplyDesktopTheme] = (SYSCALL_ENTRY){SysCall_ApplyDesktopTheme, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateWindow] = (SYSCALL_ENTRY){SysCall_CreateWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowWindow] = (SYSCALL_ENTRY){SysCall_ShowWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HideWindow] = (SYSCALL_ENTRY){SysCall_HideWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_MoveWindow] = (SYSCALL_ENTRY){SysCall_MoveWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SizeWindow] = (SYSCALL_ENTRY){SysCall_SizeWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowFunc] = (SYSCALL_ENTRY){SysCall_SetWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowFunc] = (SYSCALL_ENTRY){SysCall_GetWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowStyle] = (SYSCALL_ENTRY){SysCall_SetWindowStyle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ClearWindowStyle] = (SYSCALL_ENTRY){SysCall_ClearWindowStyle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowStyle] = (SYSCALL_ENTRY){SysCall_GetWindowStyle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowProp] = (SYSCALL_ENTRY){SysCall_SetWindowProp, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowProp] = (SYSCALL_ENTRY){SysCall_GetWindowProp, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowCaption] = (SYSCALL_ENTRY){SysCall_SetWindowCaption, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowTimer] = (SYSCALL_ENTRY){SysCall_SetWindowTimer, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowRect] = (SYSCALL_ENTRY){SysCall_GetWindowRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowClientRect] = (SYSCALL_ENTRY){SysCall_GetWindowClientRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ScreenPointToWindowPoint] =
        (SYSCALL_ENTRY){SysCall_ScreenPointToWindowPoint, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowParent] = (SYSCALL_ENTRY){SysCall_GetWindowParent, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowChildCount] = (SYSCALL_ENTRY){SysCall_GetWindowChildCount, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowChild] = (SYSCALL_ENTRY){SysCall_GetWindowChild, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetNextWindowSibling] = (SYSCALL_ENTRY){SysCall_GetNextWindowSibling, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetPreviousWindowSibling] =
        (SYSCALL_ENTRY){SysCall_GetPreviousWindowSibling, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_RegisterWindowClass] = (SYSCALL_ENTRY){SysCall_RegisterWindowClass, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_UnregisterWindowClass] = (SYSCALL_ENTRY){SysCall_UnregisterWindowClass, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FindWindowClass] = (SYSCALL_ENTRY){SysCall_FindWindowClass, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_WindowInheritsClass] = (SYSCALL_ENTRY){SysCall_WindowInheritsClass, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_InvalidateClientRect] = (SYSCALL_ENTRY){SysCall_InvalidateClientRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_InvalidateWindowRect] = (SYSCALL_ENTRY){SysCall_InvalidateWindowRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowGC] = (SYSCALL_ENTRY){SysCall_GetWindowGC, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReleaseWindowGC] = (SYSCALL_ENTRY){SysCall_ReleaseWindowGC, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_EnumWindows] = (SYSCALL_ENTRY){SysCall_EnumWindows, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_BaseWindowFunc] = (SYSCALL_ENTRY){SysCall_BaseWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemBrush] = (SYSCALL_ENTRY){SysCall_GetSystemBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemPen] = (SYSCALL_ENTRY){SysCall_GetSystemPen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateBrush] = (SYSCALL_ENTRY){SysCall_CreateBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreatePen] = (SYSCALL_ENTRY){SysCall_CreatePen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SelectBrush] = (SYSCALL_ENTRY){SysCall_SelectBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SelectPen] = (SYSCALL_ENTRY){SysCall_SelectPen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetPixel] = (SYSCALL_ENTRY){SysCall_SetPixel, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetPixel] = (SYSCALL_ENTRY){SysCall_GetPixel, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Line] = (SYSCALL_ENTRY){SysCall_Line, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Rectangle] = (SYSCALL_ENTRY){SysCall_Rectangle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DrawText] = (SYSCALL_ENTRY){SysCall_DrawText, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_MeasureText] = (SYSCALL_ENTRY){SysCall_MeasureText, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DrawWindowBackground] = (SYSCALL_ENTRY){SysCall_DrawWindowBackground, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetGraphicsDriver] = (SYSCALL_ENTRY){SysCall_SetGraphicsDriver, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetGCSurface] = (SYSCALL_ENTRY){SysCall_GetGCSurface, EXOS_PRIVILEGE_USER};
}
