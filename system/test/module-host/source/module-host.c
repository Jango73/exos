
/************************************************************************\

    EXOS Test program - Module Host
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


    Module Host - Test program for executable module loading

\************************************************************************/

#include "../../../../runtime/include/exos-runtime.h"
#include "../../../../runtime/include/exos.h"

/***************************************************************************/

typedef UINT (*MODULE_SAMPLE_ADD)(UINT Value);
typedef UINT (*MODULE_SAMPLE_INCREMENT_SHARED)(void);
typedef UINT (*MODULE_SAMPLE_GET_SHARED)(void);
typedef UINT (*MODULE_IMPORT_RESOLVE)(void);

/***************************************************************************/

#define MODULE_HOST_FAILURE(...) \
    do {                         \
        printf(__VA_ARGS__);     \
        debug(__VA_ARGS__);      \
    } while (0)

/***************************************************************************/

static MODULE_SAMPLE_ADD SharedModuleSampleAdd = NULL;
static MODULE_SAMPLE_INCREMENT_SHARED SharedModuleSampleIncrementShared = NULL;
static volatile UINT LateTlsReady = 0;
static volatile UINT LateTlsDone = 0;
static volatile UINT LateTlsResult = 0;
static volatile UINT SharedDataReady = 0;
static volatile UINT SharedDataDone = 0;
static volatile UINT SharedDataResult = 0;

/***************************************************************************/

static HANDLE LoadModuleWithStatus(LPCSTR Path, UINT* StatusOut, UINT* StageOut) {
    MODULE_LOAD_INFO ModuleInfo;
    UINT Status;

    memset(&ModuleInfo, 0, sizeof(ModuleInfo));
    ModuleInfo.Header.Size = sizeof(ModuleInfo);
    ModuleInfo.Header.Version = EXOS_ABI_VERSION;
    ModuleInfo.Header.Flags = 0;
    ModuleInfo.Path = Path;
    ModuleInfo.Module = 0;
    ModuleInfo.Flags = 0;
    ModuleInfo.ModuleIdentifierHigh = 0;
    ModuleInfo.ModuleIdentifierLow = 0;

    Status = (UINT)exoscall(SYSCALL_LoadModule, EXOS_PARAM(&ModuleInfo));
    if (StatusOut != NULL) {
        *StatusOut = Status;
    }
    if (StageOut != NULL) {
        *StageOut = ModuleInfo.Flags;
    }

    if (Status != DF_RETURN_SUCCESS) {
        return 0;
    }

    return ModuleInfo.Module;
}

/***************************************************************************/

static LPVOID GetModuleSymbolWithStatus(HANDLE Module, LPCSTR Name, UINT* StatusOut) {
    MODULE_SYMBOL_INFO ModuleInfo;
    UINT Status;

    memset(&ModuleInfo, 0, sizeof(ModuleInfo));
    ModuleInfo.Header.Size = sizeof(ModuleInfo);
    ModuleInfo.Header.Version = EXOS_ABI_VERSION;
    ModuleInfo.Header.Flags = 0;
    ModuleInfo.Module = Module;
    ModuleInfo.Name = Name;
    ModuleInfo.Address = 0;

    Status = (UINT)exoscall(SYSCALL_GetModuleSymbol, EXOS_PARAM(&ModuleInfo));
    if (StatusOut != NULL) {
        *StatusOut = Status;
    }

    if (Status != DF_RETURN_SUCCESS) {
        return NULL;
    }

    return (LPVOID)ModuleInfo.Address;
}

/***************************************************************************/

static BOOL WaitForFlag(volatile UINT* Flag, UINT TimeoutMilliseconds) {
    UINT StartTime;

    StartTime = GetSystemTime();
    while (*Flag == 0) {
        if (GetSystemTime() - StartTime >= TimeoutMilliseconds) {
            return FALSE;
        }
        Sleep(10);
    }

    return TRUE;
}

/***************************************************************************/

static void LateTlsWorker(void* Parameter) {
    UNUSED(Parameter);

    if (!WaitForFlag(&LateTlsReady, 5000) || SharedModuleSampleAdd == NULL) {
        LateTlsResult = 0;
    } else {
        LateTlsResult = SharedModuleSampleAdd(0x23);
    }

    LateTlsDone = 1;
}

/***************************************************************************/

static void SharedDataWorker(void* Parameter) {
    UNUSED(Parameter);

    if (!WaitForFlag(&SharedDataReady, 5000) || SharedModuleSampleIncrementShared == NULL) {
        SharedDataResult = 0;
    } else {
        SharedDataResult = SharedModuleSampleIncrementShared();
    }

    SharedDataDone = 1;
}

/***************************************************************************/

static BOOL WaitForChildProcess(void) {
    PROCESS_INFO ProcessInfo;
    WAIT_INFO WaitInfo;
    U32 WaitResult;
    U32 StartTime;
    U32 ObjectHandle;
    U32 Index;

    memset(&ProcessInfo, 0, sizeof(ProcessInfo));
    ProcessInfo.Header.Size = sizeof(ProcessInfo);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    strcpy((char*)ProcessInfo.CommandLine, "/system/apps/test/module-host --child");
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;

    if (!ExosIsSuccess((UINT)exoscall(SYSCALL_CreateProcess, EXOS_PARAM(&ProcessInfo)))) {
        MODULE_HOST_FAILURE("Module child process creation failed\n");
        return FALSE;
    }

    memset(&WaitInfo, 0, sizeof(WaitInfo));
    WaitInfo.Header.Size = sizeof(WaitInfo);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.MilliSeconds = 1000;
    WaitInfo.Flags = WAIT_FLAG_ANY;

    StartTime = GetSystemTime();
    WaitResult = WAIT_INVALID_PARAMETER;
    for (Index = 0; Index < 2; Index++) {
        ObjectHandle = (Index == 0) ? (U32)ProcessInfo.Process : (U32)ProcessInfo.Task;
        WaitInfo.Objects[0] = (HANDLE)ObjectHandle;

        while (GetSystemTime() - StartTime < 10000) {
            WaitResult = Wait(&WaitInfo);
            if (WaitResult == WAIT_INVALID_PARAMETER) {
                Sleep(10);
                continue;
            }

            if (WaitResult == WAIT_TIMEOUT) {
                break;
            }

            if (WaitResult == WAIT_OBJECT_0 && WaitInfo.ExitCodes[0] == 0) {
                return TRUE;
            }

            MODULE_HOST_FAILURE("Module child process failed wait=%u exit=%u object=%p\n",
                                WaitResult,
                                WaitInfo.ExitCodes[0],
                                (HANDLE)ObjectHandle);
            return FALSE;
        }
    }

    MODULE_HOST_FAILURE("Module child process wait failed wait=%u process=%p task=%p\n",
                        WaitResult,
                        ProcessInfo.Process,
                        ProcessInfo.Task);
    return FALSE;
}

/***************************************************************************/

static BOOL RunChildModuleTest(void) {
    HANDLE Module;
    MODULE_SAMPLE_ADD ModuleSampleAdd;
    MODULE_SAMPLE_INCREMENT_SHARED ModuleSampleIncrementShared;
    UINT LoadStatus = 0;
    UINT LoadStage = 0;
    UINT SymbolStatus = 0;

    Module = LoadModuleWithStatus((LPCSTR)"/system/apps/test/module-sample", &LoadStatus, &LoadStage);
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module child load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbolWithStatus(Module, (LPCSTR)"ModuleSampleAdd", &SymbolStatus);
    if (ModuleSampleAdd == NULL) {
        MODULE_HOST_FAILURE("Module child symbol lookup failed name=ModuleSampleAdd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleIncrementShared =
        (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbolWithStatus(Module, (LPCSTR)"ModuleSampleIncrementShared", &SymbolStatus);
    if (ModuleSampleIncrementShared == NULL) {
        MODULE_HOST_FAILURE("Module child symbol lookup failed name=ModuleSampleIncrementShared status=%u\n", SymbolStatus);
        return FALSE;
    }

    if (ModuleSampleAdd(0x23) != 0x2B) {
        MODULE_HOST_FAILURE("Module child TLS call failed\n");
        return FALSE;
    }

    if (ModuleSampleIncrementShared() != 1) {
        MODULE_HOST_FAILURE("Module child global data call failed\n");
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

static BOOL RunParentModuleTest(void) {
    HANDLE Module;
    HANDLE ImportModule;
    MODULE_SAMPLE_ADD ModuleSampleAdd;
    MODULE_SAMPLE_INCREMENT_SHARED ModuleSampleIncrementShared;
    MODULE_SAMPLE_GET_SHARED ModuleSampleGetShared;
    MODULE_IMPORT_RESOLVE ModuleImportResolve;
    UINT Result;
    UINT LoadStatus = 0;
    UINT LoadStage = 0;
    UINT SymbolStatus = 0;
    int LateThread;
    int SharedThread;

    LateThread = _beginthread(LateTlsWorker, 65536, NULL);
    if (LateThread == 0) {
        MODULE_HOST_FAILURE("Module late TLS worker creation failed\n");
        return FALSE;
    }

    printf("Module host starting...\n");

    Module = LoadModuleWithStatus((LPCSTR)"/system/apps/test/module-sample", &LoadStatus, &LoadStage);
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbolWithStatus(Module, (LPCSTR)"ModuleSampleAdd", &SymbolStatus);
    if (ModuleSampleAdd == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleAdd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleIncrementShared =
        (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbolWithStatus(Module, (LPCSTR)"ModuleSampleIncrementShared", &SymbolStatus);
    if (ModuleSampleIncrementShared == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleIncrementShared status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleGetShared = (MODULE_SAMPLE_GET_SHARED)GetModuleSymbolWithStatus(Module, (LPCSTR)"ModuleSampleGetShared", &SymbolStatus);
    if (ModuleSampleGetShared == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleGetShared status=%u\n", SymbolStatus);
        return FALSE;
    }

    SharedModuleSampleAdd = ModuleSampleAdd;
    LateTlsReady = 1;
    if (!WaitForFlag(&LateTlsDone, 5000) || LateTlsResult != 0x2B) {
        MODULE_HOST_FAILURE("Module late TLS failed result=%u\n", LateTlsResult);
        return FALSE;
    }

    Result = ModuleSampleAdd(0x23);
    printf("Module result: %u\n", Result);
    if (Result != 0x2B || ModuleSampleAdd(0x23) != 0x2C) {
        MODULE_HOST_FAILURE("Module main TLS sequence failed\n");
        return FALSE;
    }

    if (ModuleSampleIncrementShared() != 1 || ModuleSampleIncrementShared() != 2) {
        MODULE_HOST_FAILURE("Module main global data sequence failed\n");
        return FALSE;
    }

    SharedModuleSampleIncrementShared = ModuleSampleIncrementShared;
    SharedThread = _beginthread(SharedDataWorker, 65536, NULL);
    if (SharedThread == 0) {
        MODULE_HOST_FAILURE("Module shared data worker creation failed\n");
        return FALSE;
    }

    SharedDataReady = 1;
    if (!WaitForFlag(&SharedDataDone, 5000) || SharedDataResult != 3 || ModuleSampleGetShared() != 3) {
        MODULE_HOST_FAILURE("Module task global data failed result=%u total=%u\n", SharedDataResult, ModuleSampleGetShared());
        return FALSE;
    }

    ImportModule = LoadModuleWithStatus((LPCSTR)"/system/apps/test/module-import", &LoadStatus, &LoadStage);
    if (ImportModule == 0) {
        MODULE_HOST_FAILURE("Module import load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleImportResolve = (MODULE_IMPORT_RESOLVE)GetModuleSymbolWithStatus(ImportModule, (LPCSTR)"ModuleImportResolve", &SymbolStatus);
    if (ModuleImportResolve == NULL) {
        MODULE_HOST_FAILURE("Module import resolver failed name=ModuleImportResolve status=%u\n", SymbolStatus);
        return FALSE;
    }

    if (ModuleImportResolve() != 0x123D) {
        MODULE_HOST_FAILURE("Module import resolver failed wrong-result\n");
        return FALSE;
    }

    if (!WaitForChildProcess()) {
        return FALSE;
    }

    debug("module-host success\n");
    return TRUE;
}

/***************************************************************************/

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        return RunChildModuleTest() ? 0 : 1;
    }

    return RunParentModuleTest() ? 0 : 1;
}
