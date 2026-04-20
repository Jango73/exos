
;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   Copyright (c) 1999-2025 Jango73
;
;   This program is free software: you can redistribute it and/or modify
;   it under the terms of the GNU General Public License as published by
;   the Free Software Foundation, either version 3 of the License, or
;   (at your option) any later version.
;
;   This program is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;   GNU General Public License for more details.
;
;   You should have received a copy of the GNU General Public License
;   along with this program.  If not, see <https://www.gnu.org/licenses/>.
;
;
;   x86-32-specific kernel definitions shared with assembly sources
;
;-------------------------------------------------------------------------

%ifndef KERNEL_X86_32_INC
%define KERNEL_X86_32_INC

%define KERNEL_ARCH_MAX_UINT 0xFFFFFFFF

%include "../../common/Kernel.inc"
%include "../../common/Cpu.inc"

;-------------------------------------------------------------------------
; Page descriptor helpers

PAGE_MASK          equ 0xFFFFF000
PAGE_TABLE_SIZE    equ N_4KB

PAGE_BIT_PRESENT   equ 0x00000001
PAGE_BIT_RDWR      equ 0x00000002
PAGE_BIT_PRIVILEGE equ 0x00000004
PAGE_BIT_THROUGH   equ 0x00000008
PAGE_BIT_NOCACHE   equ 0x00000010
PAGE_BIT_ACCESSED  equ 0x00000020
PAGE_BIT_DIRTY     equ 0x00000040
PAGE_BIT_RESERVED  equ 0x00000080
PAGE_BIT_GLOBAL    equ 0x00000100
PAGE_BIT_USER1     equ 0x00000200
PAGE_BIT_USER2     equ 0x00000400
PAGE_BIT_FIXED     equ 0x00000800

PAGE_BIT_SYSTEM    equ (PAGE_BIT_PRESENT | PAGE_BIT_RDWR | PAGE_BIT_FIXED)

PAGE_SIZE_MUL      equ MUL_4KB

;-------------------------------------------------------------------------
; Physical and virtual memory addresses

IDT_SIZE     equ N_4KB
GDT_SIZE     equ N_8KB

VMA_USER      equ 0x00400000
VMA_USER_LIMIT equ 0xA0000000
%ifndef CONFIG_VMA_KERNEL
    %error "CONFIG_VMA_KERNEL is not defined"
%endif

VMA_KERNEL    equ CONFIG_VMA_KERNEL

;-------------------------------------------------------------------------
; Values related to IDT, GDT, LDT

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

NUM_INTS                equ 256

;-------------------------------------------------------------------------
; Privilege levels and selectors

CPU_PRIVILEGE_KERNEL   equ 0x00
CPU_PRIVILEGE_DRIVERS  equ 0x01
CPU_PRIVILEGE_ROUTINES equ 0x02
CPU_PRIVILEGE_USER     equ 0x03

SELECTOR_GLOBAL equ 0x00
SELECTOR_LOCAL  equ 0x04

SELECTOR_NULL        equ 0x00
SELECTOR_KERNEL_CODE equ (0x08 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA equ (0x10 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_USER_CODE   equ (0x18 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
SELECTOR_USER_DATA   equ (0x20 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
SELECTOR_REAL_CODE   equ (0x28 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_REAL_DATA   equ (0x30 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)

DESC_OFFSET_BASE_00_15    equ 0x02
DESC_OFFSET_BASE_16_23    equ 0x04
DESC_OFFSET_BASE_24_31    equ 0x07

GATE_OFFSET_OFFSET_00_15  equ 0x02
GATE_OFFSET_OFFSET_16_31  equ 0x06

;-------------------------------------------------------------------------
; KernelStartupInfo data layout (partial)
; WARNING: MUST stay in sync with C struct tag_KERNEL_STARTUP_INFO
; in kernel/include/KernelData.h.

STRUC KernelStartupInfo
    .KernelPhysicalBase RESB 4
    .KernelSize         RESB 4
    .KernelReservedBytes RESB 4
    .StackTop           RESB 4
    .PageDirectory      RESB 4
    .IRQMask_21_PM      RESB 4
    .IRQMask_A1_PM      RESB 4
    .IRQMask_21_RM      RESB 4
    .IRQMask_A1_RM      RESB 4
    ; Console cursor position now handled directly in InitializeConsole
    .MemorySize         RESB 4
    .PageCount          RESB 4
    .MultibootMemoryEntryCount RESB 4
ENDSTRUC

;-------------------------------------------------------------------------
; Interrupt frame
; /!\ Structure MUST BE IDENTICAL to struct INTERRUPT_FRAME in arch/x86-32/x86-32.h /!\

STRUC INTERRUPT_FRAME
; CPU registers
    .EFlags             RESB 4
    .EAX                RESB 4
    .EBX                RESB 4
    .ECX                RESB 4
    .EDX                RESB 4
    .ESI                RESB 4
    .EDI                RESB 4
    .ESP                RESB 4
    .EBP                RESB 4
    .EIP                RESB 4
    .CS                 RESB 4
    .DS                 RESB 4
    .SS                 RESB 4
    .ES                 RESB 4
    .FS                 RESB 4
    .GS                 RESB 4
    .CR0                RESB 4
    .CR2                RESB 4
    .CR3                RESB 4
    .CR4                RESB 4
    .DR0                RESB 4
    .DR1                RESB 4
    .DR2                RESB 4
    .DR3                RESB 4
    .DR4                RESB 4
    .DR5                RESB 4
    .DR6                RESB 4
    .DR7                RESB 4
; FPU registers
    .FPU_Control        RESB 2      ; FPU Control register
    .FPU_Status         RESB 2      ; FPU Status register
    .FPU_Tag            RESB 2      ; FPU Tag register
    .FPU_IP_LO          RESB 2      ; FPU Instruction Pointer (low 16 bits)
    .FPU_IP_HI          RESB 4      ; FPU Instruction Pointer (high 32 bits)
    .FPU_DP_LO          RESB 2      ; FPU Data Pointer (low 16 bits)
    .FPU_DP_HI          RESB 4      ; FPU Data Pointer (high 32 bits)
    .FPU_ST0_LO         RESB 2      ; ST0 register (low 16 bits)
    .FPU_ST0_HI         RESB 8      ; ST0 register (high 64 bits)
    .FPU_ST1_LO         RESB 2      ; ST1 register (low 16 bits)
    .FPU_ST1_HI         RESB 8      ; ST1 register (high 64 bits)
    .FPU_ST2_LO         RESB 2      ; ST2 register (low 16 bits)
    .FPU_ST2_HI         RESB 8      ; ST2 register (high 64 bits)
    .FPU_ST3_LO         RESB 2      ; ST3 register (low 16 bits)
    .FPU_ST3_HI         RESB 8      ; ST3 register (high 64 bits)
    .FPU_ST4_LO         RESB 2      ; ST4 register (low 16 bits)
    .FPU_ST4_HI         RESB 8      ; ST4 register (high 64 bits)
    .FPU_ST5_LO         RESB 2      ; ST5 register (low 16 bits)
    .FPU_ST5_HI         RESB 8      ; ST5 register (high 64 bits)
    .FPU_ST6_LO         RESB 2      ; ST6 register (low 16 bits)
    .FPU_ST6_HI         RESB 8      ; ST6 register (high 64 bits)
    .FPU_ST7_LO         RESB 2      ; ST7 register (low 16 bits)
    .FPU_ST7_HI         RESB 8      ; ST7 register (high 64 bits)
; Fault data
    .SS0                RESB 4      ; SS in ring 0
    .ESP0               RESB 4      ; ESP in ring 0
    .IntNo              RESB 4      ; Interrupt / exception vector
    .ErrCode            RESB 4      ; CPU error code (0 for #UD)
ENDSTRUC

;-------------------------------------------------------------------------
; Kernel x86-32 data layout - used to fetch GDT/IDT pointers from C
; Minimal mapping: first fields must match C struct order.
; Keep this in sync with the C definition of 'Kernel_x86_32'.

STRUC KERNEL_DATA_X86_32
    .IDT                RESB 4        ; LPSEGMENT_DESCRIPTOR*
    .GDT                RESB 4        ; LPSEGMENT_DESCRIPTOR*
    .TSS                RESB 4        ; LPTASK_STATE_SEGMENT*
ENDSTRUC

;-------------------------------------------------------------------------
; Macros

%macro FUNC_HEADER 0
    align 16
%endmacro

%endif ; KERNEL_X86_32_INC
