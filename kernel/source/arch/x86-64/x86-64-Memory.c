
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


    x86-64 memory high-level orchestration

\************************************************************************/


#include "arch/x86-64/x86-64-Memory-Internal.h"
#include "memory/BuddyAllocator.h"

/************************************************************************/

#define MEMORY_MANAGER_VER_MAJOR 1
#define MEMORY_MANAGER_VER_MINOR 0

static UINT MemoryManagerCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION MemoryManagerDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_MEMORY,
    .VersionMajor = MEMORY_MANAGER_VER_MAJOR,
    .VersionMinor = MEMORY_MANAGER_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Intel",
    .Product = "MemoryManager",
    .Alias = "memory_manager",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = MemoryManagerCommands};

/************************************************************************/

/************************************************************************/

/**
 * @brief Retrieves the memory manager driver descriptor.
 * @return Pointer to the memory manager driver.
 */
LPDRIVER MemoryManagerGetDriver(void) {
    return &MemoryManagerDriver;
}

/************************************************************************/

typedef enum {
    PAGE_TABLE_POPULATE_IDENTITY,
    PAGE_TABLE_POPULATE_SINGLE_ENTRY,
    PAGE_TABLE_POPULATE_EMPTY
} PAGE_TABLE_POPULATE_MODE;

#define USERLAND_SEEDED_TABLES 1

typedef struct tag_PAGE_TABLE_SETUP {
    UINT DirectoryIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PAGE_TABLE_POPULATE_MODE Mode;
    PHYSICAL Physical;
    union {
        struct {
            PHYSICAL PhysicalBase;
            BOOL ProtectBios;
        } Identity;
        struct {
            UINT TableIndex;
            PHYSICAL Physical;
            U32 ReadWrite;
            U32 Privilege;
            U32 Global;
        } Single;
    } Data;
} PAGE_TABLE_SETUP, *LPPAGE_TABLE_SETUP;

typedef struct tag_REGION_SETUP {
    LPCSTR Label;
    UINT PdptIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PAGE_TABLE_SETUP Tables[64];
    UINT TableCount;
} REGION_SETUP, *LPREGION_SETUP;

/************************************************************************/

typedef struct tag_LOW_REGION_SHARED_TABLES {
    PHYSICAL BiosTablePhysical;
    PHYSICAL IdentityTablePhysical;
} LOW_REGION_SHARED_TABLES;

LOW_REGION_SHARED_TABLES DATA_SECTION LowRegionSharedTables = {
    .BiosTablePhysical = NULL,
    .IdentityTablePhysical = NULL,
};

PHYSICAL DATA_SECTION BootstrapAllocatorMetadataPhysical = NULL;

/************************************************************************/

/************************************************************************/

/**
 * @brief Compute the maximum page count whose buddy metadata fits a fixed window.
 *
 * @param InitialPageCount Current page count candidate.
 * @param MetadataWindowSize Available bytes for buddy metadata.
 * @return Best page count that fits the metadata window, or 0 when none fits.
 */
static UINT ComputePageCountForMetadataWindow(UINT InitialPageCount, UINT MetadataWindowSize) {
    UINT Low = 0;
    UINT High = InitialPageCount;
    UINT Best = 0;

    while (Low <= High) {
        UINT Mid = Low + ((High - Low) >> 1);
        UINT MidMetadataSize = (UINT)PAGE_ALIGN(BuddyGetMetadataSize(Mid));

        if (MidMetadataSize <= MetadataWindowSize) {
            Best = Mid;
            Low = Mid + 1;
        } else {
            if (Mid == 0) {
                break;
            }
            High = Mid - 1;
        }
    }

    return Best;
}

/************************************************************************/

/**
 * @brief Determine the largest paging granularity compatible with a region.
 * @param Base Canonical base of the region.
 * @param PageCount Number of pages described by the region.
 * @return Corresponding granularity.
 */
MEMORY_REGION_GRANULARITY ComputeDescriptorGranularity(LINEAR Base, UINT PageCount) {
    if (PageCount == 0) {
        return MEMORY_REGION_GRANULARITY_4K;
    }

    if ((((U64)Base & ((U64)N_1GB - (U64)1)) == 0) && (PageCount % (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_NUM_ENTRIES)) == 0) {
        return MEMORY_REGION_GRANULARITY_1G;
    }

    if ((((U64)Base & ((U64)N_2MB - (U64)1)) == 0) && (PageCount % PAGE_TABLE_NUM_ENTRIES) == 0) {
        return MEMORY_REGION_GRANULARITY_2M;
    }

    return MEMORY_REGION_GRANULARITY_4K;
}

/************************************************************************/

/**
 * @brief Obtain or create a shared identity table used by the low region.
 *
 * The function lazily allocates the table, initializes its entries according
 * to the requested physical base and BIOS protection flag, and records the
 * physical address for future reuse.
 *
 * @param TablePhysical Receives the physical address of the shared table.
 * @param PhysicalBase Physical base used to populate identity mappings.
 * @param ProtectBios TRUE when BIOS ranges must be cleared from the table.
 * @param Label Debug label describing the shared table.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL EnsureSharedLowTable(
    PHYSICAL* TablePhysical,
    PHYSICAL PhysicalBase,
    BOOL ProtectBios,
    LPCSTR Label) {

    if (TablePhysical == NULL || Label == NULL) {
        ERROR(TEXT("Invalid shared table parameters"));
        return FALSE;
    }

    if (*TablePhysical != NULL) {
        return TRUE;
    }

    PHYSICAL Physical = AllocPhysicalPage();

    if (Physical == NULL) {
        ERROR(TEXT("Out of physical pages for shared %s table"), Label);
        return FALSE;
    }

    LINEAR Linear = MapTemporaryPhysicalPage6(Physical);

    if (Linear == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage3 failed for shared %s table"), Label);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    LPPAGE_TABLE Table = (LPPAGE_TABLE)Linear;
    MemorySet(Table, 0, PAGE_SIZE);

#if !defined(PROTECT_BIOS)
    UNUSED(ProtectBios);
#endif

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL EntryPhysical = PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
        if (ProtectBios) {
            BOOL Protected =
                (EntryPhysical == 0) || (EntryPhysical > PROTECTED_ZONE_START && EntryPhysical <= PROTECTED_ZONE_END);

            if (Protected) {
                ClearPageTableEntry(Table, Index);
                continue;
            }
        }
#endif

        WritePageTableEntryValue(
            Table,
            Index,
            MakePageTableEntryValue(
                EntryPhysical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    *TablePhysical = Physical;


    return TRUE;
}

/************************************************************************/
/**
 * @brief Clear a REGION_SETUP structure to its default state.
 * @param Region Structure to reset.
 */
void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

/**
 * @brief Release the physical resources owned by a REGION_SETUP.
 * @param Region Structure that tracks the allocated tables.
 */
void ReleaseRegionSetup(REGION_SETUP* Region) {
    if (Region->PdptPhysical != NULL) {
        FreePhysicalPage(Region->PdptPhysical);
        Region->PdptPhysical = NULL;
    }

    if (Region->DirectoryPhysical != NULL) {
        FreePhysicalPage(Region->DirectoryPhysical);
        Region->DirectoryPhysical = NULL;
    }

    for (UINT Index = 0; Index < Region->TableCount; Index++) {
        if (Region->Tables[Index].Physical != NULL) {
            FreePhysicalPage(Region->Tables[Index].Physical);
            Region->Tables[Index].Physical = NULL;
        }
    }

    Region->TableCount = 0;
}

/************************************************************************/

/**
 * @brief Allocate a page table and populate it according to the setup entry.
 * @param Region Parent region that will own the table.
 * @param Table Table description containing allocation parameters.
 * @param Directory Page-directory view used to link the table.
 * @return TRUE on success, FALSE when allocation or mapping fails.
 */
BOOL AllocateTableAndPopulate(
    REGION_SETUP* Region,
    PAGE_TABLE_SETUP* Table,
    LPPAGE_DIRECTORY Directory) {


    Table->Physical = AllocPhysicalPage();

    if (Table->Physical == NULL) {
        ERROR(TEXT("%s region out of physical pages"), Region->Label);
        return FALSE;
    }


    LINEAR TableLinear = MapTemporaryPhysicalPage6(Table->Physical);

    if (TableLinear == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage3 failed for %s table"), Region->Label);
        FreePhysicalPage(Table->Physical);
        Table->Physical = NULL;
        return FALSE;
    }


    LPPAGE_TABLE TableVA = (LPPAGE_TABLE)TableLinear;
    MemorySet(TableVA, 0, PAGE_SIZE);

    switch (Table->Mode) {
    case PAGE_TABLE_POPULATE_IDENTITY:
        for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
            PHYSICAL Physical = Table->Data.Identity.PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
            if (Table->Data.Identity.ProtectBios) {
                BOOL Protected =
                    (Physical == 0) || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

                if (Protected) {
                    ClearPageTableEntry(TableVA, Index);
                    continue;
                }
            }
#endif

            WritePageTableEntryValue(
                TableVA,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    Table->ReadWrite,
                    Table->Privilege,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    Table->Global,
                    /*Fixed*/ 1));
        }
        break;

    case PAGE_TABLE_POPULATE_SINGLE_ENTRY:
        WritePageTableEntryValue(
            TableVA,
            Table->Data.Single.TableIndex,
            MakePageTableEntryValue(
                Table->Data.Single.Physical,
                Table->Data.Single.ReadWrite,
                Table->Data.Single.Privilege,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                Table->Data.Single.Global,
                /*Fixed*/ 1));
        break;

    case PAGE_TABLE_POPULATE_EMPTY:
    default:
        break;
    }

    WritePageDirectoryEntryValue(
        Directory,
        Table->DirectoryIndex,
        MakePageDirectoryEntryValue(
            Table->Physical,
            Table->ReadWrite,
            Table->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Table->Global,
            /*Fixed*/ 1));


    return TRUE;
}

/************************************************************************/

/**
 * @brief Build identity-mapped tables for the low virtual address space.
 * @param Region Region descriptor to populate.
 * @param UserSeedTables Number of empty user tables to pre-allocate.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Low");
    Region->PdptIndex = GetPdptEntry(0);
    Region->ReadWrite = 1;
    Region->Privilege = (UserSeedTables != 0u) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;


    if (EnsureSharedLowTable(&LowRegionSharedTables.BiosTablePhysical, 0, TRUE, TEXT("BIOS")) == FALSE) {
        return FALSE;
    }

    if (EnsureSharedLowTable(
            &LowRegionSharedTables.IdentityTablePhysical,
            ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
            FALSE,
            TEXT("low identity")) == FALSE) {
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();


    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("Low region out of physical pages"));
        if (Region->PdptPhysical != NULL) {
            FreePhysicalPage(Region->PdptPhysical);
            Region->PdptPhysical = NULL;
        }
        if (Region->DirectoryPhysical != NULL) {
            FreePhysicalPage(Region->DirectoryPhysical);
            Region->DirectoryPhysical = NULL;
        }
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage4(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed for low PDPT"));
        return FALSE;
    }


    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed for low directory"));
        return FALSE;
    }


    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));

    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.BiosTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex + 1u,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.IdentityTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    if (BootstrapAllocatorMetadataPhysical != NULL) {
        UINT BitmapDirectoryIndex = (UINT)(BootstrapAllocatorMetadataPhysical >> PAGE_TABLE_CAPACITY_MUL);

        if (BitmapDirectoryIndex >= 2u && BitmapDirectoryIndex < PAGE_TABLE_NUM_ENTRIES) {
            if (Region->TableCount >= ARRAY_COUNT(Region->Tables)) {
                ERROR(TEXT("Bitmap seed table overflow index=%u"), BitmapDirectoryIndex);
                return FALSE;
            }

            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
            Table->DirectoryIndex = BitmapDirectoryIndex;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_KERNEL;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_IDENTITY;
            Table->Data.Identity.PhysicalBase = (PHYSICAL)(BitmapDirectoryIndex << PAGE_TABLE_CAPACITY_MUL);
            Table->Data.Identity.ProtectBios = FALSE;

            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;

            Region->TableCount++;
        }
    }

    if (UserSeedTables != 0u) {
        UINT TableCapacity = (UINT)(sizeof(Region->Tables) / sizeof(Region->Tables[0]));

        UINT BaseDirectory = GetDirectoryEntry((U64)VMA_USER);

        for (UINT Index = 0; Index < UserSeedTables; Index++) {
            if (Region->TableCount >= TableCapacity) {
                ERROR(TEXT("User seed table overflow index=%u count=%u capacity=%u"),
                    Index,
                    Region->TableCount,
                    TableCapacity);
                return FALSE;
            }

            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];

            Table->DirectoryIndex = BaseDirectory + Index;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_USER;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_EMPTY;
            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;
            Region->TableCount++;
        }
    }


    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute the number of bytes of kernel memory that must be mapped.
 * @return Size in bytes covered by kernel tables.
 */
UINT ComputeKernelCoverageBytes(void) {
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    PHYSICAL CoverageEnd = PhysBaseKernel + (PHYSICAL)KernelStartup.KernelSize;

    if (KernelStartup.StackTop > CoverageEnd) {
        CoverageEnd = KernelStartup.StackTop;
    }

    if (CoverageEnd <= PhysBaseKernel) {
        return PAGE_TABLE_CAPACITY;
    }

    PHYSICAL Coverage = CoverageEnd - PhysBaseKernel;
    UINT CoverageBytes = (UINT)PAGE_ALIGN((UINT)Coverage);

    if (CoverageBytes < PAGE_TABLE_CAPACITY) {
        CoverageBytes = PAGE_TABLE_CAPACITY;
    }

    return CoverageBytes;
}

/************************************************************************/

/**
 * @brief Create identity mappings for the kernel virtual address space.
 * @param Region Region descriptor to populate.
 * @param TableCountRequired Number of tables that must be allocated.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupKernelRegion(REGION_SETUP* Region, UINT TableCountRequired) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Kernel");
    Region->PdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    if (TableCountRequired > ARRAY_COUNT(Region->Tables)) {
        ERROR(TEXT("Kernel region requires too many tables"));
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();


    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("Kernel region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage4(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed for kernel PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed for kernel directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));

    UINT DirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;

    for (UINT TableIndex = 0; TableIndex < TableCountRequired; TableIndex++) {
        PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
        Table->DirectoryIndex = DirectoryIndex + TableIndex;
        Table->ReadWrite = 1;
        Table->Privilege = PAGE_PRIVILEGE_KERNEL;
        Table->Global = 0;
        Table->Mode = PAGE_TABLE_POPULATE_IDENTITY;
        Table->Data.Identity.PhysicalBase = PhysBaseKernel + ((PHYSICAL)TableIndex << PAGE_TABLE_CAPACITY_MUL);
        Table->Data.Identity.ProtectBios = FALSE;

        if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
            return FALSE;
        }
        Region->TableCount++;
    }


    return TRUE;
}

/************************************************************************/

/**
 * @brief Prepare the high user paging root and map the task runner trampoline.
 * @param Region Region descriptor to populate.
 * @param TaskRunnerPhysical Physical address of the task runner code.
 * @param TaskRunnerTableIndex Page table index that contains the trampoline.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupHighUserRegion(
    REGION_SETUP* Region,
    PHYSICAL TaskRunnerPhysical,
    UINT TaskRunnerTableIndex) {
    UINT TaskRunnerPdptIndex;
    ResetRegionSetup(Region);

    Region->Label = TEXT("HighUser");
    Region->PdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_USER;
    Region->Global = 0;
    TaskRunnerPdptIndex = Region->PdptIndex;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();


    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("High user region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage4(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed for high user PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed for high user directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        TaskRunnerPdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));

    PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
    Table->DirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    Table->ReadWrite = 1;
    Table->Privilege = PAGE_PRIVILEGE_USER;
    Table->Global = 0;
    Table->Mode = PAGE_TABLE_POPULATE_SINGLE_ENTRY;
    Table->Data.Single.TableIndex = TaskRunnerTableIndex;
    Table->Data.Single.Physical = TaskRunnerPhysical;
    Table->Data.Single.ReadWrite = 0;
    Table->Data.Single.Privilege = PAGE_PRIVILEGE_USER;
    Table->Data.Single.Global = 0;

    if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
        return FALSE;
    }

    Region->TableCount++;
    return TRUE;
}

/************************************************************************/

/*
U64 ReadTableEntrySnapshot(PHYSICAL TablePhysical, UINT Index) {
    if (TablePhysical == NULL) {
        return 0;
    }

    LINEAR Linear = MapTemporaryPhysicalPage6(TablePhysical);

    if (Linear == NULL) {
        return 0;
    }

    return ReadPageTableEntryValue((LPPAGE_TABLE)Linear, Index);
}
*/

/**
 * @brief Build the kernel-mode long mode paging hierarchy.
 *
 * Low, kernel and task runner regions are prepared, connected to a newly
 * allocated PML4 and the recursive slot is configured before returning the
 * physical address.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP HighUserRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;
    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&HighUserRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);
    UNUSED(KernelPml4Index);

    UINT KernelCoverageBytes = ComputeKernelCoverageBytes();
    UINT KernelTableCount = KernelCoverageBytes >> PAGE_TABLE_CAPACITY_MUL;
    if (KernelTableCount == 0u) KernelTableCount = 1u;

    if (SetupLowRegion(&LowRegion, 0u) == FALSE) goto Out;

    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);


    if (SetupHighUserRegion(&HighUserRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage4(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        KernelPml4Index,
        MakePageDirectoryEntryValue(
            KernelRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        TaskRunnerPml4Index,
        MakePageDirectoryEntryValue(
            HighUserRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));



    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&HighUserRegion);
        return NULL;
    }

    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Create a user-mode page directory derived from the current context.
 *
 * Kernel mappings are cloned from the active CR3 while the low region is
 * seeded with identity tables and the task runner trampoline is prepared as
 * needed.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP HighUserRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&HighUserRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);
    UNUSED(KernelPml4Index);

    if (SetupLowRegion(&LowRegion, USERLAND_SEEDED_TABLES) == FALSE) goto Out;

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage4(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    if (CurrentPml4 == NULL) {
        ERROR(TEXT("Current PML4 pointer is NULL"));
        goto Out;
    }

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Mirror only the seeded low-memory entries from the active CR3 into the
    // new user directory. Copying the full low directory would leak user-space
    // mappings from the current process and may block VMA_USER allocations in
    // child processes.
    U64 CurrentLowPdptEntry = ReadPageDirectoryEntryValue(CurrentPml4, LowPml4Index);

    if ((CurrentLowPdptEntry & PAGE_FLAG_PRESENT) != 0) {
        PHYSICAL CurrentLowPdptPhysical = (PHYSICAL)(CurrentLowPdptEntry & PAGE_MASK);
        LPPAGE_DIRECTORY CurrentLowPdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(CurrentLowPdptPhysical);

        if (CurrentLowPdpt != NULL) {
            U64 CurrentLowDirectoryEntry = ReadPageDirectoryEntryValue(CurrentLowPdpt, 0);

            if ((CurrentLowDirectoryEntry & PAGE_FLAG_PRESENT) != 0) {
                PHYSICAL CurrentLowDirectoryPhysical = (PHYSICAL)(CurrentLowDirectoryEntry & PAGE_MASK);
                LPPAGE_DIRECTORY CurrentLowDirectory =
                    (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage6(CurrentLowDirectoryPhysical);
                LPPAGE_DIRECTORY NewLowDirectory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(LowRegion.DirectoryPhysical);

                if (CurrentLowDirectory != NULL && NewLowDirectory != NULL) {
                    UINT MaxSeededEntries = USERLAND_SEEDED_TABLES;
                    if (MaxSeededEntries > PAGE_TABLE_NUM_ENTRIES) {
                        MaxSeededEntries = PAGE_TABLE_NUM_ENTRIES;
                    }

                    for (UINT Index = 0; Index < MaxSeededEntries; Index++) {
                        U64 EntryValue = ReadPageDirectoryEntryValue(CurrentLowDirectory, Index);
                        if ((EntryValue & PAGE_FLAG_PRESENT) != 0) {
                            WritePageDirectoryEntryValue(NewLowDirectory, Index, EntryValue);
                        }
                    }
                }
            }
        }
    }

    UINT KernelBaseIndex = PML4_ENTRY_COUNT / 2u;
    UINT ClonedKernelEntries = 0u;
    for (UINT Index = KernelBaseIndex; Index < PML4_ENTRY_COUNT; Index++) {
        if (Index == PML4_RECURSIVE_SLOT) continue;

        U64 EntryValue = ReadPageDirectoryEntryValue(CurrentPml4, Index);
        if ((EntryValue & PAGE_FLAG_PRESENT) == 0) continue;

        WritePageDirectoryEntryValue(Pml4, Index, EntryValue);
        ClonedKernelEntries++;
    }

    if (ClonedKernelEntries == 0u) {
        ERROR(TEXT("No kernel PML4 entries copied from current directory"));
        goto Out;
    }


    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    if (SetupHighUserRegion(&HighUserRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;

    Pml4Linear = MapTemporaryPhysicalPage4(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage4 failed on PML4"));
        goto Out;
    }

    Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;

    U64 TaskRunnerEntryValue = MakePageDirectoryEntryValue(
        HighUserRegion.PdptPhysical,
        /*ReadWrite*/ 1,
        PAGE_PRIVILEGE_USER,
        /*WriteThrough*/ 0,
        /*CacheDisabled*/ 0,
        /*Global*/ 0,
        /*Fixed*/ 1);

    WritePageDirectoryEntryValue(Pml4, TaskRunnerPml4Index, TaskRunnerEntryValue);

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));



    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&HighUserRegion);
        return NULL;
    }

    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Handles driver commands for the memory manager.
 *
 * DF_LOAD initializes the memory manager and marks the driver as ready.
 * DF_UNLOAD clears the ready flag; no shutdown routine is available.
 *
 * @param Function Driver command selector.
 * @param Parameter Unused.
 * @return DF_RETURN_SUCCESS on success, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static UINT MemoryManagerCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((MemoryManagerDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeMemoryManager();
            MemoryManagerDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((MemoryManagerDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            MemoryManagerDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(MEMORY_MANAGER_VER_MAJOR, MEMORY_MANAGER_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Initialize the x86-64 memory manager and install the kernel mappings.
 *
 * The routine prepares the physical buddy allocator, constructs a new kernel page
 * directory, loads it and finalizes the descriptor tables required for long
 * mode execution.
 */
void InitializeMemoryManager(void) {
    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT RequestedPageCount = KernelStartup.PageCount;
    UINT WorkingPageCount = RequestedPageCount;
    UINT BuddyMetadataSize = 0;
    UINT BuddyMetadataSizeAligned = 0;

    UINT ReservedBytes = KernelStartup.KernelReservedBytes;
    if (ReservedBytes < KernelStartup.KernelSize) {
        ERROR(TEXT("Invalid kernel reserved span (reserved=%u size=%u)"),
            ReservedBytes,
            KernelStartup.KernelSize);
        ConsolePanic(TEXT("Invalid boot kernel reserved span"));
        DO_THE_SLEEPING_BEAUTY;
    }

    PHYSICAL LoaderReservedEnd =
        KernelStartup.KernelPhysicalBase + (PHYSICAL)PAGE_ALIGN((PHYSICAL)ReservedBytes);
    PHYSICAL BuddyMetadataPhysical = 0;
    const UINT LowWindowLowerSize = (UINT)((PHYSICAL)LOW_MEMORY_HALF - (PHYSICAL)N_1MB);
    const UINT LowWindowUpperSize = (UINT)((PHYSICAL)RESERVED_LOW_MEMORY - (PHYSICAL)LOW_MEMORY_THREE_QUARTER);

    SetLoaderReservedPhysicalRange(KernelStartup.KernelPhysicalBase, LoaderReservedEnd);

    while (TRUE) {
        BuddyMetadataSize = BuddyGetMetadataSize(WorkingPageCount);
        BuddyMetadataSizeAligned = (UINT)PAGE_ALIGN(BuddyMetadataSize);

        BOOL FoundMetadataRange = FALSE;

        FoundMetadataRange = FindAvailableMemoryRangeInWindow(
            (PHYSICAL)LOW_MEMORY_THREE_QUARTER,
            (PHYSICAL)RESERVED_LOW_MEMORY,
            KernelStartup.KernelPhysicalBase,
            LoaderReservedEnd,
            BuddyMetadataSizeAligned,
            &BuddyMetadataPhysical);

        if (FoundMetadataRange == FALSE) {
            FoundMetadataRange = FindAvailableMemoryRangeInWindow(
                (PHYSICAL)N_1MB,
                (PHYSICAL)LOW_MEMORY_HALF,
                KernelStartup.KernelPhysicalBase,
                LoaderReservedEnd,
                BuddyMetadataSizeAligned,
                &BuddyMetadataPhysical);
        }

        if (FoundMetadataRange != FALSE) {
            break;
        }

        UINT CandidateFromLowLowerWindow = ComputePageCountForMetadataWindow(WorkingPageCount, LowWindowLowerSize);
        UINT CandidateFromLowUpperWindow = ComputePageCountForMetadataWindow(WorkingPageCount, LowWindowUpperSize);
        UINT NextPageCount = CandidateFromLowLowerWindow;
        if (CandidateFromLowUpperWindow > NextPageCount) {
            NextPageCount = CandidateFromLowUpperWindow;
        }

        if (NextPageCount >= WorkingPageCount) {
            if (WorkingPageCount == 0) {
                NextPageCount = 0;
            } else {
                NextPageCount = WorkingPageCount - 1;
            }
        }

        if (NextPageCount == 0) {
            ERROR(TEXT("Could not place buddy metadata (size=%u)"), BuddyMetadataSizeAligned);
            ConsolePanic(TEXT("Could not place physical memory allocator metadata"));
            DO_THE_SLEEPING_BEAUTY;
        }

        WorkingPageCount = NextPageCount;
    }

    if (WorkingPageCount != RequestedPageCount) {
        WARNING(TEXT("Clamped page count from %u to %u to place metadata"),
            RequestedPageCount,
            WorkingPageCount);
        KernelStartup.PageCount = WorkingPageCount;
        KernelStartup.MemorySize = (PHYSICAL)WorkingPageCount << PAGE_SIZE_MUL;
    }

    BootstrapAllocatorMetadataPhysical = BuddyMetadataPhysical;
    SetPhysicalAllocatorMetadataRange(BuddyMetadataPhysical, BuddyMetadataPhysical + BuddyMetadataSizeAligned);

    if (BuddyInitialize((LINEAR)BuddyMetadataPhysical, BuddyMetadataSizeAligned, KernelStartup.PageCount) == FALSE) {
        ERROR(TEXT("BuddyInitialize failed (PA=%p size=%u pages=%u)"),
            (LPVOID)(LINEAR)BuddyMetadataPhysical,
            BuddyMetadataSizeAligned,
            KernelStartup.PageCount);
        ConsolePanic(TEXT("Could not initialize physical memory allocator"));
        DO_THE_SLEEPING_BEAUTY;
    }

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }


    LoadPageDirectory(NewPageDirectory);

    ConsoleInvalidateFramebufferMapping();

    FlushTLB();


    InitializeRegionDescriptorTracking();


    Kernel_x86_32.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("GDT"));

    if (Kernel_x86_32.GDT == NULL) {
        ERROR(TEXT("AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }


    InitializeGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_x86_32.GDT);


    LoadGlobalDescriptorTable((PHYSICAL)Kernel_x86_32.GDT, GDT_SIZE - 1);


    LogGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_x86_32.GDT, 10);


}

/************************************************************************/

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
    LINEAR Base = N_4MB;

    if (StartBase != 0) {
        LINEAR CanonStart = CanonicalizeLinearAddress(StartBase);
        if (CanonStart >= Base) {
            Base = CanonStart;
        }
    }

    while (TRUE) {
        if (IsRegionFree(Base, Size) == TRUE) {
            return Base;
        }

        LINEAR NextBase = CanonicalizeLinearAddress(Base + PAGE_SIZE);
        if (NextBase <= Base) {
            return NULL;
        }
        Base = NextBase;
    }
}

/************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
void FreeEmptyPageTables(void) {
    LPPML4 Pml4 = GetCurrentPml4VA();
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);

    for (UINT Pml4Index = 0u; Pml4Index < KernelPml4Index; Pml4Index++) {
        if (Pml4Index == PML4_RECURSIVE_SLOT) {
            continue;
        }

        U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
        if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0u) {
            continue;
        }
        if ((Pml4EntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
            continue;
        }

        PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
        LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage4(PdptPhysical);

        for (UINT PdptIndex = 0u; PdptIndex < PAGE_TABLE_NUM_ENTRIES; PdptIndex++) {
            U64 PdptEntryValue = ReadPageDirectoryEntryValue(Pdpt, PdptIndex);
            if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                continue;
            }
            if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                continue;
            }

            PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
            LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage5(DirectoryPhysical);

            for (UINT DirIndex = 0u; DirIndex < PAGE_TABLE_NUM_ENTRIES; DirIndex++) {
                U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirIndex);
                if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                    continue;
                }
                if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                    continue;
                }

                PHYSICAL TablePhysical = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
                if (TablePhysical == 0u) {
                    continue;
                }

                LPPAGE_TABLE Table = (LPPAGE_TABLE)MapTemporaryPhysicalPage6(TablePhysical);
                if (Table == NULL) {
                    ERROR(TEXT("Failed to map table PML4=%u PDPT=%u Dir=%u phys=%p"),
                        Pml4Index, PdptIndex, DirIndex, (LPVOID)TablePhysical);
                    continue;
                }

                if (PageTableIsEmpty(Table)) {
                    SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0u);
                    ClearPageDirectoryEntry(Directory, DirIndex);
                }
            }
        }
    }
}

/************************************************************************/

BOOL PopulateRegionPagesLegacy(LINEAR Base,
                                      PHYSICAL Target,
                                      UINT NumPages,
                                      U32 Flags,
                                      LINEAR RollbackBase,
                                      LPCSTR FunctionName) {
    LPPAGE_TABLE Table = NULL;
    PHYSICAL Physical = NULL;
    U32 ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;
    BOOL BootstrapTrace = (G_RegionDescriptorBootstrap == TRUE);

    if (PteCacheDisabled) PteWriteThrough = 0;

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    if (BootstrapTrace) {
    }

    for (UINT Index = 0; Index < NumPages; Index++) {
        if (BootstrapTrace) {
        }
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        LINEAR CurrentLinear = MemoryPageIteratorGetLinear(&Iterator);

        BOOL IsLargePage = FALSE;
        if (BootstrapTrace) {
        }

        if (!TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage)) {
            if (BootstrapTrace) {
            }
            if (IsLargePage) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (BootstrapTrace) {
            }
            if (AllocPageTable(CurrentLinear) == NULL) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (BootstrapTrace) {
            }
            if (!TryGetPageTableForIterator(&Iterator, &Table, NULL)) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }
            if (BootstrapTrace) {
            }
        } else {
            if (BootstrapTrace) {
            }
        }

        U32 Privilege = PAGE_PRIVILEGE(CurrentLinear);
        U32 FixedFlag = (Flags & (ALLOC_PAGES_IO | ALLOC_PAGES_FIXED)) ? 1u : 0u;
        U32 BaseFlags = BuildPageFlags(ReadWrite, Privilege, PteWriteThrough, PteCacheDisabled, 0, FixedFlag);
        U32 ReservedFlags = BaseFlags & ~PAGE_FLAG_PRESENT;
        PHYSICAL ReservedPhysical = (PHYSICAL)(MAX_U32 & ~(PAGE_SIZE - 1));

        WritePageTableEntryValue(Table, TabEntry, MakePageEntryRaw(ReservedPhysical, ReservedFlags));
        if (BootstrapTrace) {
        }

        if (Flags & ALLOC_PAGES_COMMIT) {
            if (BootstrapTrace) {
            }
            if (Target != 0) {
                Physical = Target + (PHYSICAL)(Index << PAGE_SIZE_MUL);
                if (BootstrapTrace) {
                }

                if (FixedFlag != 0u) {
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 1));
                } else {
                    SetPhysicalPageMark((UINT)(Physical >> PAGE_SIZE_MUL), 1);
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 0));
                    if (BootstrapTrace) {
                    }
                }
            } else {
                if (BootstrapTrace) {
                }
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    ERROR(TEXT("AllocPhysicalPage failed"));
                    BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                    G_RegionDescriptorBootstrap = TRUE;
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    G_RegionDescriptorBootstrap = PreviousBootstrap;
                    return FALSE;
                }

                WritePageTableEntryValue(
                    Table,
                    TabEntry,
                    MakePageTableEntryValue(
                        Physical,
                        ReadWrite,
                        Privilege,
                        PteWriteThrough,
                        PteCacheDisabled,
                        /*Global*/ 0,
                        /*Fixed*/ 0));
                if (BootstrapTrace) {
                }
            }
        }

        MemoryPageIteratorStepPage(&Iterator);
        Base += PAGE_SIZE;
        if (BootstrapTrace) {
        }
    }

    if (BootstrapTrace) {
    }
    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocate and map a physical region into the linear address space.
 * @param Base Desired base address or 0. When zero and ALLOC_PAGES_AT_OR_OVER
 *             is not set, the allocator picks any free region.
 * @param Target Desired physical base address or 0. Requires
 *               ALLOC_PAGES_COMMIT when specified. Use with ALLOC_PAGES_IO to
 *               map device memory without touching the physical allocator state.
 * @param Size Size in bytes, rounded up to page granularity. Limited to 25% of
 *             the available physical memory.
 * @param Flags Mapping flags:
 *              - ALLOC_PAGES_COMMIT: allocate and map backing pages.
 *              - ALLOC_PAGES_READWRITE: request writable pages (read-only
 *                otherwise).
 *              - ALLOC_PAGES_AT_OR_OVER: accept any region starting at or
 *                above Base.
 *              - ALLOC_PAGES_UC / ALLOC_PAGES_WC: control cache attributes
 *                (UC has priority over WC).
 *              - ALLOC_PAGES_IO: keep physical pages marked fixed for MMIO.
 *              - ALLOC_PAGES_FIXED: keep exact physical pages owned by another
 *                kernel object marked fixed.
 * @return Allocated linear base address or 0 on failure.
 */
LINEAR AllocRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    LINEAR Pointer = NULL;
    UINT NumPages = 0;
    BOOL BootstrapTrace = (G_RegionDescriptorBootstrap == TRUE);

    if (BootstrapTrace) {
    }

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("Size %x exceeds 25%% of memory (%lX)"), Size, KernelStartup.MemorySize / 4);
        return NULL;
    }

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;  // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    Base = CanonicalizeLinearAddress(Base);

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            ERROR(TEXT("Target not page-aligned (%x)"), Target);
            return NULL;
        }

        if ((Flags & ALLOC_PAGES_IO) == 0 && (Flags & ALLOC_PAGES_COMMIT) == 0) {
            ERROR(TEXT("Exact PMA mapping requires COMMIT"));
            return NULL;
        }

        if (ValidatePhysicalTargetRange(Target, NumPages) == FALSE) {
            ERROR(TEXT("Target range cannot be addressed"));
            return NULL;
        }
        /* NOTE: Do not reject pages already marked used here.
           Target may come from AllocPhysicalPage(), which marks the page in the allocator.
           We will just map it and keep the mark consistent. */
    }

    /* If the calling process requests that a linear address be mapped,
       see if the region is not already allocated. */
    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (IsRegionFree(Base, Size) == FALSE) {
            return NULL;
        }
    }

    /* If the calling process does not care about the base address of
       the region, try to find a region which is at least as large as
       the "Size" parameter. */
    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        if (BootstrapTrace) {
        }

        LINEAR NewBase = FindFreeRegion(Base, Size);

        if (NewBase == NULL) {
            return NULL;
        }

        Base = NewBase;
        if (BootstrapTrace) {
        }

    }

    // Set the return value to "Base".
    Pointer = Base;


    BOOL FastPathUsed = FALSE;

    if (FastPathUsed == FALSE) {
        if (BootstrapTrace) {
        }
        if (PopulateRegionPagesLegacy(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
            return NULL;
        }
        if (BootstrapTrace) {
        }
    }

    if (BootstrapTrace) {
    }
    if (RegionTrackAllocForProcess(TrackingProcess, Pointer, Target, NumPages << PAGE_SIZE_MUL, Flags, Tag) == FALSE) {
        G_RegionDescriptorBootstrap = TRUE;
        FreeRegionForProcess(TrackingProcess, Pointer, NumPages << PAGE_SIZE_MUL);
        G_RegionDescriptorBootstrap = FALSE;
        return NULL;
    }

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    if (BootstrapTrace) {
    }

    return Pointer;
}

/************************************************************************/

LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    return AllocRegionForProcess(NULL, Base, Target, Size, Flags, Tag);
}

/************************************************************************/

/**
 * @brief Resize an existing linear region.
 * @param Base Base linear address of the region.
 * @param Target Physical base address or 0. Must match the existing mapping
 *               when resizing committed regions.
 * @param Size Current size in bytes.
 * @param NewSize Desired size in bytes.
 * @param Flags Mapping flags used for the region (see AllocRegion).
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ResizeRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {

    if (Base == 0) {
        ERROR(TEXT("Base cannot be null"));
        return FALSE;
    }

    Base = CanonicalizeLinearAddress(Base);

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("New size %x exceeds 25%% of memory (%u)"),
              NewSize,
              KernelStartup.MemorySize / 4);
        return FALSE;
    }

    UINT CurrentPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    UINT RequestedPages = (NewSize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (CurrentPages == 0) CurrentPages = 1;
    if (RequestedPages == 0) RequestedPages = 1;

    if (RequestedPages == CurrentPages) {
        DEBUG(TEXT("No page count change"));
        return TRUE;
    }

    if (RequestedPages > CurrentPages) {
        UINT AdditionalPages = RequestedPages - CurrentPages;
        LINEAR NewBase = Base + ((LINEAR)CurrentPages << PAGE_SIZE_MUL);
        UINT AdditionalSize = AdditionalPages << PAGE_SIZE_MUL;

        if (IsRegionFree(NewBase, AdditionalSize) == FALSE) {
            DEBUG(TEXT("Additional region not free at %x"), NewBase);
            return FALSE;
        }

        PHYSICAL AdditionalTarget = 0;
        if (Target != 0) {
            AdditionalTarget = Target + (PHYSICAL)(CurrentPages << PAGE_SIZE_MUL);
        }


        BOOL ExpansionFastPathUsed = FALSE;

        if (ExpansionFastPathUsed == FALSE) {
            if (PopulateRegionPagesLegacy(NewBase,
                                          AdditionalTarget,
                                          AdditionalPages,
                                          Flags,
                                          NewBase,
                                          TEXT("ResizeRegion")) == FALSE) {
                return FALSE;
            }
        }

        RegionTrackResizeForProcess(TrackingProcess, Base, Size, NewSize, Flags);

        FlushTLB();
    } else {
        UINT PagesToRelease = CurrentPages - RequestedPages;
        if (PagesToRelease != 0) {
            LINEAR ReleaseBase = Base + ((LINEAR)RequestedPages << PAGE_SIZE_MUL);
            UINT ReleaseSize = PagesToRelease << PAGE_SIZE_MUL;

            FreeRegionForProcess(TrackingProcess, ReleaseBase, ReleaseSize);
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    return ResizeRegionForProcess(NULL, Base, Target, Size, NewSize, Flags);
}

/************************************************************************/
/**
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegionForProcess(LPPROCESS TrackingProcess, LINEAR Base, UINT Size) {
    LINEAR OriginalBase = Base;
    UINT NumPages = (Size + (PAGE_SIZE - 1u)) >> PAGE_SIZE_MUL;
    if (NumPages == 0u) {
        NumPages = 1u;
    }


    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    LPPAGE_TABLE Table = NULL;
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(CanonicalBase);

    UNUSED(OriginalBase);
    UNUSED(Size);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
#if DEBUG_OUTPUT == 0
        UNUSED(DirEntry);
#endif
        BOOL IsLargePage = FALSE;

        if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) && PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);
            BOOL Fixed = PageTableEntryIsFixed(Table, TabEntry);

            if (Fixed == FALSE) {
                SetPhysicalPageMark((UINT)(EntryPhysical >> PAGE_SIZE_MUL), 0u);
            }

            ClearPageTableEntry(Table, TabEntry);
        } else if (IsLargePage == FALSE) {
            DEBUG(TEXT("Missing mapping Dir=%u Tab=%u IsLarge=%u"),
                DirEntry,
                TabEntry,
                (UINT)(IsLargePage ? 1u : 0u));
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    RegionTrackFreeForProcess(TrackingProcess, CanonicalBase, NumPages << PAGE_SIZE_MUL);
    FreeEmptyPageTables();
    FlushTLB();
    return TRUE;
}

/************************************************************************/

BOOL FreeRegion(LINEAR Base, UINT Size) {
    return FreeRegionForProcess(NULL, Base, Size);
}

/************************************************************************/

/**
 * @brief Map an I/O physical range into virtual memory.
 * @param PhysicalBase Physical base address.
 * @param Size Size in bytes.
 * @return Linear address or 0 on failure.
 */
LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size) {
    // Basic parameter checks
    if (PhysicalBase == 0 || Size == 0) {
        ERROR(TEXT("Invalid parameters (PA=%x Size=%x)"), PhysicalBase, Size);
        return NULL;
    }

    // Calculate page-aligned base and adjusted size for non-aligned addresses
    UINT PageOffset = (UINT)(PhysicalBase & (PAGE_SIZE - 1));
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));


    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL,          // Start search in kernel space to avoid user space
        AlignedPhysicalBase, // Page-aligned PMA
        AdjustedSize,        // Page-aligned size
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO |
            ALLOC_PAGES_AT_OR_OVER,  // Do not touch RAM allocator state; mark PTE.Fixed; search at or over VMA_KERNEL
        TEXT("IOMemory")
    );

    if (AlignedResult == NULL) {
        return NULL;
    }

    // Return the address adjusted for the original offset
    LINEAR CanonicalAligned = CanonicalizeLinearAddress(AlignedResult);
    LINEAR result = CanonicalizeLinearAddress(CanonicalAligned + (LINEAR)PageOffset);
    return result;
}

/************************************************************************/

/**
 * @brief Map a framebuffer physical range using write-combining when possible.
 * @param PhysicalBase Physical base address.
 * @param Size Size in bytes.
 * @return Linear address or 0 on failure.
 */
LINEAR MapFramebufferMemory(PHYSICAL PhysicalBase, UINT Size) {
    if (PhysicalBase == 0 || Size == 0) {
        ERROR(TEXT("Invalid parameters (PA=%p Size=%u)"),
              (LPVOID)(LINEAR)PhysicalBase,
              Size);
        return NULL;
    }

    UINT PageOffset = (UINT)(PhysicalBase & (PAGE_SIZE - 1));
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL,
        AlignedPhysicalBase,
        AdjustedSize,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_WC |
            ALLOC_PAGES_IO | ALLOC_PAGES_AT_OR_OVER,
        TEXT("Framebuffer")
    );

    if (AlignedResult == NULL) {
        WARNING(TEXT("WC mapping failed, falling back to UC"));
        return MapIOMemory(PhysicalBase, Size);
    }

    LINEAR CanonicalAligned = CanonicalizeLinearAddress(AlignedResult);
    LINEAR result = CanonicalizeLinearAddress(CanonicalAligned + (LINEAR)PageOffset);
    return result;
}

/************************************************************************/

/**
 * @brief Unmap a previously mapped I/O range.
 * @param LinearBase Linear base address.
 * @param Size Size in bytes.
 * @return TRUE on success.
 */
BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size) {
    // Basic parameter checks
    if (LinearBase == 0 || Size == 0) {
        ERROR(TEXT("Invalid parameters (LA=%x Size=%x)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip allocator page release if PTE.Fixed was set
    return FreeRegion(CanonicalizeLinearAddress(LinearBase), Size);
}

/************************************************************************/

/**
 * @brief Compute a preferred base address for the kernel heap.
 * @param HeapSize Requested heap size in bytes.
 * @return Preferred linear base address in kernel space.
 */
LINEAR GetKernelHeapPreferredBase(UINT HeapSize) {
    UNUSED(HeapSize);

    U64 CanonicalHighEnd = ~((U64)0);
    U64 Midpoint = (U64)VMA_KERNEL + ((CanonicalHighEnd - (U64)VMA_KERNEL) / (U64)2);

    return (LINEAR)(Midpoint & PAGE_MASK);
}

/************************************************************************/

/**
 * @brief Allocate a kernel region - wrapper around AllocRegion with VMA_KERNEL and AT_OR_OVER.
 * @param Target Physical base address (0 for any).
 * @param Size Size in bytes.
 * @param Flags Additional allocation flags.
 * @return Linear address or 0 on failure.
 */
LINEAR AllocKernelRegion(PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    if (G_RegionDescriptorBootstrap == TRUE) {
    }

    // Always use VMA_KERNEL base and add AT_OR_OVER flag
    LINEAR Result = AllocRegion(VMA_KERNEL, Target, Size, Flags | ALLOC_PAGES_AT_OR_OVER, Tag);

    if (G_RegionDescriptorBootstrap == TRUE) {
    }

    return Result;
}

/************************************************************************/

LINEAR ResizeKernelRegion(LINEAR Base, UINT Size, UINT NewSize, U32 Flags) {
    return ResizeRegion(Base, 0, Size, NewSize, Flags | ALLOC_PAGES_AT_OR_OVER);
}
