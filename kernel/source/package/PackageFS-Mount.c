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


    PackageFS mount lifecycle implementation

\************************************************************************/

#include "PackageFS-Internal.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "fs/SystemFS.h"

/************************************************************************/

/**
 * @brief Build and mount one PackageFS from validated package bytes.
 * @param PackageBytes Package bytes.
 * @param PackageSize Package size.
 * @param VolumeName Filesystem name.
 * @param Options Parser options.
 * @param MountedFileSystemOut Optional output pointer.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSMountFromBuffer(LPCVOID PackageBytes,
                             U32 PackageSize,
                             LPCSTR VolumeName,
                             const EPK_PARSER_OPTIONS* Options,
                             LPFILESYSTEM* MountedFileSystemOut) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    EPK_PARSER_OPTIONS EffectiveOptions = {
        .VerifyPackageHash = TRUE,
        .VerifySignature = TRUE,
        .RequireSignature = FALSE};
    U32 ValidationStatus;
    U32 Result;

    if (PackageBytes == NULL || PackageSize == 0 || STRING_EMPTY(VolumeName)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Options != NULL) {
        EffectiveOptions = *Options;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)CreateKernelObject(sizeof(PACKAGEFSFILESYSTEM), KOID_FILESYSTEM);
    if (FileSystem == NULL) {
        return DF_RETURN_NO_MEMORY;
    }

    FileSystem->Root = NULL;
    FileSystem->PackageBytes = NULL;
    FileSystem->PackageSize = 0;
    MemorySet(&FileSystem->Package, 0, sizeof(EPK_VALIDATED_PACKAGE));
    FileSystem->Header.Mounted = TRUE;
    FileSystem->Header.Driver = &PackageFSDriver;
    FileSystem->Header.StorageUnit = NULL;
    FileSystem->Header.Partition.Scheme = PARTITION_SCHEME_VIRTUAL;
    FileSystem->Header.Partition.Type = FSID_NONE;
    FileSystem->Header.Partition.Format = PARTITION_FORMAT_UNKNOWN;
    FileSystem->Header.Partition.Index = 0;
    FileSystem->Header.Partition.Flags = 0;
    FileSystem->Header.Partition.StartSector = 0;
    FileSystem->Header.Partition.NumSectors = 0;
    MemorySet(FileSystem->Header.Partition.TypeGuid, 0, GPT_GUID_LENGTH);
    StringCopy(FileSystem->Header.Name, VolumeName);

    InitMutex(&FileSystem->Header.Mutex);
    InitMutex(&FileSystem->FilesMutex);
    ChunkCacheInit(&FileSystem->ChunkCache, PACKAGEFS_CHUNK_CACHE_CAPACITY, PACKAGEFS_CHUNK_CACHE_TTL_MS);

    FileSystem->PackageBytes = (U8*)KernelHeapAlloc(PackageSize);
    if (FileSystem->PackageBytes == NULL) {
        ChunkCacheDeinit(&FileSystem->ChunkCache);
        ReleaseKernelObject(FileSystem);
        return DF_RETURN_NO_MEMORY;
    }

    MemoryCopy(FileSystem->PackageBytes, PackageBytes, PackageSize);
    FileSystem->PackageSize = PackageSize;

    ValidationStatus = EpkValidatePackageBuffer(FileSystem->PackageBytes,
                                                FileSystem->PackageSize,
                                                &EffectiveOptions,
                                                &FileSystem->Package);
    if (ValidationStatus != EPK_VALIDATION_OK) {
        ERROR(TEXT("Package validation failed status=%u"), ValidationStatus);
        KernelHeapFree(FileSystem->PackageBytes);
        ChunkCacheDeinit(&FileSystem->ChunkCache);
        ReleaseKernelObject(FileSystem);
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = PackageFSBuildTree(FileSystem);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("Tree build failed status=%u"), Result);
        EpkReleaseValidatedPackage(&FileSystem->Package);
        KernelHeapFree(FileSystem->PackageBytes);
        ChunkCacheDeinit(&FileSystem->ChunkCache);
        ReleaseKernelObject(FileSystem);
        return Result;
    }

    LockMutex(MUTEX_FILESYSTEM, INFINITY);
    ListAddItem(GetFileSystemList(), &FileSystem->Header);
    UnlockMutex(MUTEX_FILESYSTEM);

    if (FileSystemReady()) {
        if (!SystemFSMountFileSystem(&FileSystem->Header)) {
            WARNING(TEXT("SystemFS mount failed for %s"), FileSystem->Header.Name);
        }
    }

    if (MountedFileSystemOut != NULL) {
        *MountedFileSystemOut = &FileSystem->Header;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unmount one PackageFS instance and release resources.
 * @param FileSystem Filesystem pointer to unmount.
 * @return TRUE on success.
 */
BOOL PackageFSUnmount(LPFILESYSTEM FileSystem) {
    LPPACKAGEFSFILESYSTEM This;
    LPLISTNODE Node;

    if (FileSystem == NULL || FileSystem->Driver != &PackageFSDriver) {
        return FALSE;
    }

    This = (LPPACKAGEFSFILESYSTEM)FileSystem;

    LockMutex(MUTEX_FILESYSTEM, INFINITY);

    for (Node = GetFileList()->First; Node != NULL; Node = Node->Next) {
        LPFILE Open = (LPFILE)Node;
        if (Open->FileSystem == FileSystem) {
            UnlockMutex(MUTEX_FILESYSTEM);
            WARNING(TEXT("Cannot unmount %s while files are open"), FileSystem->Name);
            return FALSE;
        }
    }

    if (FileSystemReady()) {
        SystemFSUnmountFileSystem(FileSystem);
    }

    ListErase(GetFileSystemList(), FileSystem);
    FileSystem->Mounted = FALSE;
    UnlockMutex(MUTEX_FILESYSTEM);

    PackageFSReleaseNodeTree(This->Root);
    This->Root = NULL;
    ChunkCacheDeinit(&This->ChunkCache);

    EpkReleaseValidatedPackage(&This->Package);

    if (This->PackageBytes != NULL) {
        KernelHeapFree(This->PackageBytes);
        This->PackageBytes = NULL;
    }

    ReleaseKernelObject(This);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Return mounted volume label.
 * @param Info Volume information structure.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSGetVolumeInfo(LPVOLUME_INFO Info) {
    LPFILESYSTEM FileSystem;

    if (Info == NULL || Info->Size != sizeof(VOLUME_INFO) || Info->Volume == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    FileSystem = (LPFILESYSTEM)Info->Volume;
    if (FileSystem == NULL || FileSystem->Driver != &PackageFSDriver) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringCopy(Info->Name, FileSystem->Name);
    return DF_RETURN_SUCCESS;
}
