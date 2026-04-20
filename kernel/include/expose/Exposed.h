
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


    Shell Script Host Exposure Helpers

\************************************************************************/

#pragma once

#include "Base.h"
#include "text/CoreString.h"
#include "utils/List.h"
#include "memory/Memory.h"
#include "script/Script.h"

/************************************************************************/

#define EXPOSE_ACCESS_PUBLIC    0x00000000u
#define EXPOSE_ACCESS_SAME_USER 0x00000001u
#define EXPOSE_ACCESS_ADMIN     0x00000002u
#define EXPOSE_ACCESS_KERNEL    0x00000004u
#define EXPOSE_ACCESS_OWNER_PROCESS 0x00000008u

#define EXPOSE_REQUIRE_ACCESS(RequiredAccess, TargetProcess) \
    do { \
        if (!ExposeCanReadProcess(ExposeGetCallerProcess(), (TargetProcess), (RequiredAccess))) { \
            return SCRIPT_ERROR_UNAUTHORIZED; \
        } \
    } while (0)

#define EXPOSE_PROPERTY_GUARD() \
    do { \
        if (OutValue == NULL || Parent == NULL || Property == NULL) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE)); \
    } while (0)

#define EXPOSE_ARRAY_GUARD() \
    do { \
        if (OutValue == NULL || Parent == NULL) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
    } while (0)

#define EXPOSE_BIND_INTEGER(PropertyName, ValueExpr) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_INTEGER; \
            OutValue->Value.Integer = (INT)(ValueExpr); \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_BIND_STRING(PropertyName, ValueExpr) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_STRING; \
            OutValue->Value.String = (LPSTR)(ValueExpr); \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_BIND_HOST_HANDLE(PropertyName, HandleValue, DescriptorValue, ContextValue) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(PropertyName))) { \
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE; \
            OutValue->Value.HostHandle = (HandleValue); \
            OutValue->HostDescriptor = (DescriptorValue); \
            OutValue->HostContext = (ContextValue); \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

#define EXPOSE_SET_HOST_HANDLE(HandleValue, DescriptorValue, ContextValue, OwnsHandle) \
    do { \
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE)); \
        OutValue->Type = SCRIPT_VAR_HOST_HANDLE; \
        OutValue->Value.HostHandle = (HandleValue); \
        OutValue->HostDescriptor = (DescriptorValue); \
        OutValue->HostContext = (ContextValue); \
        OutValue->OwnsValue = (OwnsHandle); \
    } while (0)

#define EXPOSE_LIST_ARRAY_GET_ELEMENT(FunctionName, ItemType, ValidMacro, ValidId, DescriptorValue) \
    SCRIPT_ERROR FunctionName(LPVOID Context, SCRIPT_HOST_HANDLE Parent, U32 Index, LPSCRIPT_VALUE OutValue) { \
        UNUSED(Context); \
        EXPOSE_ARRAY_GUARD(); \
        LPLIST List = (LPLIST)Parent; \
        if (List == NULL || Index >= ListGetSize(List)) { \
            return SCRIPT_ERROR_UNDEFINED_VAR; \
        } \
        ItemType Item = (ItemType)ListGetItem(List, Index); \
        ValidMacro(Item, ValidId) { \
            EXPOSE_SET_HOST_HANDLE(Item, DescriptorValue, NULL, FALSE); \
            return SCRIPT_OK; \
        } \
        return SCRIPT_ERROR_UNDEFINED_VAR; \
    }

/************************************************************************/

typedef struct tag_USER_ACCOUNT USER_ACCOUNT, *LPUSER_ACCOUNT;

LPPROCESS ExposeGetCallerProcess(void);
LPUSER_ACCOUNT ExposeGetCallerUser(void);
BOOL ExposeIsKernelCaller(void);
BOOL ExposeIsAdminCaller(void);
BOOL ExposeIsSameUser(LPPROCESS Caller, LPPROCESS Target);
BOOL ExposeIsOwnerProcess(LPPROCESS Caller, LPPROCESS Target);
BOOL ExposeCanReadProcess(LPPROCESS Caller, LPPROCESS Target, UINT RequiredAccess);
BOOL ExposeRegisterDefaultScriptHostObjects(LPSCRIPT_CONTEXT Context);

/************************************************************************/

SCRIPT_ERROR ProcessGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR ProcessArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR ProcessDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR ProcessArrayDescriptor;

SCRIPT_ERROR AccountGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR AccountArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR AccountArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR AccountDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR AccountArrayDescriptor;

SCRIPT_ERROR TaskGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR TaskArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR TaskArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR TaskRootArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR TaskRootArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR ArchitectureTaskDataGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR StackGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR TaskDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR TaskArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR TaskRootArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR ArchitectureTaskDataDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR StackDescriptor;

SCRIPT_ERROR UsbGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbPortArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDriveGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDriveArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbDriveArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbNodeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbNodeArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR UsbNodeArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR UsbDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbPortDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbPortArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDeviceDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDeviceArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDriveDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbDriveArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbNodeDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR UsbNodeArrayDescriptor;
extern SCRIPT_HOST_HANDLE UsbRootHandle;

/************************************************************************/

SCRIPT_ERROR PciBusGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR PciBusArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR PciBusArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR PciDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR PciDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR PciDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR PciBusDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR PciBusArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR PciDeviceDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR PciDeviceArrayDescriptor;

/************************************************************************/

SCRIPT_ERROR DriverGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverEnumDomainArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverEnumDomainArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverModeArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverModeArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR DriverModeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR DriverDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR DriverArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR DriverEnumDomainArrayDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR DriverModeDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR DriverModeArrayDescriptor;

/************************************************************************/

SCRIPT_ERROR GraphicsGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR GraphicsModeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR GraphicsDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR GraphicsModeDescriptor;
SCRIPT_HOST_HANDLE GetGraphicsRootHandle(void);

/************************************************************************/

SCRIPT_ERROR ClockGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR ClockDateTimeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR ClockDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR ClockBootDateTimeDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR ClockCurrentDateTimeDescriptor;
SCRIPT_HOST_HANDLE GetClockRootHandle(void);

/************************************************************************/

SCRIPT_ERROR StorageGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR StorageArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR StorageArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR StorageDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR StorageArrayDescriptor;

/************************************************************************/

SCRIPT_ERROR FileSystemRootGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR FileSystemGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR FileSystemArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR FileSystemArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR FileSystemRootDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR FileSystemDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR FileSystemArrayDescriptor;
extern SCRIPT_HOST_HANDLE FileSystemRootHandle;

/************************************************************************/

SCRIPT_ERROR MemoryMapRootGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR MemoryRegionDescriptorGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR MemoryRegionArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR MemoryRegionArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR MemoryMapRootDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR MemoryRegionDescriptorDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR MemoryRegionArrayDescriptor;
extern SCRIPT_HOST_HANDLE MemoryMapRootHandle;

/************************************************************************/

SCRIPT_ERROR NetworkGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR NetworkDeviceGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR NetworkDeviceArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR NetworkDeviceArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue);

extern const SCRIPT_HOST_DESCRIPTOR NetworkDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR NetworkDeviceDescriptor;
extern const SCRIPT_HOST_DESCRIPTOR NetworkDeviceArrayDescriptor;
extern SCRIPT_HOST_HANDLE NetworkRootHandle;

/************************************************************************/

SCRIPT_ERROR KeyboardGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

SCRIPT_ERROR MouseGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue);

const SCRIPT_HOST_DESCRIPTOR *GetKeyboardDescriptor(void);
const SCRIPT_HOST_DESCRIPTOR *GetMouseDescriptor(void);
SCRIPT_HOST_HANDLE GetKeyboardRootHandle(void);
SCRIPT_HOST_HANDLE GetMouseRootHandle(void);

/************************************************************************/
