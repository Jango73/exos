
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
;   Interrupt stubs
;
;-------------------------------------------------------------------------

BITS 32

;----------------------------------------------------------------------------

%include "x86-32.inc"

;----------------------------------------------------------------------------

; DEVICE_INTERRUPT_VECTOR_MAX must stay in sync with kernel/include/drivers/interrupts/DeviceInterrupt.h
%define DEVICE_INTERRUPT_VECTOR_MAX 32

;----------------------------------------------------------------------------

extern DisableIRQ
extern EnableIRQ
extern BuildInterruptFrame
extern KernelLogText
extern DeviceInterruptHandler
extern TaskSynchronizeCurrentSystemCallSegments

;----------------------------------------------------------------------------
; Helper values to access function parameters

PBN equ 0x08
PBF equ 0x0A

;----------------------------------------------------------------------------
; Macros

%macro ISR_HANDLER_NOERR 2
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    push        ebp
    mov         ebp, esp

    mov         eax, ss                     ; push SS
    push        eax

    call        EnterKernel

    sub         esp, INTERRUPT_FRAME_size   ; Space used by frame

    push        esp                         ; cdecl arg 3
    push        0                           ; cdecl arg 2
    push        %1                          ; cdecl arg 1
    call        BuildInterruptFrame
    add         esp, 12                     ; cdecl clear args

    push        eax                         ; cdecl arg 1
    call        %2
    add         esp, 4                      ; cdecl clear args

    add         esp, INTERRUPT_FRAME_size   ; Space used by frame
    add         esp, 4                      ; Space used by SS
    pop         ebp

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret
%endmacro

%macro ISR_HANDLER_ERR 2
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    push        ebp
    mov         ebp, esp

    mov         eax, ss
    push        eax

    call        EnterKernel

    sub         esp, INTERRUPT_FRAME_size   ; Space used by frame

    push        esp                         ; cdecl arg 3
    push        1                           ; cdecl arg 2
    push        %1                          ; cdecl arg 1
    call        BuildInterruptFrame
    add         esp, 12                     ; cdecl clear args

    push        eax                         ; cdecl arg 1
    call        %2
    add         esp, 4                      ; cdecl clear args

    add         esp, INTERRUPT_FRAME_size   ; Space used by frame
    add         esp, 4                      ; Space used by SS
    pop         ebp

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret
%endmacro

%macro ISR_PANIC_HALT 0
    cli
%%hang:
    hlt
    jmp         %%hang
%endmacro

;----------------------------------------------------------------------------

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

;--------------------------------------
; Error code : No

FUNC_HEADER
Interrupt_Default :
    ISR_HANDLER_NOERR 0xFFFF, DefaultHandler

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

FUNC_HEADER
Interrupt_DivideError :
    ISR_HANDLER_NOERR 0, DivideErrorHandler

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

FUNC_HEADER
Interrupt_DebugException :
    ISR_HANDLER_NOERR 1, DebugExceptionHandler

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

FUNC_HEADER
Interrupt_NMI :
    ISR_HANDLER_NOERR 2, NMIHandler

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

FUNC_HEADER
Interrupt_BreakPoint :
    ISR_HANDLER_NOERR 3, BreakPointHandler

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

FUNC_HEADER
Interrupt_Overflow :
    ISR_HANDLER_NOERR 4, OverflowHandler

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

FUNC_HEADER
Interrupt_BoundRange :
    ISR_HANDLER_NOERR 5, BoundRangeHandler

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

FUNC_HEADER
Interrupt_InvalidOpcode:
    ISR_HANDLER_NOERR 6, InvalidOpcodeHandler

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

FUNC_HEADER
Interrupt_DeviceNotAvail :
    ISR_HANDLER_NOERR 7, DeviceNotAvailHandler

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

FUNC_HEADER
Interrupt_DoubleFault :
    ISR_HANDLER_ERR 8, DoubleFaultHandler

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

FUNC_HEADER
Interrupt_MathOverflow :
    ISR_HANDLER_NOERR 9, MathOverflowHandler

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

FUNC_HEADER
Interrupt_InvalidTSS :
    ISR_HANDLER_ERR 10, InvalidTSSHandler

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

FUNC_HEADER
Interrupt_SegmentFault :
    ISR_HANDLER_ERR 11, SegmentFaultHandler

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

FUNC_HEADER
Interrupt_StackFault :
    ISR_HANDLER_ERR 12, StackFaultHandler

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

FUNC_HEADER
Interrupt_GeneralProtection :
    ISR_HANDLER_ERR 13, GeneralProtectionHandler

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

FUNC_HEADER
Interrupt_PageFault:
    ISR_HANDLER_ERR 14, PageFaultHandler

;--------------------------------------
; Int 16     : Floating-Point Error Exception (#MF)
; Class      : Fault
; Error code : No

;--------------------------------------
; Int 17     : Alignment Check Exception (#AC)
; Class      : Fault
; Error code : Yes, always 0

FUNC_HEADER
Interrupt_AlignmentCheck :
    ISR_HANDLER_ERR 17, AlignmentCheckHandler

;--------------------------------------
; Int 18     : Machine-Check Exception (#MC)
; Class      : Abort
; Error code : No

Interrupt_MachineCheck :
    ISR_HANDLER_ERR 18, MachineCheckHandler

;--------------------------------------
; Int 19     : SIMD Floating-Point Exception (#XM)
; Class      : Abort
; Error code : No

Interrupt_FloatingPoint :
    ISR_HANDLER_ERR 19, FloatingPointHandler

;--------------------------------------
; Int 32     : Clock
; Class      : Trap
; Error code : No

FUNC_HEADER
Interrupt_Clock:

    pushad
    push        ds
    push        es

    push        ebp
    mov         ebp, esp

    call        SendEOI                     ; Send EOI to appropriate controller

    call        EnterKernel

    call        ClockHandler

    pop         ebp

    pop         es
    pop         ds
    popad

Interrupt_Clock_Iret:

    iret

;--------------------------------------

FUNC_HEADER
Interrupt_Keyboard :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        KeyboardHandler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;--------------------------------------

FUNC_HEADER
Interrupt_PIC2 :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        PIC2Handler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_COM2 :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        COM2Handler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_COM1 :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        COM1Handler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_Mouse :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

;    mov         eax, 4
;    push        eax
;    call        DisableIRQ
;    add         esp, 4

    call        MouseHandler

    call        SendEOI

;    mov         eax, 4
;    push        eax
;    call        EnableIRQ
;    add         esp, 4

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_RTC :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        RTCHandler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_PCI :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        PCIHandler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_FPU :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        FPUHandler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_HardDrive :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    call        HardDriveHandler

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

%macro DEVICE_INTERRUPT_STUB 1

FUNC_HEADER
Interrupt_Device%1 :

    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    push        %1
    call        DeviceInterruptHandler
    add         esp, 4

    call        SendEOI

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

%endmacro

%assign __device_slot 0
%rep DEVICE_INTERRUPT_VECTOR_MAX
DEVICE_INTERRUPT_STUB __device_slot
%assign __device_slot __device_slot + 1
%endrep

;-------------------------------------------------------------------------

FUNC_HEADER
Interrupt_SystemCall :

    pushad
    push        ds
    push        es
    push        fs
    push        gs

    call        EnterKernel

    push        ebx
    push        eax
    call        SystemCallHandler
    add         esp, 8
    
    ; Store return value in the saved EAX location on stack
    ; pushad order: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    ; Stack after pushad: [ESP+0]=EDI, [ESP+4]=ESI, [ESP+8]=EBP, [ESP+12]=ESP, [ESP+16]=EBX, [ESP+20]=EDX, [ESP+24]=ECX, [ESP+28]=EAX
    ; After 4 segment registers (16 bytes): EAX is at [esp + 28 + 16] = [esp + 44]
    mov         [esp + 44], eax

    push        esp
    call        TaskSynchronizeCurrentSystemCallSegments
    add         esp, 4

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iret

;-------------------------------------------------------------------------

FUNC_HEADER
EnterKernel :

    push        eax
    mov         ax,  SELECTOR_KERNEL_DATA
    mov         ds,  ax
    mov         es,  ax
    pop         eax
    ret

;-------------------------------------------------------------------------

Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
    ret
