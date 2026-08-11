
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


    Generic Storage Transfer Layer

\************************************************************************/

#ifndef STORAGE_TRANSFER_LAYER_H_INCLUDED
#define STORAGE_TRANSFER_LAYER_H_INCLUDED

/***************************************************************************/

#include "fs/Storage.h"

/***************************************************************************/

/**
 * @brief Attach the generic storage transfer state (sector cache, buffer pool,
 *        per-command transfer limit) to a storage unit.
 * @param Storage Storage unit to attach the layer to.
 * @param MaxSectorsPerTransfer Maximum sectors the driver accepts per command.
 * @param ConfigMaxSectorsPath Optional configuration key resolving the transfer
 *        limit lazily on first use (may be NULL).
 * @return TRUE on success, FALSE on failure.
 */
BOOL StorageTransferLayerInit(LPSTORAGE_UNIT Storage, U32 MaxSectorsPerTransfer, LPCSTR ConfigMaxSectorsPath);

/***************************************************************************/

/**
 * @brief Release the generic storage transfer state attached to a storage unit.
 * @param Storage Storage unit to detach.
 */
void StorageTransferLayerDeinit(LPSTORAGE_UNIT Storage);

/***************************************************************************/

/**
 * @brief Read sectors through the generic layer (cache lookup, merge of
 *        contiguous runs, chunking to the driver transfer limit).
 * @param Control IO control structure describing the request.
 * @return DF_RETURN_SUCCESS or a driver error code.
 */
U32 StorageTransferLayerRead(LPIOCONTROL Control);

/***************************************************************************/

/**
 * @brief Write sectors through the generic layer (chunking to the driver
 *        transfer limit, write-through cache update).
 * @param Control IO control structure describing the request.
 * @return DF_RETURN_SUCCESS or a driver error code.
 */
U32 StorageTransferLayerWrite(LPIOCONTROL Control);

/***************************************************************************/

#endif  // STORAGE_TRANSFER_LAYER_H_INCLUDED
