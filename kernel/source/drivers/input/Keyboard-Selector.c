
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


    Keyboard selector

\************************************************************************/

#include "drivers/input/Keyboard.h"
#include "drivers/input/KeyboardDrivers.h"
#include "drivers/bus/PCI.h"
#include "drivers/usb/XHCI-Internal.h"

#include "core/DriverGetters.h"
#include "core/KernelData.h"
#include "log/Log.h"

/************************************************************************/

#define KEYBOARD_SELECTOR_VER_MAJOR 1
#define KEYBOARD_SELECTOR_VER_MINOR 0

#define USB_CLASS_HID 0x03
#define USB_HID_SUBCLASS_BOOT 0x01
#define USB_HID_PROTOCOL_KEYBOARD 0x01

/************************************************************************/
// Forward declarations

static UINT KeyboardSelectorCommands(UINT Function, UINT Parameter);

/************************************************************************/

static DRIVER DATA_SECTION KeyboardSelectorDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_KEYBOARD,
    .VersionMajor = KEYBOARD_SELECTOR_VER_MAJOR,
    .VersionMinor = KEYBOARD_SELECTOR_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "Keyboard selector",
    .Alias = "keyboard_selector",
    .Flags = 0,
    .Command = KeyboardSelectorCommands
};

/************************************************************************/

/**
 * @brief Check whether an interface is a HID boot keyboard.
 * @param Interface USB interface to inspect.
 * @return TRUE when the interface matches a HID keyboard.
 */
static BOOL KeyboardSelectorIsUsbKeyboardInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    return (Interface->InterfaceClass == USB_CLASS_HID &&
            Interface->InterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
            Interface->InterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD);
}

/************************************************************************/

/**
 * @brief Check if an xHCI USB device exposes a HID keyboard interface.
 * @param UsbDevice USB device to inspect.
 * @return TRUE when a keyboard interface is found.
 */
static BOOL KeyboardSelectorHasUsbKeyboardInterface(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL || UsbDevice->Present == FALSE || UsbDevice->IsHub) {
        return FALSE;
    }

    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
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
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (KeyboardSelectorIsUsbKeyboardInterface(Interface)) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Detect whether any USB HID keyboard is present.
 * @return TRUE when a keyboard is detected on any xHCI controller.
 */
static BOOL KeyboardSelectorDetectUsbKeyboard(void) {
    LPLIST PciList = GetPCIDeviceList();

    if (PciList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice == NULL || PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Controller = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Controller, KOID_PCIDEVICE) {
            XHCI_EnsureUsbDevices(Controller);

            LPLIST UsbDeviceList = GetUsbDeviceList();
            if (UsbDeviceList != NULL) {
                for (LPLISTNODE UsbNode = UsbDeviceList->First; UsbNode; UsbNode = UsbNode->Next) {
                    LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)UsbNode;
                    if (UsbDevice->Controller != Controller) {
                        continue;
                    }
                    if (KeyboardSelectorHasUsbKeyboardInterface(UsbDevice)) {
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Load the appropriate keyboard driver based on detection.
 * @param Parameter Unused.
 * @return Driver status code.
 */
static UINT KeyboardSelectorLoad(UINT Parameter) {
    UNUSED(Parameter);

    DEBUG(TEXT("Detecting keyboard"));

    BOOL HasUsbKeyboard = KeyboardSelectorDetectUsbKeyboard();
    if (HasUsbKeyboard) {
        DEBUG(TEXT("USB HID keyboard detected"));
        UINT Result = USBKeyboardGetDriver()->Command(DF_LOAD, 0);
        if (Result == DF_RETURN_SUCCESS) {
            KeyboardSelectorDriver.Flags |= DRIVER_FLAG_READY;
        }
        return Result;
    }

    U16 Ps2Identifier = DetectKeyboard();
    if (Ps2Identifier != 0) {
        DEBUG(TEXT("PS/2 keyboard detected (id=%x)"), (UINT)Ps2Identifier);
        UINT Result = StdKeyboardGetDriver()->Command(DF_LOAD, 0);
        if (Result == DF_RETURN_SUCCESS) {
            KeyboardSelectorDriver.Flags |= DRIVER_FLAG_READY;
        }
        return Result;
    }

    ERROR(TEXT("No keyboard detected"));
    KeyboardSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_UNEXPECTED;
}

/************************************************************************/

/**
 * @brief Unload keyboard drivers.
 * @param Parameter Unused.
 * @return Driver status code.
 */
static UINT KeyboardSelectorUnload(UINT Parameter) {
    UNUSED(Parameter);

    (void)USBKeyboardGetDriver()->Command(DF_UNLOAD, 0);
    (void)StdKeyboardGetDriver()->Command(DF_UNLOAD, 0);
    KeyboardSelectorDriver.Flags &= ~DRIVER_FLAG_READY;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Keyboard selector driver entry point.
 * @param Function Driver command.
 * @param Parameter Command parameter.
 * @return Driver result.
 */
static UINT KeyboardSelectorCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return KeyboardSelectorLoad(Parameter);
        case DF_UNLOAD:
            return KeyboardSelectorUnload(Parameter);
        case DF_GET_VERSION:
            return MAKE_VERSION(KEYBOARD_SELECTOR_VER_MAJOR, KEYBOARD_SELECTOR_VER_MINOR);
        case DF_KEY_GETSTATE:
        case DF_KEY_ISKEY:
        case DF_KEY_GETKEY:
        case DF_KEY_GETLED:
        case DF_KEY_SETLED:
        case DF_KEY_GETDELAY:
        case DF_KEY_SETDELAY:
        case DF_KEY_GETRATE:
        case DF_KEY_SETRATE:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Retrieve the keyboard selector driver descriptor.
 * @return Pointer to the selector driver.
 */
LPDRIVER KeyboardSelectorGetDriver(void) {
    return &KeyboardSelectorDriver;
}

/************************************************************************/
