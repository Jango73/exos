
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


    xHCI

\************************************************************************/

#include "drivers/usb/XHCI-Internal.h"
#include "drivers/usb/XHCI-Device-Internal.h"
#include "system/Clock.h"
#include "utils/ThresholdLatch.h"

/************************************************************************/

#define XHCI_ENUM_FAILURE_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENUM_FAILURE_LOG_INTERVAL_MS 2000
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_INTERVAL_MS 2000
#define XHCI_BULK_AVERAGE_TRB_LENGTH 3072

/************************************************************************/

/**
 * @brief Count active slots attached to one controller.
 * @param Device xHCI controller.
 * @return Number of active slot identifiers.
 */
static U32 XHCI_CountActiveSlots(LPXHCI_DEVICE Device) {
    U8 SlotSeen[256];
    U32 ActiveCount = 0;
    LPLIST UsbDeviceList;

    if (Device == NULL) {
        return 0;
    }

    MemorySet(SlotSeen, 0, sizeof(SlotSeen));

    UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return 0;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)Node;
        if (UsbDevice->Controller != Device) {
            continue;
        }
        if (!UsbDevice->Present || UsbDevice->SlotId == 0) {
            continue;
        }
        if (SlotSeen[UsbDevice->SlotId] != 0) {
            continue;
        }

        SlotSeen[UsbDevice->SlotId] = 1;
        ActiveCount++;
    }

    return ActiveCount;
}

/************************************************************************/

/**
 * @brief Emit one rate-limited state snapshot for EnableSlot timeout.
 * @param Device xHCI controller.
 */
static void XHCI_LogEnableSlotTimeoutState(LPXHCI_DEVICE Device) {
    static RATE_LIMITER DATA_SECTION EnableSlotTimeoutLimiter = {0};
    static BOOL DATA_SECTION EnableSlotTimeoutLimiterInitAttempted = FALSE;
    U32 Suppressed = 0;
    LINEAR InterrupterBase;
    U32 UsbStatus;
    U32 UsbCommand;
    U32 CrcrLow;
    U32 CrcrHigh;
    U32 Iman;
    U32 ErdpLow;
    U32 ErdpHigh;
    U32 ActiveSlots;
    U32 EventDword0 = 0;
    U32 EventDword1 = 0;
    U32 EventDword2 = 0;
    U32 EventDword3 = 0;
    U32 EventCycle = 0;
    U32 ExpectedCycle = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;

    if (Device == NULL) {
        return;
    }

    if (EnableSlotTimeoutLimiter.Initialized == FALSE && EnableSlotTimeoutLimiterInitAttempted == FALSE) {
        EnableSlotTimeoutLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&EnableSlotTimeoutLimiter,
                            XHCI_ENABLE_SLOT_TIMEOUT_LOG_IMMEDIATE_BUDGET,
                            XHCI_ENABLE_SLOT_TIMEOUT_LOG_INTERVAL_MS) == FALSE) {
            return;
        }
    }

    if (!RateLimiterShouldTrigger(&EnableSlotTimeoutLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    XHCI_LogHseTransitionIfNeeded(Device, TEXT("EnableSlotTimeout"));
    InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    UsbCommand = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    UsbStatus = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
    CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
    Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
    ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
    ActiveSlots = XHCI_CountActiveSlots(Device);
    PciCommand = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    PciStatus = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_STATUS);

    if (Device->EventRingLinear != 0) {
        LPXHCI_TRB EventRing = (LPXHCI_TRB)Device->EventRingLinear;
        U32 EventIndex = Device->EventRingDequeueIndex;
        XHCI_TRB Event = EventRing[EventIndex];
        EventDword0 = Event.Dword0;
        EventDword1 = Event.Dword1;
        EventDword2 = Event.Dword2;
        EventDword3 = Event.Dword3;
        EventCycle = (Event.Dword3 & XHCI_TRB_CYCLE) ? 1U : 0U;
        ExpectedCycle = Device->EventRingCycleState ? 1U : 0U;
    }

    WARNING(TEXT("USBCMD=%x USBSTS=%x PCICMD=%x PCISTS=%x CRCR=%x:%x IMAN=%x ERDP=%x:%x Slots=%u/%u CQ=%u Event=%x:%x:%x:%x Cy=%u/%u suppressed=%u"),
            UsbCommand,
            UsbStatus,
            (U32)PciCommand,
            (U32)PciStatus,
            CrcrHigh,
            CrcrLow,
            Iman,
            ErdpHigh,
            ErdpLow,
            ActiveSlots,
            (U32)Device->MaxSlots,
            Device->CompletionCount,
            EventDword3,
            EventDword2,
            EventDword1,
            EventDword0,
            EventCycle,
            ExpectedCycle,
            Suppressed);
}

/************************************************************************/

/**
 * @brief Convert endpoint address to xHCI DCI.
 * @param EndpointAddress USB endpoint address.
 * @return DCI index.
 */
static U8 XHCI_GetEndpointDci(U8 EndpointAddress) {
    U8 EndpointNumber = EndpointAddress & 0x0F;
    U8 DirectionIn = (EndpointAddress & 0x80) != 0 ? 1U : 0U;
    return (U8)((EndpointNumber * 2U) + DirectionIn);
}

/************************************************************************/

/**
 * @brief Build endpoint context DW1 for non-isochronous endpoints.
 * @param EndpointType Endpoint type value for xHCI context.
 * @param MaximumBurst Endpoint maximum burst value.
 * @param MaximumPacketSize Endpoint maximum packet size.
 * @return Encoded DW1 value.
 */
static U32 XHCI_BuildEndpointContextDword1(U32 EndpointType, U32 MaximumBurst, U32 MaximumPacketSize) {
    return (3U << 1) |
           ((EndpointType & 0x7U) << 3) |
           ((MaximumBurst & 0xFFU) << 8) |
           ((MaximumPacketSize & 0xFFFFU) << 16);
}

/************************************************************************/

/**
 * @brief Build endpoint context DW4 from average TRB length and Max ESIT payload.
 * @param AverageTrbLength Average TRB length.
 * @param MaximumEsitPayload Maximum ESIT payload.
 * @return Encoded DW4 value.
 */
static U32 XHCI_BuildEndpointContextDword4(U32 AverageTrbLength, U32 MaximumEsitPayload) {
    return (AverageTrbLength & 0xFFFFU) | ((MaximumEsitPayload & 0xFFFFU) << 16);
}

/************************************************************************/

/**
 * @brief Compute ceil(log2(Value)) for positive values.
 * @param Value Input value.
 * @return Ceil(log2(Value)).
 */
static U32 XHCI_Log2Ceil(U32 Value) {
    U32 Result = 0;
    U32 Base = 1;

    if (Value <= 1) {
        return 0;
    }

    while (Base < Value && Result < 31) {
        Base <<= 1;
        Result++;
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Compute xHCI interval field for an interrupt endpoint.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return Encoded interval field.
 */
static U32 XHCI_GetInterruptEndpointIntervalField(LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    U32 IntervalField = 0;
    U32 Microframes = 0;

    if (UsbDevice == NULL || Endpoint == NULL) {
        return 0;
    }

    if (Endpoint->Interval == 0) {
        return 0;
    }

    if (UsbDevice->SpeedId == USB_SPEED_HS || UsbDevice->SpeedId == USB_SPEED_SS) {
        IntervalField = (U32)Endpoint->Interval - 1U;
        return (IntervalField > 15U) ? 15U : IntervalField;
    }

    Microframes = (U32)Endpoint->Interval * 8U;
    IntervalField = XHCI_Log2Ceil(Microframes);
    if (IntervalField < 3U) {
        IntervalField = 3U;
    }
    if (IntervalField > 10U) {
        IntervalField = 10U;
    }

    return IntervalField;
}

/************************************************************************/

/**
 * @brief Compute Max Burst field for one interrupt endpoint context.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return Encoded Max Burst value.
 */
static U32 XHCI_GetInterruptEndpointMaximumBurst(LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    if (UsbDevice == NULL || Endpoint == NULL) {
        return 0;
    }

    if (UsbDevice->SpeedId == USB_SPEED_SS && Endpoint->HasSuperSpeedCompanion) {
        return (U32)Endpoint->MaximumBurst;
    }

    if (UsbDevice->SpeedId == USB_SPEED_HS) {
        return (U32)((Endpoint->MaxPacketSize >> 11) & 0x3U);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Compute Max ESIT payload for one interrupt endpoint context.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return Maximum ESIT payload in bytes.
 */
static U32 XHCI_GetInterruptEndpointMaximumEsitPayload(LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    U32 MaximumPacketSize;
    U32 MaximumBurst;

    if (UsbDevice == NULL || Endpoint == NULL) {
        return 0;
    }

    if (UsbDevice->SpeedId == USB_SPEED_SS && Endpoint->HasSuperSpeedCompanion &&
        Endpoint->CompanionBytesPerInterval != 0) {
        return (U32)Endpoint->CompanionBytesPerInterval;
    }

    MaximumPacketSize = (U32)Endpoint->MaxPacketSize & 0x7FFU;
    MaximumBurst = XHCI_GetInterruptEndpointMaximumBurst(UsbDevice, Endpoint);
    return MaximumPacketSize * (MaximumBurst + 1U);
}

/************************************************************************/

/**
 * @brief Return TRUE when one endpoint context is already configured.
 * @param Device xHCI device.
 * @param UsbDevice USB device.
 * @param Dci Endpoint DCI.
 * @return TRUE when endpoint state is not disabled.
 */
static BOOL XHCI_IsEndpointConfigured(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    LPXHCI_CONTEXT_32 EndpointContext;
    U32 EndpointState;

    if (Device == NULL || UsbDevice == NULL || UsbDevice->DeviceContextLinear == 0 || Dci == 0) {
        return FALSE;
    }

    // Device Context layout has slot context at index 0, then endpoint contexts at DCI index.
    EndpointContext = XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)Dci);
    EndpointState = EndpointContext->Dword0 & 0x7U;
    return (EndpointState != 0U) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Emit one snapshot of bulk endpoint context state around configure failure.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @param Completion Configure Endpoint completion code.
 * @param TimedOut TRUE when command completion timed out.
 * @param DeviceEndpointStateBefore Endpoint state observed in device context before configure.
 */
static void XHCI_LogBulkEndpointContextState(LPXHCI_DEVICE Device,
                                             LPXHCI_USB_DEVICE UsbDevice,
                                             LPXHCI_USB_ENDPOINT Endpoint,
                                             U32 Completion,
                                             BOOL TimedOut,
                                             U32 DeviceEndpointStateBefore) {
    LPXHCI_CONTEXT_32 InputControl;
    LPXHCI_CONTEXT_32 InputSlot;
    LPXHCI_CONTEXT_32 InputEndpoint;
    LPXHCI_CONTEXT_32 DeviceSlot;
    LPXHCI_CONTEXT_32 DeviceEndpoint;
    U32 InputAddFlags = 0;
    U32 InputDropFlags = 0;
    U32 InputSlotEntries = 0;
    U32 DeviceSlotEntries = 0;
    U32 InputEndpointState = 0;
    U32 DeviceEndpointStateAfter = 0;
    U32 InputEpDword1 = 0;
    U32 InputEpDword2 = 0;
    U32 InputEpDword3 = 0;
    U32 InputEpDword4 = 0;
    U32 DeviceEpDword1 = 0;
    U32 DeviceEpDword2 = 0;
    U32 DeviceEpDword3 = 0;
    U32 DeviceEpDword4 = 0;

    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || Endpoint->Dci == 0) {
        return;
    }

    if (UsbDevice->InputContextLinear != 0) {
        InputControl = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
        InputSlot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
        InputEndpoint =
                XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, (U32)Endpoint->Dci + 1);

        InputAddFlags = InputControl->Dword1;
        InputDropFlags = InputControl->Dword0;
        InputSlotEntries = (InputSlot->Dword0 >> XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT) & 0x1F;
        InputEndpointState = InputEndpoint->Dword0 & 0x7;
        InputEpDword1 = InputEndpoint->Dword1;
        InputEpDword2 = InputEndpoint->Dword2;
        InputEpDword3 = InputEndpoint->Dword3;
        InputEpDword4 = InputEndpoint->Dword4;
    }

    if (UsbDevice->DeviceContextLinear != 0) {
        DeviceSlot = XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
        DeviceEndpoint = XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)Endpoint->Dci);

        DeviceSlotEntries = (DeviceSlot->Dword0 >> XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT) & 0x1F;
        DeviceEndpointStateAfter = DeviceEndpoint->Dword0 & 0x7;
        DeviceEpDword1 = DeviceEndpoint->Dword1;
        DeviceEpDword2 = DeviceEndpoint->Dword2;
        DeviceEpDword3 = DeviceEndpoint->Dword3;
        DeviceEpDword4 = DeviceEndpoint->Dword4;
    }

    WARNING(TEXT("Slot=%x DCI=%x EP=%x Attr=%x MPS=%u Completion=%u TimedOut=%u Add=%x Drop=%x InEntries=%u DevEntries=%u InEpState=%x DevEpState=%x->%x InEp=%x:%x:%x:%x DevEp=%x:%x:%x:%x"),
            (U32)UsbDevice->SlotId,
            (U32)Endpoint->Dci,
            (U32)Endpoint->Address,
            (U32)Endpoint->Attributes,
            (U32)Endpoint->MaxPacketSize,
            Completion,
            TimedOut ? 1 : 0,
            InputAddFlags,
            InputDropFlags,
            InputSlotEntries,
            DeviceSlotEntries,
            InputEndpointState,
            DeviceEndpointStateBefore,
            DeviceEndpointStateAfter,
            InputEpDword1,
            InputEpDword2,
            InputEpDword3,
            InputEpDword4,
            DeviceEpDword1,
            DeviceEpDword2,
            DeviceEpDword3,
            DeviceEpDword4);
}

/************************************************************************/

/**
 * @brief Update slot Context Entries with the maximum between current and requested DCI.
 * @param SlotContext Slot context in the input context.
 * @param Dci Endpoint DCI to cover.
 */
static void XHCI_SetSlotContextEntriesForDci(LPXHCI_CONTEXT_32 SlotContext, U8 Dci) {
    U32 CurrentEntries;
    U32 TargetEntries;

    if (SlotContext == NULL || Dci == 0) {
        return;
    }

    CurrentEntries = (SlotContext->Dword0 >> XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT) & 0x1FU;
    TargetEntries = (U32)Dci;
    if (TargetEntries < CurrentEntries) {
        TargetEntries = CurrentEntries;
    }
    if (TargetEntries == 0) {
        TargetEntries = 1;
    }

    SlotContext->Dword0 &= ~(0x1FU << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    SlotContext->Dword0 |= ((TargetEntries & 0x1FU) << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
}

/************************************************************************/

/**
 * @brief Get the selected configuration for a device.
 * @param UsbDevice USB device state.
 * @return Pointer to configuration or NULL.
 */
LPXHCI_USB_CONFIGURATION XHCI_GetSelectedConfig(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL || UsbDevice->Configs == NULL || UsbDevice->ConfigCount == 0) {
        return NULL;
    }

    if (UsbDevice->SelectedConfigValue == 0) {
        return &UsbDevice->Configs[0];
    }

    for (UINT Index = 0; Index < UsbDevice->ConfigCount; Index++) {
        if (UsbDevice->Configs[Index].ConfigurationValue == UsbDevice->SelectedConfigValue) {
            return &UsbDevice->Configs[Index];
        }
    }

    return &UsbDevice->Configs[0];
}

/************************************************************************/

/**
 * @brief Detect whether a USB device is a hub.
 * @param UsbDevice USB device state.
 * @return TRUE when the device exposes the hub class.
 */
/*
static BOOL XHCI_IsHubDevice(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    if (UsbDevice->DeviceDescriptor.DeviceClass == USB_CLASS_HUB) {
        return TRUE;
    }

    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return FALSE;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass == USB_CLASS_HUB) {
            return TRUE;
        }
    }

    return FALSE;
}
*/

/************************************************************************/

/**
 * @brief Locate an endpoint in an interface by type and direction.
 * @param Interface USB interface.
 * @param EndpointType Endpoint type.
 * @param DirectionIn TRUE for IN endpoints, FALSE for OUT.
 * @return Endpoint pointer or NULL.
 */
LPXHCI_USB_ENDPOINT XHCI_FindInterfaceEndpoint(LPXHCI_USB_INTERFACE Interface, U8 EndpointType, BOOL DirectionIn) {
    if (Interface == NULL) {
        return NULL;
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
        if (Endpoint->Parent != (LPLISTNODE)Interface) {
            continue;
        }
        if ((Endpoint->Attributes & 0x03) != EndpointType) {
            continue;
        }
        if (DirectionIn) {
            if ((Endpoint->Address & 0x80) == 0) {
                continue;
            }
        } else {
            if ((Endpoint->Address & 0x80) != 0) {
                continue;
            }
        }

        return Endpoint;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Locate the interrupt IN endpoint for a hub device.
 * @param UsbDevice USB device state.
 * @return Endpoint pointer or NULL.
 */
LPXHCI_USB_ENDPOINT XHCI_FindHubInterruptEndpoint(LPXHCI_USB_DEVICE UsbDevice) {
    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return NULL;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass != USB_CLASS_HUB) {
            continue;
        }

        return XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_INTERRUPT, TRUE);
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initialize a transfer ring.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical base.
 * @param LinearOut Receives linear base.
 * @param CycleStateOut Receives cycle state.
 * @param EnqueueIndexOut Receives enqueue index.
 * @return TRUE on success.
 */
BOOL XHCI_InitTransferRingCore(LPCSTR Tag, PHYSICAL* PhysicalOut, LINEAR* LinearOut,
                               U32* CycleStateOut, U32* EnqueueIndexOut) {
    if (PhysicalOut == NULL || LinearOut == NULL || CycleStateOut == NULL || EnqueueIndexOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(Tag, PhysicalOut, LinearOut)) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)(*LinearOut);
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(*PhysicalOut);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    *CycleStateOut = 1;
    *EnqueueIndexOut = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize an endpoint transfer ring.
 * @param Endpoint Endpoint descriptor.
 * @param Tag Allocation tag.
 * @return TRUE on success.
 */
static BOOL XHCI_InitEndpointRing(LPXHCI_USB_ENDPOINT Endpoint, LPCSTR Tag) {
    if (Endpoint == NULL) {
        return FALSE;
    }

    return XHCI_InitTransferRingCore(Tag,
                                     &Endpoint->TransferRingPhysical,
                                     &Endpoint->TransferRingLinear,
                                     &Endpoint->TransferRingCycleState,
                                     &Endpoint->TransferRingEnqueueIndex);
}
/************************************************************************/

/**
 * @brief Populate an input context for Address Device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
void XHCI_BuildInputContextForAddress(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << 1);

    LPXHCI_CONTEXT_32 Slot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    Slot->Dword0 = (UsbDevice->RouteString & XHCI_SLOT_CTX_ROUTE_STRING_MASK) |
                   ((U32)UsbDevice->SpeedId << XHCI_SLOT_CTX_SPEED_SHIFT) |
                   (1U << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    if (UsbDevice->IsHub) {
        Slot->Dword0 |= XHCI_SLOT_CTX_HUB;
    }

    Slot->Dword1 = ((U32)UsbDevice->RootPortNumber << XHCI_SLOT_CTX_ROOT_PORT_SHIFT);
    if (UsbDevice->IsHub && UsbDevice->HubPortCount != 0) {
        Slot->Dword1 |= ((U32)UsbDevice->HubPortCount << XHCI_SLOT_CTX_PORT_COUNT_SHIFT);
    }

    LPXHCI_USB_DEVICE Parent = (LPXHCI_USB_DEVICE)UsbDevice->Parent;
    if (Parent != NULL) {
        if ((Parent->SpeedId == USB_SPEED_HS) &&
            (UsbDevice->SpeedId == USB_SPEED_LS || UsbDevice->SpeedId == USB_SPEED_FS)) {
            Slot->Dword2 = ((U32)Parent->SlotId << XHCI_SLOT_CTX_TT_HUB_SLOT_SHIFT) |
                           ((U32)UsbDevice->ParentPort << XHCI_SLOT_CTX_TT_PORT_SHIFT);
        }
    }

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = XHCI_BuildEndpointContextDword1(4, 0, (U32)UsbDevice->MaxPacketSize0);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Populate an input context for updating EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
void XHCI_BuildInputContextForEp0(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 1);

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = XHCI_BuildEndpointContextDword1(4, 0, (U32)UsbDevice->MaxPacketSize0);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Enable a new device slot.
 * @param Device xHCI device.
 * @param SlotIdOut Receives allocated slot ID.
 * @return TRUE on success.
 */
BOOL XHCI_EnableSlot(LPXHCI_DEVICE Device, U8* SlotIdOut, U32* CompletionOut) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U8 SlotId = 0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, &SlotId, &Completion)) {
        if (CompletionOut != NULL) {
            *CompletionOut = XHCI_ENUM_COMPLETION_TIMEOUT;
        }
        XHCI_LogEnableSlotTimeoutState(Device);
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        if (CompletionOut != NULL) {
            *CompletionOut = Completion;
        }
        ERROR(TEXT("Completion code %u"), Completion);
        return FALSE;
    }

    if (SlotIdOut != NULL) {
        *SlotIdOut = SlotId;
    }
    if (CompletionOut != NULL) {
        *CompletionOut = Completion;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Address a device with a prepared input context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_AddressDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Evaluate context to update EP0 parameters.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_EvaluateContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_EVALUATE_CONTEXT << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Configure endpoint contexts after a SET_CONFIGURATION.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_ConfigureEndpoint(LPXHCI_DEVICE Device,
                                   LPXHCI_USB_DEVICE UsbDevice,
                                   U32* CompletionOut,
                                   BOOL* TimedOutOut) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    if (CompletionOut != NULL) {
        *CompletionOut = 0;
    }
    if (TimedOutOut != NULL) {
        *TimedOutOut = FALSE;
    }

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_CONFIGURE_ENDPOINT << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        if (TimedOutOut != NULL) {
            *TimedOutOut = TRUE;
        }
        WARNING(TEXT("Timeout Slot=%x USBCMD=%x USBSTS=%x"),
                (U32)UsbDevice->SlotId,
                XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD),
                XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS));
        return FALSE;
    }

    if (CompletionOut != NULL) {
        *CompletionOut = Completion;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("Completion code %u Slot=%x"), Completion, (U32)UsbDevice->SlotId);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Add an interrupt IN endpoint to the device context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return TRUE on success.
 */
BOOL XHCI_AddInterruptEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (Endpoint->TransferRingLinear == 0 || Endpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(Endpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }

    Endpoint->Dci = XHCI_GetEndpointDci(Endpoint->Address);

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << Endpoint->Dci);

    LPVOID SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    LPVOID SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);

    {
        LPXHCI_CONTEXT_32 Slot = (LPXHCI_CONTEXT_32)SlotOut;
        XHCI_SetSlotContextEntriesForDci(Slot, Endpoint->Dci);
    }

    LPXHCI_CONTEXT_32 EpCtx = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, (U32)Endpoint->Dci + 1U);
    U32 EpType = 0;
    U32 IntervalField;
    U32 MaximumBurst;
    U32 MaxPacket;
    U32 MaximumEsitPayload;
    if ((Endpoint->Attributes & 0x03) == USB_ENDPOINT_TYPE_INTERRUPT) {
        EpType = ((Endpoint->Address & 0x80) != 0) ? 7U : 3U;
    }
    IntervalField = XHCI_GetInterruptEndpointIntervalField(UsbDevice, Endpoint);
    MaximumBurst = XHCI_GetInterruptEndpointMaximumBurst(UsbDevice, Endpoint);
    MaxPacket = ((U32)Endpoint->MaxPacketSize & 0x7FFU);
    MaximumEsitPayload = XHCI_GetInterruptEndpointMaximumEsitPayload(UsbDevice, Endpoint);

    EpCtx->Dword0 = (IntervalField << 16);
    EpCtx->Dword1 = XHCI_BuildEndpointContextDword1(EpType, MaximumBurst, MaxPacket);

    {
        U64 Dequeue = U64_FromUINT(Endpoint->TransferRingPhysical);
        EpCtx->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        EpCtx->Dword2 |= (Endpoint->TransferRingCycleState ? 1U : 0U);
        EpCtx->Dword3 = U64_High32(Dequeue);
        EpCtx->Dword4 = XHCI_BuildEndpointContextDword4(MaximumEsitPayload, MaximumEsitPayload);
    }

    return XHCI_ConfigureEndpoint(Device, UsbDevice, NULL, NULL);
}

/************************************************************************/

/**
 * @brief Build one bulk endpoint context in input context.
 * @param EpCtx Endpoint context output pointer.
 * @param Endpoint USB endpoint descriptor.
 */
static void XHCI_BuildBulkEndpointContext(LPXHCI_CONTEXT_32 EpCtx, LPXHCI_USB_ENDPOINT Endpoint) {
    U32 EpType;
    U32 MaximumPacketSize;
    U64 Dequeue;

    if (EpCtx == NULL || Endpoint == NULL) {
        return;
    }

    EpType = ((Endpoint->Address & 0x80) != 0) ? 6U : 2U;
    MaximumPacketSize = ((U32)Endpoint->MaxPacketSize & 0x7FFU);
    Dequeue = U64_FromUINT(Endpoint->TransferRingPhysical);

    EpCtx->Dword0 = 0;
    EpCtx->Dword1 = XHCI_BuildEndpointContextDword1(EpType, (U32)Endpoint->MaximumBurst, MaximumPacketSize);
    EpCtx->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
    EpCtx->Dword2 |= (Endpoint->TransferRingCycleState ? 1U : 0U);
    EpCtx->Dword3 = U64_High32(Dequeue);
    EpCtx->Dword4 = XHCI_BuildEndpointContextDword4(XHCI_BULK_AVERAGE_TRB_LENGTH, 0);
}

/************************************************************************/

/**
 * @brief Add a bulk endpoint to the device context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return TRUE on success.
 */
BOOL XHCI_AddBulkEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_ENDPOINT Endpoint) {
    U32 ConfigureCompletion = 0;
    BOOL ConfigureTimedOut = FALSE;
    U32 DeviceEndpointStateBefore = 0;

    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (Endpoint->TransferRingLinear == 0 || Endpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(Endpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }

    Endpoint->Dci = XHCI_GetEndpointDci(Endpoint->Address);
    if (XHCI_IsEndpointConfigured(Device, UsbDevice, Endpoint->Dci)) {
        return TRUE;
    }

    if (UsbDevice->DeviceContextLinear != 0 && Endpoint->Dci != 0) {
        LPXHCI_CONTEXT_32 DeviceEndpoint =
                XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)Endpoint->Dci);
        DeviceEndpointStateBefore = DeviceEndpoint->Dword0 & 0x7;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << Endpoint->Dci);

    LPVOID SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    LPVOID SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);

    {
        LPXHCI_CONTEXT_32 Slot = (LPXHCI_CONTEXT_32)SlotOut;
        XHCI_SetSlotContextEntriesForDci(Slot, Endpoint->Dci);
    }

    LPXHCI_CONTEXT_32 EpCtx =
            XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, (U32)Endpoint->Dci + 1U);
    XHCI_BuildBulkEndpointContext(EpCtx, Endpoint);

    if (!XHCI_ConfigureEndpoint(Device, UsbDevice, &ConfigureCompletion, &ConfigureTimedOut)) {
        if (XHCI_IsEndpointConfigured(Device, UsbDevice, Endpoint->Dci)) {
            return TRUE;
        }
        XHCI_LogBulkEndpointContextState(Device,
                                         UsbDevice,
                                         Endpoint,
                                         ConfigureCompletion,
                                         ConfigureTimedOut,
                                         DeviceEndpointStateBefore);
        WARNING(TEXT("Configure failed Slot=%x DCI=%x EP=%x MPS=%u Completion=%u TimedOut=%u"),
                (U32)UsbDevice->SlotId,
                (U32)Endpoint->Dci,
                (U32)Endpoint->Address,
                (U32)Endpoint->MaxPacketSize,
                ConfigureCompletion,
                ConfigureTimedOut ? 1 : 0);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Add BOT bulk OUT and IN endpoints in one configure transaction.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param BulkOutEndpoint Bulk OUT endpoint descriptor.
 * @param BulkInEndpoint Bulk IN endpoint descriptor.
 * @return TRUE on success.
 */
BOOL XHCI_AddBulkEndpointPair(LPXHCI_DEVICE Device,
                              LPXHCI_USB_DEVICE UsbDevice,
                              LPXHCI_USB_ENDPOINT BulkOutEndpoint,
                              LPXHCI_USB_ENDPOINT BulkInEndpoint) {
    U32 ConfigureCompletion = 0;
    BOOL ConfigureTimedOut = FALSE;
    BOOL NeedOut;
    BOOL NeedIn;
    U32 DeviceOutStateBefore = 0;
    U32 DeviceInStateBefore = 0;
    LPXHCI_CONTEXT_32 Control;
    LPVOID SlotIn;
    LPVOID SlotOut;
    LPXHCI_CONTEXT_32 Slot;

    if (Device == NULL || UsbDevice == NULL || BulkOutEndpoint == NULL || BulkInEndpoint == NULL) {
        return FALSE;
    }

    if ((BulkOutEndpoint->Address & 0x80) != 0 || (BulkInEndpoint->Address & 0x80) == 0) {
        WARNING(TEXT("Invalid endpoint directions Out=%x In=%x"),
                (U32)BulkOutEndpoint->Address,
                (U32)BulkInEndpoint->Address);
        return FALSE;
    }

    if ((BulkOutEndpoint->Attributes & 0x03) != USB_ENDPOINT_TYPE_BULK ||
        (BulkInEndpoint->Attributes & 0x03) != USB_ENDPOINT_TYPE_BULK) {
        WARNING(TEXT("Invalid endpoint type OutAttr=%x InAttr=%x"),
                (U32)BulkOutEndpoint->Attributes,
                (U32)BulkInEndpoint->Attributes);
        return FALSE;
    }

    if (BulkOutEndpoint->TransferRingLinear == 0 || BulkOutEndpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(BulkOutEndpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }
    if (BulkInEndpoint->TransferRingLinear == 0 || BulkInEndpoint->TransferRingPhysical == 0) {
        if (!XHCI_InitEndpointRing(BulkInEndpoint, TEXT("XHCI_EpRing"))) {
            return FALSE;
        }
    }

    BulkOutEndpoint->Dci = XHCI_GetEndpointDci(BulkOutEndpoint->Address);
    BulkInEndpoint->Dci = XHCI_GetEndpointDci(BulkInEndpoint->Address);
    NeedOut = !XHCI_IsEndpointConfigured(Device, UsbDevice, BulkOutEndpoint->Dci);
    NeedIn = !XHCI_IsEndpointConfigured(Device, UsbDevice, BulkInEndpoint->Dci);
    if (!NeedOut && !NeedIn) {
        return TRUE;
    }

    if (UsbDevice->DeviceContextLinear != 0) {
        LPXHCI_CONTEXT_32 DeviceOutContext =
                XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)BulkOutEndpoint->Dci);
        LPXHCI_CONTEXT_32 DeviceInContext =
                XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)BulkInEndpoint->Dci);
        DeviceOutStateBefore = DeviceOutContext->Dword0 & 0x7;
        DeviceInStateBefore = DeviceInContext->Dword0 & 0x7;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0);
    if (NeedOut) {
        Control->Dword1 |= (1U << BulkOutEndpoint->Dci);
    }
    if (NeedIn) {
        Control->Dword1 |= (1U << BulkInEndpoint->Dci);
    }

    SlotIn = (LPVOID)XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, 0);
    SlotOut = (LPVOID)XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    MemoryCopy(SlotOut, SlotIn, Device->ContextSize);
    Slot = (LPXHCI_CONTEXT_32)SlotOut;
    if (NeedOut) {
        XHCI_SetSlotContextEntriesForDci(Slot, BulkOutEndpoint->Dci);
    }
    if (NeedIn) {
        XHCI_SetSlotContextEntriesForDci(Slot, BulkInEndpoint->Dci);
    }

    if (NeedOut) {
        LPXHCI_CONTEXT_32 OutContext = XHCI_GetContextPointer(UsbDevice->InputContextLinear,
                                                              Device->ContextSize,
                                                              (U32)BulkOutEndpoint->Dci + 1U);
        XHCI_BuildBulkEndpointContext(OutContext, BulkOutEndpoint);
    }
    if (NeedIn) {
        LPXHCI_CONTEXT_32 InContext = XHCI_GetContextPointer(UsbDevice->InputContextLinear,
                                                             Device->ContextSize,
                                                             (U32)BulkInEndpoint->Dci + 1U);
        XHCI_BuildBulkEndpointContext(InContext, BulkInEndpoint);
    }

    if (!XHCI_ConfigureEndpoint(Device, UsbDevice, &ConfigureCompletion, &ConfigureTimedOut)) {
        BOOL OutConfigured = XHCI_IsEndpointConfigured(Device, UsbDevice, BulkOutEndpoint->Dci);
        BOOL InConfigured = XHCI_IsEndpointConfigured(Device, UsbDevice, BulkInEndpoint->Dci);
        if ((!NeedOut || OutConfigured) && (!NeedIn || InConfigured)) {
            return TRUE;
        }

        if (NeedOut) {
            XHCI_LogBulkEndpointContextState(Device,
                                             UsbDevice,
                                             BulkOutEndpoint,
                                             ConfigureCompletion,
                                             ConfigureTimedOut,
                                             DeviceOutStateBefore);
        }
        if (NeedIn) {
            XHCI_LogBulkEndpointContextState(Device,
                                             UsbDevice,
                                             BulkInEndpoint,
                                             ConfigureCompletion,
                                             ConfigureTimedOut,
                                             DeviceInStateBefore);
        }

        WARNING(TEXT("Configure failed Slot=%x OutDci=%x InDci=%x OutEp=%x InEp=%x Completion=%u TimedOut=%u NeedOut=%u NeedIn=%u"),
                (U32)UsbDevice->SlotId,
                (U32)BulkOutEndpoint->Dci,
                (U32)BulkInEndpoint->Dci,
                (U32)BulkOutEndpoint->Address,
                (U32)BulkInEndpoint->Address,
                ConfigureCompletion,
                ConfigureTimedOut ? 1 : 0,
                NeedOut ? 1U : 0U,
                NeedIn ? 1U : 0U);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Update slot context for hub information.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_UpdateHubSlotContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0);

    LPXHCI_CONTEXT_32 Slot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    Slot->Dword0 = (UsbDevice->RouteString & XHCI_SLOT_CTX_ROUTE_STRING_MASK) |
                   ((U32)UsbDevice->SpeedId << XHCI_SLOT_CTX_SPEED_SHIFT) |
                   XHCI_SLOT_CTX_HUB |
                   (1U << XHCI_SLOT_CTX_CONTEXT_ENTRIES_SHIFT);
    Slot->Dword1 = ((U32)UsbDevice->RootPortNumber << XHCI_SLOT_CTX_ROOT_PORT_SHIFT) |
                   ((U32)UsbDevice->HubPortCount << XHCI_SLOT_CTX_PORT_COUNT_SHIFT);

    return XHCI_EvaluateContext(Device, UsbDevice);
}

/************************************************************************/

/**
 * @brief Submit a normal transfer TRB on one endpoint transfer ring.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @param BufferPhysical Physical address of the transfer buffer.
 * @param Length Transfer length in bytes.
 * @param InterruptOnShortPacket TRUE to set ISP on the submitted TRB.
 * @param TrbPhysicalOut Receives submitted TRB physical address.
 * @return TRUE on success.
 */
BOOL XHCI_SubmitNormalTransfer(LPXHCI_DEVICE Device,
                               LPXHCI_USB_DEVICE UsbDevice,
                               LPXHCI_USB_ENDPOINT Endpoint,
                               PHYSICAL BufferPhysical,
                               U32 Length,
                               BOOL InterruptOnShortPacket,
                               U64* TrbPhysicalOut) {
    XHCI_TRB Trb;

    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || BufferPhysical == 0) {
        return FALSE;
    }

    XHCI_ClearTransferCompletions(Device, UsbDevice->SlotId, Endpoint->Dci);

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(BufferPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(BufferPhysical));
    Trb.Dword2 = Length;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (InterruptOnShortPacket) {
        Trb.Dword3 |= XHCI_TRB_ISP;
    }

    if (!XHCI_RingEnqueue(Endpoint->TransferRingLinear,
                          Endpoint->TransferRingPhysical,
                          &Endpoint->TransferRingEnqueueIndex,
                          &Endpoint->TransferRingCycleState,
                          XHCI_TRANSFER_RING_TRBS,
                          &Trb,
                          TrbPhysicalOut)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, UsbDevice->SlotId, Endpoint->Dci);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Perform a control transfer on EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Setup Setup packet.
 * @param Buffer Data buffer (optional).
 * @param Length Data length.
 * @param DirectionIn TRUE if data is IN.
 * @return TRUE on success.
 */
BOOL XHCI_ControlTransfer(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, const USB_SETUP_PACKET* Setup,
                                 PHYSICAL BufferPhysical, LPVOID BufferLinear, U16 Length, BOOL DirectionIn) {
    XHCI_TRB SetupTrb;
    XHCI_TRB DataTrb;
    XHCI_TRB StatusTrb;
    U64 StatusPhysical = U64_0;
    U32 Completion = 0;
    U32 TransferType = XHCI_TRB_TRT_NO_DATA;

    if (Setup == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet(&SetupTrb, 0, sizeof(SetupTrb));
    MemoryCopy(&SetupTrb.Dword0, Setup, sizeof(USB_SETUP_PACKET));
    SetupTrb.Dword2 = 8;
    if (Length > 0) {
        TransferType = DirectionIn ? XHCI_TRB_TRT_IN_DATA : XHCI_TRB_TRT_OUT_DATA;
    }
    SetupTrb.Dword3 = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) |
                      XHCI_TRB_IDT |
                      (TransferType << XHCI_TRB_TRT_SHIFT);

    if (!XHCI_TransferRingEnqueue(UsbDevice, &SetupTrb, NULL)) {
        return FALSE;
    }

    if (Length > 0 && BufferLinear != NULL && BufferPhysical != 0) {
        MemorySet(&DataTrb, 0, sizeof(DataTrb));
        DataTrb.Dword0 = U64_Low32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword1 = U64_High32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword2 = Length;
        DataTrb.Dword3 = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);
        if (DirectionIn) {
            DataTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }

        if (!XHCI_TransferRingEnqueue(UsbDevice, &DataTrb, NULL)) {
            return FALSE;
        }
    }

    MemorySet(&StatusTrb, 0, sizeof(StatusTrb));
    StatusTrb.Dword3 = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (Length > 0) {
        if (!DirectionIn) {
            StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }
    } else {
        StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
    }

    if (!XHCI_TransferRingEnqueue(UsbDevice, &StatusTrb, &StatusPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, UsbDevice->SlotId, XHCI_EP0_DCI);

    if (!XHCI_WaitForTransferCompletion(Device, StatusPhysical, &Completion)) {
        return FALSE;
    }

    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        return TRUE;
    }

    if (Completion == XHCI_COMPLETION_STALL_ERROR) {
        USB_SETUP_PACKET ClearFeature;
        MemorySet(&ClearFeature, 0, sizeof(ClearFeature));
        ClearFeature.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_ENDPOINT;
        ClearFeature.Request = USB_REQUEST_CLEAR_FEATURE;
        ClearFeature.Value = USB_FEATURE_ENDPOINT_HALT;
        ClearFeature.Index = 0;
        ClearFeature.Length = 0;
        (void)XHCI_ControlTransfer(Device, UsbDevice, &ClearFeature, 0, NULL, 0, FALSE);
    }

    ERROR(TEXT("Completion code %u"), Completion);
    return FALSE;
}
