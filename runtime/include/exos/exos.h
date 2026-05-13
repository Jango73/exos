
/************************************************************************\

    EXOS Runtime
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


    EXOS C API

\************************************************************************/

#ifndef EXOS_H_INCLUDED
#define EXOS_H_INCLUDED

/************************************************************************/

#include "../../../kernel/include/User.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************/

// Helper macro to cast parameters to the architecture-sized integer type.
#ifndef EXOS_PARAM
#define EXOS_PARAM(Value) ((uint_t)(Value))
#endif

#if defined(__GNUC__) || defined(__clang__)
#define EXOS_MODULE_EXPORT __attribute__((visibility("default")))
#define EXOS_MODULE_IMPORT extern
#define EXOS_THREAD_LOCAL __thread
#else
#define EXOS_MODULE_EXPORT
#define EXOS_MODULE_IMPORT extern
#define EXOS_THREAD_LOCAL
#endif

static inline I32 imin(I32 A, I32 B) { return (A < B) ? A : B; }
static inline I32 imax(I32 A, I32 B) { return (A > B) ? A : B; }

/************************************************************************/

typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;

/************************************************************************/

#ifndef EXOS_MESSAGE_DEFINED
#define EXOS_MESSAGE_DEFINED
typedef struct tag_MESSAGE {
    HANDLE Target;
    DATETIME Time;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGE, *LPMESSAGE;
#endif

/************************************************************************/

static inline BOOL ExosIsSuccess(UINT Status) { return Status == DF_RETURN_SUCCESS; }

/************************************************************************/

UINT CreateTask(LPTASK_INFO);
BOOL KillTask(HANDLE);
void Exit(void);
void Sleep(U32);
U32 Wait(LPWAIT_INFO);
UINT GetSystemTime(void);
BOOL GetLocalTime(LPDATETIME Time);
BOOL GetProcessMemoryInfo(LPPROCESS_MEMORY_INFO Info);
BOOL GetProfileInfo(LPPROFILE_QUERY_INFO Info);
LPVOID HeapAlloc(UINT Size);
void HeapFree(LPVOID Pointer);
HANDLE LoadModule(LPCSTR Path);
LPVOID GetModuleSymbol(HANDLE Module, LPCSTR Name);
BOOL ReleaseModule(HANDLE Module);
U32 FindFirstFile(FILE_FIND_INFO* Info);
U32 FindNextFile(FILE_FIND_INFO* Info);
BOOL GetMessage(HANDLE, LPMESSAGE, U32, U32);
BOOL PeekMessage(HANDLE, LPMESSAGE, U32, U32, U32);
BOOL DispatchMessage(LPMESSAGE);
BOOL PostMessage(HANDLE, U32, U32, U32);
U32 SendMessage(HANDLE, U32, U32, U32);
HANDLE CreateDesktop(HANDLE RootWindow);
BOOL ShowDesktop(HANDLE);
HANDLE GetDesktopWindow(HANDLE);
HANDLE GetCurrentDesktop(void);
BOOL GetDesktopScreenRect(HANDLE, LPRECT);
BOOL ApplyDesktopTheme(LPCSTR Target);
U32 KernelLogGetRecentSequence(void);
BOOL KernelLogCaptureRecentLines(LPKERNEL_LOG_RECENT_INFO Info);
HANDLE RegisterWindowClass(LPCSTR, HANDLE, LPCSTR, WINDOWFUNC, U32);
BOOL UnregisterWindowClass(HANDLE, LPCSTR);
HANDLE FindWindowClass(LPCSTR);
BOOL WindowInheritsClass(HANDLE, HANDLE, LPCSTR);
HANDLE CreateWindow(LPWINDOW_INFO Info);
HANDLE CreateWindowWithClass(HANDLE, HANDLE, LPCSTR, WINDOWFUNC, U32, U32, I32, I32, I32, I32);
BOOL DeleteWindow(HANDLE);
BOOL DestroyWindow(HANDLE);
HANDLE FindWindow(HANDLE, U32);
HANDLE ContainsWindow(HANDLE, HANDLE);
HANDLE GetWindowDesktop(HANDLE);
BOOL ShowWindow(HANDLE);
BOOL HideWindow(HANDLE);
BOOL SetWindowStyle(HANDLE, U32);
BOOL ClearWindowStyle(HANDLE, U32);
BOOL GetWindowStyle(HANDLE, U32*);
BOOL SetWindowCaption(HANDLE, LPCSTR);
BOOL GetWindowCaption(HANDLE, LPSTR, UINT);
BOOL InvalidateClientRect(HANDLE, LPRECT);
BOOL InvalidateWindowRect(HANDLE, LPRECT);
UINT SetWindowProp(HANDLE, LPCSTR, UINT);
UINT GetWindowProp(HANDLE, LPCSTR);
HANDLE GetWindowGC(HANDLE);
BOOL ReleaseWindowGC(HANDLE);
HANDLE BeginWindowDraw(HANDLE);
BOOL EndWindowDraw(HANDLE);
BOOL GetGCSurface(HANDLE GC, LPGC_SURFACE_INFO Info);
BOOL GetWindowRect(HANDLE, LPRECT);
BOOL GetWindowClientRect(HANDLE, LPRECT);
BOOL ScreenPointToWindowPoint(HANDLE, LPPOINT, LPPOINT);
HANDLE GetWindowParent(HANDLE);
U32 GetWindowChildCount(HANDLE);
HANDLE GetWindowChild(HANDLE, U32 ChildIndex);
HANDLE GetNextWindowSibling(HANDLE);
HANDLE GetPreviousWindowSibling(HANDLE);
BOOL MoveWindow(HANDLE, LPRECT);
HANDLE GetSystemBrush(U32);
HANDLE GetSystemPen(U32);
HANDLE CreateBrush(LPBRUSH_INFO);
HANDLE CreatePen(LPPEN_INFO);
HANDLE SelectBrush(HANDLE, HANDLE);
HANDLE SelectPen(HANDLE, HANDLE);
BOOL Line(LPLINE_INFO LineInfo);
BOOL Arc(LPARC_INFO ArcInfo);
BOOL Triangle(LPTRIANGLE_INFO TriangleInfo);
U32 BaseWindowFunc(HANDLE, U32, U32, U32);
U32 SetPixel(HANDLE, U32, U32);
U32 GetPixel(HANDLE, U32, U32);
void Rectangle(HANDLE, U32, U32, U32, U32, U32);
BOOL DrawText(LPTEXT_DRAW_INFO DrawInfo);
BOOL MeasureText(LPTEXT_MEASURE_INFO MeasureInfo);
BOOL DrawWindowBackground(HANDLE Window, HANDLE GC, LPRECT Rect, U32 ThemeToken);
BOOL GetMousePosition(LPPOINT);
U32 GetMouseButtons(void);
BOOL GetGraphicsDebugInfo(LPDRIVER_DEBUG_INFO Info);
BOOL GetMouseDebugInfo(LPDRIVER_DEBUG_INFO Info);
HANDLE CaptureMouse(HANDLE);
BOOL ReleaseMouse(void);
BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds);
BOOL KillWindowTimer(HANDLE Window, U32 TimerID);
U32 GetKeyModifiers(void);
U32 ConsoleGetKey(LPKEYCODE);
U32 ConsoleBlitBuffer(LPCONSOLE_BLIT_BUFFER);
void ConsoleGotoXY(LPPOINT);
void ConsoleClear(void);
U32 ConsoleSetColumnsRows(U32 Columns, U32 Rows);
BOOL ConsoleGetCurrentMode(LPCONSOLE_MODE_INFO Info);
BOOL DeleteObject(HANDLE);
void srand(U32);
U32 rand(void);

/************************************************************************/
// Berkeley Socket API for userland

SOCKET_HANDLE SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol);
U32 SocketBind(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);
U32 SocketListen(SOCKET_HANDLE SocketHandle, U32 Backlog);
SOCKET_HANDLE SocketAccept(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketConnect(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);
I32 SocketSend(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags);
I32 SocketReceive(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags);
I32 SocketSendTo(
    SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS DestAddress, U32 AddressLength);
I32 SocketReceiveFrom(
    SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS SourceAddress,
    U32* AddressLength);
U32 SocketClose(SOCKET_HANDLE SocketHandle);
U32 SocketShutdown(SOCKET_HANDLE SocketHandle, U32 How);
U32 SocketGetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPVOID OptionValue, U32* OptionLength);
U32 SocketSetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPCVOID OptionValue, U32 OptionLength);
U32 SocketGetPeerName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketGetSocketName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);

// Address utility functions
U32 InternetAddressFromString(LPCSTR IPString);
LPCSTR InternetAddressToString(U32 IPAddress);

// Socket address utility functions
U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline unsigned short HToNs(unsigned short Value) { return Value; }
static inline unsigned short NToHs(unsigned short Value) { return Value; }
static inline unsigned long HToNl(unsigned long Value) { return Value; }
static inline unsigned long NToHl(unsigned long Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline unsigned short HToNs(unsigned short Value) { return (unsigned short)((Value << 8) | (Value >> 8)); }
static inline unsigned short NToHs(unsigned short Value) { return HToNs(Value); }
static inline unsigned long HToNl(unsigned long Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}
static inline unsigned long NToHl(unsigned long Value) { return HToNl(Value); }

#else
#error "Endianness not defined"
#endif

/************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
