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


    Script Exposure Helpers - File System

\************************************************************************/

#include "expose/Exposed.h"

#include "fs/FileSystem.h"
#include "core/KernelData.h"

/************************************************************************/

static int DATA_SECTION FileSystemRootSentinel = 0;

SCRIPT_HOST_HANDLE FileSystemRootHandle = &FileSystemRootSentinel;

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed file system root object.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemRootGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_STRING("activePartitionName", GetFileSystemGlobalInfo()->ActivePartitionName);
    EXPOSE_BIND_HOST_HANDLE("mounted", GetFileSystemList(), &FileSystemArrayDescriptor, NULL);
    EXPOSE_BIND_HOST_HANDLE("unused", GetUnusedFileSystemList(), &FileSystemArrayDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from an exposed file system object.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    DISKINFO DiskInfo;
    LPSTORAGE_UNIT StorageUnit = NULL;
    BOOL DiskInfoValid = FALSE;

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPFILESYSTEM FileSystem = (LPFILESYSTEM)Parent;
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        StorageUnit = FileSystemGetStorageUnit(FileSystem);

        EXPOSE_BIND_STRING("name", FileSystem->Name);
        EXPOSE_BIND_INTEGER("mounted", FileSystem->Mounted);
        EXPOSE_BIND_STRING("schemeName", FileSystemGetPartitionSchemeName(FileSystem->Partition.Scheme));
        EXPOSE_BIND_STRING("typeName", FileSystemGetPartitionTypeName(&FileSystem->Partition));
        EXPOSE_BIND_STRING("formatName", FileSystemGetPartitionFormatName(FileSystem->Partition.Format));
        EXPOSE_BIND_INTEGER("scheme", FileSystem->Partition.Scheme);
        EXPOSE_BIND_INTEGER("type", FileSystem->Partition.Type);
        EXPOSE_BIND_INTEGER("format", FileSystem->Partition.Format);
        EXPOSE_BIND_INTEGER("index", FileSystem->Partition.Index);
        EXPOSE_BIND_INTEGER("flags", FileSystem->Partition.Flags);
        EXPOSE_BIND_INTEGER("startSector", FileSystem->Partition.StartSector);
        EXPOSE_BIND_INTEGER("numSectors", FileSystem->Partition.NumSectors);
        EXPOSE_BIND_INTEGER("typeGuid0", FileSystem->Partition.TypeGuid[0]);
        EXPOSE_BIND_INTEGER("typeGuid1", FileSystem->Partition.TypeGuid[1]);
        EXPOSE_BIND_INTEGER("typeGuid2", FileSystem->Partition.TypeGuid[2]);
        EXPOSE_BIND_INTEGER("typeGuid3", FileSystem->Partition.TypeGuid[3]);
        EXPOSE_BIND_INTEGER("typeGuid4", FileSystem->Partition.TypeGuid[4]);
        EXPOSE_BIND_INTEGER("typeGuid5", FileSystem->Partition.TypeGuid[5]);
        EXPOSE_BIND_INTEGER("typeGuid6", FileSystem->Partition.TypeGuid[6]);
        EXPOSE_BIND_INTEGER("typeGuid7", FileSystem->Partition.TypeGuid[7]);
        EXPOSE_BIND_INTEGER("typeGuid8", FileSystem->Partition.TypeGuid[8]);
        EXPOSE_BIND_INTEGER("typeGuid9", FileSystem->Partition.TypeGuid[9]);
        EXPOSE_BIND_INTEGER("typeGuid10", FileSystem->Partition.TypeGuid[10]);
        EXPOSE_BIND_INTEGER("typeGuid11", FileSystem->Partition.TypeGuid[11]);
        EXPOSE_BIND_INTEGER("typeGuid12", FileSystem->Partition.TypeGuid[12]);
        EXPOSE_BIND_INTEGER("typeGuid13", FileSystem->Partition.TypeGuid[13]);
        EXPOSE_BIND_INTEGER("typeGuid14", FileSystem->Partition.TypeGuid[14]);
        EXPOSE_BIND_INTEGER("typeGuid15", FileSystem->Partition.TypeGuid[15]);

        if (FileSystem->Driver != NULL) {
            EXPOSE_BIND_STRING("driverManufacturer", FileSystem->Driver->Manufacturer);
            EXPOSE_BIND_STRING("driverProduct", FileSystem->Driver->Product);
        } else {
            EXPOSE_BIND_STRING("driverManufacturer", TEXT(""));
            EXPOSE_BIND_STRING("driverProduct", TEXT(""));
        }

        EXPOSE_BIND_INTEGER("hasStorage", StorageUnit != NULL);
        if (StorageUnit != NULL && StorageUnit->Driver != NULL) {
            MemorySet(&DiskInfo, 0, sizeof(DiskInfo));
            DiskInfo.Disk = StorageUnit;
            DiskInfoValid = (StorageUnit->Driver->Command(DF_DISK_GETINFO, (UINT)&DiskInfo) == DF_RETURN_SUCCESS);

            EXPOSE_BIND_STRING("storageManufacturer", StorageUnit->Driver->Manufacturer);
            EXPOSE_BIND_STRING("storageProduct", StorageUnit->Driver->Product);
            EXPOSE_BIND_INTEGER("removable", DiskInfoValid ? DiskInfo.Removable : 0);
            EXPOSE_BIND_INTEGER("readOnly", DiskInfoValid ? ((DiskInfo.Access & DISK_ACCESS_READONLY) != 0) : 0);
            EXPOSE_BIND_INTEGER("diskNumSectorsLow", DiskInfoValid ? (U32)U64_Low32(DiskInfo.NumSectors) : 0);
            EXPOSE_BIND_INTEGER("diskNumSectorsHigh", DiskInfoValid ? (U32)U64_High32(DiskInfo.NumSectors) : 0);
        } else {
            EXPOSE_BIND_STRING("storageManufacturer", TEXT(""));
            EXPOSE_BIND_STRING("storageProduct", TEXT(""));
            EXPOSE_BIND_INTEGER("removable", 0);
            EXPOSE_BIND_INTEGER("readOnly", 0);
            EXPOSE_BIND_INTEGER("diskNumSectorsLow", 0);
            EXPOSE_BIND_INTEGER("diskNumSectorsHigh", 0);
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed file system array.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST FileSystemList = (LPLIST)Parent;
    if (FileSystemList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(FileSystemList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one file system from the exposed file system array.
 * @param Context Host callback context (unused for file system exposure)
 * @param Parent Handle to the file system list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting file system handle
 * @return SCRIPT_OK when the file system exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR FileSystemArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST FileSystemList = (LPLIST)Parent;
    if (FileSystemList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(FileSystemList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPFILESYSTEM FileSystem = (LPFILESYSTEM)ListGetItem(FileSystemList, Index);
    SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
        EXPOSE_SET_HOST_HANDLE(FileSystem, &FileSystemDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR FileSystemRootDescriptor = {
    FileSystemRootGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR FileSystemDescriptor = {
    FileSystemGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR FileSystemArrayDescriptor = {
    FileSystemArrayGetProperty,
    FileSystemArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
