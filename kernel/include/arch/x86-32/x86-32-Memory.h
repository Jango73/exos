
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


    x86-32 memory-specific definitions

\************************************************************************/

#ifndef X86_32_MEMORY_H_INCLUDED
#define X86_32_MEMORY_H_INCLUDED

#include "Base.h"

/************************************************************************/
// #defines

#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK (PAGE_SIZE - 1)

#define PAGE_TABLE_SIZE N_4KB
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_SIZE_MASK (PAGE_TABLE_SIZE - 1)

#define PAGE_TABLE_ENTRY_SIZE (sizeof(U32))
#define PAGE_TABLE_NUM_ENTRIES (PAGE_TABLE_SIZE / PAGE_TABLE_ENTRY_SIZE)

#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_4MB
#define PAGE_TABLE_CAPACITY_MASK (PAGE_TABLE_CAPACITY - 1)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_PRIVILEGE_KERNEL 0
#define PAGE_PRIVILEGE_USER 1

#define PAGE_ALIGN(a) (((a) + PAGE_SIZE - 1) & PAGE_MASK)

#define VMA_RAM 0x00000000                         // Reserved for kernel
#define VMA_VIDEO 0x000A0000                       // Reserved for kernel
#define VMA_CONSOLE 0x000B8000                     // Reserved for kernel
#define VMA_USER 0x00400000                        // Start of user address space
#define VMA_USER_LIMIT 0xA0000000                    // Upper user arena anchor
#define VMA_TASK_RUNNER (VMA_USER_LIMIT - PAGE_SIZE)  // User alias for TaskRunner

#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

#if defined(__KERNEL__) && (CONFIG_VMA_KERNEL) > 0xFFFFFFFFu
#error "CONFIG_VMA_KERNEL does not fit in 32 bits"
#endif

#define VMA_KERNEL (CONFIG_VMA_KERNEL)

#define PAGE_PRIVILEGE(adr) ((adr >= VMA_USER && adr < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

#define PD_RECURSIVE_SLOT 1023u         // PDE index used for self-map
#define PD_VA ((LINEAR)0xFFFFF000)      // Page Directory linear alias
#define PT_BASE_VA ((LINEAR)0xFFC00000) // Page Tables linear window

#define PAGE_FLAG_PRESENT (1u << 0)
#define PAGE_FLAG_READ_WRITE (1u << 1)
#define PAGE_FLAG_USER (1u << 2)
#define PAGE_FLAG_WRITE_THROUGH (1u << 3)
#define PAGE_FLAG_CACHE_DISABLED (1u << 4)
#define PAGE_FLAG_ACCESSED (1u << 5)
#define PAGE_FLAG_DIRTY (1u << 6)
#define PAGE_FLAG_PAGE_SIZE (1u << 7)
#define PAGE_FLAG_GLOBAL (1u << 8)
#define PAGE_FLAG_FIXED (1u << 9)

/************************************************************************/
// typedefs

typedef struct tag_PAGE_DIRECTORY {
    U32 Present : 1;    // Is page present in RAM?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed?
    U32 Reserved : 1;
    U32 PageSize : 1;  // 0 = 4KB
    U32 Global : 1;    // Ignored
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS: Can page be swapped?
    U32 Address : 20;  // Physical address
} PAGE_DIRECTORY, *LPPAGE_DIRECTORY;

typedef struct tag_PAGE_TABLE {
    U32 Present : 1;    // Is page present in RAM?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed?
    U32 Dirty : 1;     // Has been written to?
    U32 Reserved : 1;  // Reserved by Intel
    U32 Global : 1;
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS: Can page be swapped?
    U32 Address : 20;  // Physical address
} PAGE_TABLE, *LPPAGE_TABLE;

typedef struct tag_ARCH_PAGE_ITERATOR {
    LINEAR Linear;
    UINT DirectoryIndex;
    UINT TableIndex;
} ARCH_PAGE_ITERATOR;

/************************************************************************/

static inline U64 GetMaxLinearAddressPlusOne(void) {
    return U64_Make(1, 0x00000000u);
}

static inline U64 GetMaxPhysicalAddressPlusOne(void) {
    return U64_Make(1, 0x00000000u);
}

static inline LINEAR CanonicalizeLinearAddress(LINEAR Address) {
    return Address;
}

static inline BOOL ClipPhysicalRange(U64 Base, U64 Length, PHYSICAL* OutBase, UINT* OutLength) {
    U64 Limit = GetMaxPhysicalAddressPlusOne();

    if ((Length.HI == 0 && Length.LO == 0) || OutBase == NULL || OutLength == NULL) return FALSE;
    if (U64_Cmp(Base, Limit) >= 0) return FALSE;

    U64 End = U64_Add(Base, Length);
    if (U64_Cmp(End, Limit) > 0) End = Limit;

    U64 NewLength = U64_Sub(End, Base);

    *OutBase = Base.LO;
    if (NewLength.HI != 0) {
        *OutLength = MAX_U32 - Base.LO;
    } else {
        *OutLength = NewLength.LO;
    }

    return (*OutLength != 0);
}

#endif  // X86_32_MEMORY_H_INCLUDED
