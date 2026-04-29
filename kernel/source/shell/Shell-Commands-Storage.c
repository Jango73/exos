
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Shell commands

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "shell/Shell-Embedded-Scripts.h"
#include "utils/SizeFormat.h"

/***************************************************************************/

/**
 * @brief Print one shell line with an auto-scaled byte size.
 * @param Label Left column label.
 * @param ByteCount Size in bytes.
 */
static void ShellPrintByteSizeLine(LPCSTR Label, U64 ByteCount) {
    STR SizeText[32];

    SizeFormatBytesText(ByteCount, SizeText);
    ConsolePrint(TEXT("%s: %s\n"), Label, SizeText);
}

/***************************************************************************/
U32 CMD_sysinfo(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    SYSTEM_INFO Info;

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    DoSystemCall(SYSCALL_GetSystemInfo, SYSCALL_PARAM(&Info));

    ShellPrintByteSizeLine(TEXT("Total physical memory     "), Info.TotalPhysicalMemory);
    ShellPrintByteSizeLine(TEXT("Physical memory used      "), Info.PhysicalMemoryUsed);
    ShellPrintByteSizeLine(TEXT("Physical memory available "), Info.PhysicalMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Total swap memory         "), Info.TotalSwapMemory);
    ShellPrintByteSizeLine(TEXT("Swap memory used          "), Info.SwapMemoryUsed);
    ShellPrintByteSizeLine(TEXT("Swap memory available     "), Info.SwapMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Total memory available    "), Info.TotalMemoryAvail);
    ShellPrintByteSizeLine(TEXT("Processor page size       "), U64_FromUINT(Info.PageSize));
    ConsolePrint(TEXT("Total physical pages      : %u pages\n"), Info.TotalPhysicalPages);
    ConsolePrint(TEXT("Minimum linear address    : %x\n"), Info.MinimumLinearAddress);
    ConsolePrint(TEXT("Maximum linear address    : %x\n"), Info.MaximumLinearAddress);
    ConsolePrint(TEXT("User name                 : %s\n"), Info.UserName);
    ConsolePrint(TEXT("Number of processes       : %d\n"), Info.NumProcesses);
    ConsolePrint(TEXT("Number of tasks           : %d\n"), Info.NumTasks);
    ConsolePrint(TEXT("Keyboard layout           : %s\n"), Info.KeyboardLayout);

    TEST(TEXT("sys_info : OK"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_type(LPSHELLCONTEXT Context) {
    FILE_OPEN_INFO FileOpenInfo;
    FILE_OPERATION FileOperation;
    STR FileName[MAX_PATH_NAME];
    HANDLE Handle;
    U32 FileSize;
    U8* Buffer;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        if (QualifyFileName(Context, Context->Command, FileName)) {
            FileOpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
            FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
            FileOpenInfo.Header.Flags = 0;
            FileOpenInfo.Name = FileName;
            FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

            Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));

            if (Handle) {
                FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));

                if (FileSize) {
                    Buffer = (U8*)AllocatorAlloc(&Context->Allocator, FileSize + 1);

                    if (Buffer) {
                        FileOperation.Header.Size = sizeof(FILE_OPERATION);
                        FileOperation.Header.Version = EXOS_ABI_VERSION;
                        FileOperation.Header.Flags = 0;
                        FileOperation.File = Handle;
                        FileOperation.NumBytes = FileSize;
                        FileOperation.Buffer = Buffer;

                        if (DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation))) {
                            Buffer[FileSize] = STR_NULL;
                            ConsolePrint((LPSTR)Buffer);
                            Success = TRUE;
                        }

                        AllocatorFree(&Context->Allocator, Buffer);
                    }
                }
                DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
            }
        }
    }

    if (Success) {
        TEST(TEXT("type %s : OK"), FileName);
    } else {
        TEST(TEXT("type : KO"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_copy(LPSHELLCONTEXT Context) {
    STR SrcName[MAX_PATH_NAME];
    STR DstName[MAX_PATH_NAME];
    LPVOID SourceBytes = NULL;
    UINT FileSize = 0;
    UINT TotalCopied = 0;
    BOOL Success = FALSE;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, SrcName) == 0) return DF_RETURN_SUCCESS;

    ParseNextCommandLineComponent(Context);
    if (QualifyFileName(Context, Context->Command, DstName) == 0) return DF_RETURN_SUCCESS;

    ConsolePrint(TEXT("%s %s\n"), SrcName, DstName);

    SourceBytes = FileReadAll(SrcName, &FileSize);
    if (SourceBytes != NULL) {
        TotalCopied = FileWriteAll(DstName, SourceBytes, FileSize);
        KernelHeapFree(SourceBytes);
    }

    Success = (TotalCopied == FileSize);
    DEBUG(TEXT("TotalCopied=%u FileSize=%u"), TotalCopied, FileSize);

    if (Success) {
        TEST(TEXT("copy %s %s : OK"), SrcName, DstName);
    } else {
        TEST(TEXT("copy %s %s : KO"), SrcName, DstName);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_edit(LPSHELLCONTEXT Context) {
    LPSTR Arguments[2];
    STR FileName[MAX_PATH_NAME];
    BOOL HasArgument = FALSE;
    BOOL ArgumentProvided = FALSE;
    BOOL LineNumbers;

    FileName[0] = STR_NULL;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        ArgumentProvided = TRUE;
        if (QualifyFileName(Context, Context->Command, FileName)) {
            Arguments[0] = FileName;
            HasArgument = TRUE;
        }
    }

    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }

    LineNumbers = HasOption(Context, TEXT("n"), TEXT("line_numbers"));

    if (HasArgument) {
        Edit(1, (LPCSTR*)Arguments, LineNumbers);
    } else if (!ArgumentProvided) {
        Edit(0, NULL, LineNumbers);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_disk(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: disk list\n"));
        return DF_RETURN_SUCCESS;
    }

    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_DISK_LIST));
}

/***************************************************************************/

U32 CMD_filesystem(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: fs list\n"));
        return DF_RETURN_SUCCESS;
    }

    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_FILE_SYSTEM_LIST));
}

/***************************************************************************/
