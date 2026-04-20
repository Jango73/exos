
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


    File

\************************************************************************/

#include "fs/File.h"

#include "memory/Heap.h"
#include "utils/Helpers.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "process/Process.h"

/***************************************************************************/

/**
 * @brief Build a path qualified with the current process working directory.
 *
 * @param Name Input path (absolute or relative).
 * @param QualifiedName Output buffer receiving the qualified path.
 * @return TRUE when the qualified path is produced, FALSE otherwise.
 */
static BOOL BuildQualifiedFileName(LPCSTR Name, LPSTR QualifiedName) {
    LPPROCESS Process;
    U32 BaseLength;
    U32 RelativeLength;

    if (Name == NULL || QualifiedName == NULL) {
        ERROR(TEXT("Bad parameters"));
        return FALSE;
    }

    Process = GetCurrentProcess();

    // Kernel process keeps legacy lookup (relative to system partition)
    if (Process == &KernelProcess) {
        StringCopy(QualifiedName, Name);
        return TRUE;
    }

    if (Name[0] == PATH_SEP) {
        StringCopy(QualifiedName, Name);
        return TRUE;
    }

    QualifiedName[0] = STR_NULL;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) { StringCopy(QualifiedName, Process->WorkFolder); }

    if (QualifiedName[0] == STR_NULL) {
        StringCopy(QualifiedName, TEXT(ROOT));
    }

    BaseLength = StringLength(QualifiedName);
    if (BaseLength == 0) {
        ERROR(TEXT("Empty base path"));
        return FALSE;
    }

    if (QualifiedName[BaseLength - 1] != PATH_SEP) {
        if (BaseLength + 1 >= MAX_PATH_NAME) {
            ERROR(TEXT("Base path too long (%u)"), BaseLength);
            return FALSE;
        }
        QualifiedName[BaseLength] = PATH_SEP;
        QualifiedName[BaseLength + 1] = STR_NULL;
        BaseLength++;
    }

    RelativeLength = StringLength(Name);
    if (BaseLength + RelativeLength >= MAX_PATH_NAME) {
        ERROR(TEXT("Path too long (%u + %u)"), BaseLength, RelativeLength);
        return FALSE;
    }

    StringConcat(QualifiedName, Name);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build one retained snapshot of mounted filesystems for probing.
 *
 * Each returned filesystem has its reference count incremented and must be
 * released with ReleaseKernelObject by the caller.
 *
 * @param SnapshotOut Output pointer to allocated filesystem pointer array.
 * @param CountOut Output number of valid entries in SnapshotOut.
 * @return TRUE on success, FALSE on allocation failure.
 */
static BOOL BuildFileSystemProbeSnapshot(LPFILESYSTEM** SnapshotOut, U32* CountOut) {
    LPFILESYSTEM* Snapshot = NULL;
    U32 Capacity = 0;
    U32 Count = 0;

    if (SnapshotOut == NULL || CountOut == NULL) return FALSE;

    *SnapshotOut = NULL;
    *CountOut = 0;

    LockMutex(MUTEX_FILESYSTEM, INFINITY);

    LPLIST FileSystemList = GetFileSystemList();
    Capacity = (FileSystemList != NULL) ? FileSystemList->NumItems : 0;

    if (Capacity != 0) {
        Snapshot = (LPFILESYSTEM*)KernelHeapAlloc(Capacity * sizeof(LPFILESYSTEM));
        if (Snapshot == NULL) {
            UnlockMutex(MUTEX_FILESYSTEM);
            return FALSE;
        }

        for (LPLISTNODE Node = FileSystemList->First; Node != NULL; Node = Node->Next) {
            LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
            if (FileSystem == NULL) continue;

            FileSystem->References++;
            Snapshot[Count] = FileSystem;
            Count++;
        }
    }

    UnlockMutex(MUTEX_FILESYSTEM);

    *SnapshotOut = Snapshot;
    *CountOut = Count;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release one retained filesystem snapshot returned by BuildFileSystemProbeSnapshot.
 *
 * @param Snapshot Filesystem pointer array.
 * @param Count Number of valid entries.
 */
static void ReleaseFileSystemProbeSnapshot(LPFILESYSTEM* Snapshot, U32 Count) {
    U32 Index;

    if (Snapshot == NULL) return;

    for (Index = 0; Index < Count; Index++) {
        if (Snapshot[Index] != NULL) {
            ReleaseKernelObject(Snapshot[Index]);
        }
    }

    KernelHeapFree(Snapshot);
}

/***************************************************************************/

/**
 * @brief Opens a file based on provided information
 * @param Info Pointer to file open information structure
 * @return Pointer to opened file structure, or NULL on failure
 */
LPFILE OpenFile(LPFILE_OPEN_INFO Info) {
    FILE_INFO Find;
    LPFILESYSTEM FileSystem = NULL;
    LPLISTNODE Node = NULL;
    LPFILE File = NULL;
    LPFILE AlreadyOpen = NULL;
    LPFILESYSTEM* FileSystemSnapshot = NULL;
    U32 FileSystemSnapshotCount = 0;
    STR QualifiedName[MAX_PATH_NAME];
    LPCSTR RequestedName;

    //-------------------------------------
    // Check validity of parameters

    if (Info == NULL || Info->Name == NULL) return NULL;

    //-------------------------------------
    // Resolve the requested name against the process working directory

    if (!BuildQualifiedFileName(Info->Name, QualifiedName)) {
        ERROR(TEXT("BuildQualifiedFileName failed name=%s flags=%x"), Info->Name, Info->Flags);
        return NULL;
    }

    DEBUG(TEXT("Name=%s, Flags=%x"), Info->Name, Info->Flags);
    {
        LPPROCESS Process = GetCurrentProcess();
        if (Process != NULL) {
            DEBUG(TEXT("Current process workfolder=%s"), Process->WorkFolder);
        }
    }

    RequestedName = QualifiedName;
    DEBUG(TEXT("QualifiedName=%s"), RequestedName);

    //-------------------------------------
    // Check if the file is already open

    LockMutex(MUTEX_FILE, INFINITY);

    LPLIST FileList = GetFileList();
    for (Node = FileList != NULL ? FileList->First : NULL; Node; Node = Node->Next) {
        AlreadyOpen = (LPFILE)Node;

        LockMutex(&(AlreadyOpen->Mutex), INFINITY);

        if (STRINGS_EQUAL(AlreadyOpen->Name, RequestedName) || STRINGS_EQUAL(AlreadyOpen->Name, Info->Name)) {
            if (AlreadyOpen->OwnerTask == GetCurrentTask()) {
                if (AlreadyOpen->OpenFlags == Info->Flags) {
                    File = AlreadyOpen;
                    File->References++;

                    UnlockMutex(&(AlreadyOpen->Mutex));
                    UnlockMutex(MUTEX_FILE);
                    return File;
                }
            }
        }

        UnlockMutex(&(AlreadyOpen->Mutex));
    }

    UnlockMutex(MUTEX_FILE);

    //-------------------------------------
    // Use SystemFS if an absolute path is provided

    if (RequestedName[0] == PATH_SEP) {
        LPFILESYSTEM SystemFileSystem = NULL;

        LockMutex(MUTEX_FILESYSTEM, INFINITY);
        SystemFileSystem = GetSystemFS();
        if (SystemFileSystem != NULL) {
            SystemFileSystem->References++;
        }
        UnlockMutex(MUTEX_FILESYSTEM);

        if (SystemFileSystem == NULL) {
            WARNING(TEXT("SystemFS unavailable path=%s"), RequestedName);
            return NULL;
        }

        Find.Size = sizeof Find;
        Find.FileSystem = SystemFileSystem;
        Find.Attributes = MAX_U32;
        Find.Flags = Info->Flags;
        StringCopy(Find.Name, RequestedName);

        DEBUG(TEXT("Using SystemFS, path=%s"), Find.Name);

        File = (LPFILE)SystemFileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        ReleaseKernelObject(SystemFileSystem);

        SAFE_USE(File) {
            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(FileList, File);

            UnlockMutex(MUTEX_FILE);
        }
        if (File == NULL) {
            WARNING(TEXT("SystemFS open failed path=%s flags=%x"), Find.Name, Find.Flags);
        }

        return File;
    }

    //-------------------------------------
    // Get the name of the volume in which the file
    // is supposed to be located

    DEBUG(TEXT("Searching for %s in file systems"), RequestedName);

    if (!BuildFileSystemProbeSnapshot(&FileSystemSnapshot, &FileSystemSnapshotCount)) {
        ERROR(TEXT("Unable to build filesystem probe snapshot"));
        return NULL;
    }

    for (U32 Index = 0; Index < FileSystemSnapshotCount; Index++) {
        FileSystem = FileSystemSnapshot[Index];
        if (FileSystem == NULL || FileSystem->Driver == NULL) continue;

        Find.Size = sizeof Find;
        Find.FileSystem = FileSystem;
        Find.Attributes = MAX_U32;
        Find.Flags = Info->Flags;
        StringCopy(Find.Name, RequestedName);

        DEBUG(TEXT("Probing %s with %s"), FileSystem->Driver->Product, Find.Name);

        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);

        SAFE_USE(File) {
            DEBUG(TEXT("Found %s in %s"), RequestedName, FileSystem->Driver->Product);

            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(FileList, File);

            UnlockMutex(MUTEX_FILE);
            break;
        }
    }

    ReleaseFileSystemProbeSnapshot(FileSystemSnapshot, FileSystemSnapshotCount);

    return File;
}

/***************************************************************************/

/**
 * @brief Closes an open file and decrements reference count
 * @param File Pointer to file structure to close
 * @return 1 on success, 0 on failure
 */
UINT CloseFile(LPFILE File) {
    //-------------------------------------
    // Check validity of parameters

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        if (File->TypeID != KOID_FILE) return 0;

        LockMutex(&(File->Mutex), INFINITY);

        // Call filesystem-specific close function
        File->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);

        UnlockMutex(&(File->Mutex));

        return 1;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Gets current position within a file
 * @param File Pointer to file structure
 * @return Current file position
 */
UINT GetFilePosition(LPFILE File) {
    UINT Position = 0;

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        //-------------------------------------
        // Lock access to the file

        LockMutex(&(File->Mutex), INFINITY);

        Position = File->Position;

        //-------------------------------------
        // Unlock access to the file

        UnlockMutex(&(File->Mutex));
    }

    return Position;
}

/***************************************************************************/

/**
 * @brief Sets current position within a file
 * @param Operation Pointer to file operation structure containing new position
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER on failure
 */
UINT SetFilePosition(LPFILE_OPERATION Operation) {
    SAFE_USE_VALID(Operation) {
        LPFILE File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->Position = Operation->NumBytes;

            //-------------------------------------
            // Unlock access to the file

            UnlockMutex(&(File->Mutex));

            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}

/***************************************************************************/

/**
 * @brief Reads data from a file
 * @param Operation Pointer to file operation structure
 * @return Number of bytes read, 0 on failure
 */
UINT ReadFile(LPFILE_OPERATION Operation) {
    LPFILE File = NULL;
    UINT Result = 0;
    UINT BytesTransferred = 0;

    SAFE_USE_VALID(Operation) {
        File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if ((File->OpenFlags & FILE_OPEN_READ) == 0) return 0;

            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->ByteCount = Operation->NumBytes;
            File->Buffer = Operation->Buffer;

            Result = File->FileSystem->Driver->Command(DF_FS_READ, (UINT)File);

            if (Result == DF_RETURN_SUCCESS) {
                BytesTransferred = File->BytesTransferred;
            }

            UnlockMutex(&(File->Mutex));

            return BytesTransferred;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Writes data to a file
 * @param Operation Pointer to file operation structure
 * @return Number of bytes written, 0 on failure
 */
UINT WriteFile(LPFILE_OPERATION Operation) {
    LPFILE File = NULL;
    UINT Result = 0;
    UINT BytesWritten = 0;

    SAFE_USE_VALID(Operation) {
        File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if ((File->OpenFlags & FILE_OPEN_WRITE) == 0) return 0;

            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->ByteCount = Operation->NumBytes;
            File->Buffer = Operation->Buffer;

            Result = File->FileSystem->Driver->Command(DF_FS_WRITE, (UINT)File);

            if (Result == DF_RETURN_SUCCESS) {
                BytesWritten = File->BytesTransferred;
                if (BytesWritten != Operation->NumBytes) {
                    ERROR(TEXT("Short write rejected path=%s requested=%u written=%u"),
                        File->Name,
                        Operation->NumBytes,
                        BytesWritten);
                    BytesWritten = 0;
                }
            }

            UnlockMutex(&(File->Mutex));

            return BytesWritten;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Gets the size of a file
 * @param File Pointer to file structure
 * @return File size in bytes
 */
UINT GetFileSize(LPFILE File) {
    UINT Size = 0;

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        LockMutex(&(File->Mutex), INFINITY);

#ifdef __EXOS_64__
        Size = U64_Make(File->SizeHigh, File->SizeLow);
#else
        Size = File->SizeLow;
#endif

        UnlockMutex(&(File->Mutex));

        return Size;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Reads entire file content into memory
 * @param Name File name to read
 * @param Size Pointer to variable to receive file size
 * @return Pointer to allocated buffer containing file content, NULL on failure
 */
LPVOID FileReadAll(LPCSTR Name, UINT *Size) {
    FILE_OPEN_INFO OpenInfo;
    FILE_OPERATION FileOp;
    LPFILE File = NULL;
    LPVOID Buffer = NULL;
    UINT BytesToRead = 0;
    UINT BytesRead = 0;

    DEBUG(TEXT("Name = %s"), Name);

    SAFE_USE_2(Name, Size) {
        //-------------------------------------
        // Open the file

        OpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
        OpenInfo.Name = (LPSTR)Name;
        OpenInfo.Flags = FILE_OPEN_READ;
        File = OpenFile(&OpenInfo);

        if (File == NULL) return NULL;

        DEBUG(TEXT("File found"));

        //-------------------------------------
        // Allocate buffer and read content

        BytesToRead = GetFileSize(File);
        *Size = BytesToRead;
        Buffer = KernelHeapAlloc(BytesToRead + 1);

        SAFE_USE(Buffer) {
            FileOp.Header.Size = sizeof(FILE_OPERATION);
            FileOp.File = (HANDLE)File;
            FileOp.Buffer = Buffer;
            FileOp.NumBytes = BytesToRead;
            BytesRead = ReadFile(&FileOp);
            if (BytesRead < BytesToRead) {
                WARNING(TEXT("Short read on %s (%u/%u)"), Name, BytesRead, BytesToRead);
            }
            *Size = BytesRead;
            ((LPSTR)Buffer)[BytesRead] = STR_NULL;
        }

        CloseFile(File);

        return Buffer;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Writes entire buffer content to a file
 * @param Name File name to write to
 * @param Buffer Pointer to data buffer
 * @param Size Size of data to write
 * @return Number of bytes written
 */
UINT FileWriteAll(LPCSTR Name, LPCVOID Buffer, UINT Size) {
    FILE_OPEN_INFO OpenInfo;
    FILE_OPERATION FileOp;
    LPFILE File = NULL;
    UINT BytesWritten = 0;

    DEBUG(TEXT("name %s, size %u"), Name, Size);

    SAFE_USE_2(Name, Buffer) {
        //-------------------------------------
        // Open the file

        OpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
        OpenInfo.Name = (LPSTR)Name;
        OpenInfo.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
        File = OpenFile(&OpenInfo);

        if (File == NULL) {
            DEBUG(TEXT("OpenFile failed to create %s"), Name);
            return 0;
        }

        //-------------------------------------
        // Write the buffer to the file

        FileOp.Header.Size = sizeof(FILE_OPERATION);
        FileOp.File = (HANDLE)File;
        FileOp.Buffer = (LPVOID)Buffer;
        FileOp.NumBytes = Size;
        BytesWritten = WriteFile(&FileOp);
        if (BytesWritten != Size) {
            WARNING(TEXT("Write failed name=%s requested=%u written=%u"),
                Name,
                Size,
                BytesWritten);
            BytesWritten = 0;
        }

        CloseFile(File);

        return BytesWritten;
    }

    return 0;
}

/***************************************************************************/
