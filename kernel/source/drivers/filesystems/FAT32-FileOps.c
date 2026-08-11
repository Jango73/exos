
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


    FAT32

\************************************************************************/

#include "drivers/filesystems/FAT32-Private.h"
/**
 * @brief Convert a FAT directory entry name to a null-terminated string.
 * @param DirEntry Directory entry containing 8.3 name.
 * @param Name Output buffer for decoded name.
 */
static void DecodeFileName(LPFATDIRENTRY_EXT DirEntry, LPSTR Name) {
    LPFATDIRENTRY_LFN LFNEntry = NULL;
    LPSTR LongName = Name;
    U32 Index;
    U32 Checksum;

    //-------------------------------------
    // 8.3 names

    for (Index = 0; Index < 8; Index++) {
        if (DirEntry->Name[Index] == STR_SPACE) break;
        *Name++ = DirEntry->Name[Index];
    }

    if (DirEntry->Ext[0] != STR_SPACE) {
        *Name++ = STR_DOT;
        for (Index = 0; Index < 3; Index++) {
            if (DirEntry->Ext[Index] == STR_SPACE) break;
            *Name++ = DirEntry->Ext[Index];
        }
    }

    *Name++ = STR_NULL;

    //-------------------------------------
    // Long names

    // Compute checksum

    Checksum = GetNameChecksum(DirEntry->Name);

    LFNEntry = (LPFATDIRENTRY_LFN)DirEntry;

    FOREVER {
        LFNEntry--;
        if (LFNEntry->Checksum != Checksum) break;

        *LongName++ = LFNEntry->Char01;
        *LongName++ = LFNEntry->Char02;
        *LongName++ = LFNEntry->Char03;
        *LongName++ = LFNEntry->Char04;
        *LongName++ = LFNEntry->Char05;
        *LongName++ = LFNEntry->Char06;
        *LongName++ = LFNEntry->Char07;
        *LongName++ = LFNEntry->Char08;
        *LongName++ = LFNEntry->Char09;
        *LongName++ = LFNEntry->Char10;
        *LongName++ = LFNEntry->Char11;
        *LongName++ = LFNEntry->Char12;
        *LongName++ = LFNEntry->Char13;
        *LongName = STR_NULL;

        if (LFNEntry->Ordinal & BIT_6) break;
    }
}

/***************************************************************************/

/**
 * @brief Locate a file within the FAT32 file system.
 * @param FileSystem Target file system.
 * @param Path Path of the file to locate.
 * @param FileLoc Output location information.
 * @return TRUE on success, FALSE if not found.
 */
static BOOL LocateFile(LPFAT32FILESYSTEM FileSystem, LPCSTR Path, LPFATFILELOC FileLoc) {
    STR Component[MAX_FILE_NAME];
    STR Name[MAX_FILE_NAME];
    LPFATDIRENTRY_EXT DirEntry;
    BOOL NamesMatch;
    U32 PathIndex = 0;
    U32 CompIndex = 0;

    FileLoc->PreviousCluster = 0;
    FileLoc->FolderCluster = FileSystem->Master.RootCluster;
    FileLoc->FileCluster = FileLoc->FolderCluster;
    FileLoc->Offset = 0;
    FileLoc->DataCluster = 0;

    //-------------------------------------
    // Read the root cluster

    if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer)) {
        return FALSE;
    }

    FOREVER {
        //-------------------------------------
        // Parse the next component to look for

    NextComponent:

        CompIndex = 0;

        FOREVER {
            if (Path[PathIndex] == STR_SLASH) {
                Component[CompIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (Path[PathIndex] == STR_NULL) {
                Component[CompIndex] = STR_NULL;
                break;
            } else {
                Component[CompIndex++] = Path[PathIndex++];
            }
        }
        if (Component[0] == STR_NULL) {
            if (Path[PathIndex] == STR_NULL) {
                FileLoc->DataCluster = FileLoc->FolderCluster;
                return TRUE;
            }
            continue;
        }

        //-------------------------------------
        // Loop through all directory entries

        FOREVER {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc->Offset);

            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                (DirEntry->Name[0] != 0xE5)) {
                DecodeFileName(DirEntry, Name);

                NamesMatch = (StringCompare(Component, TEXT("*")) == 0) || STRINGS_EQUAL(Component, Name) ||
                             (StringCompareNC(Component, Name) == 0);

                if (NamesMatch) {
                    if (Path[PathIndex] == STR_NULL) {
                        FileLoc->DataCluster = (((U32)DirEntry->ClusterLow) | (((U32)DirEntry->ClusterHigh) << 16));

                        return TRUE;
                    } else {
                        if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
                            U32 NextDir;

                            NextDir = (U32)DirEntry->ClusterLow;
                            NextDir |= ((U32)DirEntry->ClusterHigh) << 16;

                            FileLoc->FolderCluster = NextDir;
                            FileLoc->FileCluster = FileLoc->FolderCluster;
                            FileLoc->Offset = 0;

                            if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE)
                                return FALSE;

                            goto NextComponent;
                        } else {
                            return FALSE;
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc->Offset += sizeof(FAT_DIR_ENTRY_EXT);

            if (FileLoc->Offset >= FileSystem->BytesPerCluster) {
                FileLoc->Offset = 0;
                FileLoc->FileCluster = GetNextClusterInChain(FileSystem, FileLoc->FileCluster);

                if (FileLoc->FileCluster == 0 || FileLoc->FileCluster >= FAT32_CLUSTER_RESERVED) return FALSE;

                if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Populate file information from a directory entry.
 * @param DirEntry Directory entry source.
 * @param File Target FAT file object.
 */
static void TranslateFileInfo(LPFATDIRENTRY_EXT DirEntry, LPFATFILE File) {
    //-------------------------------------
    // Translate the attributes

    File->Header.Attributes = 0;

    if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if (DirEntry->Attributes & FAT_ATTR_READONLY) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if (DirEntry->Attributes & FAT_ATTR_HIDDEN) {
        File->Header.Attributes |= FS_ATTR_HIDDEN;
    }

    if (DirEntry->Attributes & FAT_ATTR_SYSTEM) {
        File->Header.Attributes |= FS_ATTR_SYSTEM;
    }

    File->Header.Attributes |= FS_ATTR_EXECUTABLE;

    //-------------------------------------
    // Translate the size

    File->Header.SizeLow = DirEntry->Size;
    File->Header.SizeHigh = 0;

    //-------------------------------------
    // Translate the time

    File->Header.Creation.Year = ((DirEntry->CreationYM & 0xFE00) >> 9) + 1980;
    File->Header.Creation.Month = (DirEntry->CreationYM & 0x01E0) >> 5;
    File->Header.Creation.Day = (DirEntry->CreationYM & 0x001F) >> 0;
    File->Header.Creation.Hour = (DirEntry->CreationHM & 0xF800) >> 11;
    File->Header.Creation.Minute = (DirEntry->CreationHM & 0x07E0) >> 5;
    File->Header.Creation.Second = ((DirEntry->CreationHM & 0x001F) >> 0) * 2;
    File->Header.Creation.Milli = 0;
}

/***************************************************************************/

/**
 * @brief Initialize the FAT32 driver.
 * @return DF_RETURN_SUCCESS.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/***************************************************************************/

/**
 * @brief Create a file or folder on the file system.
 * @param File File information containing path and attributes.
 * @param IsFolder TRUE to create a folder, FALSE to create a file.
 * @return DF_RETURN_* code.
 */
static U32 CreateFile(LPFILE_INFO File, BOOL IsFolder) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    FAT_FILE_LOCATION FileLoc;
    STR Component[MAX_FILE_NAME];
    STR Name[MAX_FILE_NAME];
    LPFATDIRENTRY_EXT DirEntry;
    U32 PathIndex = 0;
    U32 CompIndex = 0;
    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->FileSystem;
    if (FileSystem == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    //-------------------------------------
    // Initialize file location

    FileLoc.PreviousCluster = 0;
    FileLoc.FolderCluster = FileSystem->Master.RootCluster;
    FileLoc.FileCluster = FileLoc.FolderCluster;
    FileLoc.Offset = 0;
    FileLoc.DataCluster = 0;

    //-------------------------------------
    // Read the root cluster

    if (!ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer)) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    FOREVER {
        //-------------------------------------
        // Parse the next component to look for

    NextComponent:

        CompIndex = 0;

        FOREVER {
            if (File->Name[PathIndex] == STR_SLASH) {
                Component[CompIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (File->Name[PathIndex] == STR_NULL) {
                Component[CompIndex] = STR_NULL;
                break;
            } else {
                Component[CompIndex++] = File->Name[PathIndex++];
            }
        }

        // Check if we're at the last component (file/folder to create)
        BOOL IsLastComponent = (File->Name[PathIndex] == STR_NULL);

        //-------------------------------------
        // Loop through all directory entries

        FOREVER {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                (DirEntry->Name[0] != 0xE5)) {
                BOOL ExpectFolderMatch = (!IsLastComponent) || IsFolder;
                BOOL NamesMatch;

                DecodeFileName(DirEntry, Name);

                NamesMatch = (StringCompare(Component, TEXT("*")) == 0) || STRINGS_EQUAL(Component, Name);

                if (!NamesMatch && ExpectFolderMatch && (DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                    NamesMatch = (StringCompareNC(Component, Name) == 0);
                }

                if (NamesMatch) {
                    if (IsLastComponent) {
                        // Found existing item with same name
                        if (IsFolder && (DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                            return DF_RETURN_SUCCESS; // Folder already exists
                        } else if (!IsFolder && !(DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                            return DF_RETURN_SUCCESS; // File already exists
                        }
                        return DF_RETURN_GENERIC; // Type mismatch
                    } else {
                        // Navigating to next directory component
                        if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
                            U32 NextDir;

                            NextDir = (U32)DirEntry->ClusterLow;
                            NextDir |= ((U32)DirEntry->ClusterHigh) << 16;

                            FileLoc.FolderCluster = NextDir;
                            FileLoc.FileCluster = FileLoc.FolderCluster;
                            FileLoc.Offset = 0;

                            if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE)
                                return DF_RETURN_INPUT_OUTPUT;

                            goto NextComponent;
                        } else {
                            return DF_RETURN_GENERIC; // Path component is not a directory
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc.Offset += sizeof(FAT_DIR_ENTRY_EXT);

            if (FileLoc.Offset >= FileSystem->BytesPerCluster) {
                FileLoc.Offset = 0;
                FileLoc.FileCluster = GetNextClusterInChain(FileSystem, FileLoc.FileCluster);

                if (FileLoc.FileCluster == 0 || (FileLoc.FileCluster & 0x0FFFFFFF) >= 0x0FFFFFF8) {
                    //-------------------------------------
                    // We are at the end of this directory
                    // and we did not find the current component
                    // so we create it

                    if (IsLastComponent) {
                        // Create the final file/folder
                        BOOL result = CreateDirEntry(FileSystem, FileLoc.FolderCluster, Component, IsFolder ? FAT_ATTR_FOLDER : FAT_ATTR_ARCHIVE);
                        return result ? DF_RETURN_SUCCESS : DF_RETURN_GENERIC;
                    } else {
                        // Create intermediate directory and continue navigation
                        if (CreateDirEntry(FileSystem, FileLoc.FolderCluster, Component, FAT_ATTR_FOLDER) != TRUE) {
                            return DF_RETURN_GENERIC;
                        }

                        // Find the newly created directory and navigate into it
                        FileLoc.Offset = 0;
                        FileLoc.FileCluster = FileLoc.FolderCluster;

                        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                            return DF_RETURN_INPUT_OUTPUT;
                        }

                        // Search for the newly created directory
                        FOREVER {
                            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

                            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                                (DirEntry->Name[0] != 0xE5)) {
                                DecodeFileName(DirEntry, Name);
                
                                if (STRINGS_EQUAL(Component, Name) && (DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                                    U32 NextDir = (U32)DirEntry->ClusterLow | (((U32)DirEntry->ClusterHigh) << 16);
                                    FileLoc.FolderCluster = NextDir;
                                    FileLoc.FileCluster = NextDir;
                                    FileLoc.Offset = 0;

                                    if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                                        return DF_RETURN_INPUT_OUTPUT;
                                    }

                                    goto NextComponent;
                                }
                            }

                            FileLoc.Offset += sizeof(FAT_DIR_ENTRY_EXT);
                            if (FileLoc.Offset >= FileSystem->BytesPerCluster) {
                                return DF_RETURN_GENERIC; // Should have found the directory we just created
                            }
                        }
                    }
                }

                if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_RETURN_INPUT_OUTPUT;
            }
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Delete a folder from the file system.
 * @param File File information describing folder.
 * @return DF_RETURN_* code.
 */
static U32 DeleteFolder(LPFILE_INFO File) {
    UNUSED(File);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Rename a folder within the file system.
 * @param File File information with old and new names.
 * @return DF_RETURN_* code.
 */
static U32 RenameFolder(LPFILE_INFO File) {
    UNUSED(File);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Open a file for reading or writing.
 * @param Find File information containing path.
 * @return Handle to FAT file or NULL on failure.
 */
static LPFATFILE OpenFile(LPFILE_INFO Find) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    LPFATFILE File = NULL;
    LPFATDIRENTRY_EXT DirEntry = NULL;
    FAT_FILE_LOCATION FileLoc;
    //-------------------------------------
    // Check validity of parameters

    if (Find == NULL) return NULL;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)Find->FileSystem;

    if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE) {
        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

        File = NewFATFile(FileSystem, &FileLoc);
        if (File == NULL) return NULL;

        File->DirectoryBufferCluster = FileLoc.FileCluster;
        File->DirectoryBufferGeneration = FileSystem->IOBufferGeneration;
        File->DirectoryBufferValid = TRUE;

        DecodeFileName(DirEntry, File->Header.Name);
        TranslateFileInfo(DirEntry, File);

        //-------------------------------------
        // Handle FILE_OPEN_TRUNCATE flag

        if (Find->Flags & FILE_OPEN_TRUNCATE) {

            // Reset file size to zero in memory
            File->Header.SizeLow = 0;
            File->Header.SizeHigh = 0;

            // Update directory entry
            DirEntry->Size = 0;

            // Write the updated directory entry back to storage
            if (WriteCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                // Don't free directly - let ReleaseKernelObject handle it
                return NULL;
            }

        }
    } else if (Find->Flags & FILE_OPEN_CREATE_ALWAYS) {

        //-------------------------------------
        // Create the file

        FILE_INFO TempFileInfo;
        TempFileInfo.Size = sizeof(FILE_INFO);
        TempFileInfo.FileSystem = (LPFILESYSTEM)FileSystem;
        TempFileInfo.Attributes = MAX_U32;
        TempFileInfo.Flags = FILE_OPEN_CREATE_ALWAYS;
        StringCopy(TempFileInfo.Name, Find->Name);

        if (CreateFile(&TempFileInfo, FALSE) != DF_RETURN_SUCCESS) {
            return NULL;
        }

        //-------------------------------------
        // Now locate the newly created file

        if (LocateFile(FileSystem, Find->Name, &FileLoc) == FALSE) {
            return NULL;
        }

        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
            return NULL;
        }

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

        File = NewFATFile(FileSystem, &FileLoc);
        if (File == NULL) {
            return NULL;
        }

        File->DirectoryBufferCluster = FileLoc.FileCluster;
        File->DirectoryBufferGeneration = FileSystem->IOBufferGeneration;
        File->DirectoryBufferValid = TRUE;

        DecodeFileName(DirEntry, File->Header.Name);
        TranslateFileInfo(DirEntry, File);

    }

    return File;
}

/***************************************************************************/

/**
 * @brief Advance to next directory entry during enumeration.
 * @param File Current FAT file handle representing directory.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 OpenNext(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    LPFATDIRENTRY_EXT DirEntry = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    if (!File->DirectoryBufferValid || File->DirectoryBufferCluster != File->Location.FileCluster ||
        File->DirectoryBufferGeneration != FileSystem->IOBufferGeneration) {
        if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_RETURN_INPUT_OUTPUT;
    }

    FOREVER {
        File->Location.Offset += sizeof(FAT_DIR_ENTRY_EXT);

        if (File->Location.Offset >= FileSystem->BytesPerCluster) {
            File->Location.Offset = 0;

            File->Location.FileCluster = GetNextClusterInChain(FileSystem, File->Location.FileCluster);

            if (File->Location.FileCluster == 0 || File->Location.FileCluster >= FAT32_CLUSTER_RESERVED)
                return DF_RETURN_GENERIC;

            if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_RETURN_INPUT_OUTPUT;
        }

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + File->Location.Offset);

        if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
            (DirEntry->Name[0] != 0xE5)) {
            File->Location.DataCluster = (((U32)DirEntry->ClusterLow) | (((U32)DirEntry->ClusterHigh) << 16));

            DecodeFileName(DirEntry, File->Header.Name);
            TranslateFileInfo(DirEntry, File);
            File->DirectoryBufferCluster = File->Location.FileCluster;
            File->DirectoryBufferGeneration = FileSystem->IOBufferGeneration;
            File->DirectoryBufferValid = TRUE;
            break;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Close an open FAT32 file handle.
 * @param File File handle to close.
 * @return DF_RETURN_SUCCESS.
 */
static U32 CloseFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    LPFATDIRENTRY_EXT DirEntry;

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Update file information in directory entry

    if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) {
        return DF_RETURN_INPUT_OUTPUT;
    }

    DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + File->Location.Offset);

    if (File->Header.SizeLow > DirEntry->Size) {
        DirEntry->Size = File->Header.SizeLow;

        if (WriteCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }
    }

    ReleaseKernelObject(File);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read data from a file.
 * @param File File handle with read parameters.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 ReadFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    Cluster = File->Location.DataCluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);
        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            return DF_RETURN_INPUT_OUTPUT;
        }
    }

    FOREVER {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
        if (ByteCount > BytesRemaining) ByteCount = BytesRemaining;

        //-------------------------------------
        // Copy the data to the user buffer

        MemoryCopy(
            ((U8*)File->Header.Buffer) + File->Header.BytesTransferred, FileSystem->IOBuffer + OffsetInCluster, ByteCount);

        //-------------------------------------
        // Update counters

        OffsetInCluster = 0;
        BytesRemaining -= ByteCount;
        File->Header.BytesTransferred += ByteCount;
        File->Header.Position += ByteCount;

        //-------------------------------------
        // Check if we read all data

        if (BytesRemaining == 0) break;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            break;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Write data to a file.
 * @param File File handle with write parameters.
 * @return DF_RETURN_SUCCESS or error code.
 */
static U32 WriteFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    CLUSTER LastValidCluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.TypeID != KOID_FILE) return DF_RETURN_BAD_PARAMETER;
    if (File->Header.Buffer == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    if (ByteCount > BytesRemaining) {
        ByteCount = BytesRemaining;
    }

    Cluster = File->Location.DataCluster;
    LastValidCluster = Cluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
                return DF_RETURN_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    while (BytesRemaining > 0) {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        //-------------------------------------
        // Copy the user buffer

        U32 BytesToTransfer = ByteCount;

        if (BytesToTransfer > BytesRemaining) {
            BytesToTransfer = BytesRemaining;
        }

        MemoryCopy(FileSystem->IOBuffer + OffsetInCluster,
                   ((U8*)File->Header.Buffer) + File->Header.BytesTransferred, BytesToTransfer);

        //-------------------------------------
        // Write the current data cluster

        if (WriteCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_RETURN_INPUT_OUTPUT;
        }

        //-------------------------------------
        // Update counters

        File->Header.BytesTransferred += BytesToTransfer;
        File->Header.Position += BytesToTransfer;
        BytesRemaining -= BytesToTransfer;

        if (BytesRemaining == 0) {
            break;
        }

        OffsetInCluster = 0;
        ByteCount = FileSystem->BytesPerCluster;

        if (ByteCount > BytesRemaining) {
            ByteCount = BytesRemaining;
        }

        LastValidCluster = Cluster;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
                return DF_RETURN_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    if (File->Header.Position > File->Header.SizeLow) {
        File->Header.SizeLow = File->Header.Position;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Create a new FAT32 partition in storage.
 * @param Create Partition creation parameters.
 * @return DF_RETURN_* code.
 */
static U32 CreatePartition(LPPARTITION_CREATION Create) {
    LPFAT32MBR Master = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Create == NULL) return DF_RETURN_BAD_PARAMETER;
    if (Create->Storage == NULL) return DF_RETURN_BAD_PARAMETER;

    //-------------------------------------

    Master = (LPFAT32MBR)KernelHeapAlloc(sizeof(FAT32_MBR));

    if (Master == NULL) return DF_RETURN_NO_MEMORY;

    //-------------------------------------
    // Fill the master boot record

    Master->OEMName[0] = 'M';
    Master->OEMName[1] = 'S';
    Master->OEMName[2] = 'W';
    Master->OEMName[3] = 'I';
    Master->OEMName[4] = 'N';
    Master->OEMName[5] = '4';
    Master->OEMName[6] = '.';
    Master->OEMName[7] = '1';
    Master->BytesPerSector = 512;
    Master->SectorsPerCluster = 8;
    Master->ReservedSectors = 3;
    Master->NumFATs = 2;
    Master->NumRootEntries_NA = 0;
    Master->NumSectors_NA = 0;
    Master->MediaDescriptor = 0xF8;
    Master->SectorsPerFAT_NA = 0;
    Master->SectorsPerTrack = 63;
    Master->NumHeads = 255;
    Master->NumHiddenSectors = 127;
    Master->NumSectors = Create->PartitionNumSectors;
    Master->NumSectorsPerFAT = 4;
    Master->Flags = 0;
    Master->Version = 0;
    Master->RootCluster = 2;
    Master->InfoSector = 1;
    Master->BackupBootSector = 6;
    Master->LogicalDriveNumber = 0x80;
    Master->Reserved2 = 0;
    Master->ExtendedSignature = 0x29;
    Master->SerialNumber = 0;
    Master->FATName[0] = 'F';
    Master->FATName[1] = 'A';
    Master->FATName[2] = 'T';
    Master->FATName[3] = '3';
    Master->FATName[4] = '2';
    Master->FATName[5] = ' ';
    Master->FATName[6] = ' ';
    Master->FATName[7] = ' ';
    Master->BIOSMark = 0xAA55;

    //-------------------------------------

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Dispatch function for FAT32 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter pointer.
 * @return DF_RETURN_* result code.
 */
UINT FAT32Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_SETVOLUME_INFO:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_CREATEFOLDER:
            return (UINT)CreateFile((LPFILE_INFO)Parameter, TRUE);
        case DF_FS_DELETEFOLDER:
            return (UINT)DeleteFolder((LPFILE_INFO)Parameter);
        case DF_FS_RENAMEFOLDER:
            return (UINT)RenameFolder((LPFILE_INFO)Parameter);
        case DF_FS_OPENFILE:
            return (UINT)OpenFile((LPFILE_INFO)Parameter);
        case DF_FS_OPENNEXT:
            return (UINT)OpenNext((LPFATFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (UINT)CloseFile((LPFATFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_RENAMEFILE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_FS_READ:
            return (UINT)ReadFile((LPFATFILE)Parameter);
        case DF_FS_WRITE:
            return (UINT)WriteFile((LPFATFILE)Parameter);
        case DF_FS_CREATEPARTITION:
            return (UINT)CreatePartition((LPPARTITION_CREATION)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
