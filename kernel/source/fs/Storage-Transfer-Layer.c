
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

#include "fs/Storage-Transfer-Layer.h"

#include "core/Kernel.h"
#include "log/Profile.h"
#include "memory/Memory.h"
#include "system/Clock.h"
#include "utils/BufferPool.h"
#include "utils/Cache.h"
#include "utils/Helpers.h"

/***************************************************************************/
// Buffer pool configuration

#define STORAGE_TRANSFER_LAYER_BUFFER_FLAGS (ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE)
#define STORAGE_TRANSFER_LAYER_CACHE_CAPACITY NUM_BUFFERS
#define STORAGE_TRANSFER_LAYER_CONFIG_MIN_SECTORS 1
#define STORAGE_TRANSFER_LAYER_CONFIG_MAX_SECTORS 65535

/***************************************************************************/

struct tag_STORAGE_TRANSFER_UNIT {
    CACHE SectorCache;
    BUFFER_POOL SectorBufferPool;
    U32 MaxSectorsPerTransfer;
    LPCSTR ConfigMaxSectorsPath;
    UINT CachedConfigMaxSectors;
    BOOL ConfigInitialized;
};

/***************************************************************************/

/**
 * @brief Matcher callback for transfer layer sector cache entries.
 * @param Data Cache entry (LPSECTORBUFFER).
 * @param Context Matching context describing the requested sector.
 * @return TRUE if entry matches the requested sector.
 */
static BOOL StorageTransferCacheMatcher(LPVOID Data, LPVOID Context) {
    LPSECTORBUFFER Buffer = (LPSECTORBUFFER)Data;
    LPSECTORBUFFER Match = (LPSECTORBUFFER)Context;

    if (Buffer == NULL || Match == NULL) {
        return FALSE;
    }

    return Buffer->SectorLow == Match->SectorLow && Buffer->SectorHigh == Match->SectorHigh;
}

/***************************************************************************/

/**
 * @brief Release callback for transfer layer sector cache entries.
 * @param Data Cache entry payload (LPSECTORBUFFER).
 * @param Dirty Dirty flag from cache entry.
 * @param Context Buffer pool context pointer.
 */
static void StorageTransferCacheRelease(LPVOID Data, BOOL Dirty, LPVOID Context) {
    LPBUFFER_POOL Pool = (LPBUFFER_POOL)Context;

    UNUSED(Dirty);

    if (Data == NULL) {
        return;
    }

    if (Pool == NULL) {
        KernelHeapFree(Data);
        return;
    }

    BufferPoolRelease(Pool, Data);
}

/***************************************************************************/

/**
 * @brief Resolve the maximum sectors per transfer for a transfer unit.
 *
 * When a configuration path is supplied, the value is read lazily on first
 * use (the configuration file is loaded after the drivers), then cached. The
 * value passed at attach time acts as the default until the configuration is
 * available.
 *
 * @param Unit Transfer unit to resolve the limit for.
 * @return Maximum sectors accepted by the driver per command.
 */
static U32 StorageTransferGetMaxSectors(LPSTORAGE_TRANSFER_UNIT Unit) {
    if (Unit == NULL) {
        return 0;
    }

    if (Unit->ConfigMaxSectorsPath == NULL) {
        return Unit->MaxSectorsPerTransfer;
    }

    return (U32)GetConfigurationUIntLazy(
        &Unit->CachedConfigMaxSectors,
        &Unit->ConfigInitialized,
        Unit->ConfigMaxSectorsPath,
        Unit->MaxSectorsPerTransfer,
        STORAGE_TRANSFER_LAYER_CONFIG_MIN_SECTORS,
        STORAGE_TRANSFER_LAYER_CONFIG_MAX_SECTORS);
}

/***************************************************************************/

/**
 * @brief Attach the generic storage transfer state to a storage unit.
 *
 * Allocates the sector cache, the sector buffer pool and the per-command
 * transfer limit. A NULL transfer state is allowed for storage units without the
 * layer attached; in that case transfers fall back to the raw driver command.
 *
 * @param Storage Storage unit to attach the layer to.
 * @param MaxSectorsPerTransfer Maximum sectors the driver accepts per command.
 * @param ConfigMaxSectorsPath Optional configuration key resolving the transfer
 *        limit lazily on first use (may be NULL).
 * @return TRUE on success, FALSE on failure.
 */
BOOL StorageTransferLayerInit(LPSTORAGE_UNIT Storage, U32 MaxSectorsPerTransfer, LPCSTR ConfigMaxSectorsPath) {
    LPSTORAGE_TRANSFER_UNIT Unit;

    if (Storage == NULL || MaxSectorsPerTransfer == 0) {
        return FALSE;
    }

    Unit = (LPSTORAGE_TRANSFER_UNIT)KernelHeapAlloc(sizeof(STORAGE_TRANSFER_UNIT));
    if (Unit == NULL) {
        return FALSE;
    }

    MemorySet(Unit, 0, sizeof(STORAGE_TRANSFER_UNIT));

    if (!BufferPoolInit(
            &Unit->SectorBufferPool,
            (UINT)sizeof(SECTOR_BUFFER),
            STORAGE_TRANSFER_LAYER_CACHE_CAPACITY,
            1,
            STORAGE_TRANSFER_LAYER_BUFFER_FLAGS,
            TEXT("StorageTransferBuffer"))) {
        KernelHeapFree(Unit);
        return FALSE;
    }

    if (!BufferPoolReserve(&Unit->SectorBufferPool, STORAGE_TRANSFER_LAYER_CACHE_CAPACITY)) {
        BufferPoolDeinit(&Unit->SectorBufferPool);
        KernelHeapFree(Unit);
        return FALSE;
    }

    CacheInit(&Unit->SectorCache, STORAGE_TRANSFER_LAYER_CACHE_CAPACITY);
    if (Unit->SectorCache.Entries == NULL) {
        BufferPoolDeinit(&Unit->SectorBufferPool);
        KernelHeapFree(Unit);
        return FALSE;
    }

    CacheSetWritePolicy(
        &Unit->SectorCache, CACHE_WRITE_POLICY_READ_ONLY, NULL, StorageTransferCacheRelease, &Unit->SectorBufferPool);

    Unit->MaxSectorsPerTransfer = MaxSectorsPerTransfer;
    Unit->ConfigMaxSectorsPath = ConfigMaxSectorsPath;
    Unit->CachedConfigMaxSectors = 0;
    Unit->ConfigInitialized = FALSE;

    Storage->StorageTransfer = Unit;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release the generic storage transfer state attached to a storage unit.
 * @param Storage Storage unit to detach.
 */
void StorageTransferLayerDeinit(LPSTORAGE_UNIT Storage) {
    LPSTORAGE_TRANSFER_UNIT Unit;

    if (Storage == NULL) {
        return;
    }

    Unit = Storage->StorageTransfer;
    if (Unit == NULL) {
        return;
    }

    Storage->StorageTransfer = NULL;

    CacheDeinit(&Unit->SectorCache);
    BufferPoolDeinit(&Unit->SectorBufferPool);
    KernelHeapFree(Unit);
}

/***************************************************************************/

/**
 * @brief Read sectors through the generic transfer layer.
 *
 * The request is split into chunks bounded by the driver transfer limit. For
 * each chunk, cached sectors are copied from the cache and the uncached run
 * is served by a single raw driver read command, then cached.
 *
 * @param Control IO control structure describing the request.
 * @return DF_RETURN_SUCCESS or a driver error code.
 */
U32 StorageTransferLayerRead(LPIOCONTROL Control) {
    LPSTORAGE_TRANSFER_UNIT Unit;
    U32 MaxSectors;
    U32 Current;
    U32 Result;

    if (Control == NULL || Control->Storage == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Unit = Control->Storage->StorageTransfer;
    if (Unit == NULL) {
        return Control->Storage->Driver->Command(DF_STORAGE_READ, (UINT)Control);
    }

    PROFILE_SCOPED("StorageTransferLayerRead") {
        CacheCleanup(&Unit->SectorCache, GetSystemTime());

        MaxSectors = StorageTransferGetMaxSectors(Unit);
        if (MaxSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        Current = 0;
        while (Current < Control->NumSectors) {
            U32 ChunkSectors = MaxSectors;
            U64 ChunkLba;
            U32 FirstUncached;
            U8* Dest;

            if (ChunkSectors > Control->NumSectors - Current) {
                ChunkSectors = Control->NumSectors - Current;
            }

            ChunkLba = U64_Add(U64_Make(Control->SectorHigh, Control->SectorLow), U64_FromU32(Current));
            Dest = (U8*)Control->Buffer + Current * SECTOR_SIZE;

            // Phase 1: copy cache hits to the destination.
            FirstUncached = ChunkSectors;
            for (U32 Index = 0; Index < ChunkSectors; Index++) {
                SECTOR_BUFFER Match = { U64_Low32(U64_Add(ChunkLba, U64_FromU32(Index))), 0, 0, { 0 } };
                LPSECTORBUFFER Buffer =
                    (LPSECTORBUFFER)CacheFind(&Unit->SectorCache, StorageTransferCacheMatcher, &Match);

                if (Buffer == NULL) {
                    if (FirstUncached == ChunkSectors) {
                        FirstUncached = Index;
                    }
                    continue;
                }

                MemoryCopy(Dest + Index * SECTOR_SIZE, Buffer->Data, SECTOR_SIZE);
            }

            // Phase 2: serve the uncached run with a single raw driver read.
            if (FirstUncached < ChunkSectors) {
                U32 ReadSectors = ChunkSectors - FirstUncached;
                U8* ReadDest = Dest + FirstUncached * SECTOR_SIZE;
                U64 ReadLba = U64_Add(ChunkLba, U64_FromU32(FirstUncached));
                IOCONTROL Chunk;

                Chunk.TypeID = KOID_IOCONTROL;
                Chunk.Storage = Control->Storage;
                Chunk.SectorLow = U64_Low32(ReadLba);
                Chunk.SectorHigh = U64_High32(ReadLba);
                Chunk.NumSectors = ReadSectors;
                Chunk.Buffer = ReadDest;
                Chunk.BufferSize = ReadSectors * SECTOR_SIZE;

                Result = Control->Storage->Driver->Command(DF_STORAGE_READ, (UINT)&Chunk);
                if (Result != DF_RETURN_SUCCESS) {
                    return Result;
                }

                // Phase 3: populate the cache from the sectors read from storage.
                for (U32 Index = 0; Index < ReadSectors; Index++) {
                    SECTOR_BUFFER Match = { U64_Low32(U64_Add(ReadLba, U64_FromU32(Index))), 0, 0, { 0 } };
                    LPSECTORBUFFER Buffer;

                    if (CacheFind(&Unit->SectorCache, StorageTransferCacheMatcher, &Match) != NULL) {
                        continue;
                    }

                    Buffer = (LPSECTORBUFFER)BufferPoolAcquire(&Unit->SectorBufferPool);
                    if (Buffer == NULL) {
                        continue;
                    }

                    Buffer->SectorLow = Match.SectorLow;
                    Buffer->SectorHigh = Match.SectorHigh;
                    Buffer->Dirty = 0;
                    MemoryCopy(Buffer->Data, ReadDest + Index * SECTOR_SIZE, SECTOR_SIZE);

                    if (!CacheAdd(&Unit->SectorCache, Buffer, STORAGE_CACHE_TTL_MS)) {
                        BufferPoolRelease(&Unit->SectorBufferPool, Buffer);
                    }
                }
            }

            Current += ChunkSectors;
        }

        return DF_RETURN_SUCCESS;
    }
}

/***************************************************************************/

/**
 * @brief Write sectors through the generic transfer layer.
 *
 * The request is split into chunks bounded by the driver transfer limit and
 * each chunk is written with a single raw driver command. After a successful
 * write the sector cache is populated (or updated) so subsequent reads do not
 * touch the storage.
 *
 * @param Control IO control structure describing the request.
 * @return DF_RETURN_SUCCESS or a driver error code.
 */
U32 StorageTransferLayerWrite(LPIOCONTROL Control) {
    LPSTORAGE_TRANSFER_UNIT Unit;
    U32 MaxSectors;
    U32 Current;
    U32 Result;

    if (Control == NULL || Control->Storage == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Unit = Control->Storage->StorageTransfer;
    if (Unit == NULL) {
        return Control->Storage->Driver->Command(DF_STORAGE_WRITE, (UINT)Control);
    }

    PROFILE_SCOPED("StorageTransferLayerWrite") {
        CacheCleanup(&Unit->SectorCache, GetSystemTime());

        MaxSectors = StorageTransferGetMaxSectors(Unit);
        if (MaxSectors == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        Current = 0;
        while (Current < Control->NumSectors) {
            U32 ChunkSectors = MaxSectors;
            U64 ChunkLba;
            U8* Src;
            IOCONTROL Chunk;

            if (ChunkSectors > Control->NumSectors - Current) {
                ChunkSectors = Control->NumSectors - Current;
            }

            ChunkLba = U64_Add(U64_Make(Control->SectorHigh, Control->SectorLow), U64_FromU32(Current));
            Src = (U8*)Control->Buffer + Current * SECTOR_SIZE;

            // Issue one raw driver write for the whole chunk.
            Chunk.TypeID = KOID_IOCONTROL;
            Chunk.Storage = Control->Storage;
            Chunk.SectorLow = U64_Low32(ChunkLba);
            Chunk.SectorHigh = U64_High32(ChunkLba);
            Chunk.NumSectors = ChunkSectors;
            Chunk.Buffer = Src;
            Chunk.BufferSize = ChunkSectors * SECTOR_SIZE;

            Result = Control->Storage->Driver->Command(DF_STORAGE_WRITE, (UINT)&Chunk);
            if (Result != DF_RETURN_SUCCESS) {
                return Result;
            }

            // Populate or update the cache for each sector in the chunk.
            for (U32 Index = 0; Index < ChunkSectors; Index++) {
                SECTOR_BUFFER Match = { U64_Low32(U64_Add(ChunkLba, U64_FromU32(Index))), 0, 0, { 0 } };
                LPSECTORBUFFER Buffer =
                    (LPSECTORBUFFER)CacheFind(&Unit->SectorCache, StorageTransferCacheMatcher, &Match);

                if (Buffer == NULL) {
                    Buffer = (LPSECTORBUFFER)BufferPoolAcquire(&Unit->SectorBufferPool);
                    if (Buffer == NULL) {
                        continue;
                    }

                    Buffer->SectorLow = Match.SectorLow;
                    Buffer->SectorHigh = Match.SectorHigh;
                    Buffer->Dirty = 0;
                    MemoryCopy(Buffer->Data, Src + Index * SECTOR_SIZE, SECTOR_SIZE);

                    if (!CacheAdd(&Unit->SectorCache, Buffer, STORAGE_CACHE_TTL_MS)) {
                        BufferPoolRelease(&Unit->SectorBufferPool, Buffer);
                    }
                } else {
                    MemoryCopy(Buffer->Data, Src + Index * SECTOR_SIZE, SECTOR_SIZE);
                }
            }

            Current += ChunkSectors;
        }

        return DF_RETURN_SUCCESS;
    }
}

/***************************************************************************/
