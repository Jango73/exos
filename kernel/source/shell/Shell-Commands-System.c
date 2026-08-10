
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

#include "autotest/Autotest.h"
#include "shell/Shell-Commands-Private.h"
#include "shell/Shell-Embedded-Scripts.h"
#include "text/Text.h"
#include "utils/ProcessAccess.h"
#include "utils/SizeFormat.h"
#include "network/ICMP.h"

/************************************************************************/

/**
 * @brief Run the embedded driver detail script for one alias.
 * @param Context Shell context.
 * @param Alias Driver alias.
 * @return `DF_RETURN_*` status code.
 */
static UINT RunEmbeddedDriverDetailsScript(LPSHELLCONTEXT Context, LPCSTR Alias) {
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

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: task list\n"));
        return DF_RETURN_SUCCESS;
    }

    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_TASK_LIST));
}

/************************************************************************/

U32 CMD_memEdit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

#define MEMORY_MAP_MAX_PROCESSES 32

/************************************************************************/

/**
 * @brief Return the display name of a process privilege level.
 * @param Process Process to classify.
 * @return Privilege display name.
 */
static LPCSTR MemoryMapGetPrivilegeName(LPPROCESS Process) {
    if (ProcessAccessIsKernelProcess(Process)) {
        return TEXT("kernel");
    }

    if (ProcessAccessIsAdministratorProcess(Process)) {
        return TEXT("admin");
    }

    return TEXT("user");
}

/************************************************************************/

/**
 * @brief Print one process memory carving snapshot header.
 * @param Process Process being reported.
 * @param Snapshot Carving snapshot.
 */
static void MemoryMapPrintProcessHeader(LPPROCESS Process, LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot) {
    STR SizeText[MAX_STRING_BUFFER];
    LPCSTR ProcessName = TEXT("<kernel>");

    if (Snapshot != NULL && StringLength(Snapshot->FileName) != 0) {
        ProcessName = Snapshot->FileName;
    }

    ConsolePrint(TEXT("Process : %s (%s)\n"), ProcessName, MemoryMapGetPrivilegeName(Process));
    ConsolePrint(TEXT("  Page directory : %p\n"), (LPVOID)Snapshot->PageDirectory);

    SizeFormatBytesText(U64_FromUINT(Snapshot->HeapSize), SizeText);
    ConsolePrint(
        TEXT("  Heap : [%p, %p)  size : %s\n"),
        (LPVOID)Snapshot->HeapBase,
        (LPVOID)(Snapshot->HeapBase + Snapshot->HeapSize),
        SizeText);

    ConsolePrint(
        TEXT("  Address space : %s  carved partition : %s\n"),
        Snapshot->AddressSpaceInitialized ? TEXT("initialized") : TEXT("not initialized"),
        Snapshot->ArenasCarved ? TEXT("yes") : TEXT("no"));
}

/************************************************************************/

/**
 * @brief Print the arena ranges of one carving snapshot.
 * @param Snapshot Carving snapshot.
 */
static void MemoryMapPrintArenas(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot) {
    for (UINT ArenaIndex = 0; ArenaIndex < PROCESS_ARENA_COUNT; ArenaIndex++) {
        LPPROCESS_ARENA_RANGE Range = &(Snapshot->Arenas[ArenaIndex]);

        if (Range->Limit == 0) {
            continue;
        }

        ConsolePrint(
            TEXT("  Arena %-8s : [%p, %p)  low=%p  high=%p\n"),
            MemoryAnalysisGetArenaName(ArenaIndex),
            (LPVOID)Range->Base,
            (LPVOID)Range->Limit,
            (LPVOID)Range->NextLow,
            (LPVOID)Range->NextHigh);
    }
}

/************************************************************************/

/**
 * @brief Print the memory region descriptors of one carving snapshot.
 * @param Snapshot Carving snapshot.
 */
static void MemoryMapPrintRegions(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot) {
    ConsolePrint(TEXT("  Regions : %u\n"), Snapshot->RegionCount);

    for (UINT RegionIndex = 0; RegionIndex < Snapshot->RegionCount; RegionIndex++) {
        LPMEMORY_CARVING_REGION Region = &(Snapshot->Regions[RegionIndex]);

        ConsolePrint(
            TEXT("    [%p, %p)  size=%u  pages=%u  attr=%x  tag=%s\n"),
            (LPVOID)Region->Base,
            (LPVOID)Region->Limit,
            Region->Size,
            Region->PageCount,
            Region->Attributes,
            Region->Tag);
    }
}

/************************************************************************/

/**
 * @brief Print the task stacks of one carving snapshot.
 * @param Snapshot Carving snapshot.
 */
static void MemoryMapPrintStacks(LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot) {
    ConsolePrint(TEXT("  Stacks : %u\n"), Snapshot->StackCount);

    for (UINT StackIndex = 0; StackIndex < Snapshot->StackCount; StackIndex++) {
        LPMEMORY_CARVING_STACK Stack = &(Snapshot->Stacks[StackIndex]);

        ConsolePrint(
            TEXT("    [%p, %p)  size=%u  kind=%s  task=%s\n"),
            (LPVOID)Stack->Base,
            (LPVOID)Stack->Limit,
            Stack->Size,
            MemoryAnalysisGetStackKindName(Stack->Kind),
            Stack->TaskName);
    }
}

/************************************************************************/

/**
 * @brief Print the carving issues of one analysis report.
 * @param Report Carving analysis report.
 */
static void MemoryMapPrintIssues(LPMEMORY_CARVING_REPORT Report) {
    LPCSTR SeverityName = TEXT("info");
    UINT ErrorCount = MemoryAnalysisCountSeverity(Report, MEMORY_CARVING_SEVERITY_ERROR);
    UINT WarningCount = MemoryAnalysisCountSeverity(Report, MEMORY_CARVING_SEVERITY_WARNING);
    UINT InfoCount = MemoryAnalysisCountSeverity(Report, MEMORY_CARVING_SEVERITY_INFO);

    ConsolePrint(
        TEXT("  Carving issues : %u errors, %u warnings, %u info  (overlaps : %u)\n"),
        ErrorCount,
        WarningCount,
        InfoCount,
        Report->OverlapCount);

    for (UINT IssueIndex = 0; IssueIndex < Report->IssueCount; IssueIndex++) {
        LPMEMORY_CARVING_ISSUE Issue = &(Report->Issues[IssueIndex]);

        switch (Issue->Severity) {
            case MEMORY_CARVING_SEVERITY_ERROR:
                SeverityName = TEXT("error");
                break;
            case MEMORY_CARVING_SEVERITY_WARNING:
                SeverityName = TEXT("warning");
                break;
            default:
                SeverityName = TEXT("info");
                break;
        }

        ConsolePrint(TEXT("    %s : %s\n"), SeverityName, Issue->Text);
    }
}

/************************************************************************/

/**
 * @brief Print the heap fragmentation assessment of one process.
 * @param Process Process being reported.
 * @param Snapshot Carving snapshot.
 */
static void MemoryMapPrintHeapFragmentation(LPPROCESS Process, LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot) {
    STR SizeText[MAX_STRING_BUFFER];
    HEAP_FRAGMENTATION_INFO Fragmentation;

    if (Process == NULL || Snapshot == NULL || Snapshot->AddressSpaceInitialized == FALSE || Snapshot->HeapSize == 0) {
        return;
    }

    if (HeapQueryFragmentation(Process, &Fragmentation) == FALSE) {
        ConsolePrint(TEXT("  Heap fragmentation : unavailable\n"));
        return;
    }

    SizeFormatBytesText(U64_FromUINT(Fragmentation.LargestFreeBlock), SizeText);
    ConsolePrint(
        TEXT("  Heap fragmentation : freeBlocks=%u  largest=%s  freeBytes=%u  totalBytes=%u  fragmentation=%u%%\n"),
        Fragmentation.FreeBlockCount,
        SizeText,
        Fragmentation.FreeBytes,
        Fragmentation.TotalBytes,
        Fragmentation.FragmentationPercent);
}

/************************************************************************/

U32 CMD_memorymap(LPSHELLCONTEXT Context) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    LPPROCESS ProcessArray[MEMORY_MAP_MAX_PROCESSES];
    LPPROCESS_MEMORY_CARVING_SNAPSHOT Snapshot = NULL;
    LPMEMORY_CARVING_REPORT Report = NULL;
    LPLIST ProcessList = NULL;
    UINT ProcessCount = 0;
    BOOL ShowHeap = FALSE;

    if (ProcessAccessIsKernelProcess(CurrentProcess) == FALSE &&
        ProcessAccessIsAdministratorProcess(CurrentProcess) == FALSE) {
        ConsolePrint(TEXT("Access denied : memoryMap requires kernel or administrator privilege\n"));
        DEBUG(TEXT("[CMD_memorymap] Access denied for non privileged caller"));
        return DF_RETURN_NO_PERMISSION;
    }

    ParseNextCommandLineComponent(Context);
    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }
    ShowHeap = HasOption(Context, TEXT("h"), TEXT("heap"));

    Snapshot = (LPPROCESS_MEMORY_CARVING_SNAPSHOT)AllocatorAlloc(&Context->Allocator, sizeof(*Snapshot));
    Report = (LPMEMORY_CARVING_REPORT)AllocatorAlloc(&Context->Allocator, sizeof(*Report));
    if (Snapshot == NULL || Report == NULL) {
        if (Snapshot != NULL) {
            AllocatorFree(&Context->Allocator, Snapshot);
        }
        ConsolePrint(TEXT("memoryMap : out of memory\n"));
        return DF_RETURN_NO_MEMORY;
    }

    MemorySet(ProcessArray, 0, sizeof(ProcessArray));

    ProcessList = GetProcessList();
    if (ProcessList != NULL) {
        LockMutex(MUTEX_PROCESS, INFINITY);

        for (LPLISTNODE Node = ProcessList->First; Node != NULL && ProcessCount < MEMORY_MAP_MAX_PROCESSES;
             Node = Node->Next) {
            LPPROCESS Process = (LPPROCESS)Node;

            SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
                ProcessArray[ProcessCount] = Process;
                ProcessCount++;
            }
        }

        UnlockMutex(MUTEX_PROCESS);
    }

    for (UINT ProcessIndex = 0; ProcessIndex < ProcessCount; ProcessIndex++) {
        LPPROCESS Process = ProcessArray[ProcessIndex];

        if (ProcessSnapshotMemoryCarving(Process, Snapshot) == FALSE) {
            continue;
        }

        Snapshot->StackCount = TaskSnapshotStacksForProcess(Process, Snapshot->Stacks, MEMORY_CARVING_MAX_STACKS);
        MemoryAnalysisAnalyzeCarving(Snapshot, Report);

        MemoryMapPrintProcessHeader(Process, Snapshot);
        MemoryMapPrintArenas(Snapshot);
        MemoryMapPrintRegions(Snapshot);
        MemoryMapPrintStacks(Snapshot);
        MemoryMapPrintIssues(Report);

        if (ShowHeap) {
            MemoryMapPrintHeapFragmentation(Process, Snapshot);
        }
    }

    AllocatorFree(&Context->Allocator, Snapshot);
    AllocatorFree(&Context->Allocator, Report);

    TEST(TEXT("memoryMap : OK"));

    return DF_RETURN_SUCCESS;
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

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("devices")) != 0) {
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
    DEBUG(TEXT("%s"), Buffer);
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

U32 CMD_profiling(LPSHELLCONTEXT Context) {
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

    PrintProfileDumpLine(
        TEXT("entries=%u total_entries=%u samples=%u dropped=%u%s"),
        Query.EntryCount,
        Query.TotalEntryCount,
        Query.SampleCount,
        Query.DroppedCount,
        (Query.Flags & PROFILE_QUERY_FLAG_RESET) != 0 ? TEXT(" reset=yes") : TEXT(""));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Print one task statistics snapshot entry.
 * @param Entry Snapshot entry to print.
 */
static void PrintTaskProfileEntry(LPTASK_PROFILE_ENTRY_INFO Entry) {
    if (Entry == NULL) {
        return;
    }

    PrintProfileDumpLine(
        TEXT("%-24s status=%u prio=%u quantum=%u used=%u max=%u run=%u sleep=%u wakeLat=%u disp=%u prempt=%u woken=%u"),
        Entry->Name,
        Entry->Status,
        Entry->Priority,
        Entry->QuantumGrantedMilliseconds,
        Entry->QuantumUsedMilliseconds,
        Entry->MaxQuantumUsedMilliseconds,
        Entry->TotalRunTimeMilliseconds,
        Entry->TotalSleepTimeMilliseconds,
        Entry->TotalWakeupLatencyMilliseconds,
        Entry->DispatchCount,
        Entry->PreemptionCount,
        Entry->WakeupCount);
}

/************************************************************************/

/**
 * @brief Show per-task scheduling statistics.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_taskStat(LPSHELLCONTEXT Context) {
    TASK_PROFILE_ENTRY_INFO Entries[TASK_PROFILE_MAX_ENTRIES];
    TASK_PROFILE_QUERY_INFO Query;
    UINT Result;

    MemorySet(Entries, 0, sizeof(Entries));
    MemorySet(&Query, 0, sizeof(Query));

    ParseNextCommandLineComponent(Context);

    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Header.Flags = 0;
    Query.Capacity = TASK_PROFILE_MAX_ENTRIES;
    Query.Flags = 0;
    Query.Entries = Entries;

    if (StringLength(Context->Command) != 0) {
        if (StringCompareNC(Context->Command, TEXT("reset")) == 0) {
            Query.Flags = TASK_PROFILE_QUERY_FLAG_RESET;
        } else {
            ConsolePrint(TEXT("Usage: taskStat [reset]\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    Result = DoSystemCall(SYSCALL_GetTaskProfileInfo, SYSCALL_PARAM(&Query));
    if (Result != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Task statistics snapshot unavailable.\n"));
        return Result;
    }

    if (Query.EntryCount == 0) {
        PrintProfileDumpLine(TEXT("No task statistics available."));
        return DF_RETURN_SUCCESS;
    }

    for (UINT Index = 0; Index < Query.EntryCount; ++Index) {
        PrintTaskProfileEntry(&Entries[Index]);
    }

    PrintProfileDumpLine(
        TEXT("tasks=%u dispatches=%u run=%u sleep=%u%s"),
        Query.TotalTaskCount,
        Query.TotalDispatchCount,
        Query.TotalRunTimeMilliseconds,
        Query.TotalSleepTimeMilliseconds,
        (Query.Flags & TASK_PROFILE_QUERY_FLAG_RESET) != 0 ? TEXT(" reset=yes") : TEXT(""));
    TEST(TEXT("taskStat : OK"));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run one on-demand autotest module.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
U32 CMD_autotest(LPSHELLCONTEXT Context) {
    U8 TestName[64];
    BOOL Result = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: autotest <testname>\n"));
        ListAllTests();
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("stack")) == 0) {
        StringCopy(TestName, TEXT("TestCopyStack"));
    } else if (StringCompareNC(Context->Command, TEXT("udp")) == 0) {
        StringCopy(TestName, TEXT("TestUDP"));
    } else if (StringCompareNC(Context->Command, TEXT("tcp")) == 0) {
        StringCopy(TestName, TEXT("TestTCP"));
    } else {
        StringCopy(TestName, Context->Command);
    }

    Result = RunSingleTestByName(TestName);

    if (Result) {
        ConsolePrint(TEXT("autotest %s: passed\n"), Context->Command);
    } else {
        ConsolePrint(TEXT("autotest %s: failed\n"), Context->Command);
        ERROR(TEXT("autotest %s failed"), Context->Command);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run the System Data View mode from the shell.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_dataView(LPSHELLCONTEXT Context) {
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

    if (StringLength(Context->Command) == 0 || (StringCompareNC(Context->Command, TEXT("ports")) != 0 &&
                                                StringCompareNC(Context->Command, TEXT("devices")) != 0 &&
                                                StringCompareNC(Context->Command, TEXT("deviceTree")) != 0 &&
                                                StringCompareNC(Context->Command, TEXT("drives")) != 0 &&
                                                StringCompareNC(Context->Command, TEXT("probe")) != 0)) {
        ConsolePrint(TEXT("Usage: usb ports|devices|deviceTree|drives|probe\n"));
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
    } else if (StringCompareNC(Context->Command, TEXT("deviceTree")) == 0) {
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

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: nvme list\n"));
        return DF_RETURN_SUCCESS;
    }
    return RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_NVME_LIST));
}

/************************************************************************/

U32 CMD_credits(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(Text_Credits);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

#define PING_DEFAULT_COUNT 4
#define PING_TIMEOUT_MILLISECONDS 2000
#define PING_POLL_INTERVAL_MILLISECONDS 20

/**
 * @brief Send ICMP echo requests to an IPv4 address.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_ping(LPSHELLCONTEXT Context) {
    STR TargetString[32];
    STR DisplayTarget[32];
    U32 TargetIP;
    U32 Count = PING_DEFAULT_COUNT;
    LPPCI_DEVICE Device;
    U32 PingIndex;
    U32 SentCount = 0;
    U32 ReceivedCount = 0;

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: ping IPv4Address [Count]\n"));
        return DF_RETURN_SUCCESS;
    }

    StringCopyLimit(DisplayTarget, Context->Command, sizeof(DisplayTarget));
    TargetIP = ParseIPAddress(Context->Command);
    if (TargetIP == 0) {
        ConsolePrint(TEXT("Invalid IPv4 address: %s\n"), DisplayTarget);
        return DF_RETURN_SUCCESS;
    }
    StringCopyLimit(TargetString, DisplayTarget, sizeof(TargetString));

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) != 0) {
        U32 ParsedCount = StringToU32(Context->Command);
        if (ParsedCount != 0 && ParsedCount <= 64) {
            Count = ParsedCount;
        }
    }

    Device = NetworkManager_GetPrimaryDevice();
    if (Device == NULL) {
        ConsolePrint(TEXT("No network device available\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!NetworkManager_IsDeviceReady((LPDEVICE)Device)) {
        ConsolePrint(TEXT("Network device is not ready\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Pinging %s with %u request(s)\n"), TargetString, Count);

    for (PingIndex = 0; PingIndex < Count; PingIndex++) {
        U16 Identifier;
        U16 SequenceNumber;
        U32 StartTick;
        ICMP_ECHO_STATUS Status = ICMP_ECHO_PENDING;

        if (!ICMP_StartEcho((LPDEVICE)Device, TargetIP, &Identifier, &SequenceNumber)) {
            ConsolePrint(TEXT("Ping request failed\n"));
            break;
        }
        SentCount++;

        StartTick = GetSystemTime();
        while ((GetSystemTime() - StartTick) < PING_TIMEOUT_MILLISECONDS) {
            U32 ReplySource;
            U32 RoundTripMilliseconds;

            Status = ICMP_CheckEcho(Identifier, SequenceNumber, &ReplySource, &RoundTripMilliseconds);
            if (Status == ICMP_ECHO_RECEIVED) {
                STR ReplyIpString[24];
                FormatIPv4(ReplySource, ReplyIpString);
                ConsolePrint(TEXT("Reply from %s: time=%ums\n"), ReplyIpString, RoundTripMilliseconds);
                ReceivedCount++;
                break;
            }
            Sleep(PING_POLL_INTERVAL_MILLISECONDS);
        }

        if (Status != ICMP_ECHO_RECEIVED) {
            ConsolePrint(TEXT("Request timed out\n"));
        }

        ICMP_CancelEcho(Identifier, SequenceNumber);
    }

    ConsolePrint(
        TEXT("Ping statistics: sent=%u received=%u lost=%u\n"), SentCount, ReceivedCount, SentCount - ReceivedCount);
    TEST(TEXT("ping %s sent=%u received=%u : OK"), TargetString, SentCount, ReceivedCount);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Resolve a host name to an IPv4 address through DNS.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */

U32 CMD_dnsresolve(LPSHELLCONTEXT Context) {
    STR DisplayName[DNS_RESOLVE_MAX_HOST_NAME_LENGTH + 1];
    STR StatusText[32];
    DNS_RESOLVE_INFO Info;

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: dnsresolve HostName\n"));
        return DF_RETURN_SUCCESS;
    }

    StringCopyLimit(DisplayName, Context->Command, sizeof(DisplayName));

    MemorySet(&Info, 0, sizeof(Info));
    Info.Header.Size = sizeof(Info);
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.TimeoutMillis = DNS_RESOLVE_DEFAULT_TIMEOUT_MILLISECONDS;
    StringCopyLimit(Info.Name, DisplayName, sizeof(Info.Name));

    ConsolePrint(TEXT("Resolving %s ...\n"), DisplayName);

    DoSystemCall(SYSCALL_DNSResolve, SYSCALL_PARAM(&Info));

    if (Info.Status == DNS_RESOLVE_STATUS_SUCCESS) {
        STR IpString[24];
        FormatIPv4(Info.IP_Be, IpString);

        ConsolePrint(TEXT("Resolved %s to %s\n"), DisplayName, IpString);
        TEST(TEXT("dnsresolve %s : OK"), DisplayName);
        return DF_RETURN_SUCCESS;
    }

    switch (Info.Status) {
        case DNS_RESOLVE_STATUS_TIMEOUT:
            StringCopy(StatusText, TEXT("timed out"));
            break;
        case DNS_RESOLVE_STATUS_NAME_ERROR:
            StringCopy(StatusText, TEXT("name error (NXDOMAIN)"));
            break;
        case DNS_RESOLVE_STATUS_NO_ANSWER:
            StringCopy(StatusText, TEXT("no A record"));
            break;
        case DNS_RESOLVE_STATUS_TRUNCATED:
            StringCopy(StatusText, TEXT("truncated response"));
            break;
        default:
            StringCopy(StatusText, TEXT("error"));
            break;
    }

    ConsolePrint(TEXT("Resolution of %s %s\n"), DisplayName, StatusText);
    TEST(TEXT("dnsresolve %s : %s"), DisplayName, StatusText);

    return DF_RETURN_SUCCESS;
}
