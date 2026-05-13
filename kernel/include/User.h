
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


    EXOS Public API

\************************************************************************/

#ifndef USER_H_INCLUDED
#define USER_H_INCLUDED

#include "Types.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************\

    EXOS version

    MAJOR (1): Incremented for incompatible changes (breaking changes, e.g.,
        API mods that break existing code). Reset MINOR and PATCH to 0.
    MINOR (5): Incremented for compatible additions (new features or
        backward-compatible APIs). Reset PATCH to 0, MAJOR unchanged.
    PATCH (147): Incremented for bug fixes or minor opts (no API impact).
        MAJOR and MINOR unchanged.

\************************************************************************/

#define EXOS_VERSION_MAJOR 0
#define EXOS_VERSION_MINOR 73
#define EXOS_VERSION_PATCH 0

#define EXOS_COPYRIGHT_FROM 1999
#define EXOS_COPYRIGHT_TO 2026

#define EXOS_VERSION MAKE_VERSION(EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR)

/************************************************************************/
// EXOS ABI

// Global ABI version for user/kernel boundary
#define EXOS_ABI_VERSION 0x0001

/* Common header prefix for syscall payload structures.
   - Size: sizeof(struct) at compile-time of the caller
   - Version: per-struct or global EXOS_ABI_VERSION
   - Flags: reserved for extensions
*/
typedef struct PACKED tag_ABI_HEADER {
    U32 Size;
    U16 Version;
    U16 Flags;
} ABI_HEADER;

// C11 static assert helper
#define ABI_STATIC_ASSERT(cond, name) typedef char static_assert_##name[(cond) ? 1 : -1]

/************************************************************************/
// EXOS Base Services - Syscall IDs

#define SYSCALL_GetVersion 0x00000000
#define SYSCALL_GetSystemInfo 0x00000001
#define SYSCALL_GetLastError 0x00000002
#define SYSCALL_SetLastError 0x00000003
#define SYSCALL_Debug 0x00000066

/************************************************************************/
// Time Services

#define SYSCALL_GetSystemTime 0x00000004
#define SYSCALL_GetLocalTime 0x00000005
#define SYSCALL_SetLocalTime 0x00000006

/************************************************************************/
// Process services

#define SYSCALL_DeleteObject 0x00000007
#define SYSCALL_CreateProcess 0x00000008
#define SYSCALL_KillProcess 0x00000009
#define SYSCALL_GetProcessInfo 0x0000000A
#define SYSCALL_GetProcessMemoryInfo 0x00000087
#define SYSCALL_GetProfileInfo 0x00000089

/************************************************************************/
// Executable Module Services

#define SYSCALL_LoadModule 0x00000091
#define SYSCALL_GetModuleSymbol 0x00000092
#define SYSCALL_ReleaseModule 0x00000093

/************************************************************************/
// Threading Services

#define SYSCALL_CreateTask 0x0000000B
#define SYSCALL_KillTask 0x0000000C
#define SYSCALL_Exit 0x00000033
#define SYSCALL_SuspendTask 0x0000000D
#define SYSCALL_ResumeTask 0x0000000E
#define SYSCALL_Sleep 0x0000000F
#define SYSCALL_Wait 0x00000010
#define SYSCALL_PostMessage 0x00000011
#define SYSCALL_SendMessage 0x00000012
#define SYSCALL_PeekMessage 0x00000013
#define SYSCALL_GetMessage 0x00000014
#define SYSCALL_DispatchMessage 0x00000015
#define SYSCALL_CreateMutex 0x00000016
#define SYSCALL_LockMutex 0x00000017
#define SYSCALL_UnlockMutex 0x00000018

/************************************************************************/
// Memory Services

#define SYSCALL_AllocRegion 0x00000019
#define SYSCALL_FreeRegion 0x0000001A
#define SYSCALL_IsMemoryValid 0x0000001B
#define SYSCALL_GetProcessHeap 0x0000001C
#define SYSCALL_HeapAlloc 0x0000001D
#define SYSCALL_HeapFree 0x0000001E
#define SYSCALL_HeapRealloc 0x0000001F

/************************************************************************/
// File Services

#define SYSCALL_EnumVolumes 0x00000020
#define SYSCALL_GetVolumeInfo 0x00000021
#define SYSCALL_OpenFile 0x00000022
#define SYSCALL_ReadFile 0x00000023
#define SYSCALL_WriteFile 0x00000024
#define SYSCALL_GetFileSize 0x00000025
#define SYSCALL_GetFilePointer 0x00000026
#define SYSCALL_SetFilePointer 0x00000027
#define SYSCALL_FindFirstFile 0x00000028
#define SYSCALL_FindNextFile 0x00000029
#define SYSCALL_CreateFileMapping 0x0000002A
#define SYSCALL_OpenFileMapping 0x0000002B
#define SYSCALL_MapViewOfFile 0x0000002C
#define SYSCALL_UnmapViewOfFile 0x0000002D
#define SYSCALL_CreatePipe 0x00000094

/************************************************************************/
// Console Services

#define SYSCALL_ConsolePeekKey 0x0000002E
#define SYSCALL_ConsoleGetKey 0x0000002F
#define SYSCALL_ConsolePrint 0x00000030
#define SYSCALL_ConsoleGetString 0x00000031
#define SYSCALL_ConsoleGotoXY 0x00000032
#define SYSCALL_ConsoleClear 0x00000034
#define SYSCALL_ConsoleBlitBuffer 0x00000077
#define SYSCALL_ConsoleGetKeyModifiers 0x00000078
#define SYSCALL_ConsoleSetMode 0x00000079
#define SYSCALL_ConsoleGetModeCount 0x0000007A
#define SYSCALL_ConsoleGetModeInfo 0x0000007B
#define SYSCALL_ConsoleGetCurrentMode 0x0000007F

/************************************************************************/
/* Console Colors                                                        */
/************************************************************************/

#define CONSOLE_BLACK 0
#define CONSOLE_BLUE 1
#define CONSOLE_GREEN 2
#define CONSOLE_CYAN 3
#define CONSOLE_RED 4
#define CONSOLE_MAGENTA 5
#define CONSOLE_BROWN 6
#define CONSOLE_GRAY 7
#define CONSOLE_DARK_GRAY 8
#define CONSOLE_LIGHT_BLUE 9
#define CONSOLE_LIGHT_GREEN 10
#define CONSOLE_LIGHT_CYAN 11
#define CONSOLE_SALMON 12
#define CONSOLE_LIGHT_MAGENTA 13
#define CONSOLE_YELLOW 14
#define CONSOLE_WHITE 15

/************************************************************************/
/* Key modifier flags                                                   */
/************************************************************************/

#define KEYMOD_SHIFT 0x00000001
#define KEYMOD_CONTROL 0x00000002
#define KEYMOD_ALT 0x00000004

/************************************************************************/
// Authentication Services

#define SYSCALL_Login 0x00000035
#define SYSCALL_Logout 0x00000036
#define SYSCALL_GetCurrentUser 0x00000037
#define SYSCALL_ChangePassword 0x00000038
#define SYSCALL_CreateUser 0x00000039
#define SYSCALL_DeleteUser 0x0000003A
#define SYSCALL_ListUsers 0x0000003B

/************************************************************************/
// Mouse Services

#define SYSCALL_GetMousePos 0x0000003C
#define SYSCALL_SetMousePos 0x0000003D
#define SYSCALL_GetMouseButtons 0x0000003E
#define SYSCALL_ShowMouse 0x0000003F
#define SYSCALL_HideMouse 0x00000040
#define SYSCALL_ClipMouse 0x00000041
#define SYSCALL_CaptureMouse 0x00000042
#define SYSCALL_ReleaseMouse 0x00000043

/************************************************************************/
// Windowing Services

#define SYSCALL_CreateDesktop 0x00000044
#define SYSCALL_ShowDesktop 0x00000045
#define SYSCALL_GetDesktopWindow 0x00000046
#define SYSCALL_GetCurrentDesktop 0x00000067
#define SYSCALL_CreateWindow 0x00000047
#define SYSCALL_ShowWindow 0x00000048
#define SYSCALL_HideWindow 0x00000049
#define SYSCALL_MoveWindow 0x0000004A
#define SYSCALL_SizeWindow 0x0000004B
#define SYSCALL_SetWindowFunc 0x0000004C
#define SYSCALL_GetWindowFunc 0x0000004D
#define SYSCALL_SetWindowStyle 0x0000004E
#define SYSCALL_GetWindowStyle 0x0000004F
#define SYSCALL_SetWindowProp 0x00000050
#define SYSCALL_GetWindowProp 0x00000051
#define SYSCALL_SetWindowCaption 0x00000096
#define SYSCALL_SetWindowTimer 0x00000097
#define SYSCALL_FindWindow 0x00000098
#define SYSCALL_GetWindowCaption 0x00000099
#define SYSCALL_GetDesktopScreenRect 0x0000009A
#define SYSCALL_GetKernelLogRecent 0x0000009B
#define SYSCALL_GetWindowRect 0x00000052
#define SYSCALL_GetWindowClientRect 0x0000007C
#define SYSCALL_GetWindowParent 0x0000008B
#define SYSCALL_GetWindowChildCount 0x0000008C
#define SYSCALL_GetWindowChild 0x0000008D
#define SYSCALL_GetNextWindowSibling 0x0000008E
#define SYSCALL_GetPreviousWindowSibling 0x0000008F
#define SYSCALL_RegisterWindowClass 0x0000007D
#define SYSCALL_UnregisterWindowClass 0x0000007E
#define SYSCALL_FindWindowClass 0x00000090
#define SYSCALL_WindowInheritsClass 0x00000083
#define SYSCALL_ClearWindowStyle 0x00000084
#define SYSCALL_ScreenPointToWindowPoint 0x00000085
#define SYSCALL_InvalidateClientRect 0x00000086
#define SYSCALL_InvalidateWindowRect 0x00000053
#define SYSCALL_GetWindowGC 0x00000054
#define SYSCALL_ReleaseWindowGC 0x00000055
#define SYSCALL_EnumWindows 0x00000056
#define SYSCALL_BaseWindowFunc 0x00000057
#define SYSCALL_GetSystemBrush 0x00000058
#define SYSCALL_GetSystemPen 0x00000059
#define SYSCALL_CreateBrush 0x0000005A
#define SYSCALL_CreatePen 0x0000005B
#define SYSCALL_SelectBrush 0x0000005C
#define SYSCALL_SelectPen 0x0000005D
#define SYSCALL_SetPixel 0x0000005E
#define SYSCALL_GetPixel 0x0000005F
#define SYSCALL_Line 0x00000060
#define SYSCALL_Rectangle 0x00000061
#define SYSCALL_CreateRectRegion 0x00000062
#define SYSCALL_CreatePolyRegion 0x00000063
#define SYSCALL_MoveRegion 0x00000064
#define SYSCALL_CombineRegion 0x00000065
#define SYSCALL_DrawText 0x00000080
#define SYSCALL_MeasureText 0x00000081
#define SYSCALL_DrawWindowBackground 0x00000082
#define SYSCALL_ApplyDesktopTheme 0x00000088
#define SYSCALL_SetGraphicsDriver 0x0000008A
#define SYSCALL_GetGCSurface 0x0000009C
#define SYSCALL_Arc 0x0000009D
#define SYSCALL_Triangle 0x0000009E
#define SYSCALL_GetGraphicsDebugInfo 0x0000009F
#define SYSCALL_GetMouseDebugInfo 0x000000A0
#define SYSCALL_SetMouseSerpentineMode 0x000000A1

/************************************************************************/
// Network Socket Services

#define SYSCALL_SocketCreate 0x00000068
#define SYSCALL_SocketShutdown 0x00000069
#define SYSCALL_SocketBind 0x0000006A
#define SYSCALL_SocketListen 0x0000006B
#define SYSCALL_SocketAccept 0x0000006C
#define SYSCALL_SocketConnect 0x0000006D
#define SYSCALL_SocketSend 0x0000006E
#define SYSCALL_SocketReceive 0x0000006F
#define SYSCALL_SocketSendTo 0x00000070
#define SYSCALL_SocketReceiveFrom 0x00000071
#define SYSCALL_SocketClose 0x00000072
#define SYSCALL_SocketGetOption 0x00000073
#define SYSCALL_SocketSetOption 0x00000074
#define SYSCALL_SocketGetPeerName 0x00000075
#define SYSCALL_SocketGetSocketName 0x00000076

/************************************************************************/

#define SYSCALL_Last 0x000000A2

/************************************************************************/
// Structure limits

#define WAIT_INFO_MAX_OBJECTS 32
#define PROFILE_MAX_ENTRIES 64
#define PROFILE_NAME_LENGTH 64

/************************************************************************/
// ABI Data Structures

// A function for a thread entry
typedef U32 (*TASKFUNC)(LPVOID);

// A function for window messaging
typedef U32 (*WINDOWFUNC)(HANDLE, U32, U32, U32);

// A function for volume enumeration
typedef BOOL (*ENUMVOLUMESFUNC)(HANDLE, LPVOID);

/************************************************************************/
// A datetime

typedef struct tag_DATETIME {
    U32 Year : 26;   // 67 108 863
    U32 Month : 4;   // 15
    U32 Day : 6;     // 63
    U32 Hour : 6;    // 63
    U32 Minute : 6;  // 63
    U32 Second : 6;  // 63
    U32 Milli : 10;  // 1023
} DATETIME, *LPDATETIME;

typedef struct PACKED tag_SYSTEM_INFO {
    ABI_HEADER Header;
    U64 TotalPhysicalMemory;
    U64 PhysicalMemoryUsed;
    U64 PhysicalMemoryAvail;
    U64 TotalSwapMemory;
    U64 SwapMemoryUsed;
    U64 SwapMemoryAvail;
    U64 TotalMemoryUsed;
    U64 TotalMemoryAvail;
    UINT PageSize;
    UINT TotalPhysicalPages;
    UINT MinimumLinearAddress;
    UINT MaximumLinearAddress;
    U32 NumProcesses;
    U32 NumTasks;
    STR UserName[MAX_USER_NAME];
    STR KeyboardLayout[MAX_USER_NAME];
} SYSTEM_INFO, *LPSYSTEM_INFO;

typedef struct PACKED tag_SECURITY_ATTRIBUTES {
    U32 Nothing;
} SECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct PACKED tag_PROCESS_INFO {
    ABI_HEADER Header;
    U32 Flags;
    STR CommandLine[MAX_PATH_NAME];
    STR WorkFolder[MAX_PATH_NAME];
    HANDLE StdOut;
    HANDLE StdIn;
    HANDLE StdErr;
    HANDLE Process;
    HANDLE Task;
    SECURITY_ATTRIBUTES Security;
} PROCESS_INFO, *LPPROCESS_INFO;

typedef struct PACKED tag_PROCESS_MEMORY_INFO {
    ABI_HEADER Header;
    HANDLE Process;
    LINEAR HeapBase;
    UINT HeapReservedSize;
    UINT HeapFirstUnallocatedOffset;
    UINT HeapUsedBytes;
    UINT HeapFreeBytes;
} PROCESS_MEMORY_INFO, *LPPROCESS_MEMORY_INFO;

typedef struct PACKED tag_PROFILE_ENTRY_INFO {
    STR Name[PROFILE_NAME_LENGTH];
    UINT CallCount;
    UINT TimedCallCount;
    UINT LastTicks;
    UINT TotalTicks;
    UINT MaxTicks;
} PROFILE_ENTRY_INFO, *LPPROFILE_ENTRY_INFO;

#define PROFILE_QUERY_FLAG_RESET 0x00000001

typedef struct PACKED tag_PROFILE_QUERY_INFO {
    ABI_HEADER Header;
    UINT Capacity;
    UINT Flags;
    UINT EntryCount;
    UINT TotalEntryCount;
    UINT SampleCount;
    UINT DroppedCount;
    LPPROFILE_ENTRY_INFO Entries;
} PROFILE_QUERY_INFO, *LPPROFILE_QUERY_INFO;

typedef struct PACKED tag_MODULE_LOAD_INFO {
    ABI_HEADER Header;
    LPCSTR Path;
    HANDLE Module;
    U32 Flags;
    U32 ModuleIdentifierHigh;
    U32 ModuleIdentifierLow;
} MODULE_LOAD_INFO, *LPMODULE_LOAD_INFO;

typedef struct PACKED tag_MODULE_SYMBOL_INFO {
    ABI_HEADER Header;
    HANDLE Module;
    LPCSTR Name;
    LINEAR Address;
} MODULE_SYMBOL_INFO, *LPMODULE_SYMBOL_INFO;

typedef struct PACKED tag_CONSOLE_BLIT_BUFFER {
    UINT X;
    UINT Y;
    UINT Width;
    UINT Height;
    LPCSTR Text;
    UINT ForeColor;
    UINT BackColor;
    UINT TextPitch;
    const U8* Attr; /* Fore | (Back << 4) per cell, optional */
    UINT AttrPitch; /* Bytes per row in Attr when provided */
} CONSOLE_BLIT_BUFFER, *LPCONSOLE_BLIT_BUFFER;

typedef struct PACKED tag_CONSOLE_MODE_INFO {
    ABI_HEADER Header;
    U32 Index;
    U32 Columns;
    U32 Rows;
    U32 CharHeight;
} CONSOLE_MODE_INFO, *LPCONSOLE_MODE_INFO;

typedef struct PACKED tag_TASK_INFO {
    ABI_HEADER Header;
    TASKFUNC Func;
    LPVOID Parameter;
    U32 StackSize;
    U32 Priority;
    U32 Flags;
    SECURITY_ATTRIBUTES Security;
    STR Name[MAX_USER_NAME];
    HANDLE Task;
} TASK_INFO, *LPTASK_INFO;

typedef struct PACKED tag_MESSAGE_INFO {
    ABI_HEADER Header;
    DATETIME Time;
    U32 First;
    U32 Last;
    HANDLE Target;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGE_INFO, *LPMESSAGE_INFO;

// Describes a mutex and some delay
typedef struct PACKED tag_MUTEX_INFO {
    ABI_HEADER Header;
    HANDLE Mutex;
    UINT MilliSeconds;
} MUTEX_INFO, *LPMUTEX_INFO;

typedef struct PACKED tag_WAIT_INFO {
    ABI_HEADER Header;
    U32 Count;
    U32 MilliSeconds;
    U32 Flags;
    HANDLE Objects[WAIT_INFO_MAX_OBJECTS];
    UINT ExitCodes[WAIT_INFO_MAX_OBJECTS];
} WAIT_INFO, *LPWAIT_INFO;

typedef struct PACKED tag_ALLOC_REGION_INFO {
    ABI_HEADER Header;
    U32 Base;    // The base virtual address (0 = don't care)
    U32 Target;  // The physical address to map to (0 = don't care)
    U32 Size;    // The size in bytes to allocate
    U32 Flags;   // See ALLOC_PAGES_xxx
} ALLOC_REGION_INFO, *LPALLOC_REGION_INFO;

typedef struct PACKED tag_HEAP_REALLOC_INFO {
    ABI_HEADER Header;
    LPVOID Pointer;  // Pointer to existing memory block, or NULL
    U32 Size;        // New size of memory block in bytes
} HEAP_REALLOC_INFO, *LPHEAP_REALLOC_INFO;

typedef struct PACKED tag_ENUM_VOLUMES_INFO {
    ABI_HEADER Header;
    ENUMVOLUMESFUNC Func;  // The callback for enumeration
    LPVOID Parameter;      //
} ENUM_VOLUMES_INFO, *LPENUM_VOLUMES_INFO;

typedef struct PACKED tag_VOLUME_INFO {
    U32 Size;
    HANDLE Volume;
    STR Name[MAX_FS_LOGICAL_NAME];
} VOLUME_INFO, *LPVOLUME_INFO;

typedef struct PACKED tag_FILE_OPEN_INFO {
    ABI_HEADER Header;
    LPCSTR Name;
    U32 Flags;
} FILE_OPEN_INFO, *LPFILE_OPEN_INFO;

typedef struct PACKED tag_GRAPHICS_MODE_INFO {
    ABI_HEADER Header;
    U32 ModeIndex;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
} GRAPHICS_MODE_INFO, *LPGRAPHICS_MODE_INFO;

typedef struct PACKED tag_GRAPHICS_DRIVER_SELECTION_INFO {
    ABI_HEADER Header;
    STR DriverAlias[MAX_NAME];
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
} GRAPHICS_DRIVER_SELECTION_INFO, *LPGRAPHICS_DRIVER_SELECTION_INFO;

typedef struct PACKED tag_FILE_OPERATION {
    ABI_HEADER Header;
    HANDLE File;
    U32 NumBytes;
    LPVOID Buffer;
} FILE_OPERATION, *LPFILE_OPERATION;

typedef struct PACKED tag_PIPE_INFO {
    ABI_HEADER Header;
    HANDLE ReadHandle;
    HANDLE WriteHandle;
} PIPE_INFO, *LPPIPE_INFO;

typedef struct PACKED tag_FILE_FIND_INFO {
    ABI_HEADER Header;
    LPCSTR Path;          // Base directory to search
    LPCSTR Pattern;       // Wildcard pattern (supports '*')
    HANDLE SearchHandle;  // Internal search state
    U32 Attributes;
    STR Name[MAX_FILE_NAME];
} FILE_FIND_INFO, *LPFILE_FIND_INFO;

typedef struct PACKED tag_NETWORK_INFO {
    ABI_HEADER Header;
    U8 MAC[6];       // MAC address
    U32 LinkUp;      // 1 = link up, 0 = link down
    U32 SpeedMbps;   // Link speed in Mbps
    U32 DuplexFull;  // 1 = full duplex, 0 = half duplex
    U32 MTU;         // Maximum Transmission Unit
} NETWORK_INFO, *LPNETWORK_INFO;

typedef struct PACKED tag_KEYCODE {
    U8 VirtualKey;
    STR ASCIICode;
    USTR Unicode;
} KEYCODE, *LPKEYCODE;

typedef struct PACKED tag_POINT {
    I32 X, Y;
} POINT, *LPPOINT;

typedef struct PACKED tag_RECT {
    I32 X1, Y1;
    I32 X2, Y2;
} RECT, *LPRECT;

typedef struct PACKED tag_WINDOW_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    HANDLE Parent;
    HANDLE WindowClass;
    LPCSTR WindowClassName;
    WINDOWFUNC Function;
    U32 Style;
    U32 ID;
    POINT WindowPosition;
    POINT WindowSize;
    BOOL ShowHide;
} WINDOW_INFO, *LPWINDOW_INFO;

typedef struct PACKED tag_WINDOW_CLASS_INFO {
    ABI_HEADER Header;
    HANDLE WindowClass;
    HANDLE BaseClass;
    LPCSTR ClassName;
    LPCSTR BaseClassName;
    WINDOWFUNC Function;
    U32 ClassDataSize;
} WINDOW_CLASS_INFO, *LPWINDOW_CLASS_INFO;

typedef struct PACKED tag_PROP_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    LPCSTR Name;
    UINT Value;
} PROP_INFO, *LPPROP_INFO;

typedef struct PACKED tag_WINDOW_CAPTION {
    ABI_HEADER Header;
    HANDLE Window;
    U8 Text[MAX_WINDOW_CAPTION];
} WINDOW_CAPTION, *LPWINDOW_CAPTION;

typedef struct PACKED tag_TIMER_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    U32 TimerID;
    U32 Interval;
} TIMER_INFO, *LPTIMER_INFO;

typedef struct PACKED tag_WINDOW_CLASS_QUERY_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    HANDLE WindowClass;
    LPCSTR ClassName;
} WINDOW_CLASS_QUERY_INFO, *LPWINDOW_CLASS_QUERY_INFO;

typedef struct PACKED tag_WINDOW_RECT {
    ABI_HEADER Header;
    HANDLE Window;
    RECT Rect;
} WINDOW_RECT, *LPWINDOW_RECT;

#define WINDOW_RECT_FLAG_ALL 0x00000001

typedef struct PACKED tag_DESKTOP_RECT_INFO {
    ABI_HEADER Header;
    HANDLE Desktop;
    RECT Rect;
} DESKTOP_RECT_INFO, *LPDESKTOP_RECT_INFO;

typedef struct PACKED tag_WINDOW_POINT_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    POINT ScreenPoint;
    POINT WindowPoint;
} WINDOW_POINT_INFO, *LPWINDOW_POINT_INFO;

typedef struct PACKED tag_WINDOW_FIND_INFO {
    ABI_HEADER Header;
    HANDLE Parent;
    U32 WindowID;
    HANDLE Window;
} WINDOW_FIND_INFO, *LPWINDOW_FIND_INFO;

typedef struct PACKED tag_WINDOW_CHILD_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    U32 ChildIndex;
} WINDOW_CHILD_INFO, *LPWINDOW_CHILD_INFO;

typedef struct PACKED tag_DESKTOP_THEME_INFO {
    ABI_HEADER Header;
    LPCSTR Target;
} DESKTOP_THEME_INFO, *LPDESKTOP_THEME_INFO;

typedef struct PACKED tag_GCSELECT {
    ABI_HEADER Header;
    HANDLE GC;
    HANDLE Object;
} GCSELECT, *LPGCSELECT;

typedef struct PACKED tag_BRUSH_INFO {
    ABI_HEADER Header;
    COLOR Color;
    U32 Pattern;
    U32 Flags;
} BRUSH_INFO, *LPBRUSH_INFO;

typedef struct PACKED tag_PEN_INFO {
    ABI_HEADER Header;
    COLOR Color;
    U32 Pattern;
    U32 Width;
    U32 Flags;
} PEN_INFO, *LPPEN_INFO;

typedef struct PACKED tag_PIXEL_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X;
    I32 Y;
    COLOR Color;
} PIXEL_INFO, *LPPIXEL_INFO;

typedef struct PACKED tag_LINE_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
} LINE_INFO, *LPLINE_INFO;

#define RECT_FLAG_FILL_VERTICAL_GRADIENT 0x00000001
#define RECT_FLAG_FILL_HORIZONTAL_GRADIENT 0x00000002
#define RECT_FLAG_FILL_GRADIENT_MASK (RECT_FLAG_FILL_VERTICAL_GRADIENT | RECT_FLAG_FILL_HORIZONTAL_GRADIENT)

#define RECT_CORNER_STYLE_SQUARE 0x00000000
#define RECT_CORNER_STYLE_ROUNDED 0x00000001
#define RECT_CORNER_STYLE_BEVEL 0x00000002
#define RECT_CORNER_RADIUS_AUTO (-1)
#define RECT_CORNER_RADIUS_AUTO_LIMIT(MaximumRadius) (-(1 + (MaximumRadius)))

typedef struct PACKED tag_RECT_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
    COLOR StartColor;
    COLOR EndColor;
    I32 CornerRadius;
    U32 CornerStyle;
} RECT_INFO, *LPRECT_INFO;

typedef struct PACKED tag_GC_SURFACE_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 Width;
    I32 Height;
    I32 Pitch;
    U8* MemoryBase;
} GC_SURFACE_INFO, *LPGC_SURFACE_INFO;

typedef struct PACKED tag_WINDOW_BACKGROUND_INFO {
    ABI_HEADER Header;
    HANDLE Window;
    HANDLE GC;
    RECT Rect;
    U32 ThemeToken;
} WINDOW_BACKGROUND_INFO, *LPWINDOW_BACKGROUND_INFO;

typedef struct PACKED tag_ARC_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 CenterX;
    I32 CenterY;
    I32 Radius;
    I32 StartAngle;
    I32 EndAngle;
    COLOR StartColor;
    COLOR EndColor;
} ARC_INFO, *LPARC_INFO;

#define ARC_FLAG_FILL 0x00000001
#define ARC_FLAG_FILL_VERTICAL_GRADIENT 0x00000002
#define ARC_FLAG_FILL_HORIZONTAL_GRADIENT 0x00000004
#define ARC_FLAG_FILL_GRADIENT_MASK (ARC_FLAG_FILL_VERTICAL_GRADIENT | ARC_FLAG_FILL_HORIZONTAL_GRADIENT)

typedef struct PACKED tag_TRIANGLE_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    POINT P1;
    POINT P2;
    POINT P3;
} TRIANGLE_INFO, *LPTRIANGLE_INFO;

typedef struct PACKED tag_TEXT_DRAW_INFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X;
    I32 Y;
    LPCSTR Text;
    HANDLE Font;
} TEXT_DRAW_INFO, *LPTEXT_DRAW_INFO;

typedef struct PACKED tag_TEXT_MEASURE_INFO {
    ABI_HEADER Header;
    LPCSTR Text;
    HANDLE Font;
    U32 Width;
    U32 Height;
} TEXT_MEASURE_INFO, *LPTEXT_MEASURE_INFO;

typedef struct PACKED tag_DRIVER_DEBUG_INFO {
    ABI_HEADER Header;
    STR Text[MAX_STRING_BUFFER];
} DRIVER_DEBUG_INFO, *LPDRIVER_DEBUG_INFO;

typedef struct PACKED tag_KERNEL_LOG_RECENT_INFO {
    ABI_HEADER Header;
    LPSTR Text;
    UINT TextBufferSize;
    UINT MaxLines;
    U32 Sequence;
    UINT TotalLines;
    UINT CopiedLines;
    BOOL Truncated;
} KERNEL_LOG_RECENT_INFO, *LPKERNEL_LOG_RECENT_INFO;

typedef struct PACKED tag_LOGIN_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
} LOGIN_INFO, *LPLOGIN_INFO;

typedef struct PACKED tag_PASSWORD_CHANGE {
    ABI_HEADER Header;
    STR OldPassword[MAX_USER_NAME];
    STR NewPassword[MAX_USER_NAME];
} PASSWORD_CHANGE, *LPPASSWORD_CHANGE;

typedef struct PACKED tag_USER_CREATE_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    U32 Privilege;
} USER_CREATE_INFO, *LPUSER_CREATE_INFO;

typedef struct PACKED tag_USER_DELETE_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
} USER_DELETE_INFO, *LPUSER_DELETE_INFO;

typedef struct PACKED tag_USER_LIST_INFO {
    ABI_HEADER Header;
    U32 MaxUsers;
    U32 UserCount;
    STR UserNames[1][MAX_USER_NAME];
} USER_LIST_INFO, *LPUSER_LIST_INFO;

typedef struct PACKED tag_CURRENT_USER_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    U32 Privilege;
    U64 LoginTime;
    U64 SessionID;
} CURRENT_USER_INFO, *LPCURRENT_USER_INFO;

/************************************************************************/
// Socket Syscall Structures

typedef struct PACKED tag_SOCKET_CREATE_INFO {
    ABI_HEADER Header;
    U16 AddressFamily;  // SOCKET_AF_INET
    U16 SocketType;     // SOCKET_TYPE_STREAM, SOCKET_TYPE_DGRAM
    U16 Protocol;       // SOCKET_PROTOCOL_TCP, SOCKET_PROTOCOL_UDP
} SOCKET_CREATE_INFO, *LPSOCKET_CREATE_INFO;

typedef struct PACKED tag_SOCKET_BIND_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    U8 AddressData[16];  // Storage for socket address
    U32 AddressLength;
} SOCKET_BIND_INFO, *LPSOCKET_BIND_INFO;

typedef struct PACKED tag_SOCKET_LISTEN_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    U32 Backlog;
} SOCKET_LISTEN_INFO, *LPSOCKET_LISTEN_INFO;

typedef struct PACKED tag_SOCKET_ACCEPT_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    LPVOID AddressBuffer;
    U32* AddressLength;
} SOCKET_ACCEPT_INFO, *LPSOCKET_ACCEPT_INFO;

typedef struct PACKED tag_SOCKET_CONNECT_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    U8 AddressData[16];  // Storage for socket address
    U32 AddressLength;
} SOCKET_CONNECT_INFO, *LPSOCKET_CONNECT_INFO;

typedef struct PACKED tag_SOCKET_DATA_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    LPVOID Buffer;
    U32 Length;
    U32 Flags;
    U8 AddressData[16];  // For SendTo/ReceiveFrom
    U32 AddressLength;
} SOCKET_DATA_INFO, *LPSOCKET_DATA_INFO;

typedef struct PACKED tag_SOCKET_OPTION_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    U32 Level;
    U32 OptionName;
    LPVOID OptionValue;
    U32 OptionLength;
} SOCKET_OPTION_INFO, *LPSOCKET_OPTION_INFO;

typedef struct PACKED tag_SOCKET_SHUTDOWN_INFO {
    ABI_HEADER Header;
    SOCKET_HANDLE SocketHandle;
    U32 How;  // SOCKET_SHUTDOWN_READ, SOCKET_SHUTDOWN_WRITE, SOCKET_SHUTDOWN_BOTH
} SOCKET_SHUTDOWN_INFO, *LPSOCKET_SHUTDOWN_INFO;

/************************************************************************/
// Socket Address Structures

typedef struct PACKED tag_SOCKET_ADDRESS {
    U16 AddressFamily;
    U8 Data[14];
} SOCKET_ADDRESS, *LPSOCKET_ADDRESS;

typedef struct PACKED tag_SOCKET_ADDRESS_INET {
    U16 AddressFamily;  // SOCKET_AF_INET
    U16 Port;           // Port in network byte order
    U32 Address;        // IPv4 address in network byte order
    U8 Zero[8];         // Padding to 16 bytes
} SOCKET_ADDRESS_INET, *LPSOCKET_ADDRESS_INET;

#define ROOT "/"
#define PATH_SEP ((STR)'/')

/************************************************************************/
// Flags

#define TASK_PRIORITY_LOWEST 0x00
#define TASK_PRIORITY_LOWER 0x04
#define TASK_PRIORITY_MEDIUM 0x08
#define TASK_PRIORITY_HIGHER 0x0C
#define TASK_PRIORITY_HIGHEST 0x10
#define TASK_PRIORITY_CRITICAL 0xFF

#define WAIT_FLAG_ANY 0x00000000
#define WAIT_FLAG_ALL 0x00000001

#define WAIT_INVALID_PARAMETER 0xFFFFFFFF
#define WAIT_TIMEOUT 0x00000102
#define WAIT_OBJECT_0 0x00000000

#define ALLOC_PAGES_RESERVE 0x00000000
#define ALLOC_PAGES_COMMIT 0x00000001
#define ALLOC_PAGES_READONLY 0x00000000
#define ALLOC_PAGES_READWRITE 0x00000002
#define ALLOC_PAGES_UC 0x00000004          // Uncached (for MMIO/BAR mappings)
#define ALLOC_PAGES_WC 0x00000008          // Write-combining (rare; mostly for framebuffers)
#define ALLOC_PAGES_IO 0x00000010          // Exact PMA mapping for IO (BAR) -> do not touch RAM bitmap
#define ALLOC_PAGES_AT_OR_OVER 0x00000020  // If a linear address is specified, can allocate anywhere above it
#define ALLOC_PAGES_FIXED 0x00000040       // Exact PMA mapping owned by another kernel object

#define FILE_OPEN_READ 0x00000001
#define FILE_OPEN_WRITE 0x00000002
#define FILE_OPEN_APPEND 0x00000004
#define FILE_OPEN_EXISTING 0x00000008
#define FILE_OPEN_CREATE_ALWAYS 0x00000010
#define FILE_OPEN_TRUNCATE 0x00000020
#define FILE_OPEN_SEEK_END 0x00000040

/************************************************************************/
// Driver generic functions

#define DF_LOAD 0x0000
#define DF_UNLOAD 0x0001
#define DF_GET_VERSION 0x0002
#define DF_GET_CAPS 0x0003
#define DF_GET_LAST_FUNCTION 0x0004
#define DF_PROBE 0x0005
#define DF_ATTACH 0x0006
#define DF_DETACH 0x0007
#define DF_ENUM_BEGIN 0x0008
#define DF_ENUM_NEXT 0x0009
#define DF_ENUM_END 0x000A
#define DF_ENUM_PRETTY 0x000B
#define DF_DEBUG_INFO 0x000C

#define DF_FIRST_FUNCTION 0x1000

/************************************************************************/
// Error codes common to all EXOS calls

#define DF_RETURN_SUCCESS 0x00000000
#define DF_RETURN_NOT_IMPLEMENTED 0x00000001
#define DF_RETURN_BAD_PARAMETER 0x00000002
#define DF_RETURN_NO_MEMORY 0x00000003
#define DF_RETURN_UNEXPECTED 0x00000004
#define DF_RETURN_INPUT_OUTPUT 0x00000005
#define DF_RETURN_NO_PERMISSION 0x00000006
#define DF_RETURN_NO_MORE 0x00000007
#define DF_RETURN_HARDWARE_ABSENT 0x00000008
#define DF_RETURN_FIRST 0x00001000
#define DF_RETURN_GENERIC 0xFFFFFFFF

/************************************************************************/
// Window styles

#define EWS_VISIBLE 0x0001
#define EWS_ALWAYS_IN_FRONT 0x0002
#define EWS_ALWAYS_AT_BOTTOM 0x0004
#define EWS_SYSTEM_DECORATED 0x0008
#define EWS_CLIENT_DECORATED 0x0010
#define EWS_BARE_SURFACE 0x0020
#define EWS_EXCLUDE_SIBLING_PLACEMENT 0x0040
#define EWS_CLOSE_BUTTON_VISIBLE 0x0080
#define EWS_MINIMIZE_BUTTON_VISIBLE 0x0100
#define EWS_MAXIMIZE_BUTTON_VISIBLE 0x0200
#define EWS_TITLE_BAR_BUTTONS_VISIBLE_MASK \
    (EWS_CLOSE_BUTTON_VISIBLE | EWS_MINIMIZE_BUTTON_VISIBLE | EWS_MAXIMIZE_BUTTON_VISIBLE)
#define EWS_DECORATION_MASK (EWS_SYSTEM_DECORATED | EWS_CLIENT_DECORATED | EWS_BARE_SURFACE)

/************************************************************************/
// Window docking

#define WINDOW_DOCK_HOST_CLASS_NAME TEXT("WindowDockHostClass")
#define WINDOW_DOCKABLE_CLASS_NAME TEXT("WindowDockableClass")

#define WINDOW_DOCK_PROP_ENABLED TEXT("dock.enabled")
#define WINDOW_DOCK_PROP_EDGE TEXT("dock.edge")
#define WINDOW_DOCK_PROP_PRIORITY TEXT("dock.priority")
#define WINDOW_DOCK_PROP_ORDER TEXT("dock.order")
#define WINDOW_DOCK_PROP_SIZE_POLICY TEXT("dock.size.policy")
#define WINDOW_DOCK_PROP_SIZE_PREFERRED TEXT("dock.size.preferred")
#define WINDOW_DOCK_PROP_SIZE_MINIMUM TEXT("dock.size.minimum")
#define WINDOW_DOCK_PROP_SIZE_MAXIMUM TEXT("dock.size.maximum")
#define WINDOW_DOCK_PROP_SIZE_WEIGHT TEXT("dock.size.weight")
#define WINDOW_PROP_BYPASS_PARENT_WORK_RECT TEXT("window.bypass.parent_work_rect")

/************************************************************************/
// Task and window messages

#define ETM_NONE 0x00000000
#define ETM_QUIT 0x00000001
#define ETM_CREATE 0x00000002
#define ETM_DELETE 0x00000003
#define ETM_PAUSE 0x00000004
#define ETM_USB_MASS_STORAGE_MOUNTED 0x00000005
#define ETM_USB_MASS_STORAGE_UNMOUNTED 0x00000006

#define EWM_NONE 0x40000000
#define EWM_CREATE 0x40000001
#define EWM_DELETE 0x40000002
#define EWM_SHOW 0x40000003
#define EWM_HIDE 0x40000004
#define EWM_MOVE 0x40000005
#define EWM_MOVING 0x40000006
#define EWM_SIZE 0x40000007
#define EWM_SIZING 0x40000008
#define EWM_DRAW 0x40000009
#define EWM_KEYDOWN 0x4000000A
#define EWM_KEYUP 0x4000000B
#define EWM_MOUSEMOVE 0x4000000C
#define EWM_MOUSEDOWN 0x4000000D
#define EWM_MOUSEUP 0x4000000E
#define EWM_COMMAND 0x4000000F
#define EWM_NOTIFY 0x40000010
#define EWM_GOTFOCUS 0x40000011
#define EWM_LOSTFOCUS 0x40000012
#define EWM_TIMER 0x40000013
#define EWM_CLEAR 0x40000014
#define EWM_CHILD_APPENDED 0x40000015
#define EWM_CHILD_REMOVED 0x40000016
#define EWM_CLICKED 0x40000017
#define EWM_CLOSE 0x40000018
#define EWM_MAXIMIZE 0x40000019
#define EWM_MINIMIZE 0x4000001A

// Window procedure result contract
#define EWM_NOT_HANDLED 0xFFFFFFFF

// Window notify codes (Param1 when Message == EWM_NOTIFY)
#define EWN_WINDOW_RECT_CHANGED 0x00000001
#define EWN_WINDOW_PROPERTY_CHANGED 0x00000002

// Task messages define by userland apps begin here
#define EM_USER 0x60000000

/************************************************************************/
// Built-in theme tokens

#define THEME_TOKEN_COLOR_DESKTOP_BACKGROUND 0x00001000
#define THEME_TOKEN_COLOR_HIGHLIGHT 0x00001001
#define THEME_TOKEN_COLOR_NORMAL 0x00001002
#define THEME_TOKEN_COLOR_LIGHT_SHADOW 0x00001003
#define THEME_TOKEN_COLOR_DARK_SHADOW 0x00001004
#define THEME_TOKEN_COLOR_CLIENT_BACKGROUND 0x00001005
#define THEME_TOKEN_COLOR_TEXT_NORMAL 0x00001006
#define THEME_TOKEN_COLOR_TEXT_SELECTED 0x00001007
#define THEME_TOKEN_COLOR_SELECTION 0x00001008
#define THEME_TOKEN_COLOR_TITLE_BAR 0x00001009
#define THEME_TOKEN_COLOR_TITLE_BAR_2 0x0000100A
#define THEME_TOKEN_COLOR_TITLE_BAR_FOCUSED 0x0000100B
#define THEME_TOKEN_COLOR_TITLE_BAR_FOCUSED_2 0x0000100C
#define THEME_TOKEN_COLOR_TITLE_TEXT 0x0000100D
#define THEME_TOKEN_COLOR_WINDOW_BORDER 0x0000100E
#define THEME_TOKEN_COLOR_BUTTON_BACKGROUND 0x0000100F
#define THEME_TOKEN_COLOR_BUTTON_BACKGROUND_HOVER 0x00001010
#define THEME_TOKEN_COLOR_BUTTON_BACKGROUND_PRESSED 0x00001011
#define THEME_TOKEN_COLOR_BUTTON_BACKGROUND_DISABLED 0x00001012
#define THEME_TOKEN_COLOR_BUTTON_BORDER 0x00001013
#define THEME_TOKEN_COLOR_BUTTON_BORDER_HOVER 0x00001014
#define THEME_TOKEN_COLOR_BUTTON_BORDER_PRESSED 0x00001015
#define THEME_TOKEN_COLOR_BUTTON_TEXT_DISABLED 0x00001016

#define THEME_TOKEN_METRIC_MINIMUM_WINDOW_WIDTH 0x00002000
#define THEME_TOKEN_METRIC_MINIMUM_WINDOW_HEIGHT 0x00002001
#define THEME_TOKEN_METRIC_MAXIMUM_WINDOW_WIDTH 0x00002002
#define THEME_TOKEN_METRIC_MAXIMUM_WINDOW_HEIGHT 0x00002003
#define THEME_TOKEN_METRIC_TITLE_BAR_HEIGHT 0x00002004
#define THEME_TOKEN_METRIC_CORNER_RADIUS_AUTO 0x00002005

#define THEME_TOKEN_CORNER_STYLE_SQUARE 0x00002800
#define THEME_TOKEN_CORNER_STYLE_ROUNDED 0x00002801
#define THEME_TOKEN_CORNER_STYLE_BEVEL 0x00002802

#define THEME_TOKEN_WINDOW_BACKGROUND_DESKTOP 0x00003000
#define THEME_TOKEN_WINDOW_BACKGROUND_CLIENT 0x00003001
#define THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_NORMAL 0x00003002
#define THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_HOVER 0x00003003
#define THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_PRESSED 0x00003004
#define THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED 0x00003005

/************************************************************************/
// Value for GetSystemMetrics

#define SM_SCREEN_WIDTH 1
#define SM_SCREEN_HEIGHT 2
#define SM_SCREEN_BITS_PER_PIXEL 3
#define SM_MINIMUM_WINDOW_WIDTH 4
#define SM_MINIMUM_WINDOW_HEIGHT 5
#define SM_MAXIMUM_WINDOW_WIDTH 6
#define SM_MAXIMUM_WINDOW_HEIGHT 7
#define SM_SMALL_ICON_WIDTH 8
#define SM_SMALL_ICON_HEIGHT 9
#define SM_LARGE_ICON_WIDTH 10
#define SM_LARGE_ICON_HEIGHT 11
#define SM_MOUSE_CURSOR_WIDTH 12
#define SM_MOUSE_CURSOR_HEIGHT 13
#define SM_TITLE_BAR_HEIGHT 14
#define SM_COLOR_DESKTOP 100
#define SM_COLOR_HIGHLIGHT 101
#define SM_COLOR_NORMAL 102
#define SM_COLOR_LIGHT_SHADOW 103
#define SM_COLOR_DARK_SHADOW 104
#define SM_COLOR_CLIENT 105
#define SM_COLOR_TEXT_NORMAL 106
#define SM_COLOR_TEXT_SELECTED 107
#define SM_COLOR_SELECTION 108
#define SM_COLOR_TITLE_BAR 109
#define SM_COLOR_TITLE_BAR_2 110
#define SM_COLOR_TITLE_TEXT 111

/************************************************************************/
// Values for mouse buttons

#define MB_LEFT 0x0001
#define MB_RIGHT 0x0002
#define MB_MIDDLE 0x0004

/************************************************************************/
// Socket Constants

// Socket Address Family
#define SOCKET_AF_UNSPEC 0
#define SOCKET_AF_INET 2
#define SOCKET_AF_INET6 10

// Socket Type
#define SOCKET_TYPE_STREAM 1  // TCP
#define SOCKET_TYPE_DGRAM 2   // UDP
#define SOCKET_TYPE_RAW 3     // Raw socket

// Socket Protocol
#define SOCKET_PROTOCOL_IP 0
#define SOCKET_PROTOCOL_TCP 6
#define SOCKET_PROTOCOL_UDP 17

// Socket States
#define SOCKET_STATE_CLOSED 0
#define SOCKET_STATE_CREATED 1
#define SOCKET_STATE_BOUND 2
#define SOCKET_STATE_LISTENING 3
#define SOCKET_STATE_CONNECTING 4
#define SOCKET_STATE_CONNECTED 5
#define SOCKET_STATE_CLOSING 6

// Socket Error Codes
#define SOCKET_ERROR_NONE 0
#define SOCKET_ERROR_INVALID -1
#define SOCKET_ERROR_NOMEM -2
#define SOCKET_ERROR_INUSE -3
#define SOCKET_ERROR_NOTBOUND -4
#define SOCKET_ERROR_NOTLISTENING -5
#define SOCKET_ERROR_NOTCONNECTED -6
#define SOCKET_ERROR_WOULDBLOCK -7
#define SOCKET_ERROR_CONNREFUSED -8
#define SOCKET_ERROR_TIMEOUT -9
#define SOCKET_ERROR_MSGSIZE -10
#define SOCKET_ERROR_OVERFLOW -11

// Socket Shutdown Types
#define SOCKET_SHUTDOWN_READ 0
#define SOCKET_SHUTDOWN_WRITE 1
#define SOCKET_SHUTDOWN_BOTH 2

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif /* USER_H_INCLUDED */
