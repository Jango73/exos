
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


    Process address space arenas

\************************************************************************/

#include "process/Process-Arena.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "memory/Heap.h"
#include "text/CoreString.h"
#include "process/Process.h"

/************************************************************************/

#ifdef __EXOS_32__
#define PROCESS_ARENA_SYSTEM_RESERVED N_64MB
#define PROCESS_ARENA_MMIO_RESERVED N_64MB
#define PROCESS_ARENA_STACK_RESERVED (N_128MB + N_128MB)
#define PROCESS_ARENA_MODULE_RESERVED N_1GB
#else
#define PROCESS_ARENA_SYSTEM_RESERVED N_1GB
#define PROCESS_ARENA_MMIO_RESERVED N_1GB
#define PROCESS_ARENA_STACK_RESERVED (N_1GB * 4)
#define PROCESS_ARENA_MODULE_RESERVED (N_1GB * 64)
#endif
#define PROCESS_ARENA(Process, Id) (&((Process)->AddressSpace.Ranges[(Id)]))

/************************************************************************/

static LINEAR ProcessArenaAlignUp(LINEAR Value) {
    if ((Value & (PAGE_SIZE - 1)) == 0) {
        return Value;
    }

    return (Value + PAGE_SIZE) & PAGE_MASK;
}

/************************************************************************/

static LINEAR ProcessArenaAlignDown(LINEAR Value) {
    return Value & PAGE_MASK;
}

/************************************************************************/

static void ProcessArenaRangeInitialize(LPPROCESS_ARENA_RANGE Range, LINEAR Base, LINEAR Limit) {
    if (Range == NULL) {
        return;
    }

    Range->Base = Base;
    Range->Limit = Limit;
    Range->NextLow = Base;
    Range->NextHigh = Limit;
}

/************************************************************************/

static BOOL ProcessArenaRangeContains(LPPROCESS_ARENA_RANGE Range, LINEAR Address, UINT Size) {
    LINEAR End;

    if (Range == NULL || Address == 0 || Size == 0) {
        return FALSE;
    }

    End = Address + Size;

    if (End < Address) {
        return FALSE;
    }

    if (Address < Range->Base) {
        return FALSE;
    }

    if (Range->Limit != 0 && End > Range->Limit) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static LPCSTR ProcessArenaGetModuleAllocationTag(UINT Purpose) {
    switch (Purpose) {
        case PROCESS_MODULE_ALLOCATION_SHARED:
            return TEXT("ModuleShared");
        case PROCESS_MODULE_ALLOCATION_PRIVATE:
            return TEXT("ModulePrivate");
        case PROCESS_MODULE_ALLOCATION_TLS:
            return TEXT("ModuleTls");
        case PROCESS_MODULE_ALLOCATION_BOOKKEEPING:
            return TEXT("ModuleBookkeeping");
        default:
            return NULL;
    }
}

/************************************************************************/

static LINEAR ProcessArenaAllocateLow(
    LPPROCESS Process,
    UINT ArenaID,
    UINT Size,
    U32 Flags,
    LPCSTR Tag,
    LPCSTR FunctionName) {
    LINEAR AllocationBase;
    LINEAR Result;
    UINT AlignedSize;
    LPPROCESS_ARENA_RANGE Range;

    if (Process == NULL || FunctionName == NULL || Size == 0) {
        return 0;
    }

    Range = PROCESS_ARENA(Process, ArenaID);
    AlignedSize = ProcessArenaAlignUp(Size);
    AllocationBase = ProcessArenaAlignUp(Range->NextLow);

    if (Range->Limit != 0 && AllocationBase + AlignedSize > Range->Limit) {
        ERROR(TEXT("Arena exhausted for process %p"), Process);
        return 0;
    }

    Result = AllocRegion(AllocationBase,
                         0,
                         AlignedSize,
                         Flags | ALLOC_PAGES_AT_OR_OVER,
                         Tag);
    if (Result == 0) {
        ERROR(TEXT("AllocRegion failed for process %p (Base=%p Size=%u)"),
              Process,
              AllocationBase,
              AlignedSize);
        return 0;
    }

    if (ProcessArenaRangeContains(Range, Result, AlignedSize) == FALSE) {
        ERROR(TEXT("Out-of-range allocation %p (size=%u) for process %p"),
              Result,
              AlignedSize,
              Process);
        FreeRegion(Result, AlignedSize);
        return 0;
    }

    Range->NextLow = ProcessArenaAlignUp(Result + AlignedSize);
    return Result;
}

/************************************************************************/

static BOOL ProcessArenaResizeMainHeap(LPVOID Context, LINEAR HeapBase, UINT OldSize, UINT NewSize, U32 Flags) {
    LPPROCESS Process = (LPPROCESS)Context;
    LINEAR HeapLimit;
    LINEAR HeapStart;
    UINT MaximumSize;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Process->HeapBase != HeapBase) {
            return FALSE;
        }

        HeapStart = ProcessArenaAlignDown(Process->HeapBase);
        HeapLimit = PROCESS_ARENA(Process, PROCESS_ARENA_HEAP)->Limit;
        if (HeapLimit <= HeapStart) {
            return FALSE;
        }

        MaximumSize = (UINT)(HeapLimit - HeapStart);
        if (NewSize > MaximumSize) {
            WARNING(TEXT("Heap growth exceeds arena limit process=%p requested=%u limit=%u"),
                    Process,
                    NewSize,
                    MaximumSize);
            return FALSE;
        }

        return ResizeRegion(HeapBase, 0, OldSize, NewSize, Flags);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Reset all process arena descriptors.
 */
void ProcessArenaReset(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        MemorySet(&(Process->AddressSpace), 0, sizeof(Process->AddressSpace));
    }
}

/************************************************************************/

/**
 * @brief Initialize kernel process arenas.
 */
BOOL ProcessArenaInitializeKernel(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        ProcessArenaReset(Process);

        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_IMAGE), 0, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_HEAP), Process->HeapBase, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_STACK), 0, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MODULE), 0, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM), VMA_KERNEL, 0);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), VMA_KERNEL, 0);

        Process->AddressSpace.Initialized = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize user process arenas.
 */
BOOL ProcessArenaInitializeUser(
    LPPROCESS Process,
    LINEAR ImageBase,
    UINT ImageSize,
    LINEAR HeapBase,
    UINT InitialHeapSize) {
    LINEAR UserLimit;
    LINEAR ImageLimit;
    LINEAR HeapStart;
    LINEAR HeapInitialEnd;
    LINEAR StackBase;
    LINEAR ModuleBase;
    LINEAR SystemBase;
    LINEAR MmioBase;
    LINEAR ReservedSpace;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (ImageSize == 0 || InitialHeapSize == 0) {
            ERROR(TEXT("Invalid image/heap size (ImageSize=%u InitialHeapSize=%u)"),
                  ImageSize,
                  InitialHeapSize);
            return FALSE;
        }

        ImageBase = ProcessArenaAlignDown(ImageBase);
        ImageLimit = ProcessArenaAlignUp(ImageBase + ImageSize);
        HeapStart = ProcessArenaAlignDown(HeapBase);
        HeapInitialEnd = ProcessArenaAlignUp(HeapBase + InitialHeapSize);
        UserLimit = ProcessArenaAlignDown(VMA_TASK_RUNNER);

        if (HeapStart < ImageLimit || HeapInitialEnd <= HeapStart || UserLimit <= HeapInitialEnd) {
            ERROR(TEXT("Invalid user ranges Image=[%p,%p) Heap=[%p,%p) UserLimit=%p"),
                  ImageBase,
                  ImageLimit,
                  HeapStart,
                  HeapInitialEnd,
                  UserLimit);
            return FALSE;
        }

        ReservedSpace = PROCESS_ARENA_SYSTEM_RESERVED +
                        PROCESS_ARENA_MMIO_RESERVED +
                        PROCESS_ARENA_STACK_RESERVED +
                        PROCESS_ARENA_MODULE_RESERVED;
        if (UserLimit <= ReservedSpace) {
            ERROR(TEXT("User linear space too small"));
            return FALSE;
        }

        SystemBase = ProcessArenaAlignDown(UserLimit - PROCESS_ARENA_SYSTEM_RESERVED);
        MmioBase = ProcessArenaAlignDown(SystemBase - PROCESS_ARENA_MMIO_RESERVED);
        ModuleBase = ProcessArenaAlignDown(MmioBase - PROCESS_ARENA_MODULE_RESERVED);
        StackBase = ProcessArenaAlignDown(ModuleBase - PROCESS_ARENA_STACK_RESERVED);

        if (StackBase <= HeapInitialEnd || ModuleBase <= StackBase || MmioBase <= ModuleBase) {
            ERROR(TEXT("Not enough user arena room (HeapEnd=%p StackBase=%p ModuleBase=%p MmioBase=%p)"),
                  HeapInitialEnd,
                  StackBase,
                  ModuleBase,
                  MmioBase);
            return FALSE;
        }

        ProcessArenaReset(Process);

        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_IMAGE), ImageBase, HeapStart);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_HEAP), HeapStart, StackBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_STACK), StackBase, ModuleBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MODULE), ModuleBase, MmioBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), MmioBase, SystemBase);
        ProcessArenaRangeInitialize(PROCESS_ARENA(Process, PROCESS_ARENA_SYSTEM), SystemBase, UserLimit);

        Process->AddressSpace.Initialized = TRUE;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Configure the process main heap growth policy from arena limits.
 */
void ProcessArenaConfigureMainHeap(LPPROCESS Process) {
    UINT MaximumSize;
    LINEAR HeapStart;
    LINEAR HeapLimit;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->HeapBase == 0 || Process->HeapSize == 0 || Process->AddressSpace.Initialized == FALSE) {
            return;
        }

        HeapStart = ProcessArenaAlignDown(Process->HeapBase);
        HeapLimit = PROCESS_ARENA(Process, PROCESS_ARENA_HEAP)->Limit;
        if (HeapLimit <= HeapStart) {
            ERROR(TEXT("Invalid heap arena for process %p"), Process);
            return;
        }

        MaximumSize = (UINT)(HeapLimit - HeapStart);
        HeapConfigureGrowth(Process->HeapBase,
                            Process,
                            ProcessArenaResizeMainHeap,
                            MaximumSize,
                            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    }
}

/************************************************************************/

/**
 * @brief Allocate a block in the process system arena.
 */
LINEAR ProcessArenaAllocateSystem(LPPROCESS Process, UINT Size, U32 Flags, LPCSTR Tag) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        return ProcessArenaAllocateLow(
            Process,
            PROCESS_ARENA_SYSTEM,
            Size,
            Flags,
            Tag,
            TEXT("ProcessArenaAllocateSystem"));
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a block in the process MMIO arena.
 */
LINEAR ProcessArenaAllocateMmio(LPPROCESS Process, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    LINEAR AllocationBase;
    LINEAR Result;
    UINT AlignedSize;
    U32 EffectiveFlags = Flags | ALLOC_PAGES_IO;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        AlignedSize = ProcessArenaAlignUp(Size);
        AllocationBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->NextLow);

        if (PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->Limit != 0 &&
            AllocationBase + AlignedSize > PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->Limit) {
            ERROR(TEXT("Arena exhausted for process %p"), Process);
            return 0;
        }

        Result = AllocRegion(AllocationBase,
                             Target,
                             AlignedSize,
                             EffectiveFlags | ALLOC_PAGES_AT_OR_OVER,
                             Tag);
        if (Result == 0) {
            ERROR(TEXT("AllocRegion failed for process %p (Base=%p Size=%u)"),
                  Process,
                  AllocationBase,
                  AlignedSize);
            return 0;
        }

        if (ProcessArenaRangeContains(PROCESS_ARENA(Process, PROCESS_ARENA_MMIO), Result, AlignedSize) == FALSE) {
            ERROR(TEXT("Out-of-range allocation %p (size=%u) for process %p"),
                  Result,
                  AlignedSize,
                  Process);
            FreeRegion(Result, AlignedSize);
            return 0;
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_MMIO)->NextLow = ProcessArenaAlignUp(Result + AlignedSize);
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a block in the process module arena.
 */
LINEAR ProcessArenaAllocateModule(LPPROCESS Process, UINT Purpose, UINT Size, U32 Flags, LPCSTR Tag) {
    LPCSTR EffectiveTag = Tag;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        if (Purpose >= PROCESS_MODULE_ALLOCATION_COUNT) {
            ERROR(TEXT("Invalid module allocation purpose=%u"), Purpose);
            return 0;
        }

        if (EffectiveTag == NULL) {
            EffectiveTag = ProcessArenaGetModuleAllocationTag(Purpose);
        }

        return ProcessArenaAllocateLow(
            Process,
            PROCESS_ARENA_MODULE,
            Size,
            Flags,
            EffectiveTag,
            TEXT("ProcessArenaAllocateModule"));
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Map fixed physical pages into the process module arena.
 *
 * @param Process Target process whose page directory is active.
 * @param Purpose Module allocation purpose.
 * @param PhysicalPages Physical page array owned by another kernel object.
 * @param PageCount Number of pages to map.
 * @param Flags Mapping flags, excluding ownership flags.
 * @param Tag Optional memory descriptor tag.
 * @return Linear base of the contiguous virtual mapping or 0.
 */
LINEAR ProcessArenaMapModulePages(
    LPPROCESS Process,
    UINT Purpose,
    PHYSICAL* PhysicalPages,
    UINT PageCount,
    U32 Flags,
    LPCSTR Tag) {
    LINEAR AllocationBase;
    LINEAR PageBase;
    UINT AlignedSize;
    LPCSTR EffectiveTag = Tag;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || PhysicalPages == NULL || PageCount == 0) {
            return 0;
        }

        if (Purpose >= PROCESS_MODULE_ALLOCATION_COUNT) {
            ERROR(TEXT("Invalid module allocation purpose=%u"), Purpose);
            return 0;
        }

        if (EffectiveTag == NULL) {
            EffectiveTag = ProcessArenaGetModuleAllocationTag(Purpose);
        }

        AlignedSize = PageCount << PAGE_SIZE_MUL;
        AllocationBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_MODULE)->NextLow);

        if (PROCESS_ARENA(Process, PROCESS_ARENA_MODULE)->Limit != 0 &&
            AllocationBase + AlignedSize > PROCESS_ARENA(Process, PROCESS_ARENA_MODULE)->Limit) {
            ERROR(TEXT("Arena exhausted for process %p"), Process);
            return 0;
        }

        for (UINT PageIndex = 0; PageIndex < PageCount; PageIndex++) {
            if (PhysicalPages[PageIndex] == 0) {
                ERROR(TEXT("Invalid physical page index=%u"), PageIndex);
                if (PageIndex != 0) {
                    FreeRegionForProcess(Process, AllocationBase, PageIndex << PAGE_SIZE_MUL);
                }
                return 0;
            }

            PageBase = AllocationBase + (PageIndex << PAGE_SIZE_MUL);
            if (AllocRegionForProcess(Process,
                                      PageBase,
                                      PhysicalPages[PageIndex],
                                      PAGE_SIZE,
                                      Flags | ALLOC_PAGES_COMMIT | ALLOC_PAGES_FIXED,
                                      EffectiveTag) == 0) {
                ERROR(TEXT("AllocRegion failed process=%p base=%p page=%u"),
                      Process,
                      PageBase,
                      PageIndex);
                if (PageIndex != 0) {
                    FreeRegionForProcess(Process, AllocationBase, PageIndex << PAGE_SIZE_MUL);
                }
                return 0;
            }
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_MODULE)->NextLow = ProcessArenaAlignUp(AllocationBase + AlignedSize);
        return AllocationBase;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a task stack in the appropriate arena for the process.
 */
LINEAR ProcessArenaAllocateTaskStack(LPPROCESS Process, UINT Size) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->Privilege == CPU_PRIVILEGE_USER) {
            return ProcessArenaAllocateUserStack(Process, Size);
        }

        return ProcessArenaAllocateSystem(Process, Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("TaskStack"));
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate a user stack from the top of the stack arena.
 */
LINEAR ProcessArenaAllocateUserStack(LPPROCESS Process, UINT Size) {
    LINEAR MinimumBase;
    LINEAR Candidate;
    LINEAR Result = 0;
    UINT AlignedSize;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->AddressSpace.Initialized == FALSE || Size == 0) {
            return 0;
        }

        AlignedSize = ProcessArenaAlignUp(Size);
        MinimumBase = ProcessArenaAlignUp(PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Base);

        if (Process->HeapBase != 0 && Process->HeapSize != 0) {
            LINEAR HeapEnd = ProcessArenaAlignUp(Process->HeapBase + Process->HeapSize);
            if (HeapEnd > MinimumBase) {
                MinimumBase = HeapEnd;
            }
        }

        Candidate = ProcessArenaAlignDown(PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->NextHigh);
        if (Candidate > PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Limit) {
            Candidate = PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->Limit;
        }

        while (Candidate >= MinimumBase + AlignedSize) {
            LINEAR Base = Candidate - AlignedSize;
            Result = AllocRegion(Base,
                                 0,
                                 AlignedSize,
                                 ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                 TEXT("TaskStack"));
            if (Result != 0) {
                break;
            }

            Candidate = Base;
        }

        if (Result == 0) {
            ERROR(TEXT("No stack slot for process %p (Size=%u MinimumBase=%p)"),
                  Process,
                  AlignedSize,
                  MinimumBase);
            return 0;
        }

        PROCESS_ARENA(Process, PROCESS_ARENA_STACK)->NextHigh = ProcessArenaAlignDown(Result);
        return Result;
    }

    return 0;
}

/************************************************************************/
