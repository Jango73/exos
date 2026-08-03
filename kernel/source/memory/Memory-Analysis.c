
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


    Memory carving analysis

\************************************************************************/

#include "memory/Memory-Analysis.h"

#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Record one issue into the report.
 * @param Report Target report.
 * @param Severity Issue severity.
 * @param Format Issue text format.
 */
static void MemoryAnalysisAddIssue(
    LPMEMORY_CARVING_REPORT Report, MEMORY_CARVING_SEVERITY Severity, LPCSTR Format, ...) {
    LPMEMORY_CARVING_ISSUE Issue;
    VarArgList Args;

    if (Report == NULL || Format == NULL || Report->IssueCount >= MEMORY_CARVING_MAX_ISSUES) {
        return;
    }

    Issue = &(Report->Issues[Report->IssueCount]);
    Issue->Severity = Severity;

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Issue->Text, Format, Args);
    VarArgEnd(Args);

    Report->IssueCount++;
}

/************************************************************************/

/**
 * @brief Check whether two closed ranges overlap.
 * @param FirstBase First range base.
 * @param FirstLimit First range end (exclusive).
 * @param SecondBase Second range base.
 * @param SecondLimit Second range end (exclusive).
 * @return TRUE when the ranges overlap.
 */
static BOOL MemoryAnalysisRangesOverlap(LINEAR FirstBase, LINEAR FirstLimit, LINEAR SecondBase, LINEAR SecondLimit) {
    if (FirstBase >= FirstLimit || SecondBase >= SecondLimit) {
        return FALSE;
    }

    return FirstBase < SecondLimit && SecondBase < FirstLimit;
}

/************************************************************************/

/**
 * @brief Find the arena containing a range.
 * @param Snapshot Carving snapshot.
 * @param Base Range base.
 * @param Limit Range end (exclusive).
 * @return Arena identifier or PROCESS_ARENA_COUNT when not contained.
 */
static UINT MemoryAnalysisFindContainingArena(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LINEAR Base, LINEAR Limit) {
    for (UINT ArenaIndex = 0; ArenaIndex < PROCESS_ARENA_COUNT; ArenaIndex++) {
        LPPROCESS_ARENA_RANGE Range = &(Snapshot->Arenas[ArenaIndex]);

        if (Range->Limit == 0) {
            continue;
        }

        if (Base >= Range->Base && Limit <= Range->Limit) {
            return ArenaIndex;
        }
    }

    return PROCESS_ARENA_COUNT;
}

/************************************************************************/

/**
 * @brief Validate the internal consistency of every arena range.
 * @param Snapshot Carving snapshot.
 * @param Report Target report.
 */
static void MemoryAnalysisCheckArenas(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report) {
    for (UINT ArenaIndex = 0; ArenaIndex < PROCESS_ARENA_COUNT; ArenaIndex++) {
        LPPROCESS_ARENA_RANGE Range = &(Snapshot->Arenas[ArenaIndex]);
        LPCSTR ArenaName = MemoryAnalysisGetArenaName(ArenaIndex);

        if (Range->Limit != 0 && Range->Base > Range->Limit) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Arena %s has invalid range [%p, %p)"), ArenaName,
                Range->Base, Range->Limit);
        }

        if (Range->Limit != 0) {
            if (Range->NextLow < Range->Base || Range->NextLow > Range->Limit) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_WARNING, TEXT("Arena %s low cursor %p outside [%p, %p)"), ArenaName,
                    Range->NextLow, Range->Base, Range->Limit);
            }

            if (Range->NextHigh < Range->Base || Range->NextHigh > Range->Limit) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_WARNING, TEXT("Arena %s high cursor %p outside [%p, %p)"),
                    ArenaName, Range->NextHigh, Range->Base, Range->Limit);
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Validate that arenas form a contiguous carved partition.
 *
 * Only valid when the process address space uses a carved user layout,
 * where every arena limit equals the base of the following arena.
 *
 * @param Snapshot Carving snapshot.
 * @param Report Target report.
 */
static void MemoryAnalysisCheckArenaPartition(
    LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report) {
    UINT OrderedArenas[PROCESS_ARENA_COUNT];
    UINT Count = 0;

    for (UINT ArenaIndex = 0; ArenaIndex < PROCESS_ARENA_COUNT; ArenaIndex++) {
        if (Snapshot->Arenas[ArenaIndex].Limit != 0) {
            OrderedArenas[Count] = ArenaIndex;
            Count++;
        }
    }

    for (UINT Index = 1; Index < Count; Index++) {
        UINT Value = OrderedArenas[Index];
        UINT Insert = Index;

        while (Insert > 0 && Snapshot->Arenas[OrderedArenas[Insert - 1]].Base > Snapshot->Arenas[Value].Base) {
            OrderedArenas[Insert] = OrderedArenas[Insert - 1];
            Insert--;
        }

        OrderedArenas[Insert] = Value;
    }

    for (UINT Index = 0; Index + 1 < Count; Index++) {
        LPPROCESS_ARENA_RANGE First = &(Snapshot->Arenas[OrderedArenas[Index]]);
        LPPROCESS_ARENA_RANGE Second = &(Snapshot->Arenas[OrderedArenas[Index + 1]]);
        LPCSTR FirstName = MemoryAnalysisGetArenaName(OrderedArenas[Index]);
        LPCSTR SecondName = MemoryAnalysisGetArenaName(OrderedArenas[Index + 1]);

        if (First->Limit > Second->Base) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Arenas %s and %s overlap"), FirstName, SecondName);
            Report->OverlapCount++;
        } else if (First->Limit < Second->Base) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Arenas %s and %s are not contiguous"), FirstName,
                SecondName);
        }
    }
}

/************************************************************************/

/**
 * @brief Validate region descriptors for overlaps and arena containment.
 * @param Snapshot Carving snapshot.
 * @param Report Target report.
 */
static void MemoryAnalysisCheckRegions(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report) {
    for (UINT RegionIndex = 0; RegionIndex < Snapshot->RegionCount; RegionIndex++) {
        LPMEMORY_CARVING_REGION Region = &(Snapshot->Regions[RegionIndex]);

        if (Region->Size == 0 || Region->Limit <= Region->Base) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Region %s has invalid range [%p, %p)"), Region->Tag,
                Region->Base, Region->Limit);
        }

        for (UINT OtherIndex = RegionIndex + 1; OtherIndex < Snapshot->RegionCount; OtherIndex++) {
            LPMEMORY_CARVING_REGION Other = &(Snapshot->Regions[OtherIndex]);

            if (MemoryAnalysisRangesOverlap(Region->Base, Region->Limit, Other->Base, Other->Limit)) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Regions %s and %s overlap"), Region->Tag, Other->Tag);
                Report->OverlapCount++;
            }
        }

        if (Snapshot->ArenasCarved &&
            MemoryAnalysisFindContainingArena(Snapshot, Region->Base, Region->Limit) == PROCESS_ARENA_COUNT) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Region %s [%p, %p) outside all arenas"), Region->Tag,
                Region->Base, Region->Limit);
        }
    }
}

/************************************************************************/

/**
 * @brief Validate task stacks for bounds, overlaps and region conflicts.
 * @param Snapshot Carving snapshot.
 * @param Report Target report.
 */
static void MemoryAnalysisCheckStacks(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report) {
    for (UINT StackIndex = 0; StackIndex < Snapshot->StackCount; StackIndex++) {
        LPMEMORY_CARVING_STACK Stack = &(Snapshot->Stacks[StackIndex]);
        LPCSTR StackName = MemoryAnalysisGetStackKindName(Stack->Kind);

        if (Stack->Size == 0 || Stack->Base == 0 || Stack->Limit <= Stack->Base) {
            MemoryAnalysisAddIssue(
                Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Stack %s invalid range [%p, %p)"), StackName, Stack->Base,
                Stack->Limit);
        }

        for (UINT OtherIndex = StackIndex + 1; OtherIndex < Snapshot->StackCount; OtherIndex++) {
            LPMEMORY_CARVING_STACK Other = &(Snapshot->Stacks[OtherIndex]);

            if (MemoryAnalysisRangesOverlap(Stack->Base, Stack->Limit, Other->Base, Other->Limit)) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Stacks %s and %s overlap"), StackName,
                    MemoryAnalysisGetStackKindName(Other->Kind));
                Report->OverlapCount++;
            }
        }

        for (UINT RegionIndex = 0; RegionIndex < Snapshot->RegionCount; RegionIndex++) {
            LPMEMORY_CARVING_REGION Region = &(Snapshot->Regions[RegionIndex]);

            if (Region->Base <= Stack->Base && Region->Limit >= Stack->Limit) {
                continue;
            }

            if (MemoryAnalysisRangesOverlap(Stack->Base, Stack->Limit, Region->Base, Region->Limit)) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Stack %s overlaps region %s"), StackName, Region->Tag);
                Report->OverlapCount++;
            }
        }

        if (Snapshot->ArenasCarved) {
            LPPROCESS_ARENA_RANGE StackArena = &(Snapshot->Arenas[PROCESS_ARENA_STACK]);

            if (StackArena->Limit != 0 && (Stack->Base < StackArena->Base || Stack->Limit > StackArena->Limit)) {
                MemoryAnalysisAddIssue(
                    Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Stack %s [%p, %p) outside stack arena"), StackName,
                    Stack->Base, Stack->Limit);
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Reset a carving report to its empty state.
 * @param Report Target report.
 */
void MemoryAnalysisResetReport(LPMEMORY_CARVING_REPORT Report) {
    if (Report == NULL) {
        return;
    }

    MemorySet(Report, 0, sizeof(*Report));
}

/************************************************************************/

/**
 * @brief Analyze a carving snapshot and fill the report with issues.
 * @param Snapshot Carving snapshot taken under the owning locks.
 * @param Report Target report.
 */
void MemoryAnalysisAnalyzeCarving(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report) {
    if (Snapshot == NULL || Report == NULL) {
        return;
    }

    MemoryAnalysisResetReport(Report);

    if (Snapshot->AddressSpaceInitialized == FALSE) {
        MemoryAnalysisAddIssue(Report, MEMORY_CARVING_SEVERITY_ERROR, TEXT("Address space is not initialized"));
        return;
    }

    MemoryAnalysisCheckArenas(Snapshot, Report);

    if (Snapshot->ArenasCarved) {
        MemoryAnalysisCheckArenaPartition(Snapshot, Report);
    }

    MemoryAnalysisCheckRegions(Snapshot, Report);
    MemoryAnalysisCheckStacks(Snapshot, Report);
}

/************************************************************************/

/**
 * @brief Count the issues of a given severity in a report.
 * @param Report Target report.
 * @param Severity Severity to count.
 * @return Number of matching issues.
 */
UINT MemoryAnalysisCountSeverity(LPMEMORY_CARVING_REPORT Report, MEMORY_CARVING_SEVERITY Severity) {
    UINT Count = 0;

    if (Report == NULL) {
        return 0;
    }

    for (UINT Index = 0; Index < Report->IssueCount; Index++) {
        if (Report->Issues[Index].Severity == Severity) {
            Count++;
        }
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Return the display name of an arena identifier.
 * @param ArenaID Process arena identifier.
 * @return Arena display name.
 */
LPCSTR MemoryAnalysisGetArenaName(UINT ArenaID) {
    switch (ArenaID) {
        case PROCESS_ARENA_IMAGE:
            return TEXT("Image");
        case PROCESS_ARENA_HEAP:
            return TEXT("Heap");
        case PROCESS_ARENA_STACK:
            return TEXT("Stack");
        case PROCESS_ARENA_MODULE:
            return TEXT("Module");
        case PROCESS_ARENA_SYSTEM:
            return TEXT("System");
        case PROCESS_ARENA_MMIO:
            return TEXT("Mmio");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/

/**
 * @brief Return the display name of a stack kind identifier.
 * @param Kind Stack kind value.
 * @return Stack kind display name.
 */
LPCSTR MemoryAnalysisGetStackKindName(UINT Kind) {
    switch (Kind) {
        case MEMORY_CARVING_STACK_KIND_TASK:
            return TEXT("Task");
        case MEMORY_CARVING_STACK_KIND_SYSTEM:
            return TEXT("System");
        case MEMORY_CARVING_STACK_KIND_IST1:
            return TEXT("Ist1");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/
