
/************************************************************************\

    EXOS Bootloader
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


    VBR Payload main code

\************************************************************************/

// x86-32 32 bits real mode payload entry point

#include "../include/vbr-realmode-utils.h"
#include "../include/vbr-payload-shared.h"
#include "boot-reservation.h"
#include "arch/x86-32/x86-32.h"
#include "system/SerialPort.h"
#include "text/CoreString.h"

/************************************************************************/

__asm__(".code16gcc");

#ifndef KERNEL_FILE
#error "KERNEL_FILE must be defined (e.g., -DKERNEL_FILE=\"exos.bin\")"
#endif

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize, U32 MultibootInfoPtr, U64 UefiImageBase, U64 UefiImageSize);
BOOL LoadKernelFat32(U32 BootDrive, U32 PartitionLba, const char* KernelFile, U32* FileSizeOut);
BOOL LoadKernelExt2(U32 BootDrive, U32 PartitionLba, const char* KernelName, U32* FileSizeOut);

/************************************************************************/

static void InitDebug(void);
static void OutputChar(U8 Char);
static void WriteString(LPCSTR Str);
static U32 ComputeKernelReservedBytes(U32 FileSize);
static BOOL BootQueryTextCursorPosition(U32* CursorX, U32* CursorY);

STR TempString[128];
static const U16 COMPorts[4] = {0x3F8, 0x2F8, 0x3E8, 0x2E8};

/************************************************************************/

static void InitDebug(void) {
#if DEBUG_OUTPUT != 0
    SerialReset(0);
#endif
}

/************************************************************************/

static void OutputChar(U8 Char) {
#if DEBUG_OUTPUT == 2
    SerialOut(0, Char);
#elif DEBUG_OUTPUT == 1
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Char)
        : "ah", "al");
    SerialOut(0, Char);
#else
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Char)
        : "ah", "al");
#endif
}

/************************************************************************/

static void WriteString(LPCSTR Str) {
    while (*Str) {
        OutputChar((U8)*Str++);
    }
}

/************************************************************************/

static U32 ComputeKernelReservedBytes(U32 FileSize) {
    U32 MapSize = PAGE_ALIGN(FileSize + BOOT_KERNEL_MAP_PADDING_BYTES);

#if defined(ARCH_X86_64)
    if (MapSize < BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN) {
        MapSize = BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN;
    }

    U32 TotalPages = (MapSize + PAGE_SIZE - 1) >> MUL_4KB;
    U32 TableCount = (TotalPages + BOOT_X86_64_PAGE_TABLE_ENTRIES - 1) / BOOT_X86_64_PAGE_TABLE_ENTRIES;
    return MapSize + (TableCount * BOOT_X86_64_PAGE_TABLE_SIZE);
#else
    return MapSize;
#endif
}

/************************************************************************/

/**
 * @brief Query the BIOS text cursor position on page zero.
 * @param CursorX Receives the text column.
 * @param CursorY Receives the text row.
 * @return TRUE when the BIOS call completed.
 */
static BOOL BootQueryTextCursorPosition(U32* CursorX, U32* CursorY) {
    U16 CursorPosition = 0;

    if (CursorX == NULL || CursorY == NULL) {
        return FALSE;
    }

    __asm__ __volatile__(
        "movb $0x03, %%ah\n\t"
        "movb $0x00, %%bh\n\t"
        "int $0x10\n\t"
        "movw %%dx, %0\n\t"
        : "=rm"(CursorPosition)
        :
        : "ax", "bx", "dx");

    *CursorX = (U32)(CursorPosition & 0x00FF);
    *CursorY = (U32)((CursorPosition >> 8) & 0x00FF);
    return TRUE;
}

/************************************************************************/

void BootDebugPrint(LPCSTR Format, ...) {
#if DEBUG_OUTPUT == 0
    UNUSED(Format);
#else
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
#endif
}

/************************************************************************/

void BootVerbosePrint(LPCSTR Format, ...) {
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
}

/************************************************************************/

void BootErrorPrint(LPCSTR Format, ...) {
    VarArgList Args;
    VarArgStart(Args, Format);
    StringPrintFormatArgs(TempString, Format, Args);
    VarArgEnd(Args);
    WriteString(TempString);
}

/************************************************************************/

const char* BootGetFileName(const char* Path) {
    if (Path == NULL) {
        return "";
    }

    const char* Result = Path;
    for (const char* Ptr = Path; *Ptr != '\0'; ++Ptr) {
        if (*Ptr == '/' || *Ptr == '\\') {
            Result = Ptr + 1;
        }
    }

    return Result;
}

/************************************************************************/

static char ToLowerChar(char C) {
    if (C >= 'A' && C <= 'Z') {
        return (char)(C - 'A' + 'a');
    }
    return C;
}

/************************************************************************/

static void BuildKernelExt2Name(char* Out, U32 OutSize) {
    if (Out == NULL || OutSize == 0U) {
        return;
    }

    const char* FileName = BootGetFileName(KERNEL_FILE);
    U32 Pos = 0;

    for (const char* Ptr = FileName; *Ptr != '\0' && Pos + 1U < OutSize; ++Ptr) {
        Out[Pos++] = ToLowerChar(*Ptr);
    }

    Out[Pos] = '\0';
}

/************************************************************************/

static void VerifyKernelImage(U32 FileSize) {
    if (FileSize < 8U) {
        BootErrorPrint(TEXT("[VBR] ERROR: FileSize too small for checksum. Halting.\r\n"));
        Hang();
    }

    const U8* const FileStart = (const U8*)KERNEL_LINEAR_LOAD_ADDRESS;
    const U8* const ChecksumPtr = FileStart + FileSize - sizeof(U32);

    BootDebugPrint(
        TEXT("[VBR] VerifyKernelImage scanning %u data bytes\r\n"),
        FileSize - (U32)sizeof(U32));

    U32 LastBytes1 = 0;
    U32 LastBytes2 = 0;

    for (int Index = 0; Index < 4; ++Index) {
        LastBytes1 |= ((U32)FileStart[FileSize - 8U + (U32)Index]) << (Index * 8);
        LastBytes2 |= ((U32)FileStart[FileSize - 4U + (U32)Index]) << (Index * 8);
    }

    BootDebugPrint(TEXT("[VBR] Last 8 bytes of file: %x %x\r\n"), LastBytes1, LastBytes2);

    U32 Computed = 0;
    for (U32 Index = 0; Index < FileSize - sizeof(U32); ++Index) {
        Computed += FileStart[Index];
    }

    U32 Stored = 0;
    for (int Index = 0; Index < 4; ++Index) {
        Stored |= ((U32)ChecksumPtr[Index]) << (Index * 8);
    }

    BootDebugPrint(TEXT("[VBR] Stored checksum in image : %x\r\n"), Stored);

    if (Computed == Stored) {
        BootDebugPrint(
            TEXT("[VBR] Image checksum OK. Stored : %x vs computed : %x\r\n"),
            Stored,
            Computed);
    } else {
        BootErrorPrint(
            TEXT("[VBR] Checksum mismatch. Halting. Stored : %x vs computed : %x\r\n"),
            Stored,
            Computed);
        Hang();
    }
}

/************************************************************************/
// E820 memory map buffers shared with architecture specific code
/************************************************************************/

U32 E820_EntryCount = 0;
E820ENTRY E820_Map[E820_MAX_ENTRIES];

// Multiboot structures - placed at a safe memory location
multiboot_info_t MultibootInfo;
multiboot_memory_map_t MultibootMemMap[E820_MAX_ENTRIES];
multiboot_module_t KernelModule;
EXOS_MULTIBOOT_CONFIG_TABLE MultibootConfigTable;
const char BootloaderName[] = "EXOS VBR";
const char KernelCmdLine[] = KERNEL_FILE;

/************************************************************************/
// Low-level I/O + A20

static inline U8 InPortByte(U16 Port) {
    U8 Val;
    __asm__ __volatile__("inb %1, %0" : "=a"(Val) : "Nd"(Port));
    return Val;
}

static inline void OutPortByte(U16 Port, U8 Val) { __asm__ __volatile__("outb %0, %1" ::"a"(Val), "Nd"(Port)); }

/************************************************************************/

void SerialReset(U8 Which) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    /* Disable UART interrupts */
    OutPortByte(base + UART_IER, 0x00);

    /* Enable DLAB to program baud rate */
    OutPortByte(base + UART_LCR, LCR_DLAB);

    /* Set baud rate divisor (38400) */
    OutPortByte(base + UART_DLL, (U8)(BAUD_DIV_38400 & 0xFF));
    OutPortByte(base + UART_DLM, (U8)((BAUD_DIV_38400 >> 8) & 0xFF));

    /* 8N1, clear DLAB */
    OutPortByte(base + UART_LCR, LCR_8N1);

    /* Enable FIFO, clear RX/TX, set trigger level */
    OutPortByte(base + UART_FCR, (U8)(FCR_ENABLE | FCR_CLR_RX | FCR_CLR_TX | FCR_TRIG_14));

    /* Assert DTR/RTS and enable OUT2 (required for IRQ routing) */
    OutPortByte(base + UART_MCR, (U8)(MCR_DTR | MCR_RTS | MCR_OUT2));
}

/************************************************************************/

void SerialOut(U8 Which, U8 Char) {
    if (Which > 3) return;
    U16 base = COMPorts[Which];

    const U32 MaxSpin = 100000;
    U32 spins = 0;

    /* Wait for THR empty (LSR_THRE). Give up on timeout. */
    while (!(InPortByte(base + UART_LSR) & LSR_THRE)) {
        if (++spins >= MaxSpin) return;
    }

    OutPortByte(base + UART_THR, Char);
}

/************************************************************************/

static void RetrieveMemoryMap(void) {
    MemorySet((void*)E820_Map, 0, E820_SIZE);
    E820_EntryCount = BiosGetMemoryMap(MakeSegOfs(E820_Map), E820_MAX_ENTRIES);

    BootDebugPrint(TEXT("[VBR] E820 map at %x\r\n"), (U32)E820_Map);
    BootDebugPrint(TEXT("[VBR] E820 entry count : %d\r\n"), E820_EntryCount);
}

/************************************************************************/

void BootMain(U32 BootDrive, U32 PartitionLba) {
    InitDebug();
    EnableA20();

    RetrieveMemoryMap();

    BootDebugPrint(
        TEXT("[VBR] Loading and running binary OS at %08X\r\n"),
        KERNEL_LINEAR_LOAD_ADDRESS);

    char Ext2KernelName[32];
    BuildKernelExt2Name(Ext2KernelName, sizeof(Ext2KernelName));

    U32 FileSize = 0;
    const char* LoadedFs = NULL;
    U32 KernelReservedBytes = 0;

    if (LoadKernelFat32(BootDrive, PartitionLba, KERNEL_FILE, &FileSize)) {
        LoadedFs = "FAT32";
    } else if (LoadKernelExt2(BootDrive, PartitionLba, Ext2KernelName, &FileSize)) {
        LoadedFs = "EXT2";
    } else {
        BootErrorPrint(TEXT("[VBR] Unsupported filesystem detected. Halting.\r\n"));
        Hang();
    }

    BootDebugPrint(TEXT("[VBR] Kernel loaded via %s\r\n"), LoadedFs);

    VerifyKernelImage(FileSize);
    KernelReservedBytes = ComputeKernelReservedBytes(FileSize);

    BootDebugPrint(TEXT("[VBR] Calling architecture specific boot code\r\n"));

    BOOT_FRAMEBUFFER_INFO FramebufferInfo;
    BOOT_CONFIG_TABLE_INFO ConfigTableInfo;
    MemorySet(&FramebufferInfo, 0, sizeof(FramebufferInfo));
    MemorySet(&ConfigTableInfo, 0, sizeof(ConfigTableInfo));
    FramebufferInfo.Type = MULTIBOOT_FRAMEBUFFER_TEXT;
    FramebufferInfo.Address = U64_Make(0u, 0x000B8000u);
    FramebufferInfo.Pitch = 80U * 2U;
    FramebufferInfo.Width = 80U;
    FramebufferInfo.Height = 25U;
    FramebufferInfo.BitsPerPixel = 16U;
    if (BootQueryTextCursorPosition(&ConfigTableInfo.ConsoleCursorX, &ConfigTableInfo.ConsoleCursorY) != FALSE) {
        ConfigTableInfo.HasConsoleCursor = TRUE;
    }

    U32 MultibootInfoPtr = BootBuildMultibootInfo(
        &MultibootInfo,
        MultibootMemMap,
        &KernelModule,
        &MultibootConfigTable,
        E820_Map,
        E820_EntryCount,
        KERNEL_LINEAR_LOAD_ADDRESS,
        FileSize,
        KernelReservedBytes,
        (LPCSTR)BootloaderName,
        (LPCSTR)KernelCmdLine,
        &FramebufferInfo,
        &ConfigTableInfo);

    const U64 UefiImageBase = U64_0;
    const U64 UefiImageSize = U64_0;
    EnterProtectedPagingAndJump(FileSize, MultibootInfoPtr, UefiImageBase, UefiImageSize);

    Hang();
}
