
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


    Process manager

\************************************************************************/

#include "process/Process.h"

#include "console/Console.h"
#include "core/Driver.h"
#include "exec/Executable.h"
#include "fs/File.h"
#include "core/Kernel.h"
#include "memory/Heap.h"
#include "process/Process-Module.h"
#include "utils/List.h"
#include "log/Log.h"
#include "text/CoreString.h"
#if defined(__EXOS_ARCH_X86_32__)
    #include "arch/x86-32/x86-32-Log.h"
#endif

/***************************************************************************/

PROCESS DATA_SECTION KernelProcess = {
    .TypeID = KOID_PROCESS,  // ID
    .References = 1,   // References
    .OwnerProcess = NULL, // OwnerProcess (from LISTNODE_FIELDS)
    .Next = NULL,
    .Prev = NULL,                   // Next, previous
    .Desktop = NULL,                // Desktop
    .Privilege = CPU_PRIVILEGE_KERNEL,  // Privilege
    .Status = PROCESS_STATUS_ALIVE, // Status
    .Flags = PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH, // Flags
    .ControlFlags = 0,             // Process control flags
    .SchedulerState = {.Paused = FALSE},
    .PageDirectory = 0,             // Page directory
    .MemoryRegionList = { .Head = NULL, .Tail = NULL, .Count = 0 },
    .HeapBase = 0,                  // Heap base
    .HeapSize = 0,                  // Heap size
    .TaskCount = 0                  // Task count (will be incremented by KernelCreateTask)
};

/***************************************************************************/

#define KERNEL_PROCESS_VER_MAJOR 1
#define KERNEL_PROCESS_VER_MINOR 0

static UINT KernelProcessDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION KernelProcessDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = KERNEL_PROCESS_VER_MAJOR,
    .VersionMinor = KERNEL_PROCESS_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "KernelProcess",
    .Alias = "kernel_process",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = KernelProcessDriverCommands};

/***************************************************************************/

/**
 * @brief Retrieves the kernel process driver descriptor.
 * @return Pointer to the kernel process driver.
 */
LPDRIVER KernelProcessGetDriver(void) {
    return &KernelProcessDriver;
}

/***************************************************************************/

/**
 * @brief Initialize the kernel process and main task.
 *
 * Prepare the kernel heap, set up the kernel process fields and create the
 * primary kernel task.
 */
void InitializeKernelProcess(void) {
    TRACED_FUNCTION;

    TASK_INFO TaskInfo;

    DEBUG(TEXT("Enter"));

    InitMutex(&(KernelProcess.Mutex));
    InitMutex(&(KernelProcess.HeapMutex));
    InitSecurity(&(KernelProcess.Security));
    KernelProcess.PageDirectory = GetPageDirectory();
    KernelProcess.MaximumAllocatedMemory = N_HalfMemory;
    KernelProcess.HeapSize = KERNEL_PROCESS_HEAP_SIZE;
    StringCopy(KernelProcess.WorkFolder, TEXT(ROOT));

    DEBUG(TEXT("Memory : %u"), KernelStartup.MemorySize);
    DEBUG(TEXT("Pages : %u"), KernelStartup.PageCount);

    LINEAR HeapPreferredBase = GetKernelHeapPreferredBase(KernelProcess.HeapSize);
    LINEAR HeapBase = AllocRegion(HeapPreferredBase,
                                  0,
                                  KernelProcess.HeapSize,
                                  ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
                                  TEXT("KernelHeap"));

    DEBUG(TEXT("HeapPreferredBase : %p"), (LINEAR)HeapPreferredBase);
    DEBUG(TEXT("HeapBase : %p"), (LINEAR)HeapBase);

    if (HeapBase == NULL) {
        DEBUG(TEXT("Could not create kernel heap, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    KernelProcess.HeapBase = (LINEAR)HeapBase;
    HeapInit(&KernelProcess, KernelProcess.HeapBase, KernelProcess.HeapSize);
    if (!InitializeProcessModuleBindings(&KernelProcess)) {
        ERROR(TEXT("Could not initialize kernel process module bindings"));
        DO_THE_SLEEPING_BEAUTY;
    }

    if (ProcessArenaInitializeKernel(&KernelProcess) == FALSE) {
        ERROR(TEXT("Could not initialize kernel process arenas"));
        DO_THE_SLEEPING_BEAUTY;
    }

    MemorySet(&(KernelProcess.MessageQueue), 0, sizeof(MESSAGEQUEUE));
    InitMessageQueue(&(KernelProcess.MessageQueue));
    if (EnsureProcessMessageQueue(&KernelProcess, TRUE) == FALSE) {
        ERROR(TEXT("Could not initialize kernel process message queue"));
        DO_THE_SLEEPING_BEAUTY;
    }

    StringCopy(KernelProcess.FileName, KernelStartup.CommandLine);
    StringCopy(KernelProcess.CommandLine, KernelStartup.CommandLine);

    TaskInfo.Header.Size = sizeof(TASK_INFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = (TASKFUNC)InitializeKernel;
    TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWEST;
    TaskInfo.Flags = TASK_CREATE_MAIN_KERNEL;
    StringCopy(TaskInfo.Name, TEXT("KernelMain"));

    LPTASK KernelTask = KernelCreateTask(&KernelProcess, &TaskInfo);

    if (KernelTask == NULL) {
        DEBUG(TEXT("Could not create kernel task, halting."));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("Kernel main task = %p (%s)"), (LINEAR)KernelTask, KernelTask->Name);

    KernelTask->Type = TASK_TYPE_KERNEL_MAIN;

    DEBUG(TEXT("Exit"));

    TRACED_EPILOGUE("InitializeKernelProcess");
}

/***************************************************************************/

/**
 * @brief Driver command handler for the kernel process initialization.
 */
static UINT KernelProcessDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((KernelProcessDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeKernelProcess();
            KernelProcessDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((KernelProcessDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            KernelProcessDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(KERNEL_PROCESS_VER_MAJOR, KERNEL_PROCESS_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a new user process structure.
 *
 * @return Pointer to the new PROCESS or NULL on failure.
 */
LPPROCESS NewProcess(void) {
    TRACED_FUNCTION;

    LPPROCESS This = NULL;

    DEBUG(TEXT("Enter"));

    This = (LPPROCESS)CreateKernelObject(sizeof(PROCESS), KOID_PROCESS);

    if (This == NULL) {
        TRACED_EPILOGUE("NewProcess");
        return NULL;
    }

    // Zero out non-LISTNODE_FIELDS (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&This->Mutex, 0, sizeof(PROCESS) - sizeof(LISTNODE));

    LPLIST DesktopList = GetDesktopList();
    if (DesktopList != NULL && DesktopList->First != NULL) {
        This->Desktop = (LPDESKTOP)DesktopList->First;
    } else {
        This->Desktop = NULL;
    }
    This->Privilege = CPU_PRIVILEGE_USER;
    This->Status = PROCESS_STATUS_ALIVE;
    This->Flags = 0; // Will be set by CreateProcess
    This->ControlFlags = 0;
    This->SchedulerState.Paused = FALSE;
    This->MaximumAllocatedMemory = N_HalfMemory;
    This->TaskCount = 0;
    This->StdOut = 0;
    This->StdIn = 0;
    This->StdErr = 0;
    This->Session = NULL;
    ProcessArenaReset(This);

    // Inherit session from parent process
    SAFE_USE_VALID_ID(This->OwnerProcess, KOID_PROCESS) {
        This->Session = This->OwnerProcess->Session;
        if (This->Session != NULL) {
            This->UserID = This->Session->UserID;
        } else {
            This->UserID = This->OwnerProcess->UserID;
        }
    }

    //-------------------------------------
    // Initialize the process' mutex

    InitMutex(&(This->Mutex));
    InitMutex(&(This->HeapMutex));
    if (!InitializeProcessModuleBindings(This)) {
        ReleaseKernelObject(This);
        TRACED_EPILOGUE("NewProcess");
        return NULL;
    }

    //-------------------------------------
    // Initialize the process' security

    InitSecurity(&(This->Security));

    DEBUG(TEXT("Exit"));

    TRACED_EPILOGUE("NewProcess");
    return This;
}

/***************************************************************************/

/**
 * @brief Actually delete a single process (the original DeleteProcess logic).
 *
 * @param This The process to delete.
 */
void DeleteProcessCommit(LPPROCESS This) {
    TRACED_FUNCTION;

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        if (This == &KernelProcess) {
            ERROR(TEXT("Cannot delete kernel process"));
            TRACED_EPILOGUE("DeleteProcessCommit");
            return;
        }

        DEBUG(TEXT("Deleting process %s (TaskCount=%u)"), This->FileName, This->TaskCount);

        if (GetFocusedProcess() == This) {
            SetFocusedProcess(&KernelProcess);
        }

        DeleteProcessModuleBindings(This);

        if (This->StdIn != 0) {
            CloseHandle(This->StdIn);
            This->StdIn = 0;
        }

        if (This->StdOut != 0) {
            CloseHandle(This->StdOut);
            This->StdOut = 0;
        }

        if (This->StdErr != 0) {
            CloseHandle(This->StdErr);
            This->StdErr = 0;
        }

        // Free page directory if allocated
        // TODO : FREE ALL PD PAGES
        if (This->PageDirectory != 0) {
            DEBUG(TEXT("Freeing page directory %p"), (LINEAR)This->PageDirectory);
            FreePhysicalPage(This->PageDirectory);
        }

        // Free process heap if allocated
        if (This->HeapBase != 0 && This->HeapSize != 0) {
            DEBUG(TEXT("Freeing process heap base=%p size=%x"), (LINEAR)This->HeapBase,
                (UINT)This->HeapSize);
            FreeRegionForProcess(This, This->HeapBase, This->HeapSize);
        }

        if (This->MessageQueue.MessageBufferBase != 0 && This->MessageQueue.MessageBufferSize > 0) {
            KernelHeapFree((LPVOID)This->MessageQueue.MessageBufferBase);
            This->MessageQueue.MessageBufferBase = 0;
            This->MessageQueue.MessageBufferSize = 0;
        }
        DeleteMessageQueue(&(This->MessageQueue));

        ReleaseKernelObject(This);

        DEBUG(TEXT("Process deleted"));
    }

    TRACED_EPILOGUE("DeleteProcessCommit");
}

/***************************************************************************/

void KillProcess(LPPROCESS This) {
    TRACED_FUNCTION;

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        if (This == &KernelProcess) {
            ERROR(TEXT("Cannot delete kernel process"));
            TRACED_EPILOGUE("KillProcess");
            return;
        }

        DEBUG(TEXT("Killing process %s and all its children"), This->FileName);

        // Lock the process list early and keep it locked throughout the entire operation
        LockMutex(MUTEX_PROCESS, INFINITY);

        // Create a temporary list to collect all child processes
        LPLIST ChildProcesses = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        if (ChildProcesses == NULL) {
            ERROR(TEXT("Failed to create temporary list"));
            UnlockMutex(MUTEX_PROCESS);
            TRACED_EPILOGUE("KillProcess");
            return;
        }

        // Find all child processes recursively
        BOOL FoundChildren = TRUE;
        LPLIST ProcessesToCheck = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        ListAddItem(ProcessesToCheck, This);
        LPLIST ProcessList = GetProcessList();
        LPLIST TaskList = GetTaskList();

        while (FoundChildren) {
            FoundChildren = FALSE;
            LPPROCESS Current = (LPPROCESS)ProcessList->First;

            while (Current != NULL) {
                SAFE_USE_VALID_ID(Current, KOID_PROCESS) {
                    // Check if this process has a parent in our check list
                    for (UINT i = 0; i < ListGetSize(ProcessesToCheck); i++) {
                        LPPROCESS ParentToCheck = (LPPROCESS)ListGetItem(ProcessesToCheck, (U32)i);

                        if (Current->OwnerProcess == ParentToCheck && Current != This) {
                            // Check if this child is not already in the list
                            BOOL AlreadyInList = FALSE;

                            for (UINT j = 0; j < ListGetSize(ChildProcesses); j++) {
                                if (ListGetItem(ChildProcesses, (U32)j) == Current) {
                                    AlreadyInList = TRUE;
                                    break;
                                }
                            }

                            if (!AlreadyInList) {
                                ListAddItem(ChildProcesses, Current);
                                ListAddItem(ProcessesToCheck, Current);
                                FoundChildren = TRUE;
                                DEBUG(TEXT("Found child process %s"), Current->FileName);
                            }
                            break;
                        }
                    }
                }
                Current = (LPPROCESS)Current->Next;
            }
        }

        DeleteList(ProcessesToCheck);

        // Process child processes according to parent's policy
        UINT ChildCount = ListGetSize(ChildProcesses);
        DEBUG(TEXT("Processing %u child processes"), ChildCount);

        if (This->Flags & PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH) {
            DEBUG(TEXT("Policy: KILL_CHILDREN_ON_DEATH - killing all children"));

            for (UINT i = 0; i < ChildCount; i++) {
                LPPROCESS ChildProcess = (LPPROCESS)ListGetItem(ChildProcesses, (U32)i);
                SAFE_USE_VALID_ID(ChildProcess, KOID_PROCESS) {
                    DEBUG(TEXT("Killing tasks of child process %s"), ChildProcess->FileName);

                    // Kill all tasks of this child process
                    LPTASK Task = (LPTASK)TaskList->First;
                    while (Task != NULL) {
                        LPTASK NextTask = (LPTASK)Task->Next;
                        SAFE_USE_VALID_ID(Task, KOID_TASK) {
                            if (Task->OwnerProcess == ChildProcess) {
                                DEBUG(TEXT("Killing task %s"), Task->Name);
                                KernelKillTask(Task);
                            }
                        }
                        Task = NextTask;
                    }

                    // Mark the child process as DEAD
                    SetProcessStatus(ChildProcess, PROCESS_STATUS_DEAD);
                }
            }
        } else {
            DEBUG(TEXT("Policy: ORPHAN_CHILDREN - detaching children from parent"));

            for (UINT i = 0; i < ChildCount; i++) {
                LPPROCESS ChildProcess = (LPPROCESS)ListGetItem(ChildProcesses, (U32)i);
                SAFE_USE_VALID_ID(ChildProcess, KOID_PROCESS) {
                    // Detach child from parent (make it orphan)
                    ChildProcess->OwnerProcess = NULL;
                    DEBUG(TEXT("Detached child process %s from parent"), ChildProcess->FileName);
                }
            }
        }

        // Clean up the temporary list
        DeleteList(ChildProcesses);

        // Kill all tasks of the target process itself
        DEBUG(TEXT("Killing tasks of target process %s"), This->FileName);

        LPTASK Task = (LPTASK)TaskList->First;
        while (Task != NULL) {
            LPTASK NextTask = (LPTASK)Task->Next;
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->OwnerProcess == This) {
                    DEBUG(TEXT("Killing task %s"), Task->Name);
                    KernelKillTask(Task);
                }
            }
            Task = NextTask;
        }

        // Mark the target process as DEAD
        SetProcessStatus(This, PROCESS_STATUS_DEAD);

        // Finally unlock the process mutex
        UnlockMutex(MUTEX_PROCESS);

        DEBUG(TEXT("Process and children marked for deletion"));
    }

    TRACED_EPILOGUE("KillProcess");
}

/************************************************************************/

/**
 * @brief Create a new process from an executable file.
 *
 * @param Info Pointer to a PROCESS_INFO describing the executable.
 * @return TRUE on success, FALSE on failure.
 */
BOOL CreateProcess(LPPROCESS_INFO Info) {
    TRACED_FUNCTION;

    EXECUTABLE_METADATA ExecutableMetadata;
    TASK_INFO TaskInfo;
    FILE_OPEN_INFO FileOpenInfo;
    LPPROCESS Process = NULL;
    LPPROCESS ParentProcess = NULL;
    LPTASK Task = NULL;
    LPFILE File = NULL;
    PHYSICAL PageDirectory = NULL;
    LINEAR CodeBase = NULL;
    LINEAR DataBase = NULL;
    LINEAR HeapBase = NULL;
    UINT FileSize = 0;
    UINT CodeSize = 0;
    UINT DataSize = 0;
    UINT HeapSize = 0;
    UINT StackSize = 0;
    UINT TotalSize = 0;
    BOOL Result = FALSE;
    HANDLE SourceStdOut = 0;
    HANDLE SourceStdIn = 0;
    HANDLE SourceStdErr = 0;

    DEBUG(TEXT("Enter"));

    if (Info == NULL) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TASK_INFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;

    StringCopy(TaskInfo.Name, TEXT("UserMain"));

    //-------------------------------------
    // Extract filename from CommandLine

    STR FileName[MAX_PATH_NAME];
    LPCSTR CommandLineStart;
    INT i;

    // Find the first space or end of string to extract filename
    for (i = 0; i < MAX_PATH_NAME - 1 && Info->CommandLine[i] != STR_NULL && Info->CommandLine[i] != STR_SPACE; i++) {
        FileName[i] = Info->CommandLine[i];
    }
    FileName[i] = STR_NULL;

    // CommandLine starts after the filename and any spaces
    CommandLineStart = Info->CommandLine;
    while (*CommandLineStart != STR_NULL && *CommandLineStart != STR_SPACE) CommandLineStart++;
    while (*CommandLineStart == STR_SPACE) CommandLineStart++;

    //-------------------------------------
    // Open the executable file

    DEBUG(TEXT("Opening file %s"), FileName);

    FileOpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    if (File == NULL) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    //-------------------------------------
    // Read the size of the file

    FileSize = GetFileSize(File);

    if (FileSize == 0) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    DEBUG(TEXT("File size %u"), FileSize);

    //-------------------------------------
    // Get executable information

    if (GetExecutableImageInfo(File, &ExecutableMetadata) == FALSE) {
        TRACED_EPILOGUE("CreateProcess");
        return FALSE;
    }

    CloseFile(File);

    //-------------------------------------
    // Check executable information

    if (ExecutableMetadata.Layout.CodeSize == 0) return FALSE;

    //-------------------------------------
    // Lock access to kernel data

    LockMutex(MUTEX_KERNEL, INFINITY);

    //-------------------------------------
    // Allocate a new process structure

    DEBUG(TEXT("Allocating process"));

    Process = NewProcess();
    if (Process == NULL) goto Out;

    StringCopy(Process->FileName, FileName);

    // Initialize CommandLine (could be empty if not provided)
    if (StringEmpty(Info->CommandLine) == FALSE) {
        StringCopy(Process->CommandLine, Info->CommandLine);
    } else {
        StringClear(Process->CommandLine);
    }

    ParentProcess = GetCurrentProcess();

    // Initialize WorkFolder from PROCESS_INFO or inherit from parent
    if (!StringEmpty(Info->WorkFolder)) {
        StringCopy(Process->WorkFolder, Info->WorkFolder);
    } else {
        SAFE_USE_VALID_ID(ParentProcess, KOID_PROCESS) {
            StringCopy(Process->WorkFolder, ParentProcess->WorkFolder);
        } else {
            StringCopy(Process->WorkFolder, TEXT(ROOT));
        }
    }

    // Update returned PROCESS_INFO with effective WorkFolder
    StringCopy(Info->WorkFolder, Process->WorkFolder);

    // Copy process creation flags
    Process->Flags = Info->Flags;

    SourceStdOut = EnsureHandle((LINEAR)Info->StdOut);
    SourceStdIn = EnsureHandle((LINEAR)Info->StdIn);
    SourceStdErr = EnsureHandle((LINEAR)Info->StdErr);

    SAFE_USE_VALID_ID(ParentProcess, KOID_PROCESS) {
        if (SourceStdOut == 0) {
            SourceStdOut = ParentProcess->StdOut;
        }

        if (SourceStdIn == 0) {
            SourceStdIn = ParentProcess->StdIn;
        }

        if (SourceStdErr == 0) {
            SourceStdErr = ParentProcess->StdErr;
        }
    }

    if (SourceStdOut != 0) {
        Process->StdOut = DuplicateHandle(SourceStdOut);
        if (Process->StdOut == 0) {
            goto Out;
        }
    }

    if (SourceStdIn != 0) {
        Process->StdIn = DuplicateHandle(SourceStdIn);
        if (Process->StdIn == 0) {
            goto Out;
        }
    }

    if (SourceStdErr != 0) {
        Process->StdErr = DuplicateHandle(SourceStdErr);
        if (Process->StdErr == 0) {
            goto Out;
        }
    }

    CodeSize = ExecutableMetadata.Layout.CodeSize;
    DataSize = ExecutableMetadata.Layout.DataSize;
    HeapSize = ExecutableMetadata.Layout.HeapRequested;
    StackSize = ExecutableMetadata.Layout.StackRequested;

    if (HeapSize < N_64KB) {
        HeapSize = N_64KB;
    }

    if (StackSize < TASK_MINIMUM_TASK_STACK_SIZE) {
        StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    }

    //-------------------------------------
    // Compute addresses

    CodeBase = VMA_USER;
    DataBase = CodeBase + CodeSize;

    while (DataBase & N_4KB_M1) DataBase++;  // Align 4K

    HeapBase = DataBase + DataSize;

    while (HeapBase & N_4KB_M1) HeapBase++;  // Align 4K

    //-------------------------------------
    // Compute total size

    TotalSize = (HeapBase + HeapSize) - VMA_USER;

    //-------------------------------------

    FreezeScheduler();

    //-------------------------------------
    // Allocate and setup the page directory

    Process->PageDirectory = AllocUserPageDirectory();

    if (Process->PageDirectory == NULL) {
        ERROR(TEXT("Failed to allocate page directory"));
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    //-------------------------------------
    // We can use the new page directory from now on
    // and switch back to the previous when done

    DEBUG(TEXT("Switching page directory to new process : %p"), (LINEAR)Process->PageDirectory);

    PageDirectory = GetCurrentProcess()->PageDirectory;

    LoadPageDirectory(Process->PageDirectory);

#if defined(__EXOS_ARCH_X86_32__)
    LogPageDirectory(Process->PageDirectory);
#endif

    //-------------------------------------
    // Allocate enough memory for the code, data and heap

    DEBUG(TEXT("Allocating process space"));

    if (AllocRegionForProcess(Process, VMA_USER, 0, TotalSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("ProcessSpace")) == NULL) {
        ERROR(TEXT("Failed to allocate process space"));
        LoadPageDirectory(PageDirectory);
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    //-------------------------------------
    // Open the executable file

    FileOpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = FileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    File = OpenFile(&FileOpenInfo);

    //-------------------------------------
    // Load executable image
    // For tests, image must be at VMA_KERNEL

    DEBUG(TEXT("Loading executable"));

    EXECUTABLE_LOAD LoadInfo;
    LoadInfo.File = File;
    LoadInfo.Info = &(ExecutableMetadata.Layout);
    LoadInfo.CodeBase = (LINEAR)CodeBase;
    LoadInfo.DataBase = (LINEAR)DataBase;
    LoadInfo.BssBase = (LINEAR)DataBase;

    if (LoadExecutable(&LoadInfo) == FALSE) {
        DEBUG(TEXT("Load failed !"));

        FreeRegionForProcess(Process, VMA_USER, TotalSize);
        LoadPageDirectory(PageDirectory);
        UnfreezeScheduler();
        CloseFile(File);
        goto Out;
    }

    CloseFile(File);

    //-------------------------------------
    // Initialize the heap

    Process->HeapBase = HeapBase;
    Process->HeapSize = HeapSize;
    Process->MainExecutableMetadata = ExecutableMetadata;
    Process->MainExecutableCodeBase = CodeBase;
    Process->MainExecutableDataBase = DataBase;

    if (ProcessArenaInitializeUser(Process, CodeBase, CodeSize + DataSize, HeapBase, HeapSize) == FALSE) {
        ERROR(TEXT("Failed to initialize process address space arenas"));
        FreeRegionForProcess(Process, VMA_USER, TotalSize);
        LoadPageDirectory(PageDirectory);
        UnfreezeScheduler();
        goto Out;
    }

    HeapInit(Process, Process->HeapBase, Process->HeapSize);
    ProcessArenaConfigureMainHeap(Process);

    // HeapDump(KernelProcess.HeapBase, KernelProcess.HeapSize);
    // HeapDump(Process->HeapBase, Process->HeapSize);

    //-------------------------------------
    // Create the initial task

    DEBUG(TEXT("Creating initial task"));

    TaskInfo.Func =
        (TASKFUNC)(CodeBase + (ExecutableMetadata.Layout.EntryPoint - ExecutableMetadata.Layout.CodeBase));
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = StackSize;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = TASK_CREATE_SUSPENDED;

    Task = KernelCreateTask(Process, &TaskInfo);

    //-------------------------------------
    // Switch back to kernel page directory

    DEBUG(TEXT("Switching back page directory to %p"), (LINEAR)PageDirectory);

    LoadPageDirectory(PageDirectory);

    //-------------------------------------

    UnfreezeScheduler();

    //-------------------------------------
    // Add the new process to the kernel's process list

    LPLIST ProcessList = GetProcessList();
    ListAddItem(ProcessList, Process);

    if (GetActiveDesktop() == Process->Desktop) {
        SetFocusedProcess(Process);
    }

    //-------------------------------------
    // Add initial task to the scheduler's queue

    AddTaskToQueue(Task);

    Result = TRUE;

Out:

    if (!Result) {
        if (Process != NULL) {
            if (Process->StdIn != 0) {
                CloseHandle(Process->StdIn);
                Process->StdIn = 0;
            }

            if (Process->StdOut != 0) {
                CloseHandle(Process->StdOut);
                Process->StdOut = 0;
            }

            if (Process->StdErr != 0) {
                CloseHandle(Process->StdErr);
                Process->StdErr = 0;
            }
        }
    }

    Info->StdOut = Process != NULL ? Process->StdOut : 0;
    Info->StdIn = Process != NULL ? Process->StdIn : 0;
    Info->StdErr = Process != NULL ? Process->StdErr : 0;

    Info->Process = (HANDLE)Process;
    Info->Task = (HANDLE)Task;

    //-------------------------------------
    // Release access to kernel data

    UnlockMutex(MUTEX_KERNEL);

    DEBUG(TEXT("Exit, Result = %d"), Result);

    TRACED_EPILOGUE("CreateProcess");
    return Result;
}

/***************************************************************************/

/**
 * @brief Create a new process using a full command line and wait for it to complete.
 *
 * @param CommandLine Full command line including executable name and arguments.
 * @param WorkFolder Working directory to use, or empty/NULL to inherit from parent.
 * @return The process exit code on success, MAX_UINT on failure.
 */
UINT Spawn(LPCSTR CommandLine, LPCSTR WorkFolder) {
    DEBUG(TEXT("Launching : %s"), CommandLine);

    PROCESS_INFO ProcessInfo;
    WAIT_INFO WaitInfo;
    UINT Result;
    LPPROCESS ParentProcess = NULL;

    MemorySet(&ProcessInfo, 0, sizeof(PROCESS_INFO));
    ProcessInfo.Header.Size = sizeof(PROCESS_INFO);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;
    ProcessInfo.Process = NULL;

    StringCopy(ProcessInfo.CommandLine, CommandLine);

    if (StringEmpty(WorkFolder) == FALSE) {
        StringCopy(ProcessInfo.WorkFolder, WorkFolder);
    } else {
        ParentProcess = GetCurrentProcess();
        SAFE_USE_VALID_ID(ParentProcess, KOID_PROCESS) {
            StringCopy(ProcessInfo.WorkFolder, ParentProcess->WorkFolder);
        }
    }

    if (!CreateProcess(&ProcessInfo) || ProcessInfo.Process == NULL) {
        return MAX_UINT;
    }

    // Wait for the process to complete
    WaitInfo.Header.Size = sizeof(WAIT_INFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.MilliSeconds = INFINITY;
    WaitInfo.Objects[0] = ProcessInfo.Process;

    Result = Wait(&WaitInfo);

    if (Result == WAIT_TIMEOUT) {
        DEBUG(TEXT("Process wait timed out"));
        return MAX_UINT;
    } else if (Result != WAIT_OBJECT_0) {
        DEBUG(TEXT("Process wait failed: %u"), Result);
        return MAX_UINT;
    }

    DEBUG(TEXT("Process completed successfully, exit code: %u"), WaitInfo.ExitCodes[0]);
    TEST(TEXT("Executable finished normally : %s"), CommandLine);
    return WaitInfo.ExitCodes[0];
}

/************************************************************************/

void SetProcessStatus(LPPROCESS This, U32 Status) {
    LockMutex(MUTEX_PROCESS, INFINITY);

    SAFE_USE_VALID_ID(This, KOID_PROCESS) {
        This->Status = Status;

        DEBUG(TEXT("Marked process %s as %d"), This->FileName, Status);

        if (Status == PROCESS_STATUS_DEAD) {
            // Store termination state in cache before process is destroyed
            StoreObjectTerminationState(This, This->ExitCode);
        }
    }

    UnlockMutex(MUTEX_PROCESS);
}

/***************************************************************************/

/**
 * @brief Retrieve the heap base address of a process.
 *
 * @param Process Process to inspect, or NULL for the current process.
 * @return Linear address of the process heap.
 */
LINEAR GetProcessHeap(LPPROCESS Process) {
    LINEAR HeapBase = NULL;

    if (Process == NULL) Process = GetCurrentProcess();

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        HeapBase = Process->HeapBase;

        UnlockMutex(&(Process->Mutex));
    }

    return HeapBase;
}

/***************************************************************************/

/**
 * @brief Returns the memory region list owned by a process.
 *
 * @param Process Target process.
 * @return Memory region list pointer or NULL when unavailable.
 */
LPMEMORY_REGION_LIST GetProcessMemoryRegionList(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        return &Process->MemoryRegionList;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Returns the memory region list for the active address space.
 *
 * @return Memory region list pointer or NULL when unavailable.
 */
LPMEMORY_REGION_LIST GetCurrentMemoryRegionList(void) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) {
        Process = &KernelProcess;
    }

    return GetProcessMemoryRegionList(Process);
}

/***************************************************************************/

/**
 * @brief Assigns one descriptor owner process explicitly.
 *
 * @param Descriptor Target descriptor.
 * @param Process Owner process, or NULL to fall back to the kernel process.
 */
void MemoryRegionDescriptorAssignOwner(LPMEMORY_REGION_DESCRIPTOR Descriptor, LPPROCESS Process) {
    if (Process == NULL) {
        Process = &KernelProcess;
    }

    SAFE_USE(Descriptor) {
        Descriptor->OwnerProcess = Process;
    }
}

/***************************************************************************/

/**
 * @brief Assigns the active address space owner to a descriptor.
 *
 * @param Descriptor Target descriptor.
 */
void MemoryRegionDescriptorAssignCurrentOwner(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) {
        Process = &KernelProcess;
    }

    MemoryRegionDescriptorAssignOwner(Descriptor, Process);
}

/***************************************************************************/

/**
 * @brief Output process information to the kernel log.
 *
 * @param Process Process to dump. Nothing is logged if NULL.
 */
void DumpProcess(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        DEBUG(TEXT("Address        : %p\n"), (LINEAR)Process);
        DEBUG(TEXT("References     : %d\n"), Process->References);
        DEBUG(TEXT("OwnerProcess   : %p\n"), (LINEAR)Process->OwnerProcess);
        DEBUG(TEXT("Privilege      : %d\n"), Process->Privilege);
        DEBUG(TEXT("Page directory : %p\n"), (LINEAR)Process->PageDirectory);
        DEBUG(TEXT("File name      : %s\n"), Process->FileName);
        DEBUG(TEXT("Heap base      : %p\n"), (LINEAR)Process->HeapBase);
        DEBUG(TEXT("Heap size      : %d\n"), Process->HeapSize);

        UnlockMutex(&(Process->Mutex));
    }
}

/***************************************************************************/

/**
 * @brief Initialize a SECURITY structure.
 *
 * @param This SECURITY structure to initialize.
 */
void InitSecurity(LPSECURITY This) {
    SAFE_USE(This) {
        This->TypeID = KOID_SECURITY;
        This->References = 1;
        This->OwnerProcess = GetCurrentProcess();
        This->Next = NULL;
        This->Prev = NULL;
        This->Owner = U64_Make(0, 0);
        This->UserPermissionCount = 0;
        This->DefaultPermissions = PERMISSION_NONE;
    }
}
