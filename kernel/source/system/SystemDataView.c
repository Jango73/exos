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


    System Data View

\************************************************************************/

#include "Arch.h"
#include "console/Console.h"
#include "text/CoreString.h"
#include "core/DriverGetters.h"
#include "drivers/platform/ACPI.h"
#include "drivers/interrupts/IOAPIC.h"
#include "drivers/input/Keyboard.h"
#include "drivers/interrupts/LocalAPIC.h"
#include "drivers/bus/PCI.h"
#include "drivers/storage/USBStorage.h"
#include "drivers/usb/XHCI-Internal.h"
#include "core/KernelData.h"
#include "network/Network.h"
#include "network/NetworkManager.h"
#include "process/Task.h"
#include "system/System.h"
#include "input/VKey.h"

/************************************************************************/

/************************************************************************/
// Macros

#define SYSTEM_DATA_VIEW_PAGE_COUNT 14
#define SYSTEM_DATA_VIEW_OUTPUT_BUFFER_SIZE 32768
#define SYSTEM_DATA_VIEW_OUTPUT_MAX_LINES 1024
#define SYSTEM_DATA_VIEW_VALUE_COLUMN 24

#define SYSTEM_DATA_VIEW_PIC1_COMMAND 0x20
#define SYSTEM_DATA_VIEW_PIC1_DATA 0x21
#define SYSTEM_DATA_VIEW_PIC2_COMMAND 0xA0
#define SYSTEM_DATA_VIEW_PIC2_DATA 0xA1
#define SYSTEM_DATA_VIEW_PIT_COMMAND 0x43
#define SYSTEM_DATA_VIEW_PIT_CHANNEL0 0x40

#define SYSTEM_DATA_VIEW_PCI_CLASS_MASS_STORAGE 0x01
#define SYSTEM_DATA_VIEW_PCI_SUBCLASS_SATA 0x06
#define SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_AHCI 0x01
#define SYSTEM_DATA_VIEW_PCI_SUBCLASS_NVM 0x08
#define SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_NVME 0x02

#define SYSTEM_DATA_VIEW_PCI_CLASS_SERIAL_BUS 0x0C
#define SYSTEM_DATA_VIEW_PCI_SUBCLASS_USB 0x03
#define SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_EHCI 0x20
#define SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_XHCI 0x30
#define SYSTEM_DATA_VIEW_XHCI_PLS_SHIFT 5
#define SYSTEM_DATA_VIEW_USB_MASS_STORAGE_SUBCLASS_SCSI 0x06
#define SYSTEM_DATA_VIEW_USB_MASS_STORAGE_PROTOCOL_BOT 0x50
#define SYSTEM_DATA_VIEW_USB_MASS_STORAGE_PROTOCOL_UAS 0x62

#define SYSTEM_DATA_VIEW_PCI_VENDOR_INTEL 0x8086
#define SYSTEM_DATA_VIEW_PCI_VENDOR_AMD 0x1002
#define SYSTEM_DATA_VIEW_PCI_VENDOR_NVIDIA 0x10DE
#define SYSTEM_DATA_VIEW_PCI_CLASS_BRIDGE 0x06

/************************************************************************/
// Type definitions

typedef struct tag_SYSTEM_DATA_VIEW_CONTEXT {
    STR TemporaryString[128];
    STR Buffer[SYSTEM_DATA_VIEW_OUTPUT_BUFFER_SIZE];
    UINT BufferLength;
    UINT LineCount;
    UINT LineOffsets[SYSTEM_DATA_VIEW_OUTPUT_MAX_LINES];
} SYSTEM_DATA_VIEW_CONTEXT, *LPSYSTEM_DATA_VIEW_CONTEXT;

typedef struct tag_SYSTEM_DATA_VIEW_PCI_INFO {
    U8 Bus;
    U8 Dev;
    U8 Func;
    U16 VendorID;
    U16 DeviceID;
    U8 BaseClass;
    U8 SubClass;
    U8 ProgIF;
    U8 Revision;
    U8 HeaderType;
    U8 IRQLine;
    U8 IRQLegacyPin;
    U32 BAR[6];
} SYSTEM_DATA_VIEW_PCI_INFO, *LPSYSTEM_DATA_VIEW_PCI_INFO;

typedef BOOL (*SYSTEM_DATA_VIEW_PCI_VISITOR)(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData);

typedef struct tag_SYSTEM_DATA_VIEW_PCI_LIST_STATE {
    UINT Index;
} SYSTEM_DATA_VIEW_PCI_LIST_STATE, *LPSYSTEM_DATA_VIEW_PCI_LIST_STATE;

typedef struct tag_SYSTEM_DATA_VIEW_PCI_STORAGE_STATE {
    UINT Index;
    UINT Count;
} SYSTEM_DATA_VIEW_PCI_STORAGE_STATE, *LPSYSTEM_DATA_VIEW_PCI_STORAGE_STATE;

typedef struct tag_SYSTEM_DATA_VIEW_PCI_VMD_STATE {
    UINT Index;
    UINT Count;
} SYSTEM_DATA_VIEW_PCI_VMD_STATE, *LPSYSTEM_DATA_VIEW_PCI_VMD_STATE;

typedef struct tag_SYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE {
    UINT Index;
    UINT Count;
    UINT AttachedCount;
} SYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE, *LPSYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE;

typedef struct tag_SYSTEM_DATA_VIEW_NETWORK_STATE {
    UINT Index;
    UINT Count;
    UINT AttachedCount;
    UINT ReadyCount;
} SYSTEM_DATA_VIEW_NETWORK_STATE, *LPSYSTEM_DATA_VIEW_NETWORK_STATE;

/************************************************************************/

/**
 * @brief Reset the System Data View output buffer.
 *
 * @param Context Output context.
 */
static void SystemDataViewOutputReset(LPSYSTEM_DATA_VIEW_CONTEXT Context) {
    Context->BufferLength = 0;
    Context->LineCount = 1;
    Context->LineOffsets[0] = 0;
}

/************************************************************************/

/**
 * @brief Append a character to the System Data View output buffer.
 *
 * @param Context Output context.
 * @param Character Character to append.
 */
static void SystemDataViewAppendChar(LPSYSTEM_DATA_VIEW_CONTEXT Context, STR Character) {
    if (Context->BufferLength + 1 >= SYSTEM_DATA_VIEW_OUTPUT_BUFFER_SIZE) {
        return;
    }

    Context->Buffer[Context->BufferLength] = Character;
    Context->BufferLength++;

    if (Character == '\n' && Context->LineCount < SYSTEM_DATA_VIEW_OUTPUT_MAX_LINES) {
        Context->LineOffsets[Context->LineCount] = Context->BufferLength;
        Context->LineCount++;
    }
}

/************************************************************************/

/**
 * @brief Append a string to the System Data View output buffer.
 *
 * @param Context Output context.
 * @param String Text to append.
 */
static void SystemDataViewWriteString(LPSYSTEM_DATA_VIEW_CONTEXT Context, LPCSTR String) {
    while (*String) {
        SystemDataViewAppendChar(Context, (STR)(*String++));
    }
}

/************************************************************************/

/**
 * @brief Append formatted output to the System Data View output buffer.
 *
 * @param Context Output context.
 * @param Format Format string.
 */
static void SystemDataViewWriteFormatRaw(LPSYSTEM_DATA_VIEW_CONTEXT Context, LPCSTR Format, ...) {
    VarArgList Arguments;

    VarArgStart(Arguments, Format);
    StringPrintFormatArgs(Context->TemporaryString, Format, Arguments);
    VarArgEnd(Arguments);

    SystemDataViewWriteString(Context, Context->TemporaryString);
}

/************************************************************************/

/**
 * @brief Append spacing to the System Data View output buffer.
 *
 * @param Context Output context.
 * @param Count Number of spaces.
 */
static void SystemDataViewWritePadding(LPSYSTEM_DATA_VIEW_CONTEXT Context, UINT Count) {
    for (UINT Index = 0; Index < Count; Index++) {
        SystemDataViewAppendChar(Context, ' ');
    }
}

/************************************************************************/

/**
 * @brief Append a formatted label and value aligned to a column.
 *
 * @param Context Output context.
 * @param ValueColumn Column where values should start.
 * @param Label Label text.
 * @param ValueFormat Value format string.
 */
static void SystemDataViewWriteFormat(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, UINT ValueColumn, LPCSTR Label, LPCSTR ValueFormat, ...) {
    VarArgList Arguments;
    UINT LabelLength = StringLength(Label);
    UINT Padding = 1;

    SystemDataViewWriteString(Context, Label);
    if (ValueColumn > LabelLength) {
        Padding = ValueColumn - LabelLength;
    }
    SystemDataViewWritePadding(Context, Padding);

    VarArgStart(Arguments, ValueFormat);
    StringPrintFormatArgs(Context->TemporaryString, ValueFormat, Arguments);
    VarArgEnd(Arguments);

    SystemDataViewWriteString(Context, Context->TemporaryString);
}

/************************************************************************/

/**
 * @brief Draw the System Data View page header.
 *
 * @param Context Output context.
 * @param Title Page title.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageHeader(LPSYSTEM_DATA_VIEW_CONTEXT Context, LPCSTR Title, U8 PageIndex) {
    SystemDataViewWriteFormatRaw(Context, TEXT("System Data View\n"));
    SystemDataViewWriteFormatRaw(
        Context, TEXT("Page %u/%u: %s\n"), (U32)(PageIndex + 1), (U32)SYSTEM_DATA_VIEW_PAGE_COUNT, Title);
    SystemDataViewWriteString(Context, TEXT("-------------------------------------------------------------\n"));
}

/************************************************************************/

/**
 * @brief Draw the System Data View page footer.
 *
 * @param Context Output context.
 */
static void SystemDataViewDrawFooter(LPSYSTEM_DATA_VIEW_CONTEXT Context) {
    SystemDataViewWriteString(Context, TEXT("-------------------------------------------------------------\n"));
    SystemDataViewWriteString(Context, TEXT("[<-] Previous page  |  [->] Next page  |  [Esc] Continue\n"));
    SystemDataViewWriteString(Context, TEXT("[Up/Down] Scroll\n"));
}

/************************************************************************/

/**
 * @brief Render buffered output on the console.
 *
 * @param Context Output context.
 * @param ScrollOffset Scroll offset in lines.
 * @param ScreenRows Number of rows to render.
 */
static void SystemDataViewRender(LPSYSTEM_DATA_VIEW_CONTEXT Context, UINT ScrollOffset, UINT ScreenRows) {
    for (UINT Row = 0; Row < ScreenRows; Row++) {
        UINT LineIndex = ScrollOffset + Row;
        UINT Start = 0;
        UINT End = 0;

        if (LineIndex >= Context->LineCount) {
            ConsolePrintLine(Row, 0, TEXT(""), 0);
            continue;
        }

        Start = Context->LineOffsets[LineIndex];
        if ((LineIndex + 1) < Context->LineCount) {
            End = Context->LineOffsets[LineIndex + 1];
        } else {
            End = Context->BufferLength;
        }

        while (End > Start && (Context->Buffer[End - 1] == '\n' || Context->Buffer[End - 1] == '\r')) {
            End--;
        }

        ConsolePrintLine(Row, 0, &Context->Buffer[Start], End - Start);
    }
}

/************************************************************************/

/**
 * @brief Check if a PCI device matches class criteria.
 *
 * @param Device PCI device to evaluate.
 * @param BaseClass Base class code.
 * @param SubClass Subclass code.
 * @param ProgrammingInterface Programming interface code.
 * @return TRUE when the device matches.
 */
static BOOL SystemDataViewPciMatch(LPPCI_DEVICE Device, U8 BaseClass, U8 SubClass, U8 ProgrammingInterface) {
    if (Device == NULL) {
        return FALSE;
    }

    if (BaseClass != PCI_ANY_CLASS && Device->Info.BaseClass != BaseClass) {
        return FALSE;
    }

    if (SubClass != PCI_ANY_CLASS && Device->Info.SubClass != SubClass) {
        return FALSE;
    }

    if (ProgrammingInterface != PCI_ANY_CLASS && Device->Info.ProgIF != ProgrammingInterface) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Find the first PCI controller matching class criteria.
 *
 * @param BaseClass Base class code.
 * @param SubClass Subclass code.
 * @param ProgrammingInterface Programming interface code.
 * @param FirstDeviceOut Output pointer for the first match.
 * @param DeviceCountOut Output count of matching devices.
 * @return TRUE when at least one device is found.
 */
static BOOL SystemDataViewFindPciController(
    U8 BaseClass, U8 SubClass, U8 ProgrammingInterface, LPPCI_DEVICE* FirstDeviceOut, UINT* DeviceCountOut) {
    LPLIST DeviceList = GetPCIDeviceList();
    LPPCI_DEVICE FirstDevice = NULL;
    UINT DeviceCount = 0;

    if (DeviceList != NULL) {
        for (LPLISTNODE Node = DeviceList->First; Node != NULL; Node = Node->Next) {
            LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;
            SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                if (SystemDataViewPciMatch(Device, BaseClass, SubClass, ProgrammingInterface)) {
                    DeviceCount++;
                    if (FirstDevice == NULL) {
                        FirstDevice = Device;
                    }
                }
            }
        }
    }

    if (FirstDeviceOut != NULL) {
        *FirstDeviceOut = FirstDevice;
    }
    if (DeviceCountOut != NULL) {
        *DeviceCountOut = DeviceCount;
    }

    return (FirstDevice != NULL);
}

/************************************************************************/

/**
 * @brief Read a PIC register using an OCW3 command.
 *
 * @param CommandPort PIC command port.
 * @param Command OCW3 command value.
 * @return Register value.
 */
static U8 SystemDataViewReadPicRegister(U32 CommandPort, U32 Command) {
    OutPortByte(CommandPort, Command);
    return (U8)InPortByte(CommandPort);
}

/************************************************************************/

/**
 * @brief Read the PIT counter for channel 0.
 *
 * @return Counter value.
 */
static U16 SystemDataViewReadPitCounter0(void) {
    OutPortByte(SYSTEM_DATA_VIEW_PIT_COMMAND, 0x00);
    U8 Low = (U8)InPortByte(SYSTEM_DATA_VIEW_PIT_CHANNEL0);
    U8 High = (U8)InPortByte(SYSTEM_DATA_VIEW_PIT_CHANNEL0);
    return (U16)((U16)Low | ((U16)High << 8));
}

/************************************************************************/

/**
 * @brief Read the PIT status for channel 0.
 *
 * @return Status byte.
 */
static U8 SystemDataViewReadPitStatus0(void) {
    OutPortByte(SYSTEM_DATA_VIEW_PIT_COMMAND, 0xE2);
    return (U8)InPortByte(SYSTEM_DATA_VIEW_PIT_CHANNEL0);
}

/************************************************************************/

/**
 * @brief Read IO APIC redirection entry for a global interrupt.
 *
 * @param GlobalInterrupt Global interrupt number.
 * @param Low Output low dword.
 * @param High Output high dword.
 * @return TRUE when the entry is read.
 */
static BOOL SystemDataViewReadIoApicRedirection(U32 GlobalInterrupt, U32* Low, U32* High) {
    LPIOAPIC_CONFIG Config = GetIOAPICConfig();
    if (Config == NULL || Config->Initialized == FALSE) {
        return FALSE;
    }

    for (UINT Index = 0; Index < Config->ControllerCount; Index++) {
        LPIOAPIC_CONTROLLER Controller = GetIOAPICController(Index);
        if (Controller == NULL || Controller->Present == FALSE) {
            continue;
        }

        U32 EntryCount = (U32)(Controller->MaxRedirectionEntry + 1);
        if (GlobalInterrupt < Controller->GlobalInterruptBase) {
            continue;
        }

        U32 Entry = GlobalInterrupt - Controller->GlobalInterruptBase;
        if (Entry >= EntryCount) {
            continue;
        }

        IO_APIC_REDIRECTION_ENTRY Redirection;
        if (ReadRedirectionEntry(Index, (U8)Entry, &Redirection)) {
            if (Low != NULL) {
                *Low = Redirection.Low;
            }
            if (High != NULL) {
                *High = Redirection.High;
            }
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Compute IO APIC redirection entry count for a controller.
 *
 * @param ControllerIndex IO APIC controller index.
 * @return Entry count.
 */
static UINT SystemDataViewGetIoApicRedirectionCount(UINT ControllerIndex) {
    LPIOAPIC_CONTROLLER Controller = GetIOAPICController(ControllerIndex);
    if (Controller == NULL || Controller->Present == FALSE) {
        return 0;
    }

    if (Controller->MaxRedirectionEntry != 0) {
        return (UINT)(Controller->MaxRedirectionEntry + 1);
    }

    U32 VersionReg = ReadIOAPICRegister(ControllerIndex, IOAPIC_REG_VER);
    return (UINT)(((VersionReg >> 16) & 0xFF) + 1);
}

/************************************************************************/

/**
 * @brief Draw the ACPI page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageAcpi(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPACPI_CONFIG Config = GetACPIConfig();

    SystemDataViewDrawPageHeader(Context, TEXT("ACPI MADT"), PageIndex);

    if (Config == NULL || Config->Valid == FALSE) {
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("ACPI"), TEXT("Not Available\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Use Local APIC"),
        TEXT("%s\n"),
        Config->UseLocalApic ? TEXT("Yes") : TEXT("No"));
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Use IO APIC"),
        TEXT("%s\n"),
        Config->UseIoApic ? TEXT("Yes") : TEXT("No"));
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Local APIC Address"),
        TEXT("%p\n"),
        (LPVOID)(LINEAR)Config->LocalApicAddress);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Local APIC Count"), TEXT("%u\n"), (U32)Config->LocalApicCount);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IO APIC Count"), TEXT("%u\n"), (U32)Config->IoApicCount);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Interrupt Overrides"),
        TEXT("%u\n"),
        (U32)Config->InterruptOverrideCount);

    for (UINT Index = 0; Index < Config->LocalApicCount; Index++) {
        LPLOCAL_APIC_INFO Info = GetLocalApicInfo(Index);
        STR Label[32];

        if (Info == NULL) {
            continue;
        }

        StringPrintFormat(Label, TEXT("Local APIC %u"), (U32)Index);
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Processor=%u Apic=%u Flags=%x\n"),
            (U32)Info->ProcessorId,
            (U32)Info->ApicId,
            (U32)Info->Flags);
    }

    for (UINT Index = 0; Index < Config->IoApicCount; Index++) {
        LPIO_APIC_INFO Info = GetIOApicInfo(Index);
        STR Label[32];

        if (Info == NULL) {
            continue;
        }

        StringPrintFormat(Label, TEXT("IO APIC %u"), (U32)Index);
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Identifier=%u Address=%p Global Interrupt Base=%u\n"),
            (U32)Info->IoApicId,
            (LPVOID)(LINEAR)Info->IoApicAddress,
            (U32)Info->GlobalSystemInterruptBase);
    }

    for (UINT Index = 0; Index < Config->InterruptOverrideCount; Index++) {
        LPINTERRUPT_OVERRIDE_INFO Info = GetInterruptOverrideInfo(Index);
        STR Label[32];

        if (Info == NULL) {
            continue;
        }

        StringPrintFormat(Label, TEXT("Override %u"), (U32)Index);
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Bus=%u Source=%u Global Interrupt=%u Flags=%x\n"),
            (U32)Info->Bus,
            (U32)Info->Source,
            (U32)Info->GlobalSystemInterrupt,
            (U32)Info->Flags);
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw PIC, PIT, and IO APIC page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPagePicPitIoApic(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    U8 Mask1 = (U8)InPortByte(SYSTEM_DATA_VIEW_PIC1_DATA);
    U8 Mask2 = (U8)InPortByte(SYSTEM_DATA_VIEW_PIC2_DATA);
    U8 InterruptRequest1 = SystemDataViewReadPicRegister(SYSTEM_DATA_VIEW_PIC1_COMMAND, 0x0A);
    U8 InterruptRequest2 = SystemDataViewReadPicRegister(SYSTEM_DATA_VIEW_PIC2_COMMAND, 0x0A);
    U8 InService1 = SystemDataViewReadPicRegister(SYSTEM_DATA_VIEW_PIC1_COMMAND, 0x0B);
    U8 InService2 = SystemDataViewReadPicRegister(SYSTEM_DATA_VIEW_PIC2_COMMAND, 0x0B);
    U16 PITCounter = SystemDataViewReadPitCounter0();
    U8 PITStatus = SystemDataViewReadPitStatus0();
    U8 ImcrValue = 0;

    OutPortByte(0x22, 0x70);
    ImcrValue = (U8)InPortByte(0x23);

    SystemDataViewDrawPageHeader(Context, TEXT("PIC / PIT / IO APIC"), PageIndex);

    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC Mask1"), TEXT("%x\n"), Mask1);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC Mask2"), TEXT("%x\n"), Mask2);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC IRR1"), TEXT("%x\n"), InterruptRequest1);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC IRR2"), TEXT("%x\n"), InterruptRequest2);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC ISR1"), TEXT("%x\n"), InService1);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIC ISR2"), TEXT("%x\n"), InService2);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IMCR Value"), TEXT("%x\n"), ImcrValue);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIT Counter"), TEXT("%u\n"), (U32)PITCounter);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("PIT Status"), TEXT("%x\n"), PITStatus);

    {
        LPIOAPIC_CONFIG IOApicConfig = GetIOAPICConfig();
        if (IOApicConfig == NULL || IOApicConfig->Initialized == FALSE || IOApicConfig->ControllerCount == 0) {
            SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IO APIC"), TEXT("Not Available\n"));
        } else {
            U32 IdentifierReg = ReadIOAPICRegister(0, IOAPIC_REG_ID);
            U32 VersionReg = ReadIOAPICRegister(0, IOAPIC_REG_VER);
            U32 RedirectionLow = ReadIOAPICRegister(0, IOAPIC_REG_REDTBL_BASE + (2 * 2));
            U32 RedirectionHigh = ReadIOAPICRegister(0, IOAPIC_REG_REDTBL_BASE + (2 * 2) + 1);
            LPIOAPIC_CONTROLLER Controller = GetIOAPICController(0);

            if (Controller != NULL) {
                SystemDataViewWriteFormat(
                    Context,
                    SYSTEM_DATA_VIEW_VALUE_COLUMN,
                    TEXT("IO APIC Base"),
                    TEXT("%p\n"),
                    (LPVOID)(LINEAR)Controller->PhysicalAddress);
            }
            SystemDataViewWriteFormat(
                Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IO APIC ID"), TEXT("%x\n"), IdentifierReg);
            SystemDataViewWriteFormat(
                Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IO APIC Version"), TEXT("%x\n"), VersionReg);
            SystemDataViewWriteFormat(
                Context,
                SYSTEM_DATA_VIEW_VALUE_COLUMN,
                TEXT("IO APIC Redirection[2].Low"),
                TEXT("%x\n"),
                RedirectionLow);
            SystemDataViewWriteFormat(
                Context,
                SYSTEM_DATA_VIEW_VALUE_COLUMN,
                TEXT("IO APIC Redirection[2].High"),
                TEXT("%x\n"),
                RedirectionHigh);
        }
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw Local APIC page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageLocalApic(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPLOCAL_APIC_CONFIG Config = GetLocalAPICConfig();

    SystemDataViewDrawPageHeader(Context, TEXT("Local APIC"), PageIndex);

    if (Config == NULL || Config->Present == FALSE) {
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Local APIC"), TEXT("Not Available\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    U32 IdentifierReg = ReadLocalAPICRegister(LOCAL_APIC_ID);
    U32 VersionReg = ReadLocalAPICRegister(LOCAL_APIC_VERSION);
    U32 Spurious = ReadLocalAPICRegister(LOCAL_APIC_SPURIOUS_IV);
    U32 LvtTimerRegister = ReadLocalAPICRegister(LOCAL_APIC_LVT_TIMER);
    U32 LvtLint0Register = ReadLocalAPICRegister(LOCAL_APIC_LVT_LINT0);
    U32 LvtLint1Register = ReadLocalAPICRegister(LOCAL_APIC_LVT_LINT1);

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Base Address"),
        TEXT("%p\n"),
        (LPVOID)(LINEAR)Config->BaseAddress);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("APIC ID"), TEXT("%x\n"), IdentifierReg);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("APIC Version"), TEXT("%x\n"), VersionReg);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Spurious Vector"), TEXT("%x\n"), Spurious);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("LVT Timer"), TEXT("%x\n"), LvtTimerRegister);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("LVT LINT0"), TEXT("%x\n"), LvtLint0Register);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("LVT LINT1"), TEXT("%x\n"), LvtLint1Register);

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Write PCI controller routing info on the interrupt routing page.
 *
 * @param Context Output context.
 * @param Name Controller name.
 * @param BaseClass PCI base class.
 * @param SubClass PCI subclass.
 * @param ProgrammingInterface PCI programming interface.
 */
static void SystemDataViewWriteControllerRouting(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, LPCSTR Name, U8 BaseClass, U8 SubClass, U8 ProgrammingInterface) {
    LPPCI_DEVICE Controller = NULL;
    UINT ControllerCount = 0;
    STR Label[32];

    BOOL Found =
        SystemDataViewFindPciController(BaseClass, SubClass, ProgrammingInterface, &Controller, &ControllerCount);

    StringPrintFormat(Label, TEXT("%s Controllers"), Name);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, Label, TEXT("%u\n"), (U32)ControllerCount);

    if (Found == FALSE || Controller == NULL) {
        StringPrintFormat(Label, TEXT("%s Interrupt Route"), Name);
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, Label, TEXT("Not Found\n"));
        return;
    }

    StringPrintFormat(Label, TEXT("%s Interrupt Route"), Name);

    if (Controller->Info.IRQLine == 0xFF) {
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Line=Not Available Pin=%u\n"),
            (U32)Controller->Info.IRQLegacyPin);
        return;
    }

    U32 RedirectionLow = 0;
    U32 RedirectionHigh = 0;
    BOOL HasRedirection =
        SystemDataViewReadIoApicRedirection((U32)Controller->Info.IRQLine, &RedirectionLow, &RedirectionHigh);

    if (HasRedirection) {
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Line=%u Pin=%u Redirection=%x/%x\n"),
            (U32)Controller->Info.IRQLine,
            (U32)Controller->Info.IRQLegacyPin,
            RedirectionLow,
            RedirectionHigh);
    } else {
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("Line=%u Pin=%u Redirection=Not Available\n"),
            (U32)Controller->Info.IRQLine,
            (U32)Controller->Info.IRQLegacyPin);
    }
}

/************************************************************************/

/**
 * @brief Draw the interrupt routing page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageInterruptRouting(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPIOAPIC_CONFIG Config = GetIOAPICConfig();

    SystemDataViewDrawPageHeader(Context, TEXT("Interrupt Routing"), PageIndex);

    if (Config == NULL || Config->Initialized == FALSE || Config->ControllerCount == 0) {
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IO APIC"), TEXT("Not Available\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("IO APIC Controllers"),
        TEXT("%u\n"),
        (U32)Config->ControllerCount);

    for (UINT Index = 0; Index < Config->ControllerCount; Index++) {
        LPIOAPIC_CONTROLLER Controller = GetIOAPICController(Index);
        STR Label[32];

        if (Controller == NULL || Controller->Present == FALSE) {
            continue;
        }

        StringPrintFormat(Label, TEXT("IO APIC %u Base"), (U32)Index);
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, Label, TEXT("%p\n"), (LPVOID)(LINEAR)Controller->PhysicalAddress);

        StringPrintFormat(Label, TEXT("IO APIC %u Global Base"), (U32)Index);
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, Label, TEXT("%u\n"), (U32)Controller->GlobalInterruptBase);

        StringPrintFormat(Label, TEXT("IO APIC %u Entries"), (U32)Index);
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("%u\n"),
            (U32)SystemDataViewGetIoApicRedirectionCount(Index));
    }

    SystemDataViewWriteControllerRouting(
        Context,
        TEXT("AHCI"),
        SYSTEM_DATA_VIEW_PCI_CLASS_MASS_STORAGE,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_SATA,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_AHCI);

    SystemDataViewWriteControllerRouting(
        Context,
        TEXT("EHCI"),
        SYSTEM_DATA_VIEW_PCI_CLASS_SERIAL_BUS,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_USB,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_EHCI);

    SystemDataViewWriteControllerRouting(
        Context,
        TEXT("xHCI"),
        SYSTEM_DATA_VIEW_PCI_CLASS_SERIAL_BUS,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_USB,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_XHCI);

    for (UINT ControllerIndex = 0; ControllerIndex < Config->ControllerCount; ControllerIndex++) {
        UINT EntryCount = SystemDataViewGetIoApicRedirectionCount(ControllerIndex);
        LPIOAPIC_CONTROLLER Controller = GetIOAPICController(ControllerIndex);

        if (Controller == NULL || Controller->Present == FALSE) {
            continue;
        }

        for (UINT Entry = 0; Entry < EntryCount; Entry++) {
            IO_APIC_REDIRECTION_ENTRY Redirection;
            STR Label[32];

            if (!ReadRedirectionEntry(ControllerIndex, (U8)Entry, &Redirection)) {
                continue;
            }

            U32 Vector = Redirection.Low & 0xFF;
            U32 Delivery = (Redirection.Low >> 8) & 0x7;
            U32 DestinationMode = (Redirection.Low >> 11) & 0x1;
            U32 Polarity = (Redirection.Low >> 13) & 0x1;
            U32 Trigger = (Redirection.Low >> 15) & 0x1;
            U32 Mask = (Redirection.Low >> 16) & 0x1;
            U32 Destination = (Redirection.High >> 24) & 0xFF;
            U32 GlobalLine = Controller->GlobalInterruptBase + Entry;

            StringPrintFormat(Label, TEXT("Redirection %u"), GlobalLine);
            SystemDataViewWriteFormat(
                Context,
                SYSTEM_DATA_VIEW_VALUE_COLUMN,
                Label,
                TEXT("Vector=%x Delivery=%x DestinationMode=%x Polarity=%x Trigger=%x Mask=%x Destination=%x\n"),
                Vector,
                Delivery,
                DestinationMode,
                Polarity,
                Trigger,
                Mask,
                Destination);
        }
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw a PCI controller page for System Data View.
 *
 * @param Context Output context.
 * @param Title Page title.
 * @param BaseClass PCI base class.
 * @param SubClass PCI subclass.
 * @param ProgrammingInterface PCI programming interface.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPciControllerPage(
    LPSYSTEM_DATA_VIEW_CONTEXT Context,
    LPCSTR Title,
    U8 BaseClass,
    U8 SubClass,
    U8 ProgrammingInterface,
    U8 PageIndex) {
    LPPCI_DEVICE Controller = NULL;
    UINT ControllerCount = 0;
    BOOL Found =
        SystemDataViewFindPciController(BaseClass, SubClass, ProgrammingInterface, &Controller, &ControllerCount);

    SystemDataViewDrawPageHeader(Context, Title, PageIndex);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Controllers Found"), TEXT("%u\n"), (U32)ControllerCount);

    if (Found == FALSE || Controller == NULL) {
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("First Controller"), TEXT("Not Found\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Bus/Device/Function"),
        TEXT("%u/%u/%u\n"),
        (U32)Controller->Info.Bus,
        (U32)Controller->Info.Dev,
        (U32)Controller->Info.Func);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Vendor Identifier"),
        TEXT("%x\n"),
        (U32)Controller->Info.VendorID);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Device Identifier"),
        TEXT("%x\n"),
        (U32)Controller->Info.DeviceID);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Class Code"), TEXT("%x\n"), (U32)Controller->Info.BaseClass);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Subclass"), TEXT("%x\n"), (U32)Controller->Info.SubClass);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Programming Interface"),
        TEXT("%x\n"),
        (U32)Controller->Info.ProgIF);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("BAR5 Base"),
        TEXT("%p\n"),
        (LPVOID)(LINEAR)Controller->BARPhys[5]);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Interrupt Line"), TEXT("%u\n"), (U32)Controller->Info.IRQLine);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Interrupt Pin"),
        TEXT("%u\n"),
        (U32)Controller->Info.IRQLegacyPin);

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw the AHCI page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageAhci(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SystemDataViewDrawPciControllerPage(
        Context,
        TEXT("AHCI"),
        SYSTEM_DATA_VIEW_PCI_CLASS_MASS_STORAGE,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_SATA,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_AHCI,
        PageIndex);
}

/************************************************************************/

/**
 * @brief Draw the EHCI page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageEhci(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SystemDataViewDrawPciControllerPage(
        Context,
        TEXT("EHCI"),
        SYSTEM_DATA_VIEW_PCI_CLASS_SERIAL_BUS,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_USB,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_EHCI,
        PageIndex);
}

/************************************************************************/

/**
 * @brief Count active xHCI slots attached to one controller.
 *
 * @param Device xHCI controller.
 * @return Active slot count.
 */
static U32 SystemDataViewCountActiveXhciSlots(LPXHCI_DEVICE Device) {
    U8 SlotSeen[256];
    U32 ActiveCount = 0;
    LPLIST UsbDeviceList = GetUsbDeviceList();

    if (Device == NULL || UsbDeviceList == NULL) {
        return 0;
    }

    MemorySet(SlotSeen, 0, sizeof(SlotSeen));

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)Node;
        SAFE_USE_VALID_ID(UsbDevice, KOID_USBDEVICE) {
            if (UsbDevice->Controller != Device || !UsbDevice->Present || UsbDevice->SlotId == 0) {
                continue;
            }
            if (SlotSeen[UsbDevice->SlotId] != 0) {
                continue;
            }
            SlotSeen[UsbDevice->SlotId] = 1;
            ActiveCount++;
        }
    }

    return ActiveCount;
}

/************************************************************************/

/**
 * @brief Count present and total USB mass storage entries.
 *
 * @param PresentOut Receives present entry count.
 * @param TotalOut Receives total entry count.
 */
static void SystemDataViewCountUsbStorage(UINT* PresentOut, UINT* TotalOut) {
    UINT Present = 0;
    UINT Total = 0;
    LPLIST UsbStorageList = GetUsbStorageList();

    if (UsbStorageList != NULL) {
        for (LPLISTNODE Node = UsbStorageList->First; Node != NULL; Node = Node->Next) {
            LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
            Total++;
            if (Entry != NULL && Entry->Present != FALSE) {
                Present++;
            }
        }
    }

    if (PresentOut != NULL) {
        *PresentOut = Present;
    }
    if (TotalOut != NULL) {
        *TotalOut = Total;
    }
}

/************************************************************************/

/**
 * @brief Build a short mass-storage hint for one USB device.
 *
 * @param UsbDevice USB device.
 * @return Hint string.
 */
static LPCSTR SystemDataViewXhciMassStorageHint(LPXHCI_USB_DEVICE UsbDevice) {
    LPXHCI_USB_CONFIGURATION Config;
    LPLIST InterfaceList;

    if (UsbDevice == NULL || !UsbDevice->Present) {
        return TEXT("-");
    }

    Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return TEXT("NoCfg");
    }

    InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return TEXT("NoIf");
    }

    for (LPLISTNODE IfNode = InterfaceList->First; IfNode != NULL; IfNode = IfNode->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)IfNode;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass != USB_CLASS_MASS_STORAGE) {
            continue;
        }
        if (Interface->InterfaceSubClass != SYSTEM_DATA_VIEW_USB_MASS_STORAGE_SUBCLASS_SCSI) {
            return TEXT("MS-Sub");
        }
        if (Interface->InterfaceProtocol == SYSTEM_DATA_VIEW_USB_MASS_STORAGE_PROTOCOL_BOT) {
            return TEXT("MS-BOT");
        }
        if (Interface->InterfaceProtocol == SYSTEM_DATA_VIEW_USB_MASS_STORAGE_PROTOCOL_UAS) {
            return TEXT("MS-UAS");
        }
        return TEXT("MS-Proto");
    }

    return TEXT("NoMS");
}

/************************************************************************/

/**
 * @brief Draw detailed xHCI controller and port state.
 *
 * @param Context Output context.
 * @param Device xHCI controller.
 */
static void SystemDataViewDrawXhciDetails(LPSYSTEM_DATA_VIEW_CONTEXT Context, LPXHCI_DEVICE Device) {
    U32 Usbcmd = 0;
    U32 Usbsts = 0;
    U32 Config = 0;
    U32 CrcrLow = 0;
    U32 CrcrHigh = 0;
    U32 DcbaapLow = 0;
    U32 DcbaapHigh = 0;
    U32 DcbaaEntry0Low = 0;
    U32 DcbaaEntry0High = 0;
    U32 Iman = 0;
    U32 Imod = 0;
    U32 Erstsz = 0;
    U32 ErdpLow = 0;
    U32 ErdpHigh = 0;
    U32 ErstbaLow = 0;
    U32 ErstbaHigh = 0;
    U16 PciCommand = 0;
    U16 PciStatus = 0;
    U32 ConnectedPorts = 0;
    U32 EnabledPorts = 0;
    U32 ErrorPorts = 0;
    U32 ActiveSlots = 0;
    UINT UsbStoragePresent = 0;
    UINT UsbStorageTotal = 0;
    LPDRIVER UsbMassStorageDriver = USBStorageGetDriver();

    if (Context == NULL || Device == NULL) {
        return;
    }

    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Driver Attached"), TEXT("Yes\n"));
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("MMIO Base/Size"),
        TEXT("%p / %u\n"),
        (LPVOID)Device->MmioBase,
        Device->MmioSize);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("OP/RT/DB Base"),
        TEXT("%p / %p / %p\n"),
        (LPVOID)Device->OpBase,
        (LPVOID)Device->RuntimeBase,
        (LPVOID)Device->DoorbellBase);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("HCI Version"), TEXT("%x\n"), (U32)Device->HciVersion);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Cap Length"), TEXT("%u\n"), (U32)Device->CapLength);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Ports/Slots/Context"),
        TEXT("%u / %u / %u\n"),
        (U32)Device->MaxPorts,
        (U32)Device->MaxSlots,
        (U32)Device->ContextSize);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("HCSPARAMS2/Scratchpads"),
        TEXT("%x / %u\n"),
        Device->HcsParams2,
        (U32)Device->MaxScratchpadBuffers);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Interrupt"),
        TEXT("Reg=%u En=%u Count=%u Slot=%u\n"),
        Device->InterruptRegistered ? 1U : 0U,
        Device->InterruptEnabled ? 1U : 0U,
        Device->InterruptCount,
        (U32)Device->InterruptSlot);
    PciCommand = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_COMMAND);
    PciStatus = PCI_Read16(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, PCI_CFG_STATUS);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("PCI Command/Status"),
        TEXT("%x / %x\n"),
        (U32)PciCommand,
        (U32)PciStatus);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("PCI Status Decode"),
        TEXT("DetPar=%u SERR#=%u MA=%u TARecv=%u TASent=%u MDP=%u DEVSEL=%u INT=%u\n"),
        (PciStatus & 0x8000) ? 1U : 0U,
        (PciStatus & 0x4000) ? 1U : 0U,
        (PciStatus & 0x2000) ? 1U : 0U,
        (PciStatus & 0x1000) ? 1U : 0U,
        (PciStatus & 0x0800) ? 1U : 0U,
        (PciStatus & 0x0100) ? 1U : 0U,
        (U32)((PciStatus >> 9) & 0x3),
        (PciStatus & 0x0008) ? 1U : 0U);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Ring Indexes"),
        TEXT("Cmd=%u/%u Event=%u/%u\n"),
        Device->CommandRingEnqueueIndex,
        Device->CommandRingCycleState,
        Device->EventRingDequeueIndex,
        Device->EventRingCycleState);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Completion Queue"), TEXT("%u\n"), Device->CompletionCount);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("USBStorage Driver"),
        TEXT("Ready=%u\n"),
        (UsbMassStorageDriver != NULL && (UsbMassStorageDriver->Flags & DRIVER_FLAG_READY) != 0) ? 1U : 0U);
    SystemDataViewCountUsbStorage(&UsbStoragePresent, &UsbStorageTotal);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("USB Storage Entries"),
        TEXT("%u/%u\n"),
        (U32)UsbStoragePresent,
        (U32)UsbStorageTotal);

    if (Device->OpBase != 0) {
        Usbcmd = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
        Usbsts = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
        Config = XHCI_Read32(Device->OpBase, XHCI_OP_CONFIG);
        CrcrLow = XHCI_Read32(Device->OpBase, XHCI_OP_CRCR);
        CrcrHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4));
        DcbaapLow = XHCI_Read32(Device->OpBase, XHCI_OP_DCBAAP);
        DcbaapHigh = XHCI_Read32(Device->OpBase, (U32)(XHCI_OP_DCBAAP + 4));
    }
    if (Device->DcbaaLinear != 0) {
        U64 DcbaaEntry0 = ((volatile U64*)Device->DcbaaLinear)[0];
        DcbaaEntry0Low = U64_Low32(DcbaaEntry0);
        DcbaaEntry0High = U64_High32(DcbaaEntry0);
    }

    if (Device->RuntimeBase != 0) {
        LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
        Iman = XHCI_Read32(InterrupterBase, XHCI_IMAN);
        Imod = XHCI_Read32(InterrupterBase, XHCI_IMOD);
        Erstsz = XHCI_Read32(InterrupterBase, XHCI_ERSTSZ);
        ErdpLow = XHCI_Read32(InterrupterBase, XHCI_ERDP);
        ErdpHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERDP + 4));
        ErstbaLow = XHCI_Read32(InterrupterBase, XHCI_ERSTBA);
        ErstbaHigh = XHCI_Read32(InterrupterBase, (U32)(XHCI_ERSTBA + 4));
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("USBCMD/USBSTS/CONFIG"),
        TEXT("%x / %x / %x\n"),
        Usbcmd,
        Usbsts,
        Config);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Status Decode"),
        TEXT("Run=%u Halted=%u HSE=%u CNR=%u EINT=%u PCD=%u\n"),
        (Usbcmd & XHCI_USBCMD_RS) ? 1U : 0U,
        (Usbsts & XHCI_USBSTS_HCH) ? 1U : 0U,
        (Usbsts & 0x00000004) ? 1U : 0U,
        (Usbsts & XHCI_USBSTS_CNR) ? 1U : 0U,
        (Usbsts & 0x00000008) ? 1U : 0U,
        (Usbsts & 0x00000010) ? 1U : 0U);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("CRCR"), TEXT("%x:%x\n"), CrcrHigh, CrcrLow);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("DCBAAP"), TEXT("%x:%x\n"), DcbaapHigh, DcbaapLow);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("DCBAA[0]"), TEXT("%x:%x\n"), DcbaaEntry0High, DcbaaEntry0Low);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IMAN/IMOD/ERSTSZ"), TEXT("%x / %x / %x\n"), Iman, Imod, Erstsz);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("ERSTBA"), TEXT("%x:%x\n"), ErstbaHigh, ErstbaLow);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("ERDP"), TEXT("%x:%x\n"), ErdpHigh, ErdpLow);

    ActiveSlots = SystemDataViewCountActiveXhciSlots(Device);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Active Slots"),
        TEXT("%u/%u\n"),
        ActiveSlots,
        (U32)Device->MaxSlots);

    if (Device->UsbDevices == NULL) {
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Root Port Objects"), TEXT("Unavailable\n"));
        return;
    }

    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        U32 LinkState = (PortStatus & XHCI_PORTSC_PLS_MASK) >> SYSTEM_DATA_VIEW_XHCI_PLS_SHIFT;
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        BOOL Enabled = (PortStatus & XHCI_PORTSC_PED) != 0;
        BOOL Reset = (PortStatus & XHCI_PORTSC_PR) != 0;
        LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
        U8 EnumError = XHCI_ENUM_ERROR_NONE;
        U16 EnumCompletion = 0;
        U32 Present = 0;
        U32 SlotId = 0;
        LPCSTR MassStorageHint = TEXT("-");
        STR Label[32];

        if (Connected) {
            ConnectedPorts++;
        }
        if (Enabled) {
            EnabledPorts++;
        }

        if (UsbDevice != NULL) {
            EnumError = UsbDevice->LastEnumError;
            EnumCompletion = UsbDevice->LastEnumCompletion;
            Present = UsbDevice->Present ? 1U : 0U;
            SlotId = (U32)UsbDevice->SlotId;
            MassStorageHint = SystemDataViewXhciMassStorageHint(UsbDevice);
        }

        if (EnumError != XHCI_ENUM_ERROR_NONE) {
            ErrorPorts++;
        }

        StringPrintFormat(Label, TEXT("Port %u"), PortIndex + 1);
        SystemDataViewWriteFormat(
            Context,
            SYSTEM_DATA_VIEW_VALUE_COLUMN,
            Label,
            TEXT("CCS=%u PED=%u PR=%u PP=%u Speed=%x PLS=%x Raw=%x Err=%s C=%x Present=%u Slot=%u MS=%s\n"),
            Connected ? 1U : 0U,
            Enabled ? 1U : 0U,
            Reset ? 1U : 0U,
            (PortStatus & XHCI_PORTSC_PP) ? 1U : 0U,
            SpeedId,
            LinkState,
            PortStatus,
            XHCIEnumErrorToString(EnumError),
            (U32)EnumCompletion,
            Present,
            SlotId,
            MassStorageHint);
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Port Summary"),
        TEXT("Connected=%u Enabled=%u Error=%u\n"),
        ConnectedPorts,
        EnabledPorts,
        ErrorPorts);
}

/************************************************************************/

/**
 * @brief Draw the xHCI page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageXhci(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPPCI_DEVICE Controller = NULL;
    UINT ControllerCount = 0;
    BOOL Found = SystemDataViewFindPciController(
        SYSTEM_DATA_VIEW_PCI_CLASS_SERIAL_BUS,
        SYSTEM_DATA_VIEW_PCI_SUBCLASS_USB,
        SYSTEM_DATA_VIEW_PCI_PROGRAMMING_INTERFACE_XHCI,
        &Controller,
        &ControllerCount);

    SystemDataViewDrawPageHeader(Context, TEXT("xHCI"), PageIndex);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Controllers Found"), TEXT("%u\n"), (U32)ControllerCount);

    if (Found == FALSE || Controller == NULL) {
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("First Controller"), TEXT("Not Found\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Bus/Device/Function"),
        TEXT("%u/%u/%u\n"),
        (U32)Controller->Info.Bus,
        (U32)Controller->Info.Dev,
        (U32)Controller->Info.Func);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Vendor Identifier"),
        TEXT("%x\n"),
        (U32)Controller->Info.VendorID);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Device Identifier"),
        TEXT("%x\n"),
        (U32)Controller->Info.DeviceID);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Revision"), TEXT("%x\n"), (U32)Controller->Info.Revision);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("IRQ Line/Pin"),
        TEXT("%u / %u\n"),
        (U32)Controller->Info.IRQLine,
        (U32)Controller->Info.IRQLegacyPin);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("BAR0/BAR1 Raw"),
        TEXT("%x / %x\n"),
        Controller->Info.BAR[0],
        Controller->Info.BAR[1]);

    if (Controller->Driver != (LPDRIVER)&XHCIDriver) {
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Driver Attached"), TEXT("No\n"));
        SystemDataViewDrawFooter(Context);
        return;
    }

    SystemDataViewDrawXhciDetails(Context, (LPXHCI_DEVICE)Controller);
    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Fill PCI info for a given function.
 *
 * @param Bus Bus number.
 * @param Device Device number.
 * @param Function Function number.
 * @param Info Output info structure.
 * @return TRUE when the function exists.
 */
static BOOL SystemDataViewPciReadInfo(U8 Bus, U8 Device, U8 Function, LPSYSTEM_DATA_VIEW_PCI_INFO Info) {
    if (Info == NULL) {
        return FALSE;
    }

    U16 VendorId = PCI_Read16(Bus, Device, Function, PCI_CFG_VENDOR_ID);
    if (VendorId == 0xFFFFU) {
        return FALSE;
    }

    MemorySet(Info, 0, sizeof(SYSTEM_DATA_VIEW_PCI_INFO));
    Info->Bus = Bus;
    Info->Dev = Device;
    Info->Func = Function;
    Info->VendorID = VendorId;
    Info->DeviceID = PCI_Read16(Bus, Device, Function, PCI_CFG_DEVICE_ID);
    Info->BaseClass = PCI_Read8(Bus, Device, Function, PCI_CFG_BASECLASS);
    Info->SubClass = PCI_Read8(Bus, Device, Function, PCI_CFG_SUBCLASS);
    Info->ProgIF = PCI_Read8(Bus, Device, Function, PCI_CFG_PROG_IF);
    Info->Revision = PCI_Read8(Bus, Device, Function, PCI_CFG_REVISION);
    Info->HeaderType = PCI_Read8(Bus, Device, Function, PCI_CFG_HEADER_TYPE);
    Info->IRQLine = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_LINE);
    Info->IRQLegacyPin = PCI_Read8(Bus, Device, Function, PCI_CFG_IRQ_PIN);

    for (UINT Index = 0; Index < 6; Index++) {
        Info->BAR[Index] = PCI_Read32(Bus, Device, Function, (U16)(PCI_CFG_BAR0 + Index * 4));
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate all PCI functions and call a visitor.
 *
 * @param Context Output context.
 * @param Visitor Visitor callback.
 * @param UserData Visitor data.
 * @param DeviceCountOut Device count output.
 */
static void SystemDataViewPciEnumerate(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, SYSTEM_DATA_VIEW_PCI_VISITOR Visitor, LPVOID UserData, UINT* DeviceCountOut) {
    U32 Bus = 0;
    U32 Device = 0;
    U32 Function = 0;
    UINT DeviceCount = 0;

    for (Bus = 0; Bus < PCI_MAX_BUS; Bus++) {
        for (Device = 0; Device < PCI_MAX_DEV; Device++) {
            U16 VendorFunction0 = PCI_Read16((U8)Bus, (U8)Device, 0, PCI_CFG_VENDOR_ID);
            if (VendorFunction0 == 0xFFFFU) {
                continue;
            }

            U8 HeaderType = PCI_Read8((U8)Bus, (U8)Device, 0, PCI_CFG_HEADER_TYPE);
            U8 IsMultiFunction = (U8)((HeaderType & PCI_HEADER_MULTI_FN) ? 1 : 0);
            U8 MaxFunction = (U8)(IsMultiFunction ? (PCI_MAX_FUNC - 1) : 0);

            for (Function = 0; Function <= (U32)MaxFunction; Function++) {
                SYSTEM_DATA_VIEW_PCI_INFO Info;
                if (!SystemDataViewPciReadInfo((U8)Bus, (U8)Device, (U8)Function, &Info)) {
                    continue;
                }

                DeviceCount++;

                if (Context != NULL && Context->BufferLength + 128 >= SYSTEM_DATA_VIEW_OUTPUT_BUFFER_SIZE) {
                    SystemDataViewWriteString(Context, TEXT("Output truncated\n"));
                    Bus = PCI_MAX_BUS;
                    break;
                }

                if (Visitor != NULL) {
                    if (!Visitor(Context, &Info, UserData)) {
                        Bus = PCI_MAX_BUS;
                        break;
                    }
                }
            }
        }
    }

    if (DeviceCountOut != NULL) {
        *DeviceCountOut = DeviceCount;
    }
}

/************************************************************************/

/**
 * @brief PCI list visitor for the System Data View.
 *
 * @param Context Output context.
 * @param Info PCI function info.
 * @param UserData User data pointer.
 * @return TRUE to continue.
 */
static BOOL SystemDataViewPciListVisitor(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData) {
    STR Label[24];
    LPSYSTEM_DATA_VIEW_PCI_LIST_STATE State = (LPSYSTEM_DATA_VIEW_PCI_LIST_STATE)UserData;

    if (Context == NULL || Info == NULL || State == NULL) {
        return FALSE;
    }

    State->Index++;
    StringPrintFormat(Label, TEXT("PCI %u"), (U32)State->Index);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        Label,
        TEXT("Bus=%u Dev=%u Fn=%u Class=%x Sub=%x IF=%x VID=%x DID=%x\n"),
        (U32)Info->Bus,
        (U32)Info->Dev,
        (U32)Info->Func,
        (U32)Info->BaseClass,
        (U32)Info->SubClass,
        (U32)Info->ProgIF,
        (U32)Info->VendorID,
        (U32)Info->DeviceID);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Storage controller visitor for the System Data View.
 *
 * @param Context Output context.
 * @param Info PCI function info.
 * @param UserData User data pointer.
 * @return TRUE to continue.
 */
static BOOL SystemDataViewPciStorageVisitor(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData) {
    STR Label[32];
    LPSYSTEM_DATA_VIEW_PCI_STORAGE_STATE State = (LPSYSTEM_DATA_VIEW_PCI_STORAGE_STATE)UserData;

    if (Context == NULL || Info == NULL || State == NULL) {
        return FALSE;
    }

    if (Info->BaseClass != SYSTEM_DATA_VIEW_PCI_CLASS_MASS_STORAGE) {
        return TRUE;
    }

    State->Count++;
    State->Index++;

    StringPrintFormat(Label, TEXT("Controller %u"), (U32)State->Index);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        Label,
        TEXT("Bus=%u Dev=%u Fn=%u Class=%x Sub=%x IF=%x\n"),
        (U32)Info->Bus,
        (U32)Info->Dev,
        (U32)Info->Func,
        (U32)Info->BaseClass,
        (U32)Info->SubClass,
        (U32)Info->ProgIF);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("VID/DID/IRQ"),
        TEXT("%x / %x / %u\n"),
        (U32)Info->VendorID,
        (U32)Info->DeviceID,
        (U32)Info->IRQLine);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("BAR0/BAR5"), TEXT("%x / %x\n"), Info->BAR[0], Info->BAR[5]);

    return TRUE;
}

/************************************************************************/

/**
 * @brief VMD candidate visitor for the System Data View.
 *
 * @param Context Output context.
 * @param Info PCI function info.
 * @param UserData User data pointer.
 * @return TRUE to continue.
 */
static BOOL SystemDataViewPciVmdVisitor(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData) {
    STR Label[32];
    LPSYSTEM_DATA_VIEW_PCI_VMD_STATE State = (LPSYSTEM_DATA_VIEW_PCI_VMD_STATE)UserData;

    if (Context == NULL || Info == NULL || State == NULL) {
        return FALSE;
    }

    if (Info->VendorID != SYSTEM_DATA_VIEW_PCI_VENDOR_INTEL || Info->BaseClass != SYSTEM_DATA_VIEW_PCI_CLASS_BRIDGE) {
        return TRUE;
    }

    State->Count++;
    State->Index++;

    StringPrintFormat(Label, TEXT("Bridge %u"), (U32)State->Index);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        Label,
        TEXT("Bus=%u Dev=%u Fn=%u Class=%x Sub=%x IF=%x\n"),
        (U32)Info->Bus,
        (U32)Info->Dev,
        (U32)Info->Func,
        (U32)Info->BaseClass,
        (U32)Info->SubClass,
        (U32)Info->ProgIF);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("VID/DID/IRQ"),
        TEXT("%x / %x / %u\n"),
        (U32)Info->VendorID,
        (U32)Info->DeviceID,
        (U32)Info->IRQLine);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Header Type/Pin"),
        TEXT("%x / %u\n"),
        (U32)Info->HeaderType,
        (U32)Info->IRQLegacyPin);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("BAR0/BAR1"), TEXT("%x / %x\n"), Info->BAR[0], Info->BAR[1]);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Return a short vendor name for common graphics vendors.
 *
 * @param VendorId PCI vendor identifier.
 * @return Vendor name.
 */
static LPCSTR SystemDataViewGraphicsVendorName(U16 VendorId) {
    switch (VendorId) {
        case SYSTEM_DATA_VIEW_PCI_VENDOR_INTEL:
            return TEXT("Intel");
        case SYSTEM_DATA_VIEW_PCI_VENDOR_NVIDIA:
            return TEXT("NVIDIA");
        case SYSTEM_DATA_VIEW_PCI_VENDOR_AMD:
            return TEXT("AMD");
    }

    return TEXT("Other");
}

/************************************************************************/

/**
 * @brief Return a short display subclass name.
 *
 * @param SubClass PCI display subclass.
 * @return Subclass name.
 */
static LPCSTR SystemDataViewGraphicsSubclassName(U8 SubClass) {
    switch (SubClass) {
        case 0x00:
            return TEXT("VGA");
        case 0x01:
            return TEXT("XGA");
        case 0x02:
            return TEXT("3D");
        case 0x80:
            return TEXT("Other");
    }

    return TEXT("Unknown");
}

/************************************************************************/

/**
 * @brief Format an IPv4 address in dotted-decimal notation.
 *
 * @param Buffer Destination buffer.
 * @param BufferLength Destination buffer length.
 * @param IPv4_Be IPv4 address in big-endian format.
 */
static void SystemDataViewFormatIPv4(LPSTR Buffer, UINT BufferLength, U32 IPv4_Be) {
    if (Buffer == NULL || BufferLength == 0) {
        return;
    }

    FormatIPv4(IPv4_Be, Buffer);
}

/************************************************************************/

/**
 * @brief Return a short network subclass name.
 *
 * @param SubClass PCI network subclass.
 * @return Subclass name.
 */
static LPCSTR SystemDataViewNetworkSubclassName(U8 SubClass) {
    switch (SubClass) {
        case PCI_SUBCLASS_ETHERNET:
            return TEXT("Ethernet");
        case 0x80:
            return TEXT("Other");
    }

    return TEXT("Unknown");
}

/************************************************************************/

/**
 * @brief Check whether one PCI BDF exists in the kernel attached PCI list.
 *
 * @param Bus PCI bus.
 * @param Device PCI device.
 * @param Function PCI function.
 * @return TRUE when present in GetPCIDeviceList.
 */
static BOOL SystemDataViewIsAttachedPciDevice(U8 Bus, U8 Device, U8 Function) {
    LPLIST DeviceList = GetPCIDeviceList();

    if (DeviceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = DeviceList->First; Node != NULL; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        SAFE_USE_VALID_ID(PciDevice, KOID_PCIDEVICE) {
            if (PciDevice->Info.Bus == Bus && PciDevice->Info.Dev == Device && PciDevice->Info.Func == Function) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Find a registered network device context by PCI location.
 *
 * @param Bus PCI bus.
 * @param Device PCI device.
 * @param Function PCI function.
 * @return Matching network device context or NULL.
 */
static LPNETWORK_DEVICE_CONTEXT SystemDataViewFindNetworkDeviceContext(U8 Bus, U8 Device, U8 Function) {
    LPLIST NetworkDeviceList = GetNetworkDeviceList();

    if (NetworkDeviceList == NULL) {
        return NULL;
    }

    for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
        LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Node;

        SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
            LPPCI_DEVICE PciDevice = NetContext->Device;

            SAFE_USE_VALID_ID(PciDevice, KOID_PCIDEVICE) {
                if (PciDevice->Info.Bus == Bus && PciDevice->Info.Dev == Device && PciDevice->Info.Func == Function) {
                    return NetContext;
                }
            }
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Graphics controller visitor for the System Data View.
 *
 * @param Context Output context.
 * @param Info PCI function info.
 * @param UserData User data pointer.
 * @return TRUE to continue.
 */
static BOOL SystemDataViewPciGraphicsVisitor(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData) {
    STR Label[32];
    BOOL Attached = FALSE;
    LPSYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE State = (LPSYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE)UserData;

    if (Context == NULL || Info == NULL || State == NULL) {
        return FALSE;
    }

    if (Info->BaseClass != PCI_CLASS_DISPLAY) {
        return TRUE;
    }

    State->Count++;
    State->Index++;
    Attached = SystemDataViewIsAttachedPciDevice(Info->Bus, Info->Dev, Info->Func);
    if (Attached) {
        State->AttachedCount++;
    }

    StringPrintFormat(Label, TEXT("Graphics %u"), (U32)State->Index);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        Label,
        TEXT("Bus=%u Dev=%u Fn=%u\n"),
        (U32)Info->Bus,
        (U32)Info->Dev,
        (U32)Info->Func);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Vendor"),
        TEXT("%s (%x)\n"),
        SystemDataViewGraphicsVendorName(Info->VendorID),
        (U32)Info->VendorID);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Device Identifier"),
        TEXT("%x rev=%x\n"),
        (U32)Info->DeviceID,
        (U32)Info->Revision);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Display Type"),
        TEXT("%s (sub=%x if=%x)\n"),
        SystemDataViewGraphicsSubclassName(Info->SubClass),
        (U32)Info->SubClass,
        (U32)Info->ProgIF);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("IRQ Line/Pin"),
        TEXT("%u / %u\n"),
        (U32)Info->IRQLine,
        (U32)Info->IRQLegacyPin);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("BAR0/BAR2"), TEXT("%x / %x\n"), Info->BAR[0], Info->BAR[2]);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Kernel PCI Link"),
        TEXT("%s\n"),
        Attached ? TEXT("Attached") : TEXT("Not attached"));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Draw the graphics controller summary page.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageGraphicsDevices(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SYSTEM_DATA_VIEW_PCI_GRAPHICS_STATE State;

    SystemDataViewDrawPageHeader(Context, TEXT("Graphics Devices (PCI)"), PageIndex);
    MemorySet(&State, 0, sizeof(State));
    SystemDataViewPciEnumerate(Context, SystemDataViewPciGraphicsVisitor, &State, NULL);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Graphics Found"), TEXT("%u\n"), (U32)State.Count);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Kernel PCI Attached"), TEXT("%u\n"), (U32)State.AttachedCount);
    if (State.Count == 0) {
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Graphics"), TEXT("No PCI display controller found\n"));
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Network controller visitor for the System Data View.
 *
 * @param Context Output context.
 * @param Info PCI function info.
 * @param UserData User data pointer.
 * @return TRUE to continue.
 */
static BOOL SystemDataViewPciNetworkVisitor(
    LPSYSTEM_DATA_VIEW_CONTEXT Context, const SYSTEM_DATA_VIEW_PCI_INFO* Info, LPVOID UserData) {
    NETWORK_INFO NetworkInfo;
    NETWORK_GET_INFO GetInfo;
    STR Label[32];
    STR IPv4Address[24];
    BOOL Attached = FALSE;
    LPNETWORK_DEVICE_CONTEXT NetContext = NULL;
    LPSYSTEM_DATA_VIEW_NETWORK_STATE State = (LPSYSTEM_DATA_VIEW_NETWORK_STATE)UserData;

    if (Context == NULL || Info == NULL || State == NULL) {
        return FALSE;
    }

    if (Info->BaseClass != PCI_CLASS_NETWORK) {
        return TRUE;
    }

    State->Count++;
    State->Index++;
    NetContext = SystemDataViewFindNetworkDeviceContext(Info->Bus, Info->Dev, Info->Func);
    Attached = NetContext != NULL;
    if (Attached) {
        State->AttachedCount++;
        SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
            if (NetContext->IsReady) {
                State->ReadyCount++;
            }
        }
    }

    StringPrintFormat(Label, TEXT("Network %u"), (U32)State->Index);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        Label,
        TEXT("Bus=%u Dev=%u Fn=%u\n"),
        (U32)Info->Bus,
        (U32)Info->Dev,
        (U32)Info->Func);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Vendor"), TEXT("%x\n"), (U32)Info->VendorID);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Device Identifier"),
        TEXT("%x rev=%x\n"),
        (U32)Info->DeviceID,
        (U32)Info->Revision);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Controller Type"),
        TEXT("%s (sub=%x if=%x)\n"),
        SystemDataViewNetworkSubclassName(Info->SubClass),
        (U32)Info->SubClass,
        (U32)Info->ProgIF);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("IRQ Line/Pin"),
        TEXT("%u / %u\n"),
        (U32)Info->IRQLine,
        (U32)Info->IRQLegacyPin);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("BAR0/BAR1"), TEXT("%x / %x\n"), Info->BAR[0], Info->BAR[1]);
    SystemDataViewWriteFormat(
        Context,
        SYSTEM_DATA_VIEW_VALUE_COLUMN,
        TEXT("Kernel Network Link"),
        TEXT("%s\n"),
        Attached ? TEXT("Attached") : TEXT("Not attached"));

    if (Attached) {
        SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
            LPPCI_DEVICE Device = NetContext->Device;

            SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {
                    MemorySet(&NetworkInfo, 0, sizeof(NetworkInfo));
                    GetInfo.Device = Device;
                    GetInfo.Info = &NetworkInfo;
                    Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);

                    SystemDataViewWriteFormat(
                        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Device Name"), TEXT("%s\n"), Device->Name);
                    SystemDataViewWriteFormat(
                        Context,
                        SYSTEM_DATA_VIEW_VALUE_COLUMN,
                        TEXT("Driver"),
                        TEXT("%s / %s\n"),
                        Device->Driver->Manufacturer,
                        Device->Driver->Product);
                    SystemDataViewWriteFormat(
                        Context,
                        SYSTEM_DATA_VIEW_VALUE_COLUMN,
                        TEXT("MAC"),
                        TEXT("%x:%x:%x:%x:%x:%x\n"),
                        NetworkInfo.MAC[0],
                        NetworkInfo.MAC[1],
                        NetworkInfo.MAC[2],
                        NetworkInfo.MAC[3],
                        NetworkInfo.MAC[4],
                        NetworkInfo.MAC[5]);
                    SystemDataViewFormatIPv4(IPv4Address, sizeof(IPv4Address), NetContext->ActiveConfig.LocalIPv4_Be);
                    SystemDataViewWriteFormat(
                        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IPv4"), TEXT("%s\n"), IPv4Address);
                    SystemDataViewWriteFormat(
                        Context,
                        SYSTEM_DATA_VIEW_VALUE_COLUMN,
                        TEXT("Link"),
                        TEXT("%s %u Mbps %s duplex\n"),
                        NetworkInfo.LinkUp ? TEXT("Up") : TEXT("Down"),
                        NetworkInfo.SpeedMbps,
                        NetworkInfo.DuplexFull ? TEXT("Full") : TEXT("Half"));
                    SystemDataViewWriteFormat(
                        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("MTU"), TEXT("%u\n"), NetworkInfo.MTU);
                    SystemDataViewWriteFormat(
                        Context,
                        SYSTEM_DATA_VIEW_VALUE_COLUMN,
                        TEXT("State"),
                        TEXT("Initialized=%s Ready=%s Interrupts=%s\n"),
                        NetContext->IsInitialized ? TEXT("Yes") : TEXT("No"),
                        NetContext->IsReady ? TEXT("Yes") : TEXT("No"),
                        NetContext->InterruptsEnabled ? TEXT("On") : TEXT("Off"));
                }
            }
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Draw the network device summary page.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageNetworkDevices(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SYSTEM_DATA_VIEW_NETWORK_STATE State;

    SystemDataViewDrawPageHeader(Context, TEXT("Network Devices"), PageIndex);
    MemorySet(&State, 0, sizeof(State));
    SystemDataViewPciEnumerate(Context, SystemDataViewPciNetworkVisitor, &State, NULL);

    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Devices Found"), TEXT("%u\n"), (U32)State.Count);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Kernel Attached"), TEXT("%u\n"), (U32)State.AttachedCount);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Devices Ready"), TEXT("%u\n"), (U32)State.ReadyCount);
    if (State.Count == 0) {
        SystemDataViewWriteFormat(
            Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Network"), TEXT("No PCI network controller found\n"));
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw the PCI device list page.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPagePciList(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    UINT DeviceCount = 0;
    SYSTEM_DATA_VIEW_PCI_LIST_STATE State;

    SystemDataViewDrawPageHeader(Context, TEXT("PCI Devices"), PageIndex);
    MemorySet(&State, 0, sizeof(State));
    SystemDataViewPciEnumerate(Context, SystemDataViewPciListVisitor, &State, &DeviceCount);

    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Devices Found"), TEXT("%u\n"), (U32)DeviceCount);

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw the VMD controller summary page.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageVmd(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SYSTEM_DATA_VIEW_PCI_VMD_STATE State;

    SystemDataViewDrawPageHeader(Context, TEXT("VMD (Intel Bridge)"), PageIndex);
    MemorySet(&State, 0, sizeof(State));
    SystemDataViewPciEnumerate(Context, SystemDataViewPciVmdVisitor, &State, NULL);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Candidates Found"), TEXT("%u\n"), (U32)State.Count);
    if (State.Count == 0) {
        SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("VMD"), TEXT("Not Detected\n"));
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Check if the PCI device is a mass storage controller.
 *
 * @param Device PCI device to evaluate.
 * @return TRUE when the device is a mass storage controller.
 */
/**
 * @brief Draw the storage controller summary page.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageStorageControllers(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    SYSTEM_DATA_VIEW_PCI_STORAGE_STATE State;

    SystemDataViewDrawPageHeader(Context, TEXT("Storage Controllers"), PageIndex);
    MemorySet(&State, 0, sizeof(State));
    SystemDataViewPciEnumerate(Context, SystemDataViewPciStorageVisitor, &State, NULL);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("Controllers Found"), TEXT("%u\n"), (U32)State.Count);

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw the IDT page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageIdt(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPGATE_DESCRIPTOR Table = (LPGATE_DESCRIPTOR)Kernel_x86_32.IDT;

    SystemDataViewDrawPageHeader(Context, TEXT("IDT"), PageIndex);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IDT Base"), TEXT("%p\n"), (LPVOID)Table);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("IDT Limit"), TEXT("%x\n"), (U32)(IDT_SIZE - 1u));

    if (Table != NULL) {
        for (U32 Vector = 0x20; Vector < 0x24; Vector++) {
            STR Label[24];
#if defined(__EXOS_ARCH_X86_64__)
            U64 Offset = (U64)Table[Vector].Offset_00_15 | ((U64)Table[Vector].Offset_16_31 << 16) |
                         ((U64)Table[Vector].Offset_32_63 << 32);
            StringPrintFormat(Label, TEXT("Vector %x"), Vector);
            SystemDataViewWriteFormat(
                Context,
                SYSTEM_DATA_VIEW_VALUE_COLUMN,
                Label,
                TEXT("Offset=%p Selector=%x\n"),
                (LPVOID)(LINEAR)Offset,
                (U32)Table[Vector].Selector);
#else
            U32 Offset = (U32)Table[Vector].Offset_00_15 | ((U32)Table[Vector].Offset_16_31 << 16);
            StringPrintFormat(Label, TEXT("Vector %x"), Vector);
            SystemDataViewWriteFormat(
                Context,
                SYSTEM_DATA_VIEW_VALUE_COLUMN,
                Label,
                TEXT("Offset=%p Selector=%x\n"),
                (LPVOID)(LINEAR)Offset,
                (U32)Table[Vector].Selector);
#endif
        }
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw the GDT page for System Data View.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPageGdt(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    LPSEGMENT_DESCRIPTOR Table = (LPSEGMENT_DESCRIPTOR)Kernel_x86_32.GDT;

    SystemDataViewDrawPageHeader(Context, TEXT("GDT"), PageIndex);
    SystemDataViewWriteFormat(Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("GDT Base"), TEXT("%p\n"), (LPVOID)Table);
    SystemDataViewWriteFormat(
        Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, TEXT("GDT Limit"), TEXT("%x\n"), (U32)(GDT_SIZE - 1u));

    if (Table != NULL) {
        for (U32 Index = 0; Index < 4; Index++) {
            STR Label[24];
            U32 Base = (U32)Table[Index].Base_00_15 | ((U32)Table[Index].Base_16_23 << 16) |
                       ((U32)Table[Index].Base_24_31 << 24);
            U32 Limit = (U32)Table[Index].Limit_00_15 | ((U32)Table[Index].Limit_16_19 << 16);

            StringPrintFormat(Label, TEXT("Index %u"), Index);
            SystemDataViewWriteFormat(
                Context, SYSTEM_DATA_VIEW_VALUE_COLUMN, Label, TEXT("Base=%p Limit=%x\n"), (LPVOID)(LINEAR)Base, Limit);
        }
    }

    SystemDataViewDrawFooter(Context);
}

/************************************************************************/

/**
 * @brief Draw a System Data View page by index.
 *
 * @param Context Output context.
 * @param PageIndex Page index.
 */
static void SystemDataViewDrawPage(LPSYSTEM_DATA_VIEW_CONTEXT Context, U8 PageIndex) {
    switch (PageIndex) {
        case 0:
            SystemDataViewDrawPageAcpi(Context, PageIndex);
            break;
        case 1:
            SystemDataViewDrawPagePicPitIoApic(Context, PageIndex);
            break;
        case 2:
            SystemDataViewDrawPageLocalApic(Context, PageIndex);
            break;
        case 3:
            SystemDataViewDrawPageInterruptRouting(Context, PageIndex);
            break;
        case 4:
            SystemDataViewDrawPageAhci(Context, PageIndex);
            break;
        case 5:
            SystemDataViewDrawPageEhci(Context, PageIndex);
            break;
        case 6:
            SystemDataViewDrawPageXhci(Context, PageIndex);
            break;
        case 7:
            SystemDataViewDrawPageGraphicsDevices(Context, PageIndex);
            break;
        case 8:
            SystemDataViewDrawPageNetworkDevices(Context, PageIndex);
            break;
        case 9:
            SystemDataViewDrawPagePciList(Context, PageIndex);
            break;
        case 10:
            SystemDataViewDrawPageVmd(Context, PageIndex);
            break;
        case 11:
            SystemDataViewDrawPageStorageControllers(Context, PageIndex);
            break;
        case 12:
            SystemDataViewDrawPageIdt(Context, PageIndex);
            break;
        case 13:
        default:
            SystemDataViewDrawPageGdt(Context, PageIndex);
            break;
    }
}

/************************************************************************/

/**
 * @brief Run the System Data View loop before tasks are created.
 */
void SystemDataViewMode(void) {
    SYSTEM_DATA_VIEW_CONTEXT Context;
    U8 CurrentPage = 0;
    UINT ScrollOffsets[SYSTEM_DATA_VIEW_PAGE_COUNT];
    UINT ScreenRows = 0;
    UINT MaxScroll = 0;
    BOOL NeedsRedraw = TRUE;

    for (UINT Index = 0; Index < SYSTEM_DATA_VIEW_PAGE_COUNT; Index++) {
        ScrollOffsets[Index] = 0;
    }

    while (TRUE) {
        if (NeedsRedraw) {
            ScreenRows = Console.Height;
            MaxScroll = 0;

            if (ScreenRows > 0) {
                ScreenRows -= 1;
            }

            SystemDataViewOutputReset(&Context);
            SystemDataViewDrawPage(&Context, CurrentPage);

            if (Context.LineCount > ScreenRows) {
                MaxScroll = Context.LineCount - ScreenRows;
            }
            if (ScrollOffsets[CurrentPage] > MaxScroll) {
                ScrollOffsets[CurrentPage] = MaxScroll;
            }

            ClearConsole();
            SystemDataViewRender(&Context, ScrollOffsets[CurrentPage], ScreenRows);
            NeedsRedraw = FALSE;
        }

        if (PeekChar() == FALSE) {
            Sleep(10);
            continue;
        }

        KEY_CODE KeyCode;
        GetKeyCode(&KeyCode);

        switch (KeyCode.VirtualKey) {
            case VK_ESCAPE:
                return;
            case VK_RIGHT:
                CurrentPage = (U8)((CurrentPage + 1) % SYSTEM_DATA_VIEW_PAGE_COUNT);
                NeedsRedraw = TRUE;
                break;
            case VK_LEFT:
                CurrentPage = (U8)((CurrentPage + SYSTEM_DATA_VIEW_PAGE_COUNT - 1) % SYSTEM_DATA_VIEW_PAGE_COUNT);
                NeedsRedraw = TRUE;
                break;
            case VK_UP:
                if (ScrollOffsets[CurrentPage] > 0) {
                    ScrollOffsets[CurrentPage]--;
                    NeedsRedraw = TRUE;
                }
                break;
            case VK_DOWN:
                if (ScrollOffsets[CurrentPage] < MaxScroll) {
                    ScrollOffsets[CurrentPage]++;
                    NeedsRedraw = TRUE;
                }
                break;
        }
    }
}

/************************************************************************/
