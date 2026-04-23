
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


    Process

\************************************************************************/

#ifndef PROCESS_H_INCLUDED
#define PROCESS_H_INCLUDED

/************************************************************************/

#include "Arch.h"
#include "Base.h"
#include "core/Driver.h"
#include "core/ID.h"
#include "exec/Executable.h"
#include "utils/List.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"
#include "core/Security.h"
#include "system/System.h"
#include "User.h"
#include "user/Account.h"
#include "user/UserSession.h"
#include "process/Message.h"
#include "process/Process-Arena.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "utils/RectRegion.h"

/***************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_WINDOW WINDOW, *LPWINDOW;
typedef struct tag_WINDOW_CLASS WINDOW_CLASS, *LPWINDOW_CLASS;
typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;
typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;
typedef struct tag_GRAPHICSCONTEXT GRAPHICSCONTEXT, *LPGRAPHICSCONTEXT;

/************************************************************************/
// Scheduler-owned process state

typedef struct tag_PROCESS_SCHEDULER_STATE {
    BOOL Paused;  // Process-wide scheduler pause state
} PROCESS_SCHEDULER_STATE, *LPPROCESS_SCHEDULER_STATE;

/************************************************************************/
// Task status values

#define TASK_STATUS_FREE 0x00
#define TASK_STATUS_READY 0x01
#define TASK_STATUS_RUNNING 0x02
#define TASK_STATUS_WAITING 0x03
#define TASK_STATUS_SLEEPING 0x04
#define TASK_STATUS_WAITMESSAGE 0x05
#define TASK_STATUS_DEAD 0xFF

// Process status values

#define PROCESS_STATUS_ALIVE 0x00
#define PROCESS_STATUS_DEAD 0xFF

// Kernel process heap size
#ifdef __EXOS_32__
#define KERNEL_PROCESS_HEAP_SIZE N_2MB
#else
#define KERNEL_PROCESS_HEAP_SIZE N_4MB
#endif

// Task stack values

#ifdef __EXOS_32__
#define TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT N_64KB
#define TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT N_16KB
#else
#define TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT N_128KB
#define TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT N_32KB
#endif

UINT TaskGetMinimumTaskStackSize(void);
UINT TaskGetMinimumSystemStackSize(void);

#define TASK_MINIMUM_TASK_STACK_SIZE TaskGetMinimumTaskStackSize()
#define TASK_MINIMUM_SYSTEM_STACK_SIZE TaskGetMinimumSystemStackSize()

#define STACK_SAFETY_MARGIN 256

// Task creation flags

#define TASK_CREATE_SUSPENDED 0x00000001
#define TASK_CREATE_MAIN_KERNEL 0x00000002

// Process creation flags

#define PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH 0x00000001

/************************************************************************/
// Desktop modes

#define DESKTOP_MODE_CONSOLE 0x00000000
#define DESKTOP_MODE_GRAPHICS 0x00000001

/************************************************************************/
// Desktop cursor rendering path

#define DESKTOP_CURSOR_PATH_UNSET 0x00000000
#define DESKTOP_CURSOR_PATH_HARDWARE 0x00000001
#define DESKTOP_CURSOR_PATH_SOFTWARE 0x00000002

/************************************************************************/
// Desktop cursor fallback reason

#define DESKTOP_CURSOR_FALLBACK_NONE 0x00000000
#define DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS 0x00000001
#define DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES 0x00000002
#define DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE 0x00000003
#define DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED 0x00000004
#define DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED 0x00000005
#define DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED 0x00000006

/************************************************************************/
// Window status bit values

#define WINDOW_STATUS_VISIBLE 0x0001
#define WINDOW_STATUS_NEED_DRAW 0x0002
#define WINDOW_STATUS_DRAWING 0x0004
#define WINDOW_STATUS_HAS_WORK_RECT 0x0008
#define WINDOW_STATUS_BYPASS_PARENT_WORK_RECT 0x0010
#define WINDOW_STATUS_CONTENT_TRANSPARENT 0x0020

/************************************************************************/
// Other window values

#define WINDOW_DIRTY_REGION_CAPACITY 32
#define WINDOW_DRAW_CONTEXT_ACTIVE 0x00000001
#define WINDOW_DRAW_CONTEXT_CLIENT_COORDINATES 0x00000002
#define WINDOW_CONTENT_TRANSPARENCY_HINT_AUTO 0x00000000
#define WINDOW_CONTENT_TRANSPARENCY_HINT_OPAQUE 0x00000001
#define WINDOW_CONTENT_TRANSPARENCY_HINT_TRANSPARENT 0x00000002

/************************************************************************/

struct tag_PROCESS {
    LISTNODE_FIELDS                                         // Standard EXOS object fields
        MUTEX Mutex;                                        // This structure's mutex
    MUTEX HeapMutex;                                        // This structure's mutex for heap allocation
    SECURITY Security;                                      // Security attributes
    U32 Privilege;                                          // This process' privilege level
    U32 Status;                                             // (alive/dead)
    U32 Flags;                                              // Process creation flags
    U32 ControlFlags;                                       // Process control state (interrupt)
    PROCESS_SCHEDULER_STATE SchedulerState;                 // Scheduler-owned ISR-visible state
    PHYSICAL PageDirectory;                                 // Physical address of this process' page directory
    LINEAR HeapBase;                                        // Base virtual address of the process heap
    UINT HeapSize;                                          // Process heap size in bytes
    UINT MaximumAllocatedMemory;                            // Peak allocated heap memory in bytes
    UINT ExitCode;                                          // Exit code
    STR FileName[MAX_PATH_NAME];                            // Executable file path
    STR CommandLine[MAX_PATH_NAME];                         // Command line used to start this process
    STR WorkFolder[MAX_PATH_NAME];                          // Process working folder
    HANDLE StdOut;                                          // Standard output handle owned by the process
    HANDLE StdIn;                                           // Standard input handle owned by the process
    HANDLE StdErr;                                          // Standard error handle owned by the process
    UINT TaskCount;                                         // Number of active tasks in this process
    MESSAGEQUEUE MessageQueue;                              // Process-level message queue (input, etc.)
    U64 UserID;                                             // Owner user
    LPDESKTOP Desktop;                                      // This process' desktop
    LPUSER_SESSION Session;                                 // User session
    LPFILESYSTEM PackageFileSystem;                         // Mounted package filesystem tied to this process
    EXECUTABLE_METADATA MainExecutableMetadata;              // Main executable metadata for module symbol resolution
    LINEAR MainExecutableCodeBase;                           // Installed main executable code base
    LINEAR MainExecutableDataBase;                           // Installed main executable data base
    LPLIST ModuleBindings;                                  // Process-owned executable module bindings
    UINT ModuleBindingCount;                                // Number of executable module bindings
    MEMORY_REGION_LIST MemoryRegionList;                    // Memory region descriptors owned by this process
    PROCESS_ADDRESS_SPACE AddressSpace;                      // Process virtual address space arena state
};

typedef struct tag_PROPERTY {
    LISTNODE_FIELDS  // Standard EXOS object fields
    STR Name[32];    // Property name
    UINT Value;      // Property value
} PROPERTY, *LPPROPERTY;

struct tag_WINDOW {
    LISTNODE_FIELDS                                 // Standard EXOS object fields
        MUTEX Mutex;                                // This window's mutex
    LPTASK Task;                                    // The task that created this window
    WINDOWFUNC Function;                            // The function that manages this window
    LPWINDOW ParentWindow;                          // The parent of this window
    LPLIST Children;                                // The children of this window
    LPLIST Properties;                              // The user-defined properties of this window
    LPWINDOW_CLASS Class;                           // Window class metadata
    LPVOID ClassData;                               // Window class private data
    U32 DrawContextFlags;                           // Active draw context state flags
    U32 WindowID;                                   // Window identifier for handle-based lookup
    U32 Style;                                      // Window style flags
    U32 Status;                                     // Window status flags
    U32 ContentTransparencyHint;                    // Declared content transparency policy
    U32 Level;                                      // Window level within its desktop layer
    I32 Order;                                      // Window ordering key within its level
    STR Caption[MAX_WINDOW_CAPTION];                // Window caption text
    POINT DrawOrigin;                               // Drawing origin in window coordinates
    RECT Rect;                                      // The rectangle of this window
    RECT ScreenRect;                                // Cached window rectangle in screen coordinates
    RECT WorkRect;                                  // Client work rectangle for child layout
    RECT DirtyRects[WINDOW_DIRTY_REGION_CAPACITY];  // Pending dirty rectangles for redraw
    RECT_REGION DirtyRegion;                        // Coalesced dirty region for redraw
    RECT DrawSurfaceRect;                           // Active draw surface bounds
    RECT DrawClipRect;                              // Active draw clipping bounds
};

typedef struct tag_WINDOW_CLASS {
    LISTNODE_FIELDS                 // Standard EXOS object fields
        STR Name[64];               // Unique class name
    LPWINDOW_CLASS BaseClass;       // Base class in inheritance chain
    WINDOWFUNC Function;            // Class window procedure
    U32 ClassID;                    // Class identifier for handle-based lookup
    U32 ClassDataSize;              // Optional class-private data size
} WINDOW_CLASS, *LPWINDOW_CLASS;

typedef struct tag_DESKTOP_THEME {
    LPVOID Builtin;                        // Built-in fallback theme runtime
    LPVOID Active;                         // Active theme runtime
    LPVOID Staged;                         // Candidate theme runtime pending activation
    STR ActivePath[MAX_FILE_NAME];         // File path of the active file-backed theme
    STR StagedPath[MAX_FILE_NAME];         // File path of the staged file-backed theme
    U32 LastStatus;                        // Last theme load or activation status
    U32 LastFallbackReason;                // Last theme fallback reason
    BOOL ActiveFromFile;                   // Indicates whether the active theme came from a file
} DESKTOP_THEME, *LPDESKTOP_THEME;

typedef struct tag_DESKTOP_DISPLAY_SELECTION {
    STR BackendAlias[MAX_NAME];        // Selected graphics backend alias
    GRAPHICS_MODE_INFO ModeInfo;       // Selected graphics mode information
    BOOL IsAssigned;                   // Indicates whether a display selection is stored
} DESKTOP_DISPLAY_SELECTION, *LPDESKTOP_DISPLAY_SELECTION;

typedef struct tag_MOUSE_CURSOR {
    I32 X;                // Cursor X position in screen coordinates
    I32 Y;                // Cursor Y position in screen coordinates
    U32 Width;            // Cursor width in pixels
    U32 Height;           // Cursor height in pixels
    BOOL Visible;         // Cursor visibility state
    RECT ClipRect;        // Cursor clipping rectangle in screen coordinates
    U32 RenderPath;       // Active cursor rendering path identifier
    U32 FallbackReason;   // Last fallback reason for cursor rendering path
    I32 PendingX;         // Pending cursor X target for deferred apply
    I32 PendingY;         // Pending cursor Y target for deferred apply
    BOOL SoftwareDirty;   // Software cursor overlay requires redraw
} MOUSE_CURSOR, *LPMOUSE_CURSOR;

struct tag_DESKTOP {
    LISTNODE_FIELDS                 // Standard EXOS object fields
        MUTEX Mutex;                // This structure's mutex
    LPTASK Task;                    // The task that created this desktop
    LPDRIVER Graphics;              // This desktop's graphics driver
    LPWINDOW Window;                // Window of the desktop
    LPWINDOW Capture;               // Window that captured mouse
    LPWINDOW LastMouseMoveTarget;   // Window that last received mouse move dispatch
    I32 CaptureOffsetX;             // Mouse offset X in captured window on drag start
    I32 CaptureOffsetY;             // Mouse offset Y in captured window on drag start
    MUTEX TimerMutex;               // Protect desktop timers
    LPLIST Timers;                  // Per-desktop timer entries
    LPTASK TimerTask;               // Per-desktop timer worker task
    LPWINDOW Focus;                 // Window that has focus
    U32 Mode;                       // Active desktop display mode
    I32 Order;                      // Desktop ordering key among active desktops
    LPGRAPHICSCONTEXT GraphicsContext;  // Desktop graphics context
    LINEAR GraphicsShadowBufferLinear;  // Virtual base of the graphics shadow buffer
    UINT GraphicsShadowBufferSize;      // Graphics shadow buffer size in bytes
    U32 PendingComponents;          // Pending desktop-owned component injection flags
    MOUSE_CURSOR Cursor;            // Desktop cursor runtime state
    DESKTOP_DISPLAY_SELECTION DisplaySelection;  // Stored graphics backend and mode selection
};

/************************************************************************/
// Global objects

extern PROCESS KernelProcess;

/************************************************************************/
// Functions in Process.c

void InitializeKernelProcess(void);
void DumpProcess(LPPROCESS);
void KillProcess(LPPROCESS);
void DeleteProcessCommit(LPPROCESS);
void InitSecurity(LPSECURITY);
BOOL CreateProcess(LPPROCESS_INFO);
UINT Spawn(LPCSTR, LPCSTR);
void SetProcessStatus(LPPROCESS Process, U32 Status);
LINEAR GetProcessHeap(LPPROCESS);
LPMEMORY_REGION_LIST GetProcessMemoryRegionList(LPPROCESS Process);
void MemoryRegionDescriptorAssignOwner(LPMEMORY_REGION_DESCRIPTOR Descriptor, LPPROCESS Process);

/***************************************************************************/

#pragma pack(pop)

#endif  // PROCESS_H_INCLUDED
