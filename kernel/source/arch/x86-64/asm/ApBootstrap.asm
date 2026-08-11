;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   Copyright (c) 1999-2026 Jango73
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
;   AP bootstrap trampoline (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"

%define SELECTOR_PM32_CODE 0x18   ; 32-bit protected mode code descriptor (index 3)
%define SELECTOR_PM32_DATA 0x20   ; 32-bit protected mode data descriptor (index 4)

extern Kernel_x86_32

section .text

bits 64

;-------------------------------------------------------------------------
; Parameter block offsets within the trampoline page (see smp/APStartup.h)

AP_PARAMETER_BLOCK_OFFSET equ 0x400
AP_CEntry    equ 0x00
AP_StackTop  equ 0x08
AP_Cr3       equ 0x10
AP_GdtBase   equ 0x18
AP_IdtBase   equ 0x20
AP_GdtLimit  equ 0x28
AP_IdtLimit  equ 0x2C
AP_Status    equ 0x30
AP_ApicId    equ 0x34

;-------------------------------------------------------------------------

FUNC_HEADER
global ApBootstrapPrepare
ApBootstrapPrepare :

    push    rbp
    mov     rbp, rsp
    push    rbx
    push    rcx
    push    rdx
    push    rsi
    push    rdi

    ; Copy the trampoline to LOW_MEMORY_PAGE_4
    mov     rbx, LOW_MEMORY_PAGE_4
    lea     rsi, [rel ApBootstrapStub]
    mov     rdi, rbx
    mov     ecx, ApBootstrapStubEnd - ApBootstrapStub
    cld
    rep     movsb

    ; Copy the first 8 GDT descriptors into the trampoline
    mov     rsi, [rel Kernel_x86_32 + KERNEL_DATA_X86_64.GDT]
    lea     rdi, [rbx + ApBootstrap_Gdt - ApBootstrapStub]
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    rep     movsb

    ; Build a 32-bit protected mode code descriptor at index 3
    lea     rdi, [rbx + ApBootstrap_Gdt - ApBootstrapStub + (3 * SEGMENT_DESCRIPTOR_SIZE)]
    mov     dword [rdi], 0x0000FFFF
    mov     dword [rdi + 4], 0x00CF9A00

    ; Build a 32-bit protected mode data descriptor at index 4
    lea     rdi, [rbx + ApBootstrap_Gdt - ApBootstrapStub + (4 * SEGMENT_DESCRIPTOR_SIZE)]
    mov     dword [rdi], 0x0000FFFF
    mov     dword [rdi + 4], 0x00CF9200

    ; Patch the 16-bit -> 32-bit protected mode far jump offset
    lea     rsi, [rbx + Rel_PM32 - ApBootstrapStub]
    add     dword [rsi], ebx

    ; Patch the 32-bit -> long mode far jump offset
    lea     rsi, [rbx + Rel_Long - ApBootstrapStub]
    add     dword [rsi], ebx

    pop     rdi
    pop     rsi
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rbp
    ret

;-------------------------------------------------------------------------
; AP trampoline. Copied to LOW_MEMORY_PAGE_4 and executed by the AP
; starting in 16-bit real mode after a SIPI.

global ApBootstrapStub
ApBootstrapStub :

bits 16

    jmp     short RealModeStart

ApBootstrap_Gdt :
    times 64 db 0

RealModeStart :

    cli

    ; Compute the physical base of this stub (CS << 4 == 0x4000)
    mov     ax, cs
    shl     eax, 4
    mov     ebx, eax

    ; Load the temporary GDT (located inside the stub)
    mov     word [ebx + GDT_Pseudo - ApBootstrapStub], (8 * SEGMENT_DESCRIPTOR_SIZE) - 1
    lea     eax, [ebx + ApBootstrap_Gdt - ApBootstrapStub]
    mov     dword [ebx + GDT_Pseudo + 2 - ApBootstrapStub], eax
    lgdt    [ebx + GDT_Pseudo - ApBootstrapStub]

    ; Load a temporary real-mode IDT (BIOS IVT at address 0)
    xor     eax, eax
    mov     word [ebx + IDT_Pseudo - ApBootstrapStub], 1023
    mov     dword [ebx + IDT_Pseudo + 2 - ApBootstrapStub], eax
    lidt    [ebx + IDT_Pseudo - ApBootstrapStub]

    ; Switch to 32-bit protected mode
    mov     eax, cr0
    or      eax, CR0_PROTECTED_MODE
    mov     cr0, eax

    ; Far jump to 32-bit protected mode code (patched by ApBootstrapPrepare)
    db      0x66
    db      0xEA
Rel_PM32 :
    dd      PM32Entry - ApBootstrapStub
    dw      SELECTOR_PM32_CODE

bits 32

PM32Entry :

    ; Reload segment registers
    mov     ax, SELECTOR_PM32_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; Minimal physical stack while paging is still off
    mov     esp, LOW_MEMORY_PAGE_4 + 0xF00

    ; Enable long mode: EFER.LME, CR4.PAE, CR3, CR0.PG
    mov     ecx, 0xC0000080          ; EFER MSR
    rdmsr
    or      eax, 0x00000100          ; LME bit
    wrmsr

    mov     eax, cr4
    or      eax, CR4_PAE
    mov     cr4, eax

    mov     eax, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_Cr3]
    mov     cr3, eax

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax

    ; Now in compatibility mode. Far jump to 64-bit code (patched by ApBootstrapPrepare)
    db      0xEA
Rel_Long :
    dd      LongModeEntry - ApBootstrapStub
    dw      SELECTOR_KERNEL_CODE

bits 64

LongModeEntry :

    ; Load the kernel GDT and IDT from the parameter block
    mov     rax, qword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_GdtBase]
    mov     edx, dword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_GdtLimit]
    sub     rsp, 16
    mov     word [rsp], dx
    mov     qword [rsp + 2], rax
    lgdt    [rsp]

    mov     rax, qword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_IdtBase]
    mov     edx, dword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_IdtLimit]
    mov     word [rsp], dx
    mov     qword [rsp + 2], rax
    lidt    [rsp]
    add     rsp, 16

    ; Reload kernel data segments
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; Set the per-AP kernel stack
    mov     rsp, qword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_StackTop]

    ; Jump to the C entry point (System V, first parameter in RDI)
    mov     rdi, LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET
    mov     rax, qword [LOW_MEMORY_PAGE_4 + AP_PARAMETER_BLOCK_OFFSET + AP_CEntry]
    call    rax

AP_Hang :

    cli
    hlt
    jmp     AP_Hang

GDT_Pseudo :
    dw  0
    dd  0
IDT_Pseudo :
    dw  0
    dd  0

global ApBootstrapStubEnd
ApBootstrapStubEnd :
