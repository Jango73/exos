
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
#include "shell/Shell-EmbeddedScripts.h"
#include "autotest/Autotest.h"
#include "utils/SizeFormat.h"

/************************************************************************/

/**
 * @brief Run the embedded driver detail script for one alias.
 * @param Context Shell context.
 * @param Alias Driver alias.
 * @return `DF_RETURN_*` status code.
 */
static UINT RunEmbeddedDriverDetailsScript(
    LPSHELLCONTEXT Context,
    LPCSTR Alias) {
    STR ScriptText[4096];

    if (Context == NULL || Alias == NULL || StringLength(Alias) == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringPrintFormat(
        ScriptText,
        TEXT("target_alias = \"%s\";\n%s"),
        Alias,
        ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_DRIVER_DETAILS));
    return RunEmbeddedScript(Context, ScriptText);
}

/************************************************************************/

/**
 * @brief Print one driver detail view selected by alias.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_driver(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: driver list\n"));
        ConsolePrint(TEXT("       driver Alias\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("list")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_DRIVER_LIST));
    }

    return RunEmbeddedDriverDetailsScript(Context, Context->Command);
}

/************************************************************************/

/**
 * @brief List the tasks visible to the current shell caller.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_task(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: task list\n"));
        return DF_RETURN_SUCCESS;
    }

    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_TASK_LIST));
}

/************************************************************************/

U32 CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_memorymap(LPSHELLCONTEXT Context) {
    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_MEMORY_MAP));
}

/************************************************************************/

U32 CMD_disasm(LPSHELLCONTEXT Context) {

    U32 Address = 0;
    U32 InstrCount = 0;
    STR Buffer[MAX_STRING_BUFFER];

    ParseNextCommandLineComponent(Context);
    Address = StringToU32(Context->Command);

    ParseNextCommandLineComponent(Context);
    InstrCount = StringToU32(Context->Command);

    if (Address != 0 && InstrCount > 0) {
        MemorySet(Buffer, 0, MAX_STRING_BUFFER);

        U32 NumBits = 32;
#if defined(__EXOS_ARCH_X86_64__)
        NumBits = 64;
#endif

        Disassemble(Buffer, Address, InstrCount, NumBits);
        ConsolePrint(Buffer);
    } else {
        ConsolePrint(TEXT("Missing parameter\n"));
    }


    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_network(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("devices")) != 0) {
        ConsolePrint(TEXT("Usage: network devices\n"));
        return DF_RETURN_SUCCESS;
    }

    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_NETWORK_DEVICES));
}

/************************************************************************/

U32 CMD_pic(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), KernelStartup.IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), KernelStartup.IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), KernelStartup.IRQMask_21_PM);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), KernelStartup.IRQMask_A1_PM);

    return DF_RETURN_SUCCESS;
}

U32 CMD_reboot(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Rebooting system...\n"));

    RebootKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shutdown command implementation.
 * @param Context Shell context.
 */
U32 CMD_shutdown(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Shutting down system...\n"));

    ShutdownKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Print one profiling dump line to the console and debug log.
 * @param Format Line format string.
 */
static void PrintProfileDumpLine(LPCSTR Format, ...) {
    STR Buffer[MAX_STRING_BUFFER];
    VarArgList Args;

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Buffer, Format, Args);
    VarArgEnd(Args);

    ConsolePrint(TEXT("%s\n"), Buffer);
    DEBUG(TEXT("[CMD_prof] %s"), Buffer);
}

/************************************************************************/

/**
 * @brief Print one profiling snapshot entry.
 * @param Entry Snapshot entry to print.
 */
static void PrintProfileEntry(LPPROFILE_ENTRY_INFO Entry) {
    UINT Average = 0;

    if (Entry == NULL) {
        return;
    }

    if (Entry->TimedCallCount > 0) {
        Average = Entry->TotalTicks / Entry->TimedCallCount;
    }

    PrintProfileDumpLine(
        TEXT("%-32s calls=%u timed=%u last=%u us avg=%u us max=%u us total=%u us"),
        Entry->Name,
        Entry->CallCount,
        Entry->TimedCallCount,
        Entry->LastTicks,
        Average,
        Entry->MaxTicks,
        Entry->TotalTicks);
}

/************************************************************************/

U32 CMD_prof(LPSHELLCONTEXT Context) {
    PROFILE_ENTRY_INFO Entries[PROFILE_MAX_ENTRIES];
    PROFILE_QUERY_INFO Query;
    UINT Result;

    MemorySet(Entries, 0, sizeof(Entries));
    MemorySet(&Query, 0, sizeof(Query));

    ParseNextCommandLineComponent(Context);

    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Header.Flags = 0;
    Query.Capacity = PROFILE_MAX_ENTRIES;
    Query.Flags = 0;
    Query.Entries = Entries;

    if (StringLength(Context->Command) != 0) {
        if (StringCompareNC(Context->Command, TEXT("reset")) == 0) {
            Query.Flags = PROFILE_QUERY_FLAG_RESET;
        } else {
            ConsolePrint(TEXT("Usage: prof [reset]\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    Result = DoSystemCall(SYSCALL_GetProfileInfo, SYSCALL_PARAM(&Query));
    if (Result != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Profiling snapshot unavailable.\n"));
        return Result;
    }

    if (Query.EntryCount == 0) {
        PrintProfileDumpLine(TEXT("No profiling samples available."));
        return DF_RETURN_SUCCESS;
    }

    for (UINT Index = 0; Index < Query.EntryCount; ++Index) {
        PrintProfileEntry(&Entries[Index]);
    }

    PrintProfileDumpLine(TEXT("entries=%u total_entries=%u samples=%u dropped=%u%s"),
                         Query.EntryCount,
                         Query.TotalEntryCount,
                         Query.SampleCount,
                         Query.DroppedCount,
                         (Query.Flags & PROFILE_QUERY_FLAG_RESET) != 0 ? TEXT(" reset=yes") : TEXT(""));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run one on-demand autotest module.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
U32 CMD_autotest(LPSHELLCONTEXT Context) {
    BOOL Result = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("stack")) != 0) {
        ConsolePrint(TEXT("Usage: autotest stack\n"));
        return DF_RETURN_SUCCESS;
    }

    Result = RunSingleTestByName(TEXT("TestCopyStack"));

    if (Result) {
        ConsolePrint(TEXT("autotest stack: passed\n"));
    } else {
        ConsolePrint(TEXT("autotest stack: failed\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run the System Data View mode from the shell.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_dataview(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    SystemDataViewMode();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief USB control command (xHCI port report).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_usb(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        (StringCompareNC(Context->Command, TEXT("ports")) != 0 &&
         StringCompareNC(Context->Command, TEXT("devices")) != 0 &&
         StringCompareNC(Context->Command, TEXT("device-tree")) != 0 &&
         StringCompareNC(Context->Command, TEXT("drives")) != 0 &&
         StringCompareNC(Context->Command, TEXT("probe")) != 0)) {
        ConsolePrint(TEXT("Usage: usb ports|devices|device-tree|drives|probe\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("drives")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DRIVES));
    } else if (StringCompareNC(Context->Command, TEXT("probe")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_PROBE));
    } else if (StringCompareNC(Context->Command, TEXT("devices")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DEVICES));
    } else if (StringCompareNC(Context->Command, TEXT("ports")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_PORTS));
    } else if (StringCompareNC(Context->Command, TEXT("device-tree")) == 0) {
        return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DEVICE_TREE));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief NVMe control command (device list).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_nvme(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: nvme list\n"));
        return DF_RETURN_SUCCESS;
    }
    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_NVME_LIST));
}
