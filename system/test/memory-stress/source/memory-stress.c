/************************************************************************\

    EXOS Sample program
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


    Memory stress - Userland heap stress and validation

\************************************************************************/

#include "../../../../runtime/include/exos/exos-runtime-main.h"
#include "../../../../runtime/include/exos/exos.h"

/************************************************************************/

#define SLOT_COUNT 64
#define LARGE_BLOCK_COUNT 12
#define PATTERN_SEED 0x5A

/************************************************************************/

typedef struct tag_BLOCK_SLOT {
    void* Pointer;
    U32 RequestedSize;
    U32 Pattern;
} BLOCK_SLOT, *LPBLOCK_SLOT;

/************************************************************************/

/**
 * @brief Fill a block with a deterministic byte pattern.
 * @param Pointer Block base address.
 * @param Size Number of bytes to fill.
 * @param Pattern Pattern selector.
 */
static void FillPattern(void* Pointer, U32 Size, U32 Pattern) {
    U8* Bytes = (U8*)Pointer;
    U32 Index = 0;

    for (Index = 0; Index < Size; Index++) {
        Bytes[Index] = (U8)((Pattern + (Index * 13)) & 0xFF);
    }
}

/************************************************************************/

/**
 * @brief Verify a previously filled block.
 * @param Pointer Block base address.
 * @param Size Number of bytes to verify.
 * @param Pattern Pattern selector.
 * @return TRUE when every byte matches the expected pattern.
 */
static BOOL ValidatePattern(const void* Pointer, U32 Size, U32 Pattern) {
    const U8* Bytes = (const U8*)Pointer;
    U32 Index = 0;

    for (Index = 0; Index < Size; Index++) {
        if (Bytes[Index] != (U8)((Pattern + (Index * 13)) & 0xFF)) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Query the current process heap snapshot.
 * @param Info Output snapshot.
 * @return TRUE on success.
 */
static BOOL QueryProcessMemoryInfo(LPPROCESS_MEMORY_INFO Info) {
    memset(Info, 0, sizeof(*Info));
    Info->Header.Size = sizeof(*Info);
    Info->Header.Version = EXOS_ABI_VERSION;
    Info->Header.Flags = 0;
    Info->Process = 0;
    return GetProcessMemoryInfo(Info);
}

/************************************************************************/

/**
 * @brief Track the highest reserved heap size seen so far.
 * @param PeakReservedSize Pointer to tracked peak.
 */
static BOOL UpdatePeakReservedSize(U32* PeakReservedSize) {
    PROCESS_MEMORY_INFO Info;

    if (!QueryProcessMemoryInfo(&Info)) {
        return FALSE;
    }

    if (Info.HeapReservedSize > *PeakReservedSize) {
        *PeakReservedSize = Info.HeapReservedSize;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Release every live slot and clear metadata.
 * @param Slots Slot array.
 * @param SlotCount Number of slots.
 */
static void FreeAllSlots(LPBLOCK_SLOT Slots, U32 SlotCount) {
    U32 Index = 0;

    for (Index = 0; Index < SlotCount; Index++) {
        if (Slots[Index].Pointer != NULL) {
            free(Slots[Index].Pointer);
            Slots[Index].Pointer = NULL;
            Slots[Index].RequestedSize = 0;
            Slots[Index].Pattern = 0;
        }
    }
}

/************************************************************************/

/**
 * @brief Validate every live slot in place.
 * @param Slots Slot array.
 * @param SlotCount Number of slots.
 * @return TRUE when all live allocations keep their contents.
 */
static BOOL ValidateAllSlots(LPBLOCK_SLOT Slots, U32 SlotCount) {
    U32 Index = 0;

    for (Index = 0; Index < SlotCount; Index++) {
        if (Slots[Index].Pointer != NULL &&
            !ValidatePattern(Slots[Index].Pointer, Slots[Index].RequestedSize, Slots[Index].Pattern)) {
            debug("memory stress: corruption at slot %u", Index);
            printf("memory stress: corruption at slot %u\n", Index);
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocate deterministic small and medium blocks.
 * @param Slots Slot array.
 * @param SlotCount Number of slots.
 * @return TRUE on success.
 */
static BOOL RunFragmentationPhase(LPBLOCK_SLOT Slots, U32 SlotCount) {
    U32 Index = 0;
    U32 Size = 0;
    void* Pointer = NULL;

    for (Index = 0; Index < SlotCount; Index++) {
        Size = 24 + ((Index * 37) % 3072);
        Pointer = malloc(Size);
        if (Pointer == NULL) {
            debug("memory stress: malloc failed during initial phase at slot %u size %u", Index, Size);
            printf("memory stress: malloc failed during initial phase at slot %u size %u\n", Index, Size);
            return FALSE;
        }

        Slots[Index].Pointer = Pointer;
        Slots[Index].RequestedSize = Size;
        Slots[Index].Pattern = PATTERN_SEED + Index;
        FillPattern(Pointer, Size, Slots[Index].Pattern);
    }

    for (Index = 0; Index < SlotCount; Index += 3) {
        free(Slots[Index].Pointer);
        Slots[Index].Pointer = NULL;
        Slots[Index].RequestedSize = 0;
        Slots[Index].Pattern = 0;
    }

    if (!ValidateAllSlots(Slots, SlotCount)) {
        return FALSE;
    }

    for (Index = 1; Index < SlotCount; Index += 2) {
        if (Slots[Index].Pointer == NULL) {
            continue;
        }

        U32 NewSize = Slots[Index].RequestedSize + 2048 + (Index * 29);
        Pointer = realloc(Slots[Index].Pointer, NewSize);
        if (Pointer == NULL) {
            debug("memory stress: realloc grow failed at slot %u size %u", Index, NewSize);
            printf("memory stress: realloc grow failed at slot %u size %u\n", Index, NewSize);
            return FALSE;
        }

        Slots[Index].Pointer = Pointer;
        FillPattern(Slots[Index].Pointer, Slots[Index].RequestedSize, Slots[Index].Pattern);
        FillPattern(
            ((U8*)Slots[Index].Pointer) + Slots[Index].RequestedSize, NewSize - Slots[Index].RequestedSize,
            Slots[Index].Pattern + 1);
        Slots[Index].RequestedSize = NewSize;
        Slots[Index].Pattern += 1;
        FillPattern(Slots[Index].Pointer, Slots[Index].RequestedSize, Slots[Index].Pattern);
    }

    if (!ValidateAllSlots(Slots, SlotCount)) {
        return FALSE;
    }

    for (Index = 0; Index < SlotCount; Index += 3) {
        if (Slots[Index].Pointer != NULL) {
            continue;
        }

        Size = 512 + ((Index * 97) % 8192);
        Pointer = malloc(Size);
        if (Pointer == NULL) {
            debug("memory stress: hole refill failed at slot %u size %u", Index, Size);
            printf("memory stress: hole refill failed at slot %u size %u\n", Index, Size);
            return FALSE;
        }

        Slots[Index].Pointer = Pointer;
        Slots[Index].RequestedSize = Size;
        Slots[Index].Pattern = PATTERN_SEED + 0x40 + Index;
        FillPattern(Pointer, Size, Slots[Index].Pattern);
    }

    for (Index = 2; Index < SlotCount; Index += 4) {
        U32 NewSize = (Slots[Index].RequestedSize / 2) + 17;
        Pointer = realloc(Slots[Index].Pointer, NewSize);
        if (Pointer == NULL) {
            debug("memory stress: realloc shrink failed at slot %u size %u", Index, NewSize);
            printf("memory stress: realloc shrink failed at slot %u size %u\n", Index, NewSize);
            return FALSE;
        }

        Slots[Index].Pointer = Pointer;
        Slots[Index].RequestedSize = NewSize;
        FillPattern(Pointer, NewSize, Slots[Index].Pattern);
    }

    return ValidateAllSlots(Slots, SlotCount);
}

/************************************************************************/

/**
 * @brief Force multiple heap growth events with large temporary blocks.
 * @param PeakReservedSize Pointer to tracked reserved-size peak.
 * @return TRUE on success.
 */
static BOOL RunGrowthPhase(U32* PeakReservedSize) {
    void* Blocks[LARGE_BLOCK_COUNT];
    U32 Sizes[LARGE_BLOCK_COUNT];
    U32 Index = 0;

    memset(Blocks, 0, sizeof(Blocks));
    memset(Sizes, 0, sizeof(Sizes));

    for (Index = 0; Index < LARGE_BLOCK_COUNT; Index++) {
        Sizes[Index] = 32768 + (Index * 16384);
        Blocks[Index] = malloc(Sizes[Index]);
        if (Blocks[Index] == NULL) {
            debug("memory stress: large malloc failed at block %u size %u", Index, Sizes[Index]);
            printf("memory stress: large malloc failed at block %u size %u\n", Index, Sizes[Index]);
            return FALSE;
        }

        FillPattern(Blocks[Index], Sizes[Index], PATTERN_SEED + 0x80 + Index);

        if (!UpdatePeakReservedSize(PeakReservedSize)) {
            debug("memory stress: snapshot failed during large allocation phase");
            printf("memory stress: snapshot failed during large allocation phase\n");
            return FALSE;
        }
    }

    for (Index = LARGE_BLOCK_COUNT; Index > 0; Index--) {
        U32 BlockIndex = Index - 1;

        if (!ValidatePattern(Blocks[BlockIndex], Sizes[BlockIndex], PATTERN_SEED + 0x80 + BlockIndex)) {
            debug("memory stress: large block corruption at block %u", BlockIndex);
            printf("memory stress: large block corruption at block %u\n", BlockIndex);
            return FALSE;
        }

        free(Blocks[BlockIndex]);
        Blocks[BlockIndex] = NULL;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Returns the executable base name from argv[0].
 * @param Path Executable path.
 * @return Pointer inside Path to the last component.
 */
static const char* GetExecutableName(const char* Path) {
    const char* Name = Path;
    const char* Cursor = Path;

    if (Path == NULL) {
        return "";
    }

    while (*Cursor != '\0') {
        if (*Cursor == '/' || *Cursor == '\\') {
            Name = Cursor + 1;
        }
        Cursor++;
    }

    return Name;
}

/************************************************************************/

/**
 * @brief Entry point for the memory stress executable.
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return Zero on success, non-zero on failure.
 */
int main(int argc, char** argv) {
    PROCESS_MEMORY_INFO InitialInfo;
    PROCESS_MEMORY_INFO FinalInfo;
    PROCESS_MEMORY_INFO ReuseInfo;
    BLOCK_SLOT Slots[SLOT_COUNT];
    void* ReusePointer = NULL;
    U32 PeakReservedSize = 0;
    BOOL RunFragmentation = TRUE;
    BOOL RunGrowth = TRUE;
    int ArgIndex = 0;

    memset(Slots, 0, sizeof(Slots));

    if (argc > 0) {
        const char* ExecutableName = GetExecutableName(argv[0]);

        if (strcmp(ExecutableName, "memory-fragmentation") == 0) {
            RunGrowth = FALSE;
        } else if (strcmp(ExecutableName, "memory-growth") == 0) {
            RunFragmentation = FALSE;
        }
    }

    for (ArgIndex = 1; ArgIndex < argc; ArgIndex++) {
        if (strcmp(argv[ArgIndex], "--fragmentation-only") == 0) {
            RunGrowth = FALSE;
        } else if (strcmp(argv[ArgIndex], "--growth-only") == 0) {
            RunFragmentation = FALSE;
        }
    }

    if (!QueryProcessMemoryInfo(&InitialInfo)) {
        debug("memory stress: failed to query initial heap state");
        printf("memory stress: failed to query initial heap state\n");
        return 10;
    }

    debug(
        "memory stress: initial reserved=%u first_unallocated=%u used=%u free=%u", InitialInfo.HeapReservedSize,
        InitialInfo.HeapFirstUnallocatedOffset, InitialInfo.HeapUsedBytes, InitialInfo.HeapFreeBytes);

    PeakReservedSize = InitialInfo.HeapReservedSize;

    if (RunFragmentation) {
        if (!RunFragmentationPhase(Slots, SLOT_COUNT)) {
            FreeAllSlots(Slots, SLOT_COUNT);
            return 11;
        }

        if (!UpdatePeakReservedSize(&PeakReservedSize)) {
            FreeAllSlots(Slots, SLOT_COUNT);
            debug("memory stress: failed to query heap after fragmentation phase");
            printf("memory stress: failed to query heap after fragmentation phase\n");
            return 12;
        }
    }

    if (RunGrowth) {
        if (!RunGrowthPhase(&PeakReservedSize)) {
            FreeAllSlots(Slots, SLOT_COUNT);
            return 13;
        }
    }

    if (!ValidateAllSlots(Slots, SLOT_COUNT)) {
        FreeAllSlots(Slots, SLOT_COUNT);
        return 14;
    }

    FreeAllSlots(Slots, SLOT_COUNT);

    if (!QueryProcessMemoryInfo(&FinalInfo)) {
        debug("memory stress: failed to query final heap state");
        printf("memory stress: failed to query final heap state\n");
        return 15;
    }

    if (PeakReservedSize <= InitialInfo.HeapReservedSize) {
        debug("memory stress: heap did not grow (initial=%u peak=%u)", InitialInfo.HeapReservedSize, PeakReservedSize);
        printf(
            "memory stress: heap did not grow (initial=%u peak=%u)\n", InitialInfo.HeapReservedSize, PeakReservedSize);
        return 16;
    }

    if (FinalInfo.HeapFirstUnallocatedOffset != InitialInfo.HeapFirstUnallocatedOffset) {
        debug(
            "memory stress: heap tail mismatch (initial=%u final=%u used_initial=%u used_final=%u)",
            InitialInfo.HeapFirstUnallocatedOffset, FinalInfo.HeapFirstUnallocatedOffset, InitialInfo.HeapUsedBytes,
            FinalInfo.HeapUsedBytes);
        printf(
            "memory stress: heap tail mismatch (initial=%u final=%u)\n", InitialInfo.HeapFirstUnallocatedOffset,
            FinalInfo.HeapFirstUnallocatedOffset);
        return 17;
    }

    ReusePointer = malloc(4096);
    if (ReusePointer == NULL) {
        debug("memory stress: post-cleanup allocation failed");
        printf("memory stress: post-cleanup allocation failed\n");
        return 18;
    }

    FillPattern(ReusePointer, 4096, PATTERN_SEED + 0xF0);
    if (!ValidatePattern(ReusePointer, 4096, PATTERN_SEED + 0xF0)) {
        free(ReusePointer);
        debug("memory stress: post-cleanup allocation corrupted");
        printf("memory stress: post-cleanup allocation corrupted\n");
        return 19;
    }

    free(ReusePointer);

    if (!QueryProcessMemoryInfo(&ReuseInfo)) {
        debug("memory stress: failed to query reuse heap state");
        printf("memory stress: failed to query reuse heap state\n");
        return 20;
    }

    if (ReuseInfo.HeapFirstUnallocatedOffset != InitialInfo.HeapFirstUnallocatedOffset) {
        debug(
            "memory stress: heap reuse state mismatch (initial=%u reuse=%u used_initial=%u used_reuse=%u)",
            InitialInfo.HeapFirstUnallocatedOffset, ReuseInfo.HeapFirstUnallocatedOffset, InitialInfo.HeapUsedBytes,
            ReuseInfo.HeapUsedBytes);
        printf("memory stress: heap reuse state mismatch\n");
        return 21;
    }

    debug(
        "memory stress: OK initial_reserved=%u peak_reserved=%u final_reserved=%u initial_used=%u final_used=%u",
        InitialInfo.HeapReservedSize, PeakReservedSize, FinalInfo.HeapReservedSize, InitialInfo.HeapUsedBytes,
        FinalInfo.HeapUsedBytes);
    printf(
        "memory stress: OK initial_reserved=%u peak_reserved=%u final_reserved=%u initial_used=%u final_used=%u\n",
        InitialInfo.HeapReservedSize, PeakReservedSize, FinalInfo.HeapReservedSize, InitialInfo.HeapUsedBytes,
        FinalInfo.HeapUsedBytes);

    return 0;
}

/************************************************************************/
