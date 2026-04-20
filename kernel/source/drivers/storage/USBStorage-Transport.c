
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

#include "drivers/storage/USBStorage-Private.h"

#include "system/Clock.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Task.h"

/************************************************************************/

#define USB_STORAGE_WAIT_LOG_IMMEDIATE_BUDGET 1
#define USB_STORAGE_WAIT_LOG_INTERVAL_MS 1000
#define USB_STORAGE_COMMAND_LOG_IMMEDIATE_BUDGET 4
#define USB_STORAGE_COMMAND_LOG_INTERVAL_MS 1000
#define USB_STORAGE_TRANSFER_STATUS_SUCCESS 0
#define USB_STORAGE_TRANSFER_STATUS_TIMEOUT 1
#define USB_STORAGE_TRANSFER_STATUS_STALL 2
#define USB_STORAGE_TRANSFER_STATUS_ERROR 3

/************************************************************************/

/**
 * @brief Check whether one BOT transfer trace line should be emitted.
 * @param SuppressedOut Receives suppressed count.
 * @return TRUE when one line can be emitted.
 */
static BOOL USBStorageShouldTraceTransfer(U32* SuppressedOut) {
    static RATE_LIMITER DATA_SECTION TransferTraceLimiter = {0};
    static BOOL DATA_SECTION TransferTraceLimiterInitAttempted = FALSE;

    if (SuppressedOut == NULL) {
        return FALSE;
    }

    if (TransferTraceLimiter.Initialized == FALSE && TransferTraceLimiterInitAttempted == FALSE) {
        TransferTraceLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&TransferTraceLimiter,
                            USB_STORAGE_WAIT_LOG_IMMEDIATE_BUDGET,
                            USB_STORAGE_WAIT_LOG_INTERVAL_MS) == FALSE) {
            return FALSE;
        }
    }

    return RateLimiterShouldTrigger(&TransferTraceLimiter, GetSystemTime(), SuppressedOut);
}

/************************************************************************/

/**
 * @brief Check whether one BOT command trace line should be emitted.
 * @param Operation SCSI operation code.
 * @param SuppressedOut Receives suppressed count.
 * @return TRUE when one line can be emitted.
 */
static BOOL USBStorageShouldTraceCommand(U8 Operation, U32* SuppressedOut) {
    static RATE_LIMITER DATA_SECTION ReadCommandTraceLimiter = {0};
    static RATE_LIMITER DATA_SECTION WriteCommandTraceLimiter = {0};
    static BOOL DATA_SECTION CommandTraceLimiterInitAttempted = FALSE;

    if (SuppressedOut == NULL) {
        return FALSE;
    }

    *SuppressedOut = 0;
    if (Operation != USB_SCSI_READ_10 && Operation != USB_SCSI_WRITE_10) {
        return TRUE;
    }

    if (ReadCommandTraceLimiter.Initialized == FALSE && CommandTraceLimiterInitAttempted == FALSE) {
        CommandTraceLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&ReadCommandTraceLimiter,
                            USB_STORAGE_COMMAND_LOG_IMMEDIATE_BUDGET,
                            USB_STORAGE_COMMAND_LOG_INTERVAL_MS) == FALSE) {
            return FALSE;
        }
        if (RateLimiterInit(&WriteCommandTraceLimiter,
                            USB_STORAGE_COMMAND_LOG_IMMEDIATE_BUDGET,
                            USB_STORAGE_COMMAND_LOG_INTERVAL_MS) == FALSE) {
            return FALSE;
        }
    }

    if (Operation == USB_SCSI_READ_10) {
        return RateLimiterShouldTrigger(&ReadCommandTraceLimiter, GetSystemTime(), SuppressedOut);
    }

    return RateLimiterShouldTrigger(&WriteCommandTraceLimiter, GetSystemTime(), SuppressedOut);
}

/************************************************************************/

/**
 * @brief Perform BOT reset recovery after a transport/protocol violation.
 * @param Device USB mass storage device context.
 * @param Operation SCSI operation code.
 * @param Reason Short reason string.
 */
static void USBStorageRunBotResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device, U8 Operation, LPCSTR Reason) {
    if (Device == NULL) {
        return;
    }

    WARNING(TEXT("Op=%x Reason=%s"),
            (U32)Operation,
            (Reason != NULL) ? Reason : TEXT("unknown"));
    (void)USBStorageResetRecovery(Device);
}

/**
 * @brief Check whether an interface matches USB mass storage BOT.
 * @param Interface USB interface descriptor.
 * @return TRUE when the interface matches BOT class/subclass/protocol.
 */
BOOL USBStorageIsMassStorageInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    return (Interface->InterfaceClass == USB_CLASS_MASS_STORAGE &&
            Interface->InterfaceSubClass == USB_MASS_STORAGE_SUBCLASS_SCSI &&
            Interface->InterfaceProtocol == USB_MASS_STORAGE_PROTOCOL_BOT);
}

/************************************************************************/

/**
 * @brief Locate bulk IN/OUT endpoints for an interface.
 * @param Interface USB interface descriptor.
 * @param BulkInOut Receives bulk IN endpoint pointer.
 * @param BulkOutOut Receives bulk OUT endpoint pointer.
 * @return TRUE when both endpoints are found.
 */
BOOL USBStorageFindBulkEndpoints(LPXHCI_USB_INTERFACE Interface,
                                            LPXHCI_USB_ENDPOINT* BulkInOut,
                                            LPXHCI_USB_ENDPOINT* BulkOutOut) {
    if (Interface == NULL || BulkInOut == NULL || BulkOutOut == NULL) {
        return FALSE;
    }

    *BulkInOut = XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_BULK, TRUE);
    *BulkOutOut = XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_BULK, FALSE);

    return (*BulkInOut != NULL && *BulkOutOut != NULL);
}

/************************************************************************/

/**
 * @brief Verify a USB device is still present on a controller.
 * @param Device xHCI controller.
 * @param UsbDevice USB device to validate.
 * @return TRUE when still present.
 */
BOOL USBStorageIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        LPLIST UsbDeviceList = GetUsbDeviceList();
        if (UsbDeviceList == NULL) {
            return FALSE;
        }

        for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
            LPXHCI_USB_DEVICE Curr = (LPXHCI_USB_DEVICE)Node;
            if (Curr->Controller != Device) {
                continue;
            }
            if (Curr == UsbDevice && Curr->Present) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether a USB device is already tracked.
 * @param UsbDevice USB device to check.
 * @return TRUE when already registered.
 */
BOOL USBStorageIsTracked(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node; Node = Node->Next) {
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        if (Entry == NULL || Entry->Device == NULL) {
            continue;
        }

        if (Entry->Device->UsbDevice == UsbDevice) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Clear the HALT feature on a USB endpoint.
 * @param Device xHCI controller.
 * @param UsbDevice USB device state.
 * @param EndpointAddress Endpoint address to clear.
 * @return TRUE on success.
 */
static BOOL USBStorageClearEndpointHalt(LPXHCI_DEVICE Device,
                                            LPXHCI_USB_DEVICE UsbDevice,
                                            U8 EndpointAddress) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_ENDPOINT;
    Setup.Request = USB_REQUEST_CLEAR_FEATURE;
    Setup.Value = USB_FEATURE_ENDPOINT_HALT;
    Setup.Index = EndpointAddress;
    Setup.Length = 0;

    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE);
}

/************************************************************************/

/**
 * @brief Perform BOT reset recovery sequence for a device.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device) {
    USB_SETUP_PACKET Setup;
    BOOL BulkInOk;
    BOOL BulkOutOk;

    if (Device == NULL || Device->Controller == NULL || Device->UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = 0xFF;  // Bulk-Only Transport Reset
    Setup.Value = 0;
    Setup.Index = Device->InterfaceNumber;
    Setup.Length = 0;

    if (!XHCI_ControlTransfer(Device->Controller, Device->UsbDevice, &Setup, 0, NULL, 0, FALSE)) {
        WARNING(TEXT("BOT reset failed for interface %u"),
            (UINT)Device->InterfaceNumber);
        return FALSE;
    }

    BulkInOk = USBStorageClearEndpointHalt(Device->Controller,
                                               Device->UsbDevice,
                                               Device->BulkInEndpoint->Address);
    BulkOutOk = USBStorageClearEndpointHalt(Device->Controller,
                                                Device->UsbDevice,
                                                Device->BulkOutEndpoint->Address);
    if (!BulkInOk || !BulkOutOk) {
        WARNING(TEXT("Clear halt failed in=%x out=%x"),
            (U32)(BulkInOk != FALSE),
            (U32)(BulkOutOk != FALSE));
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Wait for a transfer completion with a timeout.
 * @param Device xHCI controller.
 * @param TrbPhysical TRB physical address.
 * @param TimeoutMilliseconds Timeout in milliseconds.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on completion, FALSE on timeout.
 */
static BOOL USBStorageWaitCompletion(LPXHCI_DEVICE Device,
                                     LPXHCI_USB_DEVICE UsbDevice,
                                     LPXHCI_USB_ENDPOINT Endpoint,
                                     U64 TrbPhysical,
                                     UINT TimeoutMilliseconds,
                                     U32* CompletionOut,
                                     U32* TransferLengthOut) {
    UINT Remaining = TimeoutMilliseconds;
    UINT Elapsed = 0;
    U32 Suppressed = 0;
    BOOL UsedRouteFallback = FALSE;
    U64 ObservedTrbPhysical = U64_0;

    if (USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("Begin Timeout=%u Trb=%x:%x suppressed=%u"),
              TimeoutMilliseconds,
              U64_High32(TrbPhysical),
              U64_Low32(TrbPhysical),
              Suppressed);
    }

    while (Remaining > 0) {
        if (XHCI_CheckTransferCompletionRouted(Device,
                                               TrbPhysical,
                                               (UsbDevice != NULL) ? UsbDevice->SlotId : 0,
                                               (Endpoint != NULL) ? Endpoint->Dci : 0,
                                               CompletionOut,
                                               TransferLengthOut,
                                               &UsedRouteFallback,
                                               &ObservedTrbPhysical)) {
            if (UsedRouteFallback && USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("Transfer event TRB pointer mismatch Slot=%x Dci=%u Expected=%x:%x Observed=%x:%x Completion=%x"),
                      (UsbDevice != NULL) ? (U32)UsbDevice->SlotId : 0,
                      (Endpoint != NULL) ? (U32)Endpoint->Dci : 0,
                      U64_High32(TrbPhysical),
                      U64_Low32(TrbPhysical),
                      U64_High32(ObservedTrbPhysical),
                      U64_Low32(ObservedTrbPhysical),
                      (CompletionOut != NULL) ? *CompletionOut : 0);
            }
            if (USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("Completed Elapsed=%u Completion=%x Trb=%x:%x suppressed=%u"),
                      Elapsed,
                      (CompletionOut != NULL) ? *CompletionOut : 0,
                      U64_High32(TrbPhysical),
                      U64_Low32(TrbPhysical),
                      Suppressed);
            }
            return TRUE;
        }

        Sleep(1);
        Remaining--;
        Elapsed++;
    }

    if (USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("Timeout Elapsed=%u Trb=%x:%x suppressed=%u"),
              Elapsed,
              U64_High32(TrbPhysical),
              U64_Low32(TrbPhysical),
              Suppressed);
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Submit a single bulk transfer and wait for completion.
 * @param Device xHCI controller.
 * @param UsbDevice USB device.
 * @param Endpoint Bulk endpoint.
 * @param BufferPhysical Physical address of data buffer.
 * @param BufferLinear Linear address of data buffer.
 * @param Length Transfer length in bytes.
 * @param DirectionIn TRUE for IN transfer.
 * @param TimeoutMilliseconds Timeout in milliseconds.
 * @param CompletionOut Receives completion code.
 * @return TRUE on completion, FALSE otherwise.
 */
static BOOL USBStorageBulkTransferOnce(LPXHCI_DEVICE Device,
                                           LPXHCI_USB_DEVICE UsbDevice,
                                           LPXHCI_USB_ENDPOINT Endpoint,
                                           PHYSICAL BufferPhysical,
                                           LINEAR BufferLinear,
                                           UINT Length,
                                           BOOL DirectionIn,
                                           UINT TimeoutMilliseconds,
                                           U8 ScsiOpCode,
                                           U32* CompletionOut,
                                           U32* TransferLengthOut) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || BufferPhysical == 0 || BufferLinear == 0) {
        return FALSE;
    }

    if (Length > MAX_U32) {
        return FALSE;
    }

    U64 TrbPhysical = U64_0;
    if (!XHCI_SubmitNormalTransfer(Device,
                                   UsbDevice,
                                   Endpoint,
                                   BufferPhysical,
                                   (U32)Length,
                                   DirectionIn,
                                   &TrbPhysical)) {
        return FALSE;
    }

    if (!USBStorageWaitCompletion(Device,
                                  UsbDevice,
                                  Endpoint,
                                  TrbPhysical,
                                  TimeoutMilliseconds,
                                  CompletionOut,
                                  TransferLengthOut)) {
        DEBUG(TEXT("Timeout Op=%x Slot=%x Port=%u Addr=%u Ep=%x Dci=%u DirIn=%u Len=%u Trb=%p"),
              (U32)ScsiOpCode,
              (U32)UsbDevice->SlotId,
              (U32)UsbDevice->PortNumber,
              (U32)UsbDevice->Address,
              (U32)Endpoint->Address,
              (U32)Endpoint->Dci,
              (U32)(DirectionIn != FALSE),
              Length,
              (LPVOID)(UINT)U64_Low32(TrbPhysical));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Submit a bulk transfer with retry and stall recovery.
 * @param Device xHCI controller.
 * @param UsbDevice USB device.
 * @param Endpoint Bulk endpoint.
 * @param BufferPhysical Physical address of data buffer.
 * @param BufferLinear Linear address of data buffer.
 * @param Length Transfer length in bytes.
 * @param DirectionIn TRUE for IN transfer.
 * @return TRUE on success.
 */
static BOOL USBStorageBulkTransfer(LPXHCI_DEVICE Device,
                                       LPXHCI_USB_DEVICE UsbDevice,
                                       LPXHCI_USB_ENDPOINT Endpoint,
                                       PHYSICAL BufferPhysical,
                                       LINEAR BufferLinear,
                                       UINT Length,
                                       BOOL DirectionIn,
                                       U8 ScsiOpCode,
                                       U32* TransferStatusOut,
                                       U32* TransferLengthOut) {
    U32 Suppressed = 0;
    U32 Completion = 0;
    U32 TransferLength = 0;

    if (TransferStatusOut != NULL) {
        *TransferStatusOut = USB_STORAGE_TRANSFER_STATUS_ERROR;
    }
    if (ScsiOpCode == USB_SCSI_READ_CAPACITY_10 && USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("Op=%x Slot=%x Ep=%x Dci=%u DirIn=%u Len=%u suppressed=%u"),
              (U32)ScsiOpCode,
              (U32)UsbDevice->SlotId,
              (U32)Endpoint->Address,
              (U32)Endpoint->Dci,
              (U32)(DirectionIn != FALSE),
              Length,
              Suppressed);
    }

    if (!USBStorageBulkTransferOnce(Device, UsbDevice, Endpoint, BufferPhysical, BufferLinear,
                                    Length,
                                    DirectionIn,
                                    USB_MASS_STORAGE_BULK_TIMEOUT_MS,
                                    ScsiOpCode,
                                    &Completion,
                                    &TransferLength)) {
        if (TransferStatusOut != NULL) {
            *TransferStatusOut = USB_STORAGE_TRANSFER_STATUS_TIMEOUT;
        }
        if (USBStorageShouldTraceTransfer(&Suppressed)) {
            DEBUG(TEXT("Timeout Slot=%x Port=%u Addr=%u Ep=%x DirIn=%u suppressed=%u"),
                  (U32)UsbDevice->SlotId,
                  (U32)UsbDevice->PortNumber,
                  (U32)UsbDevice->Address,
                  (U32)Endpoint->Address,
                  (U32)(DirectionIn != FALSE),
                  Suppressed);
        }
        if (!XHCI_ResetTransferEndpoint(Device, UsbDevice, Endpoint, FALSE)) {
            if (USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("xHCI endpoint reset failed Slot=%x Dci=%u Ep=%x suppressed=%u"),
                      (U32)UsbDevice->SlotId,
                      (U32)Endpoint->Dci,
                      (U32)Endpoint->Address,
                      Suppressed);
            }
        }
        return FALSE;
    }

    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        if (TransferStatusOut != NULL) {
            *TransferStatusOut = USB_STORAGE_TRANSFER_STATUS_SUCCESS;
        }
        if (DirectionIn && TransferLengthOut != NULL) {
            *TransferLengthOut = TransferLength;
        }
        return TRUE;
    }

    if (Completion == XHCI_COMPLETION_STALL_ERROR) {
        if (TransferStatusOut != NULL) {
            *TransferStatusOut = USB_STORAGE_TRANSFER_STATUS_STALL;
        }
        if (!XHCI_ResetTransferEndpoint(Device, UsbDevice, Endpoint, TRUE)) {
            if (USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("xHCI endpoint reset failed after stall Slot=%x Dci=%u Ep=%x suppressed=%u"),
                      (U32)UsbDevice->SlotId,
                      (U32)Endpoint->Dci,
                      (U32)Endpoint->Address,
                      Suppressed);
            }
        }
        return FALSE;
    }

    if (USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("Completion=%x Slot=%x Port=%u Addr=%u Ep=%x DirIn=%u Len=%u suppressed=%u"),
              Completion,
              (U32)UsbDevice->SlotId,
              (U32)UsbDevice->PortNumber,
              (U32)UsbDevice->Address,
              (U32)Endpoint->Address,
              (U32)(DirectionIn != FALSE),
              Length,
              Suppressed);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Issue a BOT command (CBW/DATA/CSW).
 * @param Device USB mass storage device context.
 * @param CommandBlock SCSI command buffer.
 * @param CommandBlockLength SCSI command length.
 * @param DataLength Data stage length in bytes.
 * @param DirectionIn TRUE when data stage is IN.
 * @param DataBuffer Optional transfer buffer for data stage payload.
 * @return TRUE on success.
 */
BOOL USBStorageBotCommand(LPUSB_MASS_STORAGE_DEVICE Device,
                          const U8* CommandBlock,
                          U8 CommandBlockLength,
                          UINT DataLength,
                          BOOL DirectionIn,
                          LPVOID DataBuffer) {
    U32 Suppressed = 0;
    U32 ExpectedTag;
    U32 TransferStatus;
    U32 TransferLength;
    U32 ActualLength;
    BOOL DataStageStalled = FALSE;
    if (Device == NULL || CommandBlock == NULL || CommandBlockLength == 0) {
        return FALSE;
    }

    if (CommandBlockLength > sizeof(((USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER*)0)->CommandBlock)) {
        return FALSE;
    }

    if (Device->InputOutputBufferLinear == 0 || Device->InputOutputBufferPhysical == 0) {
        return FALSE;
    }

    if (DataLength > PAGE_SIZE || DataLength > MAX_U32) {
        return FALSE;
    }

    USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER* CommandBlockWrapper =
        (USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER*)Device->InputOutputBufferLinear;
    MemorySet(CommandBlockWrapper, 0, sizeof(USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER));
    CommandBlockWrapper->Signature = USB_MASS_STORAGE_COMMAND_BLOCK_SIGNATURE;
    CommandBlockWrapper->Tag = Device->Tag++;
    CommandBlockWrapper->DataTransferLength = (U32)DataLength;
    CommandBlockWrapper->Flags = DirectionIn ? 0x80 : 0x00;
    CommandBlockWrapper->LogicalUnitNumber = 0;
    CommandBlockWrapper->CommandBlockLength = CommandBlockLength;
    MemoryCopy(CommandBlockWrapper->CommandBlock, CommandBlock, CommandBlockLength);
    ExpectedTag = CommandBlockWrapper->Tag;
    Device->LastScsiOpCode = CommandBlock[0];
    Device->LastBotStage = 1;
    Device->LastCswStatus = 0xFF;
    Device->LastCswResidue = 0;

    if (USBStorageShouldTraceCommand(CommandBlock[0], &Suppressed)) {
        DEBUG(TEXT("Op=%x CdbLen=%u DataLen=%u DirIn=%u Tag=%x Slot=%x Port=%u Addr=%u OutEp=%x InEp=%x suppressed=%u"),
              (U32)CommandBlock[0],
              (U32)CommandBlockLength,
              DataLength,
              (U32)(DirectionIn != FALSE),
              (U32)CommandBlockWrapper->Tag,
              (U32)Device->UsbDevice->SlotId,
              (U32)Device->UsbDevice->PortNumber,
              (U32)Device->UsbDevice->Address,
              (U32)Device->BulkOutEndpoint->Address,
              (U32)Device->BulkInEndpoint->Address,
              Suppressed);
    }

    if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkOutEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH,
                                    FALSE,
                                    CommandBlock[0],
                                    &TransferStatus,
                                    NULL)) {
        Device->LastBotStage = 2;
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("CBW"));
        ERROR(TEXT("CBW send failed Op=%x Tag=%x"), (U32)CommandBlock[0], (U32)CommandBlockWrapper->Tag);
        return FALSE;
    }

    if (DataLength > 0) {
        Device->LastBotStage = 3;
        TransferLength = 0;
        TransferStatus = USB_STORAGE_TRANSFER_STATUS_ERROR;
        if (!DirectionIn && DataBuffer != NULL) {
            MemoryCopy((LPVOID)Device->InputOutputBufferLinear, DataBuffer, DataLength);
        }

        if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice,
                                        DirectionIn ? Device->BulkInEndpoint : Device->BulkOutEndpoint,
                                        Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                        DataLength,
                                        DirectionIn,
                                        CommandBlock[0],
                                        &TransferStatus,
                                        &TransferLength)) {
            Device->LastBotStage = 4;
            if (TransferStatus == USB_STORAGE_TRANSFER_STATUS_STALL) {
                LPXHCI_USB_ENDPOINT StalledEndpoint = DirectionIn ? Device->BulkInEndpoint : Device->BulkOutEndpoint;
                DataStageStalled = TRUE;
                if (!USBStorageClearEndpointHalt(Device->Controller, Device->UsbDevice, StalledEndpoint->Address)) {
                    USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("DataStageClearHalt"));
                    ERROR(TEXT("Data stage clear halt failed Op=%x Tag=%x DirIn=%u Len=%u"),
                          (U32)CommandBlock[0],
                          (U32)CommandBlockWrapper->Tag,
                          (U32)(DirectionIn != FALSE),
                          DataLength);
                    return FALSE;
                }
            } else {
                USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("DataStage"));
                ERROR(TEXT("Data stage failed Op=%x Tag=%x DirIn=%u Len=%u"),
                      (U32)CommandBlock[0],
                      (U32)CommandBlockWrapper->Tag,
                      (U32)(DirectionIn != FALSE),
                      DataLength);
                return FALSE;
            }
        }

        if (!DataStageStalled && DirectionIn && DataBuffer != NULL) {
            MemoryCopy(DataBuffer, (LPVOID)Device->InputOutputBufferLinear, DataLength);
        }
    }

    USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER* CommandStatusWrapper =
        (USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER*)Device->InputOutputBufferLinear;
    Device->LastBotStage = 5;
    MemorySet(CommandStatusWrapper, 0, USB_MASS_STORAGE_COMMAND_STATUS_LENGTH);
    TransferLength = 0;
    TransferStatus = USB_STORAGE_TRANSFER_STATUS_ERROR;
    if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkInEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_STATUS_LENGTH,
                                    TRUE,
                                    CommandBlock[0],
                                    &TransferStatus,
                                    &TransferLength)) {
        if (TransferStatus == USB_STORAGE_TRANSFER_STATUS_STALL) {
            if (!USBStorageClearEndpointHalt(Device->Controller, Device->UsbDevice, Device->BulkInEndpoint->Address)) {
                Device->LastBotStage = 6;
                USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("CSWClearHalt"));
                ERROR(TEXT("CSW clear halt failed Op=%x Tag=%x"),
                      (U32)CommandBlock[0],
                      (U32)CommandBlockWrapper->Tag);
                return FALSE;
            }

            Device->LastBotStage = 5;
            MemorySet(CommandStatusWrapper, 0, USB_MASS_STORAGE_COMMAND_STATUS_LENGTH);
            TransferLength = 0;
            TransferStatus = USB_STORAGE_TRANSFER_STATUS_ERROR;
            if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkInEndpoint,
                                        Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                        USB_MASS_STORAGE_COMMAND_STATUS_LENGTH,
                                        TRUE,
                                        CommandBlock[0],
                                        &TransferStatus,
                                        &TransferLength)) {
                Device->LastBotStage = 6;
                USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("CSWRetry"));
                ERROR(TEXT("CSW read failed Op=%x Tag=%x"),
                      (U32)CommandBlock[0],
                      (U32)CommandBlockWrapper->Tag);
                return FALSE;
            }
        } else {
            Device->LastBotStage = 6;
            USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("CSW"));
            ERROR(TEXT("CSW read failed Op=%x Tag=%x"), (U32)CommandBlock[0], (U32)CommandBlockWrapper->Tag);
            return FALSE;
        }
    }
    Device->LastBotStage = 7;
    if (TransferLength > USB_MASS_STORAGE_COMMAND_STATUS_LENGTH) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("InvalidCSWLength"));
        ERROR(TEXT("Invalid CSW transfer length=%u Op=%x Tag=%x"),
              TransferLength,
              (U32)CommandBlock[0],
              ExpectedTag);
        return FALSE;
    }
    ActualLength = (U32)USB_MASS_STORAGE_COMMAND_STATUS_LENGTH - TransferLength;
    if (ActualLength != USB_MASS_STORAGE_COMMAND_STATUS_LENGTH) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("ShortCSW"));
        ERROR(TEXT("Short CSW length=%u Op=%x Tag=%x"),
              ActualLength,
              (U32)CommandBlock[0],
              ExpectedTag);
        return FALSE;
    }
    Device->LastCswStatus = CommandStatusWrapper->Status;
    Device->LastCswResidue = CommandStatusWrapper->DataResidue;

    if (CommandStatusWrapper->Signature != USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE ||
        CommandStatusWrapper->Tag != ExpectedTag) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("InvalidCSW"));
        ERROR(TEXT("Invalid CSW Op=%x Sig=%x Tag=%x ExpectedTag=%x Status=%x Residue=%u"),
              (U32)CommandBlock[0],
              (U32)CommandStatusWrapper->Signature,
              (U32)CommandStatusWrapper->Tag,
              ExpectedTag,
              (U32)CommandStatusWrapper->Status,
              (U32)CommandStatusWrapper->DataResidue);
        return FALSE;
    }

    if (CommandStatusWrapper->DataResidue > (U32)DataLength) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("InvalidResidue"));
        ERROR(TEXT("Invalid CSW residue=%u DataLen=%u Op=%x Tag=%x"),
              (U32)CommandStatusWrapper->DataResidue,
              (U32)DataLength,
              (U32)CommandBlock[0],
              ExpectedTag);
        return FALSE;
    }

    if (CommandStatusWrapper->Status == USB_MASS_STORAGE_COMMAND_STATUS_PHASE_ERROR) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("PhaseError"));
        ERROR(TEXT("CSW phase error residue=%u Op=%x Tag=%x"),
              (U32)CommandStatusWrapper->DataResidue,
              (U32)CommandBlock[0],
              ExpectedTag);
        return FALSE;
    }

    if (CommandStatusWrapper->Status == USB_MASS_STORAGE_COMMAND_STATUS_FAILED) {
        WARNING(TEXT("CSW status=%x residue=%u Op=%x Tag=%x"),
                (U32)CommandStatusWrapper->Status,
                (U32)CommandStatusWrapper->DataResidue,
                (U32)CommandBlock[0],
                ExpectedTag);
        return FALSE;
    }

    if (CommandStatusWrapper->Status != USB_MASS_STORAGE_COMMAND_STATUS_PASSED) {
        USBStorageRunBotResetRecovery(Device, CommandBlock[0], TEXT("UnknownCSWStatus"));
        ERROR(TEXT("Invalid CSW status=%x residue=%u Op=%x Tag=%x"),
                (U32)CommandStatusWrapper->Status,
                (U32)CommandStatusWrapper->DataResidue,
                (U32)CommandBlock[0],
                ExpectedTag);
        return FALSE;
    }

    Device->LastBotStage = 8;

    return TRUE;
}

/************************************************************************/
