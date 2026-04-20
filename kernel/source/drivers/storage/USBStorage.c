
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


    USB Mass Storage (BOT)

\************************************************************************/

#include "User.h"
#include "console/Console.h"
#include "core/Kernel.h"
#include "drivers/storage/USBStorage-Private.h"
#include "fs/FileSystem.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Task-Messaging.h"
#include "sync/DeferredWork.h"
#include "system/Clock.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

#define DF_RETURN_HARDWARE 0x00001001
#define DF_RETURN_NODEVICE 0x00001004

UINT USBStorageCommands(UINT Function, UINT Parameter);

static USB_MASS_STORAGE_STATE DATA_SECTION USBStorageState = {
    .Initialized = FALSE,
    .PollToken = {.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT},
    .RetryDelay = 0,
    .ScanLogLimiter = {0}};

static DRIVER DATA_SECTION USBStorageDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_USB_STORAGE,
    .VersionMajor = USB_MASS_STORAGE_VER_MAJOR,
    .VersionMinor = USB_MASS_STORAGE_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "USB Mass Storage",
    .Alias = "usb_storage",
    .Flags = 0,
    .Command = USBStorageCommands,
    .CustomData = &USBStorageState};

/************************************************************************/

/**
 * @brief Emit rate-limited scan diagnostics for unsupported mass-storage interfaces.
 * @param UsbDevice USB device.
 * @param Interface Interface descriptor.
 * @param Reason Short reason label.
 */
static void USBStorageLogScan(LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_INTERFACE Interface, LPCSTR Reason) {
    U32 Suppressed = 0;

    if (UsbDevice == NULL || Interface == NULL) {
        return;
    }

    if (!RateLimiterShouldTrigger(&USBStorageState.ScanLogLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    WARNING(
        TEXT("Port=%u Addr=%u If=%u Class=%x/%x/%x reason=%s suppressed=%u"),
        (U32)UsbDevice->PortNumber, (U32)UsbDevice->Address, (U32)Interface->Number, (U32)Interface->InterfaceClass,
        (U32)Interface->InterfaceSubClass, (U32)Interface->InterfaceProtocol, (Reason != NULL) ? Reason : TEXT("?"),
        Suppressed);
}

/************************************************************************/

static UINT USBStorageReportMounts(LPUSB_MASS_STORAGE_DEVICE Device, LPLISTNODE PreviousLast) {
    LPLIST FileSystemList = GetFileSystemList();
    UINT MountedCount = 0;
    LPLISTNODE Node = NULL;

    if (Device == NULL || FileSystemList == NULL) {
        return 0;
    }

    if (PreviousLast != NULL) {
        Node = PreviousLast->Next;
    } else {
        Node = FileSystemList->First;
    }

    for (; Node; Node = Node->Next) {
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        if (FileSystemGetStorageUnit(FileSystem) != (LPSTORAGE_UNIT)Device) {
            continue;
        }

        MountedCount++;
    }

    return MountedCount;
}

/************************************************************************/

/**
 * @brief Attempt partition mounting for one USB storage device when possible.
 * @param Device USB storage device.
 * @return Number of newly mounted filesystems.
 */
static UINT USBStorageTryMountPending(LPUSB_MASS_STORAGE_DEVICE Device) {
    LPLIST FileSystemList = NULL;
    LPLISTNODE PreviousLast = NULL;
    UINT MountedCount = 0;

    if (Device == NULL || Device->Ready == FALSE || Device->MountPending == FALSE) {
        return 0;
    }
    if (!FileSystemReady()) {
        return 0;
    }

    FileSystemList = GetFileSystemList();
    PreviousLast = (FileSystemList != NULL) ? FileSystemList->Last : NULL;

    if (!MountDiskPartitions((LPSTORAGE_UNIT)Device, NULL, 0)) {
        WARNING(TEXT("Partition mount failed"));
        return 0;
    }

    MountedCount = USBStorageReportMounts(Device, PreviousLast);
    if (MountedCount != 0) {
        Device->MountPending = FALSE;
        if (Device->ListEntry != NULL) {
            BroadcastProcessMessage(ETM_USB_MASS_STORAGE_MOUNTED, (U32)Device->ListEntry->Address, Device->BlockCount);
        }
    }

    return MountedCount;
}

/************************************************************************/

/**
 * @brief Unmount and release filesystems associated with a USB disk.
 * @param Disk USB disk to detach.
 */
static void USBStorageDetachFileSystems(LPSTORAGE_UNIT Disk, U32 UsbAddress) {
    LPLIST FileSystemList = GetFileSystemList();
    LPLIST UnusedFileSystemList = GetUnusedFileSystemList();
    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    UINT UnmountedCount = 0;
    UINT UnusedCount = 0;

    if (Disk == NULL || FileSystemList == NULL || UnusedFileSystemList == NULL || GlobalInfo == NULL) {
        return;
    }

    for (LPLISTNODE Node = FileSystemList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        LPSTORAGE_UNIT FileSystemDisk = FileSystemGetStorageUnit(FileSystem);

        if (FileSystemDisk == Disk) {
            SystemFSUnmountFileSystem(FileSystem);
            if (StringCompare(GlobalInfo->ActivePartitionName, FileSystem->Name) == 0) {
                StringClear(GlobalInfo->ActivePartitionName);
            }
            ReleaseKernelObject(FileSystem);
            UnmountedCount++;
        }

        Node = Next;
    }

    for (LPLISTNODE Node = UnusedFileSystemList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        LPSTORAGE_UNIT FileSystemDisk = FileSystemGetStorageUnit(FileSystem);

        if (FileSystemDisk == Disk) {
            ReleaseKernelObject(FileSystem);
            UnusedCount++;
        }

        Node = Next;
    }

    if (UnmountedCount > 0 || UnusedCount > 0) {
        BroadcastProcessMessage(ETM_USB_MASS_STORAGE_UNMOUNTED, UsbAddress, 0);
    }
}

/************************************************************************/

/**
 * @brief Detach a USB mass storage device and release its resources.
 * @param Device Device to detach.
 */
static void USBStorageDetachDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Ready = FALSE;

    if (Device->ListEntry != NULL) {
        USBStorageDetachFileSystems((LPSTORAGE_UNIT)Device, (U32)Device->ListEntry->Address);
    } else {
        USBStorageDetachFileSystems((LPSTORAGE_UNIT)Device, 0);
    }

    if (Device->InputOutputBufferLinear != 0) {
        FreeRegion(Device->InputOutputBufferLinear, PAGE_SIZE);
        Device->InputOutputBufferLinear = 0;
    }
    if (Device->InputOutputBufferPhysical != 0) {
        FreePhysicalPage(Device->InputOutputBufferPhysical);
        Device->InputOutputBufferPhysical = 0;
    }

    if (Device->ListEntry != NULL) {
        Device->ListEntry->Present = FALSE;
        Device->ListEntry->Device = NULL;
        ReleaseKernelObject(Device->ListEntry);
        Device->ListEntry = NULL;
    }

    ReleaseKernelObject(Device);
}

/************************************************************************/

/**
 * @brief Retrieve the USB mass storage driver descriptor.
 * @return Pointer to the USB mass storage driver.
 */
LPDRIVER USBStorageGetDriver(void) { return &USBStorageDriver; }

/************************************************************************/

/**
 * @brief Allocate and initialize a USB mass storage device object.
 * @return Pointer to allocated device or NULL on failure.
 */
static LPUSB_MASS_STORAGE_DEVICE USBStorageAllocateDevice(void) {
    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)KernelHeapAlloc(sizeof(USB_MASS_STORAGE_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(USB_MASS_STORAGE_DEVICE));
    Device->Disk.TypeID = KOID_DISK;
    Device->Disk.References = 1;
    Device->Disk.Next = NULL;
    Device->Disk.Prev = NULL;
    Device->Disk.Driver = &USBStorageDriver;
    Device->Access = 0;
    Device->Tag = 1;
    Device->Ready = FALSE;
    return Device;
}

/************************************************************************/

/**
 * @brief Acquire USB device/interface/endpoint references for a mass storage device.
 * @param Device USB mass storage device context.
 */
static void USBStorageAcquireReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL || Device->ReferencesHeld) {
        return;
    }

    XHCI_ReferenceUsbDevice(Device->UsbDevice);
    XHCI_ReferenceUsbInterface(Device->Interface);
    XHCI_ReferenceUsbEndpoint(Device->BulkInEndpoint);
    XHCI_ReferenceUsbEndpoint(Device->BulkOutEndpoint);
    Device->ReferencesHeld = TRUE;
}

/************************************************************************/

/**
 * @brief Release USB device/interface/endpoint references for a mass storage device.
 * @param Device USB mass storage device context.
 */
static void USBStorageReleaseReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL || !Device->ReferencesHeld) {
        return;
    }

    XHCI_ReleaseUsbEndpoint(Device->BulkOutEndpoint);
    XHCI_ReleaseUsbEndpoint(Device->BulkInEndpoint);
    XHCI_ReleaseUsbInterface(Device->Interface);
    XHCI_ReleaseUsbDevice(Device->UsbDevice);
    Device->ReferencesHeld = FALSE;
}

/************************************************************************/

/**
 * @brief Free a USB mass storage device object and its resources.
 * @param Device Device to free.
 */
static void USBStorageFreeDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    USBStorageReleaseReferences(Device);

    if (Device->InputOutputBufferLinear != 0) {
        FreeRegion(Device->InputOutputBufferLinear, PAGE_SIZE);
        Device->InputOutputBufferLinear = 0;
    }
    if (Device->InputOutputBufferPhysical != 0) {
        FreePhysicalPage(Device->InputOutputBufferPhysical);
        Device->InputOutputBufferPhysical = 0;
    }

    if (Device->ListEntry != NULL) {
        Device->ListEntry->Present = FALSE;
        Device->ListEntry->Device = NULL;
        ReleaseKernelObject(Device->ListEntry);
        Device->ListEntry = NULL;
    }

    KernelHeapFree(Device);
}

/************************************************************************/

/**
 * @brief Initialize and register a detected USB mass storage device.
 * @param Controller xHCI controller.
 * @param UsbDevice USB device state.
 * @param Interface Mass storage interface.
 * @param BulkInEndpoint Bulk IN endpoint.
 * @param BulkOutEndpoint Bulk OUT endpoint.
 * @return TRUE on success.
 */
static BOOL USBStorageStartDevice(
    LPXHCI_DEVICE Controller, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_INTERFACE Interface,
    LPXHCI_USB_ENDPOINT BulkInEndpoint, LPXHCI_USB_ENDPOINT BulkOutEndpoint) {
    if (Controller == NULL || UsbDevice == NULL || Interface == NULL || BulkInEndpoint == NULL ||
        BulkOutEndpoint == NULL) {
        return FALSE;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = USBStorageAllocateDevice();
    if (Device == NULL) {
        ERROR(TEXT("Device allocation failed"));
        return FALSE;
    }

    Device->Controller = Controller;
    Device->UsbDevice = UsbDevice;
    Device->Interface = Interface;
    Device->BulkInEndpoint = BulkInEndpoint;
    Device->BulkOutEndpoint = BulkOutEndpoint;
    Device->InterfaceNumber = Interface->Number;
    USBStorageAcquireReferences(Device);

    DEBUG(
        TEXT("Begin Port=%u Addr=%u Slot=%x If=%u Class=%x/%x/%x Vid=%x Pid=%x BulkOut=%x "
             "Attr=%x MPS=%u BulkIn=%x Attr=%x MPS=%u"),
        (U32)UsbDevice->PortNumber, (U32)UsbDevice->Address, (U32)UsbDevice->SlotId, (U32)Interface->Number,
        (U32)Interface->InterfaceClass, (U32)Interface->InterfaceSubClass, (U32)Interface->InterfaceProtocol,
        (U32)UsbDevice->DeviceDescriptor.VendorID, (U32)UsbDevice->DeviceDescriptor.ProductID,
        (U32)BulkOutEndpoint->Address, (U32)BulkOutEndpoint->Attributes, (U32)BulkOutEndpoint->MaxPacketSize,
        (U32)BulkInEndpoint->Address, (U32)BulkInEndpoint->Attributes, (U32)BulkInEndpoint->MaxPacketSize);

    if (!XHCI_AddBulkEndpointPair(Controller, UsbDevice, BulkOutEndpoint, BulkInEndpoint)) {
        ERROR(
            TEXT("Bulk endpoint pair setup failed Port=%u Addr=%u Slot=%x OutEp=%x MPS=%u "
                 "InEp=%x MPS=%u"),
            (U32)UsbDevice->PortNumber, (U32)UsbDevice->Address, (U32)UsbDevice->SlotId, (U32)BulkOutEndpoint->Address,
            (U32)BulkOutEndpoint->MaxPacketSize, (U32)BulkInEndpoint->Address, (U32)BulkInEndpoint->MaxPacketSize);
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (!XHCI_AllocPage(
            TEXT("USBStorageInputOutput"), &Device->InputOutputBufferPhysical, &Device->InputOutputBufferLinear)) {
        ERROR(TEXT("IO buffer allocation failed"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (!USBStorageInquiry(Device)) {
        WARNING(TEXT("INQUIRY failed, attempting reset"));
        if (!USBStorageResetRecovery(Device) || !USBStorageInquiry(Device)) {
            ERROR(TEXT("INQUIRY failed"));
            USBStorageFreeDevice(Device);
            return FALSE;
        }
    }

    if (!USBStorageReadCapacity(Device)) {
        WARNING(TEXT("READ CAPACITY failed, attempting reset"));
        (void)USBStorageRequestSense(Device);
        if (!USBStorageResetRecovery(Device) || !USBStorageReadCapacity(Device)) {
            (void)USBStorageRequestSense(Device);
            ERROR(TEXT("READ CAPACITY failed"));
            USBStorageFreeDevice(Device);
            return FALSE;
        }
    }

    DEBUG(TEXT("Capacity blocks=%u block_size=%u"), Device->BlockCount, Device->BlockSize);

    Device->Ready = TRUE;
    Device->MountPending = TRUE;

    LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)CreateKernelObject(sizeof(USB_STORAGE_ENTRY), KOID_USBSTORAGE);
    if (Entry == NULL) {
        ERROR(TEXT("List entry allocation failed"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    MemorySet(&Entry->Device, 0, sizeof(USB_STORAGE_ENTRY) - sizeof(LISTNODE));
    Entry->Device = Device;
    Entry->Address = UsbDevice->Address;
    Entry->VendorId = UsbDevice->DeviceDescriptor.VendorID;
    Entry->ProductId = UsbDevice->DeviceDescriptor.ProductID;
    Entry->BlockCount = Device->BlockCount;
    Entry->BlockSize = Device->BlockSize;
    Entry->Present = TRUE;
    Device->ListEntry = Entry;

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL || ListAddItem(UsbStorageList, Entry) == FALSE) {
        ERROR(TEXT("Unable to register USB storage list entry"));
        ReleaseKernelObject(Entry);
        KernelHeapFree(Entry);
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    LPLIST DiskList = GetDiskList();
    if (DiskList == NULL || ListAddItem(DiskList, Device) == FALSE) {
        ERROR(TEXT("Unable to register disk entry"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (FileSystemReady()) {
        DEBUG(TEXT("Mounting disk partitions"));
        (void)USBStorageTryMountPending(Device);
    } else {
        DEBUG(TEXT("Deferred partition mount (filesystem not ready)"));
    }

    DEBUG(
        TEXT("USB disk addr=%x blocks=%u block_size=%u"), (U32)UsbDevice->Address,
        Device->BlockCount, Device->BlockSize);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Refresh presence flags for registered USB storage devices.
 */
static void USBStorageUpdatePresence(void) {
    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        if (Entry == NULL || Entry->Device == NULL) {
            Node = Next;
            continue;
        }

        LPUSB_MASS_STORAGE_DEVICE Device = Entry->Device;
        if (Device->Controller == NULL || Device->UsbDevice == NULL) {
            Entry->Present = FALSE;
            USBStorageDetachDevice(Device);
            Node = Next;
            continue;
        }

        Entry->Present = USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
        if (Entry->Present == FALSE) {
            USBStorageDetachDevice(Device);
        }

        Node = Next;
    }
}

/************************************************************************/

/**
 * @brief Scan xHCI controllers for new USB mass storage devices.
 */
static void USBStorageScanControllers(void) {
    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return;
    }

    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Controller = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Controller, KOID_PCIDEVICE) {
            XHCI_EnsureUsbDevices(Controller);

            LPLIST UsbDeviceList = GetUsbDeviceList();
            if (UsbDeviceList == NULL) {
                continue;
            }
            for (LPLISTNODE UsbNode = UsbDeviceList->First; UsbNode != NULL; UsbNode = UsbNode->Next) {
                LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)UsbNode;
                if (UsbDevice->Controller != Controller) {
                    continue;
                }
                if (!UsbDevice->Present || UsbDevice->IsHub) {
                    continue;
                }

                if (USBStorageIsTracked(UsbDevice)) {
                    continue;
                }

                LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
                if (Config == NULL) {
                    continue;
                }

                LPLIST InterfaceList = GetUsbInterfaceList();
                if (InterfaceList == NULL) {
                    continue;
                }

                for (LPLISTNODE IfNode = InterfaceList->First; IfNode != NULL; IfNode = IfNode->Next) {
                    LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)IfNode;
                    if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                        continue;
                    }
                    if (Interface->ConfigurationValue != Config->ConfigurationValue) {
                        continue;
                    }
                    if (Interface->InterfaceClass != USB_CLASS_MASS_STORAGE) {
                        continue;
                    }
                    if (Interface->InterfaceSubClass != USB_MASS_STORAGE_SUBCLASS_SCSI) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UnsupportedSubclass"));
                        continue;
                    }
                    if (Interface->InterfaceProtocol == USB_MASS_STORAGE_PROTOCOL_UAS) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UASNotSupported"));
                        continue;
                    }
                    if (!USBStorageIsMassStorageInterface(Interface)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UnsupportedProtocol"));
                        continue;
                    }

                    LPXHCI_USB_ENDPOINT BulkIn = NULL;
                    LPXHCI_USB_ENDPOINT BulkOut = NULL;
                    if (!USBStorageFindBulkEndpoints(Interface, &BulkIn, &BulkOut)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("MissingBulkEndpoints"));
                        continue;
                    }

                    if (!USBStorageStartDevice(Controller, UsbDevice, Interface, BulkIn, BulkOut)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("StartDeviceFailed"));
                        USBStorageState.RetryDelay = USB_MASS_STORAGE_SCAN_RETRY_DELAY_POLLS;
                        continue;
                    }

                    USBStorageLogScan(UsbDevice, Interface, TEXT("Attached"));
                    break;
                }
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Poll callback to maintain USB storage device list.
 * @param Context Unused.
 */
static void USBStoragePoll(LPVOID Context) {
    UNUSED(Context);

    if (USBStorageState.Initialized == FALSE) {
        return;
    }

    if (USBStorageState.RetryDelay != 0) {
        USBStorageState.RetryDelay--;
        return;
    }

    USBStorageUpdatePresence();
    USBStorageScanControllers();

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node != NULL; Node = Node->Next) {
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        LPUSB_MASS_STORAGE_DEVICE Device = NULL;
        if (Entry == NULL || Entry->Device == NULL || Entry->Present == FALSE) {
            continue;
        }

        Device = (LPUSB_MASS_STORAGE_DEVICE)Entry->Device;
        if (Device->MountPending == FALSE) {
            continue;
        }

        (void)USBStorageTryMountPending(Device);
    }
}

/************************************************************************/

/**
 * @brief Validate a USB mass storage I/O control request.
 * @param DeviceOut Receives validated USB mass storage device.
 * @param Control I/O control structure.
 * @param TotalBytesOut Receives validated transfer length in bytes.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBStorageValidateIoControl(LPUSB_MASS_STORAGE_DEVICE* DeviceOut, LPIOCONTROL Control, UINT* TotalBytesOut) {
    LPUSB_MASS_STORAGE_DEVICE Device = NULL;

    if (DeviceOut == NULL || TotalBytesOut == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPUSB_MASS_STORAGE_DEVICE)Control->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->SectorHigh != 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (!Device->Ready) {
        return DF_RETURN_NODEVICE;
    }

    if (!USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice)) {
        return DF_RETURN_NODEVICE;
    }

    *DeviceOut = Device;

    if (Control->NumSectors == 0) {
        return DF_RETURN_SUCCESS;
    }

    if (Control->SectorLow >= Device->BlockCount) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->NumSectors > (Device->BlockCount - Control->SectorLow)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->NumSectors > (MAX_UINT / Device->BlockSize)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    *TotalBytesOut = Control->NumSectors * Device->BlockSize;
    if (Control->BufferSize < *TotalBytesOut) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Flush one USB mass storage device write cache with one recovery retry.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
static BOOL USBStorageFlushWriteCache(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    if (USBStorageSynchronizeCache(Device)) {
        return TRUE;
    }

    if (!USBStorageRecoverCommandFailure(Device, USB_SCSI_SYNCHRONIZE_CACHE_10)) {
        return FALSE;
    }

    return USBStorageSynchronizeCache(Device);
}

/************************************************************************/

/**
 * @brief Transfer sectors through USB BOT read or write commands.
 * @param Control I/O control structure.
 * @param DirectionIn TRUE for read, FALSE for write.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBStorageTransfer(LPIOCONTROL Control, BOOL DirectionIn) {
    LPUSB_MASS_STORAGE_DEVICE Device = NULL;
    UINT TotalBytes = 0;
    UINT Remaining = 0;
    UINT CurrentLogicalBlockAddress = 0;
    U8* Buffer = NULL;
    U32 Validation = USBStorageValidateIoControl(&Device, Control, &TotalBytes);

    if (Validation != DF_RETURN_SUCCESS) {
        return Validation;
    }

    if (!DirectionIn && (Device->Access & DISK_ACCESS_READONLY) != 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    Remaining = Control->NumSectors;
    CurrentLogicalBlockAddress = Control->SectorLow;
    Buffer = (U8*)Control->Buffer;

    while (Remaining > 0) {
        UINT MaximumBlocks = PAGE_SIZE / Device->BlockSize;
        UINT Blocks = Remaining;
        if (Blocks > MaximumBlocks) {
            Blocks = MaximumBlocks;
        }

        if (DirectionIn) {
            if (!USBStorageReadBlocks(Device, CurrentLogicalBlockAddress, Blocks, Buffer)) {
                if (!USBStorageRecoverCommandFailure(Device, USB_SCSI_READ_10) ||
                    !USBStorageReadBlocks(Device, CurrentLogicalBlockAddress, Blocks, Buffer)) {
                    Device->Ready = FALSE;
                    return DF_RETURN_HARDWARE;
                }
            }
        } else {
            if (!USBStorageWriteBlocks(Device, CurrentLogicalBlockAddress, Blocks, Buffer)) {
                if (!USBStorageRecoverCommandFailure(Device, USB_SCSI_WRITE_10) ||
                    !USBStorageWriteBlocks(Device, CurrentLogicalBlockAddress, Blocks, Buffer)) {
                    Device->Ready = FALSE;
                    return DF_RETURN_HARDWARE;
                }
            }
        }

        Buffer += Blocks * Device->BlockSize;
        CurrentLogicalBlockAddress += Blocks;
        Remaining -= Blocks;
    }

    if (!DirectionIn && Control->NumSectors != 0 && !USBStorageFlushWriteCache(Device)) {
        Device->Ready = FALSE;
        return DF_RETURN_HARDWARE;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read sectors from a USB mass storage device.
 * @param Control I/O control structure.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBStorageRead(LPIOCONTROL Control) { return USBStorageTransfer(Control, TRUE); }

/************************************************************************/

/**
 * @brief Write sectors to a USB mass storage device.
 * @param Control I/O control structure.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBStorageWrite(LPIOCONTROL Control) { return USBStorageTransfer(Control, FALSE); }

/************************************************************************/

/**
 * @brief Populate disk information for a USB mass storage device.
 * @param Info Output disk info structure.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 USBStorageGetInfo(LPDISKINFO Info) {
    if (Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)Info->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Info->Type = DRIVER_TYPE_USB_STORAGE;
    Info->Removable = 1;
    Info->BytesPerSector = Device->BlockSize;
    Info->NumSectors = U64_FromUINT(Device->BlockCount);
    Info->Access = Device->Access;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Update access flags for a USB mass storage device.
 * @param Access Access request.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 USBStorageSetAccess(LPDISKACCESS Access) {
    if (Access == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)Access->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device->Access = Access->Access;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reset readiness state for a USB mass storage device.
 * @param Device USB mass storage device.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 USBStorageReset(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device->Ready = USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
    if (!Device->Ready) {
        return DF_RETURN_NODEVICE;
    }

    if (!USBStorageFlushWriteCache(Device)) {
        Device->Ready = FALSE;
        return DF_RETURN_HARDWARE;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver command dispatcher for USB mass storage.
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver status or data.
 */
UINT USBStorageCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((USBStorageDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            (void)RateLimiterInit(
                &USBStorageState.ScanLogLimiter, USB_MASS_STORAGE_SCAN_LOG_IMMEDIATE_BUDGET,
                USB_MASS_STORAGE_SCAN_LOG_INTERVAL_MS);

            if (DeferredWorkTokenIsValid(USBStorageState.PollToken) == FALSE) {
                USBStorageState.PollToken = DeferredWorkRegisterPollOnly(USBStoragePoll, NULL, TEXT("USBStorage"));
                if (DeferredWorkTokenIsValid(USBStorageState.PollToken) == FALSE) {
                    return DF_RETURN_UNEXPECTED;
                }
            }

            USBStorageState.Initialized = TRUE;
            USBStorageDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((USBStorageDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            if (DeferredWorkTokenIsValid(USBStorageState.PollToken) != FALSE) {
                DeferredWorkUnregister(USBStorageState.PollToken);
                USBStorageState.PollToken =
                    (DEFERRED_WORK_TOKEN){.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
            }

            USBStorageState.Initialized = FALSE;
            USBStorageDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(USB_MASS_STORAGE_VER_MAJOR, USB_MASS_STORAGE_VER_MINOR);

        case DF_DISK_RESET:
            return USBStorageReset((LPUSB_MASS_STORAGE_DEVICE)Parameter);
        case DF_DISK_READ:
            return USBStorageRead((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return USBStorageWrite((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return USBStorageGetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return USBStorageSetAccess((LPDISKACCESS)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
