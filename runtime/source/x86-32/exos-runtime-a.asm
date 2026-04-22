
;-------------------------------------------------------------------------
;
;   EXOS Kernel / Runtime
;   Copyright (c) 1999-2026 Jango73
;
;   SPDX-License-Identifier: MIT
;   See runtime/LICENSE for license terms.
;
;
;   EXOS Runtime
;
;-------------------------------------------------------------------------

BITS 32

;----------------------------------------------------------------------------
; EXOS syscall

EXOS_USER_CALL equ 0x70

;----------------------------------------------------------------------------
; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------
; Runtime symbols (only required outside of the kernel build)

%ifndef __KERNEL__
extern main
extern _argc
extern _argv
extern _SetupArguments
%endif

;----------------------------------------------------------------------------

section .data

%ifndef __KERNEL__
    global StartEBP
    global StartESP

_StartEBP          : dd 0
_StartESP          : dd 0
_TaskArgument      : dd 0
%endif

;----------------------------------------------------------------------------

section .text

%ifndef __KERNEL__
    global _start
    global __start__
    global __exit__
    global exoscall
%endif
    global memset
    global memcpy
    global memcmp
    global memmove
    global strlen
    global strcpy
    global strncpy
    global strcat
    global strcmp
    global strncmp
    global strstr
    global strchr

;-------------------------------------------------------------------------
; __start__ is the entry point of an executable's main thread
; On entry, the task argument is on the stack
; The first thing to do is to save the stack
; registers in case __exit__ is called

%ifndef __KERNEL__
_start:
__start__ :

    mov     [_StartEBP], ebp
    mov     [_StartESP], esp

    push    ebp
    mov     ebp, esp

    mov     eax, [ebp+(PBN+0)]
    mov     [_TaskArgument], eax

    call    _SetupArguments

    mov     eax, [_argv]
    push    eax
    mov     eax, [_argc]
    push    eax
    call    main
    add     esp, 8

    pop     ebp
    ret

;-------------------------------------------------------------------------
; This function is used to abort an application
; We just assign EBP and ESP with the values
; they had in __start__ and do a return
; We also store the return code in EAX

__exit__ :

    push    ebp
    mov     ebp, esp

    mov     eax, [ebp+(PBN+0)]

    mov     ebp, [_StartEBP]
    mov     esp, [_StartESP]

    ret


;--------------------------------------
; This is the call to EXOS services
; We setup arguments and call system interrupt
; Function number is in EAX
; Argument is in EBX

exoscall :

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     eax, [ebp+(PBN+0)]    ; syscall number
    mov     ebx, [ebp+(PBN+4)]    ; argument
    int     EXOS_USER_CALL

    pop     ebx
    pop     ebp
    ret
%endif ; __KERNEL__

;--------------------------------------

memset :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    edi

    mov     edi, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     stosb

    pop     edi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

memcpy :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi

    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     movsb

    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

memcmp :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi

    mov     esi, [ebp+(PBN+0)]   ; s1
    mov     edi, [ebp+(PBN+4)]   ; s2
    mov     ecx, [ebp+(PBN+8)]   ; n

    test    ecx, ecx
    jz      .equal               ; if n == 0, return 0

    cld
    repe    cmpsb                ; compare bytes while equal

    jz      .equal               ; if all bytes matched, return 0

    ; bytes differ, calculate difference
    movzx   eax, byte [esi-1]    ; get last compared byte from s1
    movzx   edx, byte [edi-1]    ; get last compared byte from s2
    sub     eax, edx
    jmp     .end

.equal:
    xor     eax, eax             ; return 0

.end:
    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

memmove :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi

    mov     edi, [ebp+(PBN+0)]   ; dest
    mov     esi, [ebp+(PBN+4)]   ; src
    mov     ecx, [ebp+(PBN+8)]   ; n

    test    ecx, ecx
    jz      .end                 ; if n == 0, nothing to do

    cmp     edi, esi
    je      .end                 ; if dest == src, nothing to do
    jb      .copy_forward        ; if dest < src, copy forward

    ; dest > src, check for overlap
    mov     eax, esi
    add     eax, ecx
    cmp     edi, eax
    jae     .copy_forward        ; if dest >= src+n, no overlap, copy forward

    ; overlap detected, copy backward
    add     esi, ecx
    add     edi, ecx
    dec     esi
    dec     edi
    std                          ; set direction flag for backward copy
    rep     movsb
    cld                          ; clear direction flag
    jmp     .end

.copy_forward:
    cld                          ; clear direction flag for forward copy
    rep     movsb

.end:
    mov     eax, [ebp+(PBN+0)]   ; return dest

    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

strlen :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi

    mov     esi, [ebp+(PBN+0)]
    mov     edi, esi

.loop :
    lodsb
    cmp     al, 0
    je      .out
    jmp     .loop

.out :
    sub     esi, edi         ; esi = longueur + 1 (car lodsb a incrémenté après \0)
    mov     eax, esi         ; eax = longueur + 1
    dec     eax              ; eax = longueur correcte

    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

strcpy :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx

    mov     eax, [ebp+(PBN+4)]
    push    eax
    call    strlen
    add     esp, 4
    inc     eax
    mov     ecx, eax
    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    cld
    rep     movsb
    mov     eax, [ebp+(PBN+0)]

    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

strncpy :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx

    mov     edi, [ebp+(PBN+0)]     ; dest
    mov     esi, [ebp+(PBN+4)]     ; src
    mov     ecx, [ebp+(PBN+8)]     ; n
    mov     eax, edi               ; return dest

.copy_loop:
    test    ecx, ecx               ; check if n == 0
    jz      .done
    lodsb                          ; load byte from src
    stosb                          ; store byte to dest
    test    al, al                 ; check if null terminator
    jz      .pad_zeros
    dec     ecx
    jmp     .copy_loop

.pad_zeros:
    dec     ecx
    test    ecx, ecx
    jz      .done
    xor     al, al                 ; al = 0
    rep     stosb                  ; fill remaining with zeros

.done:
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

strcat :

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     eax, [ebp+(PBN+0)]
    push    eax
    call    strlen
    add     esp, 4
    mov     ebx, [ebp+(PBN+4)]
    push    ebx
    push    eax
    call    strcpy
    add     esp, 8
    mov     eax, [ebp+(PBN+0)]

    pop     ebx
    pop     ebp
    ret

;--------------------------------------

strcmp :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx

    mov     esi, [ebp+(PBN+0)]  ; str1
    mov     edi, [ebp+(PBN+4)]  ; str2

.loop:
    mov     al, [esi]        ; char1
    mov     bl, [edi]        ; char2
    cmp     al, bl
    jne     .diff
    test    al, al           ; end of string ?
    je      .equal
    inc     esi
    inc     edi
    jmp     .loop

.diff:
    movzx   eax, al
    movzx   ebx, bl
    sub     eax, ebx
    jmp     .end

.equal:
    xor     eax, eax         ; return 0

.end:
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

strncmp :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ecx

    mov     esi, [ebp+(PBN+0)]  ; str1
    mov     edi, [ebp+(PBN+4)]  ; str2
    mov     ecx, [ebp+(PBN+8)]  ; length

    test    ecx, ecx
    jz      .equal

.loop:
    mov     al, [esi]
    mov     dl, [edi]
    cmp     al, dl
    jne     .diff
    test    al, al
    je      .equal
    inc     esi
    inc     edi
    dec     ecx
    jz      .equal
    jmp     .loop

.diff:
    movzx   eax, al
    movzx   edx, dl
    sub     eax, edx
    jmp     .end

.equal:
    xor     eax, eax

.end:
    pop     ecx
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

strchr :

    push    ebp
    mov     ebp, esp
    push    esi

    mov     esi, [ebp+(PBN+0)]  ; string pointer
    mov     edx, [ebp+(PBN+4)]  ; character value
    and     edx, 0xFF

.scan:
    mov     al, [esi]
    cmp     al, dl
    je      .found
    test    al, al
    je      .not_found
    inc     esi
    jmp     .scan

.found:
    mov     eax, esi
    pop     esi
    pop     ebp
    ret

.not_found:
    xor     eax, eax
    pop     esi
    pop     ebp
    ret

;----------------------------------------------------------------------------

strstr:

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx
    push    ecx

    mov     esi, [ebp+(PBN+0)] ; esi = haystack pointer
    mov     edi, [ebp+(PBN+4)] ; edi = needle pointer

    mov     al, [edi]        ; check if needle is empty
    test    al, al
    jz      .needle_empty    ; if needle == "", return haystack

.loop_haystack:
    mov     ebx, esi         ; ebx = current haystack position
    mov     ecx, edi         ; ecx = current needle position

.loop_compare:
    mov     al, [ecx]        ; load current char of needle
    test    al, al
    jz      .found           ; end of needle: match found
    mov     dl, [ebx]        ; load current char of haystack
    test    dl, dl
    jz      .not_found       ; end of haystack, no match
    cmp     al, dl
    jne     .no_match        ; mismatch, try next position
    inc     ebx
    inc     ecx
    jmp     .loop_compare    ; continue comparing next chars

.no_match:
    inc     esi
    mov     al, [esi]
    test    al, al
    jz      .not_found       ; haystack ended, not found
    jmp     .loop_haystack   ; try next haystack position

.needle_empty:
    mov     eax, esi         ; needle is empty: return haystack
    pop     ecx
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

.found:
    mov     eax, esi         ; return pointer to match in haystack
    pop     ecx
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

.not_found:
    xor     eax, eax         ; not found: return 0
    pop     ecx
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret
