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


    Package namespace integration

\************************************************************************/

#include "package/PackageNamespace.h"

#include "text/CoreString.h"
#include "core/KernelData.h"
#include "log/Log.h"
#include "fs/SystemFS.h"
#include "utils/Helpers.h"
#include "utils/KernelPath.h"

/***************************************************************************/

typedef struct tag_PACKAGENAMESPACE_PATHS {
    STR UsersRoot[MAX_PATH_NAME];
    STR CurrentUserAlias[MAX_PATH_NAME];
    STR PrivatePackageAlias[MAX_PATH_NAME];
    STR PrivateUserDataAlias[MAX_PATH_NAME];
    BOOL Loaded;
} PACKAGENAMESPACE_PATHS;

static PACKAGENAMESPACE_PATHS PackageNamespacePaths = {
    .UsersRoot = "",
    .CurrentUserAlias = "",
    .PrivatePackageAlias = "",
    .PrivateUserDataAlias = "",
    .Loaded = FALSE};

/***************************************************************************/

/**
 * @brief Resolve package namespace paths from KernelPath configuration keys.
 * @return TRUE when all paths are resolved.
 */
static BOOL PackageNamespaceLoadPaths(void) {
    if (!KernelPathResolve(KERNEL_PATH_KEY_USERS_ROOT,
            KERNEL_PATH_DEFAULT_USERS_ROOT,
            PackageNamespacePaths.UsersRoot,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_CURRENT_USER_ALIAS,
            KERNEL_PATH_DEFAULT_CURRENT_USER_ALIAS,
            PackageNamespacePaths.CurrentUserAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_PRIVATE_PACKAGE_ALIAS,
            KERNEL_PATH_DEFAULT_PRIVATE_PACKAGE_ALIAS,
            PackageNamespacePaths.PrivatePackageAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_PRIVATE_USER_DATA_ALIAS,
            KERNEL_PATH_DEFAULT_PRIVATE_USER_DATA_ALIAS,
            PackageNamespacePaths.PrivateUserDataAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }

    PackageNamespacePaths.Loaded = TRUE;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure package namespace paths are resolved before use.
 * @return TRUE when paths are loaded.
 */
static BOOL PackageNamespaceEnsurePathsLoaded(void) {
    if (PackageNamespacePaths.Loaded) return TRUE;

    if (!PackageNamespaceLoadPaths()) {
        ERROR(TEXT("KernelPath resolution failed"));
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check whether a node exists in SystemFS.
 * @param Path Absolute path.
 * @return TRUE when path exists.
 */
static BOOL PackageNamespacePathExists(LPCSTR Path) {
    FILESYSTEM_PATHCHECK Check;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;

    StringCopy(Check.CurrentFolder, TEXT("/"));
    StringCopy(Check.SubFolder, Path);
    return (BOOL)GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Check);
}

/***************************************************************************/

/**
 * @brief Ensure one folder exists in SystemFS.
 * @param Path Absolute folder path.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceEnsureFolder(LPCSTR Path) {
    FILE_INFO Info;
    U32 Result;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;
    if (PackageNamespacePathExists(Path)) return TRUE;

    Info.Size = sizeof(FILE_INFO);
    Info.FileSystem = GetSystemFS();
    Info.Attributes = FS_ATTR_FOLDER;
    Info.Flags = 0;
    StringCopy(Info.Name, Path);

    Result = GetSystemFS()->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&Info);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Create folder failed path=%s status=%u"),
            Path,
            Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure one absolute folder path and its parent chain exist.
 * @param Path Absolute folder path.
 * @return TRUE when all segments are available.
 */
static BOOL PackageNamespaceEnsureFolderChain(LPCSTR Path) {
    STR SegmentPath[MAX_PATH_NAME];
    UINT Index;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;

    StringCopy(SegmentPath, Path);
    for (Index = 1; SegmentPath[Index] != STR_NULL; Index++) {
        if (SegmentPath[Index] != PATH_SEP) continue;

        SegmentPath[Index] = STR_NULL;
        if (!PackageNamespaceEnsureFolder(SegmentPath)) return FALSE;
        SegmentPath[Index] = PATH_SEP;
    }

    return PackageNamespaceEnsureFolder(SegmentPath);
}

/***************************************************************************/

/**
 * @brief Build "Base/Name" path.
 * @param Base Path prefix.
 * @param Name Last path component.
 * @param OutPath Output absolute path.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceBuildChildPath(LPCSTR Base, LPCSTR Name, STR OutPath[MAX_PATH_NAME]) {
    U32 Length;

    if (Base == NULL || Name == NULL || OutPath == NULL) return FALSE;

    StringCopy(OutPath, Base);
    Length = StringLength(OutPath);
    if (Length == 0 || OutPath[Length - 1] != PATH_SEP) {
        StringConcat(OutPath, TEXT("/"));
    }
    StringConcat(OutPath, Name);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Mount a filesystem at one absolute SystemFS path.
 * @param FileSystem Filesystem to mount.
 * @param Path Absolute SystemFS mount path.
 * @param SourcePath Optional source folder in mounted filesystem.
 * @return TRUE when mounted or already present.
 */
static BOOL PackageNamespaceMountPath(LPFILESYSTEM FileSystem, LPCSTR Path, LPCSTR SourcePath) {
    FILESYSTEM_MOUNT_CONTROL Control;
    U32 Result;

    if (FileSystem == NULL || Path == NULL || Path[0] != PATH_SEP) return FALSE;
    if (PackageNamespacePathExists(Path)) return TRUE;

    StringCopy(Control.Path, Path);
    Control.Node = (LPLISTNODE)FileSystem;
    if (SourcePath != NULL && SourcePath[0] != STR_NULL) {
        StringCopy(Control.SourcePath, SourcePath);
    } else {
        Control.SourcePath[0] = STR_NULL;
    }

    Result = GetSystemFS()->Driver->Command(DF_FS_MOUNTOBJECT, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Mount failed path=%s status=%u"), Path, Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Unmount a SystemFS object path when present.
 * @param Path Absolute mount path.
 * @return TRUE on success or when already unmounted.
 */
static BOOL PackageNamespaceUnmountPath(LPCSTR Path) {
    FILESYSTEM_UNMOUNT_CONTROL Control;
    U32 Result;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;
    if (!PackageNamespacePathExists(Path)) return TRUE;

    StringCopy(Control.Path, Path);
    Control.Node = NULL;
    Control.SourcePath[0] = STR_NULL;

    Result = GetSystemFS()->Driver->Command(DF_FS_UNMOUNTOBJECT, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Unmount failed path=%s status=%u"), Path, Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return active filesystem object from global file system list.
 * @return Active filesystem pointer or NULL when unavailable.
 */
static LPFILESYSTEM PackageNamespaceGetActiveFileSystem(void) {
    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    LPLIST FileSystemList = GetFileSystemList();
    LPLISTNODE Node;

    if (GlobalInfo == NULL || FileSystemList == NULL) return NULL;
    if (StringEmpty(GlobalInfo->ActivePartitionName)) return NULL;

    for (Node = FileSystemList->First; Node != NULL; Node = Node->Next) {
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        if (FileSystem == GetSystemFS()) continue;
        if (STRINGS_EQUAL(FileSystem->Name, GlobalInfo->ActivePartitionName)) {
            return FileSystem;
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Update "/current-user" alias mount to one concrete user folder.
 * @param UserName User folder name.
 * @return TRUE when alias mount is available.
 */
static BOOL PackageNamespaceBindCurrentUserAlias(LPCSTR UserName) {
    STR SourcePath[MAX_PATH_NAME];
    LPFILESYSTEM ActiveFileSystem;

    if (UserName == NULL || UserName[0] == STR_NULL) return FALSE;
    if (!PackageNamespaceEnsurePathsLoaded()) return FALSE;

    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) return FALSE;

    PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, UserName, SourcePath);
    if (!PackageNamespaceEnsureFolderChain(SourcePath)) return FALSE;

    return PackageNamespaceMountPath(ActiveFileSystem, PackageNamespacePaths.CurrentUserAlias, SourcePath);
}

/***************************************************************************/

/**
 * @brief Initialize package namespace integration.
 * @return TRUE when initialization succeeded.
 */
BOOL PackageNamespaceInitialize(void) {
    LPUSER_ACCOUNT CurrentUser;

    if (!FileSystemReady()) return FALSE;
    if (!PackageNamespaceEnsurePathsLoaded()) return FALSE;

    PackageNamespaceEnsureFolderChain(PackageNamespacePaths.UsersRoot);

    CurrentUser = GetCurrentUser();
    if (CurrentUser != NULL) {
        if (!PackageNamespaceBindCurrentUserAlias(CurrentUser->UserName)) {
            WARNING(TEXT("Cannot bind current-user alias for %s"),
                CurrentUser->UserName);
        }
    } else {
        if (!PackageNamespaceBindCurrentUserAlias(KERNEL_PATH_DEFAULT_ROOT_USER_NAME)) {
            WARNING(TEXT("Cannot bind current-user alias for %s"),
                KERNEL_PATH_DEFAULT_ROOT_USER_NAME);
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Bind package-local process aliases "/package" and "/user-data".
 * @param PackageFileSystem Mounted package filesystem.
 * @param PackageName Package name used for user data path routing.
 * @return TRUE when aliases are mounted.
 */
BOOL PackageNamespaceBindCurrentProcessPackageView(LPFILESYSTEM PackageFileSystem, LPCSTR PackageName) {
    LPUSER_ACCOUNT CurrentUser = GetCurrentUser();
    LPFILESYSTEM ActiveFileSystem = NULL;
    LPCSTR UserName = NULL;
    STR UserDataSourcePath[MAX_PATH_NAME];

    if (PackageFileSystem == NULL || STRING_EMPTY(PackageName)) return FALSE;
    if (!PackageNamespaceEnsurePathsLoaded()) return FALSE;

    if (!PackageNamespaceMountPath(PackageFileSystem, PackageNamespacePaths.PrivatePackageAlias, NULL)) {
        return FALSE;
    }

    if (CurrentUser != NULL) {
        UserName = CurrentUser->UserName;
    } else {
        UserName = KERNEL_PATH_DEFAULT_ROOT_USER_NAME;
    }

    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) {
        WARNING(TEXT("No active filesystem for user-data alias"));
        return TRUE;
    }

    UserDataSourcePath[0] = STR_NULL;
    PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, UserName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, PackageName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, KERNEL_PATH_LEAF_PRIVATE_USER_DATA, UserDataSourcePath);

    if (!PackageNamespaceEnsureFolderChain(UserDataSourcePath)) {
        WARNING(TEXT("Cannot ensure user-data path=%s"),
            UserDataSourcePath);
        return TRUE;
    }

    if (!PackageNamespaceMountPath(ActiveFileSystem, PackageNamespacePaths.PrivateUserDataAlias, UserDataSourcePath)) {
        WARNING(TEXT("Cannot mount user-data alias=%s source=%s"),
            PackageNamespacePaths.PrivateUserDataAlias,
            UserDataSourcePath);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Unbind package-local process aliases "/package" and "/user-data".
 *
 * @details This operation is best-effort cleanup used when one process exits.
 * It unmounts process-local alias nodes when present.
 */
void PackageNamespaceUnbindCurrentProcessPackageView(void) {
    if (!PackageNamespaceEnsurePathsLoaded()) return;

    PackageNamespaceUnmountPath(PackageNamespacePaths.PrivateUserDataAlias);
    PackageNamespaceUnmountPath(PackageNamespacePaths.PrivatePackageAlias);
}

/***************************************************************************/
