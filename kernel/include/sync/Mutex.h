
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


    Mutex

\************************************************************************/

#ifndef MUTEX_H_INCLUDED
#define MUTEX_H_INCLUDED

/************************************************************************/

#include "utils/List.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/************************************************************************/
// Mutex classes

typedef enum tag_MUTEX_CLASS {
    MUTEX_CLASS_NONE = 0,
    MUTEX_CLASS_KERNEL = 1,
    MUTEX_CLASS_LOG = 2,
    MUTEX_CLASS_MEMORY = 3,
    MUTEX_CLASS_SCHEDULE = 4,
    MUTEX_CLASS_PROCESS = 5,
    MUTEX_CLASS_PROCESS_HEAP = 6,
    MUTEX_CLASS_PROCESS_MESSAGE_QUEUE = 7,
    MUTEX_CLASS_TASK = 8,
    MUTEX_CLASS_TASK_MESSAGE_QUEUE = 9,
    MUTEX_CLASS_DESKTOP = 10,
    MUTEX_CLASS_DESKTOP_TIMER = 11,
    MUTEX_CLASS_WINDOW = 12,
    MUTEX_CLASS_GRAPHICS_CONTEXT = 13,
    MUTEX_CLASS_FILESYSTEM = 14,
    MUTEX_CLASS_FILE = 15,
    MUTEX_CLASS_CONSOLE_STATE = 16,
    MUTEX_CLASS_CONSOLE_RENDER = 17,
    MUTEX_CLASS_USER_ACCOUNT = 18,
    MUTEX_CLASS_SESSION = 19
} MUTEX_CLASS, *LPMUTEX_CLASS;

/************************************************************************/
// The mutex structure

struct tag_MUTEX {
    LISTNODE_FIELDS         // Standard EXOS object fields
    LPPROCESS Owner;        // Process that created this mutex
    LPPROCESS Process;      // Process that has locked this mutex.
    LPTASK Task;            // Task that has locked this mutex.
    U32 DebugClass;         // Lock-order prevention class
    LPCSTR DebugName;       // Optional diagnostic name
    LINEAR DebugOwnerCaller; // Return address of the current owning acquisition site
    UINT Lock;              // Lock count of this mutex.
};

typedef struct tag_MUTEX MUTEX, *LPMUTEX;

// Macro to initialize a mutex

#define EMPTY_MUTEX { \
    .TypeID = KOID_MUTEX, \
    .References = 1, \
    .OwnerProcess = NULL, \
    .Next = NULL, \
    .Prev = NULL, \
    .Process = NULL, \
    .Task = NULL, \
    .DebugClass = MUTEX_CLASS_NONE, \
    .DebugName = NULL, \
    .DebugOwnerCaller = 0, \
    .Lock = 0 \
}

/************************************************************************/
// Mutex shortcuts

#define MUTEX_KERNEL (&KernelMutex)
#define MUTEX_MEMORY (&MemoryMutex)
#define MUTEX_SCHEDULE (&ScheduleMutex)
#define MUTEX_DESKTOP (&DesktopMutex)
#define MUTEX_PROCESS (&ProcessMutex)
#define MUTEX_TASK (&TaskMutex)
#define MUTEX_FILESYSTEM (&FileSystemMutex)
#define MUTEX_FILE (&FileMutex)
#define MUTEX_CONSOLE_STATE (&ConsoleStateMutex)
#define MUTEX_CONSOLE_RENDER (&ConsoleRenderMutex)
#define MUTEX_ACCOUNTS (&UserAccountMutex)
#define MUTEX_SESSION (&SessionMutex)

/************************************************************************/
// Global mutex

extern MUTEX KernelMutex;
extern MUTEX LogMutex;
extern MUTEX MemoryMutex;
extern MUTEX ScheduleMutex;
extern MUTEX DesktopMutex;
extern MUTEX ProcessMutex;
extern MUTEX TaskMutex;
extern MUTEX FileSystemMutex;
extern MUTEX FileMutex;
extern MUTEX ConsoleStateMutex;
extern MUTEX ConsoleRenderMutex;
extern MUTEX UserAccountMutex;
extern MUTEX SessionMutex;

/************************************************************************/
// Mutex API

void InitMutex(LPMUTEX Mutex);
void InitMutexWithDebugInfo(LPMUTEX Mutex, U32 DebugClass, LPCSTR DebugName);
LPMUTEX CreateMutex(void);
BOOL DeleteMutex(LPMUTEX Mutex);
void SetMutexDebugInfo(LPMUTEX Mutex, U32 DebugClass, LPCSTR DebugName);
LPCSTR GetMutexClassName(U32 DebugClass);
UINT LockMutex(LPMUTEX Mutex, UINT Timeout);
BOOL UnlockMutex(LPMUTEX Mutex);

/************************************************************************/

#pragma pack(pop)

#endif  // MUTEX_H_INCLUDED
