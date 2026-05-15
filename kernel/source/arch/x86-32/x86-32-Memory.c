
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


    Memory

\************************************************************************/

#include "Base.h"
#include "arch/x86-32/x86-32-Log.h"
#include "console/Console.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Buddy-Allocator.h"
#include "memory/Memory-Descriptors.h"
#include "memory/Memory.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "system/System.h"

/************************************************************************\

    Virtual Address Space (32-bit)
    ┌──────────────────────────────────────────────────────────────────────────┐
    │ 0x00000000 .................................................. 0xBFFFFFFF │
    │                [User space]  (PDE 0..KernelDir-1)                        │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xC0000000 .................................................. 0xFFFFEFFF │
    │                [Kernel space] (PDE KernelDir .. 1022)                    │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xFFFFF000 .................................................. 0xFFFFFFFF │
    │                [Self-map window]                                         │
    │                0xFFFFF000 = PD_VA (Page Directory as an array of PDEs)   │
    │                0xFFC00000 = PT_BASE_VA (all Page Tables visible)         │
    └──────────────────────────────────────────────────────────────────────────┘

    Page Directory (1024 PDEs, each 4B)
    dir = (VMA >> 22)
    tab = (VMA >> 12) & 0x3FF
    ofs =  VMA & 0xFFF

                      PDE index
            ┌────────────┬────────────┬────────────┬────────────┬─────────────┐
            │     0      │     1      │   ...      │ KernelDir  │   1023      │
            ├────────────┼────────────┼────────────┼────────────┼─────────── ─┤
    points→ │  Low PT    │   PT #1    │   ...      │ Kernel PT  │  SELF-MAP   │
    to PA   │ (0..4MB)   │            │            │ (VMA_KERNEL)│ (PD itself)│
            └────────────┴────────────┴────────────┴────────────┴─────────────┘
                                                              ^
                                                              |
                                         PDE[1023] -> PD physical page (recursive)
                                                              |
                                                              v
    PD_VA = 0xFFFFF000 ----------------------------------> Page Directory (VA alias)


    All Page Tables via the recursive window:
    PT_BASE_VA = 0xFFC00000
    PT for PDE = D is at:   PT_VA(D) = 0xFFC00000 + D * 0x1000

    Examples:
    - PT of PDE 0:        0xFFC00000
    - PT of KernelDir:    0xFFC00000 + KernelDir*0x1000
    - PT of PDE 1023:     0xFFC00000 + 1023*0x1000  (not used for mappings)


    Resolution path for any VMA:
           VMA
            │
       dir = VMA>>22  ------>  PD_VA[dir] (PDE)  ------>  PT_VA(dir)[tab] (PTE)  ------>  PA + ofs

    Kernel mappings installed at init:
    - PDE[0]         -> Low PT (identity map 0..4MB)
    - PDE[KernelDir] -> Kernel PT (maps VMA_KERNEL .. VMA_KERNEL+4MB-1)
    - PDE[1023]      -> PD itself (self-map)


    Temporary mapping mechanism (MapTemporaryPhysicalPage1, MapTemporaryPhysicalPage2, ...):
    1) 3 VAs reserved dynamically (e.g., G_TempLinear1, G_TempLinear2, G_TempLinear3).
    2) To map a physical frame P into G_TempLinear1:
       - Compute dir/tab of G_TempLinear1
       - Write the PTE via the PT window:
           PT_VA(dir) = PT_BASE_VA + dir*0x1000, entry [tab]
       - Execute `invlpg [G_TempLinear1]`
       - The physical frame P is now accessible via the VA G_TempLinear1

    Simplified view of the two temporary pages:

                         (reserved via AllocRegion, not present by default)
    G_TempLinear1  -\    ┌────────────────────────────────────────────┐
                    |-─> │ PTE < (Present=1, RW=1, ..., Address=P>>12)│  map/unmap to chosen PA
    G_TempLinear2  -/    └────────────────────────────────────────────┘
                                   ^
                                   │ (written through) PT_VA(dir(G_TempLinearX)) = PT_BASE_VA + dir*0x1000
                                   │
                              PD self-map (PD_VA, PT_BASE_VA)

    PDE[1023] points to the Page Directory itself.
    PD_VA = 0xFFFFF000 gives access to the current PD (as PTE-like entries).
    PT_BASE_VA = 0xFFC00000 provides a window for Page Tables:
    PT for directory index D is at PT_BASE_VA + (D * PAGE_SIZE).

    Temporary physical access is done by remapping two reserved
    linear pages (G_TempLinear1, G_TempLinear2, G_TempLinear3) on demand.

    =================================================================

    PCI BAR mapping process (example: Intel E1000 NIC)

    ┌───────────────────────────┐
    │  PCI Configuration Space  │
    │  (accessed via PCI config │
    │   reads/writes)           │
    └───────────┬───────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
    ┌────────────────────────────────┐
    │ BAR0 value = Physical address  │
    │ of device registers (MMIO)     │
    │ + resource size                │
    └───────────┬────────────────────┘
                │
                │ Map physical MMIO region into
                │ kernel virtual space
                │ (uncached for DMA safety)
                ▼
    ┌───────────────────────────┐
    │ AllocRegion(Base=0,       │
    │   Target=BAR0,            │
    │   Size=MMIO size,         │
    │   Flags=ALLOC_PAGES_COMMIT│
    │         | ALLOC_PAGES_UC) │
    └───────────┬───────────────┘
                │
                │ Returns Linear (VMA) address
                │ where the driver can access MMIO
                ▼
    ┌───────────────────────────────┐
    │ Driver reads/writes registers │
    │ via *(volatile U32*)(VMA+ofs) │
    │ Example: E1000_CTRL register  │
    └───────────────────────────────┘

    NOTES:
    - MMIO (Memory-Mapped I/O) must be UNCACHED (UC) to avoid
     stale data and incorrect ordering.
    - BARs can also point to I/O port ranges instead of MMIO.
    - PCI devices can have multiple BARs for different resources.

\************************************************************************/

#define MEMORY_MANAGER_VER_MAJOR 1
#define MEMORY_MANAGER_VER_MINOR 0

// INTERNAL SELF-MAP + TEMP MAPPING ]
/// These are internal-only constants; do not export in public headers.

#define PD_RECURSIVE_SLOT 1023u         /* PDE index used for self-map */
#define PD_VA ((LINEAR)0xFFFFF000)      /* Page Directory linear alias */
#define PT_BASE_VA ((LINEAR)0xFFC00000) /* Page Tables linear window   */

// Uncomment below to mark BIOS memory pages "not present" in the page tables
// #define PROTECT_BIOS
#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

/************************************************************************/
// INTERNAL SELF-MAP + TEMP MAPPING ]

extern LINEAR __bss_init_end;

static BOOL G_TempLinearInitialized = FALSE;
static LINEAR G_TempLinear1 = 0;
static LINEAR G_TempLinear2 = 0;
static LINEAR G_TempLinear3 = 0;

/************************************************************************/

/**
 * @brief Place temporary mapping slots just after the kernel image.
 */
static void InitializeTemporaryLinearSlots(void) {
    if (G_TempLinearInitialized) {
        return;
    }

    LINEAR Base = ((LINEAR)(&__bss_init_end) + (LINEAR)(PAGE_SIZE - 1)) & ~(LINEAR)(PAGE_SIZE - 1);
    G_TempLinear1 = Base;
    G_TempLinear2 = G_TempLinear1 + PAGE_SIZE;
    G_TempLinear3 = G_TempLinear2 + PAGE_SIZE;
    G_TempLinearInitialized = TRUE;
}

/************************************************************************/

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
    .Product = "Memory",
    .Alias = "memory",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = MemoryManagerCommands};

/************************************************************************/

/**
 * @brief Retrieves the memory manager driver descriptor.
 * @return Pointer to the memory manager driver.
 */
LPDRIVER MemoryManagerGetDriver(void) { return &MemoryManagerDriver; }

/************************************************************************/

/**
 * @brief Clip a 64-bit range to 32 bits.
 * @param base Input base address.
 * @param len Length of the range.
 * @param outBase Resulting 32-bit base.
 * @param outLen Resulting 32-bit length.
 * @return Non-zero if clipping succeeded.
 */
static inline BOOL ClipTo32Bit(U64 base, U64 len, U32* outBase, U32* outLen) {
    U64 limit = U64_Make(1, 0x00000000u);
    if (len.HI == 0 && len.LO == 0) return 0;
    if (U64_Cmp(base, limit) >= 0) return 0;
    U64 end = U64_Add(base, len);
    if (U64_Cmp(end, limit) > 0) end = limit;
    U64 newLen = U64_Sub(end, base);

    if (newLen.HI != 0) {
        *outBase = base.LO;
        *outLen = 0xFFFFFFFFu - base.LO;
    } else {
        *outBase = base.LO;
        *outLen = newLen.LO;
    }
    return (*outLen != 0);
}

/************************************************************************/
/**
 * @brief Determine the largest paging granularity compatible with a region.
 * @param Base Canonical base of the region.
 * @param PageCount Number of pages described by the region.
 * @return Corresponding granularity.
 */
MEMORY_REGION_GRANULARITY ComputeDescriptorGranularity(LINEAR Base, UINT PageCount) {
    UNUSED(Base);
    UNUSED(PageCount);
    return MEMORY_REGION_GRANULARITY_4K;
}

/************************************************************************/

/**
 * @brief Get the page directory index for a linear address.
 * @param Address Linear address.
 * @return Page directory entry index.
 */
static inline UINT GetDirectoryEntry(LINEAR Address) { return Address >> PAGE_TABLE_CAPACITY_MUL; }

/************************************************************************/

/**
 * @brief Get the page table index for a linear address.
 * @param Address Linear address.
 * @return Page table entry index.
 */
static inline UINT GetTableEntry(LINEAR Address) { return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL; }

/************************************************************************/
// Self-map helpers (no public exposure)

/**
 * @brief Obtain the virtual address of the current page directory.
 * @return Pointer to page directory.
 */
static inline LPPAGE_DIRECTORY GetCurrentPageDirectoryVA(void) { return (LPPAGE_DIRECTORY)PD_VA; }

/************************************************************************/

/**
 * @brief Get the virtual address of the page table for a linear address.
 * @param Address Linear address.
 * @return Pointer to page table.
 */
static inline LPPAGE_TABLE GetPageTableVAFor(LINEAR Address) {
    UINT dir = GetDirectoryEntry(Address);
    return (LPPAGE_TABLE)(PT_BASE_VA + (dir << PAGE_SIZE_MUL));
}

/************************************************************************/

/**
 * @brief Get a pointer to the raw PTE entry for a linear address.
 * @param Address Linear address.
 * @return Pointer to the PTE.
 */
static inline volatile U32* GetPageTableEntryRawPointer(LINEAR Address) {
    UINT tab = GetTableEntry(Address);
    return (volatile U32*)&GetPageTableVAFor(Address)[tab];
}

/************************************************************************/
// Compose a raw 32-bit PTE value from fields + physical address.

static inline UINT MakePageTableEntryValue(
    PHYSICAL Physical, UINT ReadWrite, UINT Privilege, UINT WriteThrough, UINT CacheDisabled, UINT Global, UINT Fixed) {
    UINT val = 0;
    val |= 1u;  // Present

    if (ReadWrite) val |= (1u << 1);
    if (Privilege) val |= (1u << 2);  // 1=user, 0=kernel
    if (WriteThrough) val |= (1u << 3);
    if (CacheDisabled) val |= (1u << 4);

    // Accessed (bit 5) / Dirty (bit 6) left to CPU
    if (Global) val |= (1u << 8);
    if (Fixed) val |= (1u << 9);  // Your code uses this bit in PTE

    val |= (UINT)(Physical & ~(PAGE_SIZE - 1));  // Frame address aligned

    return val;

    /*
    PAGETABLE TableEntry;

    TableEntry.Present = 1;
    TableEntry.ReadWrite = ReadWrite;
    TableEntry.Privilege = Privilege;
    TableEntry.WriteThrough = WriteThrough;
    TableEntry.CacheDisabled = CacheDisabled;
    TableEntry.Accessed = 0;
    TableEntry.Dirty = 0;
    TableEntry.Reserved = 0;
    TableEntry.Global = Global;
    TableEntry.User = 0;
    TableEntry.Fixed = Fixed;
    TableEntry.Address = Physical & ~(PAGE_SIZE - 1);

    return *((UINT*)&TableEntry);
    */
}

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, UINT ReadWrite, UINT Privilege, UINT WriteThrough, UINT CacheDisabled,
    UINT Global, UINT Fixed) {
    volatile U32* Pte = GetPageTableEntryRawPointer(Linear);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT dir = GetDirectoryEntry(Linear);

    if (!Directory[dir].Present) {
        ERROR(TEXT("PDE not present for VA %x (dir=%d)"), Linear, dir);
        return;  // Or panic
    }

    *Pte = MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    volatile U32* Pte = GetPageTableEntryRawPointer(Linear);
    *Pte = 0;
    InvalidatePage(Linear);
}

/************************************************************************/
// Public temporary map #1

/**
 * @brief Map a physical page to a temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear1 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage1] Temp slot #1 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    return G_TempLinear1;
}

/************************************************************************/
// Public temporary map #2

/**
 * @brief Map a physical page to the second temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear2 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage2] Temp slot #2 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    return G_TempLinear2;
}

/************************************************************************/
// Public temporary map #3

/**
 * @brief Map a physical page to the third temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear3 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage3] Temp slot #3 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear3, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    return G_TempLinear3;
}

/************************************************************************/

/**
 * @brief Synchronize a kernel-space mapping into the kernel page directory.
 * @param Linear Linear address being mapped.
 * @param CurrentPdeValue Raw PDE value from the current page directory.
 * @param CurrentPteValue Raw PTE value from the current page table.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SyncKernelMappingForPage(LINEAR Linear, U32 CurrentPdeValue, U32 CurrentPteValue) {
    if (Linear < VMA_KERNEL) return TRUE;

    PHYSICAL KernelDirectoryPhysical = KernelProcess.PageDirectory;
    if (KernelDirectoryPhysical == 0) {
        KernelDirectoryPhysical = KernelStartup.PageDirectory;
    }

    if (KernelDirectoryPhysical == 0) {
        ERROR(TEXT("No kernel page directory available (Linear=%p)"), (LPVOID)Linear);
        return FALSE;
    }

    PHYSICAL CurrentDirectoryPhysical = GetPageDirectory();
    if (CurrentDirectoryPhysical == 0 || CurrentDirectoryPhysical == KernelDirectoryPhysical) {
        return TRUE;
    }

    UINT DirectoryIndex = GetDirectoryEntry(Linear);
    UINT TableIndex = GetTableEntry(Linear);

    LINEAR KernelDirectoryLinear = MapTemporaryPhysicalPage1(KernelDirectoryPhysical);
    if (KernelDirectoryLinear == 0) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed for kernel directory %p"), (LPVOID)KernelDirectoryPhysical);
        return FALSE;
    }

    LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)KernelDirectoryLinear;
    volatile U32* KernelPdePtr = (volatile U32*)&KernelDirectory[DirectoryIndex];
    PHYSICAL KernelTablePhysical;

    if ((*KernelPdePtr & PAGE_FLAG_PRESENT) == 0u) {
        *KernelPdePtr = CurrentPdeValue;
        KernelTablePhysical = (PHYSICAL)(CurrentPdeValue & PAGE_MASK);
    } else {
        KernelTablePhysical = (PHYSICAL)(*KernelPdePtr & PAGE_MASK);
    }

    LINEAR KernelTableLinear = MapTemporaryPhysicalPage2(KernelTablePhysical);
    if (KernelTableLinear == 0) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed for kernel table %p"), (LPVOID)KernelTablePhysical);
        return FALSE;
    }

    LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)KernelTableLinear;
    volatile U32* KernelPtePtr = (volatile U32*)&KernelTable[TableIndex];

    if (*KernelPtePtr != CurrentPteValue) {
        *KernelPtePtr = CurrentPteValue;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    UINT dir = GetDirectoryEntry(Pointer);
    UINT tab = GetTableEntry(Pointer);

    // Bounds check
    if (dir >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (tab >= PAGE_TABLE_NUM_ENTRIES) return FALSE;

    // Page directory present?
    if (Directory[dir].Present == 0) return FALSE;

    // Page table present?
    LPPAGE_TABLE Table = GetPageTableVAFor(Pointer);
    if (Table[tab].Present == 0) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Attempt to resolve a kernel-space page fault by cloning the kernel mapping.
 * @param FaultAddress Linear address that triggered the fault.
 * @return TRUE when the fault was resolved, FALSE otherwise.
 */
BOOL ResolveKernelPageFault(LINEAR FaultAddress) {
    if (FaultAddress < VMA_KERNEL) {
        return FALSE;
    }

    PHYSICAL KernelDirectoryPhysical = KernelProcess.PageDirectory;
    if (KernelDirectoryPhysical == 0) {
        KernelDirectoryPhysical = KernelStartup.PageDirectory;
    }

    if (KernelDirectoryPhysical == 0) {
        DEBUG(TEXT("No kernel directory available (Fault=%X)"), FaultAddress);
        return FALSE;
    }

    PHYSICAL CurrentDirectoryPhysical = GetPageDirectory();
    if (CurrentDirectoryPhysical == 0 || CurrentDirectoryPhysical == KernelDirectoryPhysical) {
        return FALSE;
    }

    UINT DirectoryIndex = GetDirectoryEntry(FaultAddress);
    UINT TableIndex = GetTableEntry(FaultAddress);

    if (DirectoryIndex >= PAGE_TABLE_NUM_ENTRIES) {
        DEBUG(TEXT("Directory index %u out of range (Fault=%X)"), DirectoryIndex, FaultAddress);
        return FALSE;
    }

    if (TableIndex >= PAGE_TABLE_NUM_ENTRIES) {
        DEBUG(TEXT("Table index %u out of range (Fault=%X)"), TableIndex, FaultAddress);
        return FALSE;
    }

    LINEAR KernelDirectoryLinear = MapTemporaryPhysicalPage1(KernelDirectoryPhysical);
    if (KernelDirectoryLinear == 0) {
        ERROR(TEXT("Unable to map kernel page directory"));
        return FALSE;
    }

    LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)KernelDirectoryLinear;
    volatile const U32 KernelPdeValue = ((volatile const U32*)KernelDirectory)[DirectoryIndex];
    if ((KernelPdeValue & PAGE_FLAG_PRESENT) == 0u) {
        DEBUG(TEXT("Kernel PDE[%u] not present (Fault=%X)"), DirectoryIndex, FaultAddress);
        return FALSE;
    }

    PHYSICAL KernelTablePhysical = (PHYSICAL)(KernelPdeValue & PAGE_MASK);
    LINEAR KernelTableLinear = MapTemporaryPhysicalPage2(KernelTablePhysical);
    if (KernelTableLinear == 0) {
        ERROR(TEXT("Unable to map kernel page table"));
        return FALSE;
    }

    LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)KernelTableLinear;
    volatile const U32 KernelPteValue = ((volatile const U32*)KernelTable)[TableIndex];
    if ((KernelPteValue & PAGE_FLAG_PRESENT) == 0u) {
        return FALSE;
    }

    LPPAGE_DIRECTORY CurrentDirectory = GetCurrentPageDirectoryVA();
    volatile U32* CurrentPdePtr = (volatile U32*)&CurrentDirectory[DirectoryIndex];
    BOOL NeedsFullFlush = FALSE;
    BOOL Updated = FALSE;

    if ((*CurrentPdePtr & PAGE_FLAG_PRESENT) == 0u || *CurrentPdePtr != KernelPdeValue) {
        *CurrentPdePtr = KernelPdeValue;
        NeedsFullFlush = TRUE;
        Updated = TRUE;
    }

    LPPAGE_TABLE CurrentTable = GetPageTableVAFor(FaultAddress);
    volatile U32* CurrentPtePtr = (volatile U32*)&CurrentTable[TableIndex];

    if (*CurrentPtePtr != KernelPteValue) {
        *CurrentPtePtr = KernelPteValue;
        Updated = TRUE;
    }

    if (Updated == FALSE) {
        return FALSE;
    }

    if (NeedsFullFlush) {
        FlushTLB();
    } else {
        InvalidatePage(FaultAddress);
    }

    DEBUG(TEXT("Mirrored kernel mapping for %X"), FaultAddress);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or 0 on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_TABLE TaskRunnerTable = NULL;

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);           // 4MB directory slot for VMA_KERNEL
    UINT DirTaskRunner = (VMA_TASK_RUNNER >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for VMA_TASK_RUNNER
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    UINT Index;

    // Allocate required physical pages (PD + 3 PTs)
    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL || PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    // Directory[0] -> identity map 0..4MB via PMA_LowTable
    Directory[0].Present = 1;
    Directory[0].ReadWrite = 1;
    Directory[0].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[0].WriteThrough = 0;
    Directory[0].CacheDisabled = 0;
    Directory[0].Accessed = 0;
    Directory[0].Reserved = 0;
    Directory[0].PageSize = 0;  // 4KB pages
    Directory[0].Global = 0;
    Directory[0].User = 0;
    Directory[0].Fixed = 1;
    Directory[0].Address = (PMA_LowTable >> PAGE_SIZE_MUL);

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to KERNEL_PHYSICAL_ORIGIN..+4MB-1
    Directory[DirKernel].Present = 1;
    Directory[DirKernel].ReadWrite = 1;
    Directory[DirKernel].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirKernel].WriteThrough = 0;
    Directory[DirKernel].CacheDisabled = 0;
    Directory[DirKernel].Accessed = 0;
    Directory[DirKernel].Reserved = 0;
    Directory[DirKernel].PageSize = 0;  // 4KB pages
    Directory[DirKernel].Global = 0;
    Directory[DirKernel].User = 0;
    Directory[DirKernel].Fixed = 1;
    Directory[DirKernel].Address = (PMA_KernelTable >> PAGE_SIZE_MUL);

    // Directory[DirTaskRunner] -> map VMA_TASK_RUNNER (one page) to TaskRunner physical location
    Directory[DirTaskRunner].Present = 1;
    Directory[DirTaskRunner].ReadWrite = 1;
    Directory[DirTaskRunner].Privilege = PAGE_PRIVILEGE_USER;
    Directory[DirTaskRunner].WriteThrough = 0;
    Directory[DirTaskRunner].CacheDisabled = 0;
    Directory[DirTaskRunner].Accessed = 0;
    Directory[DirTaskRunner].Reserved = 0;
    Directory[DirTaskRunner].PageSize = 0;  // 4KB pages
    Directory[DirTaskRunner].Global = 0;
    Directory[DirTaskRunner].User = 0;
    Directory[DirTaskRunner].Fixed = 1;
    Directory[DirTaskRunner].Address = (PMA_TaskRunnerTable >> PAGE_SIZE_MUL);

    // Install recursive mapping: PDE[1023] = PD
    Directory[PD_RECURSIVE_SLOT].Present = 1;
    Directory[PD_RECURSIVE_SLOT].ReadWrite = 1;
    Directory[PD_RECURSIVE_SLOT].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[PD_RECURSIVE_SLOT].WriteThrough = 0;
    Directory[PD_RECURSIVE_SLOT].CacheDisabled = 0;
    Directory[PD_RECURSIVE_SLOT].Accessed = 0;
    Directory[PD_RECURSIVE_SLOT].Reserved = 0;
    Directory[PD_RECURSIVE_SLOT].PageSize = 0;
    Directory[PD_RECURSIVE_SLOT].Global = 0;
    Directory[PD_RECURSIVE_SLOT].User = 0;
    Directory[PD_RECURSIVE_SLOT].Fixed = 1;
    Directory[PD_RECURSIVE_SLOT].Address = (PMA_Directory >> PAGE_SIZE_MUL);

    // Fill identity-mapped low table (0..4MB)
    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
#ifdef PROTECT_BIOS
        LINEAR Physical = (UINT)Index << 12;
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        LowTable[Index].Present = !Protected;
        LowTable[Index].ReadWrite = 1;
        LowTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        LowTable[Index].WriteThrough = 0;
        LowTable[Index].CacheDisabled = 0;
        LowTable[Index].Accessed = 0;
        LowTable[Index].Dirty = 0;
        LowTable[Index].Reserved = 0;
        LowTable[Index].Global = 0;
        LowTable[Index].User = 0;
        LowTable[Index].Fixed = 1;
        LowTable[Index].Address = Index;  // frame N -> 4KB*N
    }

    // Fill kernel mapping table by copying the current kernel PT
    VMA_PT = MapTemporaryPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;

    MemorySet(KernelTable, 0, PAGE_SIZE);

    UINT KernelFirstFrame = (PhysBaseKernel >> PAGE_SIZE_MUL);
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        KernelTable[Index].Present = 1;
        KernelTable[Index].ReadWrite = 1;
        KernelTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        KernelTable[Index].WriteThrough = 0;
        KernelTable[Index].CacheDisabled = 0;
        KernelTable[Index].Accessed = 0;
        KernelTable[Index].Dirty = 0;
        KernelTable[Index].Reserved = 0;
        KernelTable[Index].Global = 0;
        KernelTable[Index].User = 0;
        KernelTable[Index].Fixed = 1;
        KernelTable[Index].Address = KernelFirstFrame + Index;
    }

    // Fill TaskRunner page table - only map the first page where TaskRunner is located
    VMA_PT = MapTemporaryPhysicalPage2(PMA_TaskRunnerTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed on TaskRunnerTable"));
        goto Out_Error;
    }
    TaskRunnerTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);

    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + ((PHYSICAL)&__task_runner_start - VMA_KERNEL);

    UINT TaskRunnerTableIndex = GetTableEntry(VMA_TASK_RUNNER);

    TaskRunnerTable[TaskRunnerTableIndex].Present = 1;
    TaskRunnerTable[TaskRunnerTableIndex].ReadWrite = 1;  // Writable for task stack usage
    TaskRunnerTable[TaskRunnerTableIndex].Privilege = PAGE_PRIVILEGE_USER;
    TaskRunnerTable[TaskRunnerTableIndex].WriteThrough = 0;
    TaskRunnerTable[TaskRunnerTableIndex].CacheDisabled = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Accessed = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Dirty = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Reserved = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Global = 0;
    TaskRunnerTable[TaskRunnerTableIndex].User = 0;
    TaskRunnerTable[TaskRunnerTableIndex].Fixed = 1;
    TaskRunnerTable[TaskRunnerTableIndex].Address = TaskRunnerPhysical >> PAGE_SIZE_MUL;

    // TLB sync before returning
    FlushTLB();

    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_DIRECTORY CurrentPD = (LPPAGE_DIRECTORY)PD_VA;

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for VMA_KERNEL
    UINT Index;

    // Allocate required physical pages (PD + low identity PT)
    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL) {
        ERROR(TEXT("Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    // Directory[0] -> identity map 0..4MB via PMA_LowTable
    Directory[0].Present = 1;
    Directory[0].ReadWrite = 1;
    Directory[0].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[0].WriteThrough = 0;
    Directory[0].CacheDisabled = 0;
    Directory[0].Accessed = 0;
    Directory[0].Reserved = 0;
    Directory[0].PageSize = 0;  // 4KB pages
    Directory[0].Global = 0;
    Directory[0].User = 0;
    Directory[0].Fixed = 1;
    Directory[0].Address = (PMA_LowTable >> PAGE_SIZE_MUL);

    // Copy present PDEs from current directory, but skip process-owned user space
    // to allow new process to allocate its own region at VMA_USER
    UNUSED(VMA_TASK_RUNNER);
    UINT UserStartPDE = GetDirectoryEntry(VMA_USER);              // PDE index for VMA_USER
    UINT UserEndPDE = GetDirectoryEntry(VMA_USER_LIMIT - 1) - 1;  // Exclude the TaskRunner table
    for (Index = 1; Index < 1023; Index++) {                      // Skip 0 (already done) and 1023 (self-map)
        if (CurrentPD[Index].Present) {
            // Skip user space PDEs to avoid copying current process's user space
            if (Index >= UserStartPDE && Index <= UserEndPDE) {
                continue;
            }
            Directory[Index] = CurrentPD[Index];
        }
    }

    if (Directory[DirKernel].Present == 0 && CurrentPD[DirKernel].Present != 0) {
        Directory[DirKernel] = CurrentPD[DirKernel];
    }

    if (Directory[DirKernel].Present == 0) {
        ERROR(TEXT("Kernel PDE[%u] missing after copy"), DirKernel);
        goto Out_Error;
    }

    // Install recursive mapping: PDE[1023] = PD
    Directory[PD_RECURSIVE_SLOT].Present = 1;
    Directory[PD_RECURSIVE_SLOT].ReadWrite = 1;
    Directory[PD_RECURSIVE_SLOT].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[PD_RECURSIVE_SLOT].WriteThrough = 0;
    Directory[PD_RECURSIVE_SLOT].CacheDisabled = 0;
    Directory[PD_RECURSIVE_SLOT].Accessed = 0;
    Directory[PD_RECURSIVE_SLOT].Reserved = 0;
    Directory[PD_RECURSIVE_SLOT].PageSize = 0;
    Directory[PD_RECURSIVE_SLOT].Global = 0;
    Directory[PD_RECURSIVE_SLOT].User = 0;
    Directory[PD_RECURSIVE_SLOT].Fixed = 1;
    Directory[PD_RECURSIVE_SLOT].Address = (PMA_Directory >> PAGE_SIZE_MUL);

    // Fill identity-mapped low table (0..4MB) - manual setup like AllocPageDirectory
    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    // Initialize identity mapping for 0..4MB (same as AllocPageDirectory)
    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
#ifdef PROTECT_BIOS
        LINEAR Physical = (UINT)Index << 12;
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        LowTable[Index].Present = !Protected;
        LowTable[Index].ReadWrite = 1;
        LowTable[Index].Privilege = PAGE_PRIVILEGE_KERNEL;
        LowTable[Index].WriteThrough = 0;
        LowTable[Index].CacheDisabled = 0;
        LowTable[Index].Accessed = 0;
        LowTable[Index].Dirty = 0;
        LowTable[Index].Reserved = 0;
        LowTable[Index].Global = 0;
        LowTable[Index].User = 0;
        LowTable[Index].Fixed = 1;
        LowTable[Index].Address = Index;  // Identity mapping: page Index -> physical page Index
    }

    // TLB sync before returning
    FlushTLB();

    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a page table for the given base address.
 * @param Base Base linear address.
 * @return Linear address of the new table or 0.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PMA_Table = AllocPhysicalPage();

    if (PMA_Table == NULL) {
        ERROR(TEXT("Out of physical pages"));
        return NULL;
    }

    // Fill the directory entry that describes the new table
    UINT DirEntry = GetDirectoryEntry(Base);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    // Determine privilege: user space (< VMA_KERNEL) needs user privilege
    UINT Privilege = PAGE_PRIVILEGE(Base);

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = Privilege;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = PMA_Table >> PAGE_SIZE_MUL;

    // Clear the new table by mapping its physical page temporarily.
    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_Table);
    MemorySet((LPVOID)VMA_PT, 0, PAGE_SIZE);

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    // Return the linear address of the table via the recursive window
    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/

/**
 * @brief Check if a linear region is free of mappings.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE if region is free.
 */
BOOL IsRegionFree(LINEAR Base, UINT Size) {
    // DEBUG(TEXT("Enter : %x; %x"), Base, Size);

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    LINEAR Current = Base;

    // DEBUG(TEXT("Traversing pages"));

    for (UINT i = 0; i < NumPages; i++) {
        UINT dir = GetDirectoryEntry(Current);
        UINT tab = GetTableEntry(Current);

        if (Directory[dir].Present) {
            LPPAGE_TABLE Table = GetPageTableVAFor(Current);
            if (Table[tab].Present) return FALSE;
        }

        Current += PAGE_SIZE;
    }

    // DEBUG(TEXT("Exit"));

    return TRUE;
}

/************************************************************************/

static BOOL DoesRegionOverlapTrackedAllocation(LPPROCESS TrackingProcess, LINEAR Base, UINT Size) {
    LPMEMORY_REGION_LIST List =
        (TrackingProcess != NULL) ? GetProcessMemoryRegionList(TrackingProcess) : GetCurrentMemoryRegionList();
    LPMEMORY_REGION_DESCRIPTOR Current;
    LINEAR End;

    if (List == NULL || Size == 0) {
        return FALSE;
    }

    End = Base + (LINEAR)Size;
    if (End < Base) {
        return TRUE;
    }

    Current = FindDescriptorCoveringAddress(List, Base);
    if (Current != NULL) {
        return TRUE;
    }

    Current = List->Head;
    while (Current != NULL) {
        LINEAR RegionEnd = Current->CanonicalBase + (LINEAR)Current->Size;

        if (Current->CanonicalBase >= End) {
            break;
        }

        if (RegionEnd > Base) {
            return TRUE;
        }

        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    return FALSE;
}

/************************************************************************/

static BOOL IsRegionAvailableForAllocation(LPPROCESS TrackingProcess, LINEAR Base, UINT Size) {
    if (IsRegionFree(Base, Size) == FALSE) {
        return FALSE;
    }

    return DoesRegionOverlapTrackedAllocation(TrackingProcess, Base, Size) == FALSE;
}

/************************************************************************/

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
static LINEAR FindFreeRegion(LPPROCESS TrackingProcess, LINEAR StartBase, UINT Size) {
    UINT Base = N_4MB;

    if (StartBase >= Base) {
        Base = StartBase;
    }

    FOREVER {
        if (IsRegionAvailableForAllocation(TrackingProcess, Base, Size) == TRUE) return Base;
        Base += PAGE_SIZE;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
static void FreeEmptyPageTables(void) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    LINEAR Base = N_4MB;
    UINT DirEntry = 0;
    UINT Index = 0;
    UINT DestroyIt = 0;

    while (Base < VMA_KERNEL) {
        DestroyIt = 1;
        DirEntry = GetDirectoryEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = GetPageTableVAFor(Base);

            for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
                if (Table[Index].Address != NULL) DestroyIt = 0;
            }

            if (DestroyIt) {
                SetPhysicalPageMark(Directory[DirEntry].Address, 0);
                Directory[DirEntry].Present = 0;
                Directory[DirEntry].Address = NULL;
            }
        }

        Base += PAGE_TABLE_CAPACITY;
    }
}

/************************************************************************/

/**
 * @brief Translate a linear address to its physical counterpart (page-level granularity).
 * @param Address Linear address.
 * @return Physical page number or 0 on failure.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT DirEntry = GetDirectoryEntry(Address);
    UINT TabEntry = GetTableEntry(Address);

    if (Directory[DirEntry].Address == 0) return 0;

    LPPAGE_TABLE Table = GetPageTableVAFor(Address);
    if (Table[TabEntry].Address == 0) return 0;

    /* Compose physical: page frame << 12 | offset-in-page */
    return (PHYSICAL)((Table[TabEntry].Address << PAGE_SIZE_MUL) | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

static BOOL PopulateRegionPages(
    LINEAR Base, PHYSICAL Target, UINT NumPages, UINT Flags, LINEAR RollbackBase, LPCSTR FunctionName) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    PHYSICAL Physical = NULL;
    UINT ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    UINT PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    UINT PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;
    UINT Fixed = (Flags & (ALLOC_PAGES_IO | ALLOC_PAGES_FIXED)) ? 1 : 0;

    if (PteCacheDisabled) PteWriteThrough = 0;

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = GetDirectoryEntry(Base);
        UINT TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                FreeRegion(RollbackBase, (Index << PAGE_SIZE_MUL));
                DEBUG(TEXT("AllocPageTable failed"));
                return FALSE;
            }
        }

        Table = GetPageTableVAFor(Base);

        Table[TabEntry].Present = 0;
        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
        Table[TabEntry].WriteThrough = PteWriteThrough;
        Table[TabEntry].CacheDisabled = PteCacheDisabled;
        Table[TabEntry].Accessed = 0;
        Table[TabEntry].Dirty = 0;
        Table[TabEntry].Reserved = 0;
        Table[TabEntry].Global = 0;
        Table[TabEntry].User = 0;
        Table[TabEntry].Fixed = Fixed;
        Table[TabEntry].Address = NULL;

        if (Flags & ALLOC_PAGES_COMMIT) {
            if (Target != 0) {
                Physical = Target + (Index << PAGE_SIZE_MUL);

                if (Fixed) {
                    Table[TabEntry].Fixed = 1;
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                } else {
                    SetPhysicalPageMark(Physical >> PAGE_SIZE_MUL, 1);
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                }
            } else {
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    ERROR(TEXT("AllocPhysicalPage failed"));
                    FreeRegion(RollbackBase, (Index << PAGE_SIZE_MUL));
                    return FALSE;
                }

                Table[TabEntry].Present = 1;
                Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
                Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
            }
        }

        if (SyncKernelMappingForPage(Base, *(volatile U32*)&Directory[DirEntry], *(volatile U32*)&Table[TabEntry]) ==
            FALSE) {
            FreeRegion(RollbackBase, (Index << PAGE_SIZE_MUL));
            ERROR(TEXT("Kernel mapping synchronization failed for %p"), (LPVOID)Base);
            return FALSE;
        }

        Base += PAGE_SIZE;
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
LINEAR AllocRegionForProcess(
    LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    LINEAR Pointer = NULL;
    UINT NumPages = 0;

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("Size %x exceeds 25%% of memory (%x)"), Size, KernelStartup.MemorySize / 4);
        return NULL;
    }

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;  // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0 && (Flags & ALLOC_PAGES_IO) == 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            ERROR(TEXT("Target not page-aligned (%x)"), Target);
            return NULL;
        }

        if ((Flags & ALLOC_PAGES_COMMIT) == 0) {
            ERROR(TEXT("Exact PMA mapping requires COMMIT"));
            return NULL;
        }
        /* NOTE: Do not reject pages already marked used here.
           Target may come from AllocPhysicalPage(), which marks the page in the allocator.
           We will just map it and keep the mark consistent. */
    }

    /* If the calling process requests that a linear address be mapped,
       see if the region is not already allocated. */
    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (IsRegionAvailableForAllocation(TrackingProcess, Base, Size) == FALSE) {
            return NULL;
        }
    }

    /* If the calling process does not care about the base address of
       the region, try to find a region which is at least as large as
       the "Size" parameter. */
    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        LINEAR NewBase = FindFreeRegion(TrackingProcess, Base, Size);

        if (NewBase == NULL) {
            return NULL;
        }

        Base = NewBase;
    }

    // Set the return value to "Base".
    Pointer = Base;

    if (PopulateRegionPages(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
        return NULL;
    }

    if (RegionTrackAllocForProcess(TrackingProcess, Pointer, Target, NumPages << PAGE_SIZE_MUL, Flags, Tag) == FALSE) {
        FreeRegionForProcess(TrackingProcess, Pointer, NumPages << PAGE_SIZE_MUL);
        return NULL;
    }

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

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
BOOL ResizeRegionForProcess(
    LPPROCESS TrackingProcess, LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    if (Base == 0) {
        ERROR(TEXT("Base cannot be null"));
        return FALSE;
    }

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("New size %x exceeds 25%% of memory (%x)"), NewSize, KernelStartup.MemorySize / 4);
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
        LINEAR NewBase = Base + (CurrentPages << PAGE_SIZE_MUL);
        UINT AdditionalSize = AdditionalPages << PAGE_SIZE_MUL;

        if (IsRegionFree(NewBase, AdditionalSize) == FALSE) {
            DEBUG(TEXT("Additional region not free at %x"), NewBase);
            return FALSE;
        }

        PHYSICAL AdditionalTarget = 0;
        if (Target != 0) {
            AdditionalTarget = Target + (CurrentPages << PAGE_SIZE_MUL);
        }

        if (PopulateRegionPages(NewBase, AdditionalTarget, AdditionalPages, Flags, NewBase, TEXT("ResizeRegion")) ==
            FALSE) {
            return FALSE;
        }

        RegionTrackResizeForProcess(TrackingProcess, Base, Size, NewSize, Flags);

        FlushTLB();
    } else {
        UINT PagesToRelease = CurrentPages - RequestedPages;
        if (PagesToRelease != 0) {
            LINEAR ReleaseBase = Base + (RequestedPages << PAGE_SIZE_MUL);
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

static void RollbackCommittedRange(LINEAR Base, UINT NumPages) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = GetDirectoryEntry(Base);
        UINT TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            LPPAGE_TABLE Table = GetPageTableVAFor(Base);

            if (Table[TabEntry].Present != 0) {
                if (Table[TabEntry].Fixed == 0 && Table[TabEntry].Address != NULL) {
                    SetPhysicalPageMark(Table[TabEntry].Address, 0);
                }

                Table[TabEntry].Present = 0;
                Table[TabEntry].Address = NULL;
                Table[TabEntry].Fixed = 0;
            }
        }

        Base += PAGE_SIZE;
    }

    FreeEmptyPageTables();
    FlushTLB();
}

/************************************************************************/

BOOL CommitRegionRangeForProcess(LPPROCESS TrackingProcess, LINEAR Base, UINT Size, U32 Flags) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT NumPages;
    UINT ReadWrite;
    UINT PteCacheDisabled;
    UINT PteWriteThrough;
    UINT Fixed;
    LINEAR StartBase = Base;

    if (Base == 0 || Size == 0) {
        return FALSE;
    }

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (NumPages == 0) {
        NumPages = 1;
    }

    ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;
    Fixed = (Flags & (ALLOC_PAGES_IO | ALLOC_PAGES_FIXED)) ? 1 : 0;

    if (PteCacheDisabled) {
        PteWriteThrough = 0;
    }

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = GetDirectoryEntry(Base);
        UINT TabEntry = GetTableEntry(Base);
        LPPAGE_TABLE Table;
        PHYSICAL Physical;

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                RollbackCommittedRange(StartBase, Index);
                return FALSE;
            }
        }

        Table = GetPageTableVAFor(Base);
        if (Table[TabEntry].Present != 0) {
            RollbackCommittedRange(StartBase, Index);
            return FALSE;
        }

        Physical = AllocPhysicalPage();
        if (Physical == NULL) {
            RollbackCommittedRange(StartBase, Index);
            return FALSE;
        }

        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = PAGE_PRIVILEGE(Base);
        Table[TabEntry].WriteThrough = PteWriteThrough;
        Table[TabEntry].CacheDisabled = PteCacheDisabled;
        Table[TabEntry].Accessed = 0;
        Table[TabEntry].Dirty = 0;
        Table[TabEntry].Reserved = 0;
        Table[TabEntry].Global = 0;
        Table[TabEntry].User = 0;
        Table[TabEntry].Fixed = Fixed;
        Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
        Table[TabEntry].Present = 1;

        if (SyncKernelMappingForPage(Base, *(volatile U32*)&Directory[DirEntry], *(volatile U32*)&Table[TabEntry]) ==
            FALSE) {
            RollbackCommittedRange(StartBase, Index + 1);
            ERROR(TEXT("Kernel mapping synchronization failed for %p"), (LPVOID)Base);
            return FALSE;
        }

        Base += PAGE_SIZE;
    }

    if (RegionTrackCommitForProcess(TrackingProcess, StartBase, NumPages << PAGE_SIZE_MUL) == FALSE) {
        RollbackCommittedRange(StartBase, NumPages);
        return FALSE;
    }

    FlushTLB();
    return TRUE;
}

/************************************************************************/

BOOL CommitRegionRange(LINEAR Base, UINT Size, U32 Flags) {
    return CommitRegionRangeForProcess(NULL, Base, Size, Flags);
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
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)GetCurrentPageDirectoryVA();
    LPPAGE_TABLE Table = NULL;
    UINT DirEntry = 0;
    UINT TabEntry = 0;
    UINT NumPages = 0;
    UINT Index = 0;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL; /* ceil(Size / 4096) */
    if (NumPages == 0) NumPages = 1;

    // Free each page in turn.
    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = GetPageTableVAFor(Base);

            if (Table[TabEntry].Address != NULL || Table[TabEntry].Present != 0) {
                /* Skip allocator release if it was an IO mapping (BAR) */
                if (Table[TabEntry].Present != 0 && Table[TabEntry].Fixed == 0) {
                    SetPhysicalPageMark(Table[TabEntry].Address, 0);
                }

                Table[TabEntry].Present = 0;
                Table[TabEntry].Address = NULL;
                Table[TabEntry].Fixed = 0;
            }
        }

        Base += PAGE_SIZE;
    }

    RegionTrackFreeForProcess(TrackingProcess, OriginalBase, NumPages << PAGE_SIZE_MUL);

    FreeEmptyPageTables();

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    return TRUE;
}

/************************************************************************/

BOOL FreeRegion(LINEAR Base, UINT Size) { return FreeRegionForProcess(NULL, Base, Size); }

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
    PHYSICAL PageOffset = PhysicalBase & (PAGE_SIZE - 1);
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL,           // Start search in kernel space to avoid user space
        AlignedPhysicalBase,  // Page-aligned PMA
        AdjustedSize,         // Page-aligned size
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO |
            ALLOC_PAGES_AT_OR_OVER,  // Do not touch RAM allocator state; mark PTE.Fixed; search at or over VMA_KERNEL
        TEXT("IOMemory"));

    if (AlignedResult == NULL) {
        return NULL;
    }

    // Return the address adjusted for the original offset
    LINEAR result = AlignedResult + PageOffset;
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
        ERROR(TEXT("Invalid parameters (PA=%p Size=%u)"), (LPVOID)(LINEAR)PhysicalBase, Size);
        return NULL;
    }

    PHYSICAL PageOffset = PhysicalBase & (PAGE_SIZE - 1);
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL, AlignedPhysicalBase, AdjustedSize,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_WC | ALLOC_PAGES_IO | ALLOC_PAGES_AT_OR_OVER,
        TEXT("Framebuffer"));

    if (AlignedResult == NULL) {
        WARNING(TEXT("WC mapping failed, falling back to UC"));
        return MapIOMemory(PhysicalBase, Size);
    }

    LINEAR result = AlignedResult + PageOffset;
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
        ERROR(TEXT("Invalid parameters (LA=%p Size=%u)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip allocator page release if PTE.Fixed was set
    return FreeRegion(LinearBase, Size);
}

/************************************************************************/

/**
 * @brief Compute a preferred base address for the kernel heap.
 * @param HeapSize Requested heap size in bytes.
 * @return Preferred linear base address in kernel space.
 */
LINEAR GetKernelHeapPreferredBase(UINT HeapSize) {
    UNUSED(HeapSize);

    UINT MaxLinearAddress = MAX_U32;
    UINT Midpoint = VMA_KERNEL + ((MaxLinearAddress - VMA_KERNEL) / 2);

    return Midpoint & PAGE_MASK;
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
    // Always use VMA_KERNEL base and add AT_OR_OVER flag
    return AllocRegion(VMA_KERNEL, Target, Size, Flags | ALLOC_PAGES_AT_OR_OVER, Tag);
}

/************************************************************************/

LINEAR ResizeKernelRegion(LINEAR Base, UINT Size, UINT NewSize, U32 Flags) {
    return ResizeRegion(Base, 0, Size, NewSize, Flags | ALLOC_PAGES_AT_OR_OVER);
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
 * @brief Initializes the x86-32 memory manager structures.
 *
 * This routine prepares the physical buddy allocator, builds and loads the initial
 * page directory, and initializes segmentation through the GDT. It must be
 * called during early kernel initialization.
 */
void InitializeMemoryManager(void) {
    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BuddyMetadataSize = BuddyGetMetadataSize(KernelStartup.PageCount);
    UINT BuddyMetadataSizeAligned = (UINT)PAGE_ALIGN(BuddyMetadataSize);

    UINT ReservedBytes = KernelStartup.KernelReservedBytes;
    if (ReservedBytes < KernelStartup.KernelSize) {
        ERROR(TEXT("Invalid kernel reserved span (reserved=%u size=%u)"), ReservedBytes, KernelStartup.KernelSize);
        ConsolePanic(TEXT("Invalid boot kernel reserved span"));
        DO_THE_SLEEPING_BEAUTY;
    }

    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + (PHYSICAL)PAGE_ALIGN((PHYSICAL)ReservedBytes);
    PHYSICAL BuddyMetadataPhysical = 0;

    SetLoaderReservedPhysicalRange(KernelStartup.KernelPhysicalBase, LoaderReservedEnd);
    if (FindAvailableMemoryRangeInWindow(
            (PHYSICAL)N_1MB, (PHYSICAL)RESERVED_LOW_MEMORY, KernelStartup.KernelPhysicalBase, LoaderReservedEnd,
            BuddyMetadataSizeAligned, &BuddyMetadataPhysical) == FALSE) {
        ERROR(TEXT("Could not place buddy metadata (size=%u)"), BuddyMetadataSizeAligned);
        ConsolePanic(TEXT("Could not place physical memory allocator metadata"));
        DO_THE_SLEEPING_BEAUTY;
    }

    SetPhysicalAllocatorMetadataRange(BuddyMetadataPhysical, BuddyMetadataPhysical + BuddyMetadataSizeAligned);

    if (BuddyInitialize((LINEAR)BuddyMetadataPhysical, BuddyMetadataSizeAligned, KernelStartup.PageCount) == FALSE) {
        ERROR(
            TEXT("BuddyInitialize failed (PA=%p size=%u pages=%u)"), (LPVOID)(LINEAR)BuddyMetadataPhysical,
            BuddyMetadataSizeAligned, KernelStartup.PageCount);
        ConsolePanic(TEXT("Could not initialize physical memory allocator"));
        DO_THE_SLEEPING_BEAUTY;
    }

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    LogPageDirectory(NewPageDirectory);

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    ConsoleInvalidateFramebufferMapping();

    FlushTLB();

    InitializeRegionDescriptorTracking();

    Kernel_x86_32.GDT =
        (LPSEGMENT_DESCRIPTOR)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("GDT"));

    if (Kernel_x86_32.GDT == NULL) {
        ERROR(TEXT("AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable(Kernel_x86_32.GDT);

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_x86_32.GDT, GDT_SIZE - 1);

    LogGlobalDescriptorTable(Kernel_x86_32.GDT, 10);
}
