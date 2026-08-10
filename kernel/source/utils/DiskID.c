
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


    Stable disk identification

\************************************************************************/

#include "utils/DiskID.h"

#include "core/KernelData.h"
#include "log/Log.h"
#include "text/CoreString.h"

/***************************************************************************/

static BOOL DiskIdIsAllowedChar(STR Character) {
    if (Character >= 'A' && Character <= 'Z') return TRUE;
    if (Character >= 'a' && Character <= 'z') return TRUE;
    if (Character >= '0' && Character <= '9') return TRUE;
    if (Character == '_' || Character == '-' || Character == '.') return TRUE;
    return FALSE;
}

/***************************************************************************/

static void DiskIdSanitize(LPSTR Output, UINT OutputSize, LPCSTR Input) {
    UINT Index;
    UINT OutputIndex = 0;
    UINT Length;

    if (Output == NULL || OutputSize == 0) return;

    Output[0] = STR_NULL;

    if (Input == NULL) return;

    Length = StringLength(Input);
    if (Length > (OutputSize - 1)) Length = (OutputSize - 1);

    for (Index = 0; Index < Length; Index++) {
        Output[OutputIndex] = DiskIdIsAllowedChar(Input[Index]) ? Input[Index] : '_';
        OutputIndex++;
    }
    Output[OutputIndex] = STR_NULL;

    while (OutputIndex > 0 && Output[OutputIndex - 1] == '_') {
        OutputIndex--;
        Output[OutputIndex] = STR_NULL;
    }
}

/***************************************************************************/

/**
 * @brief Store the hardware identity of a storage unit.
 *
 * Each input is sanitized (unsafe characters replaced with underscores) and
 * copied into the storage unit. Any of the three parts may be NULL or empty
 * when the hardware does not provide it.
 *
 * @param Storage Target storage unit.
 * @param Vendor Hardware vendor string.
 * @param Model Hardware model string.
 * @param Serial Hardware serial string.
 */
void DiskIdSetIdentity(LPSTORAGE_UNIT Storage, LPCSTR Vendor, LPCSTR Model, LPCSTR Serial) {
    if (Storage == NULL) return;

    DiskIdSanitize(Storage->Vendor, sizeof(Storage->Vendor), Vendor);
    DiskIdSanitize(Storage->Model, sizeof(Storage->Model), Model);
    DiskIdSanitize(Storage->Serial, sizeof(Storage->Serial), Serial);
}

/***************************************************************************/

static BOOL DiskIdAppendPart(LPSTR Output, UINT OutputSize, LPCSTR Part) {
    UINT Length;
    UINT PartLength;

    if (Output == NULL || Part == NULL || Part[0] == STR_NULL) return FALSE;

    Length = StringLength(Output);
    PartLength = StringLength(Part);

    if (Length == 0) {
        if ((PartLength + 1) > OutputSize) return FALSE;
        StringCopy(Output, Part);
        return TRUE;
    }

    if ((Length + PartLength + 2) > OutputSize) return FALSE;

    Output[Length] = '_';
    StringCopy(&Output[Length + 1], Part);
    return TRUE;
}

/***************************************************************************/

static void DiskIdBuildSynthetic(LPSTORAGE_UNIT Storage) {
    LPLIST DiskList;
    UINT Index;
    UINT Count = 0;

    if (Storage == NULL || Storage->Driver == NULL) return;

    DiskList = GetDiskList();
    if (DiskList != NULL) {
        for (Index = 0; Index < ListGetSize(DiskList); Index++) {
            LPSTORAGE_UNIT Other = (LPSTORAGE_UNIT)ListGetItem(DiskList, Index);
            if (Other != NULL && Other->Driver != NULL && Other->Driver->Type == Storage->Driver->Type) {
                Count++;
            }
        }
    }

    StringPrintFormat(Storage->StorageId, TEXT("%s_%u"), Storage->Driver->Alias, Count);
}

/***************************************************************************/

/**
 * @brief Build the stable ID of a storage unit if not already present.
 *
 * The ID is composed as vendor_model_serial following the Linux
 * /dev/disk/by-id model. Empty parts are skipped. Storage units without any
 * hardware identity (for example the RAM disk) receive a deterministic
 * synthetic ID derived from the driver alias and the count of disks already
 * registered for the same driver type.
 *
 * @param Storage Target storage unit.
 * @return Pointer to the stable ID string.
 */
LPCSTR DiskIdEnsure(LPSTORAGE_UNIT Storage) {
    if (Storage == NULL) return NULL;

    if (Storage->StorageId[0] != STR_NULL) return Storage->StorageId;

    DiskIdAppendPart(Storage->StorageId, sizeof(Storage->StorageId), Storage->Vendor);
    DiskIdAppendPart(Storage->StorageId, sizeof(Storage->StorageId), Storage->Model);
    DiskIdAppendPart(Storage->StorageId, sizeof(Storage->StorageId), Storage->Serial);

    if (Storage->StorageId[0] == STR_NULL) {
        DiskIdBuildSynthetic(Storage);
    }

    if (Storage->StorageId[0] == STR_NULL) {
        ERROR(TEXT("[DiskIdEnsure] Unable to build an ID for storage unit"));
    }

    return Storage->StorageId;
}

/***************************************************************************/

/**
 * @brief Return the stable ID of a storage unit.
 * @param Storage Target storage unit.
 * @return Pointer to the stable ID string.
 */
LPCSTR DiskIdGet(LPSTORAGE_UNIT Storage) {
    return DiskIdEnsure(Storage);
}

/***************************************************************************/

/**
 * @brief Find a storage unit by its stable ID.
 * @param Id Stable ID to look up.
 * @return Matching storage unit or NULL when not found.
 */
LPSTORAGE_UNIT DiskIdFindById(LPCSTR Id) {
    LPLIST DiskList;
    UINT Index;

    if (Id == NULL || Id[0] == STR_NULL) return NULL;

    DiskList = GetDiskList();
    if (DiskList == NULL) return NULL;

    for (Index = 0; Index < ListGetSize(DiskList); Index++) {
        LPSTORAGE_UNIT Storage = (LPSTORAGE_UNIT)ListGetItem(DiskList, Index);
        if (Storage == NULL) continue;
        if (StringCompare(DiskIdGet(Storage), Id) == 0) {
            return Storage;
        }
    }

    return NULL;
}

/***************************************************************************/
