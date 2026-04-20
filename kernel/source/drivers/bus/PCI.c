
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


    PCI

\************************************************************************/

#include "drivers/bus/PCI.h"

#include "Base.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "core/DriverEnum.h"
#include "drivers/graphics/common/Graphics-PCI.h"
#include "drivers/network/E1000.h"
#include "drivers/network/RTL8139.h"
#include "drivers/network/RTL8139CPlus.h"
#include "drivers/network/RTL8169.h"
#include "drivers/usb/XHCI.h"
#include "User.h"

/***************************************************************************/
// PCI config mechanism #1 (0xCF8/0xCFC)

#define PCI_CONFIG_ADDRESS_PORT 0x0CF8
#define PCI_CONFIG_DATA_PORT 0x0CFC

/* Build the 32-bit address for type-1 config cycles */
#define PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset)                                                        \
    (0x80000000U | (((U32)(Bus)&0xFFU) << 16) | (((U32)(Device)&0x1FU) << 11) | (((U32)(Function)&0x07U) << 8) | \
     ((U32)(Offset)&0xFCU))

/***************************************************************************/
// Forward declarations

static int PciInternalMatch(const DRIVER_MATCH* DriverMatch, const PCI_INFO* PciInfo);
static void PciFillFunctionInfo(U8 Bus, U8 Device, U8 Function, PCI_INFO* PciInfo);
static void PciDecodeBARs(const PCI_INFO* PciInfo, PCI_DEVICE* PciDevice);
static U32 PCI_EnumNext(LPDRIVER_ENUM_NEXT Next);
static U32 PCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty);

/***************************************************************************/
// Registered PCI drivers

#define PCI_MAX_REGISTERED_DRIVERS 32

static LPPCI_DRIVER DATA_SECTION PciDriverTable[PCI_MAX_REGISTERED_DRIVERS];
static U32 DATA_SECTION PciDriverCount = 0;

/***************************************************************************/

#define PCI_VER_MAJOR 1
#define PCI_VER_MINOR 0

static UINT PCIDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION PCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = PCI_VER_MAJOR,
    .VersionMinor = PCI_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "PCI-SIG",
    .Product = "PCI",
    .Alias = "pci",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = PCIDriverCommands,
    .EnumDomainCount = 1,
    .EnumDomains = {ENUM_DOMAIN_PCI_DEVICE}};

/***************************************************************************/

/**
 * @brief Retrieves the PCI driver descriptor.
 * @return Pointer to the PCI driver.
 */
LPDRIVER PCIGetDriver(void) {
    return &PCIDriver;
}

/***************************************************************************/
// Low-level config space access (assumes port I/O helpers exist)

/**
 * @brief Reads a 32-bit value from PCI configuration space.
 *
 * Builds a type-1 configuration address and retrieves the value via port
 * I/O.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @return 32-bit value read from the device.
 */

U32 PCI_Read32(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Address = PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset);
    OutPortLong(PCI_CONFIG_ADDRESS_PORT, Address);
    return InPortLong(PCI_CONFIG_DATA_PORT);
}

/***************************************************************************/

/**
 * @brief Writes a 32-bit value to PCI configuration space.
 *
 * Builds a type-1 configuration address and writes the value using port
 * I/O.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @param Value    32-bit value to write.
 */

void PCI_Write32(U8 Bus, U8 Device, U8 Function, U16 Offset, U32 Value) {
    U32 Address = PCI_CONFIG_ADDRESS(Bus, Device, Function, Offset);
    OutPortLong(PCI_CONFIG_ADDRESS_PORT, Address);
    OutPortLong(PCI_CONFIG_DATA_PORT, Value);
}

/***************************************************************************/

/**
 * @brief Reads a 16-bit value from PCI configuration space.
 *
 * Reads the containing 32-bit register and extracts the requested
 * 16-bit field.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @return 16-bit value read from the device.
 */

U16 PCI_Read16(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    return (U16)((Value32 >> ((Offset & 2U) * 8U)) & 0xFFFFU);
}

/***************************************************************************/

/**
 * @brief Reads an 8-bit value from PCI configuration space.
 *
 * Reads the containing 32-bit register and extracts the requested
 * byte.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @return 8-bit value read from the device.
 */

U8 PCI_Read8(U8 Bus, U8 Device, U8 Function, U16 Offset) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    return (U8)((Value32 >> ((Offset & 3U) * 8U)) & 0xFFU);
}

/***************************************************************************/

/**
 * @brief Writes a 16-bit value to PCI configuration space.
 *
 * Reads the containing 32-bit register, updates the target field and writes
 * it back.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @param Value    16-bit value to write.
 */

void PCI_Write16(U8 Bus, U8 Device, U8 Function, U16 Offset, U16 Value) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    U32 Shift = (Offset & 2U) * 8U;
    Value32 &= ~(0xFFFFU << Shift);
    Value32 |= ((U32)Value) << Shift;
    PCI_Write32(Bus, Device, Function, (U16)(Offset & ~3U), Value32);
}

/***************************************************************************/

/**
 * @brief Writes an 8-bit value to PCI configuration space.
 *
 * Reads the containing 32-bit register, updates the target byte and writes
 * it back.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Offset   Register offset.
 * @param Value    8-bit value to write.
 */

void PCI_Write8(U8 Bus, U8 Device, U8 Function, U16 Offset, U8 Value) {
    U32 Value32 = PCI_Read32(Bus, Device, Function, (U16)(Offset & ~3U));
    U32 Shift = (Offset & 3U) * 8U;
    Value32 &= ~(0xFFU << Shift);
    Value32 |= ((U32)Value) << Shift;
    PCI_Write32(Bus, Device, Function, (U16)(Offset & ~3U), Value32);
}

/***************************************************************************/

/**
 * @brief Enables or disables bus mastering for a PCI function.
 *
 * Modifies the command register to toggle the bus master and memory
 * access bits.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param Enable   TRUE to enable bus mastering, FALSE to disable.
 * @return Previous command register value.
 */

U16 PCI_EnableBusMaster(U8 Bus, U8 Device, U8 Function, BOOL Enable) {
    U16 Command = PCI_Read16(Bus, Device, Function, PCI_CFG_COMMAND);
    U16 Previous = Command;
    if (Enable != FALSE) {
        Command |= PCI_CMD_BUSMASTER | PCI_CMD_MEM;
    } else {
        Command &= (U16)~PCI_CMD_BUSMASTER;
    }
    PCI_Write16(Bus, Device, Function, PCI_CFG_COMMAND, Command);
    return Previous;
}

/***************************************************************************/

/**
 * @brief Reads a Base Address Register of a PCI function.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param BarIndex Index of the BAR to read.
 * @return Raw BAR value.
 */

U32 PciReadBAR(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U16 Offset = (U16)(PCI_CFG_BAR0 + (BarIndex * 4));
    return PCI_Read32(Bus, Device, Function, Offset);
}

/***************************************************************************/

/**
 * @brief Retrieves the base address of a BAR.
 *
 * Handles both I/O and memory BARs and returns the base address. For
 * 64-bit memory BARs only the low part is returned.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param BarIndex Index of the BAR to query.
 * @return Base address of the BAR.
 */

U32 PCI_GetBARBase(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U32 Bar = PciReadBAR(Bus, Device, Function, BarIndex);
    if (PCI_BAR_IS_IO(Bar)) {
        return (Bar & PCI_BAR_IO_MASK);
    } else {
        /* Memory BAR (treat 64-bit as returning low part for now) */
        return (Bar & PCI_BAR_MEM_MASK);
    }
}

/***************************************************************************/

/**
 * @brief Determines the size of a BAR.
 *
 * Temporarily writes all ones to the BAR to read back the size mask, as
 * described by the PCI specification. Restores the original value
 * afterward.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param BarIndex Index of the BAR to probe.
 * @return Size of the BAR in bytes.
 */

U32 PCI_GetBARSize(U8 Bus, U8 Device, U8 Function, U8 BarIndex) {
    U16 Offset = (U16)(PCI_CFG_BAR0 + (BarIndex * 4));
    U32 Original = PCI_Read32(Bus, Device, Function, Offset);
    U32 Size = 0;

    /* Write all-ones to determine size mask per PCI spec */
    PCI_Write32(Bus, Device, Function, Offset, 0xFFFFFFFFU);
    U32 Probed = PCI_Read32(Bus, Device, Function, Offset);

    /* Restore original value */
    PCI_Write32(Bus, Device, Function, Offset, Original);

    if (PCI_BAR_IS_IO(Original)) {
        U32 Mask = Probed & PCI_BAR_IO_MASK;
        if (Mask != 0) Size = (~Mask) + 1U;
    } else {
        /* Memory BAR, may be 64-bit */
        U32 Type = (Original >> 1) & 0x3U;
        U32 Mask = Probed & PCI_BAR_MEM_MASK;
        if (Type == 0x2U) {
            /* 64-bit BAR: also probe high dword */
            U16 OffsetHigh = (U16)(Offset + 4);
            U32 OriginalHigh = PCI_Read32(Bus, Device, Function, OffsetHigh);
            PCI_Write32(Bus, Device, Function, OffsetHigh, 0xFFFFFFFFU);
            U32 ProbedHigh = PCI_Read32(Bus, Device, Function, OffsetHigh);
            PCI_Write32(Bus, Device, Function, OffsetHigh, OriginalHigh);

            /* We return the low 32-bit span as size for now */
            if ((Mask | ProbedHigh) != 0) {
                if (Mask != 0) Size = (~Mask) + 1U;
            }
        } else {
            if (Mask != 0) Size = (~Mask) + 1U;
        }
    }

    return Size;
}

/***************************************************************************/

/**
 * @brief Searches the capability list for a specific capability ID.
 *
 * Traverses the linked list of capabilities if present.
 *
 * @param Bus          Bus number.
 * @param Device       Device number.
 * @param Function     Function number.
 * @param CapabilityId Capability identifier to locate.
 * @return Offset of the capability or 0 if not found.
 */

U8 PCI_FindCapability(U8 Bus, U8 Device, U8 Function, U8 CapabilityId) {
    U16 Status = PCI_Read16(Bus, Device, Function, PCI_CFG_STATUS);
    if ((Status & 0x10U) == 0) return 0; /* no cap list */

    U8 Pointer = PCI_Read8(Bus, Device, Function, PCI_CFG_CAP_PTR) & 0xFCU;

    /* Sanity iteration bound */
    for (U32 Index = 0; Index < 48 && Pointer >= 0x40; Index++) {
        U8 Id = PCI_Read8(Bus, Device, Function, (U16)(Pointer + 0));
        U8 Next = PCI_Read8(Bus, Device, Function, (U16)(Pointer + 1)) & 0xFCU;
        if (Id == CapabilityId) return Pointer;
        if (Next == 0 || Next == Pointer) break;
        Pointer = Next;
    }
    return 0;
}

/***************************************************************************/

/**
 * @brief Registers a PCI driver with the bus layer.
 *
 * Drivers are stored in an internal table until the bus scan associates
 * them with matching devices.
 *
 * @param Driver Pointer to the driver structure.
 */

void PCI_RegisterDriver(LPPCI_DRIVER Driver) {
    if (Driver == NULL) return;
    if (PciDriverCount >= PCI_MAX_REGISTERED_DRIVERS) return;
    PciDriverTable[PciDriverCount++] = Driver;
    DEBUG(TEXT("Registered driver %s"), Driver->Product);
}

/***************************************************************************/

/**
 * @brief Scans the PCI bus and binds drivers to detected devices.
 *
 * Enumerates all buses, devices and functions, matches registered
 * drivers and attaches them to devices that report a successful probe.
 *
 * CRITICAL REQUIREMENT FOR PCI DRIVER ATTACH FUNCTIONS:
 *
 * All PCI driver attach functions MUST return a heap-allocated device object,
 * NOT the original PciDevice parameter or a stack-allocated object.
 *
 * REQUIRED PATTERN FOR PCI DRIVER ATTACH FUNCTIONS:
 * 1. Validate input parameters (return NULL if invalid)
 * 2. Allocate new device structure using KernelHeapAlloc()
 * 3. Copy PCI device information to the new structure
 * 4. Initialize device-specific fields (Next, Prev, References)
 * 5. Perform device initialization
 * 6. On any failure: KernelHeapFree(Device) and return NULL
 * 7. On success: return the heap-allocated device structure
 *
 * CORRECT EXAMPLE:
 *   Device = (LPE1000DEVICE)KernelHeapAlloc(sizeof(E1000DEVICE));
 *   if (Device == NULL) return NULL;
 *   MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
 *   Device->Next = NULL;
 *   Device->Prev = NULL;
 *   Device->References = 1;
 *   // ... device initialization ...
 *   return (LPPCI_DEVICE)Device;
 *
 * INCORRECT PATTERNS (DO NOT USE):
 *   - return PciDevice;               // Returns original parameter
 *   - return &localVariable;         // Returns stack object
 *   - return staticGlobalObject;     // Returns static object
 *
 * WHY THIS IS REQUIRED:
 * The PCI subsystem expects attach functions to return device objects that:
 * - Are allocated on the kernel heap for proper memory management
 * - Can be safely stored in device lists and referenced by other subsystems
 * - Will not be invalidated when the attach function returns
 * - Can be properly freed when the device is removed
 *
 * MEMORY MANAGEMENT:
 * - Always use KernelHeapFree() on failure paths to prevent memory leaks
 * - The returned object becomes owned by the PCI subsystem
 * - Reference counting (References field) tracks object lifetime
 *
 * This pattern is enforced across all PCI drivers in the system.
 * See E1000_Attach() for a reference implementation.
 */

void PCI_ScanBus(void) {
    /* Use 32-bit counters to avoid wrap when PCI_MAX_* == 256 */
    U32 Bus, Device, Function;

    DEBUG(TEXT("Scanning bus"));

    for (Bus = 0; Bus < PCI_MAX_BUS; Bus++) {
        for (Device = 0; Device < PCI_MAX_DEV; Device++) {
            /* Check presence on function 0 */
            U16 VendorFunction0 = PCI_Read16((U8)Bus, (U8)Device, 0, PCI_CFG_VENDOR_ID);
            if (VendorFunction0 == 0xFFFFU) continue;

            U8 HeaderType = PCI_Read8((U8)Bus, (U8)Device, 0, PCI_CFG_HEADER_TYPE);
            U8 IsMultiFunction = (HeaderType & PCI_HEADER_MULTI_FN) ? 1 : 0;
            U8 MaxFunction = IsMultiFunction ? (PCI_MAX_FUNC - 1) : 0;

            for (Function = 0; Function <= (U32)MaxFunction; Function++) {
                U16 VendorId = PCI_Read16((U8)Bus, (U8)Device, (U8)Function, PCI_CFG_VENDOR_ID);
                if (VendorId == 0xFFFFU) continue;

                PCI_INFO PciInfo;
                PCI_DEVICE PciDevice;
                U32 DriverIndex;

                PciFillFunctionInfo((U8)Bus, (U8)Device, (U8)Function, &PciInfo);
                DEBUG(TEXT("Found %x:%x.%u VID=%x DID=%x IRQ=%u"),
                    (INT)Bus,
                    (INT)Device,
                    (INT)Function,
                    (INT)PciInfo.VendorID,
                    (INT)PciInfo.DeviceID,
                    (UINT)PciInfo.IRQLine);

                MemorySet(&PciDevice, 0, sizeof(PCI_DEVICE));
                InitMutex(&(PciDevice.Mutex));
                PciDevice.TypeID = KOID_PCIDEVICE;
                PciDevice.References = 1;
                PciDevice.Driver = NULL;
                PciDevice.Info = PciInfo;
                PciDecodeBARs(&PciInfo, &PciDevice);

                for (DriverIndex = 0; DriverIndex < PciDriverCount; DriverIndex++) {
                    LPPCI_DRIVER PciDriver = PciDriverTable[DriverIndex];

                    for (U32 MatchIndex = 0; MatchIndex < PciDriver->MatchCount; MatchIndex++) {
                        const DRIVER_MATCH* DriverMatch = &PciDriver->Matches[MatchIndex];

                        if (PciInternalMatch(DriverMatch, &PciInfo)) {
                            if (PciDriver->Command) {
                                DEBUG(TEXT("%s matches %x:%x.%u"), PciDriver->Product, (INT)Bus,
                                    (INT)Device, (INT)Function);

                                U32 Result = PciDriver->Command(DF_PROBE, (UINT)(LPVOID)&PciInfo);
                                if (Result == DF_RETURN_SUCCESS) {
                                    PciDevice.Driver = (LPDRIVER)PciDriver;
                                    PciDriver->Command(DF_LOAD, 0);

                                    if (PciDriver->Attach) {
                                        LPPCI_DEVICE NewDev = PciDriver->Attach(&PciDevice);

                                        if (NewDev) {
                                            DEBUG(TEXT("Adding device %p (ID=%x) to list"), (LINEAR)NewDev, (INT)(NewDev->TypeID));
                                            ListAddItem(GetPCIDeviceList(), NewDev);
                                            DEBUG(TEXT("Attached %s to %x:%x.%u"), PciDriver->Product,
                                                (INT)Bus, (INT)Device, (INT)Function);

                                            goto NextFunction;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            NextFunction:
                (void)0;
            }
        }
    }

    DEBUG(TEXT("Bus scan complete"));
}

/***************************************************************************/

/**
 * @brief Checks whether a PCI device matches a driver's criteria.
 *
 * Compares vendor, device and class codes against the driver's match
 * structure.
 *
 * @param DriverMatch Matching criteria from the driver.
 * @param PciInfo     Device information to test.
 * @return Non-zero if the device matches.
 */

static int PciInternalMatch(const DRIVER_MATCH* DriverMatch, const PCI_INFO* PciInfo) {
    if (DriverMatch == NULL || PciInfo == NULL) return 0;

    if (DriverMatch->VendorID != PCI_ANY_ID && DriverMatch->VendorID != PciInfo->VendorID) return 0;
    if (DriverMatch->DeviceID != PCI_ANY_ID && DriverMatch->DeviceID != PciInfo->DeviceID) return 0;
    if (DriverMatch->BaseClass != PCI_ANY_CLASS && DriverMatch->BaseClass != PciInfo->BaseClass) return 0;
    if (DriverMatch->SubClass != PCI_ANY_CLASS && DriverMatch->SubClass != PciInfo->SubClass) return 0;
    if (DriverMatch->ProgIF != PCI_ANY_CLASS && DriverMatch->ProgIF != PciInfo->ProgIF) return 0;

    return 1;
}

/***************************************************************************/

/**
 * @brief Populates a PCI_INFO structure with data from configuration space.
 *
 * Reads common identification fields, BAR values and interrupt lines for a
 * given function.
 *
 * @param Bus      Bus number.
 * @param Device   Device number.
 * @param Function Function number.
 * @param PciInfo  Output structure to fill.
 */

static void PciFillFunctionInfo(U8 Bus, U8 Device, U8 Function, PCI_INFO* PciInfo) {
    U32 Index;

    PciInfo->Bus = Bus;
    PciInfo->Dev = Device;
    PciInfo->Func = Function;

    PciInfo->VendorID = PCI_Read16(Bus, Device, Function, PCI_CFG_VENDOR_ID);
    PciInfo->DeviceID = PCI_Read16(Bus, Device, Function, PCI_CFG_DEVICE_ID);

    PciInfo->BaseClass = PCI_Read8(Bus, Device, Function, PCI_CFG_BASECLASS);
    PciInfo->SubClass = PCI_Read8(Bus, Device, Function, PCI_CFG_SUBCLASS);
    PciInfo->ProgIF = PCI_Read8(Bus, Device, Function, PCI_CFG_PROG_IF);
    PciInfo->Revision = PCI_Read8(Bus, Device, Function, PCI_CFG_REVISION);

    for (Index = 0; Index < 6; Index++) {
        PciInfo->BAR[Index] = PCI_Read32(Bus, Device, Function, (U16)(PCI_CFG_BAR0 + Index * 4));
    }

    PciInfo->IRQLine = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_LINE);
    PciInfo->IRQLegacyPin = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_PIN);
}

/************************************************************************/

/**
 * @brief Decodes raw BAR values into physical addresses.
 *
 * Extracts base addresses from the BARs and stores them in the PCI_DEVICE
 * structure while resetting mapped pointers.
 *
 * @param PciInfo   Source information obtained from the device.
 * @param PciDevice Target device structure to update.
 */

static void PciDecodeBARs(const PCI_INFO* PciInfo, PCI_DEVICE* PciDevice) {
    U32 Index;
    for (Index = 0; Index < 6; Index++) {
        U32 BarValue = PciInfo->BAR[Index];
        if (PCI_BAR_IS_IO(BarValue)) {
            PciDevice->BARPhys[Index] = (BarValue & PCI_BAR_IO_MASK);
        } else {
            PciDevice->BARPhys[Index] = (BarValue & PCI_BAR_MEM_MASK);
        }
        PciDevice->BARMapped[Index] = NULL;
    }
}

/***************************************************************************/

/**
 * @brief Driver command handler for the PCI subsystem.
 *
 * DF_LOAD registers built-in drivers and scans the bus; DF_UNLOAD clears
 * readiness only.
 */
static UINT PCIDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD: {
            if ((PCIDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            extern PCI_DRIVER AHCIPCIDriver;
            extern PCI_DRIVER NVMePCIDriver;

            PCI_RegisterDriver(&E1000Driver);
            PCI_RegisterDriver(&RTL8139CPlusDriver);
            PCI_RegisterDriver(&RTL8139Driver);
            PCI_RegisterDriver(&RTL8169Driver);
            PCI_RegisterDriver(&AHCIPCIDriver);
            PCI_RegisterDriver(&NVMePCIDriver);
            PCI_RegisterDriver(&XHCIDriver);
            PCI_RegisterDriver(GraphicsPCIGetDisplayAttachDriver());
            PCI_ScanBus();

            PCIDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        }

        case DF_UNLOAD:
            if ((PCIDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            PCIDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(PCI_VER_MAJOR, PCI_VER_MINOR);
        case DF_ENUM_NEXT:
            return PCI_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Parameter);
        case DF_ENUM_PRETTY:
            return PCI_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/**
 * @brief Top-level PCI interrupt handler.
 *
 * Currently forwards interrupts to AHCI when initialized.
 */
void PCIHandler(void) {
    DEBUG(TEXT("Enter"));

    // For now, only handle AHCI interrupts if we have an AHCI device
    // TODO: Implement proper IRQ mapping for multiple PCI devices
    extern void AHCIInterruptHandler(void);
    extern BOOL AHCIIsInitialized(void);

    if (AHCIIsInitialized()) {
        AHCIInterruptHandler();
    }

    DEBUG(TEXT("Exit"));
}

/************************************************************************/

static U32 PCI_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) ||
        Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_PCI_DEVICE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return DF_RETURN_NO_MORE;
    }

    UINT MatchIndex = 0;
    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            if (MatchIndex == Next->Query->Index) {
                DRIVER_ENUM_PCI_DEVICE Data;
                MemorySet(&Data, 0, sizeof(Data));

                Data.Bus = Device->Info.Bus;
                Data.Dev = Device->Info.Dev;
                Data.Func = Device->Info.Func;
                Data.VendorID = Device->Info.VendorID;
                Data.DeviceID = Device->Info.DeviceID;
                Data.BaseClass = Device->Info.BaseClass;
                Data.SubClass = Device->Info.SubClass;
                Data.ProgIF = Device->Info.ProgIF;
                Data.Revision = Device->Info.Revision;

                MemorySet(Next->Item, 0, sizeof(DRIVER_ENUM_ITEM));
                Next->Item->Header.Size = sizeof(DRIVER_ENUM_ITEM);
                Next->Item->Header.Version = EXOS_ABI_VERSION;
                Next->Item->Domain = ENUM_DOMAIN_PCI_DEVICE;
                Next->Item->Index = Next->Query->Index;
                Next->Item->DataSize = sizeof(Data);
                MemoryCopy(Next->Item->Data, &Data, sizeof(Data));

                Next->Query->Index++;
                return DF_RETURN_SUCCESS;
            }
            MatchIndex++;
        }
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

static U32 PCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Pretty->Item->Domain != ENUM_DOMAIN_PCI_DEVICE ||
        Pretty->Item->DataSize < sizeof(DRIVER_ENUM_PCI_DEVICE)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    const DRIVER_ENUM_PCI_DEVICE* Data = (const DRIVER_ENUM_PCI_DEVICE*)Pretty->Item->Data;
    StringPrintFormat(Pretty->Buffer,
                      TEXT("PCI %x:%x.%u VID=%x DID=%x Class=%x Sub=%x ProgIF=%x Rev=%x"),
                      (U32)Data->Bus,
                      (U32)Data->Dev,
                      (U32)Data->Func,
                      (U32)Data->VendorID,
                      (U32)Data->DeviceID,
                      (U32)Data->BaseClass,
                      (U32)Data->SubClass,
                      (U32)Data->ProgIF,
                      (U32)Data->Revision);

    return DF_RETURN_SUCCESS;
}
