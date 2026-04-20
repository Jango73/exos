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


    NTFS path lookup helpers

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

/**
 * @brief Convert one Unicode code point to lowercase for ASCII letters.
 *
 * @param CodePoint Input Unicode code point.
 * @return Lowercase code point for A..Z, unchanged otherwise.
 */
static U32 NtfsAsciiToLowerCodePoint(U32 CodePoint) {
    if (CodePoint >= 'A' && CodePoint <= 'Z') {
        return CodePoint + ('a' - 'A');
    }

    return CodePoint;
}

/***************************************************************************/

/**
 * @brief Decode next UTF-8 code point from one null-terminated string.
 *
 * Invalid sequences are consumed as one byte and mapped to '?'.
 *
 * @param Text UTF-8 string.
 * @param Cursor In/out byte cursor.
 * @param CodePointOut Output decoded code point.
 * @return TRUE while a code point can be produced, FALSE at end of string.
 */
static BOOL NtfsUtf8NextCodePoint(LPCSTR Text, U32* Cursor, U32* CodePointOut) {
    U8 Byte0;

    if (CodePointOut != NULL) *CodePointOut = 0;
    if (Text == NULL || Cursor == NULL || CodePointOut == NULL) return FALSE;

    Byte0 = (U8)Text[*Cursor];
    if (Byte0 == 0) return FALSE;

    if (Byte0 < 0x80) {
        *CodePointOut = Byte0;
        (*Cursor)++;
        return TRUE;
    }

    if ((Byte0 & 0xE0) == 0xC0) {
        U8 Byte1 = (U8)Text[*Cursor + 1];
        if ((Byte1 & 0xC0) == 0x80) {
            *CodePointOut = ((U32)(Byte0 & 0x1F) << 6) | (U32)(Byte1 & 0x3F);
            *Cursor += 2;
            return TRUE;
        }
    } else if ((Byte0 & 0xF0) == 0xE0) {
        U8 Byte1 = (U8)Text[*Cursor + 1];
        U8 Byte2 = (U8)Text[*Cursor + 2];
        if ((Byte1 & 0xC0) == 0x80 && (Byte2 & 0xC0) == 0x80) {
            *CodePointOut = ((U32)(Byte0 & 0x0F) << 12) |
                ((U32)(Byte1 & 0x3F) << 6) |
                (U32)(Byte2 & 0x3F);
            *Cursor += 3;
            return TRUE;
        }
    } else if ((Byte0 & 0xF8) == 0xF0) {
        U8 Byte1 = (U8)Text[*Cursor + 1];
        U8 Byte2 = (U8)Text[*Cursor + 2];
        U8 Byte3 = (U8)Text[*Cursor + 3];
        if ((Byte1 & 0xC0) == 0x80 && (Byte2 & 0xC0) == 0x80 && (Byte3 & 0xC0) == 0x80) {
            *CodePointOut = ((U32)(Byte0 & 0x07) << 18) |
                ((U32)(Byte1 & 0x3F) << 12) |
                ((U32)(Byte2 & 0x3F) << 6) |
                (U32)(Byte3 & 0x3F);
            *Cursor += 4;
            return TRUE;
        }
    }

    *CodePointOut = '?';
    (*Cursor)++;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compare two UTF-8 names with ASCII-insensitive behavior.
 *
 * ASCII letters are case-folded first. Non-ASCII code points are compared
 * as decoded Unicode values.
 *
 * @param Left Left UTF-8 string.
 * @param Right Right UTF-8 string.
 * @return TRUE when names match, FALSE otherwise.
 */
static BOOL NtfsCompareNameCaseInsensitive(LPCSTR Left, LPCSTR Right) {
    U32 LeftCursor;
    U32 RightCursor;

    if (Left == NULL || Right == NULL) return FALSE;

    LeftCursor = 0;
    RightCursor = 0;
    while (TRUE) {
        U32 LeftCodePoint;
        U32 RightCodePoint;
        BOOL HasLeft;
        BOOL HasRight;

        HasLeft = NtfsUtf8NextCodePoint(Left, &LeftCursor, &LeftCodePoint);
        HasRight = NtfsUtf8NextCodePoint(Right, &RightCursor, &RightCodePoint);
        if (!HasLeft || !HasRight) {
            return HasLeft == HasRight;
        }

        if (NtfsAsciiToLowerCodePoint(LeftCodePoint) != NtfsAsciiToLowerCodePoint(RightCodePoint)) {
            return FALSE;
        }
    }
}

/***************************************************************************/

/**
 * @brief Parse next path component from a path string.
 *
 * Leading and repeated separators '/' and '\' are skipped.
 *
 * @param Path Path string.
 * @param Cursor In/out byte cursor in Path.
 * @param ComponentOut Output component buffer.
 * @param ComponentSize Size of ComponentOut in bytes.
 * @return TRUE when a component is produced, FALSE when no more components exist.
 */
static BOOL NtfsReadNextPathComponent(
    LPCSTR Path, U32* Cursor, LPSTR ComponentOut, U32 ComponentSize) {
    U32 Index;
    U32 WriteIndex;

    if (Path == NULL || Cursor == NULL || ComponentOut == NULL || ComponentSize == 0) return FALSE;

    Index = *Cursor;
    while (Path[Index] == '/' || Path[Index] == '\\') {
        Index++;
    }

    if (Path[Index] == STR_NULL) {
        *Cursor = Index;
        return FALSE;
    }

    WriteIndex = 0;
    while (Path[Index] != STR_NULL && Path[Index] != '/' && Path[Index] != '\\') {
        if (WriteIndex + 1 >= ComponentSize) {
            WARNING(TEXT("Path component too long"));
            return FALSE;
        }

        ComponentOut[WriteIndex] = Path[Index];
        WriteIndex++;
        Index++;
    }

    ComponentOut[WriteIndex] = STR_NULL;
    *Cursor = Index;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Try one path-lookup cache hit for a folder component.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param ParentFolderIndex Parent folder file-record index.
 * @param Name Component name.
 * @param ChildFileRecordIndexOut Output child file-record index.
 * @param ChildIsFolderOut Output folder flag.
 * @return TRUE on cache hit, FALSE otherwise.
 */
static BOOL NtfsLookupPathCache(
    LPNTFSFILESYSTEM FileSystem,
    U32 ParentFolderIndex,
    LPCSTR Name,
    U32* ChildFileRecordIndexOut,
    BOOL* ChildIsFolderOut) {
    U32 Index;

    if (ChildFileRecordIndexOut != NULL) *ChildFileRecordIndexOut = 0;
    if (ChildIsFolderOut != NULL) *ChildIsFolderOut = FALSE;
    if (FileSystem == NULL || Name == NULL || ChildFileRecordIndexOut == NULL || ChildIsFolderOut == NULL) {
        return FALSE;
    }

    for (Index = 0; Index < NTFS_PATH_LOOKUP_CACHE_SIZE; Index++) {
        LPNTFS_PATH_LOOKUP_CACHE_ENTRY Entry = &(FileSystem->PathLookupCache[Index]);
        if (!Entry->IsValid) continue;
        if (Entry->ParentFolderIndex != ParentFolderIndex) continue;
        if (!NtfsCompareNameCaseInsensitive(Entry->Name, Name)) continue;

        *ChildFileRecordIndexOut = Entry->ChildFileRecordIndex;
        *ChildIsFolderOut = Entry->ChildIsFolder;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Insert one path-lookup cache entry.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param ParentFolderIndex Parent folder file-record index.
 * @param Name Component name.
 * @param ChildFileRecordIndex Child file-record index.
 * @param ChildIsFolder Child folder flag.
 */
static void NtfsStorePathCache(
    LPNTFSFILESYSTEM FileSystem,
    U32 ParentFolderIndex,
    LPCSTR Name,
    U32 ChildFileRecordIndex,
    BOOL ChildIsFolder) {
    U32 SlotIndex;
    LPNTFS_PATH_LOOKUP_CACHE_ENTRY Entry;

    if (FileSystem == NULL || Name == NULL) return;

    SlotIndex = FileSystem->PathLookupCacheNextSlot % NTFS_PATH_LOOKUP_CACHE_SIZE;
    Entry = &(FileSystem->PathLookupCache[SlotIndex]);

    Entry->IsValid = TRUE;
    Entry->ParentFolderIndex = ParentFolderIndex;
    Entry->ChildFileRecordIndex = ChildFileRecordIndex;
    Entry->ChildIsFolder = ChildIsFolder;
    StringCopyLimit(Entry->Name, Name, sizeof(Entry->Name));

    FileSystem->PathLookupCacheNextSlot++;
}

/***************************************************************************/

/**
 * @brief Resolve one child component name inside one folder record index.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param ParentFolderIndex Parent folder file-record index.
 * @param Name Path component to match.
 * @param ChildFileRecordIndexOut Output child file-record index.
 * @param ChildIsFolderOut Output folder/file flag.
 * @return TRUE on success, FALSE when not found or on malformed index metadata.
 */
static BOOL NtfsLookupChildByName(
    LPNTFSFILESYSTEM FileSystem,
    U32 ParentFolderIndex,
    LPCSTR Name,
    U32* ChildFileRecordIndexOut,
    BOOL* ChildIsFolderOut) {
    LPNTFS_FOLDER_ENTRY_INFO Entries;
    U32 StartEntryIndex;
    U32 StoredEntries;
    BOOL Found;

    if (ChildFileRecordIndexOut != NULL) *ChildFileRecordIndexOut = 0;
    if (ChildIsFolderOut != NULL) *ChildIsFolderOut = FALSE;
    if (FileSystem == NULL || Name == NULL || ChildFileRecordIndexOut == NULL || ChildIsFolderOut == NULL) {
        return FALSE;
    }

    LockMutex(&(FileSystem->Header.Mutex), INFINITY);
    Found = NtfsLookupPathCache(FileSystem, ParentFolderIndex, Name, ChildFileRecordIndexOut, ChildIsFolderOut);
    UnlockMutex(&(FileSystem->Header.Mutex));
    if (Found) {
        return TRUE;
    }

    Entries = (LPNTFS_FOLDER_ENTRY_INFO)KernelHeapAlloc(NTFS_ENUMERATION_WINDOW_SIZE * sizeof(NTFS_FOLDER_ENTRY_INFO));
    if (Entries == NULL) {
        ERROR(TEXT("Unable to allocate folder entry window"));
        return FALSE;
    }

    Found = FALSE;
    StartEntryIndex = 0;

    while (TRUE) {
        U32 EntryIndex;

        StoredEntries = 0;
        if (!NtfsEnumerateFolderByIndexWindow(
                (LPFILESYSTEM)FileSystem,
                ParentFolderIndex,
                StartEntryIndex,
                Entries,
                NTFS_ENUMERATION_WINDOW_SIZE,
                &StoredEntries)) {
            WARNING(TEXT("Unable to enumerate parent=%u name=%s start=%u"),
                ParentFolderIndex,
                Name,
                StartEntryIndex);
            KernelHeapFree(Entries);
            return FALSE;
        }

        if (StoredEntries == 0) {
            break;
        }

        for (EntryIndex = 0; EntryIndex < StoredEntries; EntryIndex++) {
            LPNTFS_FOLDER_ENTRY_INFO Entry = Entries + EntryIndex;
            if (!NtfsCompareNameCaseInsensitive(Entry->Name, Name)) continue;

            *ChildFileRecordIndexOut = Entry->FileRecordIndex;
            *ChildIsFolderOut = Entry->IsFolder;
            Found = TRUE;
            break;
        }

        if (Found) {
            break;
        }

        StartEntryIndex += StoredEntries;
        if (StoredEntries < NTFS_ENUMERATION_WINDOW_SIZE) {
            break;
        }
    }

    if (Found) {
        LockMutex(&(FileSystem->Header.Mutex), INFINITY);
        NtfsStorePathCache(
            FileSystem,
            ParentFolderIndex,
            Name,
            *ChildFileRecordIndexOut,
            *ChildIsFolderOut);
        UnlockMutex(&(FileSystem->Header.Mutex));
    }
    if (!Found) {
        WARNING(TEXT("Entry not found parent=%u name=%s entries=%u"),
            ParentFolderIndex,
            Name,
            StoredEntries);
    }

    KernelHeapFree(Entries);
    return Found;
}

/***************************************************************************/

/**
 * @brief Resolve one NTFS path to a file-record index.
 *
 * Path separators '\' and '/' are both accepted.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Path NTFS path to resolve.
 * @param IndexOut Output resolved file-record index.
 * @param IsFolderOut Optional output folder/file flag for resolved record.
 * @return TRUE on success, FALSE when one component is missing or invalid.
 */
BOOL NtfsResolvePathToIndex(
    LPFILESYSTEM FileSystem, LPCSTR Path, U32* IndexOut, BOOL* IsFolderOut) {
    LPNTFSFILESYSTEM NtfsFileSystem;
    U32 Cursor;
    U32 CurrentIndex;
    BOOL CurrentIsFolder;
    STR Component[MAX_FILE_NAME];
    BOOL HasComponent;

    if (IndexOut != NULL) *IndexOut = 0;
    if (IsFolderOut != NULL) *IsFolderOut = FALSE;
    if (FileSystem == NULL || Path == NULL || IndexOut == NULL) return FALSE;

    NtfsFileSystem = NULL;
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        if (FileSystem->Driver != &NTFSDriver) return FALSE;
        NtfsFileSystem = (LPNTFSFILESYSTEM)FileSystem;
    }
    if (NtfsFileSystem == NULL) return FALSE;

    Cursor = 0;
    CurrentIndex = NTFS_ROOT_FILE_RECORD_INDEX;
    CurrentIsFolder = TRUE;

    HasComponent = NtfsReadNextPathComponent(Path, &Cursor, Component, sizeof(Component));
    while (HasComponent) {
        U32 ChildIndex;
        BOOL ChildIsFolder;

        if (!CurrentIsFolder) {
            WARNING(TEXT("Path walks through non-folder node index=%u"), CurrentIndex);
            return FALSE;
        }

        if (!NtfsLookupChildByName(NtfsFileSystem, CurrentIndex, Component, &ChildIndex, &ChildIsFolder)) {
            WARNING(TEXT("Component lookup failed component=%s parent=%u path=%s"),
                Component,
                CurrentIndex,
                Path);
            return FALSE;
        }

        CurrentIndex = ChildIndex;
        CurrentIsFolder = ChildIsFolder;
        HasComponent = NtfsReadNextPathComponent(Path, &Cursor, Component, sizeof(Component));
    }

    *IndexOut = CurrentIndex;
    if (IsFolderOut != NULL) *IsFolderOut = CurrentIsFolder;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Read default DATA stream using a path lookup.
 *
 * @param FileSystem Mounted NTFS file system.
 * @param Path NTFS path to file.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination buffer size in bytes.
 * @param BytesReadOut Optional output for number of bytes copied to Buffer.
 * @return TRUE on success, FALSE on lookup or read failure.
 */
BOOL NtfsReadFileDataByPath(
    LPFILESYSTEM FileSystem, LPCSTR Path, LPVOID Buffer, U32 BufferSize, U32* BytesReadOut) {
    U32 FileIndex;
    BOOL IsFolder;

    if (BytesReadOut != NULL) *BytesReadOut = 0;
    if (FileSystem == NULL || Path == NULL || Buffer == NULL) return FALSE;

    if (!NtfsResolvePathToIndex(FileSystem, Path, &FileIndex, &IsFolder)) {
        return FALSE;
    }
    if (IsFolder) {
        return FALSE;
    }

    return NtfsReadFileDataByIndex(FileSystem, FileIndex, Buffer, BufferSize, BytesReadOut);
}

/***************************************************************************/
