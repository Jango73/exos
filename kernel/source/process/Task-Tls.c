
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


    Task executable module TLS

\************************************************************************/

#include "process/Task.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Heap.h"
#include "memory/Memory.h"
#include "process/Process-Arena.h"
#include "process/Process-Module.h"
#include "process/Schedule.h"

/************************************************************************/

/**
 * @brief Destroy one task-owned module TLS block.
 *
 * @param Block TLS block to destroy.
 */
static void DeleteTaskModuleTlsBlock(LPTASK_MODULE_TLS_BLOCK Block) {
    if (Block == NULL) {
        return;
    }

    if (Block->Base != 0 && Block->Size != 0 && Block->Binding != NULL && Block->Binding->Process != NULL) {
        FreeRegionForProcess(Block->Binding->Process, Block->Base, Block->Size);
    }

    KernelHeapFree(Block);
}

/************************************************************************/

/**
 * @brief Return the TLS block list owned by one task.
 *
 * @param Task Target task.
 * @return TLS block list or NULL.
 */
static LPLIST TaskGetModuleTlsBlockList(LPTASK Task) {
    if (Task == NULL) {
        return NULL;
    }

    if (Task->ModuleTlsBlocks != NULL) {
        return Task->ModuleTlsBlocks;
    }

    Task->ModuleTlsBlocks = NewList((LISTITEMDESTRUCTOR)DeleteTaskModuleTlsBlock, KernelHeapAlloc, KernelHeapFree);
    return Task->ModuleTlsBlocks;
}

/************************************************************************/

/**
 * @brief Find one TLS block for one module binding.
 *
 * @param Task Target task.
 * @param Binding Module binding to match.
 * @return Matching TLS block or NULL.
 */
static LPTASK_MODULE_TLS_BLOCK TaskFindModuleTlsBlock(
    LPTASK Task,
    LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Task == NULL || Binding == NULL || Task->ModuleTlsBlocks == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = Task->ModuleTlsBlocks->First; Node != NULL; Node = Node->Next) {
        LPTASK_MODULE_TLS_BLOCK Block = (LPTASK_MODULE_TLS_BLOCK)Node;

        if (Block->Binding == Binding) {
            return Block;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Create one task-owned module TLS block.
 *
 * @param Binding Module binding that owns the TLS template.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return New TLS block or NULL.
 */
static LPTASK_MODULE_TLS_BLOCK TaskCreateModuleTlsBlock(
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPTASK_MODULE_TLS_BLOCK Block = NULL;
    LINEAR TlsBase;
    LINEAR ThreadPointer;
    UINT AllocationSize;

    if (Binding == NULL || Binding->Process == NULL || TotalSize == 0 || TemplateSize > TotalSize) {
        return NULL;
    }

    if (TemplateSize != 0 && TemplateBase == 0) {
        return NULL;
    }

    AllocationSize = TotalSize + (UINT)sizeof(LINEAR);
    TlsBase = ProcessArenaAllocateModule(Binding->Process,
                                         PROCESS_MODULE_ALLOCATION_TLS,
                                         AllocationSize,
                                         ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                         TEXT("TaskModuleTls"));
    if (TlsBase == 0) {
        ERROR(TEXT("Module TLS allocation failed task process=%p size=%u"),
              Binding->Process,
              AllocationSize);
        return NULL;
    }

    MemorySet((LPVOID)TlsBase, 0, AllocationSize);
    if (TemplateSize != 0) {
        MemoryCopy((LPVOID)TlsBase, (LPCVOID)TemplateBase, TemplateSize);
    }
    ThreadPointer = TlsBase + TotalSize;
    *((LINEAR*)ThreadPointer) = ThreadPointer;

    Block = (LPTASK_MODULE_TLS_BLOCK)KernelHeapAlloc(sizeof(TASK_MODULE_TLS_BLOCK));
    if (Block == NULL) {
        FreeRegionForProcess(Binding->Process, TlsBase, AllocationSize);
        return NULL;
    }

    MemorySet(Block, 0, sizeof(TASK_MODULE_TLS_BLOCK));
    Block->Binding = Binding;
    Block->Base = TlsBase;
    Block->Size = AllocationSize;
    Block->TemplateSize = TemplateSize;
    Block->Alignment = Alignment;
    return Block;
}

/************************************************************************/

/**
 * @brief Return the size needed for a task user TLS control block.
 *
 * @param ModuleTlsBlockCount Number of module TLS entries.
 * @return Required size in bytes.
 */
static UINT TaskGetUserTlsAnchorSize(UINT ModuleTlsBlockCount) {
    return sizeof(TASK_USER_TLS_CONTROL_BLOCK) +
           (ModuleTlsBlockCount * sizeof(TASK_USER_TLS_MODULE_ENTRY));
}

/************************************************************************/

/**
 * @brief Return the compiler ABI thread pointer for the first module TLS block.
 *
 * @param Task Target task.
 * @return Thread pointer base, or zero when no module TLS block exists.
 */
static LINEAR TaskGetUserTlsThreadPointer(LPTASK Task) {
    LPTASK_MODULE_TLS_BLOCK Block;

    if (Task == NULL || Task->ModuleTlsBlocks == NULL || Task->ModuleTlsBlocks->First == NULL) {
        return 0;
    }

    Block = (LPTASK_MODULE_TLS_BLOCK)Task->ModuleTlsBlocks->First;
    if (Block == NULL || Block->Base == 0 || Block->Size <= (UINT)sizeof(LINEAR)) {
        return 0;
    }

    return Block->Base + Block->Size - (UINT)sizeof(LINEAR);
}

/************************************************************************/

/**
 * @brief Populate one module TLS entry for the user TLS vector.
 *
 * @param Entry User-visible vector entry.
 * @param Block Task-owned module TLS block.
 */
static void TaskPopulateUserTlsModuleEntry(
    LPTASK_USER_TLS_MODULE_ENTRY Entry,
    LPTASK_MODULE_TLS_BLOCK Block) {
    if (Entry == NULL || Block == NULL || Block->Binding == NULL || Block->Binding->Image == NULL) {
        return;
    }

    Entry->ModuleIdentifierHigh = U64_High32(Block->Binding->Image->InstanceID);
    Entry->ModuleIdentifierLow = U64_Low32(Block->Binding->Image->InstanceID);
    Entry->Base = Block->Base;
    Entry->Size = Block->Size;
    Entry->TemplateSize = Block->TemplateSize;
    Entry->Alignment = Block->Alignment;
}

/************************************************************************/

/**
 * @brief Build one user-visible TLS control block for a task.
 *
 * @param Task Target task.
 * @param AnchorBase User mapping where the control block is stored.
 * @param AnchorSize Size of the user mapping.
 */
static void TaskBuildUserTlsAnchor(
    LPTASK Task,
    LINEAR AnchorBase,
    UINT AnchorSize) {
    LPTASK_USER_TLS_CONTROL_BLOCK ControlBlock = (LPTASK_USER_TLS_CONTROL_BLOCK)AnchorBase;
    LPTASK_USER_TLS_MODULE_ENTRY Entries =
        (LPTASK_USER_TLS_MODULE_ENTRY)(AnchorBase + sizeof(TASK_USER_TLS_CONTROL_BLOCK));
    UINT EntryIndex = 0;

    MemorySet((LPVOID)AnchorBase, 0, AnchorSize);

    ControlBlock->Magic = TASK_USER_TLS_CONTROL_BLOCK_MAGIC;
    ControlBlock->Version = TASK_USER_TLS_CONTROL_BLOCK_VERSION;
    ControlBlock->Size = AnchorSize;
    ControlBlock->Self = AnchorBase;
    ControlBlock->ProcessIdentifierHigh = U64_High32(Task->OwnerProcess->InstanceID);
    ControlBlock->ProcessIdentifierLow = U64_Low32(Task->OwnerProcess->InstanceID);
    ControlBlock->ThreadIdentifierHigh = U64_High32(Task->InstanceID);
    ControlBlock->ThreadIdentifierLow = U64_Low32(Task->InstanceID);
    ControlBlock->ModuleTlsVector = (LINEAR)Entries;
    ControlBlock->ModuleTlsVectorCount = Task->ModuleTlsBlockCount;
    ControlBlock->ModuleTlsVectorStride = sizeof(TASK_USER_TLS_MODULE_ENTRY);

    if (Task->ModuleTlsBlocks == NULL) {
        return;
    }

    for (LPLISTNODE Node = Task->ModuleTlsBlocks->First; Node != NULL; Node = Node->Next) {
        if (EntryIndex >= Task->ModuleTlsBlockCount) {
            return;
        }

        TaskPopulateUserTlsModuleEntry(Entries + EntryIndex, (LPTASK_MODULE_TLS_BLOCK)Node);
        EntryIndex++;
    }
}

/************************************************************************/

/**
 * @brief Refresh one task user TLS control block while the task is locked.
 *
 * @param Task Target task.
 * @return TRUE when the anchor reflects the task TLS block list.
 */
static BOOL TaskRefreshModuleTlsLocked(LPTASK Task) {
    LINEAR NewAnchor;
    LINEAR OldAnchor;
    LINEAR ThreadPointer;
    UINT NewAnchorSize;
    UINT OldAnchorSize;

    if (Task == NULL || Task->OwnerProcess == NULL) {
        return FALSE;
    }

    if (Task->OwnerProcess->Privilege != CPU_PRIVILEGE_USER) {
        return TaskSetUserTlsAnchor(Task, 0);
    }

    if (Task->ModuleTlsBlockCount == 0) {
        OldAnchor = Task->UserTlsAnchor;
        OldAnchorSize = Task->UserTlsAnchorSize;
        if (TaskSetUserTlsAnchor(Task, 0) == FALSE) {
            return FALSE;
        }
        Task->UserTlsAnchor = 0;
        Task->UserTlsAnchorSize = 0;
        if (OldAnchor != 0 && OldAnchorSize != 0) {
            FreeRegionForProcess(Task->OwnerProcess, OldAnchor, OldAnchorSize);
        }
        return TRUE;
    }

    NewAnchorSize = TaskGetUserTlsAnchorSize(Task->ModuleTlsBlockCount);
    NewAnchor = ProcessArenaAllocateSystem(Task->OwnerProcess,
                                           NewAnchorSize,
                                           ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                           TEXT("TaskUserTls"));
    if (NewAnchor == 0) {
        ERROR(TEXT("User TLS anchor allocation failed task=%p size=%u"),
              Task,
              NewAnchorSize);
        return FALSE;
    }

    TaskBuildUserTlsAnchor(Task, NewAnchor, NewAnchorSize);

    OldAnchor = Task->UserTlsAnchor;
    OldAnchorSize = Task->UserTlsAnchorSize;
    ThreadPointer = TaskGetUserTlsThreadPointer(Task);
    if (ThreadPointer == 0) {
        FreeRegionForProcess(Task->OwnerProcess, NewAnchor, NewAnchorSize);
        return FALSE;
    }

    FreezeScheduler();
    if (TaskSetUserTlsAnchor(Task, ThreadPointer) == FALSE) {
        UnfreezeScheduler();
        FreeRegionForProcess(Task->OwnerProcess, NewAnchor, NewAnchorSize);
        return FALSE;
    }

    Task->UserTlsAnchor = NewAnchor;
    Task->UserTlsAnchorSize = NewAnchorSize;
    UnfreezeScheduler();

    if (OldAnchor != 0 && OldAnchorSize != 0) {
        FreeRegionForProcess(Task->OwnerProcess, OldAnchor, OldAnchorSize);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Ensure one task owns a TLS block for one module binding.
 *
 * @param Task Target task.
 * @param Binding Module binding requiring TLS.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return TRUE when the TLS block exists.
 */
BOOL TaskEnsureModuleTlsBlock(
    LPTASK Task,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPTASK_MODULE_TLS_BLOCK Block = NULL;
    LPLIST BlockList = NULL;
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Task->Mutex), INFINITY);

            if (Task->OwnerProcess != Binding->Process) {
                UnlockMutex(&(Task->Mutex));
                return FALSE;
            }

            if (TaskFindModuleTlsBlock(Task, Binding) != NULL) {
                UnlockMutex(&(Task->Mutex));
                return TRUE;
            }

            BlockList = TaskGetModuleTlsBlockList(Task);
            if (BlockList == NULL) {
                UnlockMutex(&(Task->Mutex));
                return FALSE;
            }

            Block = TaskCreateModuleTlsBlock(Binding, TemplateBase, TemplateSize, TotalSize, Alignment);
            if (Block != NULL) {
                FreezeScheduler();
                Result = ListAddItem(BlockList, Block);
                if (Result != FALSE) {
                    Task->ModuleTlsBlockCount++;
                }
                UnfreezeScheduler();

                if (Result == FALSE) {
                    DeleteTaskModuleTlsBlock(Block);
                } else if (TaskRefreshModuleTlsLocked(Task) == FALSE) {
                    FreezeScheduler();
                    ListRemove(BlockList, Block);
                    if (Task->ModuleTlsBlockCount > 0) {
                        Task->ModuleTlsBlockCount--;
                    }
                    UnfreezeScheduler();
                    DeleteTaskModuleTlsBlock(Block);
                    Result = FALSE;
                }
            }

            UnlockMutex(&(Task->Mutex));
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Release one module TLS block owned by one task.
 *
 * @param Task Target task.
 * @param Binding Module binding whose TLS block must be released.
 */
void TaskReleaseModuleTlsBlock(LPTASK Task, LPEXECUTABLE_MODULE_BINDING Binding) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Task->Mutex), INFINITY);

            LPTASK_MODULE_TLS_BLOCK Block = TaskFindModuleTlsBlock(Task, Binding);
            if (Block != NULL) {
                FreezeScheduler();
                ListRemove(Task->ModuleTlsBlocks, Block);
                if (Task->ModuleTlsBlockCount > 0) {
                    Task->ModuleTlsBlockCount--;
                }
                UnfreezeScheduler();
                DeleteTaskModuleTlsBlock(Block);
                if (TaskRefreshModuleTlsLocked(Task) == FALSE) {
                    WARNING(TEXT("User TLS anchor refresh failed task=%p"), Task);
                }
            }

            UnlockMutex(&(Task->Mutex));
        }
    }
}

/************************************************************************/

/**
 * @brief Release all module TLS blocks owned by one task.
 *
 * @param Task Target task.
 */
void TaskReleaseModuleTlsBlocks(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(&(Task->Mutex), INFINITY);

        if (Task->ModuleTlsBlocks != NULL) {
            FreezeScheduler();
            DeleteList(Task->ModuleTlsBlocks);
            Task->ModuleTlsBlocks = NULL;
            Task->ModuleTlsBlockCount = 0;
            UnfreezeScheduler();
        }

        TaskReleaseUserTlsAnchor(Task);
        UnlockMutex(&(Task->Mutex));
    }
}

/************************************************************************/

/**
 * @brief Release one module TLS block from every task owned by a process.
 *
 * @param Process Target process.
 * @param Binding Module binding whose TLS blocks must be released.
 */
void TaskReleaseProcessModuleTlsBlocks(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding) {
    LPLIST TaskList = GetTaskList();

    if (Process == NULL || Binding == NULL || TaskList == NULL) {
        return;
    }

    for (LPLISTNODE Node = TaskList->First; Node != NULL; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        if (Task == NULL || Task->OwnerProcess != Process) {
            continue;
        }

        TaskReleaseModuleTlsBlock(Task, Binding);
    }
}

/************************************************************************/

/**
 * @brief Install one module TLS block in every task owned by a process.
 *
 * @param Process Target process.
 * @param Binding Module binding requiring TLS.
 * @param TemplateBase Mapped template base inside the process.
 * @param TemplateSize Initialized template size.
 * @param TotalSize Total TLS block size.
 * @param Alignment Required TLS alignment.
 * @return TRUE when every task owns the TLS block.
 */
BOOL TaskInstallProcessModuleTlsBlocks(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR TemplateBase,
    UINT TemplateSize,
    UINT TotalSize,
    UINT Alignment) {
    LPLIST TaskList = GetTaskList();

    if (Process == NULL || Binding == NULL || TaskList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = TaskList->First; Node != NULL; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        if (Task == NULL || Task->OwnerProcess != Process) {
            continue;
        }

        if (!TaskEnsureModuleTlsBlock(Task, Binding, TemplateBase, TemplateSize, TotalSize, Alignment)) {
            for (LPLISTNODE RollbackNode = TaskList->First; RollbackNode != Node; RollbackNode = RollbackNode->Next) {
                LPTASK RollbackTask = (LPTASK)RollbackNode;

                if (RollbackTask == NULL || RollbackTask->OwnerProcess != Process) {
                    continue;
                }

                TaskReleaseModuleTlsBlock(RollbackTask, Binding);
            }

            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Return the current user TLS anchor for a task.
 *
 * @param Task Target task.
 * @return User TLS anchor base or zero.
 */
LINEAR TaskGetUserTlsAnchor(LPTASK Task) {
    LINEAR Anchor = 0;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(&(Task->Mutex), INFINITY);
        Anchor = Task->UserTlsAnchor;
        UnlockMutex(&(Task->Mutex));
    }

    return Anchor;
}

/************************************************************************/

/**
 * @brief Refresh the user TLS control block from the task TLS block list.
 *
 * @param Task Target task.
 * @return TRUE when the anchor was refreshed.
 */
BOOL TaskRefreshModuleTls(LPTASK Task) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(&(Task->Mutex), INFINITY);
        Result = TaskRefreshModuleTlsLocked(Task);
        UnlockMutex(&(Task->Mutex));
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Release the user TLS anchor owned by one task.
 *
 * @param Task Target task.
 */
void TaskReleaseUserTlsAnchor(LPTASK Task) {
    LINEAR OldAnchor;
    UINT OldAnchorSize;

    if (Task == NULL) {
        return;
    }

    OldAnchor = Task->UserTlsAnchor;
    OldAnchorSize = Task->UserTlsAnchorSize;

    FreezeScheduler();
    if (TaskSetUserTlsAnchor(Task, 0) == FALSE) {
        WARNING(TEXT("User TLS anchor reset failed task=%p"), Task);
    }
    Task->UserTlsAnchor = 0;
    Task->UserTlsAnchorSize = 0;
    UnfreezeScheduler();

    if (OldAnchor != 0 && OldAnchorSize != 0 && Task->OwnerProcess != NULL) {
        FreeRegionForProcess(Task->OwnerProcess, OldAnchor, OldAnchorSize);
    }
}
