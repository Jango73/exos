/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    NTFS VFS integration (read-only)

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

/**
 * @brief Returns TRUE when a path contains wildcard characters.
 *
 * @param Path Input path.
 * @return TRUE when '*' or '?' is present, FALSE otherwise.
 */
static BOOL NtfsHasWildcard(LPCSTR Path) {
    if (Path == NULL) return FALSE;

    while (*Path != STR_NULL) {
        if (*Path == '*' || *Path == '?') return TRUE;
        Path++;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief ASCII case-insensitive character compare.
 *
 * @param Left Left character.
 * @param Right Right character.
 * @return TRUE when equal ignoring ASCII case.
 */
static BOOL NtfsMatchCharIgnoreCase(STR Left, STR Right) {
    if (Left >= 'A' && Left <= 'Z') Left = Left + ('a' - 'A');
    if (Right >= 'A' && Right <= 'Z') Right = Right + ('a' - 'A');

    return Left == Right;
}

/***************************************************************************/

/**
 * @brief Wildcard matcher for file names.
 *
 * Supports '*' and '?' with ASCII case-insensitive matching.
 *
 * @param Name Candidate name.
 * @param Pattern Wildcard pattern.
 * @return TRUE when Name matches Pattern.
 */
static BOOL NtfsMatchPattern(LPCSTR Name, LPCSTR Pattern) {
    if (Pattern == NULL) return FALSE;
    if (Name == NULL) return FALSE;

    if (Pattern[0] == STR_NULL) return Name[0] == STR_NULL;

    if (Pattern[0] == '*') {
        while (*Pattern == '*') Pattern++;
        if (*Pattern == STR_NULL) return TRUE;

        while (*Name != STR_NULL) {
            if (NtfsMatchPattern(Name, Pattern)) return TRUE;
            Name++;
        }

        return NtfsMatchPattern(Name, Pattern);
    }

    if (Pattern[0] == '?') {
        if (Name[0] == STR_NULL) return FALSE;
        return NtfsMatchPattern(Name + 1, Pattern + 1);
    }

    if (!NtfsMatchCharIgnoreCase(Name[0], Pattern[0])) return FALSE;

    return NtfsMatchPattern(Name + 1, Pattern + 1);
}

/***************************************************************************/

/**
 * @brief Extract the last component from a path.
 *
 * @param Path Input path.
 * @param NameOut Output component buffer.
 */
static void NtfsExtractBaseName(LPCSTR Path, LPSTR NameOut) {
    LPCSTR Base;

    if (NameOut == NULL) return;

    NameOut[0] = STR_NULL;
    if (Path == NULL || Path[0] == STR_NULL) return;

    Base = Path;
    while (*Path != STR_NULL) {
        if (*Path == PATH_SEP || *Path == '\\') {
            Base = Path + 1;
        }
        Path++;
    }

    StringCopy(NameOut, Base);
}

/***************************************************************************/

/**
 * @brief Split a wildcard path into folder path and pattern.
 *
 * @param Path Full wildcard path.
 * @param FolderPathOut Output folder path.
 * @param PatternOut Output wildcard pattern.
 */
static void NtfsSplitWildcardPath(LPCSTR Path, LPSTR FolderPathOut, LPSTR PatternOut) {
    U32 LastSeparator;
    U32 Index;

    if (FolderPathOut != NULL) FolderPathOut[0] = STR_NULL;
    if (PatternOut != NULL) PatternOut[0] = STR_NULL;
    if (Path == NULL || FolderPathOut == NULL || PatternOut == NULL) return;

    LastSeparator = MAX_U32;
    for (Index = 0; Path[Index] != STR_NULL; Index++) {
        if (Path[Index] == PATH_SEP || Path[Index] == '\\') {
            LastSeparator = Index;
        }
    }

    if (LastSeparator == MAX_U32) {
        StringCopy(PatternOut, Path);
        return;
    }

    if (LastSeparator > 0) {
        MemoryCopy(FolderPathOut, Path, LastSeparator);
    }
    FolderPathOut[LastSeparator] = STR_NULL;

    if (Path[LastSeparator + 1] != STR_NULL) {
        StringCopy(PatternOut, Path + LastSeparator + 1);
    } else {
        StringCopy(PatternOut, TEXT("*"));
    }
}

/***************************************************************************/

/**
 * @brief Fill generic FILE metadata from NTFS record metadata.
 *
 * @param File Target file handle.
 * @param Name Preferred file name.
 * @param RecordInfo NTFS record metadata.
 */
static void NtfsFillFileHeader(
    LPNTFSFILE File,
    LPCSTR Name,
    LPNTFS_FILE_RECORD_INFO RecordInfo) {
    if (File == NULL || RecordInfo == NULL) return;

    if (Name != NULL && Name[0] != STR_NULL) {
        StringCopy(File->Header.Name, Name);
    } else if (RecordInfo->HasPrimaryFileName) {
        StringCopy(File->Header.Name, RecordInfo->PrimaryFileName);
    } else {
        File->Header.Name[0] = STR_NULL;
    }

    File->Header.Attributes = FS_ATTR_READONLY;
    if ((RecordInfo->Flags & NTFS_FR_FLAG_FOLDER) != 0) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if (RecordInfo->HasDataAttribute) {
        File->Header.SizeLow = U64_Low32(RecordInfo->DataSize);
        File->Header.SizeHigh = U64_High32(RecordInfo->DataSize);
    } else {
        File->Header.SizeLow = 0;
        File->Header.SizeHigh = 0;
    }

    File->Header.Creation = RecordInfo->CreationTime;
    File->Header.Accessed = RecordInfo->LastAccessTime;
    File->Header.Modified = RecordInfo->LastModificationTime;
}

/***************************************************************************/

/**
 * @brief Fill current enumeration entry metadata in one NTFS file handle.
 *
 * @param File NTFS file enumeration handle.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL NtfsLoadCurrentEnumerationEntry(LPNTFSFILE File) {
    NTFS_FILE_RECORD_INFO RecordInfo;

    if (File == NULL) return FALSE;
    if (!File->Enumerate) return FALSE;
    if (File->EnumerationEntries == NULL) return FALSE;

    while (TRUE) {
        while (File->EnumerationIndex < File->EnumerationCount) {
            LPNTFS_FOLDER_ENTRY_INFO Entry = File->EnumerationEntries + File->EnumerationIndex;
            File->EnumerationIndex++;

            if (!NtfsMatchPattern(Entry->Name, File->EnumerationPattern)) {
                continue;
            }

            MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
            if (!NtfsReadFileRecord(File->Header.FileSystem, Entry->FileRecordIndex, &RecordInfo)) {
                continue;
            }

            NtfsFillFileHeader(File, Entry->Name, &RecordInfo);
            File->FileRecordIndex = Entry->FileRecordIndex;
            File->IsFolder = (RecordInfo.Flags & NTFS_FR_FLAG_FOLDER) != 0;
            return TRUE;
        }

        if (File->EnumerationReachedEnd) {
            return FALSE;
        }

        File->EnumerationCount = 0;
        File->EnumerationIndex = 0;
        if (!NtfsEnumerateFolderByIndexWindow(
                File->Header.FileSystem,
                File->EnumerationFolderIndex,
                File->EnumerationNextStartIndex,
                File->EnumerationEntries,
                NTFS_ENUMERATION_WINDOW_SIZE,
                &File->EnumerationCount)) {
            return FALSE;
        }

        File->EnumerationNextStartIndex += File->EnumerationCount;
        if (File->EnumerationCount < NTFS_ENUMERATION_WINDOW_SIZE) {
            File->EnumerationReachedEnd = TRUE;
        }
        if (File->EnumerationCount == 0) {
            return FALSE;
        }
    }
}

/***************************************************************************/

/**
 * @brief Allocate and initialize one NTFS file handle.
 *
 * @param FileSystem Target NTFS file system.
 * @return New file handle or NULL on allocation failure.
 */
static LPNTFSFILE NtfsCreateFileHandle(LPFILESYSTEM FileSystem) {
    LPNTFSFILE File;

    if (FileSystem == NULL) return NULL;

    File = (LPNTFSFILE)CreateKernelObject(sizeof(NTFS_FILE), KOID_FILE);
    if (File == NULL) return NULL;

    InitMutex(&(File->Header.Mutex));
    InitSecurity(&(File->Header.Security));

    File->Header.FileSystem = FileSystem;
    File->Header.OwnerTask = GetCurrentTask();
    File->Header.OpenFlags = 0;
    File->Header.Attributes = FS_ATTR_READONLY;
    File->Header.SizeLow = 0;
    File->Header.SizeHigh = 0;
    File->Header.Position = 0;
    File->Header.ByteCount = 0;
    File->Header.BytesTransferred = 0;
    File->Header.Buffer = NULL;
    File->Header.Name[0] = STR_NULL;

    File->FileRecordIndex = 0;
    File->IsFolder = FALSE;
    File->Enumerate = FALSE;
    File->EnumerationFolderIndex = 0;
    File->EnumerationNextStartIndex = 0;
    File->EnumerationReachedEnd = FALSE;
    File->EnumerationPattern[0] = STR_NULL;
    File->EnumerationIndex = 0;
    File->EnumerationCount = 0;
    File->EnumerationEntries = NULL;

    return File;
}

/***************************************************************************/

/**
 * @brief Open a file or folder on NTFS through VFS.
 *
 * @param Info VFS open parameters.
 * @return Open file handle or NULL on failure.
 */
LPFILE NtfsOpenFile(LPFILE_INFO Info) {
    LPNTFSFILE File;
    NTFS_FILE_RECORD_INFO RecordInfo;
    U32 FileRecordIndex;
    BOOL IsFolder;

    if (Info == NULL) return NULL;
    if (Info->FileSystem == NULL) return NULL;
    if (Info->Name[0] == STR_NULL) return NULL;

    if (Info->Flags & (FILE_OPEN_WRITE | FILE_OPEN_APPEND | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE)) {
        return NULL;
    }

    if (NtfsHasWildcard(Info->Name)) {
        STR FolderPath[MAX_PATH_NAME];
        STR Pattern[MAX_FILE_NAME];
        U32 FolderIndex;
        BOOL FolderIsFolder;

        NtfsSplitWildcardPath(Info->Name, FolderPath, Pattern);
        if (!NtfsResolvePathToIndex(Info->FileSystem, FolderPath, &FolderIndex, &FolderIsFolder)) {
            return NULL;
        }
        if (!FolderIsFolder) return NULL;

        File = NtfsCreateFileHandle(Info->FileSystem);
        if (File == NULL) {
            return NULL;
        }

        File->EnumerationEntries =
            (LPNTFS_FOLDER_ENTRY_INFO)KernelHeapAlloc(NTFS_ENUMERATION_WINDOW_SIZE * sizeof(NTFS_FOLDER_ENTRY_INFO));
        if (File->EnumerationEntries == NULL) {
            ReleaseKernelObject(File);
            return NULL;
        }

        File->Header.OpenFlags = Info->Flags;
        File->IsFolder = TRUE;
        File->Enumerate = TRUE;
        File->EnumerationFolderIndex = FolderIndex;
        File->EnumerationNextStartIndex = 0;
        File->EnumerationReachedEnd = FALSE;
        StringCopy(File->EnumerationPattern, Pattern);
        File->EnumerationIndex = 0;
        File->EnumerationCount = 0;

        if (!NtfsLoadCurrentEnumerationEntry(File)) {
            KernelHeapFree(File->EnumerationEntries);
            File->EnumerationEntries = NULL;
            ReleaseKernelObject(File);
            return NULL;
        }

        return (LPFILE)File;
    }

    if (!NtfsResolvePathToIndex(Info->FileSystem, Info->Name, &FileRecordIndex, &IsFolder)) {
        return NULL;
    }

    MemorySet(&RecordInfo, 0, sizeof(NTFS_FILE_RECORD_INFO));
    if (!NtfsReadFileRecord(Info->FileSystem, FileRecordIndex, &RecordInfo)) {
        return NULL;
    }

    File = NtfsCreateFileHandle(Info->FileSystem);
    if (File == NULL) return NULL;

    File->Header.OpenFlags = Info->Flags;
    File->FileRecordIndex = FileRecordIndex;
    File->IsFolder = IsFolder;

    {
        STR BaseName[MAX_FILE_NAME];

        NtfsExtractBaseName(Info->Name, BaseName);
        NtfsFillFileHeader(File, BaseName, &RecordInfo);
    }

    if ((Info->Flags & FILE_OPEN_APPEND) != 0) {
#ifdef __EXOS_64__
        File->Header.Position = U64_Make(File->Header.SizeHigh, File->Header.SizeLow);
#else
        File->Header.Position = File->Header.SizeLow;
#endif
    } else {
        File->Header.Position = 0;
    }

    return (LPFILE)File;
}

/***************************************************************************/

/**
 * @brief Move to the next folder entry in an NTFS enumeration.
 *
 * @param File Open NTFS file handle.
 * @return DF_RETURN_SUCCESS when an entry is loaded.
 */
U32 NtfsOpenNext(LPNTFSFILE File) {
    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    if (!File->Enumerate) return DF_RETURN_GENERIC;
    if (File->EnumerationEntries == NULL) return DF_RETURN_GENERIC;

    if (!NtfsLoadCurrentEnumerationEntry(File)) {
        return DF_RETURN_GENERIC;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Close an NTFS file handle.
 *
 * @param File Open NTFS file handle.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 NtfsCloseFile(LPNTFSFILE File) {
    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    if (File->EnumerationEntries != NULL) {
        KernelHeapFree(File->EnumerationEntries);
        File->EnumerationEntries = NULL;
    }

    ReleaseKernelObject(File);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read from an NTFS file handle.
 *
 * @param File Open NTFS file handle.
 * @return DF_RETURN_SUCCESS on success or an error status.
 */
U32 NtfsReadFile(LPNTFSFILE File) {
    U64 Position64;
    U64 FileSize64;
    U64 Remaining64;
    U32 ReadSize;
    U32 BytesRead;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    if (File->IsFolder) {
        return DF_RETURN_GENERIC;
    }

#ifdef __EXOS_64__
    Position64 = U64_FromUINT(File->Header.Position);
#else
    Position64 = U64_FromU32(File->Header.Position);
#endif
    FileSize64 = U64_Make(File->Header.SizeHigh, File->Header.SizeLow);

    File->Header.BytesTransferred = 0;
    if (U64_Cmp(Position64, FileSize64) >= 0) {
        return DF_RETURN_SUCCESS;
    }

    Remaining64 = U64_Sub(FileSize64, Position64);
    ReadSize = File->Header.ByteCount;
    if (U64_High32(Remaining64) == 0 && U64_Low32(Remaining64) < ReadSize) {
        ReadSize = U64_Low32(Remaining64);
    }

    if (ReadSize == 0) {
        return DF_RETURN_SUCCESS;
    }

    if (!NtfsReadFileDataRangeByIndex(
            File->Header.FileSystem,
            File->FileRecordIndex,
            Position64,
            File->Header.Buffer,
            ReadSize,
            &BytesRead)) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    File->Header.BytesTransferred = BytesRead;
    File->Header.Position += File->Header.BytesTransferred;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Write to an NTFS file handle.
 *
 * NTFS VFS integration is read-only for now.
 *
 * @param File Open NTFS file handle.
 * @return DF_RETURN_NO_PERMISSION.
 */
U32 NtfsWriteFile(LPNTFSFILE File) {
    UNUSED(File);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/
