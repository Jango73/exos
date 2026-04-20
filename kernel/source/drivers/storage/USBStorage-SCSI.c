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


    USB Storage SCSI Helpers

\************************************************************************/

#include "drivers/storage/USBStorage-Private.h"

#include "system/Clock.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Task.h"

/************************************************************************/

/**
 * @brief Build a SCSI READ(10) or WRITE(10) command block.
 * @param CommandBlock Output command block buffer.
 * @param CommandBlockLength Command block buffer length in bytes.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Block count to transfer.
 * @param DirectionIn TRUE for READ(10), FALSE for WRITE(10).
 * @return TRUE on success.
 */
static BOOL USBStorageBuildReadWrite10(U8* CommandBlock,
                                       UINT CommandBlockLength,
                                       UINT LogicalBlockAddress,
                                       UINT TransferBlocks,
                                       BOOL DirectionIn) {
    if (CommandBlock == NULL || CommandBlockLength < USB_SCSI_READ_WRITE_10_COMMAND_BLOCK_LENGTH) {
        return FALSE;
    }

    MemorySet(CommandBlock, 0, CommandBlockLength);
    CommandBlock[0] = DirectionIn ? USB_SCSI_READ_10 : USB_SCSI_WRITE_10;
    CommandBlock[2] = (U8)((LogicalBlockAddress >> 24) & 0xFF);
    CommandBlock[3] = (U8)((LogicalBlockAddress >> 16) & 0xFF);
    CommandBlock[4] = (U8)((LogicalBlockAddress >> 8) & 0xFF);
    CommandBlock[5] = (U8)(LogicalBlockAddress & 0xFF);
    CommandBlock[7] = (U8)((TransferBlocks >> 8) & 0xFF);
    CommandBlock[8] = (U8)(TransferBlocks & 0xFF);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Transfer blocks using SCSI READ(10) or WRITE(10).
 * @param Device USB mass storage device context.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Number of blocks to transfer.
 * @param DirectionIn TRUE for READ(10), FALSE for WRITE(10).
 * @param Buffer Data buffer.
 * @return TRUE on success.
 */
static BOOL USBStorageTransferBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                                     UINT LogicalBlockAddress,
                                     UINT TransferBlocks,
                                     BOOL DirectionIn,
                                     LPVOID Buffer) {
    U8 CommandBlock[USB_SCSI_READ_WRITE_10_COMMAND_BLOCK_LENGTH];
    UINT Length = 0;

    if (Device == NULL || Buffer == NULL || Device->BlockSize == 0) {
        return FALSE;
    }

    Length = TransferBlocks * Device->BlockSize;
    if (Length == 0 || Length > PAGE_SIZE) {
        return FALSE;
    }

    if (TransferBlocks > MAX_U16) {
        return FALSE;
    }

    if (!USBStorageBuildReadWrite10(CommandBlock,
                                    sizeof(CommandBlock),
                                    LogicalBlockAddress,
                                    TransferBlocks,
                                    DirectionIn)) {
        return FALSE;
    }

    return USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock), Length, DirectionIn, Buffer);
}

/************************************************************************/

/**
 * @brief Update cached geometry metadata after successful reinitialization.
 * @param Device USB mass storage device context.
 */
static void USBStorageUpdateEntryGeometry(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL || Device->ListEntry == NULL) {
        return;
    }

    Device->ListEntry->BlockCount = Device->BlockCount;
    Device->ListEntry->BlockSize = (U16)Device->BlockSize;
}

/************************************************************************/

/**
 * @brief Read and decode fixed-format sense data.
 * @param Device USB mass storage device context.
 * @param SenseDataOut Receives decoded sense data.
 * @param LogResult TRUE to emit a warning line with decoded sense values.
 * @return TRUE on success.
 */
BOOL USBStorageRequestSenseData(LPUSB_MASS_STORAGE_DEVICE Device,
                                LPUSB_STORAGE_SENSE_DATA SenseDataOut,
                                BOOL LogResult) {
    U8 CommandBlock[6];
    U8 SenseData[18];

    if (Device == NULL || SenseDataOut == NULL) {
        return FALSE;
    }

    MemorySet(SenseDataOut, 0, sizeof(USB_STORAGE_SENSE_DATA));
    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_REQUEST_SENSE;
    CommandBlock[4] = (U8)sizeof(SenseData);

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                              sizeof(SenseData), TRUE, SenseData)) {
        WARNING(TEXT("Request sense failed"));
        DEBUG(TEXT("LastOp=%x Stage=%u LastCSW=%x Residue=%u"),
              (U32)Device->LastScsiOpCode,
              (U32)Device->LastBotStage,
              (U32)Device->LastCswStatus,
              Device->LastCswResidue);
        return FALSE;
    }

    SenseDataOut->ResponseCode = (U8)(SenseData[0] & 0x7F);
    SenseDataOut->SenseKey = (U8)(SenseData[2] & 0x0F);
    SenseDataOut->AdditionalSenseCode = SenseData[12];
    SenseDataOut->AdditionalSenseCodeQualifier = SenseData[13];

    if (LogResult) {
        WARNING(TEXT("Sense=%x ASC=%x ASCQ=%x LastOp=%x"),
                (U32)SenseDataOut->SenseKey,
                (U32)SenseDataOut->AdditionalSenseCode,
                (U32)SenseDataOut->AdditionalSenseCodeQualifier,
                (U32)Device->LastScsiOpCode);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Run a SCSI INQUIRY command and log basic identification.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageInquiry(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[6];
    U8 InquiryData[36];
    STR Vendor[9];
    STR Product[17];

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_INQUIRY;
    CommandBlock[4] = (U8)sizeof(InquiryData);

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                              sizeof(InquiryData), TRUE, InquiryData)) {
        return FALSE;
    }

    MemorySet(Vendor, 0, sizeof(Vendor));
    MemorySet(Product, 0, sizeof(Product));
    MemoryCopy(Vendor, &InquiryData[8], 8);
    MemoryCopy(Product, &InquiryData[16], 16);

    DEBUG(TEXT("Vendor=%s Product=%s"), Vendor, Product);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run SCSI REQUEST SENSE and emit one decoded warning line.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageRequestSense(LPUSB_MASS_STORAGE_DEVICE Device) {
    USB_STORAGE_SENSE_DATA SenseData;

    return USBStorageRequestSenseData(Device, &SenseData, TRUE);
}

/************************************************************************/

/**
 * @brief Probe whether the medium reports ready through TEST UNIT READY.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageTestUnitReady(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[USB_SCSI_TEST_UNIT_READY_COMMAND_BLOCK_LENGTH];

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_TEST_UNIT_READY;

    return USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock), 0, FALSE, NULL);
}

/************************************************************************/

/**
 * @brief Flush the device write cache through SYNCHRONIZE CACHE(10).
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageSynchronizeCache(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[USB_SCSI_SYNCHRONIZE_CACHE_10_COMMAND_BLOCK_LENGTH];

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_SYNCHRONIZE_CACHE_10;

    return USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock), 0, FALSE, NULL);
}

/************************************************************************/

/**
 * @brief Run SCSI READ CAPACITY(10) and capture block geometry.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageReadCapacity(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[10];
    U8 CapacityData[8];
    U32 LastLogicalBlockAddress = 0;
    UINT BlockSize = 0;
    U32 TemporaryValue = 0;

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_READ_CAPACITY_10;

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                              sizeof(CapacityData), TRUE, CapacityData)) {
        return FALSE;
    }

    MemoryCopy(&TemporaryValue, &CapacityData[0], sizeof(TemporaryValue));
    LastLogicalBlockAddress = Ntohl(TemporaryValue);
    TemporaryValue = 0;
    MemoryCopy(&TemporaryValue, &CapacityData[4], sizeof(TemporaryValue));
    BlockSize = Ntohl(TemporaryValue);

    if (LastLogicalBlockAddress == 0xFFFFFFFF) {
        ERROR(TEXT("Device too large for READ CAPACITY(10)"));
        return FALSE;
    }

    if (BlockSize != 512 && BlockSize != 4096) {
        ERROR(TEXT("Unsupported block size %u"), BlockSize);
        return FALSE;
    }

    Device->BlockCount = (UINT)LastLogicalBlockAddress + 1;
    Device->BlockSize = BlockSize;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reinitialize one USB storage device after media-state sense errors.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageReinitializeDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    UINT Attempt = 0;
    BOOL MediumReady = FALSE;
    USB_STORAGE_SENSE_DATA SenseData;

    if (Device == NULL) {
        return FALSE;
    }

    if (!USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice)) {
        Device->Ready = FALSE;
        return FALSE;
    }

    if (!USBStorageResetRecovery(Device)) {
        Device->Ready = FALSE;
        return FALSE;
    }

    for (Attempt = 0; Attempt < USB_MASS_STORAGE_READY_RETRY_COUNT; Attempt++) {
        if (USBStorageTestUnitReady(Device)) {
            MediumReady = TRUE;
            break;
        }

        if (!USBStorageRequestSenseData(Device, &SenseData, FALSE)) {
            break;
        }

        if (SenseData.SenseKey != USB_SCSI_SENSE_KEY_NOT_READY &&
            SenseData.SenseKey != USB_SCSI_SENSE_KEY_UNIT_ATTENTION) {
            DEBUG(TEXT("Sense=%x ASC=%x ASCQ=%x"),
                  (U32)SenseData.SenseKey,
                  (U32)SenseData.AdditionalSenseCode,
                  (U32)SenseData.AdditionalSenseCodeQualifier);
            break;
        }

        Sleep(USB_MASS_STORAGE_READY_RETRY_DELAY_MS);
    }

    if (!MediumReady) {
        WARNING(TEXT("Device stayed busy"));
        Device->Ready = FALSE;
        return FALSE;
    }

    if (!USBStorageInquiry(Device) || !USBStorageReadCapacity(Device)) {
        WARNING(TEXT("Device reinit failed"));
        Device->Ready = FALSE;
        return FALSE;
    }

    USBStorageUpdateEntryGeometry(Device);
    Device->Ready = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Recover one failed SCSI command when the device reports media-state changes.
 * @param Device USB mass storage device context.
 * @param FailedOperation Failed SCSI operation code.
 * @return TRUE when the device was reinitialized and the caller can retry.
 */
BOOL USBStorageRecoverCommandFailure(LPUSB_MASS_STORAGE_DEVICE Device, U8 FailedOperation) {
    USB_STORAGE_SENSE_DATA SenseData;

    if (Device == NULL) {
        return FALSE;
    }

    if (!USBStorageRequestSenseData(Device, &SenseData, TRUE)) {
        return FALSE;
    }

    if (SenseData.SenseKey == USB_SCSI_SENSE_KEY_UNIT_ATTENTION) {
        WARNING(TEXT("Media state changed Op=%x"),
                (U32)FailedOperation);
        return USBStorageReinitializeDevice(Device);
    }

    if (SenseData.SenseKey == USB_SCSI_SENSE_KEY_NOT_READY) {
        WARNING(TEXT("Device not ready Op=%x"),
                (U32)FailedOperation);
        return USBStorageReinitializeDevice(Device);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Read blocks through SCSI READ(10).
 * @param Device USB mass storage device context.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Number of blocks to transfer.
 * @param Output Output buffer.
 * @return TRUE on success.
 */
BOOL USBStorageReadBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                          UINT LogicalBlockAddress,
                          UINT TransferBlocks,
                          LPVOID Output) {
    return USBStorageTransferBlocks(Device, LogicalBlockAddress, TransferBlocks, TRUE, Output);
}

/************************************************************************/

/**
 * @brief Write blocks through SCSI WRITE(10).
 * @param Device USB mass storage device context.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Number of blocks to transfer.
 * @param Input Input buffer.
 * @return TRUE on success.
 */
BOOL USBStorageWriteBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                           UINT LogicalBlockAddress,
                           UINT TransferBlocks,
                           LPCVOID Input) {
    return USBStorageTransferBlocks(Device, LogicalBlockAddress, TransferBlocks, FALSE, (LPVOID)Input);
}

/************************************************************************/
