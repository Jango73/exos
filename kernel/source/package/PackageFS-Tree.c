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


    PackageFS tree and path resolution implementation

\************************************************************************/

#include "PackageFS-Internal.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"

/************************************************************************/

/**
 * @brief Decode packed DATETIME value stored in EPK entries.
 * @param Packed Packed U64 date/time.
 * @param OutTime Destination DATETIME.
 */
static void PackageFSDecodeDateTime(U64 Packed, LPDATETIME OutTime) {
    U32 Low;
    U32 High;

    if (OutTime == NULL) {
        return;
    }

    Low = U64_Low32(Packed);
    High = U64_High32(Packed);

    MemorySet(OutTime, 0, sizeof(DATETIME));

    OutTime->Year = Low & 0x03FFFFFF;
    OutTime->Month = (Low >> 26) & 0x0F;
    OutTime->Day = ((Low >> 30) & 0x03) | ((High & 0x0F) << 2);
    OutTime->Hour = (High >> 4) & 0x3F;
    OutTime->Minute = (High >> 10) & 0x3F;
    OutTime->Second = (High >> 16) & 0x3F;
    OutTime->Milli = (High >> 22) & 0x03FF;
}

/************************************************************************/

/**
 * @brief Allocate a new PackageFS tree node.
 * @param Name Node short name.
 * @param Parent Parent node or NULL for root.
 * @return Newly allocated node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSCreateNode(LPCSTR Name, LPPACKAGEFS_NODE Parent) {
    LPPACKAGEFS_NODE Node;

    Node = (LPPACKAGEFS_NODE)KernelHeapAlloc(sizeof(PACKAGEFS_NODE));
    if (Node == NULL) {
        return NULL;
    }

    MemorySet(Node, 0, sizeof(PACKAGEFS_NODE));
    Node->ParentNode = Parent;
    Node->NodeType = PACKAGEFS_NODE_TYPE_ROOT;
    Node->TocIndex = MAX_U32;
    Node->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;

    if (Name != NULL) {
        StringCopy(Node->Name, Name);
    }

    return Node;
}

/************************************************************************/

/**
 * @brief Release a node tree recursively.
 * @param Node Root node to release.
 */
void PackageFSReleaseNodeTree(LPPACKAGEFS_NODE Node) {
    LPPACKAGEFS_NODE Child;

    if (Node == NULL) {
        return;
    }

    Child = Node->FirstChild;
    while (Child != NULL) {
        LPPACKAGEFS_NODE Next = Child->NextSibling;
        PackageFSReleaseNodeTree(Child);
        Child = Next;
    }

    KernelHeapFree(Node);
}

/************************************************************************/

/**
 * @brief Find a direct child by name.
 * @param Parent Parent folder node.
 * @param Name Child name to find.
 * @return Matching child or NULL.
 */
static LPPACKAGEFS_NODE PackageFSFindChild(LPPACKAGEFS_NODE Parent, LPCSTR Name) {
    LPPACKAGEFS_NODE Child;

    if (Parent == NULL || Name == NULL) {
        return NULL;
    }

    Child = Parent->FirstChild;
    while (Child != NULL) {
        if (StringCompare(Child->Name, Name) == 0) {
            return Child;
        }
        Child = Child->NextSibling;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Append a child node to a parent.
 * @param Parent Parent folder node.
 * @param Child Child node to append.
 */
static void PackageFSAddChild(LPPACKAGEFS_NODE Parent, LPPACKAGEFS_NODE Child) {
    LPPACKAGEFS_NODE Cursor;

    if (Parent == NULL || Child == NULL) {
        return;
    }

    if (Parent->FirstChild == NULL) {
        Parent->FirstChild = Child;
        return;
    }

    Cursor = Parent->FirstChild;
    while (Cursor->NextSibling != NULL) {
        Cursor = Cursor->NextSibling;
    }
    Cursor->NextSibling = Child;
}

/************************************************************************/

/**
 * @brief Convert package node type/permissions to generic FS attributes.
 * @param Entry Parsed TOC entry.
 * @return Generic attribute flags.
 */
static U32 PackageFSBuildAttributes(const EPK_PARSED_TOC_ENTRY* Entry) {
    U32 Attributes = FS_ATTR_READONLY;

    if (Entry == NULL) {
        return Attributes;
    }

    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER || Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        Attributes |= FS_ATTR_FOLDER;
    }

    if ((Entry->Permissions & 0x49) != 0) {
        Attributes |= FS_ATTR_EXECUTABLE;
    }

    return Attributes;
}

/************************************************************************/

/**
 * @brief Read one path component from a path string.
 * @param Path Source path.
 * @param Cursor In/out cursor index.
 * @param Component Destination component buffer.
 * @return TRUE when a component is produced, FALSE at end or error.
 */
static BOOL PackageFSNextPathComponent(LPCSTR Path, U32* Cursor, STR Component[MAX_FILE_NAME]) {
    U32 Index = 0;
    U32 Position;

    if (Path == NULL || Cursor == NULL || Component == NULL) {
        return FALSE;
    }

    Position = *Cursor;
    while (Path[Position] == PATH_SEP) {
        Position++;
    }

    if (Path[Position] == STR_NULL) {
        *Cursor = Position;
        return FALSE;
    }

    while (Path[Position] != STR_NULL && Path[Position] != PATH_SEP) {
        if (Index + 1 >= MAX_FILE_NAME) {
            return FALSE;
        }
        Component[Index++] = Path[Position++];
    }

    Component[Index] = STR_NULL;
    *Cursor = Position;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Insert or update one TOC entry into the in-memory tree.
 * @param FileSystem PackageFS instance.
 * @param TocIndex TOC entry index.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSInsertTocEntry(LPPACKAGEFSFILESYSTEM FileSystem, U32 TocIndex) {
    const EPK_PARSED_TOC_ENTRY* Entry;
    const U8* PackageBytes;
    STR FullPath[MAX_PATH_NAME];
    STR AliasTarget[MAX_PATH_NAME];
    U32 PathCursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current;
    LPPACKAGEFS_NODE Node = NULL;
    BOOL HasComponent = FALSE;

    if (FileSystem == NULL || FileSystem->Root == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (TocIndex >= FileSystem->Package.TocEntryCount) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Entry = &FileSystem->Package.TocEntries[TocIndex];
    PackageBytes = FileSystem->Package.PackageBytes;

    if (Entry->PathLength == 0 || Entry->PathLength >= MAX_PATH_NAME) {
        return DF_RETURN_BAD_PARAMETER;
    }

    MemoryCopy(FullPath, PackageBytes + Entry->PathOffset, Entry->PathLength);
    FullPath[Entry->PathLength] = STR_NULL;

    AliasTarget[0] = STR_NULL;
    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        if (Entry->AliasTargetLength == 0 || Entry->AliasTargetLength >= MAX_PATH_NAME) {
            return DF_RETURN_BAD_PARAMETER;
        }
        MemoryCopy(AliasTarget, PackageBytes + Entry->AliasTargetOffset, Entry->AliasTargetLength);
        AliasTarget[Entry->AliasTargetLength] = STR_NULL;
    }

    Current = FileSystem->Root;
    while (PackageFSNextPathComponent(FullPath, &PathCursor, Component)) {
        LPPACKAGEFS_NODE Existing = PackageFSFindChild(Current, Component);

        HasComponent = TRUE;
        if (Existing == NULL) {
            Existing = PackageFSCreateNode(Component, Current);
            if (Existing == NULL) {
                return DF_RETURN_NO_MEMORY;
            }
            PackageFSAddChild(Current, Existing);
        }

        Current = Existing;
        Node = Existing;

        while (FullPath[PathCursor] == PATH_SEP) {
            PathCursor++;
        }
    }

    if (!HasComponent || Node == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Node->Defined == TRUE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Node->Defined = TRUE;
    Node->NodeType = Entry->NodeType;
    Node->TocIndex = TocIndex;
    Node->Attributes = PackageFSBuildAttributes(Entry);
    PackageFSDecodeDateTime(Entry->ModifiedTime, &Node->Modified);
    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        StringCopy(Node->AliasTarget, AliasTarget);
    } else {
        Node->AliasTarget[0] = STR_NULL;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Ensure implicit folders are explicit folder nodes.
 * @param Node Node tree root.
 * @return TRUE when tree is valid.
 */
static BOOL PackageFSFinalizeImplicitFolders(LPPACKAGEFS_NODE Node) {
    LPPACKAGEFS_NODE Child;

    if (Node == NULL) {
        return FALSE;
    }

    if (Node->Defined == FALSE && Node->ParentNode != NULL) {
        Node->NodeType = EPK_NODE_TYPE_FOLDER;
        Node->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;
        Node->TocIndex = MAX_U32;
    }

    Child = Node->FirstChild;
    while (Child != NULL) {
        if (!PackageFSFinalizeImplicitFolders(Child)) {
            return FALSE;
        }
        Child = Child->NextSibling;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build full tree from validated TOC entries.
 * @param FileSystem Target PackageFS instance.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSBuildTree(LPPACKAGEFSFILESYSTEM FileSystem) {
    U32 TocIndex;

    if (FileSystem == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    FileSystem->Root = PackageFSCreateNode(TEXT(""), NULL);
    if (FileSystem->Root == NULL) {
        return DF_RETURN_NO_MEMORY;
    }

    FileSystem->Root->Defined = TRUE;
    FileSystem->Root->NodeType = PACKAGEFS_NODE_TYPE_ROOT;
    FileSystem->Root->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;
    FileSystem->Root->TocIndex = MAX_U32;

    for (TocIndex = 0; TocIndex < FileSystem->Package.TocEntryCount; TocIndex++) {
        U32 Result = PackageFSInsertTocEntry(FileSystem, TocIndex);
        if (Result != DF_RETURN_SUCCESS) {
            return Result;
        }
    }

    if (!PackageFSFinalizeImplicitFolders(FileSystem->Root)) {
        return DF_RETURN_GENERIC;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Resolve an internal path to a node without alias expansion.
 * @param Root Root node.
 * @param Path Internal package path.
 * @return Located node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSResolveInternalPath(LPPACKAGEFS_NODE Root, LPCSTR Path) {
    U32 Cursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current = Root;

    if (Root == NULL || Path == NULL) {
        return NULL;
    }

    while (PackageFSNextPathComponent(Path, &Cursor, Component)) {
        if (StringCompare(Component, TEXT(".")) == 0) {
            continue;
        }
        if (StringCompare(Component, TEXT("..")) == 0) {
            if (Current->ParentNode != NULL) {
                Current = Current->ParentNode;
            }
            continue;
        }

        Current = PackageFSFindChild(Current, Component);
        if (Current == NULL) {
            return NULL;
        }
    }

    return Current;
}

/************************************************************************/

/**
 * @brief Resolve alias node target with recursion guard.
 * @param FileSystem PackageFS instance.
 * @param Node Alias node.
 * @param Depth Current alias depth.
 * @return Resolved node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSResolveAliasTarget(LPPACKAGEFSFILESYSTEM FileSystem,
                                                    LPPACKAGEFS_NODE Node,
                                                    U32 Depth) {
    LPPACKAGEFS_NODE Target;
    LPCSTR Path = NULL;

    if (FileSystem == NULL || Node == NULL) {
        return NULL;
    }

    if (Depth >= PACKAGEFS_ALIAS_MAX_DEPTH) {
        WARNING(TEXT("Alias depth exceeded"));
        return NULL;
    }

    Path = Node->AliasTarget;
    if (Path == NULL || Path[0] == STR_NULL) {
        return NULL;
    }

    while (*Path == PATH_SEP) {
        Path++;
    }

    Target = PackageFSResolveInternalPath(FileSystem->Root, Path);
    if (Target == NULL) {
        WARNING(TEXT("Alias target not found path=%s"), Node->AliasTarget);
        return NULL;
    }

    if (Target->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        return PackageFSResolveAliasTarget(FileSystem, Target, Depth + 1);
    }

    if ((Target->Attributes & FS_ATTR_FOLDER) == 0) {
        WARNING(TEXT("Alias target is not folder path=%s"), Node->AliasTarget);
        return NULL;
    }

    return Target;
}

/************************************************************************/

/**
 * @brief Resolve external path and optionally expand final alias.
 * @param FileSystem PackageFS instance.
 * @param Path Path to resolve.
 * @param FollowFinalAlias TRUE to resolve trailing alias.
 * @return Located node or NULL.
 */
LPPACKAGEFS_NODE PackageFSResolvePath(LPPACKAGEFSFILESYSTEM FileSystem,
                                      LPCSTR Path,
                                      BOOL FollowFinalAlias) {
    U32 Cursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current;
    BOOL HasAny = FALSE;

    if (FileSystem == NULL || FileSystem->Root == NULL || Path == NULL) {
        return NULL;
    }

    Current = FileSystem->Root;
    while (PackageFSNextPathComponent(Path, &Cursor, Component)) {
        HasAny = TRUE;

        if (StringCompare(Component, TEXT(".")) == 0) {
            continue;
        }
        if (StringCompare(Component, TEXT("..")) == 0) {
            if (Current->ParentNode != NULL) {
                Current = Current->ParentNode;
            }
            continue;
        }

        Current = PackageFSFindChild(Current, Component);
        if (Current == NULL) {
            return NULL;
        }

        while (Path[Cursor] == PATH_SEP) {
            Cursor++;
        }

        if (Current->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS &&
            (Path[Cursor] != STR_NULL || FollowFinalAlias == TRUE)) {
            Current = PackageFSResolveAliasTarget(FileSystem, Current, 0);
            if (Current == NULL) {
                return NULL;
            }
        }
    }

    if (!HasAny) {
        return FileSystem->Root;
    }

    return Current;
}
