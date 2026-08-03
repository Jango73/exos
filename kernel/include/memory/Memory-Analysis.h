
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

#ifndef MEMORY_ANALYSIS_H_INCLUDED
#define MEMORY_ANALYSIS_H_INCLUDED

#include "Base.h"
#include "memory/Memory.h"
#include "process/Process-Arena.h"

/************************************************************************/

#define MEMORY_CARVING_MAX_REGIONS 128
#define MEMORY_CARVING_MAX_STACKS 32
#define MEMORY_CARVING_MAX_ISSUES 96
#define MEMORY_CARVING_ISSUE_TEXT 96

/************************************************************************/

typedef enum {
    MEMORY_CARVING_STACK_KIND_TASK = 0,
    MEMORY_CARVING_STACK_KIND_SYSTEM = 1,
    MEMORY_CARVING_STACK_KIND_IST1 = 2
} MEMORY_CARVING_STACK_KIND;

typedef struct tag_MEMORY_CARVING_REGION {
    LINEAR Base;                     // Region base address
    LINEAR Limit;                    // Region end address (exclusive)
    UINT Size;                       // Region size in bytes
    UINT PageCount;                  // Number of mapped pages
    U32 Attributes;                  // MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_* flags
    STR Tag[MEMORY_REGION_TAG_MAX];  // Allocation tag
} MEMORY_CARVING_REGION, *LPMEMORY_CARVING_REGION;

typedef struct tag_MEMORY_CARVING_STACK {
    LINEAR Base;                  // Stack base address
    LINEAR Limit;                 // Stack end address (exclusive)
    UINT Size;                    // Stack size in bytes
    U32 Kind;                     // MEMORY_CARVING_STACK_KIND_* value
    UINT TaskIndex;               // Owning task index within the snapshot
    STR TaskName[MAX_USER_NAME];  // Owning task name
} MEMORY_CARVING_STACK, *LPMEMORY_CARVING_STACK;

typedef struct tag_PROCESS_MEMORY_CARVING_SNAPSHOT {
    BOOL AddressSpaceInitialized;                               // Arena state is available
    BOOL ArenasCarved;                                          // Arenas form a contiguous carved partition
    STR FileName[MAX_PATH_NAME];                                // Process executable file path
    LINEAR PageDirectory;                                       // Physical address of the page directory
    LINEAR HeapBase;                                            // Process heap base
    UINT HeapSize;                                              // Process heap size in bytes
    PROCESS_ARENA_RANGE Arenas[PROCESS_ARENA_COUNT];            // Process address space arenas
    MEMORY_CARVING_REGION Regions[MEMORY_CARVING_MAX_REGIONS];  // Memory region descriptors
    UINT RegionCount;                                           // Number of captured regions
    MEMORY_CARVING_STACK Stacks[MEMORY_CARVING_MAX_STACKS];     // Task stacks
    UINT StackCount;                                            // Number of captured stacks
} PROCESS_MEMORY_CARVING_SNAPSHOT, *LPPROCESS_MEMORY_CARVING_SNAPSHOT;

typedef enum {
    MEMORY_CARVING_SEVERITY_INFO = 0,
    MEMORY_CARVING_SEVERITY_WARNING = 1,
    MEMORY_CARVING_SEVERITY_ERROR = 2
} MEMORY_CARVING_SEVERITY;

typedef struct tag_MEMORY_CARVING_ISSUE {
    MEMORY_CARVING_SEVERITY Severity;     // Issue severity
    STR Text[MEMORY_CARVING_ISSUE_TEXT];  // Issue description
} MEMORY_CARVING_ISSUE, *LPMEMORY_CARVING_ISSUE;

typedef struct tag_MEMORY_CARVING_REPORT {
    UINT IssueCount;    // Number of recorded issues
    UINT OverlapCount;  // Number of recorded overlapping zones
    MEMORY_CARVING_ISSUE Issues[MEMORY_CARVING_MAX_ISSUES];
} MEMORY_CARVING_REPORT, *LPMEMORY_CARVING_REPORT;

/************************************************************************/

void MemoryAnalysisResetReport(LPMEMORY_CARVING_REPORT Report);
void MemoryAnalysisAnalyzeCarving(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot, LPMEMORY_CARVING_REPORT Report);
UINT MemoryAnalysisCountSeverity(LPMEMORY_CARVING_REPORT Report, MEMORY_CARVING_SEVERITY Severity);
LPCSTR MemoryAnalysisGetArenaName(UINT ArenaID);
LPCSTR MemoryAnalysisGetStackKindName(UINT Kind);

/************************************************************************/

#endif  // MEMORY_ANALYSIS_H_INCLUDED
