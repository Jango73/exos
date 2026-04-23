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


    Process executable module bindings

\************************************************************************/

#include "process/Process-Module.h"

#include "core/Kernel.h"
#include "exec/ExecutableELF.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Process-Arena.h"
#include "text/CoreString.h"

/***************************************************************************/

typedef struct tag_PROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT {
    LPPROCESS Process;
    LPEXECUTABLE_MODULE_BINDING TargetBinding;
} PROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT, *LPPROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT;

/***************************************************************************/

/**
 * @brief Find one module binding in one process without taking locks.
 *
 * @param Process Target process.
 * @param Image Shared module image to match.
 * @return Matching binding or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING FindProcessModuleBindingLocked(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_IMAGE Image) {
    LPLIST BindingList = NULL;

    if (Process == NULL || Image == NULL) {
        return NULL;
    }

    BindingList = Process->ModuleBindings;
    if (BindingList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = BindingList->First; Node != NULL; Node = Node->Next) {
        LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)Node;

        if (Binding == NULL) continue;
        if (Binding->Image != Image) continue;

        return Binding;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Create one empty dependency edge entry.
 *
 * @param Dependency Referenced dependency binding.
 * @return New edge node or NULL on allocation failure.
 */
static LPEXECUTABLE_MODULE_BINDING_DEPENDENCY CreateExecutableModuleBindingDependency(
    LPEXECUTABLE_MODULE_BINDING Dependency) {
    LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge = NULL;

    Edge = (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)KernelHeapAlloc(sizeof(EXECUTABLE_MODULE_BINDING_DEPENDENCY));
    if (Edge == NULL) {
        return NULL;
    }

    MemorySet(Edge, 0, sizeof(EXECUTABLE_MODULE_BINDING_DEPENDENCY));
    Edge->Binding = Dependency;
    return Edge;
}

/***************************************************************************/

/**
 * @brief Return TRUE when one segment is writable and executable.
 */
static BOOL ProcessModuleSegmentIsWritableExecutable(LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment) {
    if (Segment == NULL) {
        return FALSE;
    }

    return (Segment->Access & EXECUTABLE_SEGMENT_ACCESS_WRITE) != 0 &&
           (Segment->Access & EXECUTABLE_SEGMENT_ACCESS_EXECUTE) != 0;
}

/***************************************************************************/

/**
 * @brief Map one main executable virtual address into the process image mapping.
 */
static LINEAR MapProcessMainExecutableAddress(LPVOID Context, UINT VirtualAddress) {
    LPPROCESS Process = (LPPROCESS)Context;
    LPEXECUTABLE_INFO Layout = NULL;

    if (Process == NULL) {
        return 0;
    }

    Layout = &(Process->MainExecutableMetadata.Layout);
    if (Layout->CodeSize != 0 && VirtualAddress >= Layout->CodeBase &&
        VirtualAddress < Layout->CodeBase + Layout->CodeSize) {
        return Process->MainExecutableCodeBase + (VirtualAddress - Layout->CodeBase);
    }

    if (Layout->DataSize != 0 && VirtualAddress >= Layout->DataBase &&
        VirtualAddress < Layout->DataBase + Layout->DataSize) {
        return Process->MainExecutableDataBase + (VirtualAddress - Layout->DataBase);
    }

    if (Layout->BssSize != 0 && VirtualAddress >= Layout->BssBase &&
        VirtualAddress < Layout->BssBase + Layout->BssSize) {
        return Process->MainExecutableDataBase + (VirtualAddress - Layout->DataBase);
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Map one module virtual address into its installed process mapping.
 */
LINEAR MapProcessModuleBindingAddress(LPVOID Context, UINT VirtualAddress) {
    LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)Context;

    if (Binding == NULL || Binding->Image == NULL) {
        return 0;
    }

    for (UINT SegmentIndex = 0; SegmentIndex < Binding->Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Binding->Image->Metadata.Segments[SegmentIndex]);
        UINT SegmentEnd;
        UINT Offset;

        if (Segment->SourceType != PT_LOAD || Binding->SegmentBases[SegmentIndex] == 0) {
            continue;
        }

        SegmentEnd = Segment->VirtualAddress + Segment->MemorySize;
        if (VirtualAddress < Segment->VirtualAddress || VirtualAddress >= SegmentEnd) {
            continue;
        }

        Offset = VirtualAddress - Segment->VirtualAddress;
        if (Offset >= Binding->SegmentSizes[SegmentIndex]) {
            return 0;
        }

        return Binding->SegmentBases[SegmentIndex] + Offset;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Resolve one symbol against a mapped module binding.
 */
static BOOL ResolveProcessModuleBindingSymbol(
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPCSTR Name,
    LINEAR* Address) {
    if (Binding == NULL || Binding->Image == NULL || Name == NULL || Address == NULL) {
        return FALSE;
    }

    return ResolveExecutableMappedSymbol(
        &(Binding->Image->Metadata),
        MapProcessModuleBindingAddress,
        Binding,
        Name,
        Address);
}

/***************************************************************************/

/**
 * @brief Record one dependency while the owning process is already locked.
 */
static BOOL AddProcessModuleBindingDependencyLocked(
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPEXECUTABLE_MODULE_BINDING Dependency) {
    LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge = NULL;

    if (Binding == NULL || Dependency == NULL || Binding == Dependency || Binding->Dependencies == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = Binding->Dependencies->First; Node != NULL; Node = Node->Next) {
        LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Existing = (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)Node;

        if (Existing->Binding == Dependency) {
            Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_DEPENDENCIES_RESOLVED;
            return TRUE;
        }
    }

    Edge = CreateExecutableModuleBindingDependency(Dependency);
    if (Edge == NULL) {
        return FALSE;
    }

    if (!ListAddItem(Binding->Dependencies, Edge)) {
        KernelHeapFree(Edge);
        return FALSE;
    }

    Dependency->ProcessReferences++;
    Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_DEPENDENCIES_RESOLVED;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one process symbol for executable module relocation.
 */
static BOOL ResolveProcessModuleSymbol(LPVOID Context, LPEXECUTABLE_SYMBOL_RESOLUTION Resolution) {
    LPPROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT ResolverContext = (LPPROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT)Context;
    LPPROCESS Process = NULL;

    if (ResolverContext == NULL || Resolution == NULL || Resolution->Name == NULL) {
        return FALSE;
    }

    Process = ResolverContext->Process;
    if (Process == NULL || Process->ModuleBindings == NULL) {
        return FALSE;
    }

    if (ResolveExecutableMappedSymbol(
            &(Process->MainExecutableMetadata),
            MapProcessMainExecutableAddress,
            Process,
            Resolution->Name,
            &(Resolution->Address))) {
        return TRUE;
    }

    for (LPLISTNODE Node = Process->ModuleBindings->First; Node != NULL; Node = Node->Next) {
        LPEXECUTABLE_MODULE_BINDING Binding = (LPEXECUTABLE_MODULE_BINDING)Node;

        if (Binding == NULL || Binding == ResolverContext->TargetBinding) {
            continue;
        }

        if ((Binding->StateFlags & EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED) == 0) {
            continue;
        }

        if (ResolveProcessModuleBindingSymbol(Binding, Resolution->Name, &(Resolution->Address))) {
            if (!AddProcessModuleBindingDependencyLocked(ResolverContext->TargetBinding, Binding)) {
                return FALSE;
            }

            return TRUE;
        }
    }

    if (ResolverContext->TargetBinding != NULL && ResolverContext->TargetBinding->Dependencies != NULL) {
        for (LPLISTNODE Node = ResolverContext->TargetBinding->Dependencies->First; Node != NULL; Node = Node->Next) {
            LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge = (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)Node;

            if (Edge == NULL || Edge->Binding == NULL) {
                continue;
            }

            if (ResolveProcessModuleBindingSymbol(Edge->Binding, Resolution->Name, &(Resolution->Address))) {
                return TRUE;
            }
        }
    }

    if (Resolution->Required != FALSE) {
        WARNING(TEXT("Unresolved symbol name=%s"), Resolution->Name);
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Copy one template backing into a private process mapping.
 */
static BOOL CopyProcessModulePrivateSegmentTemplate(
    LPEXECUTABLE_MODULE_SHARED_SEGMENT Template,
    LINEAR MappingBase) {
    if (Template == NULL || MappingBase == 0) {
        return FALSE;
    }

    for (UINT PageIndex = 0; PageIndex < Template->PageCount; PageIndex++) {
        LINEAR Source = MapTemporaryPhysicalPage1(Template->PhysicalPages[PageIndex]);
        LINEAR Destination = MappingBase + (PageIndex << PAGE_SIZE_MUL);

        if (Source == 0) {
            ERROR(TEXT("MapTemporaryPhysicalPage1 failed page=%u"),
                  PageIndex);
            return FALSE;
        }

        MemoryCopy((LPVOID)Destination, (LPCVOID)Source, PAGE_SIZE);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Install one read-only shared segment into a process binding.
 */
static BOOL InstallProcessModuleSharedSegment(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPEXECUTABLE_MODULE_SHARED_SEGMENT SharedSegment) {
    LINEAR MappingBase;
    LINEAR SegmentBase;

    if (Process == NULL || Binding == NULL || SharedSegment == NULL || SharedSegment->Present == FALSE) {
        return FALSE;
    }

    MappingBase = ProcessArenaMapModulePages(Process,
                                             PROCESS_MODULE_ALLOCATION_SHARED,
                                             SharedSegment->PhysicalPages,
                                             SharedSegment->PageCount,
                                             ALLOC_PAGES_READONLY,
                                             TEXT("ModuleShared"));
    if (MappingBase == 0) {
        return FALSE;
    }

    SegmentBase = MappingBase + SharedSegment->VirtualAddressOffset;
    Binding->SegmentBases[SharedSegment->SegmentIndex] = SegmentBase;
    Binding->SegmentSizes[SharedSegment->SegmentIndex] = SharedSegment->MemorySize;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Install one writable private segment into a process binding.
 */
static BOOL InstallProcessModulePrivateSegment(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPEXECUTABLE_MODULE_SHARED_SEGMENT PrivateSegment) {
    LINEAR MappingBase;
    LINEAR SegmentBase;

    if (Process == NULL || Binding == NULL || PrivateSegment == NULL || PrivateSegment->Present == FALSE) {
        return FALSE;
    }

    MappingBase = ProcessArenaAllocateModule(Process,
                                             PROCESS_MODULE_ALLOCATION_PRIVATE,
                                             PrivateSegment->MemorySize,
                                             ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE,
                                             TEXT("ModulePrivate"));
    if (MappingBase == 0) {
        return FALSE;
    }

    if (!CopyProcessModulePrivateSegmentTemplate(PrivateSegment, MappingBase)) {
        FreeRegionForProcess(Process, MappingBase, PrivateSegment->MemorySize);
        return FALSE;
    }

    SegmentBase = MappingBase + PrivateSegment->VirtualAddressOffset;
    Binding->SegmentBases[PrivateSegment->SegmentIndex] = SegmentBase;
    Binding->SegmentSizes[PrivateSegment->SegmentIndex] = PrivateSegment->MemorySize;
    if (Binding->WritableDataBase == 0) {
        Binding->WritableDataBase = SegmentBase;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Unmap all installed module segments from one process binding.
 */
static void UninstallProcessModuleBindingSegmentsLocked(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Process == NULL || Binding == NULL || Binding->Image == NULL) {
        return;
    }

    for (UINT SegmentIndex = 0; SegmentIndex < Binding->Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Binding->Image->Metadata.Segments[SegmentIndex]);
        LINEAR SegmentBase = Binding->SegmentBases[SegmentIndex];
        UINT SegmentSize = Binding->SegmentSizes[SegmentIndex];
        LINEAR MappingBase;

        if (SegmentBase == 0 || SegmentSize == 0) {
            continue;
        }

        MappingBase = SegmentBase - (Segment->VirtualAddress & PAGE_SIZE_MASK);
        FreeRegionForProcess(Process, MappingBase, SegmentSize);
        Binding->SegmentBases[SegmentIndex] = 0;
        Binding->SegmentSizes[SegmentIndex] = 0;
    }
    TaskReleaseProcessModuleTlsBlocks(Process, Binding);
    Binding->WritableDataBase = 0;
    Binding->WritableDataSize = 0;
    Binding->StateFlags &= ~EXECUTABLE_MODULE_BINDING_STATE_TLS_REGISTERED;
    Binding->StateFlags &= ~EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED;
    Binding->StateFlags &= ~EXECUTABLE_MODULE_BINDING_STATE_GLOBAL_DATA_INITIALIZED;
}

/***************************************************************************/

/**
 * @brief Decrement one process binding reference while the owner is locked.
 *
 * @param Binding Binding whose process reference must be dropped.
 */
static void ReleaseProcessModuleBindingLocked(LPEXECUTABLE_MODULE_BINDING Binding) {
    LPPROCESS Process = NULL;

    if (Binding == NULL) {
        return;
    }

    if (Binding->ProcessReferences > 0) {
        Binding->ProcessReferences--;
    }

    if (Binding->ProcessReferences > 0) {
        return;
    }

    Process = Binding->Process;
    if (Process != NULL && Process->ModuleBindings != NULL) {
        UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
        ListRemove(Process->ModuleBindings, Binding);

        if (Process->ModuleBindingCount > 0) {
            Process->ModuleBindingCount--;
        }
    }

    Binding->Process = NULL;
    Binding->OwnerProcess = NULL;
    DestroyKernelObject(Binding);
}

/***************************************************************************/

/**
 * @brief Free all dependency edges owned by one process module binding.
 *
 * @param Binding Binding whose dependency list must be destroyed.
 */
static void DeleteExecutableModuleBindingDependencies(LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Binding == NULL || Binding->Dependencies == NULL) {
        return;
    }

    while (Binding->Dependencies->First != NULL) {
        LPEXECUTABLE_MODULE_BINDING_DEPENDENCY Edge =
            (LPEXECUTABLE_MODULE_BINDING_DEPENDENCY)Binding->Dependencies->First;

        ListRemove(Binding->Dependencies, Edge);

        if (Edge->Binding != NULL) {
            ReleaseProcessModuleBindingLocked(Edge->Binding);
        }

        KernelHeapFree(Edge);
    }

    DeleteList(Binding->Dependencies);
    Binding->Dependencies = NULL;
}

/***************************************************************************/

/**
 * @brief Allocate one new binding object for one process and one module image.
 *
 * @param Process Owning process.
 * @param Image Shared module image.
 * @return New binding or NULL.
 */
static LPEXECUTABLE_MODULE_BINDING CreateProcessModuleBinding(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    Binding = (LPEXECUTABLE_MODULE_BINDING)CreateKernelObject(
        sizeof(EXECUTABLE_MODULE_BINDING),
        KOID_EXECUTABLE_MODULE_BINDING);
    if (Binding == NULL) {
        return NULL;
    }

    SetKernelObjectDestructor(Binding, (OBJECTDESTRUCTOR)DeleteExecutableModuleBinding);
    Binding->Process = Process;
    Binding->OwnerProcess = Process;
    Binding->Image = Image;
    Binding->ProcessReferences = 1;
    Binding->StateFlags = EXECUTABLE_MODULE_BINDING_STATE_CREATED;
    InitMutex(&(Binding->Mutex));
    SetMutexDebugInfo(&(Binding->Mutex), MUTEX_CLASS_KERNEL, TEXT("ExecutableModuleBinding"));

    Binding->Dependencies = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (Binding->Dependencies == NULL) {
        DestroyKernelObject(Binding);
        return NULL;
    }

    RetainExecutableModuleImage(Image);
    return Binding;
}

/***************************************************************************/

/**
 * @brief Ensure one process owns a binding list.
 *
 * @param Process Target process.
 * @return TRUE when the binding list exists.
 */
BOOL InitializeProcessModuleBindings(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->ModuleBindings != NULL) {
            return TRUE;
        }

        Process->ModuleBindings = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
        Process->ModuleBindingCount = 0;
        return Process->ModuleBindings != NULL;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Destroy all executable module bindings owned by one process.
 *
 * @param Process Target process.
 */
void DeleteProcessModuleBindings(LPPROCESS Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);

        if (Process->ModuleBindings != NULL) {
            while (Process->ModuleBindings->First != NULL) {
                LPEXECUTABLE_MODULE_BINDING Binding =
                    (LPEXECUTABLE_MODULE_BINDING)Process->ModuleBindings->First;

                ListRemove(Process->ModuleBindings, Binding);
                if (Process->ModuleBindingCount > 0) {
                    Process->ModuleBindingCount--;
                }

                UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                Binding->Process = NULL;
                Binding->OwnerProcess = NULL;
                DestroyKernelObject(Binding);
            }

            DeleteList(Process->ModuleBindings);
            Process->ModuleBindings = NULL;
        }

        Process->ModuleBindingCount = 0;
        UnlockMutex(&(Process->Mutex));
    }
}

/***************************************************************************/

/**
 * @brief Return the number of executable module bindings attached to one process.
 *
 * @param Process Target process.
 * @return Binding count.
 */
UINT GetProcessModuleBindingCount(LPPROCESS Process) {
    UINT Count = 0;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LockMutex(&(Process->Mutex), INFINITY);
        Count = Process->ModuleBindingCount;
        UnlockMutex(&(Process->Mutex));
    }

    return Count;
}

/***************************************************************************/

/**
 * @brief Find one executable module binding owned by one process.
 *
 * @param Process Target process.
 * @param Image Shared module image to match.
 * @return Matching binding or NULL.
 */
LPEXECUTABLE_MODULE_BINDING FindProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
            LockMutex(&(Process->Mutex), INFINITY);
            Binding = FindProcessModuleBindingLocked(Process, Image);
            UnlockMutex(&(Process->Mutex));
        }
    }

    return Binding;
}

/***************************************************************************/

/**
 * @brief Acquire one process-owned binding for one shared module image.
 *
 * Reuses an existing binding when the process already loaded the module.
 *
 * @param Process Target process.
 * @param Image Shared module image.
 * @return Existing or newly created binding.
 */
LPEXECUTABLE_MODULE_BINDING AcquireProcessModuleBinding(LPPROCESS Process, LPEXECUTABLE_MODULE_IMAGE Image) {
    LPEXECUTABLE_MODULE_BINDING Binding = NULL;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (!InitializeProcessModuleBindings(Process)) {
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            Binding = FindProcessModuleBindingLocked(Process, Image);
            if (Binding != NULL) {
                Binding->ProcessReferences++;
                UnlockMutex(&(Process->Mutex));
                return Binding;
            }

            Binding = CreateProcessModuleBinding(Process, Image);
            if (Binding == NULL) {
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            if (!ListAddItem(Process->ModuleBindings, Binding)) {
                DestroyKernelObject(Binding);
                UnlockMutex(&(Process->Mutex));
                return NULL;
            }

            Process->ModuleBindingCount++;
            UnlockMutex(&(Process->Mutex));
        }
    }

    return Binding;
}

/***************************************************************************/

/**
 * @brief Release one process-owned binding reference.
 *
 * @param Binding Binding to release.
 */
void ReleaseProcessModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding) {
    LPPROCESS Process = NULL;

    SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
        Process = Binding->Process;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            LockMutex(&(Process->Mutex), INFINITY);
            ReleaseProcessModuleBindingLocked(Binding);
            UnlockMutex(&(Process->Mutex));
        }
    }
}

/***************************************************************************/

/**
 * @brief Store one per-segment base address on one process binding.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @param SegmentIndex Segment index in executable metadata.
 * @param Base Installed base inside process address space.
 * @return TRUE on success.
 */
BOOL SetProcessModuleBindingSegmentBase(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    UINT SegmentIndex,
    LINEAR Base) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            if (SegmentIndex >= EXECUTABLE_MAX_SEGMENTS) {
                return FALSE;
            }

            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process == Process) {
                Binding->SegmentBases[SegmentIndex] = Base;
                Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_ASSIGNED;
                Result = TRUE;
            }

            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Store process-private layout addresses on one binding.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @param WritableDataBase Writable module data base.
 * @param GlobalOffsetTableBase Process-global GOT base if used.
 * @param ProcedureLinkageTableBase Process-global PLT base if used.
 * @param BookkeepingBase Module bookkeeping base if used.
 * @param StateFlags State bits to OR into the binding state.
 * @return TRUE on success.
 */
BOOL SetProcessModuleBindingLayout(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LINEAR WritableDataBase,
    LINEAR GlobalOffsetTableBase,
    LINEAR ProcedureLinkageTableBase,
    LINEAR BookkeepingBase,
    U32 StateFlags) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process == Process) {
                Binding->WritableDataBase = WritableDataBase;
                Binding->WritableDataSize = 0;
                Binding->GlobalOffsetTableBase = GlobalOffsetTableBase;
                Binding->ProcedureLinkageTableBase = ProcedureLinkageTableBase;
                Binding->BookkeepingBase = BookkeepingBase;
                Binding->StateFlags |= StateFlags;
                Result = TRUE;
            }

            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Record one dependency edge between two bindings of the same process.
 *
 * @param Process Owning process.
 * @param Binding Binding that depends on another binding.
 * @param Dependency Binding required by @p Binding.
 * @return TRUE when the dependency edge exists.
 */
BOOL AddProcessModuleBindingDependency(
    LPPROCESS Process,
    LPEXECUTABLE_MODULE_BINDING Binding,
    LPEXECUTABLE_MODULE_BINDING Dependency) {
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            SAFE_USE_VALID_ID(Dependency, KOID_EXECUTABLE_MODULE_BINDING) {
                LockMutex(&(Process->Mutex), INFINITY);

                if (Binding->Process == Process && Dependency->Process == Process) {
                    Result = AddProcessModuleBindingDependencyLocked(Binding, Dependency);
                }

                UnlockMutex(&(Process->Mutex));
            }
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Initialize process-global writable state for one installed binding.
 *
 * @param Binding Target binding.
 * @return TRUE when the binding global data state is ready.
 */
static BOOL InitializeProcessModuleGlobalDataLocked(LPEXECUTABLE_MODULE_BINDING Binding) {
    if (Binding == NULL || Binding->Image == NULL) {
        return FALSE;
    }

    Binding->WritableDataBase = 0;
    Binding->WritableDataSize = 0;

    for (UINT SegmentIndex = 0; SegmentIndex < Binding->Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Binding->Image->Metadata.Segments[SegmentIndex]);

        if (Segment->SourceType != PT_LOAD || Segment->MemorySize == 0) {
            continue;
        }

        if ((Segment->Access & EXECUTABLE_SEGMENT_ACCESS_WRITE) == 0) {
            continue;
        }

        if (Binding->SegmentBases[SegmentIndex] == 0 || Binding->SegmentSizes[SegmentIndex] == 0) {
            return FALSE;
        }

        if (Binding->WritableDataBase == 0) {
            Binding->WritableDataBase = Binding->SegmentBases[SegmentIndex];
        }

        Binding->WritableDataSize += Binding->SegmentSizes[SegmentIndex];
    }

    Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_GLOBAL_DATA_INITIALIZED;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Install all module segments into one process binding.
 *
 * The target process page directory must be active while this function runs.
 *
 * @param Process Owning process.
 * @param Binding Target binding.
 * @return TRUE when all loadable segments are installed.
 */
BOOL InstallProcessModuleBindingSegments(LPPROCESS Process, LPEXECUTABLE_MODULE_BINDING Binding) {
    PROCESS_MODULE_SYMBOL_RESOLVER_CONTEXT ResolverContext;
    BOOL Result = FALSE;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
            LockMutex(&(Process->Mutex), INFINITY);

            if (Binding->Process != Process || Binding->Image == NULL) {
                UnlockMutex(&(Process->Mutex));
                return FALSE;
            }

            if ((Binding->StateFlags & EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED) != 0) {
                UnlockMutex(&(Process->Mutex));
                return TRUE;
            }

            for (UINT SegmentIndex = 0; SegmentIndex < Binding->Image->Metadata.SegmentCount; SegmentIndex++) {
                LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Binding->Image->Metadata.Segments[SegmentIndex]);
                LPEXECUTABLE_MODULE_SHARED_SEGMENT SharedSegment = &(Binding->Image->SharedSegments[SegmentIndex]);
                LPEXECUTABLE_MODULE_SHARED_SEGMENT PrivateSegment = &(Binding->Image->PrivateSegments[SegmentIndex]);

                if (Segment->SourceType != PT_LOAD || Segment->MemorySize == 0) {
                    continue;
                }

                if (ProcessModuleSegmentIsWritableExecutable(Segment)) {
                    ERROR(TEXT("Writable executable segment rejected index=%u"),
                          SegmentIndex);
                    UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                    UnlockMutex(&(Process->Mutex));
                    return FALSE;
                }

                if (PrivateSegment->Present != FALSE) {
                    if (!InstallProcessModulePrivateSegment(Process, Binding, PrivateSegment)) {
                        WARNING(TEXT("[InstallProcessModuleBindingSegments] Private segment install failed index=%u"),
                                SegmentIndex);
                        UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                        UnlockMutex(&(Process->Mutex));
                        return FALSE;
                    }
                    continue;
                }

                if (SharedSegment->Present != FALSE) {
                    if (!InstallProcessModuleSharedSegment(Process, Binding, SharedSegment)) {
                        WARNING(TEXT("[InstallProcessModuleBindingSegments] Shared segment install failed index=%u"),
                                SegmentIndex);
                        UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                        UnlockMutex(&(Process->Mutex));
                        return FALSE;
                    }
                }
            }

            MemorySet(&ResolverContext, 0, sizeof(ResolverContext));
            ResolverContext.Process = Process;
            ResolverContext.TargetBinding = Binding;
            if (!RelocateExecutableModuleBinding(
                    Binding->Image,
                    Binding->SegmentBases,
                    Binding->SegmentSizes,
                    ResolveProcessModuleSymbol,
                    &ResolverContext)) {
                WARNING(TEXT("[InstallProcessModuleBindingSegments] Relocation failed"));
                UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                UnlockMutex(&(Process->Mutex));
                return FALSE;
            }

            if (!InitializeProcessModuleGlobalDataLocked(Binding)) {
                WARNING(TEXT("[InstallProcessModuleBindingSegments] Global data init failed"));
                UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                UnlockMutex(&(Process->Mutex));
                return FALSE;
            }

            if (!InitializeProcessModuleTls(Process, Binding)) {
                WARNING(TEXT("[InstallProcessModuleBindingSegments] TLS init failed"));
                UninstallProcessModuleBindingSegmentsLocked(Process, Binding);
                UnlockMutex(&(Process->Mutex));
                return FALSE;
            }

            Binding->StateFlags |= EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_ASSIGNED |
                                   EXECUTABLE_MODULE_BINDING_STATE_SEGMENTS_INSTALLED |
                                   EXECUTABLE_MODULE_BINDING_STATE_RELOCATED;
            Result = TRUE;
            UnlockMutex(&(Process->Mutex));
        }
    }

    return Result;
}

/***************************************************************************/

/**
 * @brief Destroy one process-owned executable module binding.
 *
 * @param Binding Binding to destroy.
 */
void DeleteExecutableModuleBinding(LPEXECUTABLE_MODULE_BINDING Binding) {
    SAFE_USE_VALID_ID(Binding, KOID_EXECUTABLE_MODULE_BINDING) {
        if (Binding->Process != NULL) {
            UninstallProcessModuleBindingSegmentsLocked(Binding->Process, Binding);
        }

        DeleteExecutableModuleBindingDependencies(Binding);

        if (Binding->Image != NULL) {
            ReleaseExecutableModuleImage(Binding->Image);
            Binding->Image = NULL;
        }

        Binding->TypeID = KOID_NONE;
        KernelHeapFree(Binding);
    }
}
