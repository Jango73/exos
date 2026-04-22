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


    Script Exposure Helpers - Clock

\************************************************************************/

#include "expose/Exposed.h"

#include "core/DriverGetters.h"
#include "system/Clock.h"

/************************************************************************/

/**
 * @brief Bind one DATETIME property for script access.
 * @param DateTime Source date-time value.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise.
 */
static SCRIPT_ERROR ClockBindDateTimeProperty(LPDATETIME DateTime, LPCSTR Property, LPSCRIPT_VALUE OutValue) {
    if (DateTime == NULL || Property == NULL || OutValue == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("year", DateTime->Year);
    EXPOSE_BIND_INTEGER("month", DateTime->Month);
    EXPOSE_BIND_INTEGER("day", DateTime->Day);
    EXPOSE_BIND_INTEGER("hour", DateTime->Hour);
    EXPOSE_BIND_INTEGER("minute", DateTime->Minute);
    EXPOSE_BIND_INTEGER("second", DateTime->Second);
    EXPOSE_BIND_INTEGER("milli", DateTime->Milli);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed boot date-time object.
 * @param Context Host callback context (unused for clock exposure).
 * @param Parent Handle to the boot date-time object.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise.
 */
static SCRIPT_ERROR ClockBootDateTimeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    DATETIME BootTime;

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    if (GetBootLocalTime(&BootTime) == FALSE) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return ClockBindDateTimeProperty(&BootTime, Property, OutValue);
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed current date-time object.
 * @param Context Host callback context (unused for clock exposure).
 * @param Parent Handle to the current date-time object.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise.
 */
SCRIPT_ERROR ClockDateTimeGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    DATETIME CurrentTime;

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    if (GetLocalTime(&CurrentTime) == FALSE) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return ClockBindDateTimeProperty(&CurrentTime, Property, OutValue);
}

/************************************************************************/

/**
 * @brief Retrieve one property from the exposed clock root object.
 * @param Context Host callback context (unused for clock exposure).
 * @param Parent Handle to the clock root object.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise.
 */
SCRIPT_ERROR ClockGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("uptimeMs", GetSystemTime());
    EXPOSE_BIND_HOST_HANDLE("bootDatetime", GetClockRootHandle(), &ClockBootDateTimeDescriptor, NULL);
    EXPOSE_BIND_HOST_HANDLE("currentDatetime", GetClockRootHandle(), &ClockCurrentDateTimeDescriptor, NULL);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR ClockDescriptor = {
    ClockGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ClockBootDateTimeDescriptor = {
    ClockBootDateTimeGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ClockCurrentDateTimeDescriptor = {
    ClockDateTimeGetProperty,
    NULL,
    NULL,
    NULL
};

/************************************************************************/

/**
 * @brief Retrieve the clock root handle for script exposure.
 * @return Host handle for the clock root object.
 */
SCRIPT_HOST_HANDLE GetClockRootHandle(void) {
    return ClockGetDriver();
}

/************************************************************************/
