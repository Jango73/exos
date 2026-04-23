
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


    Executable module image cache

\************************************************************************/

#include "exec/ExecutableModule.h"
#include "exec/ExecutableELF.h"

#include "core/Kernel.h"
#include "core/KernelData.h"
#include "fs/File.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "text/CoreString.h"

/***************************************************************************/

/**
 * @brief Read one byte range from one module file at one fixed offset.
 *
 * @param File Open file.
 * @param Offset Byte offset in file.
 * @param Buffer Destination buffer.
 * @param Size Number of bytes to read.
 * @return TRUE when all requested bytes are read.
 */
static BOOL ReadExecutableModuleFileBytes(LPFILE File, UINT Offset, LPVOID Buffer, UINT Size) {
    FILE_OPERATION FileOperation;

    if (File == NULL || Buffer == NULL) return FALSE;
    if (Size == 0) return TRUE;

    FileOperation.Header.Size = sizeof(FILE_OPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;
    FileOperation.Buffer = NULL;
    FileOperation.NumBytes = Offset;

    if (SetFilePosition(&FileOperation) != DF_RETURN_SUCCESS) {
        return FALSE;
    }

    FileOperation.Buffer = Buffer;
    FileOperation.NumBytes = Size;
    return ReadFile(&FileOperation) == Size;
}

/***************************************************************************/

/**
 * @brief Capture one stable cache identity from one currently opened file.
 *
 * @param File Open file to inspect.
 * @param Identity Output identity snapshot.
 * @return TRUE on success, FALSE on invalid input.
 */
static BOOL CaptureExecutableModuleFileIdentity(LPFILE File, LPEXECUTABLE_MODULE_FILE_IDENTITY Identity) {
    if (File == NULL || Identity == NULL) {
        return FALSE;
    }

    MemorySet(Identity, 0, sizeof(EXECUTABLE_MODULE_FILE_IDENTITY));

    LockMutex(&(File->Mutex), INFINITY);

    Identity->FileSystem = File->FileSystem;
    Identity->FileSize = GetFileSize(File);
    Identity->Modified = File->Modified;
    StringCopy(Identity->Name, File->Name);

    if (Identity->FileSystem != NULL) {
        Identity->FileSystem->References++;
    }

    UnlockMutex(&(File->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release one retained file identity snapshot.
 *
 * @param Identity Identity snapshot to release.
 */
static void ReleaseExecutableModuleFileIdentity(LPEXECUTABLE_MODULE_FILE_IDENTITY Identity) {
    if (Identity == NULL) {
        return;
    }

    if (Identity->FileSystem != NULL) {
        ReleaseKernelObject(Identity->FileSystem);
        Identity->FileSystem = NULL;
    }
}

/***************************************************************************/

/**
 * @brief Compare two module file identities for cache reuse.
 *
 * @param Left First identity.
 * @param Right Second identity.
 * @return TRUE when both identities describe the same source file revision.
 */
static BOOL ExecutableModuleFileIdentityEquals(
    const EXECUTABLE_MODULE_FILE_IDENTITY* Left,
    const EXECUTABLE_MODULE_FILE_IDENTITY* Right) {
    if (Left == NULL || Right == NULL) {
        return FALSE;
    }

    if (Left->FileSystem != Right->FileSystem) {
        return FALSE;
    }

    if (Left->FileSize != Right->FileSize) {
        return FALSE;
    }

    if (!STRINGS_EQUAL(Left->Name, Right->Name)) {
        return FALSE;
    }

    return MemoryCompare(&(Left->Modified), &(Right->Modified), sizeof(DATETIME)) == 0;
}

/***************************************************************************/

/**
 * @brief Find one cached module image matching one file identity and ABI.
 *
 * @param Identity File identity to match.
 * @param Metadata Parsed executable metadata to match.
 * @return Matching cached image or NULL.
 */
static LPEXECUTABLE_MODULE_IMAGE FindExecutableModuleImage(
    const EXECUTABLE_MODULE_FILE_IDENTITY* Identity,
    const EXECUTABLE_METADATA* Metadata) {
    LPLIST ModuleImageList = NULL;

    if (Identity == NULL || Metadata == NULL) {
        return NULL;
    }

    ModuleImageList = GetExecutableModuleImageList();
    if (ModuleImageList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = ModuleImageList->First; Node != NULL; Node = Node->Next) {
        LPEXECUTABLE_MODULE_IMAGE Image = (LPEXECUTABLE_MODULE_IMAGE)Node;

        if (Image == NULL) continue;
        if (Image->Metadata.Format != Metadata->Format) continue;
        if (Image->Metadata.Architecture != Metadata->Architecture) continue;
        if (Image->Metadata.Target != Metadata->Target) continue;
        if (!ExecutableModuleFileIdentityEquals(&(Image->Identity), Identity)) continue;

        return Image;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Determine whether one segment can be shared through one module image backing.
 *
 * @param Segment Segment descriptor to inspect.
 * @return TRUE for shared read-only file-backed segments.
 */
static BOOL IsExecutableModuleSharedSegmentCandidate(const EXECUTABLE_SEGMENT_DESCRIPTOR* Segment) {
    if (Segment == NULL) {
        return FALSE;
    }

    if (Segment->SourceType != PT_LOAD) {
        return FALSE;
    }

    if ((Segment->Access & EXECUTABLE_SEGMENT_ACCESS_WRITE) != 0) {
        return FALSE;
    }

    if (Segment->FileSize == 0) {
        return FALSE;
    }

    if (Segment->MemorySize == 0) {
        return FALSE;
    }

    return Segment->Mapping == EXECUTABLE_SEGMENT_MAPPING_CODE || Segment->Mapping == EXECUTABLE_SEGMENT_MAPPING_DATA;
}

/***************************************************************************/

/**
 * @brief Determine whether one segment needs a per-process private copy.
 *
 * @param Segment Segment descriptor to inspect.
 * @return TRUE for writable loadable module data.
 */
static BOOL IsExecutableModulePrivateSegmentCandidate(const EXECUTABLE_SEGMENT_DESCRIPTOR* Segment) {
    if (Segment == NULL) {
        return FALSE;
    }

    if (Segment->SourceType != PT_LOAD) {
        return FALSE;
    }

    if ((Segment->Access & EXECUTABLE_SEGMENT_ACCESS_WRITE) == 0) {
        return FALSE;
    }

    if (Segment->MemorySize == 0) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Free all physical pages attached to one cached shared segment.
 *
 * @param Segment Shared segment descriptor to release.
 */
static void DeleteExecutableModuleSharedSegment(LPEXECUTABLE_MODULE_SHARED_SEGMENT Segment) {
    UINT PageIndex;

    if (Segment == NULL) {
        return;
    }

    if (Segment->PhysicalPages != NULL) {
        for (PageIndex = 0; PageIndex < Segment->PageCount; PageIndex++) {
            if (Segment->PhysicalPages[PageIndex] != 0) {
                FreePhysicalPage(Segment->PhysicalPages[PageIndex]);
            }
        }

        KernelHeapFree(Segment->PhysicalPages);
    }

    MemorySet(Segment, 0, sizeof(EXECUTABLE_MODULE_SHARED_SEGMENT));
}

/***************************************************************************/

/**
 * @brief Build one read-only shared backing for one validated segment.
 *
 * @param File Source file.
 * @param Segment Segment metadata.
 * @param SegmentIndex Index in Metadata->Segments.
 * @param SharedSegment Destination shared-segment descriptor.
 * @return TRUE on success, FALSE on allocation or validation failure.
 */
static BOOL BuildExecutableModuleSharedSegment(
    LPFILE File,
    const EXECUTABLE_SEGMENT_DESCRIPTOR* Segment,
    UINT SegmentIndex,
    LPEXECUTABLE_MODULE_SHARED_SEGMENT SharedSegment) {
    UINT VirtualAddressOffset;
    UINT FileBackedStart;
    UINT FileBackedEnd;
    UINT PageCount;
    UINT PageIndex;
    U8* PageBuffer = NULL;
    UINT ReadOffset;
    UINT ReadSize;

    if (File == NULL || Segment == NULL || SharedSegment == NULL) {
        return FALSE;
    }

    VirtualAddressOffset = Segment->VirtualAddress & PAGE_SIZE_MASK;
    if ((Segment->FileOffset & PAGE_SIZE_MASK) != VirtualAddressOffset) {
        WARNING(TEXT("Misaligned file-backed segment index=%u offset=%u vaddr=%p"),
            SegmentIndex,
            Segment->FileOffset,
            Segment->VirtualAddress);
        return FALSE;
    }

    FileBackedStart = VirtualAddressOffset;
    FileBackedEnd = VirtualAddressOffset + Segment->FileSize;
    PageCount = (UINT)(PAGE_ALIGN(VirtualAddressOffset + Segment->MemorySize) >> PAGE_SIZE_MUL);
    if (PageCount == 0) {
        return FALSE;
    }

    MemorySet(SharedSegment, 0, sizeof(EXECUTABLE_MODULE_SHARED_SEGMENT));
    SharedSegment->PhysicalPages = (PHYSICAL*)KernelHeapAlloc(PageCount * sizeof(PHYSICAL));
    if (SharedSegment->PhysicalPages == NULL) {
        ERROR(TEXT("KernelHeapAlloc failed pages=%u"), PageCount);
        return FALSE;
    }

    MemorySet(SharedSegment->PhysicalPages, 0, PageCount * sizeof(PHYSICAL));
    SharedSegment->Present = TRUE;
    SharedSegment->SegmentIndex = SegmentIndex;
    SharedSegment->AlignedVirtualAddress = Segment->VirtualAddress - VirtualAddressOffset;
    SharedSegment->VirtualAddressOffset = VirtualAddressOffset;
    SharedSegment->FileBackedSize = FileBackedEnd;
    SharedSegment->MemorySize = (UINT)PAGE_ALIGN(VirtualAddressOffset + Segment->MemorySize);
    SharedSegment->PageCount = PageCount;
    PageBuffer = (U8*)KernelHeapAlloc(PAGE_SIZE);
    if (PageBuffer == NULL) {
        ERROR(TEXT("KernelHeapAlloc failed page_buffer"));
        DeleteExecutableModuleSharedSegment(SharedSegment);
        return FALSE;
    }

    for (PageIndex = 0; PageIndex < PageCount; PageIndex++) {
        PHYSICAL PhysicalPage = AllocPhysicalPage();
        UINT PageReadOffset = PageIndex << PAGE_SIZE_MUL;

        if (PhysicalPage == 0) {
            ERROR(TEXT("AllocPhysicalPage failed index=%u page=%u"),
                SegmentIndex,
                PageIndex);
            KernelHeapFree(PageBuffer);
            DeleteExecutableModuleSharedSegment(SharedSegment);
            return FALSE;
        }

        SharedSegment->PhysicalPages[PageIndex] = PhysicalPage;
        MemorySet(PageBuffer, 0, PAGE_SIZE);
        ReadOffset = 0;
        ReadSize = 0;

        if (Segment->FileSize != 0 && PageReadOffset < FileBackedEnd) {
            UINT PageEnd = PageReadOffset + PAGE_SIZE;
            UINT DataStart = (PageReadOffset > FileBackedStart) ? PageReadOffset : FileBackedStart;
            UINT DataEnd = (PageEnd < FileBackedEnd) ? PageEnd : FileBackedEnd;
            UINT DestinationOffset;

            if (DataStart < DataEnd) {
                DestinationOffset = DataStart - PageReadOffset;
                ReadOffset = Segment->FileOffset + (DataStart - FileBackedStart);
                ReadSize = DataEnd - DataStart;

                if (!ReadExecutableModuleFileBytes(File, ReadOffset, (LPVOID)(PageBuffer + DestinationOffset), ReadSize)) {
                    ERROR(TEXT("ReadExecutableModuleFileBytes failed index=%u offset=%u size=%u"),
                        SegmentIndex,
                        ReadOffset,
                        ReadSize);
                    KernelHeapFree(PageBuffer);
                    DeleteExecutableModuleSharedSegment(SharedSegment);
                    return FALSE;
                }
            }
        }

        {
            LINEAR MappedPage = MapTemporaryPhysicalPage1(PhysicalPage);
            if (MappedPage == 0) {
                ERROR(TEXT("MapTemporaryPhysicalPage1 failed index=%u page=%u"),
                    SegmentIndex,
                    PageIndex);
                KernelHeapFree(PageBuffer);
                DeleteExecutableModuleSharedSegment(SharedSegment);
                return FALSE;
            }

            MemoryCopy((LPVOID)MappedPage, (LPCVOID)PageBuffer, PAGE_SIZE);
        }
    }

    KernelHeapFree(PageBuffer);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build all shared read-only backings for one module image.
 *
 * @param File Source file.
 * @param Image Image under construction.
 * @return TRUE on success, FALSE on any backing failure.
 */
static BOOL BuildExecutableModuleSharedSegments(LPFILE File, LPEXECUTABLE_MODULE_IMAGE Image) {
    UINT SegmentIndex;

    if (File == NULL || Image == NULL) {
        return FALSE;
    }

    for (SegmentIndex = 0; SegmentIndex < Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Image->Metadata.Segments[SegmentIndex]);
        LPEXECUTABLE_MODULE_SHARED_SEGMENT SharedSegment = &(Image->SharedSegments[SegmentIndex]);

        if (!IsExecutableModuleSharedSegmentCandidate(Segment)) {
            continue;
        }

        if (!BuildExecutableModuleSharedSegment(File, Segment, SegmentIndex, SharedSegment)) {
            return FALSE;
        }

        Image->SharedSegmentCount++;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build per-process private segment templates for one module image.
 *
 * @param File Source file.
 * @param Image Image under construction.
 * @return TRUE on success, FALSE on any backing failure.
 */
static BOOL BuildExecutableModulePrivateSegments(LPFILE File, LPEXECUTABLE_MODULE_IMAGE Image) {
    UINT SegmentIndex;

    if (File == NULL || Image == NULL) {
        return FALSE;
    }

    for (SegmentIndex = 0; SegmentIndex < Image->Metadata.SegmentCount; SegmentIndex++) {
        LPEXECUTABLE_SEGMENT_DESCRIPTOR Segment = &(Image->Metadata.Segments[SegmentIndex]);
        LPEXECUTABLE_MODULE_SHARED_SEGMENT PrivateSegment = &(Image->PrivateSegments[SegmentIndex]);

        if (!IsExecutableModulePrivateSegmentCandidate(Segment)) {
            continue;
        }

        if (!BuildExecutableModuleSharedSegment(File, Segment, SegmentIndex, PrivateSegment)) {
            return FALSE;
        }

        Image->PrivateSegmentCount++;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Allocate and populate one new executable module image object.
 *
 * @param File Source file.
 * @param Identity Captured file identity.
 * @param Metadata Parsed module metadata.
 * @return New module image or NULL on failure.
 */
static LPEXECUTABLE_MODULE_IMAGE CreateExecutableModuleImage(
    LPFILE File,
    LPEXECUTABLE_MODULE_FILE_IDENTITY Identity,
    const EXECUTABLE_METADATA* Metadata) {
    LPEXECUTABLE_MODULE_IMAGE Image = NULL;

    if (File == NULL || Identity == NULL || Metadata == NULL) {
        return NULL;
    }

    Image = (LPEXECUTABLE_MODULE_IMAGE)CreateKernelObject(sizeof(EXECUTABLE_MODULE_IMAGE), KOID_EXECUTABLE_MODULE_IMAGE);
    if (Image == NULL) {
        return NULL;
    }

    SetKernelObjectDestructor(Image, (OBJECTDESTRUCTOR)DeleteExecutableModuleImage);
    InitMutex(&(Image->Mutex));
    SetMutexDebugInfo(&(Image->Mutex), MUTEX_CLASS_KERNEL, TEXT("ExecutableModuleImage"));
    Image->Identity = *Identity;
    Identity->FileSystem = NULL;
    Image->Metadata = *Metadata;
    Image->SharedSegmentCount = 0;
    Image->PrivateSegmentCount = 0;

    if (!BuildExecutableModuleSharedSegments(File, Image)) {
        DeleteExecutableModuleImage(Image);
        return NULL;
    }

    if (!BuildExecutableModulePrivateSegments(File, Image)) {
        DeleteExecutableModuleImage(Image);
        return NULL;
    }

    return Image;
}

/***************************************************************************/

/**
 * @brief Acquire one shared module image object from the global cache.
 *
 * The returned object keeps a kernel-object reference for the caller.
 *
 * @param File Open module file.
 * @return Cached or newly created module image, or NULL on failure.
 */
LPEXECUTABLE_MODULE_IMAGE AcquireExecutableModuleImage(LPFILE File) {
    EXECUTABLE_METADATA Metadata;
    EXECUTABLE_MODULE_FILE_IDENTITY Identity;
    LPEXECUTABLE_MODULE_IMAGE Image = NULL;
    LPEXECUTABLE_MODULE_IMAGE ExistingImage = NULL;
    LPLIST ModuleImageList = NULL;

    if (File == NULL || File->TypeID != KOID_FILE) {
        ERROR(TEXT("Invalid file"));
        return NULL;
    }

    if (!GetExecutableModuleInfo(File, &Metadata)) {
        WARNING(TEXT("GetExecutableModuleInfo failed name=%s"), File->Name);
        return NULL;
    }

    if (Metadata.Dynamic.RequiresTextRelocation != FALSE) {
        WARNING(TEXT("Text relocations are not supported name=%s"), File->Name);
        return NULL;
    }

    if (Metadata.Dynamic.HasConstructors != FALSE) {
        WARNING(TEXT("Module constructors are not supported name=%s"), File->Name);
        return NULL;
    }

    if (!CaptureExecutableModuleFileIdentity(File, &Identity)) {
        ERROR(TEXT("CaptureExecutableModuleFileIdentity failed"));
        return NULL;
    }

    LockMutex(MUTEX_KERNEL, INFINITY);
    ExistingImage = FindExecutableModuleImage(&Identity, &Metadata);
    if (ExistingImage != NULL) {
        ExistingImage->References++;
        UnlockMutex(MUTEX_KERNEL);
        ReleaseExecutableModuleFileIdentity(&Identity);
        return ExistingImage;
    }
    UnlockMutex(MUTEX_KERNEL);

    Image = CreateExecutableModuleImage(File, &Identity, &Metadata);
    if (Image == NULL) {
        ReleaseExecutableModuleFileIdentity(&Identity);
        WARNING(TEXT("CreateExecutableModuleImage failed name=%s"), Identity.Name);
        return NULL;
    }

    LockMutex(MUTEX_KERNEL, INFINITY);
    ExistingImage = FindExecutableModuleImage(&Identity, &Metadata);
    if (ExistingImage != NULL) {
        ExistingImage->References++;
        UnlockMutex(MUTEX_KERNEL);
        DeleteExecutableModuleImage(Image);
        return ExistingImage;
    }

    ModuleImageList = GetExecutableModuleImageList();
    if (ModuleImageList == NULL) {
        UnlockMutex(MUTEX_KERNEL);
        DeleteExecutableModuleImage(Image);
        return NULL;
    }

    ListAddTail(ModuleImageList, Image);
    UnlockMutex(MUTEX_KERNEL);

    return Image;
}

/***************************************************************************/

/**
 * @brief Increment one module image reference count.
 *
 * @param Image Module image to retain.
 */
void RetainExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image) {
    SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
        Image->References++;
    }
}

/***************************************************************************/

/**
 * @brief Release one caller reference on one module image object.
 *
 * @param Image Module image to release.
 */
void ReleaseExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image) {
    SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
        ReleaseKernelObject(Image);
    }
}

/***************************************************************************/

/**
 * @brief Destroy one module image object and all retained backings.
 *
 * @param Image Module image to destroy.
 */
void DeleteExecutableModuleImage(LPEXECUTABLE_MODULE_IMAGE Image) {
    UINT SegmentIndex;

    SAFE_USE_VALID_ID(Image, KOID_EXECUTABLE_MODULE_IMAGE) {
        for (SegmentIndex = 0; SegmentIndex < ARRAY_COUNT(Image->SharedSegments); SegmentIndex++) {
            DeleteExecutableModuleSharedSegment(&(Image->SharedSegments[SegmentIndex]));
            DeleteExecutableModuleSharedSegment(&(Image->PrivateSegments[SegmentIndex]));
        }

        ReleaseExecutableModuleFileIdentity(&(Image->Identity));
        Image->TypeID = KOID_NONE;
        KernelHeapFree(Image);
    }
}
