
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


    Script Exposure Helpers - Driver

\************************************************************************/

#include "expose/Exposed.h"

#include "core/Driver.h"
#include "core/DriverGetters.h"
#include "GFX.h"
#include "core/KernelData.h"

/************************************************************************/

#define EXPOSE_ACCESS_DRIVER (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

/**
 * @brief Check whether one driver can expose a graphics mode catalog.
 * @param Driver Driver to inspect.
 * @return TRUE when the driver is a concrete graphics backend.
 */
static BOOL DriverSupportsModeExposure(LPDRIVER Driver) {
    return Driver != NULL &&
           Driver->Type == DRIVER_TYPE_GRAPHICS &&
           Driver->Command != NULL &&
           Driver != GraphicsSelectorGetDriver();
}

/************************************************************************/

/**
 * @brief Query the supported graphics mode count for one driver.
 * @param Driver Driver to query.
 * @return Mode count, or 0 when unavailable.
 */
static U32 DriverGetGraphicsModeCountInternal(LPDRIVER Driver) {
    if (!DriverSupportsModeExposure(Driver)) {
        return 0;
    }

    if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
        if (Driver->Command(DF_LOAD, 0) != DF_RETURN_SUCCESS ||
            (Driver->Flags & DRIVER_FLAG_READY) == 0) {
            return 0;
        }
    }

    return Driver->Command(DF_GFX_GETMODECOUNT, 0);
}

/************************************************************************/

/**
 * @brief Query one graphics mode descriptor from one driver.
 * @param Driver Driver to query.
 * @param ModeIndex Mode index.
 * @param ModeInfo Output holder.
 * @return TRUE on success.
 */
static BOOL DriverGetGraphicsModeInfoInternal(
    LPDRIVER Driver,
    U32 ModeIndex,
    LPGRAPHICS_MODE_INFO ModeInfo) {
    U32 ModeCount = 0;

    if (ModeInfo == NULL) {
        return FALSE;
    }

    ModeCount = DriverGetGraphicsModeCountInternal(Driver);
    if (ModeIndex >= ModeCount) {
        return FALSE;
    }

    MemorySet(ModeInfo, 0, sizeof(GRAPHICS_MODE_INFO));
    ModeInfo->Header.Size = sizeof(GRAPHICS_MODE_INFO);
    ModeInfo->Header.Version = EXOS_ABI_VERSION;
    ModeInfo->Header.Flags = 0;
    ModeInfo->ModeIndex = ModeIndex;

    return Driver->Command(DF_GFX_GETMODEINFO, (UINT)(LPVOID)ModeInfo) == DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a driver exposed to the script engine.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_BIND_STRING("alias", Driver->Alias);
        EXPOSE_BIND_STRING("typeName", DriverTypeToText(Driver->Type));
        EXPOSE_BIND_INTEGER("type", Driver->Type);
        EXPOSE_BIND_STRING("product", Driver->Product);
        EXPOSE_BIND_INTEGER("ready", (Driver->Flags & DRIVER_FLAG_READY) != 0);
        EXPOSE_BIND_HOST_HANDLE("mode", Driver, &DriverModeArrayDescriptor, NULL);

        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

        EXPOSE_BIND_INTEGER("versionMajor", Driver->VersionMajor);
        EXPOSE_BIND_INTEGER("versionMinor", Driver->VersionMinor);
        EXPOSE_BIND_STRING("designer", Driver->Designer);
        EXPOSE_BIND_STRING("manufacturer", Driver->Manufacturer);
        EXPOSE_BIND_INTEGER("flags", Driver->Flags);
        EXPOSE_BIND_INTEGER("enumDomainCount", Driver->EnumDomainCount);

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("enumDomain"))) {
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = Driver;
            OutValue->HostDescriptor = &DriverEnumDomainArrayDescriptor;
            OutValue->HostContext = NULL;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed driver mode array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverModeArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_BIND_INTEGER("count", DriverGetGraphicsModeCountInternal(Driver));
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one mode view from the exposed driver mode array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting mode view
 * @return SCRIPT_OK when the mode exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverModeArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (Index >= DriverGetGraphicsModeCountInternal(Driver)) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        EXPOSE_SET_HOST_HANDLE(Driver, &DriverModeDescriptor, (LPVOID)(LINEAR)(Index + 1), FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from one exposed graphics mode.
 * @param Context Host callback context containing the mode index plus one
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverModeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    GRAPHICS_MODE_INFO ModeInfo;
    LPDRIVER Driver = (LPDRIVER)Parent;
    U32 ModeIndex = 0;

    EXPOSE_PROPERTY_GUARD();

    if (Context == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    ModeIndex = (U32)(LINEAR)Context - 1;

    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (!DriverGetGraphicsModeInfoInternal(Driver, ModeIndex, &ModeInfo)) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        EXPOSE_BIND_INTEGER("width", ModeInfo.Width);
        EXPOSE_BIND_INTEGER("height", ModeInfo.Height);
        EXPOSE_BIND_INTEGER("bpp", ModeInfo.BitsPerPixel);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed driver array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST DriverList = (LPLIST)Parent;
    if (DriverList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(DriverList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a driver from the exposed driver array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting driver handle
 * @return SCRIPT_OK when the driver exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPLIST DriverList = (LPLIST)Parent;
    if (DriverList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(DriverList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPDRIVER Driver = (LPDRIVER)ListGetItem(DriverList, Index);
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_SET_HOST_HANDLE(Driver, &DriverDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed driver enum domain array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverEnumDomainArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        EXPOSE_BIND_INTEGER("count", Driver->EnumDomainCount);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve an enum domain from the exposed driver enum domain array.
 * @param Context Host callback context (unused for driver exposure)
 * @param Parent Handle to the driver instance requested by the script
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting enum domain value
 * @return SCRIPT_OK when the domain exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR DriverEnumDomainArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();
    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_DRIVER, NULL);

    LPDRIVER Driver = (LPDRIVER)Parent;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (Index >= Driver->EnumDomainCount) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        OutValue->Type = SCRIPT_VAR_INTEGER;
        OutValue->Value.Integer = (INT)Driver->EnumDomains[Index];
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR DriverDescriptor = {
    DriverGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverArrayDescriptor = {
    DriverArrayGetProperty,
    DriverArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverEnumDomainArrayDescriptor = {
    DriverEnumDomainArrayGetProperty,
    DriverEnumDomainArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverModeDescriptor = {
    DriverModeGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR DriverModeArrayDescriptor = {
    DriverModeArrayGetProperty,
    DriverModeArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
