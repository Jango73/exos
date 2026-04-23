
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
typedef UINT (*MODULE_SAMPLE_SUBTRACT)(UINT A, UINT B);
typedef UINT (*MODULE_SAMPLE_MULTIPLY)(UINT A, UINT B);
typedef UINT (*MODULE_SAMPLE_DIVIDE)(UINT A, UINT B);
typedef UINT (*MODULE_SAMPLE_FACTORIAL)(UINT N);
typedef UINT (*MODULE_SAMPLE_FIBONACCI)(UINT N);
typedef UINT (*MODULE_SAMPLE_GCD)(UINT A, UINT B);
typedef UINT (*MODULE_SAMPLE_POWER)(UINT Base, UINT Exp);
typedef UINT (*MODULE_IMPORT_MATH_BASIC)(void);
typedef UINT (*MODULE_IMPORT_MATH_MULTIPLY)(void);
typedef UINT (*MODULE_IMPORT_MATH_DIVIDE)(void);
typedef UINT (*MODULE_IMPORT_MATH_FACTORIAL_SUM)(void);
typedef UINT (*MODULE_IMPORT_MATH_FIBONACCI)(void);
typedef UINT (*MODULE_IMPORT_MATH_GCD)(void);
typedef UINT (*MODULE_IMPORT_MATH_POWER)(void);

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

            MODULE_HOST_FAILURE(
                "Module child process failed wait=%u exit=%u object=%p\n", WaitResult, WaitInfo.ExitCodes[0],
                (HANDLE)ObjectHandle);
            return FALSE;
        }
    }

    MODULE_HOST_FAILURE(
        "Module child process wait failed wait=%u process=%p task=%p\n", WaitResult, ProcessInfo.Process,
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

    Module = LoadModuleWithStatus((LPCSTR) "/system/apps/test/module-sample", &LoadStatus, &LoadStage);
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module child load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleAdd", &SymbolStatus);
    if (ModuleSampleAdd == NULL) {
        MODULE_HOST_FAILURE("Module child symbol lookup failed name=ModuleSampleAdd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleIncrementShared = (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbolWithStatus(
        Module, (LPCSTR) "ModuleSampleIncrementShared", &SymbolStatus);
    if (ModuleSampleIncrementShared == NULL) {
        MODULE_HOST_FAILURE(
            "Module child symbol lookup failed name=ModuleSampleIncrementShared status=%u\n", SymbolStatus);
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
    MODULE_SAMPLE_SUBTRACT ModuleSampleSubtract;
    MODULE_SAMPLE_MULTIPLY ModuleSampleMultiply;
    MODULE_SAMPLE_DIVIDE ModuleSampleDivide;
    MODULE_SAMPLE_FACTORIAL ModuleSampleFactorial;
    MODULE_SAMPLE_FIBONACCI ModuleSampleFibonacci;
    MODULE_SAMPLE_GCD ModuleSampleGcd;
    MODULE_SAMPLE_POWER ModuleSamplePower;
    MODULE_IMPORT_MATH_BASIC ModuleImportMathBasic;
    MODULE_IMPORT_MATH_MULTIPLY ModuleImportMathMultiply;
    MODULE_IMPORT_MATH_DIVIDE ModuleImportMathDivide;
    MODULE_IMPORT_MATH_FACTORIAL_SUM ModuleImportMathFactorialSum;
    MODULE_IMPORT_MATH_FIBONACCI ModuleImportMathFibonacci;
    MODULE_IMPORT_MATH_GCD ModuleImportMathGcd;
    MODULE_IMPORT_MATH_POWER ModuleImportMathPower;
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

    Module = LoadModuleWithStatus((LPCSTR) "/system/apps/test/module-sample", &LoadStatus, &LoadStage);
    if (Module == 0) {
        MODULE_HOST_FAILURE("Module load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleSampleAdd = (MODULE_SAMPLE_ADD)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleAdd", &SymbolStatus);
    if (ModuleSampleAdd == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleAdd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleIncrementShared = (MODULE_SAMPLE_INCREMENT_SHARED)GetModuleSymbolWithStatus(
        Module, (LPCSTR) "ModuleSampleIncrementShared", &SymbolStatus);
    if (ModuleSampleIncrementShared == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleIncrementShared status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleGetShared =
        (MODULE_SAMPLE_GET_SHARED)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleGetShared", &SymbolStatus);
    if (ModuleSampleGetShared == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleGetShared status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleSubtract =
        (MODULE_SAMPLE_SUBTRACT)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleSubtract", &SymbolStatus);
    if (ModuleSampleSubtract == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleSubtract status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleMultiply =
        (MODULE_SAMPLE_MULTIPLY)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleMultiply", &SymbolStatus);
    if (ModuleSampleMultiply == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleMultiply status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleDivide =
        (MODULE_SAMPLE_DIVIDE)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleDivide", &SymbolStatus);
    if (ModuleSampleDivide == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleDivide status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleFactorial =
        (MODULE_SAMPLE_FACTORIAL)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleFactorial", &SymbolStatus);
    if (ModuleSampleFactorial == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleFactorial status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleFibonacci =
        (MODULE_SAMPLE_FIBONACCI)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleFibonacci", &SymbolStatus);
    if (ModuleSampleFibonacci == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleFibonacci status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSampleGcd = (MODULE_SAMPLE_GCD)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSampleGcd", &SymbolStatus);
    if (ModuleSampleGcd == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSampleGcd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleSamplePower =
        (MODULE_SAMPLE_POWER)GetModuleSymbolWithStatus(Module, (LPCSTR) "ModuleSamplePower", &SymbolStatus);
    if (ModuleSamplePower == NULL) {
        MODULE_HOST_FAILURE("Module symbol lookup failed name=ModuleSamplePower status=%u\n", SymbolStatus);
        return FALSE;
    }

    Result = ModuleSampleSubtract(100, 38);
    if (Result != 62) {
        MODULE_HOST_FAILURE("Module math subtract failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSampleMultiply(8, 9);
    if (Result != 72) {
        MODULE_HOST_FAILURE("Module math multiply failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSampleDivide(90, 9);
    if (Result != 10) {
        MODULE_HOST_FAILURE("Module math divide failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSampleFactorial(5);
    if (Result != 120) {
        MODULE_HOST_FAILURE("Module math factorial failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSampleFibonacci(10);
    if (Result != 55) {
        MODULE_HOST_FAILURE("Module math fibonacci failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSampleGcd(48, 18);
    if (Result != 6) {
        MODULE_HOST_FAILURE("Module math gcd failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleSamplePower(3, 4);
    if (Result != 81) {
        MODULE_HOST_FAILURE("Module math power failed result=%u\n", Result);
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
        MODULE_HOST_FAILURE(
            "Module task global data failed result=%u total=%u\n", SharedDataResult, ModuleSampleGetShared());
        return FALSE;
    }

    ImportModule = LoadModuleWithStatus((LPCSTR) "/system/apps/test/module-import", &LoadStatus, &LoadStage);
    if (ImportModule == 0) {
        MODULE_HOST_FAILURE("Module import load failed status=%u stage=%u\n", LoadStatus, LoadStage);
        return FALSE;
    }

    ModuleImportResolve =
        (MODULE_IMPORT_RESOLVE)GetModuleSymbolWithStatus(ImportModule, (LPCSTR) "ModuleImportResolve", &SymbolStatus);
    if (ModuleImportResolve == NULL) {
        MODULE_HOST_FAILURE("Module import resolver failed name=ModuleImportResolve status=%u\n", SymbolStatus);
        return FALSE;
    }

    if (ModuleImportResolve() != 0x123D) {
        MODULE_HOST_FAILURE("Module import resolver failed wrong-result\n");
        return FALSE;
    }

    ModuleImportMathBasic = (MODULE_IMPORT_MATH_BASIC)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathBasic", &SymbolStatus);
    if (ModuleImportMathBasic == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathBasic status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathMultiply = (MODULE_IMPORT_MATH_MULTIPLY)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathMultiply", &SymbolStatus);
    if (ModuleImportMathMultiply == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathMultiply status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathDivide = (MODULE_IMPORT_MATH_DIVIDE)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathDivide", &SymbolStatus);
    if (ModuleImportMathDivide == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathDivide status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathFactorialSum = (MODULE_IMPORT_MATH_FACTORIAL_SUM)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathFactorialSum", &SymbolStatus);
    if (ModuleImportMathFactorialSum == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathFactorialSum status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathFibonacci = (MODULE_IMPORT_MATH_FIBONACCI)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathFibonacci", &SymbolStatus);
    if (ModuleImportMathFibonacci == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathFibonacci status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathGcd =
        (MODULE_IMPORT_MATH_GCD)GetModuleSymbolWithStatus(ImportModule, (LPCSTR) "ModuleImportMathGcd", &SymbolStatus);
    if (ModuleImportMathGcd == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathGcd status=%u\n", SymbolStatus);
        return FALSE;
    }

    ModuleImportMathPower = (MODULE_IMPORT_MATH_POWER)GetModuleSymbolWithStatus(
        ImportModule, (LPCSTR) "ModuleImportMathPower", &SymbolStatus);
    if (ModuleImportMathPower == NULL) {
        MODULE_HOST_FAILURE("Module import failed name=ModuleImportMathPower status=%u\n", SymbolStatus);
        return FALSE;
    }

    Result = ModuleImportMathBasic();
    if (Result != 63) {
        MODULE_HOST_FAILURE("Module import math basic failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathMultiply();
    if (Result != 84) {
        MODULE_HOST_FAILURE("Module import math multiply failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathDivide();
    if (Result != 14) {
        MODULE_HOST_FAILURE("Module import math divide failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathFactorialSum();
    if (Result != 30) {
        MODULE_HOST_FAILURE("Module import math factorial sum failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathFibonacci();
    if (Result != 13) {
        MODULE_HOST_FAILURE("Module import math fibonacci failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathGcd();
    if (Result != 6) {
        MODULE_HOST_FAILURE("Module import math gcd failed result=%u\n", Result);
        return FALSE;
    }

    Result = ModuleImportMathPower();
    if (Result != 256) {
        MODULE_HOST_FAILURE("Module import math power failed result=%u\n", Result);
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
