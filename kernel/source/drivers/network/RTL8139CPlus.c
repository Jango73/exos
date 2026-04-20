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


    RTL8139CPlus

\************************************************************************/

#include "drivers/network/RTL8139CPlus.h"

#include "core/Kernel.h"
#include "log/Log.h"

/************************************************************************/

typedef struct tag_RTL8139CPLUS_DEVICE RTL8139CPLUS_DEVICE, *LPRTL8139CPLUS_DEVICE;

/************************************************************************/

#define RTL8139CPLUS_VERSION_MAJOR 1
#define RTL8139CPLUS_VERSION_MINOR 0
#define RTL8139CPLUS_LINK_SPEED_UNKNOWN 0

/************************************************************************/

struct tag_RTL8139CPLUS_DEVICE {
    REALTEK_NETWORK_COMMON_DEVICE_FIELDS
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

static UINT RTL8139CPlusCommands(UINT Function, UINT Parameter);
static U32 RTL8139CPlusOnProbe(const PCI_INFO* PciInfo);
static LPPCI_DEVICE RTL8139CPlusAttach(LPPCI_DEVICE PciDevice);
static void RTL8139CPlusInitializeHardwareDescription(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusInitializeRegisterAccess(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusAllocateBuffers(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusInitializeDescriptorRings(LPRTL8139CPLUS_DEVICE Device);
static void RTL8139CPlusInitializeReceiveFilter(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusGetReceiveDescriptorErrorMask(void);
static U32 RTL8139CPlusInitializeController(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusOnReset(const NETWORK_RESET* Reset);
static void RTL8139CPlusReadPermanentMac(LPRTL8139CPLUS_DEVICE Device);
static void RTL8139CPlusQueryLinkState(LPRTL8139CPLUS_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull);
static U32 RTL8139CPlusOnGetInfo(const NETWORK_GET_INFO* GetInfo);
static U32 RTL8139CPlusPollReceive(LPRTL8139CPLUS_DEVICE Device);
static U32 RTL8139CPlusPollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device);
static U32 RTL8139CPlusOnSend(const NETWORK_SEND* Send);
static U32 RTL8139CPlusOnPoll(const NETWORK_POLL* Poll);
static U32 RTL8139CPlusOnGetVersion(void);

/************************************************************************/

static DRIVER_MATCH RTL8139CPlusMatchTable[] = {
    REALTEK_NETWORK_MATCH_ENTRY(RTL8139CPLUS_DEVICE_8139),
};

/************************************************************************/

PCI_DRIVER DATA_SECTION RTL8139CPlusDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = RTL8139CPLUS_VERSION_MAJOR,
    .VersionMinor = RTL8139CPLUS_VERSION_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Realtek",
    .Product = "RTL8139CPlus",
    .Alias = "rtl8139cplus",
    .Command = RTL8139CPlusCommands,
    .Matches = RTL8139CPlusMatchTable,
    .MatchCount = sizeof(RTL8139CPlusMatchTable) / sizeof(RTL8139CPlusMatchTable[0]),
    .Attach = RTL8139CPlusAttach,
};

/************************************************************************/

/**
 * @brief Retrieves the RTL8139CPlus PCI driver descriptor.
 * @return Pointer to the RTL8139CPlus PCI driver.
 */
LPDRIVER RTL8139CPlusGetDriver(void) {
    return (LPDRIVER)&RTL8139CPlusDriver;
}

/************************************************************************/

/**
 * @brief Handles PCI probe requests for the Realtek RTL8139CPlus family.
 * @param PciInfo PCI function information provided by the PCI layer.
 * @return DF_RETURN_SUCCESS when the revision matches the CPlus range.
 */
static U32 RTL8139CPlusOnProbe(const PCI_INFO* PciInfo) {
    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->Revision < RTL8139CPLUS_MINIMUM_REVISION) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Initializes revision-independent hardware-description state.
 * @param Device Target RTL8139CPlus device context.
 */
static void RTL8139CPlusInitializeHardwareDescription(LPRTL8139CPLUS_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->HardwareRevision = 0;
    Device->RxDescriptorCount = RTL8139CPLUS_RX_DESCRIPTOR_COUNT;
    Device->TxDescriptorCount = RTL8139CPLUS_TX_DESCRIPTOR_COUNT;
    Device->RxNextDescriptor = 0;
    Device->TxNextDescriptor = 0;
}

/************************************************************************/

/**
 * @brief Allocate descriptor rings and packet buffers for CPlus mode.
 * @param Device Target RTL8139CPlus device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusAllocateBuffers(LPRTL8139CPLUS_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->RxRing.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->RxRing,
                Device->RxDescriptorCount * sizeof(RTL8139CPLUS_DESCRIPTOR),
                TRUE,
                TEXT("RTL8139CPlusRxRing"))) {
            ERROR(TEXT("RX ring allocation failed"));
            return DF_RETURN_NO_MEMORY;
        }
    }

    if (Device->TxRing.LinearBase == 0) {
        if (!DMABufferAllocate(
                &Device->TxRing,
                Device->TxDescriptorCount * sizeof(RTL8139CPLUS_DESCRIPTOR),
                TRUE,
                TEXT("RTL8139CPlusTxRing"))) {
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
                TEXT("RTL8139CPlusRxBufferPool"))) {
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
                TEXT("RTL8139CPlusTxBufferPool"))) {
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
 * @brief Initialize CPlus RX and TX descriptor rings.
 * @param Device Target RTL8139CPlus device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusInitializeDescriptorRings(LPRTL8139CPLUS_DEVICE Device) {
    LPRTL8139CPLUS_DESCRIPTOR RxDescriptors;
    LPRTL8139CPLUS_DESCRIPTOR TxDescriptors;
    UINT Index;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    RxDescriptors = (LPRTL8139CPLUS_DESCRIPTOR)(LPVOID)Device->RxRing.LinearBase;
    TxDescriptors = (LPRTL8139CPLUS_DESCRIPTOR)(LPVOID)Device->TxRing.LinearBase;
    MemorySet(RxDescriptors, 0, Device->RxRing.AllocatedSize);
    MemorySet(TxDescriptors, 0, Device->TxRing.AllocatedSize);

    for (Index = 0; Index < Device->RxDescriptorCount; Index++) {
        PHYSICAL BufferPhysical;
        U32 DescriptorFlags;

        BufferPhysical = DMABufferGetPhysical(&Device->RxBufferPool, Index << PAGE_SIZE_MUL);
        if (BufferPhysical == 0) {
            ERROR(TEXT("RX buffer physical lookup failed at %u"), Index);
            return DF_RETURN_INPUT_OUTPUT;
        }

        DescriptorFlags = RTL8139CPLUS_DESCRIPTOR_OWN | RTL8139CPLUS_RX_BUFFER_SIZE;
        if (Index + 1 == Device->RxDescriptorCount) {
            DescriptorFlags |= RTL8139CPLUS_DESCRIPTOR_RING_END;
        }

        RxDescriptors[Index].CommandStatus = DescriptorFlags;
        RxDescriptors[Index].VLANInformation = 0;
        RxDescriptors[Index].BufferAddressLow = (U32)(BufferPhysical & MAX_U32);
        RxDescriptors[Index].BufferAddressHigh = 0;
    }

    for (Index = 0; Index < Device->TxDescriptorCount; Index++) {
        PHYSICAL BufferPhysical;
        U32 DescriptorFlags;

        BufferPhysical = DMABufferGetPhysical(&Device->TxBufferPool, Index << PAGE_SIZE_MUL);
        if (BufferPhysical == 0) {
            ERROR(TEXT("TX buffer physical lookup failed at %u"), Index);
            return DF_RETURN_INPUT_OUTPUT;
        }

        DescriptorFlags = 0;
        if (Index + 1 == Device->TxDescriptorCount) {
            DescriptorFlags |= RTL8139CPLUS_DESCRIPTOR_RING_END;
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
 * @brief Program the default multicast filter state.
 * @param Device Target RTL8139CPlus device context.
 */
static void RTL8139CPlusInitializeReceiveFilter(LPRTL8139CPLUS_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkClearMulticastRegisters((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_MAR0);
}

/************************************************************************/

/**
 * @brief Returns the descriptor status bits that make one RX frame invalid.
 * @return Combined RTL8139CPlus RX descriptor error mask.
 */
static U32 RTL8139CPlusGetReceiveDescriptorErrorMask(void) {
    return RTL8139CPLUS_DESCRIPTOR_RX_ERROR |
           RTL8139CPLUS_DESCRIPTOR_IP_CHECKSUM_FAIL |
           RTL8139CPLUS_DESCRIPTOR_UDP_CHECKSUM_FAIL |
           RTL8139CPLUS_DESCRIPTOR_TCP_CHECKSUM_FAIL;
}

/************************************************************************/

/**
 * @brief Attaches the driver to a supported PCI function.
 * @param PciDevice Supported PCI function.
 * @return Attached PCI device on success, NULL on failure.
 */
static LPPCI_DEVICE RTL8139CPlusAttach(LPPCI_DEVICE PciDevice) {
    LPRTL8139CPLUS_DEVICE Device;
    U32 Result;

    Device = (LPRTL8139CPLUS_DEVICE)RealtekNetworkAttachCommon(
        sizeof(RTL8139CPLUS_DEVICE),
        PciDevice,
        TEXT("RTL8139CPlusAttach"));
    if (Device == NULL) {
        return NULL;
    }

    RTL8139CPlusInitializeHardwareDescription(Device);
    Device->PollRoutine = RTL8139CPlusPollDevice;
    RealtekNetworkConfigureInterruptSupport(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_INTRMASK,
        RTL8139CPLUS_REG_INTRSTATUS,
        RTL8139CPLUS_INTERRUPT_ENABLE_MASK,
        RTL8139CPLUS_INTERRUPT_RELEVANT_MASK,
        0);

    Result = RTL8139CPlusInitializeRegisterAccess(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8139CPlusAllocateBuffers(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8139CPlusInitializeController(Device);
    if (Result != DF_RETURN_SUCCESS) {
        DMABufferRelease(&Device->TxBufferPool);
        DMABufferRelease(&Device->RxBufferPool);
        DMABufferRelease(&Device->TxRing);
        DMABufferRelease(&Device->RxRing);
        ReleaseKernelObject(Device);
        return NULL;
    }

    Device->ProductName = TEXT("RTL8139CPlus");
    DEBUG(TEXT("Attached RTL8139CPlus controller %x:%x on %x:%x.%x revision=%x"),
          (UINT)Device->Info.VendorID,
          (UINT)Device->Info.DeviceID,
          (UINT)Device->Info.Bus,
          (UINT)Device->Info.Dev,
          (UINT)Device->Info.Func,
          (UINT)Device->Info.Revision);
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Reset the controller and leave it in a quiet CPlus baseline state.
 * @param Device Target RTL8139CPlus device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusInitializeController(LPRTL8139CPLUS_DEVICE Device) {
    U32 Result;
    U32 RxConfig;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkResetController(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CHIPCMD,
        RTL8139CPLUS_CHIPCMD_RESET,
        TEXT("RTL8139CPlusInitializeController"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    RealtekNetworkInitializeQuietState(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CHIPCMD,
        RTL8139CPLUS_REG_INTRMASK,
        RTL8139CPLUS_REG_INTRSTATUS);

    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CFG9346,
        RTL8139CPLUS_CFG9346_UNLOCK);

    RealtekNetworkWriteRegister16(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CPLUSCMD,
        RTL8139CPLUS_CPLUSCMD_DEFAULT);
    RealtekNetworkWriteRegister32((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_HITXRINGADDRLOW, 0);
    RealtekNetworkWriteRegister32((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_HITXRINGADDRHIGH, 0);

    Result = RTL8139CPlusInitializeDescriptorRings(Device);
    if (Result != DF_RETURN_SUCCESS) {
        RealtekNetworkWriteRegister8(
            (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
            RTL8139CPLUS_REG_CFG9346,
            RTL8139CPLUS_CFG9346_LOCK);
        return Result;
    }

    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_RXRINGADDRLOW,
        (U32)(Device->RxRing.PhysicalBase & MAX_U32));
    RealtekNetworkWriteRegister32((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_RXRINGADDRHIGH, 0);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_TXRINGADDRLOW,
        (U32)(Device->TxRing.PhysicalBase & MAX_U32));
    RealtekNetworkWriteRegister32((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_TXRINGADDRHIGH, 0);

    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CHIPCMD,
        RTL8139CPLUS_CHIPCMD_RX_ENABLE | RTL8139CPLUS_CHIPCMD_TX_ENABLE);

    RTL8139CPlusInitializeReceiveFilter(Device);
    RxConfig = RTL8139CPLUS_RXCONFIG_FIFO_256 |
               RTL8139CPLUS_RXCONFIG_DMA_256 |
               RTL8139CPLUS_RXCONFIG_ACCEPT_PHYSICAL |
               RTL8139CPLUS_RXCONFIG_ACCEPT_BROADCAST |
               RTL8139CPLUS_RXCONFIG_ACCEPT_MULTICAST;
    RealtekNetworkWriteRegister32((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_RXCONFIG, RxConfig);
    RealtekNetworkWriteRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_TXCONFIG,
        RTL8139CPLUS_TXCONFIG_IFG_NORMAL | RTL8139CPLUS_TXCONFIG_DMA_1024);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_TXTHRESHOLD,
        RTL8139CPLUS_TXTHRESHOLD_DEFAULT);
    RealtekNetworkWriteRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_MULTIINTR, 0);
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_CFG9346,
        RTL8139CPLUS_CFG9346_LOCK);

    RTL8139CPlusReadPermanentMac(Device);
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
static U32 RTL8139CPlusOnReset(const NETWORK_RESET* Reset) {
    LPRTL8139CPLUS_DEVICE Device;
    U32 Result;

    Result = RealtekNetworkOnReset(Reset);
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device = (LPRTL8139CPLUS_DEVICE)Reset->Device;
    return RTL8139CPlusInitializeController(Device);
}

/************************************************************************/

/**
 * @brief Read the permanent MAC address from controller registers.
 * @param Device Target RTL8139CPlus device context.
 */
static void RTL8139CPlusReadPermanentMac(LPRTL8139CPLUS_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkReadMacFromRegisters(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_IDR0,
        RTL8139CPLUS_REG_IDR4,
        Device->Mac);
}

/************************************************************************/

/**
 * @brief Query the current link state from the PHY registers.
 * @param Device Target RTL8139CPlus device context.
 * @param LinkUp Output link state.
 * @param SpeedMbps Output speed in Mbps.
 * @param DuplexFull Output duplex state.
 */
static void RTL8139CPlusQueryLinkState(LPRTL8139CPLUS_DEVICE Device, BOOL* LinkUp, U32* SpeedMbps, BOOL* DuplexFull) {
    U16 BasicStatus;
    U16 LinkPartnerAbility;

    if (Device == NULL || LinkUp == NULL || SpeedMbps == NULL || DuplexFull == NULL) {
        return;
    }

    BasicStatus = RealtekNetworkReadRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_MII_BMSR);
    LinkPartnerAbility = RealtekNetworkReadRegister16((LPREALTEK_NETWORK_COMMON_DEVICE)Device, RTL8139CPLUS_REG_ANLPAR);

    *LinkUp = (BasicStatus & RTL8139CPLUS_MII_BMSR_LINK_STATUS) != 0;
    *SpeedMbps = RTL8139CPLUS_LINK_SPEED_UNKNOWN;
    *DuplexFull = FALSE;

    if (*LinkUp == FALSE) {
        return;
    }

    if ((LinkPartnerAbility & RTL8139CPLUS_ANLPAR_100_FULL) != 0) {
        *SpeedMbps = 100;
        *DuplexFull = TRUE;
    } else if ((LinkPartnerAbility & RTL8139CPLUS_ANLPAR_100_HALF) != 0) {
        *SpeedMbps = 100;
    } else if ((LinkPartnerAbility & RTL8139CPLUS_ANLPAR_10_FULL) != 0) {
        *SpeedMbps = 10;
        *DuplexFull = TRUE;
    } else if ((LinkPartnerAbility & RTL8139CPLUS_ANLPAR_10_HALF) != 0) {
        *SpeedMbps = 10;
    }
}

/************************************************************************/

/**
 * @brief Report MAC and link information for the RTL8139CPlus controller.
 * @param GetInfo Information query and output buffer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusOnGetInfo(const NETWORK_GET_INFO* GetInfo) {
    LPRTL8139CPLUS_DEVICE Device;
    BOOL LinkUp;
    U32 SpeedMbps;
    BOOL DuplexFull;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8139CPLUS_DEVICE)GetInfo->Device;
    RTL8139CPlusReadPermanentMac(Device);
    RTL8139CPlusQueryLinkState(Device, &LinkUp, &SpeedMbps, &DuplexFull);
    return RealtekNetworkOnGetInfo(GetInfo, LinkUp, SpeedMbps, DuplexFull, RTL8139CPLUS_MAXIMUM_MTU);
}

/************************************************************************/

/**
 * @brief Initialize the active register BAR and cache the hardware revision.
 * @param Device Target RTL8139CPlus device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusInitializeRegisterAccess(LPRTL8139CPLUS_DEVICE Device) {
    U32 Result;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkInitializeRegisterWindow(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        REALTEK_REGISTER_ACCESS_MODE_MMIO,
        REALTEK_REGISTER_ACCESS_MODE_IO,
        RTL8139CPLUS_REG_TXCONFIG,
        TEXT("RTL8139CPlusInitializeRegisterAccess"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device->HardwareRevision = RealtekNetworkReadRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_TXCONFIG);
    DEBUG(TEXT("Revision=%x access=%u bar=%u"),
          Device->HardwareRevision,
          (UINT)Device->RegisterAccessMode,
          (UINT)Device->RegisterBarIndex);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Drain received packets from the RTL8139CPlus RX descriptor ring.
 * @param Device Target RTL8139CPlus device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusPollReceive(LPRTL8139CPLUS_DEVICE Device) {
    LPRTL8139CPLUS_DESCRIPTOR Descriptors;
    UINT Guard;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Descriptors = (LPRTL8139CPLUS_DESCRIPTOR)(LPVOID)Device->RxRing.LinearBase;
    for (Guard = 0; Guard < Device->RxDescriptorCount; Guard++) {
        LPRTL8139CPLUS_DESCRIPTOR Descriptor;
        U32 DescriptorStatus;
        U32 DescriptorErrorMask;
        U32 FrameLength;
        U8* Frame;
        PHYSICAL BufferPhysical;
        U32 RearmFlags;

        Descriptor = &Descriptors[Device->RxNextDescriptor];
        DescriptorStatus = Descriptor->CommandStatus;
        if ((DescriptorStatus & RTL8139CPLUS_DESCRIPTOR_OWN) != 0) {
            return DF_RETURN_SUCCESS;
        }

        DescriptorErrorMask = RTL8139CPlusGetReceiveDescriptorErrorMask();
        FrameLength = DescriptorStatus & RTL8139CPLUS_DESCRIPTOR_LENGTH_MASK;
        if ((DescriptorStatus & DescriptorErrorMask) != 0 ||
            (DescriptorStatus & RTL8139CPLUS_DESCRIPTOR_FIRST_FRAGMENT) == 0 ||
            (DescriptorStatus & RTL8139CPLUS_DESCRIPTOR_LAST_FRAGMENT) == 0 ||
            FrameLength <= 4 || FrameLength > RTL8139CPLUS_RX_BUFFER_SIZE) {
            WARNING(TEXT("Dropping invalid RX descriptor status=%x length=%u"),
                    DescriptorStatus,
                    FrameLength);
        } else {
            Frame = (U8*)(LPVOID)(Device->RxBufferPool.LinearBase + (Device->RxNextDescriptor << PAGE_SIZE_MUL));
            RealtekNetworkDeliverReceivedFrame((LPREALTEK_NETWORK_COMMON_DEVICE)Device, Frame, FrameLength - 4);
        }

        BufferPhysical = DMABufferGetPhysical(&Device->RxBufferPool, Device->RxNextDescriptor << PAGE_SIZE_MUL);
        RearmFlags = RTL8139CPLUS_DESCRIPTOR_OWN | RTL8139CPLUS_RX_BUFFER_SIZE;
        if (Device->RxNextDescriptor + 1 == Device->RxDescriptorCount) {
            RearmFlags |= RTL8139CPLUS_DESCRIPTOR_RING_END;
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
 * @brief Transmit one Ethernet frame using one RTL8139CPlus TX descriptor.
 * @param Send Send request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusOnSend(const NETWORK_SEND* Send) {
    LPRTL8139CPLUS_DEVICE Device;
    LPRTL8139CPLUS_DESCRIPTOR Descriptors;
    LPRTL8139CPLUS_DESCRIPTOR Descriptor;
    UINT DescriptorIndex;
    LINEAR BufferLinear;
    U32 DescriptorFlags;

    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Send->Length > RTL8139CPLUS_TX_BUFFER_SIZE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8139CPLUS_DEVICE)Send->Device;
    Descriptors = (LPRTL8139CPLUS_DESCRIPTOR)(LPVOID)Device->TxRing.LinearBase;
    DescriptorIndex = Device->TxNextDescriptor;
    Descriptor = &Descriptors[DescriptorIndex];
    if ((Descriptor->CommandStatus & RTL8139CPLUS_DESCRIPTOR_OWN) != 0) {
        return DF_RETURN_UNEXPECTED;
    }

    BufferLinear = Device->TxBufferPool.LinearBase + (DescriptorIndex << PAGE_SIZE_MUL);
    MemoryCopy((LPVOID)BufferLinear, Send->Data, Send->Length);

    DescriptorFlags = RTL8139CPLUS_DESCRIPTOR_OWN |
                      RTL8139CPLUS_DESCRIPTOR_FIRST_FRAGMENT |
                      RTL8139CPLUS_DESCRIPTOR_LAST_FRAGMENT |
                      (Send->Length & RTL8139CPLUS_DESCRIPTOR_LENGTH_MASK);
    if (DescriptorIndex + 1 == Device->TxDescriptorCount) {
        DescriptorFlags |= RTL8139CPLUS_DESCRIPTOR_RING_END;
    }

    Descriptor->VLANInformation = 0;
    Descriptor->CommandStatus = DescriptorFlags;
    RealtekNetworkWriteRegister8(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8139CPLUS_REG_TXPOLL,
        RTL8139CPLUS_TXPOLL_NORMAL_PRIORITY);
    Device->TxNextDescriptor = (DescriptorIndex + 1) % Device->TxDescriptorCount;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared poll entry used by the Realtek common interrupt wrapper.
 * @param Device Common Realtek device pointer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusPollDevice(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8139CPlusPollReceive((LPRTL8139CPLUS_DEVICE)Device);
}

/************************************************************************/

/**
 * @brief Poll the RTL8139CPlus RX descriptor ring.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8139CPlusOnPoll(const NETWORK_POLL* Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return RTL8139CPlusPollDevice((LPREALTEK_NETWORK_COMMON_DEVICE)Poll->Device);
}

/************************************************************************/

/**
 * @brief Returns the RTL8139CPlus driver version.
 * @return Packed major/minor version.
 */
static U32 RTL8139CPlusOnGetVersion(void) {
    return MAKE_VERSION(RTL8139CPLUS_VERSION_MAJOR, RTL8139CPLUS_VERSION_MINOR);
}

/************************************************************************/

/**
 * @brief Dispatches RTL8139CPlus driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8139CPlusCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RealtekNetworkOnLoad();
        case DF_UNLOAD:
            return RealtekNetworkOnUnload();
        case DF_GET_VERSION:
            return RTL8139CPlusOnGetVersion();
        case DF_GET_CAPS:
            return RealtekNetworkOnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RealtekNetworkOnGetLastFunction();
        case DF_PROBE:
            return RTL8139CPlusOnProbe((const PCI_INFO*)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RTL8139CPlusOnReset((const NETWORK_RESET*)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RTL8139CPlusOnGetInfo((const NETWORK_GET_INFO*)(LPVOID)Parameter);
        case DF_NT_SETRXCB:
            return RealtekNetworkOnSetReceiveCallback((const NETWORK_SET_RX_CB*)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RealtekNetworkOnEnableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RealtekNetworkOnDisableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RTL8139CPlusOnSend((const NETWORK_SEND*)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RTL8139CPlusOnPoll((const NETWORK_POLL*)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
