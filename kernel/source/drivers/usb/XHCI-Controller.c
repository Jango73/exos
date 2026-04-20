
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

#include "drivers/input/USBKeyboard.h"
#include "drivers/input/USBMouse.h"
#include "drivers/usb/XHCI-Internal.h"

/************************************************************************/

static UINT XHCI_Commands(UINT Function, UINT Param);
static LPPCI_DEVICE XHCI_Attach(LPPCI_DEVICE PciDevice);

/************************************************************************/

static DRIVER_MATCH XHCI_MatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, XHCI_CLASS_SERIAL_BUS, XHCI_SUBCLASS_USB, XHCI_PROGIF_XHCI}};

PCI_DRIVER DATA_SECTION XHCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_XHCI,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "xHCI",
    .Alias = "xhci",
    .Command = XHCI_Commands,
    .EnumDomainCount = 3,
    .EnumDomains = {ENUM_DOMAIN_XHCI_PORT, ENUM_DOMAIN_USB_DEVICE, ENUM_DOMAIN_USB_NODE},
    .Matches = XHCI_MatchTable,
    .MatchCount = sizeof(XHCI_MatchTable) / sizeof(XHCI_MatchTable[0]),
    .Attach = XHCI_Attach};

/************************************************************************/

/**
 * @brief Retrieve the xHCI driver descriptor.
 * @return Pointer to xHCI driver descriptor.
 */
LPDRIVER XHCIGetDriver(void) { return (LPDRIVER)&XHCIDriver; }

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
static void XHCI_LogInitReadback(
    LPXHCI_DEVICE Device, LPCSTR Step, U64 ProgrammedDcbaap, U64 ProgrammedCrcr, U64 ProgrammedErstba,
    U64 ProgrammedErdp) {
    U32 Usbcmd = 0;
    U32 Usbsts = 0;
    U32 Config = 0;
    U32 CrcrLow = 0;
    U32 CrcrHigh = 0;
    U32 DcbaapLow = 0;
    U32 DcbaapHigh = 0;
    U32 Imod = 0;
    U32 Iman = 0;
    U32 Erstsz = 0;
    U32 ErdpLow = 0;
    U32 ErdpHigh = 0;
    U32 DcbaaEntry0Low = 0;
    U32 DcbaaEntry0High = 0;
    U32 ErstbaLow = 0;
    U32 ErstbaHigh = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;
    LINEAR InterrupterBase;

    if (Device == NULL || Device->OpBase == 0 || Device->RuntimeBase == 0) {
        return;
    }

    InterrupterBase = XHCI_GetInterrupterBase(Device);

    Usbcmd = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Usbsts = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    Config = XHCI_Read32(Device->OpBase, XHCI_OP_CONFIG);
    CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
    CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
    DcbaapLow = XHCI_Read32(Device->OpBase, XHCI_OP_DCBAAP);
    DcbaapHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_DCBAAP + 4));

    Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    Imod = XHCI_Read32(InterrupterBase, XHCI_IMOD);
    Erstsz = XHCI_Read32(InterrupterBase, XHCI_ERSTSZ);
    ErstbaLow = XHCI_Read32(InterrupterBase, XHCI_ERSTBA);
    ErstbaHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERSTBA + 4));
    ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
    ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
    if (Device->DcbaaLinear != 0) {
        U64 DcbaaEntry0 = ((volatile U64*)Device->DcbaaLinear)[0];
        DcbaaEntry0Low = U64_Low32(DcbaaEntry0);
        DcbaaEntry0High = U64_High32(DcbaaEntry0);
    }

    PciCommand = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    PciStatus = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_STATUS);

    DEBUG(
        TEXT("step=%s prog-dcbaap=%x:%x rd-dcbaap=%x:%x prog-crcr=%x:%x rd-crcr=%x:%x"), Step,
        U64_High32(ProgrammedDcbaap), U64_Low32(ProgrammedDcbaap), DcbaapHigh, DcbaapLow, U64_High32(ProgrammedCrcr),
        U64_Low32(ProgrammedCrcr), CrcrHigh, CrcrLow);
    DEBUG(
        TEXT("step=%s prog-erstba=%x:%x rd-erstba=%x:%x prog-erdp=%x:%x rd-erdp=%x:%x"), Step,
        U64_High32(ProgrammedErstba), U64_Low32(ProgrammedErstba), ErstbaHigh, ErstbaLow, U64_High32(ProgrammedErdp),
        U64_Low32(ProgrammedErdp), ErdpHigh, ErdpLow);
    DEBUG(
        TEXT("step=%s usbcmd=%x usbsts=%x config=%x iman=%x imod=%x erstsz=%x dcbaa0=%x:%x "
             "pci-cmd=%x pci-sts=%x"),
        Step, Usbcmd, Usbsts, Config, Iman, Imod, Erstsz, DcbaaEntry0High, DcbaaEntry0Low, PciCommand, PciStatus);
}

/************************************************************************/

/**
 * @brief Clear pending interrupt status.
 * @param Device xHCI device.
 */
static void XHCI_ClearInterruptPending(LPXHCI_DEVICE Device) {
    LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
    U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    Iman |= XHCI_IMAN_IP;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, Iman);
}

/************************************************************************/

/**
 * @brief Enable or disable interrupter delivery.
 * @param Device xHCI device.
 * @param Enabled TRUE to enable interrupts, FALSE to disable.
 */
static void XHCI_SetInterruptEnabled(LPXHCI_DEVICE Device, BOOL Enabled) {
    LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
    U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
    U32 Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Iman &= ~XHCI_IMAN_IE;
    if (Enabled) {
        Iman |= XHCI_IMAN_IE;
        Command |= XHCI_USBCMD_INTE;
    } else {
        Command &= ~XHCI_USBCMD_INTE;
    }
    Iman |= XHCI_IMAN_IP;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, Iman);
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);
}

/************************************************************************/

/**
 * @brief Top-half xHCI interrupt handler.
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (xHCI device).
 * @return TRUE if interrupt was handled.
 */
static BOOL XHCI_InterruptTopHalf(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)Context;
    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        if (Device->RuntimeBase == 0) {
            return FALSE;
        }

        LINEAR InterrupterBase = XHCI_GetInterrupterBase(Device);
        U32 Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
        if ((Iman & XHCI_IMAN_IP) == 0U) {
            return FALSE;
        }

        XHCI_ClearInterruptPending(Device);
        Device->InterruptCount++;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Bottom-half xHCI interrupt handler.
 * @param DevicePointer Device pointer from interrupt context.
 * @param Context Driver context (xHCI device).
 */
static void XHCI_InterruptBottomHalf(LPDEVICE DevicePointer, LPVOID Context) {
    UNUSED(DevicePointer);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)Context;
    SAFE_USE_VALID_ID((LPLISTNODE)Device, KOID_PCIDEVICE) {
        LockMutex(&(Device->Mutex), INFINITY);
        XHCI_PollCompletions(Device);
        UnlockMutex(&(Device->Mutex));
        USBKeyboardOnXhciInterrupt(Device);
        USBMouseOnXhciInterrupt(Device);
    }
}

/************************************************************************/

/**
 * @brief Poll-mode interrupt handler.
 * @param DevicePointer Device pointer from polling context.
 * @param Context Driver context (xHCI device).
 */
static void XHCI_InterruptPoll(LPDEVICE DevicePointer, LPVOID Context) {
    (void)XHCI_InterruptTopHalf(DevicePointer, Context);
    XHCI_InterruptBottomHalf(DevicePointer, Context);
}

/************************************************************************/

/**
 * @brief Register xHCI interrupts via DeviceInterrupt infrastructure.
 * @param Device xHCI device.
 * @return TRUE on success.
 */
static BOOL XHCI_RegisterInterrupts(LPXHCI_DEVICE Device) {
    if (Device == NULL) {
        return FALSE;
    }

    if (Device->InterruptRegistered) {
        return TRUE;
    }

    if (Device->Info.IRQLine == 0xFFU) {
        WARNING(TEXT("Controller reports no legacy IRQ line"));
    }

    DEVICE_INTERRUPT_REGISTRATION Registration = {
        .Device = (LPDEVICE)Device,
        .LegacyIRQ = Device->Info.IRQLine,
        .TargetCPU = 0,
        .InterruptHandler = XHCI_InterruptTopHalf,
        .DeferredCallback = XHCI_InterruptBottomHalf,
        .PollCallback = XHCI_InterruptPoll,
        .Context = (LPVOID)Device,
        .Name = (Device->Driver != NULL) ? Device->Driver->Product : TEXT("xHCI"),
    };

    if (!DeviceInterruptRegister(&Registration, &Device->InterruptSlot)) {
        WARNING(TEXT("Failed to register interrupt slot for IRQ %u"), Device->Info.IRQLine);
        Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        return FALSE;
    }

    Device->InterruptRegistered = TRUE;
    Device->InterruptEnabled = DeviceInterruptSlotIsEnabled(Device->InterruptSlot);
    XHCI_SetInterruptEnabled(Device, Device->InterruptEnabled);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Unregister xHCI interrupts.
 * @param Device xHCI device.
 */
static void XHCI_UnregisterInterrupts(LPXHCI_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    if (!Device->InterruptRegistered) {
        return;
    }

    XHCI_SetInterruptEnabled(Device, FALSE);

    if (Device->InterruptSlot != DEVICE_INTERRUPT_INVALID_SLOT) {
        DeviceInterruptUnregister(Device->InterruptSlot);
    }

    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->InterruptRegistered = FALSE;
    Device->InterruptEnabled = FALSE;
}
/************************************************************************/

/**
 * @brief Release all scratchpad-related allocations.
 * @param Device xHCI device.
 */
static void XHCI_FreeScratchpadBuffers(LPXHCI_DEVICE Device) {
    UINT Index;

    if (Device == NULL) {
        return;
    }

    if (Device->ScratchpadPages != NULL) {
        for (Index = 0; Index < Device->MaxScratchpadBuffers; Index++) {
            PHYSICAL ScratchpadPhysical = Device->ScratchpadPages[Index];
            if (ScratchpadPhysical != 0) {
                FreePhysicalPage(ScratchpadPhysical);
                Device->ScratchpadPages[Index] = 0;
            }
        }
        KernelHeapFree(Device->ScratchpadPages);
        Device->ScratchpadPages = NULL;
    }

    if (Device->ScratchpadArrayLinear != 0) {
        FreeRegion(Device->ScratchpadArrayLinear, PAGE_SIZE);
        Device->ScratchpadArrayLinear = 0;
    }
    if (Device->ScratchpadArrayPhysical != 0) {
        FreePhysicalPage(Device->ScratchpadArrayPhysical);
        Device->ScratchpadArrayPhysical = 0;
    }

    if (Device->DcbaaLinear != 0) {
        ((volatile U64*)Device->DcbaaLinear)[0] = U64_FromU32(0);
    }
}

/************************************************************************/

/**
 * @brief Allocate scratchpad buffers and program DCBAA[0] when required.
 * @param Device xHCI device.
 * @return TRUE on success.
 */
static BOOL XHCI_InitScratchpadBuffers(LPXHCI_DEVICE Device) {
    UINT Index;
    volatile U64* Dcbaa;
    volatile U64* ScratchpadArray;

    if (Device == NULL || Device->DcbaaLinear == 0) {
        return FALSE;
    }

    Dcbaa = (volatile U64*)Device->DcbaaLinear;
    Dcbaa[0] = U64_FromU32(0);

    if (Device->MaxScratchpadBuffers == 0) {
        return TRUE;
    }

    if (Device->MaxScratchpadBuffers > (PAGE_SIZE / sizeof(U64))) {
        ERROR(TEXT("Unsupported scratchpad count %u"), (U32)Device->MaxScratchpadBuffers);
        return FALSE;
    }

    Device->ScratchpadPages = (PHYSICAL*)KernelHeapAlloc(sizeof(PHYSICAL) * (UINT)Device->MaxScratchpadBuffers);
    if (Device->ScratchpadPages == NULL) {
        ERROR(TEXT("Scratchpad list allocation failed"));
        return FALSE;
    }
    MemorySet(Device->ScratchpadPages, 0, sizeof(PHYSICAL) * (UINT)Device->MaxScratchpadBuffers);

    if (!XHCI_AllocPage(
            TEXT("XHCI_ScratchpadArray"), &Device->ScratchpadArrayPhysical, &Device->ScratchpadArrayLinear)) {
        ERROR(TEXT("Scratchpad array allocation failed"));
        XHCI_FreeScratchpadBuffers(Device);
        return FALSE;
    }

    ScratchpadArray = (volatile U64*)Device->ScratchpadArrayLinear;
    MemorySet((LPVOID)Device->ScratchpadArrayLinear, 0, PAGE_SIZE);

    for (Index = 0; Index < Device->MaxScratchpadBuffers; Index++) {
        PHYSICAL ScratchpadPhysical = AllocPhysicalPage();
        if (ScratchpadPhysical == 0) {
            ERROR(TEXT("Scratchpad page allocation failed at %u"), (U32)Index);
            XHCI_FreeScratchpadBuffers(Device);
            return FALSE;
        }

        Device->ScratchpadPages[Index] = ScratchpadPhysical;
        ScratchpadArray[Index] = U64_FromUINT(ScratchpadPhysical);

        {
            LINEAR ScratchpadLinear = MapTemporaryPhysicalPage1(ScratchpadPhysical);
            if (ScratchpadLinear != 0) {
                MemorySet((LPVOID)ScratchpadLinear, 0, PAGE_SIZE);
            }
        }
    }

    Dcbaa[0] = U64_FromUINT(Device->ScratchpadArrayPhysical);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Free xHCI allocations and MMIO mapping.
 * @param Device xHCI device.
 */
void XHCI_FreeResources(LPXHCI_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        XHCI_UnregisterInterrupts(Device);
        if (DeferredWorkTokenIsValid(Device->HubPollToken) != FALSE) {
            DeferredWorkUnregister(Device->HubPollToken);
            Device->HubPollToken =
                (DEFERRED_WORK_TOKEN){.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
        }

        if (Device->UsbDevices != NULL) {
            for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
                if (UsbDevice != NULL) {
                    XHCI_DestroyUsbDevice(Device, UsbDevice, TRUE);
                    Device->UsbDevices[PortIndex] = NULL;
                }
            }
            KernelHeapFree(Device->UsbDevices);
            Device->UsbDevices = NULL;
        }
        if (Device->EventRingTableLinear) {
            FreeRegion(Device->EventRingTableLinear, PAGE_SIZE);
            Device->EventRingTableLinear = 0;
        }
        if (Device->EventRingTablePhysical) {
            FreePhysicalPage(Device->EventRingTablePhysical);
            Device->EventRingTablePhysical = 0;
        }
        if (Device->EventRingLinear) {
            FreeRegion(Device->EventRingLinear, PAGE_SIZE);
            Device->EventRingLinear = 0;
        }
        if (Device->EventRingPhysical) {
            FreePhysicalPage(Device->EventRingPhysical);
            Device->EventRingPhysical = 0;
        }
        if (Device->CommandRingLinear) {
            FreeRegion(Device->CommandRingLinear, PAGE_SIZE);
            Device->CommandRingLinear = 0;
        }
        if (Device->CommandRingPhysical) {
            FreePhysicalPage(Device->CommandRingPhysical);
            Device->CommandRingPhysical = 0;
        }
        XHCI_FreeScratchpadBuffers(Device);
        if (Device->DcbaaLinear) {
            FreeRegion(Device->DcbaaLinear, PAGE_SIZE);
            Device->DcbaaLinear = 0;
        }
        if (Device->DcbaaPhysical) {
            FreePhysicalPage(Device->DcbaaPhysical);
            Device->DcbaaPhysical = 0;
        }
        if (Device->MmioBase != 0 && Device->MmioSize != 0) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            Device->MmioBase = 0;
            Device->MmioSize = 0;
        }
    }
}

/************************************************************************/

/**
 * @brief Read the full configuration descriptor.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @param LengthOut Receives total length.
 * @return TRUE on success.
 */
/**
 * @brief Read a port status register.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return PORTSC value.
 */
U32 XHCI_ReadPortStatus(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    return XHCI_Read32(Device->OpBase, Offset);
}

/************************************************************************/

/**
 * @brief Power on a port if supported by the controller.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 */
static void XHCI_PowerPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_PP) != 0) {
        return;
    }

    U32 WriteValue = PortStatus | XHCI_PORTSC_PP;
    WriteValue &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, WriteValue);
}

/**
 * @brief Initialize the command ring.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitCommandRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_CommandRing"), &Device->CommandRingPhysical, &Device->CommandRingLinear)) {
        ERROR(TEXT("Command ring allocation failed"));
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->CommandRingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_COMMAND_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(Device->CommandRingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    Device->CommandRingCycleState = 1;
    Device->CommandRingEnqueueIndex = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the event ring and interrupter 0.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitEventRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_EventRing"), &Device->EventRingPhysical, &Device->EventRingLinear)) {
        ERROR(TEXT("Event ring allocation failed"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_EventRingTable"), &Device->EventRingTablePhysical, &Device->EventRingTableLinear)) {
        ERROR(TEXT("ERST allocation failed"));
        return FALSE;
    }

    LPXHCI_ERST_ENTRY Entries = (LPXHCI_ERST_ENTRY)Device->EventRingTableLinear;
    MemorySet(Entries, 0, PAGE_SIZE);
    Entries[0].SegmentBase = U64_FromUINT(Device->EventRingPhysical);
    Entries[0].SegmentSize = XHCI_EVENT_RING_TRBS;
    Entries[0].Reserved = 0;
    Entries[0].Reserved2 = 0;

    LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, 0);
    XHCI_Write32(InterrupterBase, XHCI_IMOD, 0);
    XHCI_Write32(InterrupterBase, XHCI_ERSTSZ, 1);
    XHCI_Write64(InterrupterBase, XHCI_ERSTBA, U64_FromUINT(Device->EventRingTablePhysical));
    XHCI_Write64(InterrupterBase, XHCI_ERDP, U64_FromUINT(Device->EventRingPhysical));

    XHCI_LogInitReadback(
        Device, TEXT("InitEventRing"), U64_FromUINT(Device->DcbaaPhysical), U64_FromUINT(Device->CommandRingPhysical),
        U64_FromUINT(Device->EventRingTablePhysical), U64_FromUINT(Device->EventRingPhysical));

    Device->EventRingDequeueIndex = 0;
    Device->EventRingCycleState = 1;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset and start the xHCI controller.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_ResetAndStart(LPXHCI_DEVICE Device) {
    U32 Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command &= ~XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);
    XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-AfterStop"));

    if (!XHCI_WaitForRegister(
            Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, XHCI_CONTROLLER_HALT_TIMEOUT_MS,
            TEXT("Controller halt"))) {
        XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-HaltTimeout"));
        ERROR(TEXT("Halt timeout"));
        return FALSE;
    }

    Command |= XHCI_USBCMD_HCRST;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);
    XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-AfterResetRequest"));

    if (!XHCI_WaitForRegister(
            Device->OpBase, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0, XHCI_CONTROLLER_RESET_TIMEOUT_MS,
            TEXT("Controller reset"))) {
        XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-ResetTimeout"));
        ERROR(TEXT("Reset bit timeout"));
        return FALSE;
    }

    if (!XHCI_WaitForRegister(
            Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0, XHCI_CONTROLLER_RESET_TIMEOUT_MS,
            TEXT("Controller ready"))) {
        XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-ReadyTimeout"));
        ERROR(TEXT("Controller not ready"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DCBAA"), &Device->DcbaaPhysical, &Device->DcbaaLinear)) {
        ERROR(TEXT("DCBAA allocation failed"));
        return FALSE;
    }

    if (!XHCI_InitCommandRing(Device)) {
        return FALSE;
    }

    if (!XHCI_InitEventRing(Device)) {
        return FALSE;
    }

    if (!XHCI_InitScratchpadBuffers(Device)) {
        return FALSE;
    }

    XHCI_Write64(Device->OpBase, XHCI_OP_DCBAAP, U64_FromUINT(Device->DcbaaPhysical));

    {
        U64 Crcr = U64_FromUINT(Device->CommandRingPhysical);
        U32 Low = U64_Low32(Crcr) | XHCI_TRB_CYCLE;
        U32 High = U64_High32(Crcr);
        XHCI_Write32(Device->OpBase, XHCI_OP_CRCR, Low);
        XHCI_Write32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4), High);
    }

    XHCI_Write32(Device->OpBase, XHCI_OP_CONFIG, Device->MaxSlots);
    XHCI_LogInitReadback(
        Device, TEXT("ProgramCoreRegisters"), U64_FromUINT(Device->DcbaaPhysical),
        U64_Add(U64_FromUINT(Device->CommandRingPhysical), U64_FromU32(XHCI_TRB_CYCLE)),
        U64_FromUINT(Device->EventRingTablePhysical), U64_FromUINT(Device->EventRingPhysical));

    Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command |= XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);
    XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-AfterRunRequest"));

    if (!XHCI_WaitForRegister(
            Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0, XHCI_CONTROLLER_RUN_TIMEOUT_MS,
            TEXT("Controller run"))) {
        XHCI_LogHseTransitionIfNeeded(Device, TEXT("ResetAndStart-RunTimeout"));
        ERROR(TEXT("Run timeout"));
        return FALSE;
    }

    XHCI_LogInitReadback(
        Device, TEXT("ControllerRunning"), U64_FromUINT(Device->DcbaaPhysical),
        U64_Add(U64_FromUINT(Device->CommandRingPhysical), U64_FromU32(XHCI_TRB_CYCLE)),
        U64_FromUINT(Device->EventRingTablePhysical), U64_FromUINT(Device->EventRingPhysical));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize xHCI MMIO offsets and controller capabilities.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitController(LPXHCI_DEVICE Device) {
    U32 CapLengthReg = XHCI_Read32(Device->MmioBase, XHCI_CAPLENGTH);
    U32 HcsParams2;
    U32 ScratchpadLow;
    U32 ScratchpadHigh;
    Device->CapLength = (U8)(CapLengthReg & MAX_U8);
    Device->HciVersion = (U16)((CapLengthReg >> 16) & 0xFFFF);

    U32 HcsParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCSPARAMS1);
    HcsParams2 = XHCI_Read32(Device->MmioBase, XHCI_HCSPARAMS2);
    Device->MaxSlots = (U8)(HcsParams1 & XHCI_HCSPARAMS1_MAXSLOTS_MASK);
    Device->MaxInterrupters = (U16)((HcsParams1 & XHCI_HCSPARAMS1_MAXINTRS_MASK) >> XHCI_HCSPARAMS1_MAXINTRS_SHIFT);
    Device->MaxPorts = (U8)((HcsParams1 & XHCI_HCSPARAMS1_MAXPORTS_MASK) >> XHCI_HCSPARAMS1_MAXPORTS_SHIFT);
    ScratchpadLow = (HcsParams2 & XHCI_HCSPARAMS2_SCRATCHPAD_LOW_MASK) >> XHCI_HCSPARAMS2_SCRATCHPAD_LOW_SHIFT;
    ScratchpadHigh = (HcsParams2 & XHCI_HCSPARAMS2_SCRATCHPAD_HIGH_MASK) >> XHCI_HCSPARAMS2_SCRATCHPAD_HIGH_SHIFT;
    Device->MaxScratchpadBuffers = (U16)(ScratchpadLow | (ScratchpadHigh << 5));
    Device->HcsParams2 = HcsParams2;
    Device->HccParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCCPARAMS1);
    Device->ContextSize = ((Device->HccParams1 & XHCI_HCCPARAMS1_CSZ) != 0) ? 64 : 32;

    Device->OpBase = Device->MmioBase + Device->CapLength;

    U32 DbOff = XHCI_Read32(Device->MmioBase, XHCI_DBOFF);
    U32 RtOff = XHCI_Read32(Device->MmioBase, XHCI_RTSOFF);
    Device->DoorbellBase = Device->MmioBase + (DbOff & 0xFFFFFFFC);
    Device->RuntimeBase = Device->MmioBase + (RtOff & 0xFFFFFFE0);

    if ((Device->HccParams1 & XHCI_HCCPARAMS1_AC64) == 0) {
        WARNING(TEXT("64-bit addressing not supported"));
    }

    if (!XHCI_ResetAndStart(Device)) {
        return FALSE;
    }

    if ((HcsParams1 & XHCI_HCSPARAMS1_PPC) != 0) {
        for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
            XHCI_PowerPort(Device, PortIndex);
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Probe callback used by PCI subsystem.
 * @param PciInfo PCI device info.
 * @return DF_RETURN_SUCCESS when supported, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static U32 XHCI_OnProbe(const PCI_INFO* PciInfo) {
    if (PciInfo == NULL) return DF_RETURN_BAD_PARAMETER;
    if (PciInfo->BaseClass != XHCI_CLASS_SERIAL_BUS) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->SubClass != XHCI_SUBCLASS_USB) return DF_RETURN_NOT_IMPLEMENTED;
    if (PciInfo->ProgIF != XHCI_PROGIF_XHCI) return DF_RETURN_NOT_IMPLEMENTED;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Load callback for driver.
 * @return DF_RETURN_SUCCESS.
 */
static U32 XHCI_OnLoad(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Unload callback for driver.
 * @return DF_RETURN_SUCCESS.
 */
static U32 XHCI_OnUnload(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Version callback for driver.
 * @return Encoded version.
 */
static U32 XHCI_OnGetVersion(void) { return MAKE_VERSION(1, 0); }

/************************************************************************/

/**
 * @brief Capabilities callback for driver.
 * @return Capability bitmask.
 */
static U32 XHCI_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Last function callback.
 * @return Last function ID.
 */
static U32 XHCI_OnGetLastFunc(void) { return DF_PROBE; }

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDevice PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE XHCI_Attach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)KernelHeapAlloc(sizeof(XHCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(XHCI_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->HubPollToken =
        (DEFERRED_WORK_TOKEN){.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
    Device->CommandTimeoutMS = XHCI_DEFAULT_EVENT_TIMEOUT_MS;
    Device->TransferTimeoutMS = XHCI_DEFAULT_TRANSFER_TIMEOUT_MS;

    U32 Bar0Raw = Device->Info.BAR[0];
    U32 Bar1Raw = Device->Info.BAR[1];
    U32 Bar0Base = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    BOOL Is64Bit = ((Bar0Raw & 0x6) == 0x4);
    PHYSICAL MmioPhysical = (PHYSICAL)Bar0Base;

    if (Is64Bit) {
#if defined(__EXOS_ARCH_X86_64__)
        U64 Bar64 = U64_Make(Bar1Raw, Bar0Base);
        MmioPhysical = (PHYSICAL)Bar64;
        if (U64_EQUAL(Bar64, U64_0)) {
            ERROR(TEXT("Invalid BAR0"));
            KernelHeapFree(Device);
            return NULL;
        }
#else
        if (Bar1Raw != 0u) {
            ERROR(TEXT("64-bit BAR above 4GB not supported (BAR1=%x)"), Bar1Raw);
            KernelHeapFree(Device);
            return NULL;
        }
#endif
    }

    if (Bar0Size == 0) {
        ERROR(TEXT("Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(MmioPhysical, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == 0) {
        ERROR(TEXT("MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!XHCI_InitController(Device)) {
        ERROR(TEXT("Controller init failed"));
        XHCI_FreeResources(Device);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Device->MaxPorts > 0) {
        U32 PortCount = Device->MaxPorts;
        Device->UsbDevices = (LPXHCI_USB_DEVICE*)KernelHeapAlloc(sizeof(LPXHCI_USB_DEVICE) * PortCount);
        if (Device->UsbDevices == NULL) {
            ERROR(TEXT("USB device state allocation failed"));
            XHCI_FreeResources(Device);
            KernelHeapFree(Device);
            return NULL;
        }
        MemorySet(Device->UsbDevices, 0, sizeof(LPXHCI_USB_DEVICE) * PortCount);
        for (U32 PortIndex = 0; PortIndex < PortCount; PortIndex++) {
            LPXHCI_USB_DEVICE UsbDevice =
                (LPXHCI_USB_DEVICE)CreateKernelObject(sizeof(XHCI_USB_DEVICE), KOID_USBDEVICE);
            if (UsbDevice == NULL) {
                ERROR(TEXT("USB device object allocation failed"));
                XHCI_FreeResources(Device);
                KernelHeapFree(Device);
                return NULL;
            }
            XHCI_InitUsbDeviceObject(Device, UsbDevice);
            UsbDevice->IsRootPort = TRUE;
            UsbDevice->PortNumber = (U8)(PortIndex + 1);
            UsbDevice->RootPortNumber = UsbDevice->PortNumber;
            UsbDevice->Depth = 0;
            UsbDevice->RouteString = 0;
            Device->UsbDevices[PortIndex] = UsbDevice;
            XHCI_AddDeviceToList(Device, UsbDevice);
        }
    }

    (void)XHCI_RegisterInterrupts(Device);
    XHCI_RegisterHubPoll(Device);

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT XHCI_Commands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return XHCI_OnLoad();
        case DF_UNLOAD:
            return XHCI_OnUnload();
        case DF_GET_VERSION:
            return XHCI_OnGetVersion();
        case DF_GET_CAPS:
            return XHCI_OnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return XHCI_OnGetLastFunc();
        case DF_PROBE:
            return XHCI_OnProbe((const PCI_INFO*)(LPVOID)Param);
        case DF_ENUM_NEXT:
            return XHCI_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Param);
        case DF_ENUM_PRETTY:
            return XHCI_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Param);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
