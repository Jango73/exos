
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
;   AP bootstrap trampoline (x86-32)
;
;-------------------------------------------------------------------------

%include "x86-32.inc"

extern Kernel_x86_32

section .text

bits 32

;-------------------------------------------------------------------------
; Parameter block offsets within the trampoline page (see smp/APStartup.h)

AP_PARAMETER_BLOCK_OFFSET equ 0x400
AP_CEntry    equ 0x00
AP_StackTop  equ 0x04
AP_Cr3       equ 0x08
AP_GdtBase   equ 0x0C
AP_IdtBase   equ 0x10
AP_GdtLimit  equ 0x14
AP_IdtLimit  equ 0x18
AP_Status    equ 0x1C
AP_ApicId    equ 0x20

;-------------------------------------------------------------------------

FUNC_HEADER
global ApBootstrapPrepare
ApBootstrapPrepare :

    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx
    push    esi
    push    edi

    ; Copy the trampoline to LOW_MEMORY_PAGE_4
    mov     ebx, LOW_MEMORY_PAGE_4
    mov     esi, ApBootstrapStub
    mov     edi, ebx
    mov     ecx, ApBootstrapStubEnd - ApBootstrapStub
    cld
    rep     movsb

    ; Copy the first 8 GDT descriptors into the trampoline
    mov     esi, [Kernel_x86_32 + KERNEL_DATA_X86_32.GDT]
    lea     edi, [ebx + ApBootstrap_Gdt - ApBootstrapStub]
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    rep     movsb

    ; Patch the 16-bit -> 32-bit protected mode far jump offset
    lea     esi, [ebx + Rel_PM32 - ApBootstrapStub]
    add     dword [esi], ebx

    pop     edi
    pop     esi
    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
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
    dw      SELECTOR_KERNEL_CODE

bits 32

PM32Entry :

    ; Reload segment registers
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    ; Minimal physical stack while paging is still off
    mov     esp, LOW_MEMORY_PAGE_4 + 0xF00

    ; Enable paging with the BSP page directory
    mov     eax, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_Cr3]
    mov     cr3, eax
    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax

    ; Load the kernel GDT and IDT (their linear addresses are valid now)
    mov     eax, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_GdtBase]
    mov     edx, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_GdtLimit]
    mov     [ebx + GDT_Pseudo - ApBootstrapStub], dx
    mov     [ebx + GDT_Pseudo + 2 - ApBootstrapStub], eax
    lgdt    [ebx + GDT_Pseudo - ApBootstrapStub]

    mov     eax, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_IdtBase]
    mov     edx, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_IdtLimit]
    mov     [ebx + IDT_Pseudo - ApBootstrapStub], dx
    mov     [ebx + IDT_Pseudo + 2 - ApBootstrapStub], eax
    lidt    [ebx + IDT_Pseudo - ApBootstrapStub]

    ; Set the per-AP kernel stack
    mov     esp, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_StackTop]

    ; Jump to the C entry point (cdecl, single parameter)
    mov     eax, [ebx + AP_PARAMETER_BLOCK_OFFSET + AP_CEntry]
    lea     ecx, [ebx + AP_PARAMETER_BLOCK_OFFSET]
    push    ecx
    call    eax

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
