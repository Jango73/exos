
/************************************************************************\

    EXOS Kernel
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


    x86-64 memory utilities

\************************************************************************/


#include "arch/x86-64/x86-64-Memory-Internal.h"
#include "process/Process.h"

/************************************************************************/
// Temporary mapping slots state

extern LINEAR __bss_init_end;

static BOOL G_TempLinearInitialized = FALSE;
static LINEAR G_TempLinear1 = 0;
static LINEAR G_TempLinear2 = 0;
static LINEAR G_TempLinear3 = 0;
static LINEAR G_TempLinear4 = 0;
static LINEAR G_TempLinear5 = 0;
static LINEAR G_TempLinear6 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical1 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical2 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical3 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical4 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical5 = 0;
static PHYSICAL DATA_SECTION G_TempPhysical6 = 0;

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
    G_TempLinear4 = G_TempLinear3 + PAGE_SIZE;
    G_TempLinear5 = G_TempLinear4 + PAGE_SIZE;
    G_TempLinear6 = G_TempLinear5 + PAGE_SIZE;
    G_TempLinearInitialized = TRUE;
}

/************************************************************************/
/**
 * @brief Build a page table entry with the supplied access flags.
 * @param Physical Physical base of the page.
 * @param ReadWrite 1 when the mapping permits writes.
 * @param Privilege Privilege level (kernel/user).
 * @param WriteThrough 1 to enable write-through caching.
 * @param CacheDisabled 1 to disable CPU caches.
 * @param Global 1 when the mapping is global.
 * @param Fixed 1 when the entry must survive reclamation.
 * @return Encoded 64-bit PTE value.
 */
U64 MakePageTableEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U64 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    return ((U64)Physical & PAGE_MASK) | Flags;
}

/************************************************************************/
/**
 * @brief Build a raw paging entry value without recomputing the flags.
 * @param Physical Physical base of the page.
 * @param Flags Pre-built flag mask.
 * @return Encoded paging entry value.
 */
U64 MakePageEntryRaw(PHYSICAL Physical, U64 Flags) {
    return ((U64)Physical & PAGE_MASK) | (Flags & 0xFFFu);
}

/************************************************************************/
/**
 * @brief Store a value inside a page-directory level entry.
 * @param Directory Directory pointer.
 * @param Index Entry index within the directory.
 * @param Value Encoded PDE value.
 */
void WritePageDirectoryEntryValue(LPPAGE_DIRECTORY Directory, UINT Index, U64 Value) {
    ((volatile U64*)Directory)[Index] = Value;
}

/************************************************************************/
/**
 * @brief Store a value inside a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index within the table.
 * @param Value Encoded PTE value.
 */
void WritePageTableEntryValue(LPPAGE_TABLE Table, UINT Index, U64 Value) {
    ((volatile U64*)Table)[Index] = Value;
}

/************************************************************************/
/**
 * @brief Read a value from a page-directory level entry.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return Encoded PDE value.
 */
U64 ReadPageDirectoryEntryValue(const LPPAGE_DIRECTORY Directory, UINT Index) {
    if (Directory == NULL) {
        ERROR(TEXT("[ReadPageDirectoryEntryValue] NULL directory pointer (Index=%u)"),
            Index);
        return 0;
    }

    return ((volatile const U64*)Directory)[Index];
}

/************************************************************************/
/**
 * @brief Read a value from a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return Encoded PTE value.
 */
U64 ReadPageTableEntryValue(const LPPAGE_TABLE Table, UINT Index) {
    return ((volatile const U64*)Table)[Index];
}

/************************************************************************/
/**
 * @brief Test whether a page-directory entry is marked present.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is present.
 */
BOOL PageDirectoryEntryIsPresent(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (ReadPageDirectoryEntryValue(Directory, Index) & PAGE_FLAG_PRESENT) != 0;
}

/************************************************************************/
/**
 * @brief Test whether a page-table entry is marked present.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is present.
 */
BOOL PageTableEntryIsPresent(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_PRESENT) != 0;
}

/************************************************************************/
/**
 * @brief Extract the physical address encoded in a PDE.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return Physical base address.
 */
PHYSICAL PageDirectoryEntryGetPhysical(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (PHYSICAL)(ReadPageDirectoryEntryValue(Directory, Index) & PAGE_MASK);
}

/************************************************************************/
/**
 * @brief Extract the physical address encoded in a PTE.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return Physical base address.
 */
PHYSICAL PageTableEntryGetPhysical(const LPPAGE_TABLE Table, UINT Index) {
    return (PHYSICAL)(ReadPageTableEntryValue(Table, Index) & PAGE_MASK);
}

/************************************************************************/
/**
 * @brief Test whether a page-table entry is marked fixed.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is fixed.
 */
BOOL PageTableEntryIsFixed(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_FIXED) != 0;
}

/************************************************************************/
/**
 * @brief Clear a page-directory entry.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 */
void ClearPageDirectoryEntry(LPPAGE_DIRECTORY Directory, UINT Index) {
    WritePageDirectoryEntryValue(Directory, Index, (U64)0);
}

/************************************************************************/
/**
 * @brief Clear a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index.
 */
void ClearPageTableEntry(LPPAGE_TABLE Table, UINT Index) {
    WritePageTableEntryValue(Table, Index, (U64)0);
}

/************************************************************************/
/**
 * @brief Return the first non-canonical linear address.
 * @return Maximum linear address plus one.
 */
U64 GetMaxLinearAddressPlusOne(void) {
    return (U64)1 << 48;
}

/************************************************************************/
/**
 * @brief Return the first non-addressable physical address.
 * @return Maximum physical address plus one.
 */
U64 GetMaxPhysicalAddressPlusOne(void) {
    return (U64)1 << 52;
}

/************************************************************************/
/**
 * @brief Compute the number of 4 KiB pages required to reach an alignment.
 * @param Base Canonical base.
 * @param SpanSize Alignment span expressed in bytes.
 * @return Number of pages until alignment (0 when already aligned).
 */
UINT ComputePagesUntilAlignment(LINEAR Base, U64 SpanSize) {
    if (SpanSize == 0u) {
        return 0u;
    }

    U64 Mask = SpanSize - (U64)1;
    U64 Offset = (U64)Base & Mask;

    if (Offset == 0u) {
        return 0u;
    }

    U64 RemainingBytes = SpanSize - Offset;
    return (UINT)(RemainingBytes >> PAGE_SIZE_MUL);
}

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT dir = GetDirectoryEntry(Linear);

    if (!PageDirectoryEntryIsPresent(Directory, dir)) {
        ConsolePanic(TEXT("[MapOnePage] PDE not present for VA %p (dir=%d)"), Linear, dir);
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);

    WritePageTableEntryValue(
        Table, tab, MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed));

    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);
    ClearPageTableEntry(Table, tab);
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

    G_TempPhysical1 = Physical;

    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

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

    G_TempPhysical2 = Physical;

    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

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

    G_TempPhysical3 = Physical;

    MapOnePage(
        G_TempLinear3, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

    return G_TempLinear3;
}

/************************************************************************/
// Public temporary map #4

/**
 * @brief Map a physical page to the fourth temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage4(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear4 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage4] Temp slot #4 not reserved"));
        return NULL;
    }

    G_TempPhysical4 = Physical;

    MapOnePage(
        G_TempLinear4, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    FlushTLB();

    return G_TempLinear4;
}

/************************************************************************/
// Public temporary map #5

/**
 * @brief Map a physical page to the fifth temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage5(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear5 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage5] Temp slot #5 not reserved"));
        return NULL;
    }

    G_TempPhysical5 = Physical;

    MapOnePage(
        G_TempLinear5, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    FlushTLB();

    return G_TempLinear5;
}

/************************************************************************/
// Public temporary map #6

/**
 * @brief Map a physical page to the sixth temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage6(PHYSICAL Physical) {
    InitializeTemporaryLinearSlots();

    if (G_TempLinear6 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage6] Temp slot #6 not reserved"));
        return NULL;
    }

    G_TempPhysical6 = Physical;

    MapOnePage(
        G_TempLinear6, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    FlushTLB();

    return G_TempLinear6;
}

/************************************************************************/

/**
 * @brief Allocate and link a page table for the provided linear address.
 *
 * The helper walks the paging hierarchy, checks that upper levels are present,
 * allocates a new table and installs it in the page directory.
 *
 * @param Base Linear address whose table should be allocated.
 * @return Canonical virtual address of the mapped table, or NULL on failure.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PMA_Table;

    Base = CanonicalizeLinearAddress(Base);

    UINT DirEntry = GetDirectoryEntry(Base);
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);

    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) {
        ERROR(TEXT("[AllocPageTable] Missing PML4 entry for base=%p"), Base);
        return NULL;
    }

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);
    PHYSICAL DirectoryPhysical;

    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) {
        DirectoryPhysical = AllocPhysicalPage();

        if (DirectoryPhysical == NULL) {
            ERROR(TEXT("[AllocPageTable] Out of physical pages for directory"));
            return NULL;
        }

        LPPAGE_DIRECTORY NewDirectory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
        if (NewDirectory == NULL) {
            FreePhysicalPage(DirectoryPhysical);
            ERROR(TEXT("[AllocPageTable] Failed to map new directory"));
            return NULL;
        }

        MemorySet(NewDirectory, 0, PAGE_SIZE);

        WritePageDirectoryEntryValue(
            PdptLinear,
            PdptIndex,
            MakePageDirectoryEntryValue(
                DirectoryPhysical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE(Base),
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    } else {
        if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
            return NULL;
        }

        DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    }

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 ExistingDirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirEntry);

    if ((ExistingDirectoryEntryValue & PAGE_FLAG_PRESENT) != 0) {
        return (LINEAR)GetPageTableVAFor(Base);
    }

    PMA_Table = AllocPhysicalPage();

    if (PMA_Table == NULL) {
        ERROR(TEXT("[AllocPageTable] Out of physical pages"));
        return NULL;
    }

    U32 Privilege = PAGE_PRIVILEGE(Base);
    U64 DirectoryEntryValue = MakePageDirectoryEntryValue(
        PMA_Table,
        /*ReadWrite*/ 1,
        Privilege,
        /*WriteThrough*/ 0,
        /*CacheDisabled*/ 0,
        /*Global*/ 0,
        /*Fixed*/ 1);

    WritePageDirectoryEntryValue(Directory, DirEntry, DirectoryEntryValue);

    LINEAR VMA_PT = MapTemporaryPhysicalPage3(PMA_Table);
    if (VMA_PT == NULL) {
        ClearPageDirectoryEntry(Directory, DirEntry);
        FreePhysicalPage(PMA_Table);
        ERROR(TEXT("[AllocPageTable] Failed to map new page table"));
        return NULL;
    }

    MemorySet((LPVOID)VMA_PT, 0, PAGE_SIZE);

    FlushTLB();

    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/

/**
 * @brief Retrieve the page table referenced by an iterator when present.
 *
 * The iterator supplies the paging indexes and the function verifies the
 * presence of intermediate levels. Large pages are reported through
 * @p OutLargePage when requested.
 *
 * @param Iterator Pointer describing the directory entry to inspect.
 * @param OutTable Receives the resulting page table pointer when available.
 * @param OutLargePage Optionally receives TRUE when the entry maps a large page.
 * @return TRUE if a table is available, FALSE otherwise.
 */
BOOL TryGetPageTableForIterator(
    const ARCH_PAGE_ITERATOR* Iterator,
    LPPAGE_TABLE* OutTable,
    BOOL* OutLargePage) {
    if (Iterator == NULL || OutTable == NULL) return FALSE;

    if (OutLargePage != NULL) {
        *OutLargePage = FALSE;
    }

    LINEAR Linear = (LINEAR)MemoryPageIteratorGetLinear(Iterator);
    UNUSED(Linear);

    UINT Pml4Index = MemoryPageIteratorGetPml4Index(Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(Iterator);
    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);

    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);

    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        if (OutLargePage != NULL) {
            *OutLargePage = TRUE;
        }
        return FALSE;
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirEntry);

    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        if (OutLargePage != NULL) {
            *OutLargePage = TRUE;
        }
        return FALSE;
    }

    *OutTable = MemoryPageIteratorGetTable(Iterator);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve a canonical linear address to its physical counterpart.
 *
 * The lookup walks the paging hierarchy, accounting for large pages, and
 * returns the physical address when the mapping exists.
 *
 * @param Address Linear address to translate.
 * @return Physical address of the resolved page, or 0 when unmapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    Address = CanonicalizeLinearAddress(Address);

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);
    UINT DirIndex = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabIndex = MemoryPageIteratorGetTableIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);
    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_1GB - 1)));
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY DirectoryLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(DirectoryLinear, DirIndex);
    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_2MB - 1)));
    }

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabIndex)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabIndex);
    if (PagePhysical == 0) return 0;

    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Address Linear address to test.
 * @return TRUE if the address resolves to a present page table entry.
 */
BOOL IsValidMemory(LINEAR Address) {
    LINEAR Canonical = CanonicalizeLinearAddress(Address);

    if (Canonical != Address) {
        return FALSE;
    }

    return (BOOL)MapLinearToPhysical(Canonical) != 0;
}

/************************************************************************/

/**
 * @brief Attempt to mirror kernel mappings into the current address space for a fault.
 * @param FaultAddress Linear address that triggered the fault.
 * @return TRUE when the mapping was recreated and the fault can be retried.
 */
BOOL ResolveKernelPageFault(LINEAR FaultAddress) {
    U64 Address = (U64)FaultAddress;
    U64 Canonical = CanonicalizeLinearAddress(Address);

    if (Canonical != Address) {
        DEBUG(TEXT("[ResolveKernelPageFault] Non-canonical address %p"), (LPVOID)Address);
        return FALSE;
    }

    if (Address < (U64)VMA_KERNEL) {
        DEBUG(TEXT("[ResolveKernelPageFault] Address %p below kernel VMA"), (LPVOID)Address);
        return FALSE;
    }

    PHYSICAL KernelDirectoryPhysical = KernelProcess.PageDirectory;
    if (KernelDirectoryPhysical == 0) {
        KernelDirectoryPhysical = KernelStartup.PageDirectory;
    }

    if (KernelDirectoryPhysical == 0) {
        DEBUG(TEXT("[ResolveKernelPageFault] No kernel directory available (Address=%p)"), (LPVOID)Address);
        return FALSE;
    }

    PHYSICAL CurrentDirectoryPhysical = GetPageDirectory();
    if (CurrentDirectoryPhysical == 0 || CurrentDirectoryPhysical == KernelDirectoryPhysical) {
        return FALSE;
    }

    UINT Pml4Index = GetPml4Entry(Address);
    UINT PdptIndex = GetPdptEntry(Address);
    UINT DirectoryIndex = GetDirectoryEntry(Address);
    UINT TableIndex = GetTableEntry(Address);

    LINEAR KernelPml4Linear = MapTemporaryPhysicalPage1(KernelDirectoryPhysical);
    if (KernelPml4Linear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel PML4"));
        return FALSE;
    }

    LPPML4 KernelPml4 = (LPPML4)KernelPml4Linear;
    U64 KernelPml4Value = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)KernelPml4, Pml4Index);
    if ((KernelPml4Value & PAGE_FLAG_PRESENT) == 0u) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel PML4[%u] not present (Address=%p)"),
              Pml4Index,
              (LPVOID)Address);
        return FALSE;
    }

    BOOL Updated = FALSE;
    BOOL NeedsFullFlush = FALSE;

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    U64 CurrentPml4Value = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)CurrentPml4, Pml4Index);
    if ((CurrentPml4Value & PAGE_FLAG_PRESENT) == 0u || CurrentPml4Value != KernelPml4Value) {
        WritePageDirectoryEntryValue((LPPAGE_DIRECTORY)CurrentPml4, Pml4Index, KernelPml4Value);
        Updated = TRUE;
        NeedsFullFlush = TRUE;
    }

    PHYSICAL KernelPdptPhysical = (PHYSICAL)(KernelPml4Value & PAGE_MASK);
    LINEAR KernelPdptLinear = MapTemporaryPhysicalPage2(KernelPdptPhysical);
    if (KernelPdptLinear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel PDPT"));
        return FALSE;
    }

    LPPDPT KernelPdpt = (LPPDPT)KernelPdptLinear;
    U64 KernelPdptValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)KernelPdpt, PdptIndex);
    if ((KernelPdptValue & PAGE_FLAG_PRESENT) == 0u) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel PDPT[%u] not present (Address=%p)"),
              PdptIndex,
              (LPVOID)Address);
        return FALSE;
    }

    LPPDPT CurrentPdpt = GetPageDirectoryPointerTableVAFor(Address);
    U64 CurrentPdptValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)CurrentPdpt, PdptIndex);
    if ((CurrentPdptValue & PAGE_FLAG_PRESENT) == 0u || CurrentPdptValue != KernelPdptValue) {
        WritePageDirectoryEntryValue((LPPAGE_DIRECTORY)CurrentPdpt, PdptIndex, KernelPdptValue);
        Updated = TRUE;
        NeedsFullFlush = TRUE;
    }

    if ((KernelPdptValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
        if (Updated == FALSE) {
            return FALSE;
        }

        if (NeedsFullFlush) {
            FlushTLB();
        } else {
            InvalidatePage((LINEAR)Address);
        }

        return TRUE;
    }

    KernelDirectoryPhysical = (PHYSICAL)(KernelPdptValue & PAGE_MASK);
    LINEAR KernelDirectoryLinear = MapTemporaryPhysicalPage3(KernelDirectoryPhysical);
    if (KernelDirectoryLinear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel page directory"));
        return FALSE;
    }

    LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)KernelDirectoryLinear;
    U64 KernelDirectoryValue = ReadPageDirectoryEntryValue(KernelDirectory, DirectoryIndex);
    if ((KernelDirectoryValue & PAGE_FLAG_PRESENT) == 0u) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel directory[%u] not present (Address=%p)"),
              DirectoryIndex,
              (LPVOID)Address);
        return FALSE;
    }

    LPPAGE_DIRECTORY CurrentDirectory = GetPageDirectoryVAFor(Address);
    U64 CurrentDirectoryValue = ReadPageDirectoryEntryValue(CurrentDirectory, DirectoryIndex);
    if ((CurrentDirectoryValue & PAGE_FLAG_PRESENT) == 0u || CurrentDirectoryValue != KernelDirectoryValue) {
        WritePageDirectoryEntryValue(CurrentDirectory, DirectoryIndex, KernelDirectoryValue);
        Updated = TRUE;
        NeedsFullFlush = TRUE;
    }

    if ((KernelDirectoryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
        if (Updated == FALSE) {
            return FALSE;
        }

        if (NeedsFullFlush) {
            FlushTLB();
        } else {
            InvalidatePage((LINEAR)Address);
        }

        return TRUE;
    }

    PHYSICAL KernelTablePhysical = (PHYSICAL)(KernelDirectoryValue & PAGE_MASK);
    LINEAR KernelTableLinear = MapTemporaryPhysicalPage2(KernelTablePhysical);
    if (KernelTableLinear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel page table"));
        return FALSE;
    }

    LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)KernelTableLinear;
    U64 KernelTableValue = ReadPageTableEntryValue(KernelTable, TableIndex);
    if ((KernelTableValue & PAGE_FLAG_PRESENT) == 0u) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel PTE[%u] not present (Address=%p)"),
              TableIndex,
              (LPVOID)Address);
        return FALSE;
    }

    LPPAGE_TABLE CurrentTable = GetPageTableVAFor(Address);
    U64 CurrentTableValue = ReadPageTableEntryValue(CurrentTable, TableIndex);
    if (CurrentTableValue != KernelTableValue) {
        WritePageTableEntryValue(CurrentTable, TableIndex, KernelTableValue);
        Updated = TRUE;
    }

    if (Updated == FALSE) {
        return FALSE;
    }

    if (NeedsFullFlush) {
        FlushTLB();
    } else {
        InvalidatePage((LINEAR)Address);
    }

    DEBUG(TEXT("[ResolveKernelPageFault] Mirrored kernel 4KB mapping for %p"), (LPVOID)Address);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Check if a linear region is free of mappings.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE if region is free.
 */
BOOL IsRegionFree(LINEAR Base, UINT Size) {
    Base = CanonicalizeLinearAddress(Base);

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT i = 0; i < NumPages; i++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        LPPAGE_TABLE Table = NULL;
        BOOL IsLargePage = FALSE;
        BOOL TableAvailable = TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage);

        if (TableAvailable) {
            if (PageTableEntryIsPresent(Table, TabEntry)) {
                return FALSE;
            }
        } else {
            if (IsLargePage) {
                return FALSE;
            }
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Validate that a physical range remains intact after clipping.
 *
 * The routine checks whether the provided base and length in pages survive
 * the ClipPhysicalRange() constraints without alteration.
 *
 * @param Base Physical base page frame to validate.
 * @param NumPages Number of pages requested in the range.
 * @return TRUE when the range is valid or degenerate, FALSE otherwise.
 */
BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ClipPhysicalRange((U64)Base, (U64)RequestedLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
}
