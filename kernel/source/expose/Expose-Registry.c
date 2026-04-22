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


    Script Exposure Helpers - Default Shell Host Registration

\************************************************************************/

#include "expose/Exposed.h"

#include "core/KernelData.h"

/************************************************************************/

/**
 * @brief Register one default shell host symbol.
 * @param Context Script context that owns the registration.
 * @param Name Symbol name.
 * @param Kind Host symbol kind.
 * @param Handle Root handle.
 * @param Descriptor Host descriptor.
 * @return TRUE on success.
 */
static BOOL ExposeRegisterDefaultHostSymbol(
    LPSCRIPT_CONTEXT Context,
    LPCSTR Name,
    SCRIPT_HOST_SYMBOL_KIND Kind,
    SCRIPT_HOST_HANDLE Handle,
    const SCRIPT_HOST_DESCRIPTOR* Descriptor) {
    if (Context == NULL || Name == NULL || Descriptor == NULL) {
        return FALSE;
    }

    return ScriptRegisterHostSymbol(
        Context,
        Name,
        Kind,
        Handle,
        Descriptor,
        NULL);
}

/************************************************************************/

/**
 * @brief Register the standard shell-visible host symbols through expose.
 * @param Context Script context that owns the host registry.
 * @return TRUE on success.
 */
BOOL ExposeRegisterDefaultScriptHostObjects(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("process"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetProcessList(),
            &ProcessArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("task"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetTaskList(),
            &TaskRootArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("driver"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetDriverList(),
            &DriverArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("graphics"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetGraphicsRootHandle(),
            &GraphicsDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("clock"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetClockRootHandle(),
            &ClockDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("storage"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetDiskList(),
            &StorageArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("fileSystem"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            FileSystemRootHandle,
            &FileSystemRootDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("memoryMap"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            MemoryMapRootHandle,
            &MemoryMapRootDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("pciBus"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetPCIDeviceList(),
            &PciBusArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("pciDevice"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetPCIDeviceList(),
            &PciDeviceArrayDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("usb"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            UsbRootHandle,
            &UsbDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("network"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            NetworkRootHandle,
            &NetworkDescriptor)) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("keyboard"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetKeyboardRootHandle(),
            GetKeyboardDescriptor())) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("mouse"),
            SCRIPT_HOST_SYMBOL_OBJECT,
            GetMouseRootHandle(),
            GetMouseDescriptor())) {
        return FALSE;
    }

    if (!ExposeRegisterDefaultHostSymbol(
            Context,
            TEXT("account"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            GetAccountList(),
            &AccountArrayDescriptor)) {
        return FALSE;
    }

    return TRUE;
}
