
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


    Database

\************************************************************************/

#include "utils/Database.h"

#include "fs/File.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/

static U32 HashInt(I32 Key, U32 Size) {
    U32 X = (U32)Key;
    X ^= X >> 16;
    X *= 0x7feb352d;
    X ^= X >> 15;
    X *= 0x846ca68b;
    X ^= X >> 16;
    return X % Size;
}

/************************************************************************/

static I32 IndexPut(DATABASE *Database, I32 Key, U32 Idx) {
    U32 H = HashInt(Key, Database->IndexSize);
    for (U32 I = 0; I < Database->IndexSize; I++) {
        U32 Pos = (H + I) % Database->IndexSize;
        if (Database->Index[Pos].Key == -1 || Database->Index[Pos].Key == Key) {
            Database->Index[Pos].Key = Key;
            Database->Index[Pos].Index = Idx;
            return 0;
        }
    }
    return -1;
}

/************************************************************************/

static I32 IndexGet(DATABASE *Database, I32 Key, U32 *OutIdx) {
    U32 H = HashInt(Key, Database->IndexSize);
    for (U32 I = 0; I < Database->IndexSize; I++) {
        U32 Pos = (H + I) % Database->IndexSize;
        if (Database->Index[Pos].Key == -1) return -1;
        if (Database->Index[Pos].Key == Key) {
            *OutIdx = Database->Index[Pos].Index;
            return 0;
        }
    }
    return -1;
}
/************************************************************************/

static void IndexRemove(DATABASE *Database, I32 Key) {
    U32 H = HashInt(Key, Database->IndexSize);
    for (U32 I = 0; I < Database->IndexSize; I++) {
        U32 Pos = (H + I) % Database->IndexSize;
        if (Database->Index[Pos].Key == Key) {
            Database->Index[Pos].Key = -1;
            return;
        }
        if (Database->Index[Pos].Key == -1) return;
    }
}

/************************************************************************/

/**
 * @brief Creates a new database with specified record parameters.
 *
 * Allocates memory for a database structure, records array, and hash index.
 * The hash index is sized to twice the capacity to reduce collisions.
 *
 * @param RecordSize Size in bytes of each record
 * @param IdOffset Byte offset of the ID field within each record
 * @param Capacity Maximum number of records the database can hold
 * @return Pointer to newly created DATABASE structure, or NULL on failure
 */
DATABASE *DatabaseCreate(U32 RecordSize, U32 IdOffset, U32 Capacity) {
    DATABASE *Database = (DATABASE *)KernelHeapAlloc(sizeof(DATABASE));
    if (!Database) return NULL;
    MemorySet(Database, 0, sizeof(DATABASE));

    Database->RecordSize = RecordSize;
    Database->IdOffset = IdOffset;
    Database->Capacity = Capacity;
    Database->Count = 0;

    Database->Records = KernelHeapAlloc(Capacity * RecordSize);
    if (!Database->Records) {
        KernelHeapFree(Database);
        return NULL;
    }
    MemorySet(Database->Records, 0, Capacity * RecordSize);

    Database->IndexSize = Capacity * 2;
    Database->Index = (DATABASE_INDEX_ENTRY *)KernelHeapAlloc(Database->IndexSize * sizeof(DATABASE_INDEX_ENTRY));
    if (!Database->Index) {
        KernelHeapFree(Database->Records);
        KernelHeapFree(Database);
        return NULL;
    }

    for (U32 I = 0; I < Database->IndexSize; I++) {
        Database->Index[I].Key = -1;
        Database->Index[I].Index = 0;
    }

    return Database;
}

/************************************************************************/

/**
 * @brief Frees all memory associated with a database.
 *
 * Deallocates the records array, hash index, and database structure itself.
 * Safe to call with NULL pointer.
 *
 * @param Database Pointer to database structure to free
 */
void DatabaseFree(DATABASE *Database) {
    if (!Database) return;
    KernelHeapFree(Database->Records);
    KernelHeapFree(Database->Index);
    KernelHeapFree(Database);
}

/************************************************************************/

/**
 * @brief Adds a new record to the database.
 *
 * Copies the record to the database's records array and adds an entry to the
 * hash index. The ID is extracted from the record at the configured offset.
 *
 * @param Database Pointer to database structure
 * @param Record Pointer to record data to add
 * @return 0 on success, -1 on failure (database full or index collision)
 */
I32 DatabaseAdd(DATABASE *Database, LPCVOID Record) {
    if (Database->Count >= Database->Capacity) return -1;

    LPVOID Dst = (U8 *)Database->Records + Database->Count * Database->RecordSize;
    MemoryCopy(Dst, Record, Database->RecordSize);

    I32 Id = *(I32 *)((U8 *)Dst + Database->IdOffset);
    if (IndexPut(Database, Id, Database->Count) != 0) return -1;

    Database->Count++;
    return 0;
}

/************************************************************************/

/**
 * @brief Finds a record in the database by ID.
 *
 * Uses the hash index to quickly locate a record with the specified ID.
 *
 * @param Database Pointer to database structure
 * @param Id ID value to search for
 * @return Pointer to record data if found, NULL if not found
 */
LPVOID DatabaseFind(DATABASE *Database, I32 Id) {
    U32 Idx;
    if (IndexGet(Database, Id, &Idx) != 0) return NULL;
    return (U8 *)Database->Records + Idx * Database->RecordSize;
}

/************************************************************************/

/**
 * @brief Deletes a record from the database by ID.
 *
 * Removes the record from the database and compacts the array by moving
 * the last record to fill the gap. Updates the hash index accordingly.
 *
 * @param Database Pointer to database structure
 * @param Id ID value of record to delete
 * @return 0 on success, -1 if record not found
 */
I32 DatabaseDelete(DATABASE *Database, I32 Id) {
    U32 Idx;
    if (IndexGet(Database, Id, &Idx) != 0) return -1;

    U32 LastIdx = Database->Count - 1;
    LPVOID Dst = (U8 *)Database->Records + Idx * Database->RecordSize;
    LPVOID Last = (U8 *)Database->Records + LastIdx * Database->RecordSize;

    if (Idx != LastIdx) {
        MemoryCopy(Dst, Last, Database->RecordSize);

        I32 LastId = *(I32 *)((U8 *)Last + Database->IdOffset);
        for (U32 I = 0; I < Database->IndexSize; I++) {
            if (Database->Index[I].Key == LastId) {
                Database->Index[I].Index = Idx;
                break;
            }
        }
    }

    IndexRemove(Database, Id);
    Database->Count--;
    return 0;
}

/************************************************************************/

/**
 * @brief Saves database contents to a file.
 *
 * Writes database header information and all records to a binary file.
 * The hash index is not saved as it is rebuilt on load.
 *
 * @param Database Pointer to database structure
 * @param Filename Path to output file
 * @return 0 on success, -1 on failure (memory allocation or file write error)
 */
I32 DatabaseSave(DATABASE *Database, LPCSTR Filename) {
    UINT HeaderSize = sizeof(DATABASE_FILE_HEADER);
    UINT DataSize = Database->RecordSize * Database->Count;
    UINT TotalSize = HeaderSize + DataSize;

    LPVOID Buffer = KernelHeapAlloc(TotalSize);
    if (!Buffer) return -1;

    DATABASE_FILE_HEADER *Hdr = (DATABASE_FILE_HEADER *)Buffer;
    Hdr->Magic = DB_FILE_MAGIC;
    Hdr->Version = DB_FILE_VERSION;
    Hdr->RecordSize = Database->RecordSize;
    Hdr->Count = Database->Count;
    Hdr->Capacity = Database->Capacity;

    MemoryCopy((U8 *)Buffer + HeaderSize, Database->Records, DataSize);

    UINT BytesWritten = FileWriteAll(Filename, Buffer, TotalSize);

    if (BytesWritten == 0) {
        DEBUG(TEXT("Failed to save database to %s"), Filename);
    }

    KernelHeapFree(Buffer);

    return (BytesWritten == TotalSize) ? 0 : -1;
}

/************************************************************************/

/**
 * @brief Loads database contents from a file.
 *
 * Reads a binary file created by DatabaseSave and populates the database
 * with the stored records. Rebuilds the hash index for all loaded records.
 *
 * @param Database Pointer to database structure (must be pre-allocated)
 * @param Filename Path to input file
 * @return 0 on success, -1 on failure (file not found, invalid format, or incompatible parameters)
 */
I32 DatabaseLoad(DATABASE *Database, LPCSTR Filename) {
    UINT FileSize;
    LPVOID Buffer = FileReadAll(Filename, &FileSize);
    if (!Buffer) {
        DEBUG(TEXT("Failed to read %s"), Filename);
        return -1;
    }

    if (FileSize < sizeof(DATABASE_FILE_HEADER)) {
        KernelHeapFree(Buffer);
        return -1;
    }

    DATABASE_FILE_HEADER *Hdr = (DATABASE_FILE_HEADER *)Buffer;
    if (Hdr->Magic != DB_FILE_MAGIC) {
        KernelHeapFree(Buffer);
        return -1;
    }
    if (Hdr->Version != DB_FILE_VERSION) {
        KernelHeapFree(Buffer);
        return -1;
    }
    if (Hdr->RecordSize != Database->RecordSize) {
        KernelHeapFree(Buffer);
        return -1;
    }
    if (Hdr->Count > Database->Capacity) {
        KernelHeapFree(Buffer);
        return -1;
    }

    UINT ExpectedSize = sizeof(DATABASE_FILE_HEADER) + (Hdr->RecordSize * Hdr->Count);
    if (FileSize != ExpectedSize) {
        KernelHeapFree(Buffer);
        return -1;
    }

    MemoryCopy(Database->Records, (U8 *)Buffer + sizeof(DATABASE_FILE_HEADER), Hdr->RecordSize * Hdr->Count);
    Database->Count = Hdr->Count;

    KernelHeapFree(Buffer);

    /* rebuild index */
    for (U32 I = 0; I < Database->IndexSize; I++) {
        Database->Index[I].Key = -1;
        Database->Index[I].Index = 0;
    }

    for (U32 I = 0; I < Database->Count; I++) {
        LPVOID Rec = (U8 *)Database->Records + I * Database->RecordSize;
        I32 Id = *(I32 *)((U8 *)Rec + Database->IdOffset);
        IndexPut(Database, Id, I);
    }

    return 0;
}
