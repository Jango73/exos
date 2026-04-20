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


    USB Storage Internal Definitions

\************************************************************************/

#ifndef USB_STORAGE_PRIVATE_H_INCLUDED
#define USB_STORAGE_PRIVATE_H_INCLUDED

/************************************************************************/

#include "drivers/storage/USBStorage.h"
#include "drivers/usb/XHCI-Internal.h"
#include "fs/Disk.h"
#include "sync/DeferredWork.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define USB_MASS_STORAGE_VER_MAJOR 1
#define USB_MASS_STORAGE_VER_MINOR 0

#define USB_MASS_STORAGE_SUBCLASS_SCSI 0x06
#define USB_MASS_STORAGE_PROTOCOL_BOT 0x50
#define USB_MASS_STORAGE_PROTOCOL_UAS 0x62

#define USB_MASS_STORAGE_COMMAND_BLOCK_SIGNATURE 0x43425355
#define USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE 0x53425355
#define USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH 31
#define USB_MASS_STORAGE_COMMAND_STATUS_LENGTH 13
#define USB_MASS_STORAGE_COMMAND_STATUS_PASSED 0
#define USB_MASS_STORAGE_COMMAND_STATUS_FAILED 1
#define USB_MASS_STORAGE_COMMAND_STATUS_PHASE_ERROR 2

#define USB_SCSI_INQUIRY 0x12
#define USB_SCSI_TEST_UNIT_READY 0x00
#define USB_SCSI_REQUEST_SENSE 0x03
#define USB_SCSI_READ_CAPACITY_10 0x25
#define USB_SCSI_READ_10 0x28
#define USB_SCSI_WRITE_10 0x2A
#define USB_SCSI_SYNCHRONIZE_CACHE_10 0x35
#define USB_SCSI_READ_WRITE_10_COMMAND_BLOCK_LENGTH 10
#define USB_SCSI_TEST_UNIT_READY_COMMAND_BLOCK_LENGTH 6
#define USB_SCSI_SYNCHRONIZE_CACHE_10_COMMAND_BLOCK_LENGTH 10

#define USB_SCSI_SENSE_KEY_NOT_READY 0x02
#define USB_SCSI_SENSE_KEY_UNIT_ATTENTION 0x06

#define USB_MASS_STORAGE_READY_RETRY_COUNT 5
#define USB_MASS_STORAGE_READY_RETRY_DELAY_MS 100

#define USB_MASS_STORAGE_BULK_RETRIES 3
#define USB_MASS_STORAGE_SCAN_LOG_IMMEDIATE_BUDGET 1
#define USB_MASS_STORAGE_SCAN_LOG_INTERVAL_MS 2000

/************************************************************************/

#pragma pack(push, 1)

typedef struct tag_USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER {
    U32 Signature;
    U32 Tag;
    U32 DataTransferLength;
    U8 Flags;
    U8 LogicalUnitNumber;
    U8 CommandBlockLength;
    U8 CommandBlock[16];
} USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER, *LPUSB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER;

typedef struct tag_USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER {
    U32 Signature;
    U32 Tag;
    U32 DataResidue;
    U8 Status;
} USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER, *LPUSB_MASS_STORAGE_COMMAND_STATUS_WRAPPER;

#pragma pack(pop)

/************************************************************************/

typedef struct tag_USB_MASS_STORAGE_DEVICE {
    STORAGE_UNIT Disk;
    U32 Access;
    LPXHCI_DEVICE Controller;
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_INTERFACE Interface;
    LPXHCI_USB_ENDPOINT BulkInEndpoint;
    LPXHCI_USB_ENDPOINT BulkOutEndpoint;
    U8 InterfaceNumber;
    U32 Tag;
    UINT BlockCount;
    UINT BlockSize;
    PHYSICAL InputOutputBufferPhysical;
    LINEAR InputOutputBufferLinear;
    BOOL Ready;
    BOOL MountPending;
    BOOL ReferencesHeld;
    LPUSB_STORAGE_ENTRY ListEntry;
    U8 LastScsiOpCode;
    U8 LastBotStage;
    U8 LastCswStatus;
    U32 LastCswResidue;
} USB_MASS_STORAGE_DEVICE, *LPUSB_MASS_STORAGE_DEVICE;

typedef struct tag_USB_MASS_STORAGE_STATE {
    BOOL Initialized;
    DEFERRED_WORK_TOKEN PollToken;
    UINT RetryDelay;
    RATE_LIMITER ScanLogLimiter;
} USB_MASS_STORAGE_STATE, *LPUSB_MASS_STORAGE_STATE;

typedef struct tag_USB_STORAGE_SENSE_DATA {
    U8 ResponseCode;
    U8 SenseKey;
    U8 AdditionalSenseCode;
    U8 AdditionalSenseCodeQualifier;
} USB_STORAGE_SENSE_DATA, *LPUSB_STORAGE_SENSE_DATA;

BOOL USBStorageResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageBotCommand(
    LPUSB_MASS_STORAGE_DEVICE Device, const U8* CommandBlock, U8 CommandBlockLength, UINT DataLength, BOOL DirectionIn,
    LPVOID DataBuffer);
BOOL USBStorageInquiry(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageRequestSense(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageRequestSenseData(
    LPUSB_MASS_STORAGE_DEVICE Device, LPUSB_STORAGE_SENSE_DATA SenseDataOut, BOOL LogResult);
BOOL USBStorageReadCapacity(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageReadBlocks(
    LPUSB_MASS_STORAGE_DEVICE Device, UINT LogicalBlockAddress, UINT TransferBlocks, LPVOID Output);
BOOL USBStorageWriteBlocks(
    LPUSB_MASS_STORAGE_DEVICE Device, UINT LogicalBlockAddress, UINT TransferBlocks, LPCVOID Input);
BOOL USBStorageTestUnitReady(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageSynchronizeCache(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageReinitializeDevice(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageRecoverCommandFailure(LPUSB_MASS_STORAGE_DEVICE Device, U8 FailedOperation);
BOOL USBStorageIsMassStorageInterface(LPXHCI_USB_INTERFACE Interface);
BOOL USBStorageFindBulkEndpoints(
    LPXHCI_USB_INTERFACE Interface, LPXHCI_USB_ENDPOINT* BulkInOut, LPXHCI_USB_ENDPOINT* BulkOutOut);
BOOL USBStorageIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL USBStorageIsTracked(LPXHCI_USB_DEVICE UsbDevice);

/************************************************************************/

#endif
