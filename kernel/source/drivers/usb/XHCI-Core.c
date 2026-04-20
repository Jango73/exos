
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
#include "system/Clock.h"

/************************************************************************/
// MMIO access

/**
 * @brief Read a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @return Register value.
 */
U32 XHCI_Read32(LINEAR Base, U32 Offset) {
    return *(volatile U32 *)((U8 *)Base + Offset);
}

/************************************************************************/

/**
 * @brief Write a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
void XHCI_Write32(LINEAR Base, U32 Offset, U32 Value) {
    *(volatile U32 *)((U8 *)Base + Offset) = Value;
}

/************************************************************************/

/**
 * @brief Write a 64-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
void XHCI_Write64(LINEAR Base, U32 Offset, U64 Value) {
    XHCI_Write32(Base, Offset, U64_Low32(Value));
    XHCI_Write32(Base, (U32)(Offset + 4), U64_High32(Value));
}

/************************************************************************/

/**
 * @brief Get pointer to an xHCI context within a context array.
 * @param Base Base of the context array.
 * @param ContextSize Context size in bytes.
 * @param Index Context index.
 * @return Pointer to context.
 */
LPXHCI_CONTEXT_32 XHCI_GetContextPointer(LINEAR Base, U32 ContextSize, U32 Index) {
    return (LPXHCI_CONTEXT_32)((U8 *)Base + (ContextSize * Index));
}

/************************************************************************/

/**
 * @brief Extract xHCI TRB type from Dword3.
 * @param Dword3 TRB Dword3 value.
 * @return TRB type.
 */
static U32 XHCI_GetTrbType(U32 Dword3) {
    return (Dword3 >> XHCI_TRB_TYPE_SHIFT) & 0x3F;
}

/************************************************************************/

/**
 * @brief Extract xHCI completion code from Dword2.
 * @param Dword2 TRB Dword2 value.
 * @return Completion code.
 */
static U32 XHCI_GetCompletionCode(U32 Dword2) {
    return (Dword2 >> 24) & 0xFF;
}

/************************************************************************/

/**
 * @brief Extract xHCI transfer-event transfer length from Dword2.
 * @param Dword2 TRB Dword2 value.
 * @return Transfer length field.
 */
static U32 XHCI_GetTransferLength(U32 Dword2) {
    return (Dword2 & 0x00FFFFFF);
}

/************************************************************************/

/**
 * @brief Compare two TRB pointers while ignoring reserved low bits.
 * @param Left Left pointer.
 * @param Right Right pointer.
 * @return TRUE when pointers reference the same TRB.
 */
static BOOL XHCI_IsSameTrbPointer(U64 Left, U64 Right) {
    if (U64_High32(Left) != U64_High32(Right)) {
        return FALSE;
    }

    return ((U64_Low32(Left) & ~0x0F) == (U64_Low32(Right) & ~0x0F)) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Publish ring/context writes before a doorbell MMIO write.
 */
static void XHCI_PublishRingState(void) {
    __sync_synchronize();
}

/************************************************************************/

/**
 * @brief Ring an xHCI doorbell.
 * @param Device xHCI device.
 * @param DoorbellIndex Doorbell index (slot ID).
 * @param Target Target endpoint.
 */
void XHCI_RingDoorbell(LPXHCI_DEVICE Device, U32 DoorbellIndex, U32 Target) {
    U32 Value = Target & XHCI_DOORBELL_TARGET_MASK;
    XHCI_PublishRingState();
    XHCI_Write32(Device->DoorbellBase, DoorbellIndex * sizeof(U32), Value);
}

/************************************************************************/

/**
 * @brief Get base address for interrupter register set 0.
 * @param Device xHCI device.
 * @return Interrupter base address.
 */
LINEAR XHCI_GetInterrupterBase(LPXHCI_DEVICE Device) {
    return Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
}

/************************************************************************/

/**
 * @brief Log key xHCI init register programming and immediate readback.
 * @param Device xHCI device.
 * @param Step Init step label.
 * @param ProgrammedDcbaap Programmed DCBAAP value.
 * @param ProgrammedCrcr Programmed CRCR value.
 * @param ProgrammedErstba Programmed ERSTBA value.
 * @param ProgrammedErdp Programmed ERDP value.
 */
void XHCI_LogHseTransitionIfNeeded(LPXHCI_DEVICE Device, LPCSTR Source) {
    U32 Usbsts = 0;
    U32 Previous = 0;
    U32 Usbcmd = 0;
    U32 Config = 0;
    U32 CrcrLow = 0;
    U32 CrcrHigh = 0;
    U32 DcbaapLow = 0;
    U32 DcbaapHigh = 0;
    U32 Iman = 0;
    U32 Imod = 0;
    U32 Erstsz = 0;
    U32 ErstbaLow = 0;
    U32 ErstbaHigh = 0;
    U32 ErdpLow = 0;
    U32 ErdpHigh = 0;
    U32 DcbaaEntry0Low = 0;
    U32 DcbaaEntry0High = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;
    LINEAR InterrupterBase;

    if (Device == NULL || Device->OpBase == 0) {
        return;
    }

    Previous = Device->LastObservedUsbStatus;
    Usbsts = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    Device->LastObservedUsbStatus = Usbsts;

    if ((Usbsts & 0x00000004) == 0) {
        return;
    }
    if ((Previous & 0x00000004) != 0) {
        return;
    }
    if (Device->HseTransitionLogged != FALSE) {
        return;
    }

    Device->HseTransitionLogged = TRUE;
    Usbcmd = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Config = XHCI_Read32(Device->OpBase, XHCI_OP_CONFIG);
    CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
    CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
    DcbaapLow = XHCI_Read32(Device->OpBase, XHCI_OP_DCBAAP);
    DcbaapHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_DCBAAP + 4));

    if (Device->RuntimeBase != 0) {
        InterrupterBase = XHCI_GetInterrupterBase(Device);
        Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
        Imod = XHCI_Read32(InterrupterBase, XHCI_IMOD);
        Erstsz = XHCI_Read32(InterrupterBase, XHCI_ERSTSZ);
        ErstbaLow = XHCI_Read32(InterrupterBase, XHCI_ERSTBA);
        ErstbaHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERSTBA + 4));
        ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
        ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
    }
    if (Device->DcbaaLinear != 0) {
        U64 DcbaaEntry0 = ((volatile U64*)Device->DcbaaLinear)[0];
        DcbaaEntry0Low = U64_Low32(DcbaaEntry0);
        DcbaaEntry0High = U64_High32(DcbaaEntry0);
    }

    PciCommand = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    PciStatus = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_STATUS);

    WARNING(TEXT("source=%s PrevUSBSTS=%x USBCMD=%x USBSTS=%x CONFIG=%x PCICMD=%x PCISTS=%x Scratch=%u DCBAA0=%x:%x CRCR=%x:%x DCBAAP=%x:%x ERSTBA=%x:%x ERDP=%x:%x IMAN=%x IMOD=%x ERSTSZ=%x"),
            (Source != NULL) ? Source : TEXT("?"),
            Previous,
            Usbcmd,
            Usbsts,
            Config,
            (U32)PciCommand,
            (U32)PciStatus,
            (U32)Device->MaxScratchpadBuffers,
            DcbaaEntry0High,
            DcbaaEntry0Low,
            CrcrHigh,
            CrcrLow,
            DcbaapHigh,
            DcbaapLow,
            ErstbaHigh,
            ErstbaLow,
            ErdpHigh,
            ErdpLow,
            Iman,
            Imod,
            Erstsz);
}
/**
 * @brief Record an xHCI completion event in the device queue.
 * @param Device xHCI device.
 * @param Event Event TRB.
 */
static void XHCI_PushCompletion(LPXHCI_DEVICE Device, const XHCI_TRB* Event) {
    if (Device == NULL || Event == NULL) {
        return;
    }

    U8 Type = (U8)XHCI_GetTrbType(Event->Dword3);
    if (Type != XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT &&
        Type != XHCI_TRB_TYPE_TRANSFER_EVENT) {
        return;
    }

    U64 Pointer = U64_Make(Event->Dword1, Event->Dword0);
    U32 Completion = XHCI_GetCompletionCode(Event->Dword2);
    U32 TransferLength = XHCI_GetTransferLength(Event->Dword2);
    U8 SlotId = (U8)((Event->Dword3 >> 24) & 0xFF);
    U8 EndpointId = (U8)((Event->Dword3 >> 16) & 0x1F);

    if (Device->CompletionCount >= XHCI_COMPLETION_QUEUE_MAX) {
        for (U32 Index = 1; Index < Device->CompletionCount; Index++) {
            Device->CompletionQueue[Index - 1] = Device->CompletionQueue[Index];
        }
        Device->CompletionCount = XHCI_COMPLETION_QUEUE_MAX - 1;
    }

    XHCI_COMPLETION* Entry = &Device->CompletionQueue[Device->CompletionCount++];
    Entry->TrbPhysical = Pointer;
    Entry->Completion = Completion;
    Entry->TransferLength = TransferLength;
    Entry->Type = Type;
    Entry->SlotId = SlotId;
    Entry->EndpointId = EndpointId;
}

/************************************************************************/

/**
 * @brief Try to pop a completion entry for a TRB.
 * @param Device xHCI device.
 * @param Type Expected completion type.
 * @param TrbPhysical TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE if a matching completion was found.
 */
BOOL XHCI_PopCompletion(LPXHCI_DEVICE Device,
                        U8 Type,
                        U64 TrbPhysical,
                        U8* SlotIdOut,
                        U32* CompletionOut,
                        U32* TransferLengthOut) {
    if (Device == NULL) {
        return FALSE;
    }

    for (U32 Index = 0; Index < Device->CompletionCount; Index++) {
        XHCI_COMPLETION* Entry = &Device->CompletionQueue[Index];
        if (Entry->Type != Type) {
            continue;
        }
        if (!XHCI_IsSameTrbPointer(Entry->TrbPhysical, TrbPhysical)) {
            continue;
        }

        if (SlotIdOut != NULL) {
            *SlotIdOut = Entry->SlotId;
        }
        if (CompletionOut != NULL) {
            *CompletionOut = Entry->Completion;
        }
        if (TransferLengthOut != NULL) {
            *TransferLengthOut = Entry->TransferLength;
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
 * @brief Remove cached transfer completions for one slot/endpoint route.
 * @param Device xHCI device.
 * @param SlotId Slot identifier.
 * @param EndpointId Endpoint identifier (DCI).
 */
void XHCI_ClearTransferCompletions(LPXHCI_DEVICE Device, U8 SlotId, U8 EndpointId) {
    U32 ReadIndex;
    U32 WriteIndex;

    if (Device == NULL || SlotId == 0 || EndpointId == 0) {
        return;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    XHCI_PollCompletions(Device);

    WriteIndex = 0;
    for (ReadIndex = 0; ReadIndex < Device->CompletionCount; ReadIndex++) {
        XHCI_COMPLETION* Entry = &Device->CompletionQueue[ReadIndex];
        if (Entry->Type == XHCI_TRB_TYPE_TRANSFER_EVENT &&
            Entry->SlotId == SlotId &&
            Entry->EndpointId == EndpointId) {
            continue;
        }

        if (WriteIndex != ReadIndex) {
            Device->CompletionQueue[WriteIndex] = *Entry;
        }
        WriteIndex++;
    }

    Device->CompletionCount = WriteIndex;
    UnlockMutex(&(Device->Mutex));
}

/************************************************************************/

/**
 * @brief Drain events until one targeted completion is found.
 * @param Device xHCI device.
 * @param Type Expected completion type.
 * @param TrbPhysical Target TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE when the target completion is found while draining events.
 */
BOOL XHCI_PollForCompletion(LPXHCI_DEVICE Device,
                            U8 Type,
                            U64 TrbPhysical,
                            U8* SlotIdOut,
                            U32* CompletionOut,
                            U32* TransferLengthOut) {
    XHCI_TRB Event;

    if (Device == NULL) {
        return FALSE;
    }

    XHCI_LogHseTransitionIfNeeded(Device, TEXT("PollForCompletion"));
    while (XHCI_DequeueEvent(Device, &Event)) {
        U8 EventType = (U8)XHCI_GetTrbType(Event.Dword3);
        if (EventType == XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT ||
            EventType == XHCI_TRB_TYPE_TRANSFER_EVENT) {
            U64 Pointer = U64_Make(Event.Dword1, Event.Dword0);
            if (EventType == Type && XHCI_IsSameTrbPointer(Pointer, TrbPhysical)) {
                if (SlotIdOut != NULL) {
                    *SlotIdOut = (U8)((Event.Dword3 >> 24) & 0xFF);
                }
                if (CompletionOut != NULL) {
                    *CompletionOut = XHCI_GetCompletionCode(Event.Dword2);
                }
                if (TransferLengthOut != NULL) {
                    *TransferLengthOut = XHCI_GetTransferLength(Event.Dword2);
                }
                return TRUE;
            }
        }

        XHCI_PushCompletion(Device, &Event);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB in a ring using xHCI link semantics.
 * @param RingLinear Ring base (linear).
 * @param RingPhysical Ring base (physical).
 * @param EnqueueIndex Ring enqueue index (in/out).
 * @param CycleState Ring cycle state (in/out).
 * @param RingTrbs Number of TRBs in ring (including link TRB).
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
BOOL XHCI_RingEnqueue(LINEAR RingLinear, PHYSICAL RingPhysical, U32* EnqueueIndex, U32* CycleState,
                      U32 RingTrbs, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (RingLinear == 0 || RingPhysical == 0 || EnqueueIndex == NULL || CycleState == NULL || Trb == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)RingLinear;
    U32 Index = *EnqueueIndex;
    U32 LinkIndex = RingTrbs - 1;

    if (Index >= LinkIndex) {
        Index = 0;
        *EnqueueIndex = 0;
    }

    XHCI_TRB Local = *Trb;
    Local.Dword3 |= (*CycleState ? XHCI_TRB_CYCLE : 0);

    Ring[Index] = Local;

    if (PhysicalOut != NULL) {
        *PhysicalOut = U64_FromUINT(RingPhysical + (Index * sizeof(XHCI_TRB)));
    }

    Index++;
    if (Index == LinkIndex) {
        Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                 (*CycleState ? XHCI_TRB_CYCLE : 0) |
                                 XHCI_TRB_TOGGLE_CYCLE;
        *CycleState ^= 1;
        Index = 0;
    }

    *EnqueueIndex = Index;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on the command ring.
 * @param Device xHCI device.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
/*
static LPCSTR XHCI_GetCommandTypeName(U32 Type) {
    switch (Type) {
        case XHCI_TRB_TYPE_ENABLE_SLOT:
            return TEXT("Enable Slot");
        case XHCI_TRB_TYPE_DISABLE_SLOT:
            return TEXT("Disable Slot");
        case XHCI_TRB_TYPE_ADDRESS_DEVICE:
            return TEXT("Address Device");
        case XHCI_TRB_TYPE_CONFIGURE_ENDPOINT:
            return TEXT("Configure Endpoint");
        case XHCI_TRB_TYPE_EVALUATE_CONTEXT:
            return TEXT("Evaluate Context");
        case XHCI_TRB_TYPE_RESET_ENDPOINT:
            return TEXT("Reset Endpoint");
        case XHCI_TRB_TYPE_STOP_ENDPOINT:
            return TEXT("Stop Endpoint");
        default:
            return TEXT("Unknown command");
    }
}
*/

/************************************************************************/

BOOL XHCI_CommandRingEnqueue(LPXHCI_DEVICE Device, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (Device == NULL || Trb == NULL) {
        return FALSE;
    }
    if (!XHCI_RingEnqueue(Device->CommandRingLinear, Device->CommandRingPhysical,
                          &Device->CommandRingEnqueueIndex, &Device->CommandRingCycleState,
                          XHCI_COMMAND_RING_TRBS, Trb, PhysicalOut)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on a transfer ring.
 * @param UsbDevice USB device state.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
BOOL XHCI_TransferRingEnqueue(LPXHCI_USB_DEVICE UsbDevice, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (UsbDevice == NULL || Trb == NULL) {
        return FALSE;
    }
    return XHCI_RingEnqueue(UsbDevice->TransferRingLinear, UsbDevice->TransferRingPhysical,
                            &UsbDevice->TransferRingEnqueueIndex, &UsbDevice->TransferRingCycleState,
                            XHCI_TRANSFER_RING_TRBS, Trb, PhysicalOut);
}

/************************************************************************/

/**
 * @brief Dequeue one event TRB if available.
 * @param Device xHCI device.
 * @param EventOut Receives the event TRB.
 * @return TRUE if an event was dequeued.
 */
BOOL XHCI_DequeueEvent(LPXHCI_DEVICE Device, XHCI_TRB* EventOut) {
    if (Device == NULL || EventOut == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->EventRingLinear;
    U32 Index = Device->EventRingDequeueIndex;
    XHCI_TRB Event = Ring[Index];

    if (((Event.Dword3 & XHCI_TRB_CYCLE) != 0) != (Device->EventRingCycleState != 0)) {
        return FALSE;
    }

    *EventOut = Event;

    Index++;
    if (Index >= XHCI_EVENT_RING_TRBS) {
        Index = 0;
        Device->EventRingCycleState ^= 1;
    }

    Device->EventRingDequeueIndex = Index;

    {
        LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
        U64 Erdp = U64_FromUINT(Device->EventRingPhysical + (Index * sizeof(XHCI_TRB)));
        Erdp = U64_Add(Erdp, U64_FromU32(XHCI_ERDP_EHB));
        XHCI_Write64(InterrupterBase, XHCI_ERDP, Erdp);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Drain the event ring and cache completion events.
 * @param Device xHCI device.
 */
void XHCI_PollCompletions(LPXHCI_DEVICE Device) {
    XHCI_TRB Event;
    XHCI_LogHseTransitionIfNeeded(Device, TEXT("PollCompletions"));
    while (XHCI_DequeueEvent(Device, &Event)) {
        XHCI_PushCompletion(Device, &Event);
    }
}

/************************************************************************/

/**
 * @brief Busy-wait for a register to match a value.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Mask Mask applied to register.
 * @param Value Expected value after masking.
 * @param Timeout Timeout in milliseconds.
 * @return TRUE on success, FALSE on timeout.
 */
BOOL XHCI_WaitForRegister(LINEAR Base, U32 Offset, U32 Mask, U32 Value, U32 Timeout, LPCSTR Name) {
    U32 ElapsedMilliseconds = 0;
    U32 NextWarningAt = 200;

    while (ElapsedMilliseconds < Timeout) {
        if ((XHCI_Read32(Base, Offset) & Mask) == Value) {
            return TRUE;
        }

        if (ElapsedMilliseconds >= NextWarningAt) {
            WARNING(TEXT("%s exceeded %u ms (base=%p off=%x mask=%x value=%x)"),
                    (Name != NULL) ? Name : TEXT("?"),
                    ElapsedMilliseconds,
                    (LPVOID)Base,
                    Offset,
                    Mask,
                    Value);
            if (NextWarningAt < MAX_U32 - 200) {
                NextWarningAt += 200;
            }
        }

        Sleep(1);
        ElapsedMilliseconds++;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate and map a single physical page.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL XHCI_AllocPage(LPCSTR Tag, PHYSICAL *PhysicalOut, LINEAR *LinearOut) {
    if (PhysicalOut == NULL || LinearOut == NULL) {
        return FALSE;
    }

    PHYSICAL Physical = AllocPhysicalPage();
    if (Physical == 0) {
        return FALSE;
    }

    LINEAR Linear = AllocKernelRegion(Physical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, Tag);
    if (Linear == 0) {
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    return TRUE;
}

/************************************************************************/
