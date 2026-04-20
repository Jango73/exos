
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


    Driver Enumeration

\************************************************************************/

#include "core/DriverEnum.h"

#include "core/Driver.h"
#include "core/KernelData.h"
#include "drivers/bus/PCI.h"

/************************************************************************/

static BOOL DriverSupportsDomain(LPDRIVER Driver, UINT Domain) {
    if (Driver == NULL || Driver->Command == NULL) {
        return FALSE;
    }

    for (UINT Index = 0; Index < Driver->EnumDomainCount && Index < DRIVER_ENUM_MAX_DOMAINS; Index++) {
        if (Driver->EnumDomains[Index] == Domain) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static BOOL DriverIsInList(LPDRIVER Driver, LPLIST List) {
    if (Driver == NULL || List == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = List->First; Node; Node = Node->Next) {
        if ((LPDRIVER)Node == Driver) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static BOOL DriverSeenInPciList(LPDRIVER Driver, LPLIST PciList, LPLISTNODE StopNode) {
    if (Driver == NULL || PciList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = PciList->First; Node && Node != StopNode; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver == Driver) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Returns the provider for an enumeration domain at a given index.
 * @param Query Enumeration query (domain filter).
 * @param ProviderIndex Index among providers that support the domain.
 * @param ProviderOut Receives the provider handle on success.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_NO_MORE otherwise.
 */
UINT KernelEnumGetProvider(const DRIVER_ENUM_QUERY* Query, UINT ProviderIndex, DRIVER_ENUM_PROVIDER* ProviderOut) {
    if (Query == NULL || ProviderOut == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Query->Header.Size < sizeof(DRIVER_ENUM_QUERY)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    UINT MatchIndex = 0;
    LPLIST DriverList = GetDriverList();

    if (DriverList != NULL) {
        for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
            LPDRIVER Driver = (LPDRIVER)Node;
            SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
                if (DriverSupportsDomain(Driver, Query->Domain)) {
                    if (MatchIndex == ProviderIndex) {
                        *ProviderOut = (DRIVER_ENUM_PROVIDER)Driver;
                        return DF_RETURN_SUCCESS;
                    }
                    MatchIndex++;
                }
            }
        }
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList != NULL) {
        for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
            LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
            LPDRIVER Driver = PciDevice->Driver;
            if (!DriverSupportsDomain(Driver, Query->Domain)) {
                continue;
            }
            if (DriverIsInList(Driver, DriverList)) {
                continue;
            }
            if (DriverSeenInPciList(Driver, PciList, Node)) {
                continue;
            }

            if (MatchIndex == ProviderIndex) {
                *ProviderOut = (DRIVER_ENUM_PROVIDER)Driver;
                return DF_RETURN_SUCCESS;
            }
            MatchIndex++;
        }
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

/**
 * @brief Enumerate next item for a provider.
 * @param Provider Provider handle returned by KernelEnumGetProvider.
 * @param Query Enumeration query (index is updated on success).
 * @param Item Receives the enumerated item.
 * @return Driver return code (DF_RETURN_SUCCESS or DF_RETURN_NO_MORE).
 */
UINT KernelEnumNext(DRIVER_ENUM_PROVIDER Provider, DRIVER_ENUM_QUERY* Query, DRIVER_ENUM_ITEM* Item) {
    if (Provider == NULL || Query == NULL || Item == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) || Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPDRIVER Driver = (LPDRIVER)Provider;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (Driver->Command == NULL) {
            return DF_RETURN_NOT_IMPLEMENTED;
        }

        DRIVER_ENUM_NEXT Next;
        MemorySet(&Next, 0, sizeof(Next));
        Next.Header.Size = sizeof(Next);
        Next.Header.Version = EXOS_ABI_VERSION;
        Next.Query = Query;
        Next.Item = Item;

        return Driver->Command(DF_ENUM_NEXT, (UINT)(LPVOID)&Next);
    }

    return DF_RETURN_BAD_PARAMETER;
}

/************************************************************************/

/**
 * @brief Pretty print an enumeration item through its provider.
 * @param Provider Provider handle returned by KernelEnumGetProvider.
 * @param Query Enumeration query.
 * @param Item Enumeration item to format.
 * @param Buffer Output buffer for formatted string.
 * @param BufferSize Output buffer size.
 * @return Driver return code.
 */
UINT KernelEnumPretty(DRIVER_ENUM_PROVIDER Provider, const DRIVER_ENUM_QUERY* Query, const DRIVER_ENUM_ITEM* Item,
                      LPSTR Buffer, UINT BufferSize) {
    if (Provider == NULL || Query == NULL || Item == NULL || Buffer == NULL || BufferSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) || Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPDRIVER Driver = (LPDRIVER)Provider;
    SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
        if (Driver->Command == NULL) {
            return DF_RETURN_NOT_IMPLEMENTED;
        }

        DRIVER_ENUM_PRETTY Pretty;
        MemorySet(&Pretty, 0, sizeof(Pretty));
        Pretty.Header.Size = sizeof(Pretty);
        Pretty.Header.Version = EXOS_ABI_VERSION;
        Pretty.Query = Query;
        Pretty.Item = Item;
        Pretty.Buffer = Buffer;
        Pretty.BufferSize = BufferSize;

        return Driver->Command(DF_ENUM_PRETTY, (UINT)(LPVOID)&Pretty);
    }

    return DF_RETURN_BAD_PARAMETER;
}
