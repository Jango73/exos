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


    Script Exposure Helpers - Storage

\************************************************************************/

#include "expose/Exposed.h"

#include "fs/Disk.h"

/************************************************************************/

/**
 * @brief Retrieve a property value from a storage object exposed to the script engine.
 * @param Context Host callback context (unused for storage exposure)
 * @param Parent Handle to the storage instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StorageGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPSTORAGE_UNIT Storage = (LPSTORAGE_UNIT)Parent;
    SAFE_USE_VALID_ID(Storage, KOID_DISK) {
        DISKINFO DiskInfo;
        U32 Result = 0;

        MemorySet(&DiskInfo, 0, sizeof(DISKINFO));
        DiskInfo.Disk = Storage;
        Result = Storage->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo);
        if (Result != DF_RETURN_SUCCESS) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        EXPOSE_BIND_INTEGER("type", DiskInfo.Type);
        EXPOSE_BIND_INTEGER("removable", DiskInfo.Removable);
        EXPOSE_BIND_INTEGER("bytesPerSector", DiskInfo.BytesPerSector);
        EXPOSE_BIND_INTEGER("numSectorsLow", (U32)U64_Low32(DiskInfo.NumSectors));
        EXPOSE_BIND_INTEGER("numSectorsHigh", (U32)U64_High32(DiskInfo.NumSectors));
        EXPOSE_BIND_INTEGER("access", DiskInfo.Access);
        EXPOSE_BIND_STRING("driverManufacturer", Storage->Driver->Manufacturer);
        EXPOSE_BIND_STRING("driverProduct", Storage->Driver->Product);

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed storage array.
 * @param Context Host callback context (unused for storage exposure)
 * @param Parent Handle to the storage list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StorageArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST StorageList = (LPLIST)Parent;
    if (StorageList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(StorageList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a storage object from the exposed storage array.
 * @param Context Host callback context (unused for storage exposure)
 * @param Parent Handle to the storage list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting storage handle
 * @return SCRIPT_OK when the storage object exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StorageArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST StorageList = (LPLIST)Parent;
    if (StorageList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(StorageList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPSTORAGE_UNIT Storage = (LPSTORAGE_UNIT)ListGetItem(StorageList, Index);
    SAFE_USE_VALID_ID(Storage, KOID_DISK) {
        EXPOSE_SET_HOST_HANDLE(Storage, &StorageDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR StorageDescriptor = {
    StorageGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR StorageArrayDescriptor = {
    StorageArrayGetProperty,
    StorageArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
