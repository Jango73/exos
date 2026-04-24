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


    Pipe

\************************************************************************/

#include "utils/Pipe.h"

#include "core/ID.h"
#include "core/Kernel.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"

/************************************************************************/

#define PIPE_DEFAULT_CAPACITY N_64KB

/************************************************************************/

typedef struct tag_PIPE {
    MUTEX Mutex;
    U8* Buffer;
    UINT Capacity;
    UINT ReadPosition;
    UINT WritePosition;
    UINT BufferedBytes;
    UINT ReaderCount;
    UINT WriterCount;
} PIPE, *LPPIPE;

typedef struct tag_PIPE_ENDPOINT {
    LISTNODE_FIELDS
    LPPIPE Pipe;
    U32 Direction;
} PIPE_ENDPOINT, *LPPIPE_ENDPOINT;

/************************************************************************/

static UINT PipeWriteInternal(LPPIPE Pipe, LPCVOID Buffer, UINT Count) {
    const U8* Source = (const U8*)Buffer;
    UINT WriteCount = 0;

    while (WriteCount < Count) {
        UINT FreeBytes;
        UINT Chunk;

        LockMutex(&Pipe->Mutex, INFINITY);

        if (Pipe->ReaderCount == 0) {
            UnlockMutex(&Pipe->Mutex);
            return WriteCount;
        }

        FreeBytes = Pipe->Capacity - Pipe->BufferedBytes;
        if (FreeBytes == 0) {
            UnlockMutex(&Pipe->Mutex);
            Sleep(1);
            continue;
        }

        Chunk = Count - WriteCount;
        if (Chunk > FreeBytes) {
            Chunk = FreeBytes;
        }

        while (Chunk > 0) {
            UINT EndBytes = Pipe->Capacity - Pipe->WritePosition;
            UINT CopyBytes = Chunk;

            if (CopyBytes > EndBytes) {
                CopyBytes = EndBytes;
            }

            MemoryCopy(Pipe->Buffer + Pipe->WritePosition, Source + WriteCount, CopyBytes);
            Pipe->WritePosition = (Pipe->WritePosition + CopyBytes) % Pipe->Capacity;
            Pipe->BufferedBytes += CopyBytes;
            WriteCount += CopyBytes;
            Chunk -= CopyBytes;
        }

        UnlockMutex(&Pipe->Mutex);
    }

    return WriteCount;
}

/************************************************************************/

static UINT PipeReadInternal(LPPIPE Pipe, LPVOID Buffer, UINT Count) {
    U8* Destination = (U8*)Buffer;
    UINT ReadCount = 0;

    if (Count == 0) {
        return 0;
    }

    FOREVER {
        UINT AvailableBytes;
        UINT Chunk;

        LockMutex(&Pipe->Mutex, INFINITY);

        AvailableBytes = Pipe->BufferedBytes;
        if (AvailableBytes == 0) {
            if (Pipe->WriterCount == 0) {
                UnlockMutex(&Pipe->Mutex);
                return ReadCount;
            }

            UnlockMutex(&Pipe->Mutex);
            Sleep(1);
            continue;
        }

        Chunk = Count - ReadCount;
        if (Chunk > AvailableBytes) {
            Chunk = AvailableBytes;
        }

        while (Chunk > 0) {
            UINT EndBytes = Pipe->Capacity - Pipe->ReadPosition;
            UINT CopyBytes = Chunk;

            if (CopyBytes > EndBytes) {
                CopyBytes = EndBytes;
            }

            MemoryCopy(Destination + ReadCount, Pipe->Buffer + Pipe->ReadPosition, CopyBytes);
            Pipe->ReadPosition = (Pipe->ReadPosition + CopyBytes) % Pipe->Capacity;
            Pipe->BufferedBytes -= CopyBytes;
            ReadCount += CopyBytes;
            Chunk -= CopyBytes;
        }

        UnlockMutex(&Pipe->Mutex);
        return ReadCount;
    }
}

/************************************************************************/

UINT PipeCreateHandles(HANDLE* OutReadHandle, HANDLE* OutWriteHandle) {
    LPPIPE Pipe = NULL;
    LPPIPE_ENDPOINT ReadEndpoint = NULL;
    LPPIPE_ENDPOINT WriteEndpoint = NULL;
    HANDLE ReadHandle = 0;
    HANDLE WriteHandle = 0;

    if (OutReadHandle == NULL || OutWriteHandle == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    *OutReadHandle = 0;
    *OutWriteHandle = 0;

    Pipe = (LPPIPE)KernelHeapAlloc(sizeof(PIPE));
    if (Pipe == NULL) {
        return DF_RETURN_NO_MEMORY;
    }

    MemorySet(Pipe, 0, sizeof(PIPE));
    InitMutex(&Pipe->Mutex);
    Pipe->Capacity = PIPE_DEFAULT_CAPACITY;
    Pipe->ReaderCount = 1;
    Pipe->WriterCount = 1;
    Pipe->Buffer = (U8*)KernelHeapAlloc(Pipe->Capacity);
    if (Pipe->Buffer == NULL) {
        KernelHeapFree(Pipe);
        return DF_RETURN_NO_MEMORY;
    }

    ReadEndpoint = (LPPIPE_ENDPOINT)CreateKernelObject(sizeof(PIPE_ENDPOINT), KOID_PIPE_ENDPOINT);
    WriteEndpoint = (LPPIPE_ENDPOINT)CreateKernelObject(sizeof(PIPE_ENDPOINT), KOID_PIPE_ENDPOINT);
    if (ReadEndpoint == NULL || WriteEndpoint == NULL) {
        if (ReadEndpoint != NULL) {
            ReadEndpoint->TypeID = KOID_NONE;
            KernelHeapFree(ReadEndpoint);
        }

        if (WriteEndpoint != NULL) {
            WriteEndpoint->TypeID = KOID_NONE;
            KernelHeapFree(WriteEndpoint);
        }

        KernelHeapFree(Pipe->Buffer);
        KernelHeapFree(Pipe);
        return DF_RETURN_NO_MEMORY;
    }

    ReadEndpoint->Pipe = Pipe;
    ReadEndpoint->Direction = PIPE_ENDPOINT_DIRECTION_READ;
    WriteEndpoint->Pipe = Pipe;
    WriteEndpoint->Direction = PIPE_ENDPOINT_DIRECTION_WRITE;

    ReadHandle = PointerToHandle((LINEAR)ReadEndpoint);
    WriteHandle = PointerToHandle((LINEAR)WriteEndpoint);
    if (ReadHandle == 0 || WriteHandle == 0) {
        if (ReadHandle != 0) {
            ReleaseHandle(ReadHandle);
        }

        if (WriteHandle != 0) {
            ReleaseHandle(WriteHandle);
        }

        ReadEndpoint->TypeID = KOID_NONE;
        WriteEndpoint->TypeID = KOID_NONE;
        KernelHeapFree(ReadEndpoint);
        KernelHeapFree(WriteEndpoint);
        KernelHeapFree(Pipe->Buffer);
        KernelHeapFree(Pipe);
        return DF_RETURN_NO_MEMORY;
    }

    *OutReadHandle = ReadHandle;
    *OutWriteHandle = WriteHandle;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT PipeReadEndpoint(LPVOID EndpointObject, LPVOID Buffer, UINT Count) {
    LPPIPE_ENDPOINT Endpoint = (LPPIPE_ENDPOINT)EndpointObject;

    if (Buffer == NULL) {
        return 0;
    }

    SAFE_USE_VALID_ID(Endpoint, KOID_PIPE_ENDPOINT) {
        LPPIPE Pipe = Endpoint->Pipe;

        if (Pipe == NULL || Endpoint->Direction != PIPE_ENDPOINT_DIRECTION_READ) {
            return 0;
        }

        return PipeReadInternal(Pipe, Buffer, Count);
    }

    return 0;
}

/************************************************************************/

UINT PipeWriteEndpoint(LPVOID EndpointObject, LPCVOID Buffer, UINT Count) {
    LPPIPE_ENDPOINT Endpoint = (LPPIPE_ENDPOINT)EndpointObject;

    if (Buffer == NULL) {
        return 0;
    }

    SAFE_USE_VALID_ID(Endpoint, KOID_PIPE_ENDPOINT) {
        LPPIPE Pipe = Endpoint->Pipe;

        if (Pipe == NULL || Endpoint->Direction != PIPE_ENDPOINT_DIRECTION_WRITE) {
            return 0;
        }

        return PipeWriteInternal(Pipe, Buffer, Count);
    }

    return 0;
}

/************************************************************************/

UINT PipeCloseEndpoint(LPVOID EndpointObject) {
    LPPIPE_ENDPOINT Endpoint = (LPPIPE_ENDPOINT)EndpointObject;
    LPPIPE Pipe = NULL;
    BOOL DeletePipe = FALSE;

    SAFE_USE_VALID_ID(Endpoint, KOID_PIPE_ENDPOINT) {
        Pipe = Endpoint->Pipe;
        if (Pipe != NULL) {
            LockMutex(&Pipe->Mutex, INFINITY);

            if (Endpoint->Direction == PIPE_ENDPOINT_DIRECTION_READ && Pipe->ReaderCount > 0) {
                Pipe->ReaderCount--;
            } else if (Endpoint->Direction == PIPE_ENDPOINT_DIRECTION_WRITE && Pipe->WriterCount > 0) {
                Pipe->WriterCount--;
            }

            DeletePipe = Pipe->ReaderCount == 0 && Pipe->WriterCount == 0;

            UnlockMutex(&Pipe->Mutex);
        }

        Endpoint->Pipe = NULL;
        Endpoint->TypeID = KOID_NONE;
        KernelHeapFree(Endpoint);

        if (DeletePipe && Pipe != NULL) {
            if (Pipe->Buffer != NULL) {
                KernelHeapFree(Pipe->Buffer);
            }
            KernelHeapFree(Pipe);
        }

        return 1;
    }

    return 0;
}

/************************************************************************/

U32 PipeGetEndpointDirection(LPVOID EndpointObject) {
    LPPIPE_ENDPOINT Endpoint = (LPPIPE_ENDPOINT)EndpointObject;

    SAFE_USE_VALID_ID(Endpoint, KOID_PIPE_ENDPOINT) {
        return Endpoint->Direction;
    }

    return 0;
}

/************************************************************************/
