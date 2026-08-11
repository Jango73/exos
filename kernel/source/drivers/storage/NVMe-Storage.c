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


    NVMe (storage integration)

\************************************************************************/

#include "core/KernelData.h"
#include "drivers/storage/NVMe-Internal.h"
#include "fs/Storage-Transfer-Layer.h"
#include "fs/File-System.h"
#include "text/CoreString.h"
#include "utils/Storage-ID.h"

/************************************************************************/

#define NVME_STORAGE_VER_MAJOR 1
#define NVME_STORAGE_VER_MINOR 0

/************************************************************************/

static UINT NVMeStorageCommands(UINT Function, UINT Parameter);
static UINT NVMeStorageRead(LPIOCONTROL Control);
static UINT NVMeStorageWrite(LPIOCONTROL Control);
static UINT NVMeStorageGetInfo(LPSTORAGEINFO Info);
static UINT NVMeStorageSetAccess(LPSTORAGEACCESS Access);

/************************************************************************/

/**
 * @brief Initialize the per-device NVMe storage driver structure.
 * @param Device NVMe device.
 */
void NVMeInitStorageDriver(LPNVME_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        MemorySet(&Device->StorageDriver, 0, sizeof(Device->StorageDriver));
        Device->StorageDriver.TypeID = KOID_DRIVER;
        Device->StorageDriver.References = 1;
        Device->StorageDriver.Type = DRIVER_TYPE_NVME_STORAGE;
        Device->StorageDriver.VersionMajor = NVME_STORAGE_VER_MAJOR;
        Device->StorageDriver.VersionMinor = NVME_STORAGE_VER_MINOR;
        StringCopy(Device->StorageDriver.Designer, TEXT("Jango73"));
        StringCopy(Device->StorageDriver.Manufacturer, TEXT("NVMe"));
        StringCopy(Device->StorageDriver.Product, TEXT("NVMe Storage"));
        Device->StorageDriver.Command = NVMeStorageCommands;
        Device->StorageDriver.EnumDomainCount = 0;
    }
}

/************************************************************************/

/**
 * @brief Check whether a buffer is 4 KiB aligned.
 * @param Buffer Buffer pointer.
 * @return TRUE when aligned, FALSE otherwise.
 */
static BOOL NVMeIsAlignedBuffer(LPVOID Buffer) {
    return (((LINEAR)Buffer & (N_4KB - 1)) == 0);
}

/************************************************************************/

/**
 * @brief Check whether a buffer is physically contiguous.
 * @param Buffer Buffer pointer.
 * @param TransferBytes Byte count.
 * @return TRUE when contiguous, FALSE otherwise.
 */
static BOOL NVMeIsContiguousBuffer(LPVOID Buffer, U32 TransferBytes) {
    if (Buffer == NULL || TransferBytes == 0) {
        return FALSE;
    }

    LINEAR BufferLinear = (LINEAR)Buffer;
    PHYSICAL BasePhys = MapLinearToPhysical(BufferLinear);
    if (BasePhys == 0) {
        return FALSE;
    }

    for (UINT Offset = 0; Offset < TransferBytes; Offset += N_4KB) {
        LINEAR Linear = BufferLinear + (LINEAR)Offset;
        PHYSICAL Physical = MapLinearToPhysical(Linear);
        if (Physical != (BasePhys + (PHYSICAL)Offset)) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Read sectors with a bounce buffer when needed.
 * @param Device NVMe device.
 * @param NamespaceId Namespace identifier.
 * @param Lba Starting logical block address.
 * @param SectorCount Number of sectors.
 * @param Buffer Destination buffer.
 * @param BufferBytes Buffer size in bytes.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeReadSectorsBuffered(
    LPNVME_DEVICE Device,
    U32 NamespaceId,
    U64 Lba,
    U32 SectorCount,
    U32 BytesPerSector,
    LPVOID Buffer,
    U32 BufferBytes) {
    if (Device == NULL || Buffer == NULL) {
        return FALSE;
    }

    if (BytesPerSector == 0 || SectorCount > (0xFFFFFFFF / BytesPerSector)) {
        return FALSE;
    }

    U32 TransferBytes = SectorCount * BytesPerSector;
    if (BufferBytes < TransferBytes) {
        return FALSE;
    }

    if (TransferBytes > (2 * N_4KB)) {
        return FALSE;
    }

    if (NVMeIsAlignedBuffer(Buffer) && NVMeIsContiguousBuffer(Buffer, TransferBytes)) {
        return NVMeReadSectors(Device, NamespaceId, Lba, SectorCount, Buffer, BufferBytes);
    }

    U32 RawSize = TransferBytes + N_4KB;
    LPVOID Raw = KernelHeapAlloc(RawSize);
    if (Raw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Raw;
    LINEAR AlignedBase = (LINEAR)((RawBase + (N_4KB - 1)) & ~(N_4KB - 1));
    LPVOID AlignedBuffer = (LPVOID)AlignedBase;
    MemorySet(AlignedBuffer, 0, TransferBytes);

    BOOL Result = NVMeReadSectors(Device, NamespaceId, Lba, SectorCount, AlignedBuffer, TransferBytes);
    if (Result) {
        MemoryCopy(Buffer, AlignedBuffer, TransferBytes);
    }

    KernelHeapFree(Raw);
    return Result;
}

/************************************************************************/

/**
 * @brief Write sectors with a bounce buffer when needed.
 * @param Device NVMe device.
 * @param NamespaceId Namespace identifier.
 * @param Lba Starting logical block address.
 * @param SectorCount Number of sectors.
 * @param Buffer Source buffer.
 * @param BufferBytes Buffer size in bytes.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL NVMeWriteSectorsBuffered(
    LPNVME_DEVICE Device,
    U32 NamespaceId,
    U64 Lba,
    U32 SectorCount,
    U32 BytesPerSector,
    LPCVOID Buffer,
    U32 BufferBytes) {
    if (Device == NULL || Buffer == NULL) {
        return FALSE;
    }

    if (BytesPerSector == 0 || SectorCount > (0xFFFFFFFF / BytesPerSector)) {
        return FALSE;
    }

    U32 TransferBytes = SectorCount * BytesPerSector;
    if (BufferBytes < TransferBytes) {
        return FALSE;
    }

    if (TransferBytes > (2 * N_4KB)) {
        return FALSE;
    }

    if (NVMeIsAlignedBuffer((LPVOID)Buffer) && NVMeIsContiguousBuffer((LPVOID)Buffer, TransferBytes)) {
        return NVMeWriteSectors(Device, NamespaceId, Lba, SectorCount, Buffer, BufferBytes);
    }

    U32 RawSize = TransferBytes + N_4KB;
    LPVOID Raw = KernelHeapAlloc(RawSize);
    if (Raw == NULL) {
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Raw;
    LINEAR AlignedBase = (LINEAR)((RawBase + (N_4KB - 1)) & ~(N_4KB - 1));
    LPVOID AlignedBuffer = (LPVOID)AlignedBase;
    MemorySet(AlignedBuffer, 0, TransferBytes);
    MemoryCopy(AlignedBuffer, Buffer, TransferBytes);

    BOOL Result = NVMeWriteSectors(Device, NamespaceId, Lba, SectorCount, AlignedBuffer, TransferBytes);

    KernelHeapFree(Raw);
    return Result;
}

/************************************************************************/

/**
 * @brief Create a storage object for a namespace.
 * @param Device NVMe device.
 * @param NamespaceId Namespace identifier.
 * @param NumSectors Namespace size in sectors.
 * @return Storage object or NULL on failure.
 */
static LPNVME_STORAGE NVMeCreateStorage(LPNVME_DEVICE Device, U32 NamespaceId, U64 NumSectors, U32 BytesPerSector) {
    if (Device == NULL || NamespaceId == 0) {
        return NULL;
    }

    LPNVME_STORAGE Storage = (LPNVME_STORAGE)CreateKernelObject(sizeof(NVME_STORAGE), KOID_STORAGE);
    if (Storage == NULL) {
        return NULL;
    }

    Storage->Header.Driver = &Device->StorageDriver;
    Storage->Controller = Device;
    Storage->NamespaceId = NamespaceId;
    Storage->NumSectors = NumSectors;
    Storage->BytesPerSector = BytesPerSector;
    Storage->Access = 0;

    StorageIdSetIdentity((LPSTORAGE_UNIT)Storage, NULL, Device->Model, Device->Serial);
    StorageIdEnsure((LPSTORAGE_UNIT)Storage);

    return Storage;
}

/************************************************************************/

/**
 * @brief Register NVMe namespaces as storage units and mount partitions.
 * @param Device NVMe device.
 * @return TRUE on success, FALSE on failure.
 */
BOOL NVMeRegisterNamespaces(LPNVME_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        UINT MaxIds = (N_4KB / sizeof(U32));
        U32* NamespaceIds = (U32*)KernelHeapAlloc(N_4KB);
        if (NamespaceIds == NULL) {
            return FALSE;
        }

        MemorySet(NamespaceIds, 0, N_4KB);

        UINT Count = 0;
        if (!NVMeIdentifyNamespaceList(Device, NamespaceIds, MaxIds, &Count)) {
            WARNING(TEXT("Identify namespace list failed, fallback to NSID=1"));
            NamespaceIds[0] = 1;
            Count = 1;
        }

        if (Count == 0) {
            WARNING(TEXT("Namespace list is empty, fallback to NSID=1"));
            NamespaceIds[0] = 1;
            Count = 1;
        }

        BOOL RegisteredAny = FALSE;
        for (UINT Index = 0; Index < Count; Index++) {
            U32 NamespaceId = NamespaceIds[Index];
            U64 NumSectors = U64_0;
            U32 BytesPerSector = 0;
            if (!NVMeIdentifyNamespace(Device, NamespaceId, &NumSectors, &BytesPerSector)) {
                WARNING(TEXT("Identify namespace failed NSID=%u"), (U32)NamespaceId);
                continue;
            }

            if (BytesPerSector == 0) {
                WARNING(TEXT("Invalid bytes per sector NSID=%u"), (U32)NamespaceId);
                continue;
            }

            LPNVME_STORAGE Storage = NVMeCreateStorage(Device, NamespaceId, NumSectors, BytesPerSector);
            if (Storage == NULL) {
                WARNING(TEXT("Storage allocation failed NSID=%u"), (U32)NamespaceId);
                continue;
            }

            if (Device->LogicalBlockSize == 0 || Device->LogicalBlockSize == SECTOR_SIZE) {
                Device->LogicalBlockSize = BytesPerSector;
            }

            // Attach the generic transfer layer (sector cache + chunking).
            // The transfer limit mirrors the buffered transfer cap.
            if (!StorageTransferLayerInit((LPSTORAGE_UNIT)Storage, (2 * N_4KB) / BytesPerSector, NULL)) {
                WARNING(TEXT("Unable to attach transfer layer NSID=%u"), (U32)NamespaceId);
                ReleaseKernelObject(Storage);
                continue;
            }

            LPLIST StorageList = GetStorageList();
            if (StorageList == NULL || !ListAddItem(StorageList, Storage)) {
                ERROR(TEXT("Unable to register storage NSID=%u"), (U32)NamespaceId);
                ReleaseKernelObject(Storage);
                continue;
            }

            RegisteredAny = TRUE;

            if (FileSystemReady()) {
                if (!MountStoragePartitions((LPSTORAGE_UNIT)Storage, NULL, 0)) {
                    WARNING(TEXT("Partition mount failed NSID=%u"), (U32)NamespaceId);
                }
            } else {
            }
        }

        KernelHeapFree(NamespaceIds);
        return RegisteredAny;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Driver command handler for NVMe storage access.
 * @param Function Function code.
 * @param Parameter Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT NVMeStorageCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_STORAGE_RESET:
            return DF_RETURN_SUCCESS;
        case DF_STORAGE_READ:
            return NVMeStorageRead((LPIOCONTROL)Parameter);
        case DF_STORAGE_WRITE:
            return NVMeStorageWrite((LPIOCONTROL)Parameter);
        case DF_STORAGE_GETINFO:
            return NVMeStorageGetInfo((LPSTORAGEINFO)Parameter);
        case DF_STORAGE_SETACCESS:
            return NVMeStorageSetAccess((LPSTORAGEACCESS)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Read sectors from an NVMe storage.
 * @param Control IO control structure describing request.
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
static UINT NVMeStorageRead(LPIOCONTROL Control) {
    if (Control == NULL || Control->Storage == NULL || Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_STORAGE Storage = (LPNVME_STORAGE)Control->Storage;
    SAFE_USE_VALID_ID((LPLISTNODE)Storage, KOID_STORAGE) {
        if (Storage->Controller == NULL || Control->NumSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        SAFE_USE_VALID_ID(Storage->Controller, KOID_PCIDEVICE) {
            if (Storage->BytesPerSector == 0 || Control->NumSectors > (0xFFFFFFFF / Storage->BytesPerSector)) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 TotalBytes = Control->NumSectors * Storage->BytesPerSector;
            if (Control->BufferSize < TotalBytes) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 MaxSectors = (2 * N_4KB) / Storage->BytesPerSector;
            if (MaxSectors == 0) {
                return DF_RETURN_BAD_PARAMETER;
            }
            U32 Remaining = Control->NumSectors;
            U8* Out = (U8*)Control->Buffer;
            U64 Lba = U64_Make(Control->SectorHigh, Control->SectorLow);

            while (Remaining > 0) {
                U32 Chunk = Remaining > MaxSectors ? MaxSectors : Remaining;
                U32 ChunkBytes = Chunk * Storage->BytesPerSector;

                if (!NVMeReadSectorsBuffered(
                        Storage->Controller,
                        Storage->NamespaceId,
                        Lba,
                        Chunk,
                        Storage->BytesPerSector,
                        Out,
                        ChunkBytes)) {
                    WARNING(
                        TEXT("Read failed LBA=%x:%x sectors=%u"),
                        (U32)U64_High32(Lba),
                        (U32)U64_Low32(Lba),
                        (U32)Chunk);
                    return DF_RETURN_UNEXPECTED;
                }

                Lba = U64_Add(Lba, U64_FromU32(Chunk));
                Out += ChunkBytes;
                Remaining -= Chunk;
            }

            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Write sectors to an NVMe storage.
 * @param Control IO control structure describing request.
 * @return DF_RETURN_* code.
 */
static UINT NVMeStorageWrite(LPIOCONTROL Control) {
    if (Control == NULL || Control->Storage == NULL || Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_STORAGE Storage = (LPNVME_STORAGE)Control->Storage;
    SAFE_USE_VALID_ID((LPLISTNODE)Storage, KOID_STORAGE) {
        if (Storage->Controller == NULL || Control->NumSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        if (Storage->Access & STORAGE_ACCESS_READONLY) {
            return DF_RETURN_NO_PERMISSION;
        }

        SAFE_USE_VALID_ID(Storage->Controller, KOID_PCIDEVICE) {
            if (Storage->BytesPerSector == 0 || Control->NumSectors > (0xFFFFFFFF / Storage->BytesPerSector)) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 TotalBytes = Control->NumSectors * Storage->BytesPerSector;
            if (Control->BufferSize < TotalBytes) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 MaxSectors = (2 * N_4KB) / Storage->BytesPerSector;
            if (MaxSectors == 0) {
                return DF_RETURN_BAD_PARAMETER;
            }
            U32 Remaining = Control->NumSectors;
            U8* In = (U8*)Control->Buffer;
            U64 Lba = U64_Make(Control->SectorHigh, Control->SectorLow);

            while (Remaining > 0) {
                U32 Chunk = Remaining > MaxSectors ? MaxSectors : Remaining;
                U32 ChunkBytes = Chunk * Storage->BytesPerSector;

                if (!NVMeWriteSectorsBuffered(
                        Storage->Controller,
                        Storage->NamespaceId,
                        Lba,
                        Chunk,
                        Storage->BytesPerSector,
                        In,
                        ChunkBytes)) {
                    WARNING(
                        TEXT("Write failed LBA=%x:%x sectors=%u"),
                        (U32)U64_High32(Lba),
                        (U32)U64_Low32(Lba),
                        (U32)Chunk);
                    return DF_RETURN_UNEXPECTED;
                }

                Lba = U64_Add(Lba, U64_FromU32(Chunk));
                In += ChunkBytes;
                Remaining -= Chunk;
            }

            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Retrieve storage information for an NVMe namespace.
 * @param Info Output structure to populate.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static UINT NVMeStorageGetInfo(LPSTORAGEINFO Info) {
    if (Info == NULL || Info->Storage == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_STORAGE Storage = (LPNVME_STORAGE)Info->Storage;
    SAFE_USE_VALID_ID((LPLISTNODE)Storage, KOID_STORAGE) {
        Info->Type = DRIVER_TYPE_NVME_STORAGE;
        Info->Removable = 0;
        Info->BytesPerSector = Storage->BytesPerSector;
        Info->NumSectors = Storage->NumSectors;
        Info->Access = Storage->Access;

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Set access parameters for an NVMe storage.
 * @param Access Access parameters to store.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static UINT NVMeStorageSetAccess(LPSTORAGEACCESS Access) {
    if (Access == NULL || Access->Storage == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_STORAGE Storage = (LPNVME_STORAGE)Access->Storage;
    SAFE_USE_VALID_ID((LPLISTNODE)Storage, KOID_STORAGE) {
        Storage->Access = Access->Access;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}
