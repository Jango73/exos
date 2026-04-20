
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


    xHCI

\************************************************************************/

#include "drivers/usb/XHCI-Internal.h"

/************************************************************************/

/**
 * @brief Read hub descriptor and return port count.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param PortCountOut Receives port count.
 * @return TRUE on success.
 */
static BOOL XHCI_ReadHubDescriptor(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8* PortCountOut) {
    USB_SETUP_PACKET Setup;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;
    U8 DescriptorType = (Hub->SpeedId == USB_SPEED_SS) ? USB_DESCRIPTOR_TYPE_SUPERSPEED_HUB : USB_DESCRIPTOR_TYPE_HUB;
    U8 PortCount = 0;

    if (PortCountOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_HubDesc"), &Physical, &Linear)) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_DEVICE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (U16)(DescriptorType << 8);
    Setup.Index = 0;
    Setup.Length = 8;

    if (!XHCI_ControlTransfer(Device, Hub, &Setup, Physical, (LPVOID)Linear, Setup.Length, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    PortCount = ((U8*)Linear)[2];
    *PortCountOut = PortCount;

    FreeRegion(Linear, PAGE_SIZE);
    FreePhysicalPage(Physical);
    return (PortCount != 0);
}

/************************************************************************/

/**
 * @brief Send a hub class request to set a port feature.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param Port Port number (1-based).
 * @param Feature Feature selector.
 * @return TRUE on success.
 */
static BOOL XHCI_HubSetPortFeature(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8 Port, U16 Feature) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER;
    Setup.Request = USB_REQUEST_SET_FEATURE;
    Setup.Value = Feature;
    Setup.Index = Port;
    Setup.Length = 0;
    return XHCI_ControlTransfer(Device, Hub, &Setup, 0, NULL, 0, FALSE);
}

/************************************************************************/

/**
 * @brief Send a hub class request to clear a port feature.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param Port Port number (1-based).
 * @param Feature Feature selector.
 * @return TRUE on success.
 */
static BOOL XHCI_HubClearPortFeature(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8 Port, U16 Feature) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER;
    Setup.Request = USB_REQUEST_CLEAR_FEATURE;
    Setup.Value = Feature;
    Setup.Index = Port;
    Setup.Length = 0;
    return XHCI_ControlTransfer(Device, Hub, &Setup, 0, NULL, 0, FALSE);
}

/************************************************************************/

/**
 * @brief Get hub port status.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param Port Port number (1-based).
 * @param StatusOut Receives port status.
 * @return TRUE on success.
 */
static BOOL XHCI_HubGetPortStatus(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8 Port, USB_PORT_STATUS* StatusOut) {
    USB_SETUP_PACKET Setup;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;

    if (StatusOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_HubPortStatus"), &Physical, &Linear)) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_OTHER;
    Setup.Request = USB_REQUEST_GET_STATUS;
    Setup.Value = 0;
    Setup.Index = Port;
    Setup.Length = sizeof(USB_PORT_STATUS);

    if (!XHCI_ControlTransfer(Device, Hub, &Setup, Physical, (LPVOID)Linear, Setup.Length, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemoryCopy(StatusOut, (LPVOID)Linear, sizeof(USB_PORT_STATUS));
    FreeRegion(Linear, PAGE_SIZE);
    FreePhysicalPage(Physical);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset a hub port and wait for completion.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param Port Port number (1-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ResetHubPort(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8 Port) {
    if (!XHCI_HubSetPortFeature(Device, Hub, Port, USB_HUB_FEATURE_PORT_RESET)) {
        return FALSE;
    }

    U32 Timeout = XHCI_PORT_RESET_TIMEOUT_MS;
    USB_PORT_STATUS Status;
    MemorySet(&Status, 0, sizeof(Status));

    while (Timeout > 0) {
        if (XHCI_HubGetPortStatus(Device, Hub, Port, &Status)) {
            if ((Status.Change & USB_HUB_PORT_CHANGE_RESET) != 0) {
                (void)XHCI_HubClearPortFeature(Device, Hub, Port, USB_HUB_FEATURE_C_PORT_RESET);
                break;
            }
        }
        Sleep(1);
        Timeout--;
    }

    if (Timeout == 0) {
        WARNING(TEXT("[XHCI_ResetHubPort] Timeout %u ms (Port=%u)"), XHCI_PORT_RESET_TIMEOUT_MS, Port);
    }

    return (Timeout != 0);
}

/************************************************************************/

/**
 * @brief Resolve device speed from hub port status.
 * @param Hub Hub device.
 * @param Status Port status.
 * @return USB speed code.
 */
static U8 XHCI_GetHubPortSpeed(LPXHCI_USB_DEVICE Hub, const USB_PORT_STATUS* Status) {
    if (Status == NULL) {
        return USB_SPEED_FS;
    }

    if ((Status->Status & USB_HUB_PORT_STATUS_LOW_SPEED) != 0) {
        return USB_SPEED_LS;
    }
    if ((Status->Status & USB_HUB_PORT_STATUS_HIGH_SPEED) != 0) {
        return USB_SPEED_HS;
    }

    return (Hub != NULL) ? Hub->SpeedId : USB_SPEED_FS;
}

/************************************************************************/

/**
 * @brief Submit a hub interrupt IN transfer.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @return TRUE on success.
 */
static BOOL XHCI_SubmitHubStatusTransfer(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub) {
    if (Device == NULL || Hub == NULL || Hub->HubInterruptEndpoint == NULL) {
        return FALSE;
    }
    if (Hub->HubInterruptLength == 0 || Hub->HubStatusPhysical == 0 || Hub->HubStatusLinear == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(Hub->HubStatusPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(Hub->HubStatusPhysical));
    Trb.Dword2 = Hub->HubInterruptLength;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;

    MemorySet((LPVOID)Hub->HubStatusLinear, 0, Hub->HubInterruptLength);
    Hub->HubStatusPending = FALSE;
    XHCI_ClearTransferCompletions(Device, Hub->SlotId, Hub->HubInterruptEndpoint->Dci);

    if (!XHCI_RingEnqueue(
            Hub->HubInterruptEndpoint->TransferRingLinear, Hub->HubInterruptEndpoint->TransferRingPhysical,
            &Hub->HubInterruptEndpoint->TransferRingEnqueueIndex, &Hub->HubInterruptEndpoint->TransferRingCycleState,
            XHCI_TRANSFER_RING_TRBS, &Trb, &Hub->HubStatusTrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, Hub->SlotId, Hub->HubInterruptEndpoint->Dci);
    Hub->HubStatusPending = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Check for completion of a transfer without blocking.
 * @param Device xHCI device.
 * @param TrbPhysical TRB physical address.
 * @param CompletionOut Receives completion code.
 * @return TRUE when completion was found.
 */
BOOL XHCI_CheckTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut) {
    return XHCI_CheckTransferCompletionRouted(Device, TrbPhysical, 0, 0, CompletionOut, NULL, NULL, NULL);
}

/************************************************************************/

/**
 * @brief Pop a completion using route fallback when TRB pointer does not match.
 * @param Device xHCI device.
 * @param SlotId Slot identifier.
 * @param EndpointId Endpoint identifier (DCI).
 * @param CompletionOut Receives completion code.
 * @param ObservedTrbPhysicalOut Receives observed transfer-event pointer.
 * @return TRUE on success.
 */
static BOOL XHCI_PopTransferCompletionByRoute(
    LPXHCI_DEVICE Device, U8 SlotId, U8 EndpointId, U32* CompletionOut, U32* TransferLengthOut,
    U64* ObservedTrbPhysicalOut) {
    U32 Index;

    if (Device == NULL || SlotId == 0 || EndpointId == 0) {
        return FALSE;
    }

    for (Index = 0; Index < Device->CompletionCount; Index++) {
        XHCI_COMPLETION* Entry = &Device->CompletionQueue[Index];
        if (Entry->Type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
            continue;
        }
        if (Entry->SlotId != SlotId || Entry->EndpointId != EndpointId) {
            continue;
        }

        if (CompletionOut != NULL) {
            *CompletionOut = Entry->Completion;
        }
        if (TransferLengthOut != NULL) {
            *TransferLengthOut = Entry->TransferLength;
        }
        if (ObservedTrbPhysicalOut != NULL) {
            *ObservedTrbPhysicalOut = Entry->TrbPhysical;
        }

        for (U32 Shift = Index + 1; Shift < Device->CompletionCount; Shift++) {
            Device->CompletionQueue[Shift - 1] = Device->CompletionQueue[Shift];
        }
        Device->CompletionCount--;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check transfer completion with optional route fallback.
 * @param Device xHCI device.
 * @param TrbPhysical Expected TRB physical address.
 * @param SlotId Slot identifier used for fallback matching.
 * @param EndpointId Endpoint identifier (DCI) used for fallback matching.
 * @param CompletionOut Receives completion code.
 * @param UsedRouteFallbackOut Receives TRUE when fallback route matching was used.
 * @param ObservedTrbPhysicalOut Receives observed transfer-event pointer when available.
 * @return TRUE when completion was found.
 */
BOOL XHCI_CheckTransferCompletionRouted(
    LPXHCI_DEVICE Device, U64 TrbPhysical, U8 SlotId, U8 EndpointId, U32* CompletionOut, U32* TransferLengthOut,
    BOOL* UsedRouteFallbackOut, U64* ObservedTrbPhysicalOut) {
    if (Device == NULL) {
        return FALSE;
    }

    BOOL Found = FALSE;
    BOOL UsedRouteFallback = FALSE;
    U64 ObservedTrbPhysical = TrbPhysical;

    LockMutex(&(Device->Mutex), INFINITY);
    XHCI_PollCompletions(Device);
    Found =
        XHCI_PopCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut, TransferLengthOut);
    if (!Found && SlotId != 0 && EndpointId != 0) {
        Found = XHCI_PopTransferCompletionByRoute(
            Device, SlotId, EndpointId, CompletionOut, TransferLengthOut, &ObservedTrbPhysical);
        UsedRouteFallback = Found ? TRUE : FALSE;
    }
    UnlockMutex(&(Device->Mutex));

    if (Found) {
        if (UsedRouteFallbackOut != NULL) {
            *UsedRouteFallbackOut = UsedRouteFallback;
        }
        if (ObservedTrbPhysicalOut != NULL) {
            *ObservedTrbPhysicalOut = ObservedTrbPhysical;
        }
    }
    return Found;
}

/**
 * @brief Allocate and initialize a child USB device.
 * @param Device xHCI device.
 * @param Parent Parent hub.
 * @param Port Port number (1-based).
 * @return Allocated device or NULL.
 */
static LPXHCI_USB_DEVICE XHCI_AllocateChildDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Parent, U8 Port) {
    if (Device == NULL || Parent == NULL) {
        return NULL;
    }

    LPXHCI_USB_DEVICE Child = (LPXHCI_USB_DEVICE)CreateKernelObject(sizeof(XHCI_USB_DEVICE), KOID_USBDEVICE);
    if (Child == NULL) {
        return NULL;
    }

    XHCI_InitUsbDeviceObject(Device, Child);
    Child->Parent = (LPLISTNODE)Parent;
    Child->ParentPort = Port;
    Child->RootPortNumber = Parent->RootPortNumber;
    Child->Depth = (U8)(Parent->Depth + 1U);
    Child->RouteString = Parent->RouteString | ((U32)Port << (Parent->Depth * 4U));
    Child->PortNumber = Port;
    Child->IsRootPort = FALSE;
    XHCI_AddDeviceToList(Device, Child);

    return Child;
}

/************************************************************************/

/**
 * @brief Probe a hub port and enumerate a child device.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @param Port Port number (1-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ProbeHubPort(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub, U8 Port) {
    USB_PORT_STATUS Status;
    MemorySet(&Status, 0, sizeof(Status));

    if (Hub == NULL || Hub->HubChildren == NULL || Port == 0 || Port > Hub->HubPortCount) {
        return FALSE;
    }

    if (Hub->HubChildren != NULL && Hub->HubChildren[Port - 1] != NULL) {
        return TRUE;
    }

    if (!XHCI_HubGetPortStatus(Device, Hub, Port, &Status)) {
        return FALSE;
    }

    if ((Status.Status & USB_HUB_PORT_STATUS_CONNECTION) == 0) {
        return FALSE;
    }

    if (!XHCI_ResetHubPort(Device, Hub, Port)) {
        return FALSE;
    }

    if (!XHCI_HubGetPortStatus(Device, Hub, Port, &Status)) {
        return FALSE;
    }

    U8 Speed = XHCI_GetHubPortSpeed(Hub, &Status);
    LPXHCI_USB_DEVICE Child = XHCI_AllocateChildDevice(Device, Hub, Port);
    if (Child == NULL) {
        return FALSE;
    }

    Child->SpeedId = Speed;

    if (!XHCI_EnumerateDevice(Device, Child)) {
        XHCI_DestroyUsbDevice(Device, Child, TRUE);
        return FALSE;
    }

    Hub->HubChildren[Port - 1] = Child;
    Hub->HubPortStatus[Port - 1] = Status.Status;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize hub-specific data and ports.
 * @param Device xHCI device.
 * @param Hub Hub device.
 * @return TRUE on success.
 */
BOOL XHCI_InitHub(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub) {
    if (Device == NULL || Hub == NULL) {
        return FALSE;
    }

    if (Hub->HubPortCount != 0 && Hub->HubChildren != NULL) {
        return TRUE;
    }

    U8 PortCount = 0;
    if (!XHCI_ReadHubDescriptor(Device, Hub, &PortCount)) {
        ERROR(TEXT("[XHCI_InitHub] Hub descriptor read failed"));
        return FALSE;
    }

    Hub->HubPortCount = PortCount;
    Hub->HubChildren = (LPXHCI_USB_DEVICE*)KernelHeapAlloc(sizeof(LPXHCI_USB_DEVICE) * PortCount);
    Hub->HubPortStatus = (U16*)KernelHeapAlloc(sizeof(U16) * PortCount);

    if (Hub->HubChildren == NULL || Hub->HubPortStatus == NULL) {
        ERROR(TEXT("[XHCI_InitHub] Hub port allocation failed"));
        if (Hub->HubChildren != NULL) {
            KernelHeapFree(Hub->HubChildren);
            Hub->HubChildren = NULL;
        }
        if (Hub->HubPortStatus != NULL) {
            KernelHeapFree(Hub->HubPortStatus);
            Hub->HubPortStatus = NULL;
        }
        return FALSE;
    }

    MemorySet(Hub->HubChildren, 0, sizeof(LPXHCI_USB_DEVICE) * PortCount);
    MemorySet(Hub->HubPortStatus, 0, sizeof(U16) * PortCount);

    Hub->HubInterruptEndpoint = XHCI_FindHubInterruptEndpoint(Hub);
    if (Hub->HubInterruptEndpoint == NULL) {
        ERROR(TEXT("[XHCI_InitHub] Hub interrupt endpoint not found"));
        KernelHeapFree(Hub->HubChildren);
        Hub->HubChildren = NULL;
        KernelHeapFree(Hub->HubPortStatus);
        Hub->HubPortStatus = NULL;
        return FALSE;
    }

    if (!XHCI_AddInterruptEndpoint(Device, Hub, Hub->HubInterruptEndpoint)) {
        ERROR(TEXT("[XHCI_InitHub] Hub interrupt endpoint init failed"));
        KernelHeapFree(Hub->HubChildren);
        Hub->HubChildren = NULL;
        KernelHeapFree(Hub->HubPortStatus);
        Hub->HubPortStatus = NULL;
        return FALSE;
    }

    Hub->HubInterruptLength = (U16)((PortCount + 1U + 7U) / 8U);
    if (!XHCI_AllocPage(TEXT("XHCI_HubStatus"), &Hub->HubStatusPhysical, &Hub->HubStatusLinear)) {
        ERROR(TEXT("[XHCI_InitHub] Hub status buffer alloc failed"));
        KernelHeapFree(Hub->HubChildren);
        Hub->HubChildren = NULL;
        KernelHeapFree(Hub->HubPortStatus);
        Hub->HubPortStatus = NULL;
        return FALSE;
    }

    MemorySet((LPVOID)Hub->HubStatusLinear, 0, Hub->HubInterruptLength);
    Hub->HubStatusPending = FALSE;

    if (!XHCI_UpdateHubSlotContext(Device, Hub)) {
        ERROR(TEXT("[XHCI_InitHub] Hub slot context update failed"));
        FreeRegion(Hub->HubStatusLinear, PAGE_SIZE);
        FreePhysicalPage(Hub->HubStatusPhysical);
        Hub->HubStatusLinear = 0;
        Hub->HubStatusPhysical = 0;
        KernelHeapFree(Hub->HubChildren);
        Hub->HubChildren = NULL;
        KernelHeapFree(Hub->HubPortStatus);
        Hub->HubPortStatus = NULL;
        return FALSE;
    }

    for (U8 Port = 1; Port <= PortCount; Port++) {
        (void)XHCI_HubSetPortFeature(Device, Hub, Port, USB_HUB_FEATURE_PORT_POWER);
    }

    for (U8 Port = 1; Port <= PortCount; Port++) {
        USB_PORT_STATUS Status;
        MemorySet(&Status, 0, sizeof(Status));
        if (XHCI_HubGetPortStatus(Device, Hub, Port, &Status)) {
            if ((Status.Status & USB_HUB_PORT_STATUS_CONNECTION) != 0) {
                if (XHCI_ProbeHubPort(Device, Hub, Port)) {
                    LPXHCI_USB_DEVICE Child = Hub->HubChildren[Port - 1];
                    if (Child != NULL && Child->IsHub) {
                        if (!XHCI_InitHub(Device, Child)) {
                            WARNING(TEXT("[XHCI_InitHub] Hub init failed on port %u"), Port);
                        }
                    }
                }
            }
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Process hub port change bitmap.
 * @param Device xHCI device.
 * @param Hub Hub device.
 */
static void XHCI_HandleHubStatus(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE Hub) {
    if (Device == NULL || Hub == NULL || Hub->HubStatusLinear == 0 || Hub->HubChildren == NULL) {
        return;
    }

    const U8* Bitmap = (const U8*)Hub->HubStatusLinear;
    for (U8 Port = 1; Port <= Hub->HubPortCount; Port++) {
        U8 ByteIndex = (U8)(Port / 8U);
        U8 BitMask = (U8)(1U << (Port % 8U));

        if ((Bitmap[ByteIndex] & BitMask) == 0) {
            continue;
        }

        USB_PORT_STATUS Status;
        MemorySet(&Status, 0, sizeof(Status));
        if (!XHCI_HubGetPortStatus(Device, Hub, Port, &Status)) {
            continue;
        }

        if ((Status.Change & USB_HUB_PORT_CHANGE_CONNECTION) != 0) {
            if ((Status.Status & USB_HUB_PORT_STATUS_CONNECTION) != 0) {
                if (Hub->HubChildren[Port - 1] == NULL) {
                    if (XHCI_ProbeHubPort(Device, Hub, Port)) {
                        LPXHCI_USB_DEVICE Child = Hub->HubChildren[Port - 1];
                        if (Child != NULL && Child->IsHub) {
                            if (!XHCI_InitHub(Device, Child)) {
                                WARNING(TEXT("[XHCI_HandleHubStatus] Hub init failed on port %u"), Port);
                            }
                        }
                    }
                }
            } else {
                if (Hub->HubChildren[Port - 1] != NULL) {
                    XHCI_DestroyUsbDevice(Device, Hub->HubChildren[Port - 1], TRUE);
                    Hub->HubChildren[Port - 1] = NULL;
                }
            }

            (void)XHCI_HubClearPortFeature(Device, Hub, Port, USB_HUB_FEATURE_C_PORT_CONNECTION);
        }

        if ((Status.Change & USB_HUB_PORT_CHANGE_ENABLE) != 0) {
            (void)XHCI_HubClearPortFeature(Device, Hub, Port, USB_HUB_FEATURE_C_PORT_ENABLE);
        }
        if ((Status.Change & USB_HUB_PORT_CHANGE_RESET) != 0) {
            (void)XHCI_HubClearPortFeature(Device, Hub, Port, USB_HUB_FEATURE_C_PORT_RESET);
        }

        Hub->HubPortStatus[Port - 1] = Status.Status;
    }
}

/************************************************************************/

/**
 * @brief Poll hub interrupt endpoints and process changes.
 * @param Context xHCI device.
 */
static void XHCI_PollHubs(LPVOID Context) {
    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)Context;
    if (Device == NULL) {
        return;
    }

    LPLIST UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_DEVICE Hub = (LPXHCI_USB_DEVICE)Node;
        if (Hub->Controller != Device) {
            continue;
        }

        if (!Hub->Present || !Hub->IsHub || Hub->HubInterruptEndpoint == NULL || Hub->HubStatusLinear == 0) {
            continue;
        }

        if (!Hub->HubStatusPending) {
            (void)XHCI_SubmitHubStatusTransfer(Device, Hub);
            continue;
        }

        U32 Completion = 0;
        if (!XHCI_CheckTransferCompletion(Device, Hub->HubStatusTrbPhysical, &Completion)) {
            continue;
        }

        Hub->HubStatusPending = FALSE;
        if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
            XHCI_HandleHubStatus(Device, Hub);
        } else {
            WARNING(TEXT("[XHCI_PollHubs] Hub interrupt completion %x"), Completion);
        }
    }
}

/************************************************************************/

/**
 * @brief Register the hub polling callback.
 * @param Device xHCI device.
 */
void XHCI_RegisterHubPoll(LPXHCI_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    if (DeferredWorkTokenIsValid(Device->HubPollToken) != FALSE) {
        return;
    }

    Device->HubPollToken = DeferredWorkRegisterPollOnly(XHCI_PollHubs, (LPVOID)Device, TEXT("XHCIHub"));
    if (DeferredWorkTokenIsValid(Device->HubPollToken) == FALSE) {
        WARNING(TEXT("[XHCI_RegisterHubPoll] Failed to register hub poll"));
    }
}
