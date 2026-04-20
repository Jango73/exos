
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

#define XHCI_COMMAND_TIMEOUT_LOG_IMMEDIATE_BUDGET 2
#define XHCI_COMMAND_TIMEOUT_LOG_INTERVAL_MS 1000
/************************************************************************/

/**
 * @brief Emit one rate-limited snapshot when command completion stalls.
 * @param Device xHCI controller.
 * @param TrbPhysical Command TRB physical address.
 * @param Stage Stage label.
 */
static void XHCI_LogCommandTimeoutState(LPXHCI_DEVICE Device, U64 TrbPhysical, LPCSTR Stage) {
    static RATE_LIMITER DATA_SECTION CommandTimeoutLimiter = {0};
    static BOOL DATA_SECTION CommandTimeoutLimiterInitAttempted = FALSE;
    U32 Suppressed = 0;
    LINEAR InterrupterBase;
    U32 UsbStatus;
    U32 UsbCommand;
    U32 CrcrLow;
    U32 CrcrHigh;
    U32 Iman;
    U32 ErdpLow;
    U32 ErdpHigh;
    U32 EventDword0 = 0;
    U32 EventDword1 = 0;
    U32 EventDword2 = 0;
    U32 EventDword3 = 0;
    U32 EventCycle = 0;
    U32 ExpectedCycle = 0;
    U32 CommandDword0 = 0;
    U32 CommandDword1 = 0;
    U32 CommandDword2 = 0;
    U32 CommandDword3 = 0;
    U32 CommandType = 0;
    U32 CommandSlot = 0;
    U32 CommandIndex = 0xFFFFFFFF;
    U32 CommandOffset = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;
    U32 SlotPort = 0;
    U32 SlotAddress = 0;
    U32 SlotPresent = 0;
    U64 CommandRingBase;
    U64 CommandOffsetU64;

    if (Device == NULL) {
        return;
    }

    if (CommandTimeoutLimiter.Initialized == FALSE && CommandTimeoutLimiterInitAttempted == FALSE) {
        CommandTimeoutLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&CommandTimeoutLimiter,
                            XHCI_COMMAND_TIMEOUT_LOG_IMMEDIATE_BUDGET,
                            XHCI_COMMAND_TIMEOUT_LOG_INTERVAL_MS) == FALSE) {
            return;
        }
    }

    if (!RateLimiterShouldTrigger(&CommandTimeoutLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    XHCI_LogHseTransitionIfNeeded(Device, TEXT("CommandTimeout"));
    InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    UsbCommand = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    UsbStatus = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
    CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
    Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
    ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
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

    CommandRingBase = U64_FromUINT(Device->CommandRingPhysical);
    if (U64_Cmp(TrbPhysical, CommandRingBase) >= 0) {
        CommandOffsetU64 = U64_Sub(TrbPhysical, CommandRingBase);
        if (U64_High32(CommandOffsetU64) == 0) {
            CommandOffset = U64_Low32(CommandOffsetU64);
            if (CommandOffset < PAGE_SIZE && (CommandOffset % sizeof(XHCI_TRB)) == 0 && Device->CommandRingLinear != 0) {
                LPXHCI_TRB CommandRing = (LPXHCI_TRB)Device->CommandRingLinear;
                CommandIndex = CommandOffset / sizeof(XHCI_TRB);
                CommandDword0 = CommandRing[CommandIndex].Dword0;
                CommandDword1 = CommandRing[CommandIndex].Dword1;
                CommandDword2 = CommandRing[CommandIndex].Dword2;
                CommandDword3 = CommandRing[CommandIndex].Dword3;
                CommandType = (CommandDword3 >> XHCI_TRB_TYPE_SHIFT) & 0x3F;
                CommandSlot = (CommandDword3 >> 24) & 0xFF;
            }
        }
    }

    if (CommandSlot != 0 && Device->UsbDevices != NULL) {
        for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
            LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
            if (UsbDevice == NULL || UsbDevice->SlotId != CommandSlot) {
                continue;
            }

            SlotPort = (U32)UsbDevice->PortNumber;
            SlotAddress = (U32)UsbDevice->Address;
            SlotPresent = UsbDevice->Present ? 1 : 0;
            break;
        }
    }

    WARNING(TEXT("stage=%s TRB=%p CmdType=%x CmdSlot=%x SlotPort=%u SlotAddr=%x SlotPresent=%u CmdIdx=%x CmdEnq=%u CmdCycle=%u Cmd=%x:%x:%x:%x USBCMD=%x USBSTS=%x PCICMD=%x PCISTS=%x CRCR=%x:%x IMAN=%x ERDP=%x:%x CQ=%u Event=%x:%x:%x:%x Cy=%u/%u suppressed=%u"),
            (Stage != NULL) ? Stage : TEXT("?"),
            (LPVOID)(UINT)U64_Low32(TrbPhysical),
            CommandType,
            CommandSlot,
            SlotPort,
            SlotAddress,
            SlotPresent,
            CommandIndex,
            Device->CommandRingEnqueueIndex,
            Device->CommandRingCycleState,
            CommandDword3,
            CommandDword2,
            CommandDword1,
            CommandDword0,
            UsbCommand,
            UsbStatus,
            (U32)PciCommand,
            (U32)PciStatus,
            CrcrHigh,
            CrcrLow,
            Iman,
            ErdpHigh,
            ErdpLow,
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
 * @brief Initialize USB device object fields for xHCI.
 *
 * LISTNODE_FIELDS are expected to be initialized by CreateKernelObject.
 * @param Device xHCI controller.
 * @param UsbDevice USB device state.
 */
void XHCI_InitUsbDeviceObject(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    MemorySet(&UsbDevice->Mutex, 0, sizeof(XHCI_USB_DEVICE) - sizeof(LISTNODE));
    UsbDevice->Controller = Device;
    UsbDevice->LastEnumError = XHCI_ENUM_ERROR_NONE;
    UsbDevice->LastEnumCompletion = 0;
    UsbDevice->LastRootPortProbeSignature = 0;
    (void)FailureGateInit(&UsbDevice->RootPortFailureGate, XHCI_ROOT_PORT_PROBE_FAILURE_THRESHOLD);

    InitMutex(&UsbDevice->Mutex);
    UsbDevice->Contexts.First = NULL;
    UsbDevice->Contexts.Last = NULL;
    UsbDevice->Contexts.Current = NULL;
    UsbDevice->Contexts.NumItems = 0;
    UsbDevice->Contexts.MemAllocFunc = KernelHeapAlloc;
    UsbDevice->Contexts.MemFreeFunc = KernelHeapFree;
    UsbDevice->Contexts.Destructor = NULL;
}

/************************************************************************/

/**
 * @brief Free USB configuration tree.
 * @param UsbDevice USB device state.
 */
void XHCI_FreeUsbTree(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList != NULL) {
        for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
            LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Endpoint->Parent;
            if (Interface == NULL || Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }
            if (Endpoint->References <= 1U) {
                if (Endpoint->TransferRingLinear) {
                    FreeRegion(Endpoint->TransferRingLinear, PAGE_SIZE);
                    Endpoint->TransferRingLinear = 0;
                }
                if (Endpoint->TransferRingPhysical) {
                    FreePhysicalPage(Endpoint->TransferRingPhysical);
                    Endpoint->TransferRingPhysical = 0;
                }
            }
            ReleaseKernelObject(Endpoint);
        }
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList != NULL) {
        for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
            if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }
            ReleaseKernelObject(Interface);
        }
    }

    if (UsbDevice->Configs != NULL) {
        KernelHeapFree(UsbDevice->Configs);
        UsbDevice->Configs = NULL;
    }

    UsbDevice->ConfigCount = 0;
    UsbDevice->SelectedConfigValue = 0;
}

/************************************************************************/

/**
 * @brief Check if any USB interface or endpoint is still referenced.
 * @param UsbDevice USB device state.
 * @return TRUE when references are still held.
 */
BOOL XHCI_UsbTreeHasReferences(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
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
        if (Interface->References > 1U) {
            return TRUE;
        }
    }

    LPLIST EndpointList = GetUsbEndpointList();
    if (EndpointList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = EndpointList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)Node;
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Endpoint->Parent;
        if (Interface == NULL || Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Endpoint->References > 1U) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Free per-device allocations excluding child nodes.
 * @param UsbDevice USB device state.
 */
static void XHCI_FreeUsbDeviceResources(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    if (UsbDevice->References > 1U) {
        UsbDevice->DestroyPending = TRUE;
        return;
    }

    if (XHCI_UsbTreeHasReferences(UsbDevice)) {
        UsbDevice->DestroyPending = TRUE;
        return;
    }

    XHCI_FreeUsbTree(UsbDevice);

    if (UsbDevice->TransferRingLinear) {
        FreeRegion(UsbDevice->TransferRingLinear, PAGE_SIZE);
        UsbDevice->TransferRingLinear = 0;
    }
    if (UsbDevice->TransferRingPhysical) {
        FreePhysicalPage(UsbDevice->TransferRingPhysical);
        UsbDevice->TransferRingPhysical = 0;
    }
    if (UsbDevice->InputContextLinear) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        UsbDevice->InputContextLinear = 0;
    }
    if (UsbDevice->InputContextPhysical) {
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextPhysical = 0;
    }
    if (UsbDevice->DeviceContextLinear) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        UsbDevice->DeviceContextLinear = 0;
    }
    if (UsbDevice->DeviceContextPhysical) {
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        UsbDevice->DeviceContextPhysical = 0;
    }
    if (UsbDevice->HubStatusLinear) {
        FreeRegion(UsbDevice->HubStatusLinear, PAGE_SIZE);
        UsbDevice->HubStatusLinear = 0;
    }
    if (UsbDevice->HubStatusPhysical) {
        FreePhysicalPage(UsbDevice->HubStatusPhysical);
        UsbDevice->HubStatusPhysical = 0;
    }
    if (UsbDevice->HubChildren != NULL) {
        KernelHeapFree(UsbDevice->HubChildren);
        UsbDevice->HubChildren = NULL;
    }
    if (UsbDevice->HubPortStatus != NULL) {
        KernelHeapFree(UsbDevice->HubPortStatus);
    UsbDevice->HubPortStatus = NULL;
    }

    UsbDevice->Present = FALSE;
    UsbDevice->DestroyPending = FALSE;
    UsbDevice->SlotId = 0;
    UsbDevice->Address = 0;
    UsbDevice->IsHub = FALSE;
    UsbDevice->HubPortCount = 0;
    UsbDevice->HubInterruptEndpoint = NULL;
    UsbDevice->HubInterruptLength = 0;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);
    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->Parent = NULL;
    UsbDevice->ParentPort = 0;
    UsbDevice->Depth = 0;
    UsbDevice->RouteString = 0;
    UsbDevice->Controller = NULL;
}

/************************************************************************/

/**
 * @brief Increment references on a USB device object.
 * @param UsbDevice USB device state.
 */
void XHCI_ReferenceUsbDevice(LPXHCI_USB_DEVICE UsbDevice) {
    SAFE_USE_VALID_ID(UsbDevice, KOID_USBDEVICE) {
        if (UsbDevice->References < MAX_UINT) {
            UsbDevice->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB device object.
 * @param UsbDevice USB device state.
 */
void XHCI_ReleaseUsbDevice(LPXHCI_USB_DEVICE UsbDevice) {
    SAFE_USE_VALID_ID(UsbDevice, KOID_USBDEVICE) {
        if (UsbDevice->References != 0) {
            ReleaseKernelObject(UsbDevice);
        }

        if (!UsbDevice->DestroyPending || XHCI_UsbTreeHasReferences(UsbDevice)) {
            return;
        }

        if ((UsbDevice->IsRootPort && UsbDevice->References == 1) ||
            (!UsbDevice->IsRootPort && UsbDevice->References == 0)) {
            XHCI_FreeUsbDeviceResources(UsbDevice);
        }
    }
}

/************************************************************************/

/**
 * @brief Increment references on a USB interface.
 * @param Interface USB interface.
 */
void XHCI_ReferenceUsbInterface(LPXHCI_USB_INTERFACE Interface) {
    SAFE_USE_VALID_ID(Interface, KOID_USBINTERFACE) {
        if (Interface->References < MAX_UINT) {
            Interface->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB interface.
 * @param Interface USB interface.
 */
void XHCI_ReleaseUsbInterface(LPXHCI_USB_INTERFACE Interface) {
    SAFE_USE_VALID_ID(Interface, KOID_USBINTERFACE) {
        if (Interface->References != 0) {
            ReleaseKernelObject(Interface);
        }
    }
}

/************************************************************************/

/**
 * @brief Increment references on a USB endpoint.
 * @param Endpoint USB endpoint.
 */
void XHCI_ReferenceUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint) {
    SAFE_USE_VALID_ID(Endpoint, KOID_USBENDPOINT) {
        if (Endpoint->References < MAX_UINT) {
            Endpoint->References++;
        }
    }
}

/************************************************************************/

/**
 * @brief Decrement references on a USB endpoint.
 * @param Endpoint USB endpoint.
 */
void XHCI_ReleaseUsbEndpoint(LPXHCI_USB_ENDPOINT Endpoint) {
    SAFE_USE_VALID_ID(Endpoint, KOID_USBENDPOINT) {
        if (Endpoint->References != 0) {
            ReleaseKernelObject(Endpoint);
        }

        if (Endpoint->References == 0) {
            if (Endpoint->TransferRingLinear) {
                FreeRegion(Endpoint->TransferRingLinear, PAGE_SIZE);
                Endpoint->TransferRingLinear = 0;
            }
            if (Endpoint->TransferRingPhysical) {
                FreePhysicalPage(Endpoint->TransferRingPhysical);
                Endpoint->TransferRingPhysical = 0;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Reset a transfer ring to an empty state.
 * @param RingPhysical Ring physical base.
 * @param RingLinear Ring linear base.
 * @param CycleStateOut Cycle state pointer.
 * @param EnqueueIndexOut Enqueue index pointer.
 */
static void XHCI_ResetTransferRingState(PHYSICAL RingPhysical, LINEAR RingLinear,
                                        U32* CycleStateOut, U32* EnqueueIndexOut) {
    if (RingPhysical == 0 || RingLinear == 0 || CycleStateOut == NULL || EnqueueIndexOut == NULL) {
        return;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)RingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(RingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    *CycleStateOut = 1;
    *EnqueueIndexOut = 0;
}

/************************************************************************/

/**
 * @brief Read the current endpoint state from the output device context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Dci Endpoint DCI.
 * @return Endpoint state value or disabled when unavailable.
 */
static U32 XHCI_GetEndpointState(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    LPXHCI_CONTEXT_32 EndpointContext;

    if (Device == NULL || UsbDevice == NULL || UsbDevice->DeviceContextLinear == 0 || Dci == 0) {
        return XHCI_ENDPOINT_STATE_DISABLED;
    }

    EndpointContext = XHCI_GetContextPointer(UsbDevice->DeviceContextLinear, Device->ContextSize, (U32)Dci);
    if (EndpointContext == NULL) {
        return XHCI_ENDPOINT_STATE_DISABLED;
    }

    return (EndpointContext->Dword0 & 0x7U);
}

/************************************************************************/

/**
 * @brief Wait for a command completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Command TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
BOOL XHCI_WaitForCommandCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut) {
    U32 Timeout = XHCI_DEFAULT_EVENT_TIMEOUT_MS;
    U32 RequestedTimeout = Timeout;
    U32 ElapsedMilliseconds = 0;
    U32 NextWarningAt = 200;

    if (Device != NULL && Device->CommandTimeoutMS != 0) {
        Timeout = Device->CommandTimeoutMS;
    }
    RequestedTimeout = Timeout;

    LockMutex(&(Device->Mutex), INFINITY);
    if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT, TrbPhysical, SlotIdOut, CompletionOut, NULL)) {
        UnlockMutex(&(Device->Mutex));
        return TRUE;
    }

    while (ElapsedMilliseconds < Timeout) {
        if (XHCI_PollForCompletion(Device, XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT, TrbPhysical, SlotIdOut, CompletionOut, NULL)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT, TrbPhysical, SlotIdOut, CompletionOut, NULL)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        if (ElapsedMilliseconds >= NextWarningAt) {
            WARNING(TEXT("exceeded %u ms (TRB=%p)"),
                    ElapsedMilliseconds,
                    (LPVOID)(UINT)U64_Low32(TrbPhysical));
            XHCI_LogCommandTimeoutState(Device, TrbPhysical, TEXT("Exceeded"));
            if (NextWarningAt < MAX_U32 - 200) {
                NextWarningAt += 200;
            }
        }

        Sleep(1);
        ElapsedMilliseconds++;
    }

    UnlockMutex(&(Device->Mutex));
    WARNING(TEXT("Timeout %u ms (TRB=%p)"), RequestedTimeout, (LPVOID)(UINT)U64_Low32(TrbPhysical));
    XHCI_LogCommandTimeoutState(Device, TrbPhysical, TEXT("Timeout"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Wait for a transfer completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Status TRB physical address.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
BOOL XHCI_WaitForTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut) {
    U32 Timeout = XHCI_DEFAULT_TRANSFER_TIMEOUT_MS;
    U32 ElapsedMilliseconds = 0;

    if (Device != NULL && Device->TransferTimeoutMS != 0) {
        Timeout = Device->TransferTimeoutMS;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut, NULL)) {
        UnlockMutex(&(Device->Mutex));
        return TRUE;
    }

    while (ElapsedMilliseconds < Timeout) {
        if (XHCI_PollForCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut, NULL)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        if (XHCI_PopCompletion(Device, XHCI_TRB_TYPE_TRANSFER_EVENT, TrbPhysical, NULL, CompletionOut, NULL)) {
            UnlockMutex(&(Device->Mutex));
            return TRUE;
        }

        Sleep(1);
        ElapsedMilliseconds++;
    }

    UnlockMutex(&(Device->Mutex));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Issue a STOP_ENDPOINT command for an endpoint.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Dci Endpoint DCI.
 * @return TRUE on success.
 */
static BOOL XHCI_StopEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci, U32* CompletionOut) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0 || Dci == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_STOP_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)Dci << 16) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        if (CompletionOut != NULL) {
            *CompletionOut = 0;
        }
        return FALSE;
    }

    if (CompletionOut != NULL) {
        *CompletionOut = Completion;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Issue a RESET_ENDPOINT command for an endpoint.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Dci Endpoint DCI.
 * @return TRUE on success.
 */
static BOOL XHCI_ResetEndpoint(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 Dci) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0 || Dci == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_RESET_ENDPOINT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)Dci << 16) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        WARNING(TEXT("Slot=%x DCI=%x completion %x"),
                (U32)UsbDevice->SlotId,
                (U32)Dci,
                Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Program transfer-ring dequeue pointer for one endpoint.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @return TRUE on success.
 */
static BOOL XHCI_SetTransferRingDequeuePointer(LPXHCI_DEVICE Device,
                                               LPXHCI_USB_DEVICE UsbDevice,
                                               LPXHCI_USB_ENDPOINT Endpoint,
                                               U64* ProgrammedDequeueOut,
                                               U32* CompletionOut) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || Endpoint->Dci == 0 || Endpoint->TransferRingPhysical == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;
    U64 DequeuePointer = U64_FromUINT(Endpoint->TransferRingPhysical);

    // DCS must match the consumer cycle state at the new dequeue pointer.
    if (Endpoint->TransferRingCycleState != 0) {
        DequeuePointer = U64_Make(U64_High32(DequeuePointer), U64_Low32(DequeuePointer) | 1);
    }

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(DequeuePointer);
    Trb.Dword1 = U64_High32(DequeuePointer);
    Trb.Dword2 = 0;
    Trb.Dword3 = (XHCI_TRB_TYPE_SET_TR_DEQUEUE_POINTER << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)Endpoint->Dci << 16) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (ProgrammedDequeueOut != NULL) {
        *ProgrammedDequeueOut = DequeuePointer;
    }

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        if (CompletionOut != NULL) {
            *CompletionOut = 0;
        }
        return FALSE;
    }

    if (CompletionOut != NULL) {
        *CompletionOut = Completion;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Recover one transfer endpoint after stall/timeout.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Endpoint Endpoint descriptor.
 * @param EndpointHalted TRUE when endpoint is in halted state (stall).
 * @return TRUE on success.
 */
BOOL XHCI_ResetTransferEndpoint(LPXHCI_DEVICE Device,
                                LPXHCI_USB_DEVICE UsbDevice,
                                LPXHCI_USB_ENDPOINT Endpoint,
                                BOOL EndpointHalted) {
    U32 EndpointState;
    U32 FinalEndpointState;
    U32 StopCompletion = 0;
    U32 SetCompletion = 0;
    U64 ProgrammedDequeue = U64_0;
    BOOL RecoveryCommandOk = FALSE;

    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || Endpoint->Dci == 0) {
        return FALSE;
    }

    EndpointState = XHCI_GetEndpointState(Device, UsbDevice, Endpoint->Dci);

    DEBUG(TEXT("Begin Slot=%x DCI=%x Halted=%u CtxState=%x"),
          (U32)UsbDevice->SlotId,
          (U32)Endpoint->Dci,
          EndpointHalted ? 1 : 0,
          EndpointState);

    if (EndpointHalted) {
        if (EndpointState == XHCI_ENDPOINT_STATE_HALTED) {
            RecoveryCommandOk = XHCI_ResetEndpoint(Device, UsbDevice, Endpoint->Dci);
        } else if (EndpointState == XHCI_ENDPOINT_STATE_RUNNING) {
            RecoveryCommandOk = XHCI_StopEndpoint(Device, UsbDevice, Endpoint->Dci, &StopCompletion);
        } else if (EndpointState == XHCI_ENDPOINT_STATE_STOPPED || EndpointState == XHCI_ENDPOINT_STATE_ERROR) {
            RecoveryCommandOk = TRUE;
        }
    } else {
        if (EndpointState == XHCI_ENDPOINT_STATE_RUNNING) {
            RecoveryCommandOk = XHCI_StopEndpoint(Device, UsbDevice, Endpoint->Dci, &StopCompletion);
        } else if (EndpointState == XHCI_ENDPOINT_STATE_HALTED) {
            RecoveryCommandOk = XHCI_ResetEndpoint(Device, UsbDevice, Endpoint->Dci);
        } else if (EndpointState == XHCI_ENDPOINT_STATE_STOPPED || EndpointState == XHCI_ENDPOINT_STATE_ERROR) {
            RecoveryCommandOk = TRUE;
        }
    }

    if (!RecoveryCommandOk) {
        WARNING(TEXT("Pre-recovery command failed Slot=%x DCI=%x Halted=%u CtxState=%x StopCompletion=%x"),
                (U32)UsbDevice->SlotId,
                (U32)Endpoint->Dci,
                EndpointHalted ? 1 : 0,
                EndpointState,
                StopCompletion);
        return FALSE;
    }

    XHCI_ResetTransferRingState(Endpoint->TransferRingPhysical,
                                Endpoint->TransferRingLinear,
                                &Endpoint->TransferRingCycleState,
                                &Endpoint->TransferRingEnqueueIndex);

    if (!XHCI_SetTransferRingDequeuePointer(Device, UsbDevice, Endpoint, &ProgrammedDequeue, &SetCompletion)) {
        FinalEndpointState = XHCI_GetEndpointState(Device, UsbDevice, Endpoint->Dci);
        WARNING(TEXT("SET_TR_DEQUEUE_POINTER failed Slot=%x DCI=%x Halted=%u InitialState=%x FinalState=%x StopCompletion=%x SetCompletion=%x Dequeue=%x:%x"),
                (U32)UsbDevice->SlotId,
                (U32)Endpoint->Dci,
                EndpointHalted ? 1 : 0,
                EndpointState,
                FinalEndpointState,
                StopCompletion,
                SetCompletion,
                U64_High32(ProgrammedDequeue),
                U64_Low32(ProgrammedDequeue));
        return FALSE;
    }

    XHCI_ClearTransferCompletions(Device, UsbDevice->SlotId, Endpoint->Dci);
    FinalEndpointState = XHCI_GetEndpointState(Device, UsbDevice, Endpoint->Dci);
    DEBUG(TEXT("End Slot=%x DCI=%x Halted=%u InitialState=%x FinalState=%x StopCompletion=%x SetCompletion=%x Dequeue=%x:%x"),
          (U32)UsbDevice->SlotId,
          (U32)Endpoint->Dci,
          EndpointHalted ? 1 : 0,
          EndpointState,
          FinalEndpointState,
          StopCompletion,
          SetCompletion,
          U64_High32(ProgrammedDequeue),
          U64_Low32(ProgrammedDequeue));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Issue a DISABLE_SLOT command for a USB device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_DisableSlot(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL || UsbDevice->SlotId == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_DISABLE_SLOT << XHCI_TRB_TYPE_SHIFT) |
                 ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        WARNING(TEXT("Slot=%x completion %x"),
                (U32)UsbDevice->SlotId,
                Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Stop endpoints and reset transfer rings for a device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_TeardownDeviceTransfers(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return;
    }

    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);

    if (UsbDevice->SlotId == 0) {
        return;
    }

    if (UsbDevice->TransferRingPhysical != 0 && UsbDevice->TransferRingLinear != 0) {
        (void)XHCI_StopEndpoint(Device, UsbDevice, XHCI_EP0_DCI, NULL);
        (void)XHCI_ResetEndpoint(Device, UsbDevice, XHCI_EP0_DCI);
        XHCI_ResetTransferRingState(UsbDevice->TransferRingPhysical,
                                    UsbDevice->TransferRingLinear,
                                    &UsbDevice->TransferRingCycleState,
                                    &UsbDevice->TransferRingEnqueueIndex);
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    LPLIST EndpointList = GetUsbEndpointList();
    if (InterfaceList != NULL && EndpointList != NULL) {
        for (LPLISTNODE IfNode = InterfaceList->First; IfNode != NULL; IfNode = IfNode->Next) {
            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)IfNode;
            if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                continue;
            }

            for (LPLISTNODE EpNode = EndpointList->First; EpNode != NULL; EpNode = EpNode->Next) {
                LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)EpNode;
                if (Endpoint->Parent != (LPLISTNODE)Interface) {
                    continue;
                }
                if (Endpoint->Dci == 0) {
                    continue;
                }

                (void)XHCI_StopEndpoint(Device, UsbDevice, Endpoint->Dci, NULL);
                (void)XHCI_ResetEndpoint(Device, UsbDevice, Endpoint->Dci);
                XHCI_ResetTransferRingState(Endpoint->TransferRingPhysical,
                                            Endpoint->TransferRingLinear,
                                            &Endpoint->TransferRingCycleState,
                                            &Endpoint->TransferRingEnqueueIndex);
            }
        }
    }

    if (XHCI_DisableSlot(Device, UsbDevice)) {
        if (Device->DcbaaLinear != 0) {
            ((U64*)Device->DcbaaLinear)[UsbDevice->SlotId] = U64_FromUINT(0);
        }
    }
}

/************************************************************************/

/**
 * @brief Add a device to the controller list.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
void XHCI_AddDeviceToList(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return;
    }

    LPLIST UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        if (Node == (LPLISTNODE)UsbDevice) {
            return;
        }
    }

    UsbDevice->Controller = Device;
    (void)ListAddItemWithParent(UsbDeviceList, UsbDevice, UsbDevice->Parent);
}

/************************************************************************/

/**
 * @brief Destroy a USB device and its children.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param FreeSelf TRUE when the UsbDevice object should be released.
 */
void XHCI_DestroyUsbDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, BOOL FreeSelf) {
    if (UsbDevice == NULL) {
        return;
    }

    UsbDevice->Present = FALSE;
    UsbDevice->DestroyPending = TRUE;

    if (UsbDevice->IsHub && UsbDevice->HubChildren != NULL) {
        for (U32 PortIndex = 0; PortIndex < UsbDevice->HubPortCount; PortIndex++) {
            LPXHCI_USB_DEVICE Child = UsbDevice->HubChildren[PortIndex];
            if (Child != NULL) {
                UsbDevice->HubChildren[PortIndex] = NULL;
                XHCI_DestroyUsbDevice(Device, Child, TRUE);
            }
        }
    }

    XHCI_TeardownDeviceTransfers(Device, UsbDevice);
    XHCI_FreeUsbDeviceResources(UsbDevice);

    if (FreeSelf) {
        XHCI_ReleaseUsbDevice(UsbDevice);
    }
}

/************************************************************************/

/**
 * @brief Convert an xHCI speed ID to a human readable name.
 * @param SpeedId Raw PORTSC speed value.
 * @return Speed string.
 */
LPCSTR XHCI_SpeedToString(U32 SpeedId) {
    switch (SpeedId) {
        case 1:
            return TEXT("FS");
        case 2:
            return TEXT("LS");
        case 3:
            return TEXT("HS");
        case 4:
            return TEXT("SS");
        case 5:
            return TEXT("SS+");
        default:
            return TEXT("Unknown");
    }
}
