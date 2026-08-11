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

#include "fs/Storage.h"
#include "utils/Storage-ID.h"

/************************************************************************/

/**
 * @brief Retrieve a property value from a storage object exposed to the script engine.
 * @param Context Host callback context (unused for storage exposure)
 * @param Parent Handle to the storage instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StorageGetProperty(LPVOID Context, SCRIPT_HOST_HANDLE Parent, LPCSTR Property, LPSCRIPT_VALUE OutValue) {
    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPSTORAGE_UNIT Storage = (LPSTORAGE_UNIT)Parent;
    SAFE_USE_VALID_ID(Storage, KOID_STORAGE) {
        STORAGE_INFO StorageInfo;
        U32 Result = 0;

        MemorySet(&StorageInfo, 0, sizeof(STORAGE_INFO));
        StorageInfo.Storage = Storage;
        Result = Storage->Driver->Command(DF_STORAGE_GETINFO, (UINT)&StorageInfo);
        if (Result != DF_RETURN_SUCCESS) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        EXPOSE_BIND_INTEGER("type", StorageInfo.Type);
        EXPOSE_BIND_INTEGER("removable", StorageInfo.Removable);
        EXPOSE_BIND_INTEGER("bytesPerSector", StorageInfo.BytesPerSector);
        EXPOSE_BIND_INTEGER("numSectorsLow", (U32)U64_Low32(StorageInfo.NumSectors));
        EXPOSE_BIND_INTEGER("numSectorsHigh", (U32)U64_High32(StorageInfo.NumSectors));
        EXPOSE_BIND_INTEGER("access", StorageInfo.Access);
        EXPOSE_BIND_STRING("driverManufacturer", Storage->Driver->Manufacturer);
        EXPOSE_BIND_STRING("driverProduct", Storage->Driver->Product);

        EXPOSE_BIND_STRING("id", StorageIdGet(Storage));
        EXPOSE_BIND_STRING("vendor", Storage->Vendor);
        EXPOSE_BIND_STRING("model", Storage->Model);
        EXPOSE_BIND_STRING("serial", Storage->Serial);

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
    LPVOID Context, SCRIPT_HOST_HANDLE Parent, LPCSTR Property, LPSCRIPT_VALUE OutValue) {
    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST StorageList = (LPLIST)Parent;
    if (StorageList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(StorageList));
    EXPOSE_BIND_HOST_HANDLE("byId", StorageList, &StorageByIdDescriptor, NULL);

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
SCRIPT_ERROR StorageArrayGetElement(LPVOID Context, SCRIPT_HOST_HANDLE Parent, U32 Index, LPSCRIPT_VALUE OutValue) {
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
    SAFE_USE_VALID_ID(Storage, KOID_STORAGE) {
        EXPOSE_SET_HOST_HANDLE(Storage, &StorageDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a storage object from the exposed storage array by stable storage ID.
 * @param Context Host callback context (unused for storage exposure)
 * @param Parent Handle to the byId lookup container exposed by the kernel
 * @param Key Stable storage ID requested by the script
 * @param OutValue Output holder for the resulting storage handle
 * @return SCRIPT_OK when the storage object exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StorageByIdGetStringElement(
    LPVOID Context, SCRIPT_HOST_HANDLE Parent, LPCSTR Key, LPSCRIPT_VALUE OutValue) {
    UNUSED(Context);
    UNUSED(Parent);

    if (OutValue == NULL || Key == NULL || Key[0] == STR_NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPSTORAGE_UNIT Storage = StorageIdFindById(Key);
    SAFE_USE_VALID_ID(Storage, KOID_STORAGE) {
        EXPOSE_SET_HOST_HANDLE(Storage, &StorageDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR StorageDescriptor = { StorageGetProperty, NULL, NULL, NULL };
const SCRIPT_HOST_DESCRIPTOR StorageByIdDescriptor = { NULL, NULL, NULL, NULL, StorageByIdGetStringElement };

const SCRIPT_HOST_DESCRIPTOR StorageArrayDescriptor = { StorageArrayGetProperty, StorageArrayGetElement, NULL, NULL };

/************************************************************************/
