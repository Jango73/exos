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


    Driver shared helpers

\************************************************************************/

#include "core/Driver.h"

/**
 * @brief Convert one driver type identifier to display text.
 * @param DriverType Driver type constant.
 * @return Static text name.
 */
LPCSTR DriverTypeToText(UINT DriverType) {
    typedef struct tag_DRIVER_TYPE_ENTRY {
        UINT Type;
        LPCSTR Name;
    } DRIVER_TYPE_ENTRY;

    static const DRIVER_TYPE_ENTRY Entries[] = {
        {DRIVER_TYPE_NONE, TEXT("none")},
        {DRIVER_TYPE_INIT, TEXT("init")},
        {DRIVER_TYPE_CLOCK, TEXT("clock")},
        {DRIVER_TYPE_CONSOLE, TEXT("console")},
        {DRIVER_TYPE_INTERRUPT, TEXT("interrupt")},
        {DRIVER_TYPE_MEMORY, TEXT("memory")},
        {DRIVER_TYPE_FLOPPYSTORAGE, TEXT("floppystorage")},
        {DRIVER_TYPE_STORAGE, TEXT("storage")},
        {DRIVER_TYPE_RAMSTORAGE, TEXT("ramstorage")},
        {DRIVER_TYPE_FILESYSTEM, TEXT("filesystem")},
        {DRIVER_TYPE_KEYBOARD, TEXT("keyboard")},
        {DRIVER_TYPE_GRAPHICS, TEXT("graphics")},
        {DRIVER_TYPE_MONITOR, TEXT("monitor")},
        {DRIVER_TYPE_MOUSE, TEXT("mouse")},
        {DRIVER_TYPE_CDROM, TEXT("cdrom")},
        {DRIVER_TYPE_MODEM, TEXT("modem")},
        {DRIVER_TYPE_NETWORK, TEXT("network")},
        {DRIVER_TYPE_WAVE, TEXT("wave")},
        {DRIVER_TYPE_MIDI, TEXT("midi")},
        {DRIVER_TYPE_SYNTH, TEXT("synth")},
        {DRIVER_TYPE_PRINTER, TEXT("printer")},
        {DRIVER_TYPE_SCANNER, TEXT("scanner")},
        {DRIVER_TYPE_GRAPHTABLE, TEXT("graphtable")},
        {DRIVER_TYPE_DVD, TEXT("dvd")},
        {DRIVER_TYPE_USB_STORAGE, TEXT("usb_storage")},
        {DRIVER_TYPE_NVME_STORAGE, TEXT("nvme_storage")},
        {DRIVER_TYPE_SATA_STORAGE, TEXT("sata_storage")},
        {DRIVER_TYPE_ATA_STORAGE, TEXT("ata_storage")},
        {DRIVER_TYPE_XHCI, TEXT("xhci")},
        {DRIVER_TYPE_OTHER, TEXT("other")},
    };

    for (UINT Index = 0; Index < ARRAY_COUNT(Entries); Index++) {
        if (Entries[Index].Type == DriverType) {
            return Entries[Index].Name;
        }
    }

    return TEXT("unknown");
}

/***************************************************************************/

/**
 * @brief Convert one driver enum domain identifier to display text.
 * @param Domain Enum domain constant.
 * @return Static text name.
 */
LPCSTR DriverDomainToText(UINT Domain) {
    typedef struct tag_DRIVER_DOMAIN_ENTRY {
        UINT Domain;
        LPCSTR Name;
    } DRIVER_DOMAIN_ENTRY;

    static const DRIVER_DOMAIN_ENTRY Entries[] = {
        {ENUM_DOMAIN_PCI_DEVICE, TEXT("pci_device")},
        {ENUM_DOMAIN_AHCI_PORT, TEXT("ahci_port")},
        {ENUM_DOMAIN_ATA_DEVICE, TEXT("ata_device")},
        {ENUM_DOMAIN_XHCI_PORT, TEXT("xhci_port")},
        {ENUM_DOMAIN_USB_DEVICE, TEXT("usb_device")},
        {ENUM_DOMAIN_USB_NODE, TEXT("usb_node")},
    };

    for (UINT Index = 0; Index < ARRAY_COUNT(Entries); Index++) {
        if (Entries[Index].Domain == Domain) {
            return Entries[Index].Name;
        }
    }

    return TEXT("unknown");
}

/***************************************************************************/
