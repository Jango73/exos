
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


    System FS

\************************************************************************/

#include "fs/SystemFS.h"

#include "system/Clock.h"
#include "utils/Helpers.h"
#include "core/Kernel.h"
#include "utils/List.h"
#include "log/Log.h"
#include "utils/Path.h"
#include "text/CoreString.h"
#include "utils/TOML.h"
#include "User.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

UINT SystemFSCommands(UINT, UINT);

DRIVER SystemFSDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "Virtual Computer File System",
    .Alias = "system_fs",
    .Command = SystemFSCommands};

/************************************************************************/

/**
 * @brief Allocates and initializes a SystemFS node.
 * @param Name Name to assign to the node, can be NULL for the root.
 * @param Parent Parent directory node, or NULL for the root.
 * @return Pointer to the initialized node, or NULL on allocation failure.
 */
static LPSYSTEMFSFILE NewSystemFile(LPCSTR Name, LPSYSTEMFSFILE Parent) {
    LPSYSTEMFSFILE Node = (LPSYSTEMFSFILE)KernelHeapAlloc(sizeof(SYSTEMFSFILE));
    if (Node == NULL) return NULL;

    *Node = (SYSTEMFSFILE){
        .TypeID = KOID_FILE,
        .References = 1,
        .Next = NULL,
        .Prev = NULL,
        .Children = NewList(NULL, KernelHeapAlloc, KernelHeapFree),
        .ParentNode = Parent,
        .Mounted = NULL,
        .MountPath = {0},
        .Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY,
        .Creation = {0}};

    GetLocalTime(&(Node->Creation));

    if (Name) {
        StringCopy(Node->Name, Name);
    } else {
        Node->Name[0] = STR_NULL;
    }

    return Node;
}

/************************************************************************/

/**
 * @brief Creates the root SystemFS node.
 * @return Pointer to the root node, or NULL on failure.
 */
static LPSYSTEMFSFILE NewSystemFileRoot(void) { return NewSystemFile(TEXT(""), NULL); }

/************************************************************************/

/**
 * @brief Searches for a child with a given name under a parent node.
 * @param Parent Parent directory to search within.
 * @param Name Child name to look for.
 * @return Matching child node or NULL if not found.
 */
static LPSYSTEMFSFILE FindChild(LPSYSTEMFSFILE Parent, LPCSTR Name) {
    LPLISTNODE Node;
    LPSYSTEMFSFILE Child;

    if (Parent == NULL || Parent->Children == NULL) return NULL;

    for (Node = Parent->Children->First; Node; Node = Node->Next) {
        Child = (LPSYSTEMFSFILE)Node;
        if (STRINGS_EQUAL(Child->Name, Name)) return Child;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Resolves a full SystemFS path to its node.
 * @param Path Path to search.
 * @return Located node, or NULL if the path is invalid.
 */
static LPSYSTEMFSFILE FindNode(LPCSTR Path) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATH_NODE Part;
    LPSYSTEMFSFILE Current;

    // SystemFS is now always available as direct object

    Parts = DecomposePath(Path);
    if (Parts == NULL) return NULL;

    Current = GetSystemFSFilesystem()->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATH_NODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        Current = FindChild(Current, Part->Name);
        if (Current == NULL) break;
    }

    DeleteList(Parts);
    return Current;
}

/************************************************************************/

/**
 * @brief Detects whether mounting a filesystem would create a circular mount.
 * @param Node Node where the mount is being attached.
 * @param FilesystemToMount Filesystem being mounted.
 * @return TRUE if a circular mount is detected, FALSE otherwise.
 */
static BOOL IsCircularMount(LPSYSTEMFSFILE Node, LPFILESYSTEM FilesystemToMount) {
    LPSYSTEMFSFILE Current = Node;

    if (FilesystemToMount == NULL) return FALSE;

    // Walk up the parent hierarchy to check for circular mounts
    while (Current != NULL) {
        if (Current->Mounted == FilesystemToMount) {
            // Found the same filesystem already mounted in a parent
            return TRUE;
        }
        Current = Current->ParentNode;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Mounts a filesystem object into SystemFS.
 * @param Control Mount parameters including target path and filesystem node.
 * @return DF_RETURN_SUCCESS on success, an error code otherwise.
 */
static U32 MountObject(LPFILESYSTEM_MOUNT_CONTROL Control) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATH_NODE Part = NULL;
    LPSYSTEMFSFILE Parent;
    LPSYSTEMFSFILE Child;

    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    Parts = DecomposePath(Control->Path);
    if (Parts == NULL) return DF_RETURN_BAD_PARAMETER;

    Parent = GetSystemFSFilesystem()->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATH_NODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        if (Node->Next == NULL) break;
        Child = FindChild(Parent, Part->Name);
        if (Child == NULL) {
            Child = NewSystemFile(Part->Name, Parent);
            if (Child == NULL) {
                DeleteList(Parts);
                return DF_RETURN_GENERIC;
            }
            ListAddTail(Parent->Children, Child);
        }
        Parent = Child;
    }

    if (Part == NULL || Control->Node == NULL) {
        DeleteList(Parts);
        return DF_RETURN_BAD_PARAMETER;
    }

    if (FindChild(Parent, Part->Name)) {
        DeleteList(Parts);
        return DF_RETURN_GENERIC;
    }

    Child = NewSystemFile(Part->Name, Parent);
    if (Child == NULL) {
        DeleteList(Parts);
        return DF_RETURN_GENERIC;
    }

    // Check for circular mount before assigning the filesystem
    if (IsCircularMount(Parent, (LPFILESYSTEM)Control->Node)) {
        KernelHeapFree(Child);
        DeleteList(Parts);
        return DF_RETURN_GENERIC;
    }

    Child->Mounted = (LPFILESYSTEM)Control->Node;
    if (Control->SourcePath[0] != STR_NULL) {
        StringCopy(Child->MountPath, Control->SourcePath);
    } else {
        Child->MountPath[0] = STR_NULL;
    }
    ListAddTail(Parent->Children, Child);

    DeleteList(Parts);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unmounts a filesystem object from SystemFS.
 * @param Control Unmount parameters containing the target path.
 * @return DF_RETURN_SUCCESS on success, an error code otherwise.
 */
static U32 UnmountObject(LPFILESYSTEM_UNMOUNT_CONTROL Control) {
    LPSYSTEMFSFILE Node;

    if (Control == NULL) return DF_RETURN_BAD_PARAMETER;

    Node = FindNode(Control->Path);
    if (Node == NULL || Node->ParentNode == NULL) return DF_RETURN_GENERIC;

    ListErase(Node->ParentNode->Children, Node);
    KernelHeapFree(Node);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Resolves a path to a SystemFS node and extracts the remaining subpath.
 * @param Path Input path to resolve.
 * @param Node Output pointer receiving the resolved SystemFS node.
 * @param Remaining Output buffer receiving the unresolved tail for mounted filesystems.
 * @return TRUE on success, FALSE if the path cannot be resolved.
 */
static BOOL ResolvePath(LPCSTR Path, LPSYSTEMFSFILE *Node, STR Remaining[MAX_PATH_NAME]) {
    LPLIST Parts;
    LPLISTNODE It;
    LPPATH_NODE Part;
    LPSYSTEMFSFILE Current;

    if (Path == NULL || Node == NULL || Remaining == NULL) return FALSE;

    Parts = DecomposePath(Path);
    if (Parts == NULL) return FALSE;

    Current = GetSystemFSFilesystem()->Root;
    Remaining[0] = STR_NULL;

    for (It = Parts->First; It; It = It->Next) {
        Part = (LPPATH_NODE)It;

        if (Part->Name[0] == STR_NULL || StringCompare(Part->Name, TEXT(".")) == 0) {
            continue;
        }

        if (StringCompare(Part->Name, TEXT("..")) == 0) {
            if (Current && Current->ParentNode) Current = Current->ParentNode;
            continue;
        }

        LPSYSTEMFSFILE Child = FindChild(Current, Part->Name);
        if (Child == NULL) {
            if (Current->Mounted) {
                STR Sep[2] = {PATH_SEP, STR_NULL};

                // Build remaining path with MountPath prefix
                if (Current->MountPath[0] != STR_NULL) {
                    StringCopy(Remaining, Current->MountPath);
                    if (Remaining[StringLength(Remaining) - 1] != PATH_SEP) {
                        StringConcat(Remaining, Sep);
                    }
                } else {
                    Remaining[0] = STR_NULL;
                }

                for (; It; It = It->Next) {
                    Part = (LPPATH_NODE)It;
                    if (Part->Name[0] == STR_NULL) continue;
                    StringConcat(Remaining, Part->Name);
                    if (It->Next) StringConcat(Remaining, Sep);
                }
                *Node = Current;
                DeleteList(Parts);
                return TRUE;
            }
            DeleteList(Parts);
            return FALSE;
        }

        Current = Child;
    }

    *Node = Current;
    DeleteList(Parts);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Wraps a file from a mounted filesystem into a SYSFSFILE object.
 * @param Parent SystemFS parent node that holds the mounted filesystem.
 * @param Mounted File object returned by the mounted filesystem.
 * @return Wrapped SYSFSFILE, or NULL on failure.
 */
static LPSYSFSFILE WrapMountedFile(LPSYSTEMFSFILE Parent, LPFILE Mounted, U32 OpenFlags) {
    LPSYSFSFILE File;

    if (Mounted == NULL) return NULL;

    File = (LPSYSFSFILE)KernelHeapAlloc(sizeof(SYSFSFILE));
    if (File == NULL) return NULL;

    Mounted->OpenFlags = OpenFlags;

    *File = (SYSFSFILE){0};
    File->Header.TypeID = KOID_FILE;
    File->Header.References = 1;
    File->Header.FileSystem = GetSystemFS();
    File->Parent = Parent;
    File->MountedFile = Mounted;
    StringCopy(File->Header.Name, Mounted->Name);
    File->Header.Attributes = Mounted->Attributes;
    File->Header.SizeLow = Mounted->SizeLow;
    File->Header.SizeHigh = Mounted->SizeHigh;
    File->Header.Creation = Mounted->Creation;
    File->Header.Accessed = Mounted->Accessed;
    File->Header.Modified = Mounted->Modified;

    return File;
}

/************************************************************************/

/**
 * @brief Checks if a SystemFS path exists, following into mounted filesystems.
 * @param Control Path check parameters including current and target folders.
 * @return TRUE if the path exists, FALSE otherwise.
 */
static BOOL PathExists(LPFILESYSTEM_PATHCHECK Control) {
    STR Temp[MAX_PATH_NAME];
    STR Remaining[MAX_PATH_NAME];
    LPSYSTEMFSFILE Node;
    FILE_INFO Info;
    LPFILE Mounted;
    BOOL Result = FALSE;

    if (Control == NULL) return FALSE;

    if (Control->SubFolder[0] == PATH_SEP) {
        StringCopy(Temp, Control->SubFolder);
    } else {
        StringCopy(Temp, Control->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP) StringConcat(Temp, TEXT("/"));
        StringConcat(Temp, Control->SubFolder);
    }

    if (!ResolvePath(Temp, &Node, Remaining)) return FALSE;

    if (Remaining[0] == STR_NULL) return TRUE;

    if (Node->Mounted == NULL) return FALSE;

    Info.Size = sizeof(FILE_INFO);
    Info.FileSystem = Node->Mounted;
    Info.Attributes = MAX_U32;
    Info.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    StringCopy(Info.Name, Remaining);

    Mounted = (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (UINT)&Info);
    if (Mounted == NULL) return FALSE;

    Result = (Mounted->Attributes & FS_ATTR_FOLDER) != 0;
    Node->Mounted->Driver->Command(DF_FS_CLOSEFILE, (UINT)Mounted);

    return Result;
}

/***************************************************************************/

/**
 * @brief Creates a SystemFS folder structure for the provided path.
 * @param Info File information containing the folder path.
 * @return DF_RETURN_SUCCESS on success, an error code otherwise.
 */
static U32 CreateFolder(LPFILE_INFO Info) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATH_NODE Part = NULL;
    LPSYSTEMFSFILE Parent;
    LPSYSTEMFSFILE Child;
    LPSYSTEMFSFILE MountedNode;
    STR Remaining[MAX_PATH_NAME];
    FILE_INFO Local;
    UINT Result;

    if (Info == NULL) return DF_RETURN_BAD_PARAMETER;

    if (ResolvePath(Info->Name, &MountedNode, Remaining)) {
        if (Remaining[0] != STR_NULL) {
            if (MountedNode != NULL && MountedNode->Mounted != NULL) {
                Local = *Info;
                Local.FileSystem = MountedNode->Mounted;
                StringCopy(Local.Name, Remaining);
                Result = MountedNode->Mounted->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&Local);
                return Result;
            }
            return DF_RETURN_GENERIC;
        }
        return DF_RETURN_GENERIC;
    }

    Parts = DecomposePath(Info->Name);
    if (Parts == NULL) return DF_RETURN_BAD_PARAMETER;

    Parent = GetSystemFSFilesystem()->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATH_NODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        if (Node->Next == NULL) break;
        Child = FindChild(Parent, Part->Name);
        if (Child == NULL) {
            Child = NewSystemFile(Part->Name, Parent);
            if (Child == NULL) {
                DeleteList(Parts);
                return DF_RETURN_GENERIC;
            }
            ListAddTail(Parent->Children, Child);
        }
        Parent = Child;
    }

    if (Part == NULL) {
        DeleteList(Parts);
        return DF_RETURN_BAD_PARAMETER;
    }

    if (FindChild(Parent, Part->Name)) {
        DeleteList(Parts);
        return DF_RETURN_GENERIC;
    }

    Child = NewSystemFile(Part->Name, Parent);
    if (Child == NULL) {
        DeleteList(Parts);
        return DF_RETURN_GENERIC;
    }

    ListAddTail(Parent->Children, Child);
    DeleteList(Parts);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Deletes an empty SystemFS folder.
 * @param Info File information specifying the folder path.
 * @return DF_RETURN_SUCCESS on success, an error code otherwise.
 */
static U32 DeleteFolder(LPFILE_INFO Info) {
    LPSYSTEMFSFILE Node;

    if (Info == NULL) return DF_RETURN_BAD_PARAMETER;

    Node = FindNode(Info->Name);
    if (Node == NULL || Node->ParentNode == NULL) return DF_RETURN_GENERIC;
    if (Node->Children && Node->Children->NumItems) return DF_RETURN_GENERIC;

    ListErase(Node->ParentNode->Children, Node);
    KernelHeapFree(Node);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Mounts a filesystem specified in configuration into SystemFS.
 * @param FileSystem Name of the filesystem to mount.
 * @param Path Target path inside SystemFS.
 * @param SourcePath Optional source path within the filesystem.
 */
static void MountConfiguredFileSystem(LPCSTR FileSystem, LPCSTR Path, LPCSTR SourcePath) {
    LPLISTNODE Node;
    LPFILESYSTEM FS;
    FILESYSTEM_MOUNT_CONTROL Control;
    FILE_INFO Info;
    LPFILE TestFile;
    BOOL FileSystemFound = FALSE;
    LPLIST FileSystemList = GetFileSystemList();
    LPSYSTEMFSFILESYSTEM SystemFS = GetSystemFSData();
    const STR ActiveLabel[] = {'a', 'c', 't', 'i', 'v', 'e', STR_NULL};
    LPCSTR EffectiveFileSystem = FileSystem;

    if (FileSystem == NULL || Path == NULL) return;

    if (STRINGS_EQUAL_NO_CASE(FileSystem, ActiveLabel)) {
        FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
        if (GlobalInfo == NULL || StringEmpty(GlobalInfo->ActivePartitionName)) {
            ERROR(TEXT("Active filesystem not set"));
            return;
        }
        EffectiveFileSystem = GlobalInfo->ActivePartitionName;
    }

    for (Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node; Node = Node->Next) {
        FS = (LPFILESYSTEM)Node;
        if (FS == &SystemFS->Header) continue;
        if (STRINGS_EQUAL(FS->Name, EffectiveFileSystem)) {
            FileSystemFound = TRUE;

            // Check if SourcePath exists in the filesystem
            if (SourcePath && SourcePath[0] != STR_NULL) {
                Info.Size = sizeof(FILE_INFO);
                Info.FileSystem = FS;
                Info.Attributes = FS_ATTR_FOLDER;
                Info.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
                StringCopy(Info.Name, SourcePath);

                TestFile = (LPFILE)FS->Driver->Command(DF_FS_OPENFILE, (UINT)&Info);
                if (TestFile == NULL) {
                    ERROR(TEXT("Source path '%s' does not exist in filesystem '%s'"), SourcePath, FileSystem);
                    return;
                }
                FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)TestFile);
            }

            StringCopy(Control.Path, Path);
            Control.Node = (LPLISTNODE)FS;
            if (SourcePath) {
                StringCopy(Control.SourcePath, SourcePath);
            } else {
                Control.SourcePath[0] = STR_NULL;
            }
            MountObject(&Control);
            break;
        }
    }

    if (!FileSystemFound) {
        ERROR(TEXT("FileSystem '%s' not found"), EffectiveFileSystem);
    }
}

/************************************************************************/

/**
 * @brief Mount a filesystem into SystemFS when the root is available.
 * @param FileSystem Filesystem to mount.
 * @return TRUE on success or when already mounted, FALSE otherwise.
 */
BOOL SystemFSMountFileSystem(LPFILESYSTEM FileSystem) {
    FILESYSTEM_MOUNT_CONTROL Control;
    FILESYSTEM_PATHCHECK Check;
    VOLUME_INFO Volume;
    STR Path[MAX_PATH_NAME];
    const STR FsRoot[] = {PATH_SEP, 'f', 's', STR_NULL};
    LPSYSTEMFSFILESYSTEM SystemFS = GetSystemFSData();
    U32 Result;
    U32 Length;

    if (FileSystem == NULL) return FALSE;
    if (SystemFS == NULL || SystemFS->Root == NULL) return FALSE;
    if (FileSystem == &SystemFS->Header) return TRUE;

    Volume.Size = sizeof(VOLUME_INFO);
    Volume.Volume = (HANDLE)FileSystem;
    Volume.Name[0] = STR_NULL;
    Result = FileSystem->Driver->Command(DF_FS_GETVOLUME_INFO, (UINT)&Volume);
    if (Result != DF_RETURN_SUCCESS || Volume.Name[0] == STR_NULL) {
        StringCopy(Volume.Name, FileSystem->Name);
    }

    StringCopy(Path, FsRoot);
    Length = StringLength(Path);
    Path[Length] = PATH_SEP;
    Path[Length + 1] = STR_NULL;
    StringConcat(Path, Volume.Name);

    StringCopy(Check.CurrentFolder, TEXT("/"));
    StringCopy(Check.SubFolder, Path);
    if (GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Check)) {
        return TRUE;
    }

    StringCopy(Control.Path, Path);
    Control.Node = (LPLISTNODE)FileSystem;
    Control.SourcePath[0] = STR_NULL;
    Result = GetSystemFS()->Driver->Command(DF_FS_MOUNTOBJECT, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Mount failed for %s (result=%x)"), Volume.Name, Result);
    }

    return Result == DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unmount a filesystem from SystemFS when the root is available.
 * @param FileSystem Filesystem to unmount.
 * @return TRUE on success or when already unmounted, FALSE otherwise.
 */
BOOL SystemFSUnmountFileSystem(LPFILESYSTEM FileSystem) {
    FILESYSTEM_UNMOUNT_CONTROL Control;
    FILESYSTEM_PATHCHECK Check;
    VOLUME_INFO Volume;
    STR Path[MAX_PATH_NAME];
    const STR FsRoot[] = {PATH_SEP, 'f', 's', STR_NULL};
    LPSYSTEMFSFILESYSTEM SystemFS = GetSystemFSData();
    U32 Result;
    U32 Length;

    if (FileSystem == NULL) return FALSE;
    if (SystemFS == NULL || SystemFS->Root == NULL) return FALSE;
    if (FileSystem == &SystemFS->Header) return TRUE;

    Volume.Size = sizeof(VOLUME_INFO);
    Volume.Volume = (HANDLE)FileSystem;
    Volume.Name[0] = STR_NULL;
    Result = FileSystem->Driver->Command(DF_FS_GETVOLUME_INFO, (UINT)&Volume);
    if (Result != DF_RETURN_SUCCESS || Volume.Name[0] == STR_NULL) {
        StringCopy(Volume.Name, FileSystem->Name);
    }

    StringCopy(Path, FsRoot);
    Length = StringLength(Path);
    Path[Length] = PATH_SEP;
    Path[Length + 1] = STR_NULL;
    StringConcat(Path, Volume.Name);

    StringCopy(Check.CurrentFolder, TEXT("/"));
    StringCopy(Check.SubFolder, Path);
    if (!GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Check)) {
        return TRUE;
    }

    StringCopy(Control.Path, Path);
    Control.Node = (LPLISTNODE)FileSystem;
    Control.SourcePath[0] = STR_NULL;
    Result = GetSystemFS()->Driver->Command(DF_FS_UNMOUNTOBJECT, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("Unmount failed for %s (result=%x)"), Volume.Name, Result);
    }

    return Result == DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Initializes and mounts the SystemFS root and known filesystems.
 * @return TRUE on successful mount, FALSE otherwise.
 */
BOOL MountSystemFS(void) {
    LPLISTNODE Node;
    LPFILESYSTEM FS;
    FILE_INFO Info;
    const STR FsRoot[] = {PATH_SEP, 'f', 's', STR_NULL};
    LPSYSTEMFSFILESYSTEM SystemFS = GetSystemFSData();
    LPLIST FileSystemList = GetFileSystemList();


    SystemFS->Root = NewSystemFileRoot();
    if (SystemFS->Root == NULL) return FALSE;

    InitMutex(&(SystemFS->Header.Mutex));
    SystemFS->Header.Mounted = TRUE;
    SystemFS->Header.Driver = &SystemFSDriver;
    SystemFS->Header.StorageUnit = NULL;
    SystemFS->Header.Partition.Scheme = PARTITION_SCHEME_VIRTUAL;
    SystemFS->Header.Partition.Type = FSID_NONE;
    SystemFS->Header.Partition.Format = PARTITION_FORMAT_UNKNOWN;
    SystemFS->Header.Partition.Index = 0;
    SystemFS->Header.Partition.Flags = 0;
    SystemFS->Header.Partition.StartSector = 0;
    SystemFS->Header.Partition.NumSectors = 0;
    MemorySet(SystemFS->Header.Partition.TypeGuid, 0, GPT_GUID_LENGTH);

    Info.Size = sizeof(FILE_INFO);
    Info.FileSystem = &SystemFS->Header;
    Info.Attributes = 0;
    Info.Flags = 0;
    StringCopy(Info.Name, FsRoot);
    CreateFolder(&Info);

    for (Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node; Node = Node->Next) {
        FS = (LPFILESYSTEM)Node;
        if (FS == &SystemFS->Header) continue;
        if (!SystemFSMountFileSystem(FS)) {
            WARNING(TEXT("Unable to mount FileSystem %s"), FS->Name);
        }
    }

    ListAddItem(FileSystemList, GetSystemFS());

    return TRUE;
}

/************************************************************************/

/**
 * @brief Performs SystemFS driver initialization.
 * @return DF_RETURN_SUCCESS always.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Opens a file or directory within SystemFS, delegating to mounts when needed.
 * @param Find File information describing the target path and flags.
 * @return Wrapped SYSFSFILE handle, or NULL on failure.
 */
static LPSYSFSFILE OpenFile(LPFILE_INFO Find) {
    STR Path[MAX_PATH_NAME];
    STR Remaining[MAX_PATH_NAME];
    LPSYSTEMFSFILE Node;
    FILE_INFO Local;
    LPFILE Mounted;
    BOOL Wildcard = FALSE;

    if (Find == NULL) return NULL;

    StringCopy(Path, Find->Name);
    {
        U32 Len = StringLength(Path);
        if (Len > 0 && Path[Len - 1] == '*') {
            Wildcard = TRUE;
            Path[Len - 1] = STR_NULL;
            if (Len > 1 && Path[Len - 2] == PATH_SEP) Path[Len - 2] = STR_NULL;
        }
    }

    if (!ResolvePath(Path, &Node, Remaining)) {
        WARNING(TEXT("ResolvePath failed path=%s"), Path);
        return NULL;
    }


    if (Remaining[0] != STR_NULL) {
        if (Node->Mounted == NULL) {
            WARNING(TEXT("No mount for path=%s remaining=%s"), Path, Remaining);
            return NULL;
        }

        Local = *Find;
        Local.FileSystem = Node->Mounted;
        StringCopy(Local.Name, Remaining);
        if (Wildcard) {
            if (Local.Name[StringLength(Local.Name) - 1] != PATH_SEP) StringConcat(Local.Name, TEXT("/"));
            StringConcat(Local.Name, TEXT("*"));
        }

        Mounted = (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (UINT)&Local);
        if (Mounted == NULL) {
            WARNING(TEXT("Mounted open failed path=%s local=%s wildcard=%u"),
                Path,
                Local.Name,
                Wildcard ? 1 : 0);
        }
        return WrapMountedFile(Node, Mounted, Find->Flags);
    }

    if (Wildcard) {
        if (Node->Mounted) {
            Local = *Find;
            Local.FileSystem = Node->Mounted;

            // Request listing of the mounted filesystem path
            if (Node->MountPath[0] != STR_NULL) {
                StringCopy(Local.Name, Node->MountPath);
                if (Local.Name[StringLength(Local.Name) - 1] != PATH_SEP) {
                    StringConcat(Local.Name, TEXT("/"));
                }
                StringConcat(Local.Name, TEXT("*"));
            } else {
                StringCopy(Local.Name, TEXT("*"));
            }
            Mounted = (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (UINT)&Local);
            if (Mounted == NULL) {
                WARNING(TEXT("Mounted wildcard open failed path=%s local=%s"),
                    Path,
                    Local.Name);
            }
            return WrapMountedFile(Node, Mounted, Find->Flags);
        } else {
            LPSYSTEMFSFILE Child = (Node->Children) ? (LPSYSTEMFSFILE)Node->Children->First : NULL;
            if (Child == NULL) {
                WARNING(TEXT("No children for wildcard path=%s"), Path);
                return NULL;
            }

            LPSYSFSFILE File = (LPSYSFSFILE)KernelHeapAlloc(sizeof(SYSFSFILE));
            if (File == NULL) {
                ERROR(TEXT("Allocation failed for SYSFSFILE"));
                return NULL;
            }

            *File = (SYSFSFILE){0};
            File->Header.TypeID = KOID_FILE;
            File->Header.References = 1;
            File->Header.FileSystem = GetSystemFS();
            File->Parent = Node;
            File->MountedFile = NULL;
            StringCopy(File->Header.Name, Child->Name);
            File->Header.Attributes = Child->Attributes;
            File->Header.Creation = Child->Creation;
            File->SystemFile = (LPSYSTEMFSFILE)Child->Next;
            return File;
        }
    }

    if (Node->Mounted) {
        Local = *Find;
        Local.FileSystem = Node->Mounted;
        // Open the mounted path (subdirectory or root)
        if (Node->MountPath[0] != STR_NULL) {
            StringCopy(Local.Name, Node->MountPath);
        } else {
            Local.Name[0] = STR_NULL;
        }
        Mounted = (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (UINT)&Local);
        if (Mounted == NULL) {
            WARNING(TEXT("Mounted direct open failed path=%s local=%s"),
                Path,
                Local.Name[0] != STR_NULL ? Local.Name : TEXT("<empty>"));
        }
        return WrapMountedFile(Node, Mounted, Find->Flags);
    }

    {
        LPSYSFSFILE File = (LPSYSFSFILE)KernelHeapAlloc(sizeof(SYSFSFILE));
        if (File == NULL) {
            ERROR(TEXT("Allocation failed for SYSFSFILE"));
            return NULL;
        }

        *File = (SYSFSFILE){0};
        File->Header.TypeID = KOID_FILE;
        File->Header.References = 1;
        File->Header.FileSystem = GetSystemFS();
        File->SystemFile = (Node->Children) ? (LPSYSTEMFSFILE)Node->Children->First : NULL;
        File->Parent = Node->ParentNode;
        StringCopy(File->Header.Name, Node->Name);
        File->Header.Attributes = Node->Attributes;
        File->Header.Creation = Node->Creation;
        return File;
    }
}

/************************************************************************/

/**
 * @brief Retrieves the next directory entry for an open SystemFS enumeration.
 * @param File SYSFSFILE used for iteration.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 OpenNext(LPSYSFSFILE File) {
    LPFILESYSTEM FS;
    U32 Result;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;

    if (File->MountedFile) {
        FS = File->Parent ? File->Parent->Mounted : NULL;
        if (FS == NULL) return DF_RETURN_GENERIC;
        Result = FS->Driver->Command(DF_FS_OPENNEXT, (UINT)File->MountedFile);
        if (Result != DF_RETURN_SUCCESS) return Result;
        StringCopy(File->Header.Name, File->MountedFile->Name);
        File->Header.Attributes = File->MountedFile->Attributes;
        File->Header.SizeLow = File->MountedFile->SizeLow;
        File->Header.SizeHigh = File->MountedFile->SizeHigh;
        File->Header.Creation = File->MountedFile->Creation;
        File->Header.Accessed = File->MountedFile->Accessed;
        File->Header.Modified = File->MountedFile->Modified;
        return DF_RETURN_SUCCESS;
    }

    if (File->SystemFile == NULL) return DF_RETURN_GENERIC;

    // Return current entry then move to the next one
    StringCopy(File->Header.Name, File->SystemFile->Name);
    File->Header.Attributes = File->SystemFile->Attributes;
    File->Header.Creation = File->SystemFile->Creation;
    File->SystemFile = (LPSYSTEMFSFILE)File->SystemFile->Next;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Closes a SystemFS file or directory handle.
 * @param File File handle to close.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static U32 CloseFile(LPSYSFSFILE File) {
    if (File == NULL) return DF_RETURN_BAD_PARAMETER;

    if (File->MountedFile && File->Parent && File->Parent->Mounted) {
        File->Parent->Mounted->Driver->Command(DF_FS_CLOSEFILE, (UINT)File->MountedFile);
    }

    ReleaseKernelObject(File);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reads from a mounted file through SystemFS.
 * @param File File handle containing buffer and position information.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 ReadFile(LPSYSFSFILE File) {
    SAFE_USE(File) {
        LPFILESYSTEM FS;
        LPFILE Mounted;
        U32 Result;

        FS = (File->Parent) ? File->Parent->Mounted : NULL;
        Mounted = File->MountedFile;

        if (FS == NULL || Mounted == NULL) return DF_RETURN_NOT_IMPLEMENTED;

        Mounted->Buffer = File->Header.Buffer;
        Mounted->ByteCount = File->Header.ByteCount;
        Mounted->Position = File->Header.Position;

        Result = FS->Driver->Command(DF_FS_READ, (UINT)Mounted);

        File->Header.BytesTransferred = Mounted->BytesTransferred;
        File->Header.Position = Mounted->Position;

        return Result;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Writes to a mounted file through SystemFS.
 * @param File File handle containing buffer and position information.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 WriteFile(LPSYSFSFILE File) {
    SAFE_USE(File) {
        LPFILESYSTEM FS;
        LPFILE Mounted;
        U32 Result;

        FS = (File->Parent) ? File->Parent->Mounted : NULL;
        Mounted = File->MountedFile;

        if (FS == NULL || Mounted == NULL) return DF_RETURN_NOT_IMPLEMENTED;

        Mounted->Buffer = File->Header.Buffer;
        Mounted->ByteCount = File->Header.ByteCount;
        Mounted->Position = File->Header.Position;

        Result = FS->Driver->Command(DF_FS_WRITE, (UINT)Mounted);

        File->Header.BytesTransferred = Mounted->BytesTransferred;
        File->Header.Position = Mounted->Position;

        return Result;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Mounts user-specified filesystems from configuration into SystemFS.
 * @return TRUE if configuration is present and processed, FALSE otherwise.
 */
BOOL MountUserNodes(void) {
    LPTOML Configuration = GetConfiguration();

    if (Configuration) {
        U32 ConfigIndex = 0;

        FOREVER {
            STR Key[0x100];
            STR IndexText[0x10];
            LPCSTR FsName;
            LPCSTR MountPath;
            LPCSTR SourcePath;

            U32ToString(ConfigIndex, IndexText);

            StringCopy(Key, TEXT("SystemFS.Mount."));
            StringConcat(Key, IndexText);
            StringConcat(Key, TEXT(".FileSystem"));
            FsName = TomlGet(Configuration, Key);
            if (FsName == NULL) break;

            StringCopy(Key, TEXT("SystemFS.Mount."));
            StringConcat(Key, IndexText);
            StringConcat(Key, TEXT(".Path"));
            MountPath = TomlGet(Configuration, Key);

            StringCopy(Key, TEXT("SystemFS.Mount."));
            StringConcat(Key, IndexText);
            StringConcat(Key, TEXT(".SourcePath"));
            SourcePath = TomlGet(Configuration, Key);

            if (MountPath) {
                MountConfiguredFileSystem(FsName, MountPath, SourcePath);
            }

            ConfigIndex++;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Checks if a file exists within SystemFS or mounted filesystems.
 * @param Info File information containing the path to test.
 * @return TRUE if the file exists, FALSE otherwise.
 */
static BOOL FileExists(LPFILE_INFO Info) {
    LPSYSFSFILE File;

    SAFE_USE(Info) {

        File = OpenFile(Info);
        if (File == NULL) return FALSE;

        CloseFile(File);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief SystemFS driver command dispatcher.
 * @param Function Driver function code.
 * @param Parameter Optional parameter for the function.
 * @return Command-specific error code or pointer cast to UINT.
 */
UINT SystemFSCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUME_INFO: {
            LPVOLUME_INFO Info = (LPVOLUME_INFO)Parameter;
            if (Info && Info->Size == sizeof(VOLUME_INFO)) {
                StringCopy(Info->Name, TEXT("/"));
                return DF_RETURN_SUCCESS;
            }
            return DF_RETURN_BAD_PARAMETER;
        }
        case DF_FS_SETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_CREATEFOLDER:
            return CreateFolder((LPFILE_INFO)Parameter);
        case DF_FS_DELETEFOLDER:
            return DeleteFolder((LPFILE_INFO)Parameter);
        case DF_FS_MOUNTOBJECT:
            return MountObject((LPFILESYSTEM_MOUNT_CONTROL)Parameter);
        case DF_FS_UNMOUNTOBJECT:
            return UnmountObject((LPFILESYSTEM_UNMOUNT_CONTROL)Parameter);
        case DF_FS_PATHEXISTS:
            return (UINT)PathExists((LPFILESYSTEM_PATHCHECK)Parameter);
        case DF_FS_FILEEXISTS:
            return (UINT)FileExists((LPFILE_INFO)Parameter);
        case DF_FS_OPENFILE:
            return (UINT)OpenFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENNEXT:
            return (UINT)OpenNext((LPSYSFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (UINT)CloseFile((LPSYSFSFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_READ:
            return (UINT)ReadFile((LPSYSFSFILE)Parameter);
        case DF_FS_WRITE:
            return (UINT)WriteFile((LPSYSFSFILE)Parameter);
        case DF_FS_GETPOSITION:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_SETPOSITION:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_GETATTRIBUTES:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_SETATTRIBUTES:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
