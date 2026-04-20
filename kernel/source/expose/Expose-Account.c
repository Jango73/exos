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


    Script Exposure Helpers - Accounts

\************************************************************************/

#include "expose/Exposed.h"

#include "user/Account.h"

/************************************************************************/

#define EXPOSE_ACCESS_ACCOUNT_DETAILS (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

/**
 * @brief Retrieve a property from one exposed account.
 * @param Context Host callback context (unused for accounts).
 * @param Parent Handle to the account.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, otherwise a script error.
 */
SCRIPT_ERROR AccountGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    if (!ExposeCanReadProcess(
            ExposeGetCallerProcess(),
            NULL,
            EXPOSE_ACCESS_ACCOUNT_DETAILS)) {
        return SCRIPT_ERROR_UNAUTHORIZED;
    }

    LPUSER_ACCOUNT Account = (LPUSER_ACCOUNT)Parent;
    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        EXPOSE_BIND_STRING("name", Account->UserName);
        EXPOSE_BIND_INTEGER("privilege", Account->Privilege);
        EXPOSE_BIND_INTEGER("status", Account->Status);
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property from the exposed account root.
 * @param Context Host callback context (unused for accounts).
 * @param Parent Handle to the account root.
 * @param Property Property name requested by the script.
 * @param OutValue Output holder for the property value.
 * @return SCRIPT_OK when the property exists, otherwise a script error.
 */
SCRIPT_ERROR AccountArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Parent);

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_BIND_INTEGER("count", GetAccountCount());

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve one exposed account by array index.
 * @param Context Host callback context (unused for accounts).
 * @param Parent Handle to the account root.
 * @param Index Zero-based account index requested by the script.
 * @param OutValue Output holder for the resulting account handle.
 * @return SCRIPT_OK when the account exists, otherwise a script error.
 */
SCRIPT_ERROR AccountArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {
    LPUSER_ACCOUNT Account;

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    if (!ExposeCanReadProcess(
            ExposeGetCallerProcess(),
            NULL,
            EXPOSE_ACCESS_ACCOUNT_DETAILS)) {
        return SCRIPT_ERROR_UNAUTHORIZED;
    }

    if (Parent == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    Account = GetAccountByIndex(Index);
    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        EXPOSE_SET_HOST_HANDLE(Account, &AccountDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR AccountDescriptor = {
    AccountGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR AccountArrayDescriptor = {
    AccountArrayGetProperty,
    AccountArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
