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


    RTL8139

\************************************************************************/

#include "drivers/network/RTL8139.h"

#include "core/Kernel.h"
#include "log/Log.h"

/************************************************************************/

typedef struct tag_RTL8139_DEVICE RTL8139_DEVICE, *LPRTL8139_DEVICE;

/************************************************************************/

#define RTL8139_VERSION_MAJOR 1
#define RTL8139_VERSION_MINOR 0
#define RTL8139_LINK_SPEED_UNKNOWN 0

/************************************************************************/

struct tag_RTL8139_DEVICE {
    REALTEK_NETWORK_COMMON_DEVICE_FIELDS

    const RTL8139_DEVICE_INFO* DeviceInfo;
    U32 HardwareRevision;
    DMA_BUFFER RxBuffer;
    DMA_BUFFER TxBufferPool;
    UINT RxReadOffset;
    UINT TxNextSlot;
};

/************************************************************************/

static UINT RTL8139Commands(UINT Function, UINT Parameter);
static U32 RTL8139OnProbe(const PCI_INFO* PciInfo);
static LPPCI_DEVICE RTL8139Attach(LPPCI_DEVICE PciDevice);
static const RTL8139_DEVICE_INFO* RTL8139FindDeviceInfo(U16 VendorID, U16 DeviceID);
static void RTL8139InitializeHardwareDescription(LPRTL8139_DEVICE Device);
static U32 RTL8139InitializeRegisterAccess(LPRTL8139_DEVICE Device);
static U32 RTL8139AllocateBuffers(LPRTL8139_DEVICE Device);
static void RTL8139InitializeReceiveFilter(LPRTL8139_DEVICE Device);
static void RTL8139WriteCurrentPacketRead(LPRTL8139_DEVICE Device);
static void RTL8139ProgramBufferAddresses(LPRTL8139_DEVICE Device);
static U32 RTL8139InitializeController(LPRTL8139_DEVICE Device);
static U32 RTL8139OnReset(const NETWORK_RESET* Reset);
static void RTL8139ReadPermanentMac(LPRTL8139_DEVICE Device);
static void RTL8139QueryLinkState(LPRTL8139_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull);
static U32 RTL8139OnGetInfo(const NETWORK_GET_INFO* GetInfo);
static U32 RTL8139PollReceive(LPRTL8139_DEVICE Device);
static U32 RTL8139PollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device);
static U32 RTL8139OnSend(const NETWORK_SEND* Send);
static U32 RTL8139OnPoll(const NETWORK_POLL* Poll);
static U32 RTL8139OnGetVersion(void);

/************************************************************************/

static DRIVER_MATCH RTL8139MatchTable[] = {
    REALTEK_NETWORK_MATCH_ENTRY(RTL8139_DEVICE_8139),
};

/************************************************************************/

static const RTL8139_TX_SLOT_INFO RTL8139TxSlotInfoTable[RTL8139_TX_SLOT_COUNT] = {
    {.StatusRegisterOffset = RTL8139_REG_TXSTATUS0, .AddressRegisterOffset = RTL8139_REG_TXADDR0},
    {.StatusRegisterOffset = RTL8139_REG_TXSTATUS1, .AddressRegisterOffset = RTL8139_REG_TXADDR1},
    {.StatusRegisterOffset = RTL8139_REG_TXSTATUS2, .AddressRegisterOffset = RTL8139_REG_TXADDR2},
    {.StatusRegisterOffset = RTL8139_REG_TXSTATUS3, .AddressRegisterOffset = RTL8139_REG_TXADDR3},
};

/************************************************************************/

static const RTL8139_DEVICE_INFO RTL8139DeviceInfoTable[] = {
    {
        .VendorID = RTL8139_VENDOR_REALTEK,
        .DeviceID = RTL8139_DEVICE_8139,
        .Family = RTL8139_DEVICE_FAMILY_8139,
        .QuirkFlags = RTL8139_QUIRK_LEGACY_PCI_FAST_ETHERNET |
                      RTL8139_QUIRK_SHARED_QEMU_MODEL |
                      RTL8139_QUIRK_RX_BUFFER_RING,
        .ProductName = TEXT("RTL8139 Family"),
    },
};

/************************************************************************/

PCI_DRIVER DATA_SECTION RTL8139Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = RTL8139_VERSION_MAJOR,
    .VersionMinor = RTL8139_VERSION_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Realtek",
    .Product = "RTL8139 Family",
    .Alias = "rtl8139",
    .Command = RTL8139Commands,
    .Matches = RTL8139MatchTable,
    .MatchCount = sizeof(RTL8139MatchTable) / sizeof(RTL8139MatchTable[0]),
    .Attach = RTL8139Attach,
};

/************************************************************************/

/**
 * @brief Retrieves the RTL8139 PCI driver descriptor.
 * @return Pointer to the RTL8139 PCI driver.
 */
LPDRIVER RTL8139GetDriver(void) {
    return (LPDRIVER)&RTL8139Driver;
}

/************************************************************************/

/**
 * @brief Handles PCI probe requests for the Realtek RTL8139 family.
 * @param PciInfo PCI function information provided by the PCI layer.
 * @return DF_RETURN_SUCCESS when the function matches the Ethernet family.
 */
static U32 RTL8139OnProbe(const PCI_INFO* PciInfo) {
    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    // Linux routes revision 0x20 and above to the CPlus datapath.
    if (PciInfo->Revision >= 0x20) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Looks up the compact hardware description for a PCI identifier.
 * @param VendorID PCI vendor identifier.
 * @param DeviceID PCI device identifier.
 * @return Matching device-description entry or NULL when unsupported.
 */
static const RTL8139_DEVICE_INFO* RTL8139FindDeviceInfo(U16 VendorID, U16 DeviceID) {
    UINT Index;

    for (Index = 0; Index < sizeof(RTL8139DeviceInfoTable) / sizeof(RTL8139DeviceInfoTable[0]); Index++) {
        const RTL8139_DEVICE_INFO* DeviceInfo = &RTL8139DeviceInfoTable[Index];

        if (DeviceInfo->VendorID == VendorID && DeviceInfo->DeviceID == DeviceID) {
            return DeviceInfo;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initializes the revision-independent hardware-description state.
 * @param Device Target RTL8139 device context.
 */
static void RTL8139InitializeHardwareDescription(LPRTL8139_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->DeviceInfo = RTL8139FindDeviceInfo(Device->Info.VendorID, Device->Info.DeviceID);
    Device->HardwareRevision = 0;
    Device->RxReadOffset = 0;
    Device->TxNextSlot = 0;
    UNUSED(RTL8139TxSlotInfoTable);
}

/************************************************************************/

/**
 * @brief Allocate RX and TX buffers required by the RTL8139 programming model.
 * @param Device Target RTL8139 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139AllocateBuffers(LPRTL8139_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->RxBuffer.LinearBase == 0) {
        if (!DMABufferAllocate(&Device->RxBuffer, RTL8139_RX_BUFFER_SIZE, TRUE, TEXT("RTL8139RxBuffer"))) {
            ERROR(TEXT("RX buffer allocation failed"));
            return DF_RETURN_NO_MEMORY;
        }
    }

    if (Device->TxBufferPool.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->TxBufferPool,
                RTL8139_TX_SLOT_COUNT * RTL8139_TX_BUFFER_SIZE,
                TRUE,
                TEXT("RTL8139TxBufferPool"))) {
            ERROR(TEXT("TX buffer allocation failed"));
            DMABufferRelease(&Device->RxBuffer);
            return DF_RETURN_NO_MEMORY;
        }
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Program the default RTL8139 multicast filter state.
 * @param Device Target RTL8139 device context.
 */
static void RTL8139InitializeReceiveFilter(LPRTL8139_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkClearMulticastRegisters((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_MAR0);
}

/************************************************************************/

/**
 * @brief Update CAPR using the RTL8139 current-read-minus-16 rule.
 * @param Device Target RTL8139 device context.
 */
static void RTL8139WriteCurrentPacketRead(LPRTL8139_DEVICE Device) {
    U16 CurrentPacketRead;

    if (Device == NULL) {
        return;
    }

    CurrentPacketRead = (U16)(Device->RxReadOffset - RTL8139_RX_READ_POINTER_ADJUST);
    if (Device->RxReadOffset == 0) {
        CurrentPacketRead = RTL8139_CAPR_INITIAL_VALUE;
    }

    RealtekNetworkWriteRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_CAPR, CurrentPacketRead);
}

/************************************************************************/

/**
 * @brief Program the active RX and TX buffer addresses into the controller.
 * @param Device Target RTL8139 device context.
 */
static void RTL8139ProgramBufferAddresses(LPRTL8139_DEVICE Device) {
    UINT Index;

    if (Device == NULL) {
        return;
    }

    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_RXBUFSTART,
        Device->RxBuffer.PhysicalBase);

    for (Index = 0; Index < RTL8139_TX_SLOT_COUNT; Index++) {
        PHYSICAL PhysicalAddress = DMABufferGetPhysical(&Device->TxBufferPool, Index * RTL8139_TX_BUFFER_SIZE);
        RealtekNetworkWriteRegister32(
            (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
            RTL8139TxSlotInfoTable[Index].AddressRegisterOffset,
            (U32)PhysicalAddress);
    }
}

/************************************************************************/

/**
 * @brief Attaches the driver to a supported PCI function.
 * @param PciDevice Supported PCI function.
 * @return Attached PCI device on success, NULL on failure.
 */
static LPPCI_DEVICE RTL8139Attach(LPPCI_DEVICE PciDevice) {
    LPRTL8139_DEVICE Device;
    U32 Result;

    Device = (LPRTL8139_DEVICE)RealtekNetworkAttachCommon(
        sizeof(RTL8139_DEVICE),
        PciDevice,
        TEXT("RTL8139Attach"));
    if (Device == NULL) {
        return NULL;
    }

    RTL8139InitializeHardwareDescription(Device);
    Device->PollRoutine = RTL8139PollDevice;
    RealtekNetworkConfigureInterruptSupport(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_INTRMASK,
        RTL8139_REG_INTRSTATUS,
        RTL8139_INTERRUPT_ENABLE_MASK,
        RTL8139_INTERRUPT_RELEVANT_MASK,
        RTL8139_INTERRUPT_ACKNOWLEDGE_AFTER_POLL_MASK);
    if (Device->DeviceInfo == NULL) {
        ERROR(TEXT("Missing hardware description for %x:%x"),
              (UINT)Device->Info.VendorID,
              (UINT)Device->Info.DeviceID);
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8139InitializeRegisterAccess(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8139AllocateBuffers(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8139InitializeController(Device);
    if (Result != DF_RETURN_SUCCESS) {
        DMABufferRelease(&Device->TxBufferPool);
        DMABufferRelease(&Device->RxBuffer);
        ReleaseKernelObject(Device);
        return NULL;
    }

    Device->ProductName = Device->DeviceInfo->ProductName;
    DEBUG(TEXT("Attached %s controller %x:%x on %x:%x.%x"),
          Device->DeviceInfo->ProductName,
          (UINT)Device->Info.VendorID,
          (UINT)Device->Info.DeviceID,
          (UINT)Device->Info.Bus,
          (UINT)Device->Info.Dev,
          (UINT)Device->Info.Func);
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Reset the controller and leave it in a quiet baseline state.
 * @param Device Target RTL8139 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139InitializeController(LPRTL8139_DEVICE Device) {
    U32 Result;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkResetController(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_CHIPCMD,
        RTL8139_CHIPCMD_RESET,
        TEXT("RTL8139InitializeController"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    RealtekNetworkInitializeQuietState(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_CHIPCMD,
        RTL8139_REG_INTRMASK,
        RTL8139_REG_INTRSTATUS);

    // Keep conservative defaults until RX/TX buffers exist.
    RTL8139ProgramBufferAddresses(Device);
    Device->RxReadOffset = 0;
    Device->TxNextSlot = 0;
    RTL8139WriteCurrentPacketRead(Device);
    RTL8139InitializeReceiveFilter(Device);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_RXCONFIG,
            RTL8139_RXCONFIG_ACCEPT_PHYSICAL |
            RTL8139_RXCONFIG_ACCEPT_BROADCAST |
            RTL8139_RXCONFIG_ACCEPT_MULTICAST |
            RTL8139_RXCONFIG_WRAP);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_CHIPCMD,
        RTL8139_CHIPCMD_RX_ENABLE | RTL8139_CHIPCMD_TX_ENABLE);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_TXCONFIG,
        RTL8139_TXCONFIG_IFG_NORMAL | RTL8139_TXCONFIG_DMA_1024);
    RTL8139ReadPermanentMac(Device);
    DEBUG(TEXT("Controller reset complete revision=%x"),
          Device->HardwareRevision);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Network-manager reset callback.
 * @param Reset Reset request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139OnReset(const NETWORK_RESET* Reset) {
    LPRTL8139_DEVICE Device;
    U32 Result;

    Result = RealtekNetworkOnReset(Reset);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device = (LPRTL8139_DEVICE)Reset->Device;
    return RTL8139InitializeController(Device);
}

/************************************************************************/

/**
 * @brief Read the permanent MAC address from controller registers.
 * @param Device Target RTL8139 device context.
 */
static void RTL8139ReadPermanentMac(LPRTL8139_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkReadMacFromRegisters(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_IDR0,
        RTL8139_REG_IDR4,
        Device->Mac);
}

/************************************************************************/

/**
 * @brief Query the current link state reported by the RTL8139 PHY registers.
 * @param Device Target RTL8139 device context.
 * @param LinkUp Output link state.
 * @param SpeedMbps Output speed in Mbps.
 * @param DuplexFull Output duplex state.
 */
static void RTL8139QueryLinkState(LPRTL8139_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull) {
    U16 BasicStatus;
    U16 LinkPartnerAbility;

    if (Device == NULL || LinkUp == NULL || SpeedMbps == NULL || DuplexFull == NULL) {
        return;
    }

    BasicStatus = RealtekNetworkReadRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_MII_BMSR);
    LinkPartnerAbility = RealtekNetworkReadRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_ANLPAR);

    *LinkUp = (BasicStatus & RTL8139_MII_BMSR_LINK_STATUS) != 0;
    *SpeedMbps = 0;
    *DuplexFull = FALSE;

    if (*LinkUp == FALSE) {
        return;
    }

    if ((LinkPartnerAbility & RTL8139_ANLPAR_100_FULL) != 0) {
        *SpeedMbps = 100;
        *DuplexFull = TRUE;
    } else if ((LinkPartnerAbility & RTL8139_ANLPAR_100_HALF) != 0) {
        *SpeedMbps = 100;
    } else if ((LinkPartnerAbility & RTL8139_ANLPAR_10_FULL) != 0) {
        *SpeedMbps = 10;
        *DuplexFull = TRUE;
    } else if ((LinkPartnerAbility & RTL8139_ANLPAR_10_HALF) != 0) {
        *SpeedMbps = 10;
    }
}

/************************************************************************/

/**
 * @brief Report MAC and link information for the RTL8139 controller.
 * @param GetInfo Information query and output buffer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139OnGetInfo(const NETWORK_GET_INFO* GetInfo) {
    LPRTL8139_DEVICE Device;
    BOOL LinkUp;
    U32 SpeedMbps;
    BOOL DuplexFull;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8139_DEVICE)GetInfo->Device;
    RTL8139ReadPermanentMac(Device);
    RTL8139QueryLinkState(Device, &LinkUp, &SpeedMbps, &DuplexFull);
    return RealtekNetworkOnGetInfo(GetInfo, LinkUp, SpeedMbps, DuplexFull, RTL8139_MAXIMUM_MTU);
}

/************************************************************************/

/**
 * @brief Initialize the active register BAR and cache the hardware revision.
 * @param Device Target RTL8139 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139InitializeRegisterAccess(LPRTL8139_DEVICE Device) {
    U32 Result;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkInitializeRegisterWindow(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        REALTEK_REGISTER_ACCESS_MODE_MMIO,
        REALTEK_REGISTER_ACCESS_MODE_IO,
        RTL8139_REG_TXCONFIG,
        TEXT("RTL8139InitializeRegisterAccess"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device->HardwareRevision = RealtekNetworkReadRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139_REG_TXCONFIG);
    DEBUG(TEXT("Revision=%x access=%u bar=%u"),
          Device->HardwareRevision,
          (UINT)Device->RegisterAccessMode,
          (UINT)Device->RegisterBarIndex);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Drain received packets from the RTL8139 software RX ring.
 * @param Device Target RTL8139 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139PollReceive(LPRTL8139_DEVICE Device) {
    UINT Guard;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    for (Guard = 0; Guard < 64; Guard++) {
        U8 ChipCommand;
        U16 CurrentBufferAddress;
        LPRTL8139_RX_PACKET_HEADER Header;
        U16 ReceiveStatus;
        U16 ReceiveLength;
        U32 FrameLength;
        UINT NextOffset;

        ChipCommand = RealtekNetworkReadRegister8((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_CHIPCMD);
        if ((ChipCommand & RTL8139_CHIPCMD_RX_BUFFER_EMPTY) != 0) {
            return DF_RETURN_SUCCESS;
        }

        CurrentBufferAddress = RealtekNetworkReadRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139_REG_CBR);
        if (Device->RxReadOffset == CurrentBufferAddress) {
            return DF_RETURN_SUCCESS;
        }

        Header = (LPRTL8139_RX_PACKET_HEADER)(LPVOID)(Device->RxBuffer.LinearBase + Device->RxReadOffset);
        ReceiveStatus = Header->ReceiveStatus;
        ReceiveLength = Header->ReceiveLength;

        if ((ReceiveStatus & RTL8139_RX_STATUS_OK) == 0 || ReceiveLength < 4 || ReceiveLength > RTL8139_RX_BUFFER_SIZE) {
            WARNING(TEXT("Dropping invalid RX packet status=%x length=%u"),
                    ReceiveStatus,
                    (UINT)ReceiveLength);
            Device->RxReadOffset = 0;
            RTL8139WriteCurrentPacketRead(Device);
            return DF_RETURN_INPUT_OUTPUT;
        }

        FrameLength = ReceiveLength - 4;
        RealtekNetworkDeliverReceivedFrame(
            (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
            (const U8*)((LPVOID)(Device->RxBuffer.LinearBase + Device->RxReadOffset + sizeof(RTL8139_RX_PACKET_HEADER))),
            FrameLength);

        NextOffset = (Device->RxReadOffset + sizeof(RTL8139_RX_PACKET_HEADER) + ReceiveLength + 3) & ~3;
        Device->RxReadOffset = NextOffset % RTL8139_RX_RING_SIZE;
        RTL8139WriteCurrentPacketRead(Device);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Transmit one Ethernet frame through an RTL8139 TX slot.
 * @param Send Send request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139OnSend(const NETWORK_SEND* Send) {
    LPRTL8139_DEVICE Device;
    UINT SlotIndex;
    LINEAR BufferLinear;
    U32 TransmitStatus;

    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Send->Length > RTL8139_TX_BUFFER_SIZE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8139_DEVICE)Send->Device;
    SlotIndex = Device->TxNextSlot % RTL8139_TX_SLOT_COUNT;
    TransmitStatus = RealtekNetworkReadRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139TxSlotInfoTable[SlotIndex].StatusRegisterOffset);
    if ((TransmitStatus & RTL8139_TXSTATUS_OWN) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    BufferLinear = Device->TxBufferPool.LinearBase + (SlotIndex * RTL8139_TX_BUFFER_SIZE);
    MemoryCopy((LPVOID)BufferLinear, Send->Data, Send->Length);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139TxSlotInfoTable[SlotIndex].StatusRegisterOffset,
        Send->Length);
    Device->TxNextSlot = (SlotIndex + 1) % RTL8139_TX_SLOT_COUNT;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared poll entry used by the Realtek common interrupt wrapper.
 * @param Device Common Realtek device pointer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139PollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8139PollReceive((LPRTL8139_DEVICE)Device);
}

/************************************************************************/

/**
 * @brief Poll the RTL8139 receive path.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139OnPoll(const NETWORK_POLL* Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8139PollDevice((LPREALTEK_NETWORK_COMMON_DEVICE)Poll->Device);
}

/************************************************************************/

/**
 * @brief Retrieves the encoded driver version.
 * @return Driver version encoded with MAKE_VERSION.
 */
static U32 RTL8139OnGetVersion(void) {
    return MAKE_VERSION(RTL8139_VERSION_MAJOR, RTL8139_VERSION_MINOR);
}

/************************************************************************/

/**
 * @brief Dispatches RTL8139 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8139Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RealtekNetworkOnLoad();
        case DF_UNLOAD:
            return RealtekNetworkOnUnload();
        case DF_GET_VERSION:
            return RTL8139OnGetVersion();
        case DF_GET_CAPS:
            return RealtekNetworkOnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RealtekNetworkOnGetLastFunction();
        case DF_PROBE:
            return RTL8139OnProbe((const PCI_INFO*)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RTL8139OnReset((const NETWORK_RESET*)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RTL8139OnGetInfo((const NETWORK_GET_INFO*)(LPVOID)Parameter);
        case DF_NT_SETRXCB:
            return RealtekNetworkOnSetReceiveCallback((const NETWORK_SET_RX_CB*)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RealtekNetworkOnEnableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RealtekNetworkOnDisableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RTL8139OnSend((const NETWORK_SEND*)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RTL8139OnPoll((const NETWORK_POLL*)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
