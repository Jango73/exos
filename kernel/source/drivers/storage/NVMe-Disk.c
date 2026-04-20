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


    NVMe (disk integration)

\************************************************************************/

#include "drivers/storage/NVMe-Internal.h"

#include "text/CoreString.h"
#include "fs/FileSystem.h"
#include "core/KernelData.h"

/************************************************************************/

#define NVME_DISK_VER_MAJOR 1
#define NVME_DISK_VER_MINOR 0

/************************************************************************/

static UINT NVMeDiskCommands(UINT Function, UINT Parameter);
static UINT NVMeDiskRead(LPIOCONTROL Control);
static UINT NVMeDiskWrite(LPIOCONTROL Control);
static UINT NVMeDiskGetInfo(LPDISKINFO Info);
static UINT NVMeDiskSetAccess(LPDISKACCESS Access);

/************************************************************************/

/**
 * @brief Initialize the per-device NVMe disk driver structure.
 * @param Device NVMe device.
 */
void NVMeInitDiskDriver(LPNVME_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        MemorySet(&Device->DiskDriver, 0, sizeof(Device->DiskDriver));
        Device->DiskDriver.TypeID = KOID_DRIVER;
        Device->DiskDriver.References = 1;
        Device->DiskDriver.Type = DRIVER_TYPE_NVME_STORAGE;
        Device->DiskDriver.VersionMajor = NVME_DISK_VER_MAJOR;
        Device->DiskDriver.VersionMinor = NVME_DISK_VER_MINOR;
        StringCopy(Device->DiskDriver.Designer, TEXT("Jango73"));
        StringCopy(Device->DiskDriver.Manufacturer, TEXT("NVMe"));
        StringCopy(Device->DiskDriver.Product, TEXT("NVMe Disk"));
        Device->DiskDriver.Command = NVMeDiskCommands;
        Device->DiskDriver.EnumDomainCount = 0;
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
static BOOL NVMeReadSectorsBuffered(LPNVME_DEVICE Device, U32 NamespaceId, U64 Lba, U32 SectorCount,
                                    U32 BytesPerSector, LPVOID Buffer, U32 BufferBytes) {
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
static BOOL NVMeWriteSectorsBuffered(LPNVME_DEVICE Device, U32 NamespaceId, U64 Lba, U32 SectorCount,
                                     U32 BytesPerSector, LPCVOID Buffer, U32 BufferBytes) {
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
 * @brief Create a disk object for a namespace.
 * @param Device NVMe device.
 * @param NamespaceId Namespace identifier.
 * @param NumSectors Namespace size in sectors.
 * @return Disk object or NULL on failure.
 */
static LPNVME_DISK NVMeCreateDisk(LPNVME_DEVICE Device, U32 NamespaceId, U64 NumSectors, U32 BytesPerSector) {
    if (Device == NULL || NamespaceId == 0) {
        return NULL;
    }

    LPNVME_DISK Disk = (LPNVME_DISK)CreateKernelObject(sizeof(NVME_DISK), KOID_DISK);
    if (Disk == NULL) {
        return NULL;
    }

    Disk->Header.Driver = &Device->DiskDriver;
    Disk->Controller = Device;
    Disk->NamespaceId = NamespaceId;
    Disk->NumSectors = NumSectors;
    Disk->BytesPerSector = BytesPerSector;
    Disk->Access = 0;

    return Disk;
}

/************************************************************************/

/**
 * @brief Register NVMe namespaces as disks and mount partitions.
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

            LPNVME_DISK Disk = NVMeCreateDisk(Device, NamespaceId, NumSectors, BytesPerSector);
            if (Disk == NULL) {
                WARNING(TEXT("Disk allocation failed NSID=%u"), (U32)NamespaceId);
                continue;
            }

            if (Device->LogicalBlockSize == 0 || Device->LogicalBlockSize == SECTOR_SIZE) {
                Device->LogicalBlockSize = BytesPerSector;
            }

            LPLIST DiskList = GetDiskList();
            if (DiskList == NULL || !ListAddItem(DiskList, Disk)) {
                ERROR(TEXT("Unable to register disk NSID=%u"), (U32)NamespaceId);
                ReleaseKernelObject(Disk);
                continue;
            }

            RegisteredAny = TRUE;

            if (FileSystemReady()) {
                if (!MountDiskPartitions((LPSTORAGE_UNIT)Disk, NULL, 0)) {
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
 * @brief Driver command handler for NVMe disk access.
 * @param Function Function code.
 * @param Parameter Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT NVMeDiskCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_DISK_RESET:
            return DF_RETURN_SUCCESS;
        case DF_DISK_READ:
            return NVMeDiskRead((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return NVMeDiskWrite((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return NVMeDiskGetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return NVMeDiskSetAccess((LPDISKACCESS)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Read sectors from an NVMe disk.
 * @param Control IO control structure describing request.
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
static UINT NVMeDiskRead(LPIOCONTROL Control) {
    if (Control == NULL || Control->Disk == NULL || Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_DISK Disk = (LPNVME_DISK)Control->Disk;
    SAFE_USE_VALID_ID((LPLISTNODE)Disk, KOID_DISK) {
        if (Disk->Controller == NULL || Control->NumSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        SAFE_USE_VALID_ID(Disk->Controller, KOID_PCIDEVICE) {
            if (Disk->BytesPerSector == 0 || Control->NumSectors > (0xFFFFFFFF / Disk->BytesPerSector)) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 TotalBytes = Control->NumSectors * Disk->BytesPerSector;
            if (Control->BufferSize < TotalBytes) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 MaxSectors = (2 * N_4KB) / Disk->BytesPerSector;
            if (MaxSectors == 0) {
                return DF_RETURN_BAD_PARAMETER;
            }
            U32 Remaining = Control->NumSectors;
            U8* Out = (U8*)Control->Buffer;
            U64 Lba = U64_Make(Control->SectorHigh, Control->SectorLow);

            while (Remaining > 0) {
                U32 Chunk = Remaining > MaxSectors ? MaxSectors : Remaining;
                U32 ChunkBytes = Chunk * Disk->BytesPerSector;

                if (!NVMeReadSectorsBuffered(Disk->Controller,
                                             Disk->NamespaceId,
                                             Lba,
                                             Chunk,
                                             Disk->BytesPerSector,
                                             Out,
                                             ChunkBytes)) {
                    WARNING(TEXT("Read failed LBA=%x:%x sectors=%u"),
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
 * @brief Write sectors to an NVMe disk.
 * @param Control IO control structure describing request.
 * @return DF_RETURN_* code.
 */
static UINT NVMeDiskWrite(LPIOCONTROL Control) {
    if (Control == NULL || Control->Disk == NULL || Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_DISK Disk = (LPNVME_DISK)Control->Disk;
    SAFE_USE_VALID_ID((LPLISTNODE)Disk, KOID_DISK) {
        if (Disk->Controller == NULL || Control->NumSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        if (Disk->Access & DISK_ACCESS_READONLY) {
            return DF_RETURN_NO_PERMISSION;
        }

        SAFE_USE_VALID_ID(Disk->Controller, KOID_PCIDEVICE) {
            if (Disk->BytesPerSector == 0 || Control->NumSectors > (0xFFFFFFFF / Disk->BytesPerSector)) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 TotalBytes = Control->NumSectors * Disk->BytesPerSector;
            if (Control->BufferSize < TotalBytes) {
                return DF_RETURN_BAD_PARAMETER;
            }

            U32 MaxSectors = (2 * N_4KB) / Disk->BytesPerSector;
            if (MaxSectors == 0) {
                return DF_RETURN_BAD_PARAMETER;
            }
            U32 Remaining = Control->NumSectors;
            U8* In = (U8*)Control->Buffer;
            U64 Lba = U64_Make(Control->SectorHigh, Control->SectorLow);

            while (Remaining > 0) {
                U32 Chunk = Remaining > MaxSectors ? MaxSectors : Remaining;
                U32 ChunkBytes = Chunk * Disk->BytesPerSector;

                if (!NVMeWriteSectorsBuffered(Disk->Controller,
                                              Disk->NamespaceId,
                                              Lba,
                                              Chunk,
                                              Disk->BytesPerSector,
                                              In,
                                              ChunkBytes)) {
                    WARNING(TEXT("Write failed LBA=%x:%x sectors=%u"),
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
 * @brief Retrieve disk information for an NVMe namespace.
 * @param Info Output structure to populate.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static UINT NVMeDiskGetInfo(LPDISKINFO Info) {
    if (Info == NULL || Info->Disk == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_DISK Disk = (LPNVME_DISK)Info->Disk;
    SAFE_USE_VALID_ID((LPLISTNODE)Disk, KOID_DISK) {
        Info->Type = DRIVER_TYPE_NVME_STORAGE;
        Info->Removable = 0;
        Info->BytesPerSector = Disk->BytesPerSector;
        Info->NumSectors = Disk->NumSectors;
        Info->Access = Disk->Access;

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Set access parameters for an NVMe disk.
 * @param Access Access parameters to store.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_BAD_PARAMETER otherwise.
 */
static UINT NVMeDiskSetAccess(LPDISKACCESS Access) {
    if (Access == NULL || Access->Disk == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPNVME_DISK Disk = (LPNVME_DISK)Access->Disk;
    SAFE_USE_VALID_ID((LPLISTNODE)Disk, KOID_DISK) {
        Disk->Access = Access->Access;
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_BAD_PARAMETER;
}
