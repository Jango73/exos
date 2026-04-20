
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
;   Assembly system definitions
;
;-------------------------------------------------------------------------

;-------------------------------------------------------------------------
; Useful constants

%ifndef KERNEL_ARCH_MAX_UINT
    %error "KERNEL_ARCH_MAX_UINT must be defined by the architecture include"
%endif

EXOS_USER_CALL equ 0x70

MAX_UINT    equ KERNEL_ARCH_MAX_UINT
U32_SIZE    equ 4
U64_SIZE    equ 8

N_1KB       equ 0x00000400
N_2KB       equ 0x00000800
N_4KB       equ 0x00001000
N_8KB       equ 0x00002000
N_16KB      equ 0x00004000
N_32KB      equ 0x00008000
N_64KB      equ 0x00010000
N_128KB     equ 0x00020000
N_256KB     equ 0x00040000
N_512KB     equ 0x00080000
N_1MB       equ 0x00100000
N_2MB       equ 0x00200000
N_4MB       equ 0x00400000
N_8MB       equ 0x00800000

LOW_MEMORY_PAGE_1   equ 0x00001000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_2   equ 0x00002000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_3   equ 0x00003000  ; Reserved by VBR system structures
LOW_MEMORY_PAGE_4   equ 0x00004000
LOW_MEMORY_PAGE_5   equ 0x00005000  ; RMC code base
LOW_MEMORY_PAGE_6   equ 0x00006000  ; RMC buffers
LOW_MEMORY_PAGE_7   equ 0x00007000

;-------------------------------------------------------------------------
; Shift values to multiply or divide by machine constants

MUL_2       equ 1
MUL_4       equ 2
MUL_8       equ 3
MUL_16      equ 4
MUL_32      equ 5
MUL_64      equ 6
MUL_128     equ 7
MUL_256     equ 8
MUL_512     equ 9
MUL_1KB     equ 10
MUL_2KB     equ 11
MUL_4KB     equ 12
MUL_8KB     equ 13
MUL_16KB    equ 14
MUL_32KB    equ 15
MUL_64KB    equ 16
MUL_128KB   equ 17
MUL_256KB   equ 18
MUL_512KB   equ 19
MUL_1MB     equ 20
MUL_2MB     equ 21
MUL_4MB     equ 22
MUL_8MB     equ 23
MUL_16MB    equ 24

;-------------------------------------------------------------------------
; Bit layout of the EFlags register

EFLAGS_CF    equ 0x00000001             ; Carry
EFLAGS_A1    equ 0x00000002             ; Always 1
EFLAGS_PF    equ 0x00000004             ; Parity
EFLAGS_AF    equ 0x00000010             ;
EFLAGS_ZF    equ 0x00000040             ; Zero
EFLAGS_SF    equ 0x00000080             ; Sign
EFLAGS_TF    equ 0x00000100             ; Trap
EFLAGS_IF    equ 0x00000200             ; Interrupt
EFLAGS_OF    equ 0x00000800             ; Overflow
EFLAGS_IOPL1 equ 0x00001000             ; IO privilege level bit 1
EFLAGS_IOPL2 equ 0x00002000             ; IO privilege level bit 2
EFLAGS_NT    equ 0x00004000             ; Nested task
EFLAGS_RF    equ 0x00010000             ; Resume
EFLAGS_VM    equ 0x00020000             ; Virtual 8086 mode

;-------------------------------------------------------------------------
; Values related to IRQs and the PIC(S) (8259-1 & 8259-2)

INTERRUPT_CONTROL equ 0x0020
INTERRUPT_DONE    equ 0x0020

PIC1_CMD          equ 0x0020           ; PIC1 command port
PIC1_DATA         equ 0x0021           ; PIC1 data port
PIC2_CMD          equ 0x00A0           ; PIC2 command port
PIC2_DATA         equ 0x00A1           ; PIC2 data port
ICW1_ICW4         equ 0x0001           ; ICW4 (not) needed
ICW1_SINGLE       equ 0x0002           ; Single (cascade) mode
ICW1_INTERVAL4    equ 0x0004           ; Call address interval 4 (8)
ICW1_LEVEL        equ 0x0008           ; Level triggered (edge) mode
ICW1_INIT         equ 0x0010           ; Initialization - r ired!
ICW4_8086         equ 0x0001           ; 8086/88 (MCS-80/85) mode
ICW4_AUTO         equ 0x0002           ; Auto (normal) EOI
ICW4_BUF_SLAVE    equ 0x0008           ; Buffered mode/slave
ICW4_BUF_MASTER   equ 0x000C           ; Buffered mode/master
ICW4_SFNM         equ 0x0010           ; Special fully nested (not)

PIC_EOI           equ 0x0020           ; End Of Interrupt
PIC1_VECTOR       equ 0x0020           ; New PIC interrupt vectors
PIC2_VECTOR       equ 0x0028

;-------------------------------------------------------------------------
; Values related to the 8253 chip (timer)

CLOCK_DATA    equ 0x0040
CLOCK_COMMAND equ 0x0043

CLOCK_18_2 equ 0xFFFF
CLOCK_100  equ 0x2E9C

;-------------------------------------------------------------------------
; Values related to the 8042 chip (keyboard)

KEYBOARD_CONTROL equ 0x0060
KEYBOARD_STATUS  equ 0x0064

KEYBOARD_STATUS_OUT_FULL     equ 0x01
KEYBOARD_STATUS_IN_FULL      equ 0x02
KEYBOARD_STATUS_COMMAND      equ 0x08
KEYBOARD_STATUS_ACTIVE       equ 0x10
KEYBOARD_STATUS_OUT_ERROR    equ 0x20
KEYBOARD_STATUS_IN_ERROR     equ 0x40
KEYBOARD_STATUS_PARITY_ERROR equ 0x80

;-------------------------------------------------------------------------
; Log values
LOG_DEBUG equ 0x0001
LOG_VERBOSE equ 0x0002
LOG_WARNING equ 0x0004
LOG_ERROR equ 0x0008
LOG_TEST equ 0x0010

;-------------------------------------------------------------------------
; Extern C symbols

extern KernelMain
extern InitializeKernel
extern KernelPrint
extern ConsolePrint
extern ConsolePrintChar
extern KernelLogText

extern SystemCallHandler

extern DefaultHandler
extern DivideErrorHandler
extern DebugExceptionHandler
extern NMIHandler
extern BreakPointHandler
extern OverflowHandler
extern BoundRangeHandler
extern InvalidOpcodeHandler
extern DeviceNotAvailHandler
extern DoubleFaultHandler
extern MathOverflowHandler
extern InvalidTSSHandler
extern SegmentFaultHandler
extern StackFaultHandler
extern GeneralProtectionHandler
extern PageFaultHandler
extern AlignmentCheckHandler
extern MachineCheckHandler
extern FloatingPointHandler

extern ClockHandler
extern SendEOI
extern KeyboardHandler
extern PIC2Handler
extern COM2Handler
extern COM1Handler
extern RTCHandler
extern PCIHandler
extern MouseHandler
extern FPUHandler
extern HardDriveHandler
extern Scheduler

extern Sleep

extern Kernel
extern KernelStartup
extern __stub_init_end
extern __stub_init_start
extern __bss_init_end
extern __bss_init_start

;----------------------------------------------------------
