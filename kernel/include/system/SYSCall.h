
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


    System calls

\************************************************************************/

#ifndef SYSCALL_H_INCLUDED
#define SYSCALL_H_INCLUDED

/************************************************************************/

#include "core/Kernel.h"
#include "User.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

extern void InitializeSystemCallTable(void);

extern SYSCALL_ENTRY SysCallTable[SYSCALL_Last];

/************************************************************************/

UINT SysCall_Debug(UINT Parameter);
UINT SysCall_GetVersion(UINT Parameter);
UINT SysCall_GetSystemInfo(UINT Parameter);
UINT SysCall_GetLastError(UINT Parameter);
UINT SysCall_SetLastError(UINT Parameter);
UINT SysCall_GetSystemTime(UINT Parameter);
UINT SysCall_GetLocalTime(UINT Parameter);
UINT SysCall_SetLocalTime(UINT Parameter);
UINT SysCall_DeleteObject(UINT Parameter);
UINT SysCall_CreateProcess(UINT Parameter);
UINT SysCall_KillProcess(UINT Parameter);

UINT SysCall_GetProcessInfo(UINT Parameter);
UINT SysCall_GetProcessMemoryInfo(UINT Parameter);
UINT SysCall_GetProfileInfo(UINT Parameter);
UINT SysCall_LoadModule(UINT Parameter);
UINT SysCall_GetModuleSymbol(UINT Parameter);
UINT SysCall_ReleaseModule(UINT Parameter);
UINT SysCall_CreateTask(UINT Parameter);
UINT SysCall_KillTask(UINT Parameter);
UINT SysCall_Exit(UINT Parameter);
UINT SysCall_SuspendTask(UINT Parameter);
UINT SysCall_ResumeTask(UINT Parameter);
UINT SysCall_Sleep(UINT Parameter);
UINT SysCall_Wait(UINT Parameter);
UINT SysCall_PostMessage(UINT Parameter);
UINT SysCall_SendMessage(UINT Parameter);
UINT SysCall_PeekMessage(UINT Parameter);
UINT SysCall_GetMessage(UINT Parameter);
UINT SysCall_DispatchMessage(UINT Parameter);
UINT SysCall_ConsoleSetMode(UINT Parameter);
UINT SysCall_ConsoleGetModeCount(UINT Parameter);
UINT SysCall_ConsoleGetModeInfo(UINT Parameter);
UINT SysCall_ConsoleGetCurrentMode(UINT Parameter);
UINT SysCall_CreateMutex(UINT Parameter);
UINT SysCall_DeleteMutex(UINT Parameter);
UINT SysCall_LockMutex(UINT Parameter);
UINT SysCall_UnlockMutex(UINT Parameter);
UINT SysCall_AllocRegion(UINT Parameter);
UINT SysCall_FreeRegion(UINT Parameter);
UINT SysCall_IsMemoryValid(UINT Parameter);
UINT SysCall_GetProcessHeap(UINT Parameter);
UINT SysCall_HeapAlloc(UINT Parameter);
UINT SysCall_HeapFree(UINT Parameter);
UINT SysCall_HeapRealloc(UINT Parameter);
UINT SysCall_EnumVolumes(UINT Parameter);
UINT SysCall_GetVolumeInfo(UINT Parameter);
UINT SysCall_OpenFile(UINT Parameter);
UINT SysCall_ReadFile(UINT Parameter);
UINT SysCall_WriteFile(UINT Parameter);
UINT SysCall_GetFileSize(UINT Parameter);
UINT SysCall_GetFilePosition(UINT Parameter);
UINT SysCall_SetFilePosition(UINT Parameter);
UINT SysCall_FindFirstFile(UINT Parameter);
UINT SysCall_FindNextFile(UINT Parameter);
UINT SysCall_CreatePipe(UINT Parameter);
UINT SysCall_ConsolePeekKey(UINT Parameter);
UINT SysCall_ConsoleGetKey(UINT Parameter);
UINT SysCall_ConsoleGetKeyModifiers(UINT Parameter);
UINT SysCall_ConsoleGetChar(UINT Parameter);
UINT SysCall_ConsolePrint(UINT Parameter);
UINT SysCall_ConsoleBlitBuffer(UINT Parameter);
UINT SysCall_ConsoleGetString(UINT Parameter);
UINT SysCall_ConsoleGotoXY(UINT Parameter);
UINT SysCall_ConsoleClear(UINT Parameter);

UINT SysCall_CreateDesktop(UINT Parameter);
UINT SysCall_ShowDesktop(UINT Parameter);
UINT SysCall_GetDesktopWindow(UINT Parameter);
UINT SysCall_GetCurrentDesktop(UINT Parameter);
UINT SysCall_ApplyDesktopTheme(UINT Parameter);
UINT SysCall_CreateWindow(UINT Parameter);
UINT SysCall_ShowWindow(UINT Parameter);
UINT SysCall_HideWindow(UINT Parameter);
UINT SysCall_MoveWindow(UINT Parameter);
UINT SysCall_SizeWindow(UINT Parameter);
UINT SysCall_SetWindowFunc(UINT Parameter);
UINT SysCall_GetWindowFunc(UINT Parameter);
UINT SysCall_SetWindowStyle(UINT Parameter);
UINT SysCall_ClearWindowStyle(UINT Parameter);
UINT SysCall_GetWindowStyle(UINT Parameter);
UINT SysCall_SetWindowProp(UINT Parameter);
UINT SysCall_GetWindowProp(UINT Parameter);
UINT SysCall_GetWindowRect(UINT Parameter);
UINT SysCall_GetWindowClientRect(UINT Parameter);
UINT SysCall_ScreenPointToWindowPoint(UINT Parameter);
UINT SysCall_GetWindowParent(UINT Parameter);
UINT SysCall_GetWindowChildCount(UINT Parameter);
UINT SysCall_GetWindowChild(UINT Parameter);
UINT SysCall_GetNextWindowSibling(UINT Parameter);
UINT SysCall_GetPreviousWindowSibling(UINT Parameter);
UINT SysCall_RegisterWindowClass(UINT Parameter);
UINT SysCall_UnregisterWindowClass(UINT Parameter);
UINT SysCall_FindWindowClass(UINT Parameter);
UINT SysCall_WindowInheritsClass(UINT Parameter);
UINT SysCall_InvalidateClientRect(UINT Parameter);
UINT SysCall_InvalidateWindowRect(UINT Parameter);
UINT SysCall_GetWindowGC(UINT Parameter);
UINT SysCall_ReleaseWindowGC(UINT Parameter);
UINT SysCall_EnumWindows(UINT Parameter);
UINT SysCall_BaseWindowFunc(UINT Parameter);
UINT SysCall_GetSystemBrush(UINT Parameter);
UINT SysCall_GetSystemPen(UINT Parameter);
UINT SysCall_CreateBrush(UINT Parameter);
UINT SysCall_CreatePen(UINT Parameter);
UINT SysCall_SelectBrush(UINT Parameter);
UINT SysCall_SelectPen(UINT Parameter);
UINT SysCall_SetPixel(UINT Parameter);
UINT SysCall_GetPixel(UINT Parameter);
UINT SysCall_Line(UINT Parameter);
UINT SysCall_Rectangle(UINT Parameter);
UINT SysCall_DrawText(UINT Parameter);
UINT SysCall_MeasureText(UINT Parameter);
UINT SysCall_DrawWindowBackground(UINT Parameter);
UINT SysCall_GetMousePos(UINT Parameter);
UINT SysCall_SetMousePos(UINT Parameter);
UINT SysCall_GetMouseButtons(UINT Parameter);
UINT SysCall_ShowMouse(UINT Parameter);
UINT SysCall_HideMouse(UINT Parameter);
UINT SysCall_ClipMouse(UINT Parameter);
UINT SysCall_CaptureMouse(UINT Parameter);
UINT SysCall_ReleaseMouse(UINT Parameter);
UINT SysCall_Login(UINT Parameter);
UINT SysCall_Logout(UINT Parameter);
UINT SysCall_GetCurrentUser(UINT Parameter);
UINT SysCall_ChangePassword(UINT Parameter);
UINT SysCall_CreateUser(UINT Parameter);
UINT SysCall_DeleteUser(UINT Parameter);
UINT SysCall_ListUsers(UINT Parameter);
UINT SysCall_SetGraphicsDriver(UINT Parameter);

UINT SysCall_SocketCreate(UINT Parameter);
UINT SysCall_SocketBind(UINT Parameter);
UINT SysCall_SocketListen(UINT Parameter);
UINT SysCall_SocketAccept(UINT Parameter);
UINT SysCall_SocketConnect(UINT Parameter);
UINT SysCall_SocketSend(UINT Parameter);
UINT SysCall_SocketReceive(UINT Parameter);
UINT SysCall_SocketSendTo(UINT Parameter);
UINT SysCall_SocketReceiveFrom(UINT Parameter);
UINT SysCall_SocketClose(UINT Parameter);
UINT SysCall_SocketShutdown(UINT Parameter);
UINT SysCall_SocketGetOption(UINT Parameter);
UINT SysCall_SocketSetOption(UINT Parameter);
UINT SysCall_SocketGetPeerName(UINT Parameter);
UINT SysCall_SocketGetSocketName(UINT Parameter);

/************************************************************************/

#pragma pack(pop)

#endif  // SYSCALL_H_INCLUDED
