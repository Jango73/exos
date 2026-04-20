
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


    Generic Temporary Cache with TTL

\************************************************************************/

#include "Base.h"
#include "utils/Cache.h"
#include "system/Clock.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "memory/Memory.h"

/************************************************************************/

static void CacheResetEntryLocked(LPCACHE_ENTRY Entry) {
    Entry->Data = NULL;
    Entry->ExpirationTime = 0;
    Entry->TTL = 0;
    Entry->Score = 0;
    Entry->Dirty = FALSE;
    Entry->Valid = FALSE;
}

/************************************************************************/

static BOOL CacheFlushEntryLocked(LPCACHE Cache, LPCACHE_ENTRY Entry) {
    if (Cache == NULL || Entry == NULL || !Entry->Valid) return FALSE;
    if (!Entry->Dirty) return TRUE;

    if (Cache->WritePolicy == CACHE_WRITE_POLICY_READ_ONLY) {
        return FALSE;
    }

    if (Cache->FlushCallback == NULL) {
        return FALSE;
    }

    if (!Cache->FlushCallback(Entry->Data, Cache->CallbackContext)) {
        return FALSE;
    }

    Entry->Dirty = FALSE;
    return TRUE;
}

/************************************************************************/

static void CacheReleaseDataLocked(LPCACHE Cache, LPVOID Data, BOOL Dirty) {
    if (Data == NULL) return;

    if (Cache != NULL && Cache->ReleaseCallback != NULL) {
        Cache->ReleaseCallback(Data, Dirty, Cache->CallbackContext);
        return;
    }

    KernelHeapFree(Data);
}

/************************************************************************/

static BOOL CacheReleaseEntryLocked(LPCACHE Cache, LPCACHE_ENTRY Entry, BOOL FlushIfDirty) {
    if (Cache == NULL || Entry == NULL) return FALSE;
    if (!Entry->Valid) return TRUE;

    if (FlushIfDirty && Entry->Dirty) {
        if (!CacheFlushEntryLocked(Cache, Entry)) {
            return FALSE;
        }
    }

    CacheReleaseDataLocked(Cache, Entry->Data, Entry->Dirty);
    CacheResetEntryLocked(Entry);

    if (Cache->Count > 0) {
        Cache->Count--;
    }

    return TRUE;
}

/************************************************************************/

static BOOL CacheEvictOneEntryLocked(LPCACHE Cache) {
    LPCACHE_ENTRY LowestCleanEntry = NULL;

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            continue;
        }

        if (Cache->Entries[Index].Dirty) {
            continue;
        }

        if (LowestCleanEntry == NULL || Cache->Entries[Index].Score < LowestCleanEntry->Score) {
            LowestCleanEntry = &Cache->Entries[Index];
        }
    }

    if (LowestCleanEntry != NULL) {
        return CacheReleaseEntryLocked(Cache, LowestCleanEntry, FALSE);
    }

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            continue;
        }

        if (!Cache->Entries[Index].Dirty) {
            continue;
        }

        if (CacheFlushEntryLocked(Cache, &Cache->Entries[Index])) {
            return CacheReleaseEntryLocked(Cache, &Cache->Entries[Index], FALSE);
        }
    }

    return FALSE;
}

/************************************************************************/

static void CacheDecayScoresLocked(LPCACHE Cache) {
    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid && Cache->Entries[Index].Score > 0) {
            Cache->Entries[Index].Score--;
        }
    }
}

/************************************************************************/

static LPCACHE_ENTRY CacheFindLowestScoreEntryInternal(LPCACHE Cache) {
    LPCACHE_ENTRY LowestEntry = NULL;

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            continue;
        }

        if (LowestEntry == NULL || Cache->Entries[Index].Score < LowestEntry->Score) {
            LowestEntry = &Cache->Entries[Index];
        }
    }

    return LowestEntry;
}

/************************************************************************/

/**
 * @brief Initialize a temporary cache.
 * @param Cache Cache structure to initialize
 * @param Capacity Maximum number of entries
 */
void CacheInit(LPCACHE Cache, UINT Capacity) {
    UINT AllocationSize = (UINT)(Capacity * sizeof(CACHE_ENTRY));

    InitMutex(&Cache->Mutex);
    Cache->Capacity = Capacity;
    Cache->Count = 0;
    Cache->WritePolicy = CACHE_WRITE_POLICY_READ_ONLY;
    Cache->FlushCallback = NULL;
    Cache->ReleaseCallback = NULL;
    Cache->CallbackContext = NULL;
    Cache->Entries = (LPCACHE_ENTRY)KernelHeapAlloc(AllocationSize);

    if (Cache->Entries == NULL) {
        ERROR(TEXT("KernelHeapAlloc failed"));
    }

    for (UINT Index = 0; Index < Capacity; Index++) {
        CacheResetEntryLocked(&Cache->Entries[Index]);
    }
}

/************************************************************************/

/**
 * @brief Deinitialize a temporary cache.
 * @param Cache Cache structure to deinitialize
 */
void CacheDeinit(LPCACHE Cache) {
    DEBUG(TEXT("Enter"));

    LockMutex(&Cache->Mutex, INFINITY);

    if (Cache->Entries) {
        for (UINT Index = 0; Index < Cache->Capacity; Index++) {
            if (!Cache->Entries[Index].Valid) {
                continue;
            }

            if (Cache->Entries[Index].Dirty) {
                if (!CacheFlushEntryLocked(Cache, &Cache->Entries[Index])) {
                    WARNING(TEXT("Flush failed for dirty entry %u"), Index);
                }
            }

            CacheReleaseDataLocked(Cache, Cache->Entries[Index].Data, Cache->Entries[Index].Dirty);
            CacheResetEntryLocked(&Cache->Entries[Index]);
        }
        KernelHeapFree(Cache->Entries);
        Cache->Entries = NULL;
    }

    Cache->Count = 0;
    Cache->WritePolicy = CACHE_WRITE_POLICY_READ_ONLY;
    Cache->FlushCallback = NULL;
    Cache->ReleaseCallback = NULL;
    Cache->CallbackContext = NULL;

    UnlockMutex(&Cache->Mutex);
}

/************************************************************************/

/**
 * @brief Configure write policy and callbacks for a cache.
 *
 * @param Cache Cache structure.
 * @param WritePolicy One of CACHE_WRITE_POLICY_* values.
 * @param FlushCallback Callback used to flush dirty entries.
 * @param ReleaseCallback Callback used to release entry payloads.
 * @param CallbackContext Context pointer passed to callbacks.
 */
void CacheSetWritePolicy(
    LPCACHE Cache, U32 WritePolicy, CACHE_FLUSH_CALLBACK FlushCallback, CACHE_RELEASE_CALLBACK ReleaseCallback, LPVOID CallbackContext) {
    if (Cache == NULL) return;

    LockMutex(&Cache->Mutex, INFINITY);

    if (WritePolicy > CACHE_WRITE_POLICY_WRITE_BACK) {
        WritePolicy = CACHE_WRITE_POLICY_READ_ONLY;
    }

    Cache->WritePolicy = WritePolicy;
    Cache->FlushCallback = FlushCallback;
    Cache->ReleaseCallback = ReleaseCallback;
    Cache->CallbackContext = CallbackContext;

    UnlockMutex(&Cache->Mutex);
}

/************************************************************************/

/**
 * @brief Add an entry to the cache with TTL.
 * @param Cache Cache structure
 * @param Data Pointer to data to store (will be managed by caller)
 * @param TTL_MS Time to live in milliseconds
 * @return TRUE if added successfully, FALSE otherwise
 */
BOOL CacheAdd(LPCACHE Cache, LPVOID Data, UINT TTL_MS) {
    UINT CurrentTime = GetSystemTime();
    UINT FreeIndex = MAX_UINT;

    if (Cache == NULL || Data == NULL) return FALSE;

    LockMutex(&Cache->Mutex, INFINITY);

    CacheDecayScoresLocked(Cache);

    // Find first free slot
    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid) {
            FreeIndex = Index;
            break;
        }

        if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
            if (CacheReleaseEntryLocked(Cache, &Cache->Entries[Index], TRUE)) {
                if (FreeIndex == MAX_UINT) {
                    FreeIndex = Index;
                }
            } else {
                Cache->Entries[Index].ExpirationTime =
                    (UINT)(CurrentTime + Cache->Entries[Index].TTL);
            }
        }
    }

    if (FreeIndex == MAX_UINT) {
        if (!CacheEvictOneEntryLocked(Cache)) {
            DEBUG(TEXT("Cache full and no entry available"));
            UnlockMutex(&Cache->Mutex);
            return FALSE;
        }

        for (UINT Index = 0; Index < Cache->Capacity; Index++) {
            if (!Cache->Entries[Index].Valid) {
                FreeIndex = Index;
                break;
            }
        }
    }

    if (FreeIndex == MAX_UINT) {
        UnlockMutex(&Cache->Mutex);
        return FALSE;
    }

    Cache->Entries[FreeIndex].Data = Data;
    Cache->Entries[FreeIndex].ExpirationTime = (UINT)(CurrentTime + TTL_MS);
    Cache->Entries[FreeIndex].TTL = TTL_MS;
    Cache->Entries[FreeIndex].Score = 1;
    Cache->Entries[FreeIndex].Dirty = FALSE;
    Cache->Entries[FreeIndex].Valid = TRUE;
    Cache->Count++;

    UnlockMutex(&Cache->Mutex);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Find an entry in the cache using a matcher function.
 * @param Cache Cache structure
 * @param Matcher Function to match entries
 * @param Context Context passed to matcher
 * @return Pointer to data if found, NULL otherwise
 */
LPVOID CacheFind(LPCACHE Cache, BOOL (*Matcher)(LPVOID Data, LPVOID Context), LPVOID Context) {
    if (Cache == NULL || Matcher == NULL) return NULL;

    LockMutex(&Cache->Mutex, INFINITY);

    UINT CurrentTime = GetSystemTime();

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            // Check if expired
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                if (CacheReleaseEntryLocked(Cache, &Cache->Entries[Index], TRUE)) {
                    continue;
                }

                Cache->Entries[Index].ExpirationTime =
                    (UINT)(CurrentTime + Cache->Entries[Index].TTL);
            }

            if (Matcher(Cache->Entries[Index].Data, Context)) {
                LPVOID Result = Cache->Entries[Index].Data;
                Cache->Entries[Index].Score++;
                Cache->Entries[Index].ExpirationTime =
                    (UINT)(CurrentTime + Cache->Entries[Index].TTL);
                UnlockMutex(&Cache->Mutex);
                return Result;
            }

            if (Cache->Entries[Index].Score > 0) {
                Cache->Entries[Index].Score--;
            }
        }
    }

    UnlockMutex(&Cache->Mutex);
    return NULL;
}

/************************************************************************/

/**
 * @brief Mark one cache entry as dirty.
 *
 * @param Cache Cache structure.
 * @param Data Entry payload pointer.
 * @return TRUE when entry is marked (and flushed for write-through), FALSE otherwise.
 */
BOOL CacheMarkEntryDirty(LPCACHE Cache, LPVOID Data) {
    if (Cache == NULL || Data == NULL) return FALSE;

    LockMutex(&Cache->Mutex, INFINITY);

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid || Cache->Entries[Index].Data != Data) {
            continue;
        }

        if (Cache->WritePolicy == CACHE_WRITE_POLICY_READ_ONLY) {
            UnlockMutex(&Cache->Mutex);
            return FALSE;
        }

        Cache->Entries[Index].Dirty = TRUE;
        if (Cache->WritePolicy == CACHE_WRITE_POLICY_WRITE_THROUGH) {
            BOOL Result = CacheFlushEntryLocked(Cache, &Cache->Entries[Index]);
            UnlockMutex(&Cache->Mutex);
            return Result;
        }

        UnlockMutex(&Cache->Mutex);
        return TRUE;
    }

    UnlockMutex(&Cache->Mutex);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Flush one cache entry payload to the backing store.
 *
 * @param Cache Cache structure.
 * @param Data Entry payload pointer.
 * @return TRUE when flush succeeds or entry is already clean, FALSE otherwise.
 */
BOOL CacheFlushEntry(LPCACHE Cache, LPVOID Data) {
    if (Cache == NULL || Data == NULL) return FALSE;

    LockMutex(&Cache->Mutex, INFINITY);

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid || Cache->Entries[Index].Data != Data) {
            continue;
        }

        BOOL Result = CacheFlushEntryLocked(Cache, &Cache->Entries[Index]);
        UnlockMutex(&Cache->Mutex);
        return Result;
    }

    UnlockMutex(&Cache->Mutex);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Flush all dirty cache entries.
 *
 * @param Cache Cache structure.
 * @return Number of flushed entries.
 */
UINT CacheFlushAllEntries(LPCACHE Cache) {
    UINT FlushedEntries = 0;

    if (Cache == NULL) return 0;

    LockMutex(&Cache->Mutex, INFINITY);

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (!Cache->Entries[Index].Valid || !Cache->Entries[Index].Dirty) {
            continue;
        }

        if (CacheFlushEntryLocked(Cache, &Cache->Entries[Index])) {
            FlushedEntries++;
        }
    }

    UnlockMutex(&Cache->Mutex);
    return FlushedEntries;
}

/************************************************************************/

/**
 * @brief Cleanup expired entries from cache.
 * @param Cache Cache structure
 * @param CurrentTime Current system time
 */
void CacheCleanup(LPCACHE Cache, UINT CurrentTime) {
    if (Cache == NULL) return;

    LockMutex(&Cache->Mutex, INFINITY);

    CacheDecayScoresLocked(Cache);

    for (UINT Index = 0; Index < Cache->Capacity; Index++) {
        if (Cache->Entries[Index].Valid) {
            if (CurrentTime >= Cache->Entries[Index].ExpirationTime) {
                if (!CacheReleaseEntryLocked(Cache, &Cache->Entries[Index], TRUE)) {
                    Cache->Entries[Index].ExpirationTime =
                        (UINT)(CurrentTime + Cache->Entries[Index].TTL);
                }
            }
        }
    }

    UnlockMutex(&Cache->Mutex);
}

/************************************************************************/

LPCACHE_ENTRY CacheFindLowestScoreEntry(LPCACHE Cache) {
    LPCACHE_ENTRY Result;

    if (Cache == NULL) return NULL;

    LockMutex(&Cache->Mutex, INFINITY);

    Result = CacheFindLowestScoreEntryInternal(Cache);

    UnlockMutex(&Cache->Mutex);

    return Result;
}
