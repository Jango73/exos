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


    RTL8169

\************************************************************************/

#include "drivers/network/RTL8169.h"

#include "core/Kernel.h"
#include "log/Log.h"

/************************************************************************/

typedef struct tag_RTL8169_DEVICE RTL8169_DEVICE, *LPRTL8169_DEVICE;

/************************************************************************/

#define RTL8169_VERSION_MAJOR 1
#define RTL8169_VERSION_MINOR 0
#define RTL8169_LINK_SPEED_UNKNOWN 0

/************************************************************************/

struct tag_RTL8169_DEVICE {
    REALTEK_NETWORK_COMMON_DEVICE_FIELDS
    const RTL8169_DEVICE_INFO *DeviceInfo;
    U32 HardwareRevision;
    DMA_BUFFER RxRing;
    DMA_BUFFER TxRing;
    DMA_BUFFER RxBufferPool;
    DMA_BUFFER TxBufferPool;
    UINT RxDescriptorCount;
    UINT TxDescriptorCount;
    UINT RxNextDescriptor;
    UINT TxNextDescriptor;
};

/************************************************************************/

static UINT RTL8169Commands(UINT Function, UINT Parameter);
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice);
static const RTL8169_DEVICE_INFO *RTL8169FindDeviceInfo(U16 VendorID, U16 DeviceID);
static void RTL8169InitializeHardwareDescription(LPRTL8169_DEVICE Device);
static U32 RTL8169InitializeRegisterAccess(LPRTL8169_DEVICE Device);
static U32 RTL8169AllocateBuffers(LPRTL8169_DEVICE Device);
static U32 RTL8169InitializeDescriptorRings(LPRTL8169_DEVICE Device);
static void RTL8169InitializeReceiveFilter(LPRTL8169_DEVICE Device);
static U32 RTL8169GetReceiveDescriptorErrorMask(void);
static U32 RTL8169InitializeController(LPRTL8169_DEVICE Device);
static U32 RTL8169OnReset(const NETWORK_RESET *Reset);
static void RTL8169ReadPermanentMac(LPRTL8169_DEVICE Device);
static void RTL8169QueryLinkState(LPRTL8169_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull);
static U32 RTL8169OnGetInfo(const NETWORK_GET_INFO *GetInfo);
static U32 RTL8169PollReceive(LPRTL8169_DEVICE Device);
static U32 RTL8169PollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device);
static U32 RTL8169OnSend(const NETWORK_SEND* Send);
static U32 RTL8169OnPoll(const NETWORK_POLL* Poll);
static U32 RTL8169OnGetVersion(void);

/************************************************************************/

static DRIVER_MATCH RTL8169MatchTable[] = {
    REALTEK_NETWORK_MATCH_ENTRY(RTL8169_DEVICE_8161),
    REALTEK_NETWORK_MATCH_ENTRY(RTL8169_DEVICE_8168),
};

/************************************************************************/

static const RTL8169_DEVICE_INFO RTL8169DeviceInfoTable[] = {
    {
        .VendorID = RTL8169_VENDOR_REALTEK,
        .DeviceID = RTL8169_DEVICE_8161,
        .Family = RTL8169_DEVICE_FAMILY_8111,
        .QuirkFlags = RTL8169_QUIRK_PCIE_GIGABIT |
                      RTL8169_QUIRK_REVISION_BY_MAC_VERSION |
                      RTL8169_QUIRK_SHARED_8111_8168_REGISTERS,
        .ProductName = TEXT("RTL8111 Family"),
    },
    {
        .VendorID = RTL8169_VENDOR_REALTEK,
        .DeviceID = RTL8169_DEVICE_8168,
        .Family = RTL8169_DEVICE_FAMILY_8168,
        .QuirkFlags = RTL8169_QUIRK_PCIE_GIGABIT |
                      RTL8169_QUIRK_REVISION_BY_MAC_VERSION |
                      RTL8169_QUIRK_SHARED_8111_8168_REGISTERS |
                      RTL8169_QUIRK_SHARED_8411_REGISTERS,
        .ProductName = TEXT("RTL8168/8411 Family"),
    },
};

/************************************************************************/

PCI_DRIVER DATA_SECTION RTL8169Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = RTL8169_VERSION_MAJOR,
    .VersionMinor = RTL8169_VERSION_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Realtek",
    .Product = "RTL8111/8168/8411 Family",
    .Alias = "rtl8169",
    .Command = RTL8169Commands,
    .Matches = RTL8169MatchTable,
    .MatchCount = sizeof(RTL8169MatchTable) / sizeof(RTL8169MatchTable[0]),
    .Attach = RTL8169Attach,
};

/************************************************************************/

/**
 * @brief Retrieves the RTL8169 PCI driver descriptor.
 * @return Pointer to the RTL8169 PCI driver.
 */
LPDRIVER RTL8169GetDriver(void) {
    return (LPDRIVER)&RTL8169Driver;
}

/************************************************************************/

/**
 * @brief Handles PCI probe requests for the Realtek RTL8169 family.
 * @param PciInfo PCI function information provided by the PCI layer.
 * @return DF_RETURN_SUCCESS when the function matches the Ethernet family.
 */
static U32 RTL8169OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) {
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
static const RTL8169_DEVICE_INFO *RTL8169FindDeviceInfo(U16 VendorID, U16 DeviceID) {
    UINT Index;

    for (Index = 0; Index < sizeof(RTL8169DeviceInfoTable) / sizeof(RTL8169DeviceInfoTable[0]); Index++) {
        const RTL8169_DEVICE_INFO *DeviceInfo = &RTL8169DeviceInfoTable[Index];

        if (DeviceInfo->VendorID == VendorID && DeviceInfo->DeviceID == DeviceID) {
            return DeviceInfo;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Initializes the revision-independent hardware-description state.
 * @param Device Target RTL8169 device context.
 */
static void RTL8169InitializeHardwareDescription(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->DeviceInfo = RTL8169FindDeviceInfo(Device->Info.VendorID, Device->Info.DeviceID);
    Device->HardwareRevision = 0;
    Device->RxDescriptorCount = RTL8169_RX_DESCRIPTOR_COUNT;
    Device->TxDescriptorCount = RTL8169_TX_DESCRIPTOR_COUNT;
    Device->RxNextDescriptor = 0;
    Device->TxNextDescriptor = 0;
}

/************************************************************************/

/**
 * @brief Allocate descriptor rings and packet buffers for the RTL8169 family.
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169AllocateBuffers(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->RxRing.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->RxRing,
                Device->RxDescriptorCount * sizeof(RTL8169_RX_DESCRIPTOR),
                TRUE,
                TEXT("RTL8169RxRing"))) {
            ERROR(TEXT("RX ring allocation failed"));
            return DF_RETURN_NO_MEMORY;
        }
    }

    if (Device->TxRing.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->TxRing,
                Device->TxDescriptorCount * sizeof(RTL8169_TX_DESCRIPTOR),
                TRUE,
                TEXT("RTL8169TxRing"))) {
            ERROR(TEXT("TX ring allocation failed"));
            DMABufferRelease(&Device->RxRing);
            return DF_RETURN_NO_MEMORY;
        }
    }

    if (Device->RxBufferPool.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->RxBufferPool,
                Device->RxDescriptorCount * PAGE_SIZE,
                FALSE,
                TEXT("RTL8169RxBufferPool"))) {
            ERROR(TEXT("RX buffer pool allocation failed"));
            DMABufferRelease(&Device->TxRing);
            DMABufferRelease(&Device->RxRing);
            return DF_RETURN_NO_MEMORY;
        }
    }

    if (Device->TxBufferPool.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->TxBufferPool,
                Device->TxDescriptorCount * PAGE_SIZE,
                FALSE,
                TEXT("RTL8169TxBufferPool"))) {
            ERROR(TEXT("TX buffer pool allocation failed"));
            DMABufferRelease(&Device->RxBufferPool);
            DMABufferRelease(&Device->TxRing);
            DMABufferRelease(&Device->RxRing);
            return DF_RETURN_NO_MEMORY;
        }
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Initialize descriptor rings from the allocated DMA buffers.
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169InitializeDescriptorRings(LPRTL8169_DEVICE Device) {
    LPRTL8169_RX_DESCRIPTOR RxDescriptors;
    LPRTL8169_TX_DESCRIPTOR TxDescriptors;
    UINT Index;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    RxDescriptors = (LPRTL8169_RX_DESCRIPTOR)(LPVOID)Device->RxRing.LinearBase;
    TxDescriptors = (LPRTL8169_TX_DESCRIPTOR)(LPVOID)Device->TxRing.LinearBase;
    MemorySet(RxDescriptors, 0, Device->RxRing.AllocatedSize);
    MemorySet(TxDescriptors, 0, Device->TxRing.AllocatedSize);

    for (Index = 0; Index < Device->RxDescriptorCount; Index++) {
        PHYSICAL BufferPhysical = DMABufferGetPhysical(&Device->RxBufferPool, Index << PAGE_SIZE_MUL);
        U32 DescriptorFlags = RTL8169_DESCRIPTOR_OWN | RTL8169_RX_BUFFER_SIZE;

        if (BufferPhysical == 0) {
            ERROR(TEXT("RX buffer physical lookup failed at %u"), Index);
            return DF_RETURN_INPUT_OUTPUT;
        }

        if (Index + 1 == Device->RxDescriptorCount) {
            DescriptorFlags |= RTL8169_DESCRIPTOR_RING_END;
        }

        RxDescriptors[Index].CommandStatus = DescriptorFlags;
        RxDescriptors[Index].VLANInformation = 0;
        RxDescriptors[Index].BufferAddressLow = (U32)(BufferPhysical & MAX_U32);
        RxDescriptors[Index].BufferAddressHigh = 0;
    }

    for (Index = 0; Index < Device->TxDescriptorCount; Index++) {
        PHYSICAL BufferPhysical = DMABufferGetPhysical(&Device->TxBufferPool, Index << PAGE_SIZE_MUL);
        U32 DescriptorFlags = 0;

        if (BufferPhysical == 0) {
            ERROR(TEXT("TX buffer physical lookup failed at %u"), Index);
            return DF_RETURN_INPUT_OUTPUT;
        }

        if (Index + 1 == Device->TxDescriptorCount) {
            DescriptorFlags |= RTL8169_DESCRIPTOR_RING_END;
        }

        TxDescriptors[Index].CommandStatus = DescriptorFlags;
        TxDescriptors[Index].VLANInformation = 0;
        TxDescriptors[Index].BufferAddressLow = (U32)(BufferPhysical & MAX_U32);
        TxDescriptors[Index].BufferAddressHigh = 0;
    }

    Device->RxNextDescriptor = 0;
    Device->TxNextDescriptor = 0;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Program the default multicast filter state required by the RTL8169 family.
 * @param Device Target RTL8169 device context.
 */
static void RTL8169InitializeReceiveFilter(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkClearMulticastRegisters((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8169_REG_MAR0);
}

/************************************************************************/

/**
 * @brief Return the descriptor status bits that make one RX buffer invalid.
 * @return Combined RTL8169 RX descriptor error mask.
 */
static U32 RTL8169GetReceiveDescriptorErrorMask(void) {
    return RTL8169_DESCRIPTOR_BUFFER_OVERFLOW |
           RTL8169_DESCRIPTOR_FIFO_OVERFLOW |
           RTL8169_DESCRIPTOR_RECEIVE_WATCHDOG |
           RTL8169_DESCRIPTOR_RECEIVE_ERROR |
           RTL8169_DESCRIPTOR_RUNT |
           RTL8169_DESCRIPTOR_CRC_ERROR;
}

/************************************************************************/

/**
 * @brief Attaches the driver to a supported PCI function.
 *
 * Step 2 establishes the generic EXOS network-driver shape and returns an
 * attached PCI device object. Hardware MMIO probing and controller setup are
 * deferred to later steps.
 *
 * @param PciDevice Supported PCI function.
 * @return Attached PCI device on success, NULL on failure.
 */
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice) {
    LPRTL8169_DEVICE Device;
    U32 Result;

    Device = (LPRTL8169_DEVICE)RealtekNetworkAttachCommon(
        sizeof(RTL8169_DEVICE),
        PciDevice,
        TEXT("RTL8169Attach"));
    if (Device == NULL) {
        return NULL;
    }

    RTL8169InitializeHardwareDescription(Device);
    Device->PollRoutine = RTL8169PollDevice;
    RealtekNetworkConfigureInterruptSupport(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_INTRMASK,
        RTL8169_REG_INTRSTATUS,
        RTL8169_INTERRUPT_ENABLE_MASK,
        RTL8169_INTERRUPT_RELEVANT_MASK,
        0);

    if (Device->DeviceInfo == NULL) {
        ERROR(TEXT("Missing hardware description for %x:%x"),
              (UINT)Device->Info.VendorID,
              (UINT)Device->Info.DeviceID);
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8169InitializeRegisterAccess(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8169AllocateBuffers(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8169InitializeController(Device);
    if (Result != DF_RETURN_SUCCESS) {
        DMABufferRelease(&Device->TxBufferPool);
        DMABufferRelease(&Device->RxBufferPool);
        DMABufferRelease(&Device->TxRing);
        DMABufferRelease(&Device->RxRing);
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
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169InitializeController(LPRTL8169_DEVICE Device) {
    U32 Result;
    U32 RxConfig;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkResetController(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CHIPCMD,
        RTL8169_CHIPCMD_RESET,
        TEXT("RTL8169InitializeController"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    RealtekNetworkInitializeQuietState(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CHIPCMD,
        RTL8169_REG_INTRMASK,
        RTL8169_REG_INTRSTATUS);

    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CFG9346,
        RTL8169_CFG9346_UNLOCK);
    RealtekNetworkWriteRegister16(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CPLUSCMD,
        RTL8169_CPLUSCMD_DEFAULT);
    RTL8169InitializeReceiveFilter(Device);
    RealtekNetworkWriteRegister16(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_RXMAXSIZE,
        RTL8169_RX_BUFFER_SIZE);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_ETTHR,
        RTL8169_ETTHR_MAXIMUM_VALID);
    Result = RTL8169InitializeDescriptorRings(Device);
    if (Result != DF_RETURN_SUCCESS) {
        RealtekNetworkWriteRegister8(
            (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
            RTL8169_REG_CFG9346,
            RTL8169_CFG9346_LOCK);
        return Result;
    }
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXDESCADDRLOW,
        (U32)(Device->TxRing.PhysicalBase & MAX_U32));
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXDESCADDRHIGH,
        0);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_RXDESCADDRLOW,
        (U32)(Device->RxRing.PhysicalBase & MAX_U32));
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_RXDESCADDRHIGH,
        0);
    RxConfig = RTL8169_RXCONFIG_FIFO_UNLIMITED |
               RTL8169_RXCONFIG_DMA_UNLIMITED |
               RTL8169_RXCONFIG_ACCEPT_PHYSICAL |
               RTL8169_RXCONFIG_ACCEPT_BROADCAST |
               RTL8169_RXCONFIG_ACCEPT_MULTICAST;
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_RXCONFIG,
        RxConfig);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CFG9346,
        RTL8169_CFG9346_LOCK);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_CHIPCMD,
        RTL8169_CHIPCMD_RX_ENABLE | RTL8169_CHIPCMD_TX_ENABLE);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXCONFIG,
        RTL8169_TXCONFIG_IFG_NORMAL | RTL8169_TXCONFIG_DMA_1024);
    RTL8169ReadPermanentMac(Device);
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
static U32 RTL8169OnReset(const NETWORK_RESET *Reset) {
    LPRTL8169_DEVICE Device;
    U32 Result;

    Result = RealtekNetworkOnReset(Reset);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device = (LPRTL8169_DEVICE)Reset->Device;
    return RTL8169InitializeController(Device);
}

/************************************************************************/

/**
 * @brief Read the permanent MAC address from controller registers.
 * @param Device Target RTL8169 device context.
 */
static void RTL8169ReadPermanentMac(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkReadMacFromRegisters(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_MAC0,
        RTL8169_REG_MAC4,
        Device->Mac);
}

/************************************************************************/

/**
 * @brief Query the current link state from the Realtek PHY status register.
 * @param Device Target RTL8169 device context.
 * @param LinkUp Output link state.
 * @param SpeedMbps Output speed in Mbps.
 * @param DuplexFull Output duplex state.
 */
static void RTL8169QueryLinkState(LPRTL8169_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull) {
    U8 PhyStatus;

    if (Device == NULL || LinkUp == NULL || SpeedMbps == NULL || DuplexFull == NULL) {
        return;
    }

    PhyStatus = RealtekNetworkReadRegister8((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8169_REG_PHYSTATUS);
    *LinkUp = (PhyStatus & RTL8169_PHYSTATUS_LINK_UP) != 0;
    *SpeedMbps = 0;
    *DuplexFull = (PhyStatus & RTL8169_PHYSTATUS_FULL_DUPLEX) != 0;

    if ((PhyStatus & RTL8169_PHYSTATUS_1000_MBPS_FULL) != 0) {
        *SpeedMbps = 1000;
    } else if ((PhyStatus & RTL8169_PHYSTATUS_100_MBPS) != 0) {
        *SpeedMbps = 100;
    } else if ((PhyStatus & RTL8169_PHYSTATUS_10_MBPS) != 0) {
        *SpeedMbps = 10;
    }
}

/************************************************************************/

/**
 * @brief Report MAC and link information for the RTL8169 controller.
 * @param GetInfo Information query and output buffer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169OnGetInfo(const NETWORK_GET_INFO *GetInfo) {
    LPRTL8169_DEVICE Device;
    BOOL LinkUp;
    U32 SpeedMbps;
    BOOL DuplexFull;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8169_DEVICE)GetInfo->Device;
    RTL8169ReadPermanentMac(Device);
    RTL8169QueryLinkState(Device, &LinkUp, &SpeedMbps, &DuplexFull);
    return RealtekNetworkOnGetInfo(GetInfo, LinkUp, SpeedMbps, DuplexFull, RTL8169_MAXIMUM_MTU);
}

/************************************************************************/

/**
 * @brief Initialize the active register BAR and cache the hardware revision.
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169InitializeRegisterAccess(LPRTL8169_DEVICE Device) {
    U32 Result;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkInitializeRegisterWindow(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        REALTEK_REGISTER_ACCESS_MODE_MMIO,
        REALTEK_REGISTER_ACCESS_MODE_NONE,
        RTL8169_REG_TXCONFIG,
        TEXT("RTL8169InitializeRegisterAccess"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device->HardwareRevision = RealtekNetworkReadRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXCONFIG);
    DEBUG(TEXT("Revision=%x access=%u bar=%u"),
          Device->HardwareRevision,
          (UINT)Device->RegisterAccessMode,
          (UINT)Device->RegisterBarIndex);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Drain received packets from the RTL8169 RX descriptor ring.
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169PollReceive(LPRTL8169_DEVICE Device) {
    LPRTL8169_RX_DESCRIPTOR Descriptors;
    UINT Guard;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Descriptors = (LPRTL8169_RX_DESCRIPTOR)(LPVOID)Device->RxRing.LinearBase;
    for (Guard = 0; Guard < Device->RxDescriptorCount; Guard++) {
        LPRTL8169_RX_DESCRIPTOR Descriptor = &Descriptors[Device->RxNextDescriptor];
        U32 DescriptorStatus = Descriptor->CommandStatus;
        U32 DescriptorErrorMask = RTL8169GetReceiveDescriptorErrorMask();
        U32 FrameLength;
        U8* Frame;
        PHYSICAL BufferPhysical;
        U32 RearmFlags;

        if ((DescriptorStatus & RTL8169_DESCRIPTOR_OWN) != 0) {
            return DF_RETURN_SUCCESS;
        }

        FrameLength = DescriptorStatus & RTL8169_DESCRIPTOR_LENGTH_MASK;
        if ((DescriptorStatus & DescriptorErrorMask) != 0 ||
            (DescriptorStatus & RTL8169_DESCRIPTOR_FIRST_FRAGMENT) == 0 ||
            (DescriptorStatus & RTL8169_DESCRIPTOR_LAST_FRAGMENT) == 0 ||
            FrameLength <= 4 || FrameLength > RTL8169_RX_BUFFER_SIZE) {
            WARNING(TEXT("Dropping invalid RX descriptor status=%x length=%u"),
                    DescriptorStatus,
                    FrameLength);
        } else {
            Frame = (U8*)(LPVOID)(Device->RxBufferPool.LinearBase + (Device->RxNextDescriptor << PAGE_SIZE_MUL));
            RealtekNetworkDeliverReceivedFrame((LPREALTEK_NETWORK_COMMON_DEVICE)Device, Frame, FrameLength - 4);
        }

        BufferPhysical = DMABufferGetPhysical(&Device->RxBufferPool, Device->RxNextDescriptor << PAGE_SIZE_MUL);
        RearmFlags = RTL8169_DESCRIPTOR_OWN | RTL8169_RX_BUFFER_SIZE;
        if (Device->RxNextDescriptor + 1 == Device->RxDescriptorCount) {
            RearmFlags |= RTL8169_DESCRIPTOR_RING_END;
        }

        Descriptor->VLANInformation = 0;
        Descriptor->BufferAddressLow = (U32)(BufferPhysical & MAX_U32);
        Descriptor->BufferAddressHigh = 0;
        Descriptor->CommandStatus = RearmFlags;
        Device->RxNextDescriptor = (Device->RxNextDescriptor + 1) % Device->RxDescriptorCount;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Transmit one Ethernet frame using one RTL8169 TX descriptor.
 * @param Send Send request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169OnSend(const NETWORK_SEND* Send) {
    LPRTL8169_DEVICE Device;
    LPRTL8169_TX_DESCRIPTOR Descriptors;
    LPRTL8169_TX_DESCRIPTOR Descriptor;
    UINT DescriptorIndex;
    LINEAR BufferLinear;
    U32 DescriptorFlags;

    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Send->Length > RTL8169_TX_BUFFER_SIZE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8169_DEVICE)Send->Device;
    Descriptors = (LPRTL8169_TX_DESCRIPTOR)(LPVOID)Device->TxRing.LinearBase;
    DescriptorIndex = Device->TxNextDescriptor;
    Descriptor = &Descriptors[DescriptorIndex];
    if ((Descriptor->CommandStatus & RTL8169_DESCRIPTOR_OWN) != 0) {
        return DF_RETURN_UNEXPECTED;
    }

    BufferLinear = Device->TxBufferPool.LinearBase + (DescriptorIndex << PAGE_SIZE_MUL);
    MemoryCopy((LPVOID)BufferLinear, Send->Data, Send->Length);

    DescriptorFlags = RTL8169_DESCRIPTOR_OWN |
                      RTL8169_DESCRIPTOR_FIRST_FRAGMENT |
                      RTL8169_DESCRIPTOR_LAST_FRAGMENT |
                      (Send->Length & RTL8169_DESCRIPTOR_LENGTH_MASK);
    if (DescriptorIndex + 1 == Device->TxDescriptorCount) {
        DescriptorFlags |= RTL8169_DESCRIPTOR_RING_END;
    }

    Descriptor->CommandStatus = DescriptorFlags;
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXPOLL,
        RTL8169_TXPOLL_NORMAL_PRIORITY);
    Device->TxNextDescriptor = (DescriptorIndex + 1) % Device->TxDescriptorCount;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared poll entry used by the Realtek common interrupt wrapper.
 * @param Device Common Realtek device pointer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169PollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8169PollReceive((LPRTL8169_DEVICE)Device);
}

/************************************************************************/

/**
 * @brief Poll the RTL8169 receive ring.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169OnPoll(const NETWORK_POLL* Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8169PollDevice((LPREALTEK_NETWORK_COMMON_DEVICE)Poll->Device);
}

/************************************************************************/

/**
 * @brief Retrieves the encoded driver version.
 * @return Driver version encoded with MAKE_VERSION.
 */
static U32 RTL8169OnGetVersion(void) {
    return MAKE_VERSION(RTL8169_VERSION_MAJOR, RTL8169_VERSION_MINOR);
}

/************************************************************************/

/**
 * @brief Dispatches RTL8169 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8169Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RealtekNetworkOnLoad();
        case DF_UNLOAD:
            return RealtekNetworkOnUnload();
        case DF_GET_VERSION:
            return RTL8169OnGetVersion();
        case DF_GET_CAPS:
            return RealtekNetworkOnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RealtekNetworkOnGetLastFunction();
        case DF_PROBE:
            return RTL8169OnProbe((const PCI_INFO *)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RTL8169OnReset((const NETWORK_RESET *)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RTL8169OnGetInfo((const NETWORK_GET_INFO *)(LPVOID)Parameter);
        case DF_NT_SETRXCB:
            return RealtekNetworkOnSetReceiveCallback((const NETWORK_SET_RX_CB *)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RealtekNetworkOnEnableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RealtekNetworkOnDisableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RTL8169OnSend((const NETWORK_SEND *)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RTL8169OnPoll((const NETWORK_POLL *)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
