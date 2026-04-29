
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


    Executable EXOS

\************************************************************************/

#include "exec/Executable-EXOS.h"

#include "log/Log.h"

/************************************************************************/

BOOL GetExecutableInfo_EXOS(LPFILE File, LPEXECUTABLE_INFO Info) {
    FILE_OPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_INIT Init;
    U32 BytesTransferred;
    U32 Index;
    U32 Dummy;

    DEBUG(TEXT("Entering GetExecutableInfo_EXOS"));

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILE_OPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesTransferred = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
        DEBUG(TEXT("GetExecutableInfo_EXOS() : Bad signature (%X)"), Header.Signature);

        goto Out_Error;
    }

    FOREVER {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesTransferred = ReadFile(&FileOperation);

        if (BytesTransferred != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_INIT) {
            FileOperation.NumBytes = sizeof(EXOSCHUNK_INIT);
            FileOperation.Buffer = (LPVOID)&Init;
            BytesTransferred = ReadFile(&FileOperation);

            if (BytesTransferred != sizeof(EXOSCHUNK_INIT)) goto Out_Error;

            Info->EntryPoint = Init.EntryPoint;
            Info->CodeBase = Init.CodeBase;
            Info->DataBase = Init.DataBase;
            Info->CodeSize = Init.CodeSize;
            Info->DataSize = Init.DataSize;
            Info->StackMinimum = Init.StackMinimum;
            Info->StackRequested = Init.StackRequested;
            Info->HeapMinimum = Init.HeapMinimum;
            Info->HeapRequested = Init.HeapRequested;

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesTransferred = ReadFile(&FileOperation);
            }
        }
    }

Out_Success:

    DEBUG(TEXT("Exiting GetExecutableInfo_EXOS (Success)"));

    return TRUE;

Out_Error:

    DEBUG(TEXT("Exiting GetExecutableInfo_EXOS (Error)"));

    return FALSE;
}

/************************************************************************/

BOOL LoadExecutable_EXOS(LPFILE File, LPEXECUTABLE_INFO Info, LINEAR CodeBase, LINEAR DataBase) {
    FILE_OPERATION FileOperation;
    EXOSCHUNK Chunk;
    EXOSHEADER Header;
    EXOSCHUNK_FIXUP Fixup;
    LINEAR ItemAddress;
    U32 BytesTransferred;
    U32 Index;
    U32 CodeRead;
    U32 DataRead;
    U32 CodeOffset;
    U32 DataOffset;
    U32 NumFixups;
    U32 Dummy;
    U32 c;

    DEBUG(TEXT("Entering LoadExecutable_EXOS"));

    if (File == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILE_OPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;

    CodeRead = 0;
    DataRead = 0;

    CodeOffset = CodeBase - Info->CodeBase;
    DataOffset = DataBase - Info->DataBase;

    DEBUG(TEXT("LoadExecutable_EXOS() : CodeBase = %X"), CodeBase);
    DEBUG(TEXT("LoadExecutable_EXOS() : DataBase = %X"), DataBase);

    //-------------------------------------
    // Read the header

    FileOperation.NumBytes = sizeof(EXOSHEADER);
    FileOperation.Buffer = (LPVOID)&Header;
    BytesTransferred = ReadFile(&FileOperation);

    if (Header.Signature != EXOS_SIGNATURE) {
        goto Out_Error;
    }

    FOREVER {
        FileOperation.NumBytes = sizeof(EXOSCHUNK);
        FileOperation.Buffer = (LPVOID)&Chunk;
        BytesTransferred = ReadFile(&FileOperation);

        if (BytesTransferred != sizeof(EXOSCHUNK)) break;

        if (Chunk.ID == EXOS_CHUNK_CODE) {
            if (CodeRead == 1) {
                //-------------------------------------
                // Only one code chunk allowed

                goto Out_Error;
            }

            DEBUG(TEXT("LoadExecutable_EXOS() : Reading code"));

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)CodeBase;
            BytesTransferred = ReadFile(&FileOperation);

            if (BytesTransferred != Chunk.Size) goto Out_Error;

            CodeRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_DATA) {
            if (DataRead == 1) {
                //-------------------------------------
                // Only one data chunk allowed

                goto Out_Error;
            }

            DEBUG(TEXT("LoadExecutable_EXOS() : Reading data"));

            FileOperation.NumBytes = Chunk.Size;
            FileOperation.Buffer = (LPVOID)DataBase;
            BytesTransferred = ReadFile(&FileOperation);

            if (BytesTransferred != Chunk.Size) goto Out_Error;

            DataRead = 1;
        } else if (Chunk.ID == EXOS_CHUNK_FIXUP) {
            FileOperation.NumBytes = sizeof(U32);
            FileOperation.Buffer = (LPVOID)&NumFixups;
            BytesTransferred = ReadFile(&FileOperation);

            if (BytesTransferred != sizeof(U32)) goto Out_Error;

            DEBUG(TEXT("LoadExecutable_EXOS() : Reading relocations"));

            for (c = 0; c < NumFixups; c++) {
                FileOperation.NumBytes = sizeof(EXOSCHUNK_FIXUP);
                FileOperation.Buffer = (LPVOID)&Fixup;
                BytesTransferred = ReadFile(&FileOperation);

                if (BytesTransferred != sizeof(EXOSCHUNK_FIXUP)) goto Out_Error;

                if (Fixup.Section & EXOS_FIXUP_SOURCE_CODE) {
                    ItemAddress = CodeBase + (Fixup.Address - Info->CodeBase);
                } else if (Fixup.Section & EXOS_FIXUP_SOURCE_DATA) {
                    ItemAddress = DataBase + (Fixup.Address - Info->DataBase);
                } else {
                    ItemAddress = NULL;
                }

                SAFE_USE(ItemAddress) {
                    if (Fixup.Section & EXOS_FIXUP_DEST_CODE) {
                        *((U32*)ItemAddress) += CodeOffset;
                    } else if (Fixup.Section & EXOS_FIXUP_DEST_DATA) {
                        *((U32*)ItemAddress) += DataOffset;
                    }
                }
            }

            goto Out_Success;
        } else {
            for (Index = 0; Index < Chunk.Size; Index++) {
                FileOperation.NumBytes = 1;
                FileOperation.Buffer = (LPVOID)&Dummy;
                BytesTransferred = ReadFile(&FileOperation);
            }
        }
    }

    if (CodeRead == 0) goto Out_Error;

Out_Success:

    DEBUG(TEXT("Exiting LoadExecutable_EXOS"));

    return TRUE;

Out_Error:

    DEBUG(TEXT("Exiting LoadExecutable_EXOS"));

    return FALSE;
}
