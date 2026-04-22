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


    Script Exposure Helpers - Graphics

\************************************************************************/

#include "expose/Exposed.h"

#include "DisplaySession.h"
#include "core/Driver.h"
#include "core/DriverGetters.h"
#include "core/KernelData.h"

/************************************************************************/

/**
 * @brief Convert one display front-end identifier to script text.
 * @param FrontEnd Display front-end identifier.
 * @return Constant script string.
 */
static LPCSTR GraphicsFrontEndToText(U32 FrontEnd) {
    switch (FrontEnd) {
        case DISPLAY_FRONTEND_CONSOLE:
            return TEXT("console");
        case DISPLAY_FRONTEND_DESKTOP:
            return TEXT("desktop");
        default:
            return TEXT("none");
    }
}

/************************************************************************/

/**
 * @brief Retrieve the active concrete graphics backend driver.
 * @return Active backend driver or NULL.
 */
static LPDRIVER GraphicsGetCurrentBackendDriver(void) {
    return GraphicsSelectorGetActiveBackendDriver();
}

/************************************************************************/

/**
 * @brief Resolve the global driver-list index of the active backend.
 * @return Zero-based driver index, or -1 when unavailable.
 */
static INT GraphicsGetCurrentDriverIndex(void) {
    LPLIST DriverList = GetDriverList();
    LPDRIVER ActiveDriver = GraphicsGetCurrentBackendDriver();
    UINT Index = 0;

    if (DriverList == NULL || ActiveDriver == NULL) {
        return -1;
    }

    for (LPLISTNODE Node = DriverList->First; Node != NULL; Node = Node->Next) {
        if ((LPDRIVER)Node == ActiveDriver) {
            return (INT)Index;
        }

        Index++;
    }

    return -1;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed graphics mode object.
 * @param Context Host callback context (unused for graphics exposure)
 * @param Parent Handle to the graphics root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR GraphicsModeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    GRAPHICS_MODE_INFO ModeInfo;

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    MemorySet(&ModeInfo, 0, sizeof(ModeInfo));
    if (DisplaySessionGetActiveMode(&ModeInfo) == FALSE) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("width", ModeInfo.Width);
    EXPOSE_BIND_INTEGER("height", ModeInfo.Height);
    EXPOSE_BIND_INTEGER("bpp", ModeInfo.BitsPerPixel);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed graphics root object.
 * @param Context Host callback context (unused for graphics exposure)
 * @param Parent Handle to the graphics root object
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR GraphicsGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    LPDRIVER CurrentDriver = NULL;

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    CurrentDriver = GraphicsGetCurrentBackendDriver();

    EXPOSE_BIND_STRING("frontend", GraphicsFrontEndToText(DisplaySessionGetActiveFrontEnd()));
    EXPOSE_BIND_INTEGER("currentDriverIndex", GraphicsGetCurrentDriverIndex());
    EXPOSE_BIND_STRING("currentDriverAlias", CurrentDriver != NULL ? CurrentDriver->Alias : TEXT(""));
    EXPOSE_BIND_HOST_HANDLE("mode", GetGraphicsRootHandle(), &GraphicsModeDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR GraphicsDescriptor = {
    GraphicsGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR GraphicsModeDescriptor = {
    GraphicsModeGetProperty,
    NULL,
    NULL,
    NULL
};

/************************************************************************/

/**
 * @brief Retrieve the graphics root handle for script exposure.
 * @return Host handle for the graphics root object.
 */
SCRIPT_HOST_HANDLE GetGraphicsRootHandle(void) {
    static INT DATA_SECTION GraphicsRootSentinel = 0;

    return &GraphicsRootSentinel;
}

/************************************************************************/
