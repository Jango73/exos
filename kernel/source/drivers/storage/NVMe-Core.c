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


    NVMe

\************************************************************************/

#include "drivers/storage/NVMe-Internal.h"

/************************************************************************/

#define NVME_VER_MAJOR 1
#define NVME_VER_MINOR 0
/************************************************************************/
// Forward declarations

static UINT NVMeCommands(UINT Function, UINT Param);
static UINT NVMeProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE NVMeAttach(LPPCI_DEVICE PciDevice);
static BOOL NVMeGetBar0Physical(LPPCI_DEVICE Device, PHYSICAL* BaseOut, U32* SizeOut);
static BOOL NVMeWaitForReady(LPNVME_DEVICE Device, BOOL Ready);

/************************************************************************/
// PCI match table and driver

static DRIVER_MATCH NVMeMatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, NVME_PCI_CLASS, NVME_PCI_SUBCLASS, NVME_PCI_PROG_IF}
};

PCI_DRIVER DATA_SECTION NVMePCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NVME_STORAGE,
    .VersionMajor = NVME_VER_MAJOR,
    .VersionMinor = NVME_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "",
    .Product = "NVMe Controller",
    .Alias = "nvme",
    .Command = NVMeCommands,
    .Matches = NVMeMatchTable,
    .MatchCount = sizeof(NVMeMatchTable) / sizeof(NVMeMatchTable[0]),
    .Attach = NVMeAttach
};

/************************************************************************/

/**
 * @brief Retrieves the NVMe PCI driver descriptor.
 * @return Pointer to the NVMe PCI driver.
 */
LPDRIVER NVMeGetDriver(void) {
    return (LPDRIVER)&NVMePCIDriver;
}

/************************************************************************/

/**
 * @brief Free one queue buffer allocation.
 *
 * @param Queue Queue buffer descriptor.
 */
void NVMeFreeQueueBuffer(LPNVME_QUEUE_BUFFER Queue) {
    if (Queue == NULL) {
        return;
    }

    if (Queue->Raw != NULL) {
        KernelHeapFree(Queue->Raw);
    }

    Queue->Raw = NULL;
    Queue->Base = 0;
    Queue->Physical = 0;
    Queue->Size = 0;
}

/************************************************************************/

/**
 * @brief Allocate one aligned queue buffer and validate physical contiguity.
 *
 * @param Queue Queue descriptor output.
 * @param QueueSize Requested queue size in bytes.
 * @param QueueAlignment Required queue alignment in bytes.
 * @param QueueName Queue name used in logs.
 * @return TRUE on success.
 */
BOOL NVMeAllocateQueueBuffer(LPNVME_QUEUE_BUFFER Queue, U32 QueueSize, UINT QueueAlignment, LPCSTR QueueName) {
    if (Queue == NULL || QueueSize == 0 || QueueAlignment == 0) {
        return FALSE;
    }

    UINT RawSize = (UINT)QueueSize + QueueAlignment;
    Queue->Raw = KernelHeapAlloc(RawSize);
    if (Queue->Raw == NULL) {
        ERROR(TEXT("KernelHeapAlloc failed for %s (raw_size=%u)"), QueueName, RawSize);
        return FALSE;
    }

    LINEAR RawBase = (LINEAR)Queue->Raw;
    Queue->Base = (LINEAR)((RawBase + (QueueAlignment - 1)) & ~(QueueAlignment - 1));
    Queue->Size = QueueSize;
    MemorySet((LPVOID)Queue->Base, 0, Queue->Size);

    Queue->Physical = MapLinearToPhysical(Queue->Base);
    if (Queue->Physical == 0) {
        ERROR(TEXT("MapLinearToPhysical failed for %s base=%p"),
              QueueName,
              (LPVOID)Queue->Base);
        NVMeFreeQueueBuffer(Queue);
        return FALSE;
    }

    for (UINT Offset = 0; Offset < Queue->Size; Offset += N_4KB) {
        LINEAR Linear = Queue->Base + (LINEAR)Offset;
        PHYSICAL Physical = MapLinearToPhysical(Linear);
        PHYSICAL Expected = Queue->Physical + (PHYSICAL)Offset;
        if (Physical != Expected) {
            ERROR(TEXT("Non contiguous %s (base_pa=%p offset=%u pa=%p expected=%p)"),
                  QueueName,
                  (LPVOID)(LINEAR)Queue->Physical,
                  Offset,
                  (LPVOID)(LINEAR)Physical,
                  (LPVOID)(LINEAR)Expected);
            NVMeFreeQueueBuffer(Queue);
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RETURN_* code.
 */
static UINT NVMeCommands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return DF_RETURN_SUCCESS;
        case DF_UNLOAD:
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(NVME_VER_MAJOR, NVME_VER_MINOR);
        case DF_GET_LAST_FUNCTION:
            return DF_PROBE;
        case DF_PROBE:
            return NVMeProbe(Function, Param);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief PCI probe entry for the NVMe driver.
 *
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return DF_RETURN_SUCCESS on handled, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static UINT NVMeProbe(UINT Function, UINT Parameter) {
    LPPCI_INFO PciInfo = (LPPCI_INFO)Parameter;

    if (Function != DF_PROBE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Retrieve BAR0 physical address and size.
 *
 * @param Device PCI device pointer.
 * @param BaseOut Output physical base.
 * @param SizeOut Output size.
 * @return TRUE when BAR0 is valid.
 */
static BOOL NVMeGetBar0Physical(LPPCI_DEVICE Device, PHYSICAL* BaseOut, U32* SizeOut) {
    if (Device == NULL || BaseOut == NULL || SizeOut == NULL) {
        return FALSE;
    }

    U32 Bar0Raw = PCI_Read32(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_BAR0);
    if (PCI_BAR_IS_IO(Bar0Raw)) {
        return FALSE;
    }

    U32 Bar0Low = (Bar0Raw & PCI_BAR_MEM_MASK);
    U32 BarType = (Bar0Raw >> 1) & 0x3;
    PHYSICAL BarPhysical = (PHYSICAL)Bar0Low;

    if (BarType == 0x2) {
        U32 Bar0High = PCI_Read32(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_BAR1);
        #ifdef __EXOS_32__
        if (Bar0High != 0) {
            return FALSE;
        }
        #else
        BarPhysical = ((PHYSICAL)Bar0High << 32) | (PHYSICAL)Bar0Low;
        #endif
    }

    *BaseOut = BarPhysical;
    *SizeOut = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);

    return (*BaseOut != 0 && *SizeOut != 0);
}

/************************************************************************/

/**
 * @brief Wait for the controller ready state.
 *
 * @param Device NVMe device.
 * @param Ready TRUE to wait for ready, FALSE to wait for not ready.
 * @return TRUE on expected state, FALSE on timeout.
 */
static BOOL NVMeWaitForReady(LPNVME_DEVICE Device, BOOL Ready) {
    if (Device == NULL || Device->MmioBase == 0) {
        return FALSE;
    }

    volatile U32* Regs = (volatile U32*)Device->MmioBase;
    for (UINT Loop = 0; Loop < NVME_READY_TIMEOUT_LOOPS; Loop++) {
        U32 Csts = Regs[NVME_REG_CSTS / 4];
        BOOL IsReady = ((Csts & 0x1) != 0);
        if (IsReady == Ready) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Attach detected NVMe controller and map registers.
 *
 * @param PciDevice Detected PCI device descriptor.
 * @return Heap-allocated PCI device pointer or NULL on failure.
 */
static LPPCI_DEVICE NVMeAttach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    LPNVME_DEVICE Device = (LPNVME_DEVICE)KernelHeapAlloc(sizeof(NVME_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(NVME_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));
    Device->Next = NULL;
    Device->Prev = NULL;
    Device->References = 1;
    NVMeInitDiskDriver(Device);
    Device->InterruptSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Device->MsixVector = 0;
    Device->MsixEnabled = FALSE;
    Device->LogicalBlockSize = SECTOR_SIZE;

    PHYSICAL Bar0Physical = 0;
    U32 Bar0Size = 0;
    if (!NVMeGetBar0Physical((LPPCI_DEVICE)Device, &Bar0Physical, &Bar0Size)) {
        ERROR(TEXT("Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(Bar0Physical, Bar0Size);
    Device->MmioSize = Bar0Size;
    if (Device->MmioBase == 0) {
        ERROR(TEXT("MapIOMemory failed for %p size %u"),
              (LPVOID)(LINEAR)Bar0Physical,
              (U32)Bar0Size);
        KernelHeapFree(Device);
        return NULL;
    }

    volatile U32* Regs = (volatile U32*)Device->MmioBase;
    U32 CapLow = Regs[NVME_REG_CAP / 4];
    U32 CapHigh = Regs[(NVME_REG_CAP / 4) + 1];
    U32 Dstrd = CapHigh & 0xF;
    U32 Version = Regs[NVME_REG_VS / 4];
    U32 Cc = Regs[NVME_REG_CC / 4];
    U32 Csts = Regs[NVME_REG_CSTS / 4];
    U32 Aqa = Regs[NVME_REG_AQA / 4];
    U32 AsqLow = Regs[NVME_REG_ASQ / 4];
    U32 AsqHigh = Regs[(NVME_REG_ASQ / 4) + 1];
    U32 AcqLow = Regs[NVME_REG_ACQ / 4];
    U32 AcqHigh = Regs[(NVME_REG_ACQ / 4) + 1];
    UNUSED(CapLow);
    UNUSED(Version);
    UNUSED(Cc);
    UNUSED(Csts);
    UNUSED(Aqa);
    UNUSED(AsqLow);
    UNUSED(AsqHigh);
    UNUSED(AcqLow);
    UNUSED(AcqHigh);

    DEBUG(TEXT("BAR0=%p size=%u CAP=%x/%x VS=%x CC=%x CSTS=%x AQA=%x"),
          (LPVOID)(LINEAR)Bar0Physical,
          (U32)Bar0Size,
          CapLow,
          CapHigh,
          Version,
          Cc,
          Csts,
          Aqa);
    DEBUG(TEXT("ASQ=%x/%x ACQ=%x/%x"),
          AsqLow,
          AsqHigh,
          AcqLow,
          AcqHigh);

    Device->DoorbellStride = (U32)(4 << Dstrd);

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!NVMeSetupAdminQueues(Device)) {
        ERROR(TEXT("Failed to allocate admin queues"));
        UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        KernelHeapFree(Device);
        return NULL;
    }

    U32 CcValue = Regs[NVME_REG_CC / 4];
    if ((CcValue & NVME_CC_EN) != 0) {
        Regs[NVME_REG_CC / 4] = (CcValue & ~NVME_CC_EN);
        if (!NVMeWaitForReady(Device, FALSE)) {
            ERROR(TEXT("Controller did not stop"));
            NVMeFreeAdminQueues(Device);
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            KernelHeapFree(Device);
            return NULL;
        }
    }

    CcValue = (0 << NVME_CC_CSS_SHIFT) |
              (0 << NVME_CC_MPS_SHIFT) |
              (0 << NVME_CC_AMS_SHIFT) |
              (0 << NVME_CC_SHN_SHIFT) |
              (6 << NVME_CC_IOSQES_SHIFT) |
              (4 << NVME_CC_IOCQES_SHIFT);
    U32 AqaValue = ((Device->AdminCqEntries - 1) << 16) | (Device->AdminSqEntries - 1);
    Regs[NVME_REG_AQA / 4] = AqaValue;

    PHYSICAL AsqPhys = Device->AdminSqBuffer.Physical;
    PHYSICAL AcqPhys = Device->AdminCqBuffer.Physical;

    U32 AsqLowNew = (U32)(AsqPhys & 0xFFFFFFFF);
    U32 AsqHighNew = 0;
    U32 AcqLowNew = (U32)(AcqPhys & 0xFFFFFFFF);
    U32 AcqHighNew = 0;
#ifdef __EXOS_64__
    AsqHighNew = (U32)((AsqPhys >> 32) & 0xFFFFFFFF);
    AcqHighNew = (U32)((AcqPhys >> 32) & 0xFFFFFFFF);
#endif

    Regs[NVME_REG_ASQ / 4] = AsqLowNew;
    Regs[(NVME_REG_ASQ / 4) + 1] = AsqHighNew;
    Regs[NVME_REG_ACQ / 4] = AcqLowNew;
    Regs[(NVME_REG_ACQ / 4) + 1] = AcqHighNew;

    Regs[NVME_REG_CC / 4] = CcValue;
    Regs[NVME_REG_CC / 4] = (CcValue | NVME_CC_EN);
    if (!NVMeWaitForReady(Device, TRUE)) {
        ERROR(TEXT("Controller did not become ready"));
        NVMeFreeAdminQueues(Device);
        UnMapIOMemory(Device->MmioBase, Device->MmioSize);
        KernelHeapFree(Device);
        return NULL;
    }

    DEBUG(TEXT("Admin queues ready ASQ=%p ACQ=%p AQA=%x"),
          (LPVOID)(LINEAR)AsqPhys,
          (LPVOID)(LINEAR)AcqPhys,
          AqaValue);

    if (!NVMeIdentifyController(Device)) {
        WARNING(TEXT("Identify controller failed"));
    }
    if (!NVMeIdentifyNamespace(Device, 1, NULL, &Device->LogicalBlockSize)) {
        WARNING(TEXT("Identify namespace 1 failed"));
    }
    if (!NVMeSetNumberOfQueues(Device, 1)) {
        WARNING(TEXT("Set number of queues failed"));
    }
#if NVME_POLLING_ONLY
    Device->MsixEnabled = FALSE;
#else
    if (!NVMeSetupInterrupts(Device)) {
        WARNING(TEXT("MSI-X setup failed"));
    }
#endif
    if (!NVMeCreateIoQueues(Device)) {
        WARNING(TEXT("Create IO queues failed"));
    } else {
        if (!NVMeSubmitIoNoop(Device)) {
            WARNING(TEXT("I/O NO-OP failed"));
            NVMeFreeIoQueues(Device);
        } else {
            if (!NVMeReadTest(Device)) {
                WARNING(TEXT("Read test failed"));
            } else if (!NVMeRegisterNamespaces(Device)) {
                WARNING(TEXT("Namespace registration failed"));
            }
        }
    }

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/
