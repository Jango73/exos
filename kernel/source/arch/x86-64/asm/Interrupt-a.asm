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
;   Interrupt stubs (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"

; DEVICE_INTERRUPT_VECTOR_MAX must match kernel/include/drivers/interrupts/DeviceInterrupt.h
%define DEVICE_INTERRUPT_VECTOR_MAX 32

BITS 64

extern BuildInterruptFrame
extern SendEOI
extern ClockHandler
extern KeyboardHandler
extern PIC2Handler
extern COM2Handler
extern COM1Handler
extern RTCHandler
extern PCIHandler
extern MouseHandler
extern FPUHandler
extern HardDriveHandler
extern DeviceInterruptHandler
extern Kernel_x86_32
extern DebugLogSyscallFrame
extern SystemCallHandler
extern RestoreCurrentTaskUserTlsBase

section .text

    global Interrupt_Default
    global Interrupt_DivideError
    global Interrupt_DebugException
    global Interrupt_NMI
    global Interrupt_BreakPoint
    global Interrupt_Overflow
    global Interrupt_BoundRange
    global Interrupt_InvalidOpcode
    global Interrupt_DeviceNotAvail
    global Interrupt_DoubleFault
    global Interrupt_MathOverflow
    global Interrupt_InvalidTSS
    global Interrupt_SegmentFault
    global Interrupt_StackFault
    global Interrupt_GeneralProtection
    global Interrupt_PageFault
    global Interrupt_AlignmentCheck
    global Interrupt_MachineCheck
    global Interrupt_FloatingPoint

    global Interrupt_Clock
    global Interrupt_Clock_Iret
    global Interrupt_Keyboard
    global Interrupt_PIC2
    global Interrupt_COM2
    global Interrupt_COM1
    global Interrupt_RTC
    global Interrupt_PCI
    global Interrupt_Mouse
    global Interrupt_FPU
    global Interrupt_HardDrive

%assign __device_slot 0
%rep DEVICE_INTERRUPT_VECTOR_MAX
    global Interrupt_Device%+__device_slot
%assign __device_slot __device_slot + 1
%endrep

    global Interrupt_SystemCall
    global EnterKernel

;-------------------------------------------------------------------------

%macro PUSH_GPRS 0
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rsp
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15
%endmacro

%macro POP_GPRS 0
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    add     rsp, 8
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax
%endmacro

%macro PUSH_SEGMENTS 0
    mov     ax, ds
    movzx   eax, ax
    push    rax
    mov     ax, es
    movzx   eax, ax
    push    rax
    mov     ax, fs
    movzx   eax, ax
    push    rax
    mov     ax, gs
    movzx   eax, ax
    push    rax
%endmacro

%macro POP_SEGMENTS 0
    pop     rax
    mov     gs, ax
    pop     rax
    mov     fs, ax
    pop     rax
    mov     es, ax
    pop     rax
    mov     ds, ax
    call    RestoreCurrentTaskUserTlsBase
%endmacro

%macro ALIGN_STACK 0
;    xor     r15, r15
;    mov     rax, rsp
;    and     rax, 0x0F
;    jz      %%aligned
;    mov     r15, rax
;    push    r15
;    sub     rsp, r15
%%aligned:
%endmacro

%macro UNALIGN_STACK 0
;    pop     r15
;    add     rsp, r15
%endmacro

SYSCALL_SAVE_SIZE      equ (15 * 8)
SYSCALL_SAVE_RAX       equ 0
SYSCALL_SAVE_RBX       equ 8
SYSCALL_INT_RAX_OFFSET equ ((4 + 15) * 8)

;-------------------------------------------------------------------------

%macro ISR_HANDLER 3
    PUSH_GPRS
    PUSH_SEGMENTS

    push    rbp
    mov     rbp, rsp

    mov     ax, ss
    movzx   eax, ax
    push    rax

    sub     rsp, INTERRUPT_FRAME_size
    lea     r11, [rsp]

    xor     r10d, r10d
    mov     rax, rsp
    and     rax, 0x0F
    jz      %%build_aligned
    mov     r10, rax
    sub     rsp, r10

%%build_aligned:

    mov     rdx, r11
    mov     esi, %3
    mov     edi, %1
    call    BuildInterruptFrame

    add     rsp, r10
    mov     rdi, rax

    xor     r10d, r10d
    mov     rax, rsp
    and     rax, 0x0F
    jz      %%handler_aligned
    mov     r10, rax
    sub     rsp, r10

%%handler_aligned:

    call    %2
    add     rsp, r10

    add     rsp, INTERRUPT_FRAME_size

    add     rsp, 8              ; SS
    pop     rbp

    POP_SEGMENTS
    POP_GPRS
    iretq
%endmacro

%macro ISR_HANDLER_NOERR 2
    ISR_HANDLER %1, %2, 0
%endmacro

%macro ISR_HANDLER_ERR 2
    ISR_HANDLER %1, %2, 1
%endmacro

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_Default:
    ISR_HANDLER_NOERR 0xFFFF, DefaultHandler

FUNC_HEADER
Interrupt_DivideError:
    ISR_HANDLER_NOERR 0, DivideErrorHandler

FUNC_HEADER
Interrupt_DebugException:
    ISR_HANDLER_NOERR 1, DebugExceptionHandler

FUNC_HEADER
Interrupt_NMI:
    ISR_HANDLER_NOERR 2, NMIHandler

FUNC_HEADER
Interrupt_BreakPoint:
    ISR_HANDLER_NOERR 3, BreakPointHandler

FUNC_HEADER
Interrupt_Overflow:
    ISR_HANDLER_NOERR 4, OverflowHandler

FUNC_HEADER
Interrupt_BoundRange:
    ISR_HANDLER_NOERR 5, BoundRangeHandler

FUNC_HEADER
Interrupt_InvalidOpcode:
    ISR_HANDLER_NOERR 6, InvalidOpcodeHandler

FUNC_HEADER
Interrupt_DeviceNotAvail:
    ISR_HANDLER_NOERR 7, DeviceNotAvailHandler

FUNC_HEADER
Interrupt_DoubleFault:
    ISR_HANDLER_ERR 8, DoubleFaultHandler

FUNC_HEADER
Interrupt_MathOverflow:
    ISR_HANDLER_NOERR 9, MathOverflowHandler

FUNC_HEADER
Interrupt_InvalidTSS:
    ISR_HANDLER_ERR 10, InvalidTSSHandler

FUNC_HEADER
Interrupt_SegmentFault:
    ISR_HANDLER_ERR 11, SegmentFaultHandler

FUNC_HEADER
Interrupt_StackFault:
    ISR_HANDLER_ERR 12, StackFaultHandler

FUNC_HEADER
Interrupt_GeneralProtection:
    ISR_HANDLER_ERR 13, GeneralProtectionHandler

FUNC_HEADER
Interrupt_PageFault:
    ISR_HANDLER_ERR 14, PageFaultHandler

FUNC_HEADER
Interrupt_AlignmentCheck:
    ISR_HANDLER_ERR 17, AlignmentCheckHandler

FUNC_HEADER
Interrupt_MachineCheck:
    ISR_HANDLER_ERR 18, MachineCheckHandler

FUNC_HEADER
Interrupt_FloatingPoint:
    ISR_HANDLER_ERR 19, FloatingPointHandler

FUNC_HEADER
Interrupt_Clock:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call SendEOI
    call EnterKernel
    call ClockHandler
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
Interrupt_Clock_Iret:
    iretq

FUNC_HEADER
Interrupt_Keyboard:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call KeyboardHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_PIC2:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call PIC2Handler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_COM2:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call COM2Handler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_COM1:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call COM1Handler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_RTC:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call RTCHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_PCI:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call PCIHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_Mouse:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call MouseHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_FPU:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call FPUHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

FUNC_HEADER
Interrupt_HardDrive:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    call HardDriveHandler
    call SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

%macro DEVICE_INTERRUPT_STUB 1

FUNC_HEADER
Interrupt_Device%1:
    cli
    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS
    call EnterKernel
    mov     edi, %1
    call    DeviceInterruptHandler
    call    SendEOI
    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK
    iretq

%endmacro

%assign __device_slot 0
%rep DEVICE_INTERRUPT_VECTOR_MAX
DEVICE_INTERRUPT_STUB __device_slot
%assign __device_slot __device_slot + 1
%endrep

FUNC_HEADER
Interrupt_SystemCall:
%if USE_SYSCALL
    ; SYSCALL entry point:
    ; RDI = Function (syscall number)
    ; RSI = Parameter (single argument)
    ; RCX = User RIP (return address)
    ; R11 = User RFLAGS

    cli

    mov     rdx, rsp                     ; Preserve user-mode stack pointer
    mov     rax, [rel Kernel_x86_32 + KERNEL_DATA_X86_64.TSS]
    mov     rsp, [rax + X86_64_TASK_STATE_SEGMENT.RSP0] ; Switch to task kernel stack

    push    rdx                          ; Store user-mode stack pointer on kernel stack
    push    rbp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    push    rcx                          ; Save user RIP (for SYSRET)
    push    r11                          ; Save user RFLAGS (for SYSRET)

    call    SystemCallHandler

    ; Restore registers
    pop     r11
    pop     rcx
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    pop     rbp

    pop     rdx                          ; Retrieve saved user-mode stack pointer
    mov     rsp, rdx                     ; Restore user stack for SYSRET

    sysretq
%else
    cli

    ALIGN_STACK
    PUSH_GPRS
    PUSH_SEGMENTS

    call    EnterKernel

    call    SystemCallHandler

    mov     [rsp + SYSCALL_INT_RAX_OFFSET], rax

    POP_SEGMENTS
    POP_GPRS
    UNALIGN_STACK

    iretq
%endif

;-------------------------------------------------------------------------

FUNC_HEADER
EnterKernel:
    push    rax
    pop     rax
    ret

section .note.GNU-stack noalloc noexec nowrite align=1
