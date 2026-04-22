
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

    memset(&ProcessInfo, 0, sizeof(ProcessInfo));
    ProcessInfo.Header.Size = sizeof(ProcessInfo);
    ProcessInfo.Header.Version = EXOS_ABI_VERSION;
    ProcessInfo.Header.Flags = 0;
    ProcessInfo.Flags = 0;
    strcpy((char*)ProcessInfo.CommandLine, "/system/apps/test/module-host --child");
    ProcessInfo.StdOut = NULL;
    ProcessInfo.StdIn = NULL;
    ProcessInfo.StdErr = NULL;

    if (exoscall(SYSCALL_CreateProcess, EXOS_PARAM(&ProcessInfo)) == 0) {
        MODULE_HOST_FAILURE("Module child process creation failed\n");
        return FALSE;
    }

    memset(&WaitInfo, 0, sizeof(WaitInfo));
    WaitInfo.Header.Size = sizeof(WaitInfo);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.MilliSeconds = 10000;
    WaitInfo.Flags = WAIT_FLAG_ANY;
    WaitInfo.Objects[0] = ProcessInfo.Task;

    WaitResult = Wait(&WaitInfo);
    if (WaitResult != WAIT_OBJECT_0 || WaitInfo.ExitCodes[0] != 0) {
        MODULE_HOST_FAILURE("Module child process failed wait=%u exit=%u\n", WaitResult, WaitInfo.ExitCodes[0]);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

static BOOL RunChildModuleTest(void) {
    HANDLE Module;
    MODULE_SAMPLE_ADD ModuleSampleAdd;
    MODULE_SAMPLE_INCREMENT_SHARED ModuleSampleIncrementShared;

    Module = LoadModule((LPCSTR)"/system/apps/test/module-sample");
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module child load failed\n");
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbol(Module, (LPCSTR)"ModuleSampleAdd");
    ModuleSampleIncrementShared = (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbol(Module, (LPCSTR)"ModuleSampleIncrementShared");
    if (ModuleSampleAdd == NULL || ModuleSampleIncrementShared == NULL) {
        MODULE_HOST_FAILURE("Module child symbol lookup failed\n");
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
    int LateThread;
    int SharedThread;

    LateThread = _beginthread(LateTlsWorker, 65536, NULL);
    if (LateThread == 0) {
        MODULE_HOST_FAILURE("Module late TLS worker creation failed\n");
        return FALSE;
    }

    printf("Module host starting...\n");

    Module = LoadModule((LPCSTR)"/system/apps/test/module-sample");
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module load failed\n");
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbol(Module, (LPCSTR)"ModuleSampleAdd");
    ModuleSampleIncrementShared = (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbol(Module, (LPCSTR)"ModuleSampleIncrementShared");
    ModuleSampleGetShared = (MODULE_SAMPLE_GET_SHARED)GetModuleSymbol(Module, (LPCSTR)"ModuleSampleGetShared");
    if (ModuleSampleAdd == NULL || ModuleSampleIncrementShared == NULL || ModuleSampleGetShared == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed\n");
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

    ImportModule = LoadModule((LPCSTR)"/system/apps/test/module-import");
    if (ImportModule == 0) {
        MODULE_HOST_FAILURE("Module import load failed\n");
        return FALSE;
    }

    ModuleImportResolve = (MODULE_IMPORT_RESOLVE)GetModuleSymbol(ImportModule, (LPCSTR)"ModuleImportResolve");
    if (ModuleImportResolve == NULL || ModuleImportResolve() != 0x123D) {
        MODULE_HOST_FAILURE("Module import resolver failed\n");
        return FALSE;
    }

    if (!WaitForChildProcess()) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "--child") == 0) {
        return RunChildModuleTest() ? 0 : 1;
    }

    return RunParentModuleTest() ? 0 : 1;
}
