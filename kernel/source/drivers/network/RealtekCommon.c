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


    Realtek network common helpers

\************************************************************************/

#include "drivers/network/RealtekCommon.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "network/NetworkManager.h"
#include "sync/Deferred-Work.h"
#include "system/Clock.h"
#include "system/System.h"
#include "text/CoreString.h"

/************************************************************************/

static BOOL RealtekNetworkConfigurePCICommand(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName);
static BOOL RealtekNetworkSelectRegisterWindowForMode(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName);
static BOOL RealtekNetworkFinalizeRegisterWindow(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U8 BarIndex, PHYSICAL RegisterBase, UINT RegisterSize,
    REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName);
static void RealtekNetworkClearRegisterWindow(LPREALTEK_NETWORK_COMMON_DEVICE Device);
static BOOL RealtekNetworkInterruptTopHalf(LPDEVICE Device, LPVOID Context);
static void RealtekNetworkDeferredRoutine(LPDEVICE Device, LPVOID Context);
static void RealtekNetworkPollRoutine(LPDEVICE Device, LPVOID Context);
static void RealtekNetworkRearmInterrupts(LPREALTEK_NETWORK_COMMON_DEVICE Device);

/************************************************************************/

/**
 * @brief Reset the cached register-window description.
 * @param Device Target common device state.
 */
static void RealtekNetworkClearRegisterWindow(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->RegisterBarIndex = 0xFF;
    Device->RegisterAccessMode = REALTEK_REGISTER_ACCESS_MODE_NONE;
    Device->PCICommand = 0;
    Device->RegisterBase = 0;
    Device->RegisterSize = 0;
    Device->RegisterLinear = 0;
    Device->RegisterPort = 0;
}

/************************************************************************/

/**
 * @brief Re-arm hardware interrupts when the slot is operating in IRQ mode.
 * @param Device Target common device state.
 */
static void RealtekNetworkRearmInterrupts(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL || Device->InterruptArmed == FALSE || Device->InterruptMaskRegisterOffset == 0 ||
        DeferredWorkIsPollingMode()) {
        return;
    }

    RealtekNetworkWriteRegister16(Device, Device->InterruptMaskRegisterOffset, Device->InterruptEnableMask);
}

/************************************************************************/

/**
 * @brief Shared top half for Realtek INTx interrupts.
 * @param Device Device pointer supplied by DeviceInterrupt.
 * @param Context Common Realtek device context.
 * @return TRUE when deferred work should be scheduled, FALSE otherwise.
 */
static BOOL RealtekNetworkInterruptTopHalf(LPDEVICE Device, LPVOID Context) {
    LPREALTEK_NETWORK_COMMON_DEVICE CommonDevice;
    U16 AcknowledgeStatus;
    U16 InterruptStatus;

    UNUSED(Device);

    CommonDevice = (LPREALTEK_NETWORK_COMMON_DEVICE)Context;
    SAFE_USE_VALID_ID(CommonDevice, KOID_PCIDEVICE) {
        InterruptStatus = RealtekNetworkReadRegister16(CommonDevice, CommonDevice->InterruptStatusRegisterOffset);
        if (InterruptStatus == 0 || InterruptStatus == MAX_U16) {
            return FALSE;
        }

        RealtekNetworkWriteRegister16(CommonDevice, CommonDevice->InterruptMaskRegisterOffset, 0);
        AcknowledgeStatus = InterruptStatus & (U16)~CommonDevice->InterruptAcknowledgeAfterPollMask;
        if (AcknowledgeStatus != 0) {
            RealtekNetworkWriteRegister16(CommonDevice, CommonDevice->InterruptStatusRegisterOffset, AcknowledgeStatus);
        }

        CommonDevice->PendingInterruptStatus |= InterruptStatus & CommonDevice->InterruptAcknowledgeAfterPollMask;
        if ((InterruptStatus & CommonDevice->InterruptRelevantMask) == 0) {
            if (CommonDevice->PendingInterruptStatus != 0) {
                RealtekNetworkWriteRegister16(
                    CommonDevice, CommonDevice->InterruptStatusRegisterOffset, CommonDevice->PendingInterruptStatus);
                CommonDevice->PendingInterruptStatus = 0;
            }
            RealtekNetworkRearmInterrupts(CommonDevice);
            return FALSE;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Shared deferred handler used by the Realtek polling-only slot.
 * @param Device Device pointer supplied by DeviceInterrupt.
 * @param Context Common Realtek device context.
 */
static void RealtekNetworkDeferredRoutine(LPDEVICE Device, LPVOID Context) {
    RealtekNetworkPollRoutine(Device, Context);
}

/************************************************************************/

/**
 * @brief Shared polling hook for Realtek family drivers.
 * @param Device Device pointer supplied by DeviceInterrupt.
 * @param Context Common Realtek device context.
 */
static void RealtekNetworkPollRoutine(LPDEVICE Device, LPVOID Context) {
    LPREALTEK_NETWORK_COMMON_DEVICE CommonDevice;
    LPNETWORK_DEVICE_CONTEXT NetworkContext;

    UNUSED(Device);

    CommonDevice = (LPREALTEK_NETWORK_COMMON_DEVICE)Context;
    SAFE_USE_VALID_ID(CommonDevice, KOID_PCIDEVICE) {
        if (CommonDevice->PollRoutine != NULL) {
            CommonDevice->PollRoutine(CommonDevice);
        }

        NetworkContext = (LPNETWORK_DEVICE_CONTEXT)CommonDevice->RxUserData;
        SAFE_USE_VALID_ID(NetworkContext, KOID_NETWORKDEVICE) { NetworkManager_MaintenanceTick(NetworkContext); }

        if (CommonDevice->PendingInterruptStatus != 0) {
            RealtekNetworkWriteRegister16(
                CommonDevice, CommonDevice->InterruptStatusRegisterOffset, CommonDevice->PendingInterruptStatus);
            CommonDevice->PendingInterruptStatus = 0;
        }

        RealtekNetworkRearmInterrupts(CommonDevice);
    }
}

/************************************************************************/

/**
 * @brief Update the PCI command register for the selected access mode.
 * @param Device Target common device state.
 * @param AccessMode Selected access mode.
 * @param FunctionName Caller function name for diagnostics.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL RealtekNetworkConfigurePCICommand(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName) {
    U16 Command;

    if (Device == NULL || StringEmpty(FunctionName)) {
        return FALSE;
    }

    Command = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    Command |= PCI_CMD_BUSMASTER;
    if (AccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        Command |= PCI_CMD_IO;
    } else if (AccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO) {
        Command |= PCI_CMD_MEM;
    } else {
        ERROR(TEXT("Unsupported register access mode"));
        return FALSE;
    }

    PCI_Write16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND, Command);
    Device->PCICommand = Command;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Cache the selected BAR and map it when MMIO is required.
 * @param Device Target common device state.
 * @param BarIndex Active BAR index.
 * @param RegisterBase Decoded BAR base address.
 * @param RegisterSize BAR size in bytes.
 * @param AccessMode Selected access mode.
 * @param FunctionName Caller function name for diagnostics.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL RealtekNetworkFinalizeRegisterWindow(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U8 BarIndex, PHYSICAL RegisterBase, UINT RegisterSize,
    REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName) {
    if (Device == NULL || RegisterBase == 0 || RegisterSize == 0 || StringEmpty(FunctionName)) {
        return FALSE;
    }

    Device->RegisterBarIndex = BarIndex;
    Device->RegisterAccessMode = (U8)AccessMode;
    Device->RegisterBase = RegisterBase;
    Device->RegisterSize = RegisterSize;

    if (AccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        Device->RegisterPort = (U32)RegisterBase;
        DEBUG(
            TEXT("Selected IO BAR%u base=%x size=%u cmd=%x"), (UINT)BarIndex, (UINT)RegisterBase, RegisterSize,
            (UINT)Device->PCICommand);
        return TRUE;
    }

    Device->RegisterLinear = MapIOMemory(RegisterBase, RegisterSize);
    if (Device->RegisterLinear == 0) {
        ERROR(TEXT("MapIOMemory failed base=%p size=%u"), (LPVOID)(LINEAR)RegisterBase, RegisterSize);
        RealtekNetworkClearRegisterWindow(Device);
        return FALSE;
    }

    Device->BARMapped[BarIndex] = (LPVOID)Device->RegisterLinear;
    DEBUG(
        TEXT("Selected MMIO BAR%u base=%p size=%u linear=%p cmd=%x"), (UINT)BarIndex, (LPVOID)(LINEAR)RegisterBase,
        RegisterSize, (LPVOID)Device->RegisterLinear, (UINT)Device->PCICommand);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Find and initialize a BAR matching the requested access mode.
 * @param Device Target common device state.
 * @param AccessMode Desired access mode.
 * @param FunctionName Caller function name for diagnostics.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL RealtekNetworkSelectRegisterWindowForMode(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, REALTEK_REGISTER_ACCESS_MODE AccessMode, LPCSTR FunctionName) {
    UINT BarIndex;

    if (Device == NULL || StringEmpty(FunctionName)) {
        return FALSE;
    }

    if (!RealtekNetworkConfigurePCICommand(Device, AccessMode, FunctionName)) {
        return FALSE;
    }

    for (BarIndex = 0; BarIndex < 6; BarIndex++) {
        U32 RawBar = Device->Info.BAR[BarIndex];
        PHYSICAL RegisterBase;
        U32 RegisterSize;
        BOOL IsModeMatch;

        if (RawBar == 0) {
            continue;
        }

        IsModeMatch = FALSE;
        if (AccessMode == REALTEK_REGISTER_ACCESS_MODE_IO && PCI_BAR_IS_IO(RawBar)) {
            IsModeMatch = TRUE;
        } else if (AccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && PCI_BAR_IS_MEM(RawBar)) {
            IsModeMatch = TRUE;
        }

        if (IsModeMatch == FALSE) {
            continue;
        }

        RegisterBase = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, (U8)BarIndex);
        RegisterSize = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, (U8)BarIndex);
        if (RegisterBase == 0 || RegisterSize == 0) {
            continue;
        }

        if (RealtekNetworkFinalizeRegisterWindow(
                Device, (U8)BarIndex, RegisterBase, RegisterSize, AccessMode, FunctionName)) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Creates and initializes the common EXOS-facing Realtek PCI device state.
 * @param DeviceSize Final driver device object size.
 * @param PciDevice Source PCI function descriptor.
 * @param FunctionName Caller function name for diagnostics.
 * @return Heap-allocated PCI device object or NULL on failure.
 */
LPPCI_DEVICE RealtekNetworkAttachCommon(UINT DeviceSize, LPPCI_DEVICE PciDevice, LPCSTR FunctionName) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (DeviceSize < sizeof(REALTEK_NETWORK_COMMON_DEVICE) || PciDevice == NULL || StringEmpty(FunctionName)) {
        return NULL;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)CreateKernelObject(DeviceSize, KOID_PCIDEVICE);
    if (Device == NULL) {
        ERROR(TEXT("Failed to allocate device object"));
        return NULL;
    }

    Device->Driver = PciDevice->Driver;
    Device->Info = PciDevice->Info;
    MemoryCopy(Device->BARPhys, PciDevice->BARPhys, sizeof(Device->BARPhys));
    MemoryCopy((LPVOID)Device->BARMapped, (LPVOID)PciDevice->BARMapped, sizeof(Device->BARMapped));
    MemoryCopy(Device->Name, PciDevice->Name, sizeof(Device->Name));
    InitMutex(&(Device->Mutex));
    RealtekNetworkClearRegisterWindow(Device);
    Device->ProductName = TEXT("Realtek Network Family");
    RealtekNetworkBuildPlaceholderMac(Device);
    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->InterruptRegistered = FALSE;
    Device->InterruptArmed = FALSE;
    Device->InterruptMaskRegisterOffset = 0;
    Device->InterruptStatusRegisterOffset = 0;
    Device->InterruptEnableMask = 0;
    Device->InterruptRelevantMask = 0;
    Device->InterruptAcknowledgeAfterPollMask = 0;
    Device->PendingInterruptStatus = 0;
    Device->PollRoutine = NULL;
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Select and validate the active PCI register window.
 * @param Device Target common device state.
 * @param PreferredMode Preferred register access mode.
 * @param FallbackMode Optional fallback register access mode.
 * @param ValidationRegisterOffset Register read used to validate visibility.
 * @param FunctionName Caller function name for diagnostics.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkInitializeRegisterWindow(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, REALTEK_REGISTER_ACCESS_MODE PreferredMode,
    REALTEK_REGISTER_ACCESS_MODE FallbackMode, U16 ValidationRegisterOffset, LPCSTR FunctionName) {
    U32 ValidationValue;

    if (Device == NULL || StringEmpty(FunctionName)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    RealtekNetworkClearRegisterWindow(Device);

    if (!RealtekNetworkSelectRegisterWindowForMode(Device, PreferredMode, FunctionName) &&
        (FallbackMode == REALTEK_REGISTER_ACCESS_MODE_NONE ||
         !RealtekNetworkSelectRegisterWindowForMode(Device, FallbackMode, FunctionName))) {
        ERROR(TEXT("No usable register BAR found"));
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    ValidationValue = RealtekNetworkReadRegister32(Device, ValidationRegisterOffset);
    if (ValidationValue == MAX_U32) {
        ERROR(TEXT("Register validation failed at %x"), (UINT)ValidationRegisterOffset);
        return DF_RETURN_INPUT_OUTPUT;
    }

    DEBUG(TEXT("Register validation value=%x at %x"), ValidationValue, (UINT)ValidationRegisterOffset);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Issue a software reset and wait until the controller clears it.
 * @param Device Target common device state.
 * @param CommandRegisterOffset Chip command register offset.
 * @param ResetMask Reset bit mask.
 * @param FunctionName Caller function name for diagnostics.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkResetController(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 CommandRegisterOffset, U8 ResetMask, LPCSTR FunctionName) {
    UINT StartTime;
    UINT Loop;

    if (Device == NULL || ResetMask == 0 || StringEmpty(FunctionName)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    RealtekNetworkWriteRegister8(
        Device, CommandRegisterOffset, (U8)(RealtekNetworkReadRegister8(Device, CommandRegisterOffset) | ResetMask));

    StartTime = GetSystemTime();
    for (Loop = 0; HasOperationTimedOut(
                       StartTime, Loop, REALTEK_NETWORK_RESET_LOOP_LIMIT, REALTEK_NETWORK_RESET_TIMEOUT_MS) == FALSE;
         Loop++) {
        U8 Command = RealtekNetworkReadRegister8(Device, CommandRegisterOffset);
        if ((Command & ResetMask) == 0) {
            return DF_RETURN_SUCCESS;
        }
    }

    ERROR(TEXT("Reset timed out"));
    return DF_RETURN_UNEXPECTED;
}

/************************************************************************/

/**
 * @brief Put the controller in a quiet polling-only state after reset.
 * @param Device Target common device state.
 * @param CommandRegisterOffset Chip command register offset.
 * @param InterruptMaskRegisterOffset Interrupt-mask register offset.
 * @param InterruptStatusRegisterOffset Interrupt-status register offset.
 */
void RealtekNetworkInitializeQuietState(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 CommandRegisterOffset, U16 InterruptMaskRegisterOffset,
    U16 InterruptStatusRegisterOffset) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkWriteRegister16(Device, InterruptMaskRegisterOffset, 0);
    RealtekNetworkWriteRegister16(Device, InterruptStatusRegisterOffset, MAX_U16);
    RealtekNetworkWriteRegister8(Device, CommandRegisterOffset, 0);
}

/************************************************************************/

/**
 * @brief Read an 8-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @return Register value or zero when the window is unavailable.
 */
U8 RealtekNetworkReadRegister8(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset) {
    if (Device == NULL) {
        return 0;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        return (U8)InPortByte(Device->RegisterPort + RegisterOffset);
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        return *(volatile U8*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Read a 16-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @return Register value or zero when the window is unavailable.
 */
U16 RealtekNetworkReadRegister16(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset) {
    if (Device == NULL) {
        return 0;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        return (U16)InPortWord(Device->RegisterPort + RegisterOffset);
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        return *(volatile U16*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Read a 32-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @return Register value or MAX_U32 when the window is unavailable.
 */
U32 RealtekNetworkReadRegister32(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset) {
    if (Device == NULL) {
        return MAX_U32;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        return (U32)InPortLong(Device->RegisterPort + RegisterOffset);
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        return *(volatile U32*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset);
    }

    return MAX_U32;
}

/************************************************************************/

/**
 * @brief Write an 8-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @param Value Value to write.
 */
void RealtekNetworkWriteRegister8(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U8 Value) {
    if (Device == NULL) {
        return;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        OutPortByte(Device->RegisterPort + RegisterOffset, Value);
    } else if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        *(volatile U8*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset) = Value;
    }
}

/************************************************************************/

/**
 * @brief Write a 16-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @param Value Value to write.
 */
void RealtekNetworkWriteRegister16(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U16 Value) {
    if (Device == NULL) {
        return;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        OutPortWord(Device->RegisterPort + RegisterOffset, Value);
    } else if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        *(volatile U16*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset) = Value;
    }
}

/************************************************************************/

/**
 * @brief Write a 32-bit controller register through IO or MMIO.
 * @param Device Target common device state.
 * @param RegisterOffset Register offset.
 * @param Value Value to write.
 */
void RealtekNetworkWriteRegister32(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U32 Value) {
    if (Device == NULL) {
        return;
    }

    if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_IO) {
        OutPortLong(Device->RegisterPort + RegisterOffset, Value);
    } else if (Device->RegisterAccessMode == REALTEK_REGISTER_ACCESS_MODE_MMIO && Device->RegisterLinear != 0) {
        *(volatile U32*)((U8*)(LPVOID)Device->RegisterLinear + RegisterOffset) = Value;
    }
}

/************************************************************************/

/**
 * @brief Read a MAC address from two consecutive low/high controller registers.
 * @param Device Target common device state.
 * @param LowRegisterOffset Register offset containing bytes 0..3.
 * @param HighRegisterOffset Register offset containing bytes 4..5.
 * @param Mac Output MAC buffer.
 */
void RealtekNetworkReadMacFromRegisters(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 LowRegisterOffset, U16 HighRegisterOffset, U8* Mac) {
    U32 LowValue;
    U32 HighValue;

    if (Device == NULL || Mac == NULL) {
        return;
    }

    LowValue = RealtekNetworkReadRegister32(Device, LowRegisterOffset);
    HighValue = RealtekNetworkReadRegister32(Device, HighRegisterOffset);

    Mac[0] = (U8)(LowValue & MAX_U8);
    Mac[1] = (U8)((LowValue >> 8) & MAX_U8);
    Mac[2] = (U8)((LowValue >> 16) & MAX_U8);
    Mac[3] = (U8)((LowValue >> 24) & MAX_U8);
    Mac[4] = (U8)(HighValue & MAX_U8);
    Mac[5] = (U8)((HighValue >> 8) & MAX_U8);
}

/************************************************************************/

/**
 * @brief Clear the Realtek multicast filter registers with DWORD writes.
 * @param Device Target common device state.
 * @param MulticastRegisterOffset Offset of the first multicast filter register.
 */
void RealtekNetworkClearMulticastRegisters(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 MulticastRegisterOffset) {
    if (Device == NULL) {
        return;
    }

    RealtekNetworkWriteRegister32(Device, MulticastRegisterOffset, 0);
    RealtekNetworkWriteRegister32(Device, (U16)(MulticastRegisterOffset + sizeof(U32)), 0);
}

/************************************************************************/

/**
 * @brief Builds a deterministic placeholder MAC address for pre-hardware bring-up.
 * @param Device Target common device state.
 */
void RealtekNetworkBuildPlaceholderMac(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Mac[0] = 0x02;
    Device->Mac[1] = 0x10;
    Device->Mac[2] = 0xEC;
    Device->Mac[3] = Device->Info.Bus;
    Device->Mac[4] = Device->Info.Dev;
    Device->Mac[5] = Device->Info.Func;
}

/************************************************************************/

/**
 * @brief Configure shared interrupt metadata for one Realtek device.
 * @param Device Target common device state.
 * @param InterruptMaskRegisterOffset Interrupt mask register offset.
 * @param InterruptStatusRegisterOffset Interrupt status register offset.
 * @param InterruptEnableMask Hardware mask applied when IRQ mode is armed.
 * @param InterruptRelevantMask Status bits that should schedule deferred work.
 */
void RealtekNetworkConfigureInterruptSupport(
    LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 InterruptMaskRegisterOffset, U16 InterruptStatusRegisterOffset,
    U16 InterruptEnableMask, U16 InterruptRelevantMask, U16 InterruptAcknowledgeAfterPollMask) {
    if (Device == NULL) {
        return;
    }

    Device->InterruptMaskRegisterOffset = InterruptMaskRegisterOffset;
    Device->InterruptStatusRegisterOffset = InterruptStatusRegisterOffset;
    Device->InterruptEnableMask = InterruptEnableMask;
    Device->InterruptRelevantMask = InterruptRelevantMask;
    Device->InterruptAcknowledgeAfterPollMask = InterruptAcknowledgeAfterPollMask;
    Device->PendingInterruptStatus = 0;
}

/************************************************************************/

/**
 * @brief Validates a generic network reset request.
 * @param Reset Reset request.
 * @return DF_RETURN_SUCCESS when the device handle is valid.
 */
U32 RealtekNetworkOnReset(const NETWORK_RESET* Reset) {
    if (Reset == NULL || Reset->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Fills NETWORK_INFO from the common Realtek device state.
 * @param GetInfo Information request.
 * @param LinkUp Link status to report.
 * @param SpeedMbps Link speed to report.
 * @param DuplexFull Duplex state to report.
 * @param Mtu MTU to report.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkOnGetInfo(const NETWORK_GET_INFO* GetInfo, BOOL LinkUp, U32 SpeedMbps, BOOL DuplexFull, U32 Mtu) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)GetInfo->Device;
    MemoryCopy(GetInfo->Info->MAC, Device->Mac, sizeof(Device->Mac));
    GetInfo->Info->LinkUp = LinkUp;
    GetInfo->Info->SpeedMbps = SpeedMbps;
    GetInfo->Info->DuplexFull = DuplexFull;
    GetInfo->Info->MTU = Mtu;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Deliver one received Ethernet frame to the registered callback.
 * @param Device Target common device state.
 * @param Frame Received frame payload.
 * @param Length Frame length in bytes.
 */
void RealtekNetworkDeliverReceivedFrame(LPREALTEK_NETWORK_COMMON_DEVICE Device, const U8* Frame, U32 Length) {
    if (Device == NULL || Frame == NULL || Length == 0) {
        return;
    }

    if (Device->RxCallback != NULL) {
        Device->RxCallback(Frame, Length, Device->RxUserData);
    }
}

/************************************************************************/

/**
 * @brief Stores the receive callback for later RX-path implementation.
 * @param Set Callback registration request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkOnSetReceiveCallback(const NETWORK_SET_RX_CB* Set) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (Set == NULL || Set->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    Device->RxUserData = Set->UserData;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared TX stub used until hardware-specific transmit support exists.
 * @param Send Send request.
 * @return DF_RETURN_NOT_IMPLEMENTED for the early integration skeleton.
 */
U32 RealtekNetworkOnSendNotImplemented(const NETWORK_SEND* Send) {
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Shared polling stub used before a receive path exists.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
U32 RealtekNetworkOnPollIdle(const NETWORK_POLL* Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared interrupt-enable stub for polling-only bring-up.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_NOT_IMPLEMENTED so polling remains active.
 */
U32 RealtekNetworkOnEnableInterrupts(DEVICE_INTERRUPT_CONFIG* Config) {
    DEVICE_INTERRUPT_REGISTRATION Registration;
    LPREALTEK_NETWORK_COMMON_DEVICE Device;
    U8 LegacyIRQ;

    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)Config->Device;
    if (Device->PollRoutine == NULL || Device->InterruptMaskRegisterOffset == 0 ||
        Device->InterruptStatusRegisterOffset == 0 || Device->InterruptEnableMask == 0 ||
        Device->InterruptRelevantMask == 0) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (Device->InterruptRegistered) {
        Config->VectorSlot = Device->InterruptSlot;
        Config->InterruptEnabled = Device->InterruptArmed;
        return DF_RETURN_SUCCESS;
    }

    MemorySet(&Registration, 0, sizeof(Registration));
    LegacyIRQ = Config->LegacyIRQ;
    if (LegacyIRQ == MAX_U8) {
        LegacyIRQ = Device->Info.IRQLine;
    }
    Registration.Device = (LPDEVICE)Device;
    Registration.LegacyIRQ = LegacyIRQ;
    Registration.TargetCPU = Config->TargetCPU;
    Registration.InterruptHandler = RealtekNetworkInterruptTopHalf;
    Registration.DeferredCallback = RealtekNetworkDeferredRoutine;
    Registration.PollCallback = RealtekNetworkPollRoutine;
    Registration.Context = Device;
    Registration.Name = Device->Driver ? Device->Driver->Product : TEXT("Realtek");

    if (!DeviceInterruptRegister(&Registration, &Device->InterruptSlot)) {
        Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        Device->InterruptRegistered = FALSE;
        Device->InterruptArmed = FALSE;
        Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
        Config->InterruptEnabled = FALSE;
        WARNING(TEXT("Failed to register device interrupt"));
        return DF_RETURN_UNEXPECTED;
    }

    Device->InterruptRegistered = TRUE;
    Device->InterruptArmed = DeviceInterruptSlotIsEnabled(Device->InterruptSlot);
    Device->PendingInterruptStatus = 0;
    RealtekNetworkWriteRegister16(Device, Device->InterruptMaskRegisterOffset, 0);
    RealtekNetworkWriteRegister16(Device, Device->InterruptStatusRegisterOffset, MAX_U16);
    RealtekNetworkRearmInterrupts(Device);
    Config->VectorSlot = Device->InterruptSlot;
    Config->InterruptEnabled = Device->InterruptArmed;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared interrupt-disable stub for polling-only bring-up.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
U32 RealtekNetworkOnDisableInterrupts(DEVICE_INTERRUPT_CONFIG* Config) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)Config->Device;
    if (Device->InterruptMaskRegisterOffset != 0) {
        RealtekNetworkWriteRegister16(Device, Device->InterruptMaskRegisterOffset, 0);
    }
    if (Device->InterruptStatusRegisterOffset != 0) {
        RealtekNetworkWriteRegister16(Device, Device->InterruptStatusRegisterOffset, MAX_U16);
    }
    if (Device->InterruptRegistered && Device->InterruptSlot != DEVICE_INTERRUPT_INVALID_SLOT) {
        DeviceInterruptUnregister(Device->InterruptSlot);
    }

    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->InterruptRegistered = FALSE;
    Device->InterruptArmed = FALSE;
    Device->PendingInterruptStatus = 0;
    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared driver-load callback for early Realtek drivers.
 * @return DF_RETURN_SUCCESS.
 */
U32 RealtekNetworkOnLoad(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Shared driver-unload callback for early Realtek drivers.
 * @return DF_RETURN_SUCCESS.
 */
U32 RealtekNetworkOnUnload(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Shared capability query for early Realtek drivers.
 * @return Zero because advanced capabilities are not exposed yet.
 */
U32 RealtekNetworkOnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Shared highest-implemented function identifier.
 * @return DF_DEV_DISABLE_INTERRUPT for the polling-first skeleton.
 */
U32 RealtekNetworkOnGetLastFunction(void) { return DF_DEV_DISABLE_INTERRUPT; }
