
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


    Kernel data

\************************************************************************/

#include "core/Kernel.h"
#include "input/Mouse.h"
#include "network/Socket.h"
#include "core/DriverGetters.h"
#include "drivers/input/KeyboardDrivers.h"
#include "drivers/input/MouseDrivers.h"
#include "utils/Helpers.h"
#include "process/Process.h"
#include "drivers/input/Keyboard.h"
#include "log/Log.h"

/************************************************************************/

typedef struct tag_CPUIDREGISTERS {
    U32 reg_EAX;
    U32 reg_EBX;
    U32 reg_ECX;
    U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

/************************************************************************/

static STARTUP_DRIVER_ENTRY StartupDriverEntries[64];
static UINT StartupDriverEntryCount = 0;

/************************************************************************/

static LIST StartupDriverList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST DriverList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST WindowClassList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST DesktopList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST ProcessList = {
    .First = (LPLISTNODE)&KernelProcess,
    .Last = (LPLISTNODE)&KernelProcess,
    .Current = (LPLISTNODE)&KernelProcess,
    .NumItems = 1,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST TaskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST MutexList = {
    .First = (LPLISTNODE)&KernelMutex,
    .Last = (LPLISTNODE)&SessionMutex,
    .Current = (LPLISTNODE)&KernelMutex,
    .NumItems = 13,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST DiskList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBInterfaceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBEndpointList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST USBStorageList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST PciDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST NetworkDeviceList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST EventList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST FileSystemList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST UnusedFileSystemList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST FileList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST ExecutableModuleImageList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST TCPConnectionList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static LIST SocketList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = SocketDestructor};

/************************************************************************/

static LIST AccountList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelHeapAlloc,
    .MemFreeFunc = KernelHeapFree,
    .Destructor = NULL};

/************************************************************************/

static KERNEL_DATA DATA_SECTION Kernel = {
    .StartupDrivers = &StartupDriverList,
    .Drivers = &DriverList,
    .WindowClass = &WindowClassList,
    .Desktop = &DesktopList,
    .Process = &ProcessList,
    .Task = &TaskList,
    .Mutex = &MutexList,
    .Disk = &DiskList,
    .USBDevice = &USBDeviceList,
    .USBInterface = &USBInterfaceList,
    .USBEndpoint = &USBEndpointList,
    .USBStorage = &USBStorageList,
    .PCIDevice = &PciDeviceList,
    .NetworkDevice = &NetworkDeviceList,
    .Event = &EventList,
    .FileSystem = &FileSystemList,
    .UnusedFileSystem = &UnusedFileSystemList,
    .File = &FileList,
    .ExecutableModuleImage = &ExecutableModuleImageList,
    .TCPConnection = &TCPConnectionList,
    .Socket = &SocketList,
    .UserSessions = NULL,
    .UserAccount = &AccountList,
    .ActiveDesktop = NULL,
    .FocusedProcess = &KernelProcess,
    .FileSystemInfo = {.ActivePartitionName = ""},
    .SystemFS = {
        .Header = {
            .TypeID = KOID_FILESYSTEM,
            .References = 1,
            .Next = NULL,
            .Prev = NULL,
            .Mutex = EMPTY_MUTEX,
            .Mounted = TRUE,
            .Driver = &SystemFSDriver,
            .StorageUnit = NULL,
            .Partition = {
                .Scheme = PARTITION_SCHEME_VIRTUAL,
                .Type = FSID_NONE,
                .Format = PARTITION_FORMAT_UNKNOWN,
                .Index = 0,
                .Flags = 0,
                .StartSector = 0,
                .NumSectors = 0,
                .TypeGuid = {0}
            },
            .Name = "System"
        },
        .Root = NULL
    },
    .HandleMap = {0},
    .CPU = {.Name = "", .Type = 0, .Family = 0, .Model = 0, .Stepping = 0, .Features = 0, .BaseFrequencyMHz = 0},
    .Configuration = NULL,
    .MinimumQuantum = 1,
    .MaximumQuantum = 8,
    .DeferredWorkWaitTimeoutMS = DEFERRED_WORK_WAIT_TIMEOUT_MS,
    .DeferredWorkPollDelayMS = DEFERRED_WORK_POLL_DELAY_MS,
    .DoLogin = 0,
    .ShowDesktop = 0,
    .BootTime = {0},
    .Debug = {
        .UseDeadlockMonitor = 0,
        .WindowPipelineTraceEnabled = 0
    },
    .LanguageCode = "en-US",
    .KeyboardCode = "fr-FR"
};

/************************************************************************/

/**
 * @brief Checks whether one driver is already present in one list.
 * @param List Target list.
 * @param Driver Driver pointer to find.
 * @return TRUE if found.
 */
static BOOL DriverListContains(LPLIST List, LPDRIVER Driver) {
    if (List == NULL || Driver == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = List->First; Node; Node = Node->Next) {
        if ((LPDRIVER)Node == Driver) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Checks whether one startup entry already references one driver.
 * @param List Startup list.
 * @param Driver Driver pointer to find.
 * @return TRUE if found.
 */
static BOOL StartupDriverListContains(LPLIST List, LPDRIVER Driver) {
    if (List == NULL || Driver == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = List->First; Node; Node = Node->Next) {
        LPSTARTUP_DRIVER_ENTRY Entry = (LPSTARTUP_DRIVER_ENTRY)Node;
        if (Entry->Driver == Driver) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Registers one startup driver reference.
 * @param Driver Driver descriptor.
 */
static void RegisterStartupDriver(LPDRIVER Driver) {
    LPSTARTUP_DRIVER_ENTRY Entry = NULL;

    if (Driver == NULL) {
        return;
    }

    if (StartupDriverListContains(Kernel.StartupDrivers, Driver)) {
        return;
    }

    if (StartupDriverEntryCount >= ARRAY_COUNT(StartupDriverEntries)) {
        WARNING(TEXT("Startup driver entry table full"));
        return;
    }

    Entry = &StartupDriverEntries[StartupDriverEntryCount++];
    *Entry = (STARTUP_DRIVER_ENTRY){0};
    Entry->Driver = Driver;

    ListAddTail(Kernel.StartupDrivers, Entry);
}

/************************************************************************/

/**
 * @brief Registers one driver into known list and optional startup list.
 * @param Driver Driver descriptor.
 * @param AddToStartup TRUE to include in startup load order.
 */
static void RegisterDriver(LPDRIVER Driver, BOOL AddToStartup) {
    if (Driver == NULL) {
        return;
    }

    if (!DriverListContains(Kernel.Drivers, Driver)) {
        ListAddTail(Kernel.Drivers, Driver);
    }

    if (AddToStartup) {
        RegisterStartupDriver(Driver);
    }
}

/************************************************************************/

/**
 * @brief Populates startup and known driver lists.
 */
void InitializeDriverList(void) {
    if (Kernel.StartupDrivers == NULL || Kernel.Drivers == NULL || Kernel.StartupDrivers->NumItems != 0 ||
        Kernel.Drivers->NumItems != 0) {
        return;
    }
    StartupDriverEntryCount = 0;

    RegisterDriver(ConsoleGetDriver(), TRUE);
    RegisterDriver(KernelLogGetDriver(), TRUE);
    RegisterDriver(MemoryManagerGetDriver(), TRUE);
    RegisterDriver(TaskSegmentsGetDriver(), TRUE);
    RegisterDriver(InterruptsGetDriver(), TRUE);
    RegisterDriver(KernelProcessGetDriver(), TRUE);
    RegisterDriver(ACPIGetDriver(), TRUE);
    RegisterDriver(LocalAPICGetDriver(), TRUE);
    RegisterDriver(IOAPICGetDriver(), TRUE);
    RegisterDriver(InterruptControllerGetDriver(), TRUE);
    RegisterDriver(DeviceInterruptGetDriver(), TRUE);
    RegisterDriver(DeferredWorkGetDriver(), TRUE);
    RegisterDriver(SerialMouseGetDriver(), TRUE);
    RegisterDriver(ClockGetDriver(), TRUE);
    RegisterDriver(PCIGetDriver(), TRUE);
    RegisterDriver(KeyboardSelectorGetDriver(), TRUE);
    RegisterDriver(MouseSelectorGetDriver(), TRUE);
    RegisterDriver(USBMouseGetDriver(), TRUE);
    RegisterDriver(USBStorageGetDriver(), TRUE);
    RegisterDriver(ATADiskGetDriver(), TRUE);
    RegisterDriver(SATADiskGetDriver(), TRUE);
    RegisterDriver(RAMDiskGetDriver(), TRUE);
    RegisterDriver(FileSystemGetDriver(), TRUE);
    RegisterDriver(NetworkManagerGetDriver(), TRUE);
    RegisterDriver(UserAccountGetDriver(), TRUE);
    RegisterDriver(VGAGetDriver(), TRUE);
    RegisterDriver(GraphicsSelectorGetDriver(), TRUE);

    RegisterDriver(E1000GetDriver(), FALSE);
    RegisterDriver(RTL8139GetDriver(), FALSE);
    RegisterDriver(RTL8139CPlusGetDriver(), FALSE);
    RegisterDriver(RTL8169GetDriver(), FALSE);
    RegisterDriver(AHCIPCIGetDriver(), FALSE);
    RegisterDriver(NVMeGetDriver(), FALSE);
    RegisterDriver(XHCIGetDriver(), FALSE);
    RegisterDriver(IntelGfxGetDriver(), FALSE);
    RegisterDriver(GOPGetDriver(), FALSE);
    RegisterDriver(VESAGetDriver(), FALSE);
}

/************************************************************************/

/**
 * @brief Retrieves the kernel driver list.
 * @return Pointer to the driver list.
 */
LPLIST GetDriverList(void) {
    return Kernel.Drivers;
}

/************************************************************************/

/**
 * @brief Retrieves the startup driver list.
 * @return Pointer to the startup driver list.
 */
LPLIST GetStartupDriverList(void) {
    return Kernel.StartupDrivers;
}

/************************************************************************/

/**
 * @brief Retrieves the registered window class list.
 * @return Pointer to the window class list.
 */
LPLIST GetWindowClassList(void) {
    return Kernel.WindowClass;
}

/************************************************************************/

/**
 * @brief Retrieves the desktop list.
 * @return Pointer to the desktop list.
 */
LPLIST GetDesktopList(void) {
    return Kernel.Desktop;
}

/************************************************************************/

/**
 * @brief Retrieves the process list.
 * @return Pointer to the process list.
 */
LPLIST GetProcessList(void) {
    return Kernel.Process;
}

/************************************************************************/

/**
 * @brief Retrieves the task list.
 * @return Pointer to the task list.
 */
LPLIST GetTaskList(void) {
    return Kernel.Task;
}

/************************************************************************/

/**
 * @brief Retrieves the mutex list.
 * @return Pointer to the mutex list.
 */
LPLIST GetMutexList(void) {
    return Kernel.Mutex;
}

/************************************************************************/

/**
 * @brief Retrieves the disk list.
 * @return Pointer to the disk list.
 */
LPLIST GetDiskList(void) {
    return Kernel.Disk;
}

/************************************************************************/

/**
 * @brief Retrieves the USB device list.
 * @return Pointer to the USB device list.
 */
LPLIST GetUsbDeviceList(void) {
    return Kernel.USBDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the USB interface list.
 * @return Pointer to the USB interface list.
 */
LPLIST GetUsbInterfaceList(void) {
    return Kernel.USBInterface;
}

/************************************************************************/

/**
 * @brief Retrieves the USB endpoint list.
 * @return Pointer to the USB endpoint list.
 */
LPLIST GetUsbEndpointList(void) {
    return Kernel.USBEndpoint;
}

/************************************************************************/

/**
 * @brief Retrieves the USB storage list.
 * @return Pointer to the USB storage list.
 */
LPLIST GetUsbStorageList(void) {
    return Kernel.USBStorage;
}

/************************************************************************/

/**
 * @brief Retrieves the PCI device list.
 * @return Pointer to the PCI device list.
 */
LPLIST GetPCIDeviceList(void) {
    return Kernel.PCIDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the network device list.
 * @return Pointer to the network device list.
 */
LPLIST GetNetworkDeviceList(void) {
    return Kernel.NetworkDevice;
}

/************************************************************************/

/**
 * @brief Retrieves the event list.
 * @return Pointer to the event list.
 */
LPLIST GetEventList(void) {
    return Kernel.Event;
}

/************************************************************************/

/**
 * @brief Retrieves the file system list.
 * @return Pointer to the file system list.
 */
LPLIST GetFileSystemList(void) {
    return Kernel.FileSystem;
}

/************************************************************************/

/**
 * @brief Retrieves the list of discovered but not mounted file systems.
 * @return Pointer to the unused file system list.
 */
LPLIST GetUnusedFileSystemList(void) {
    return Kernel.UnusedFileSystem;
}

/************************************************************************/

/**
 * @brief Retrieves the open file list.
 * @return Pointer to the file list.
 */
LPLIST GetFileList(void) {
    return Kernel.File;
}

/************************************************************************/

/**
 * @brief Retrieves the shared executable module image list.
 * @return Pointer to the module image list.
 */
LPLIST GetExecutableModuleImageList(void) {
    return Kernel.ExecutableModuleImage;
}

/************************************************************************/

/**
 * @brief Retrieves the TCP connection list.
 * @return Pointer to the TCP connection list.
 */
LPLIST GetTCPConnectionList(void) {
    return Kernel.TCPConnection;
}

/************************************************************************/

/**
 * @brief Retrieves the socket list.
 * @return Pointer to the socket list.
 */
LPLIST GetSocketList(void) {
    return Kernel.Socket;
}

/************************************************************************/

/**
 * @brief Retrieves the user session list.
 * @return Pointer to the user session list.
 */
LPLIST GetUserSessionList(void) {
    return Kernel.UserSessions;
}

/************************************************************************/

/**
 * @brief Sets the user session list pointer.
 * @param List Pointer to the user session list.
 */
void SetUserSessionList(LPLIST List) {
    Kernel.UserSessions = List;
}

/************************************************************************/

/**
 * @brief Retrieves the account list.
 * @return Pointer to the account list.
 */
LPLIST GetAccountList(void) {
    return Kernel.UserAccount;
}

/************************************************************************/

/**
 * @brief Sets the account list pointer.
 * @param List Pointer to the account list.
 */
void SetAccountList(LPLIST List) {
    Kernel.UserAccount = List;
}

/************************************************************************/

/**
 * @brief Retrieves the global file system info structure.
 * @return Pointer to the file system info structure.
 */
FILESYSTEM_GLOBAL_INFO* GetFileSystemGlobalInfo(void) {
    return &(Kernel.FileSystemInfo);
}

/************************************************************************/

/**
 * @brief Retrieves the SystemFS backing structure.
 * @return Pointer to the SystemFS structure.
 */
LPSYSTEMFSFILESYSTEM GetSystemFSData(void) {
    return &(Kernel.SystemFS);
}

/************************************************************************/

/**
 * @brief Retrieves the object termination cache.
 * @return Pointer to the cache.
 */
LPCACHE GetObjectTerminationCache(void) {
    return &(Kernel.ObjectTerminationCache);
}

/************************************************************************/

/**
 * @brief Retrieves the handle map.
 * @return Pointer to the handle map.
 */
LPHANDLE_MAP GetHandleMap(void) {
    return &(Kernel.HandleMap);
}

/**
 * @brief Retrieves the CPU information storage.
 * @return Pointer to the CPU information structure.
 */
LPCPU_INFORMATION GetKernelCPUInfo(void) {
    return &(Kernel.CPU);
}

/************************************************************************/

/**
 * @brief Retrieves the deferred work wait timeout.
 * @return Timeout in milliseconds.
 */
UINT GetDeferredWorkWaitTimeout(void) {
    return Kernel.DeferredWorkWaitTimeoutMS;
}

/************************************************************************/

/**
 * @brief Sets the deferred work wait timeout.
 * @param Timeout Timeout in milliseconds.
 */
void SetDeferredWorkWaitTimeout(UINT Timeout) {
    Kernel.DeferredWorkWaitTimeoutMS = Timeout;
}

/************************************************************************/

/**
 * @brief Retrieves the deferred work poll delay.
 * @return Poll delay in milliseconds.
 */
UINT GetDeferredWorkPollDelay(void) {
    return Kernel.DeferredWorkPollDelayMS;
}

/************************************************************************/

/**
 * @brief Sets the deferred work poll delay.
 * @param Delay Poll delay in milliseconds.
 */
void SetDeferredWorkPollDelay(UINT Delay) {
    Kernel.DeferredWorkPollDelayMS = Delay;
}

/************************************************************************/

/**
 * @brief Retrieves the kernel configuration.
 * @return Pointer to the parsed configuration or NULL if not set.
 */
LPTOML GetConfiguration(void) {
    return Kernel.Configuration;
}

/************************************************************************/

/**
 * @brief Updates the kernel configuration pointer.
 * @param Configuration Parsed configuration to store.
 */
void SetConfiguration(LPTOML Configuration) {
    Kernel.Configuration = Configuration;
}

/************************************************************************/

/**
 * @brief Gets the login sequence flag.
 * @return TRUE when login is enabled.
 */
BOOL GetDoLogin(void) {
    return Kernel.DoLogin;
}

/************************************************************************/

/**
 * @brief Gets the deadlock monitor usage flag.
 * @return TRUE when mutex deadlock diagnostics hooks are enabled.
 */
BOOL GetUseDeadlockMonitor(void) {
    return Kernel.Debug.UseDeadlockMonitor;
}

/************************************************************************/

/**
 * @brief Gets the automatic desktop activation flag.
 * @return TRUE when automatic desktop activation is enabled.
 */
BOOL GetShowDesktop(void) {
    return Kernel.ShowDesktop;
}

/************************************************************************/

/**
 * @brief Gets the desktop window pipeline trace flag.
 * @return TRUE when the visual window pipeline trace is enabled.
 */
BOOL GetWindowPipelineTraceEnabled(void) {
    return Kernel.Debug.WindowPipelineTraceEnabled;
}

/************************************************************************/

/**
 * @brief Retrieves the recorded local boot date-time.
 * @param Time Destination date-time structure.
 * @return TRUE on success.
 */
BOOL GetKernelBootTime(LPDATETIME Time) {
    if (Time == NULL) return FALSE;
    *Time = Kernel.BootTime;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Sets the deadlock monitor usage flag.
 * @param Enabled TRUE to enable mutex deadlock diagnostics hooks, FALSE to disable.
 */
void SetUseDeadlockMonitor(BOOL Enabled) {
    Kernel.Debug.UseDeadlockMonitor = Enabled;
}

/************************************************************************/

/**
 * @brief Sets the login sequence flag.
 * @param DoLogin TRUE to enable login, FALSE to disable.
 */
void SetDoLogin(BOOL DoLogin) {
    Kernel.DoLogin = DoLogin;
}

/************************************************************************/

/**
 * @brief Sets the automatic desktop activation flag.
 * @param ShowDesktop TRUE to enable automatic desktop activation, FALSE to disable.
 */
void SetShowDesktop(BOOL ShowDesktop) {
    Kernel.ShowDesktop = ShowDesktop;
}

/************************************************************************/

/**
 * @brief Sets the desktop window pipeline trace flag.
 * @param Enabled TRUE to enable the visual window pipeline trace, FALSE to disable.
 */
void SetWindowPipelineTraceEnabled(BOOL Enabled) {
    Kernel.Debug.WindowPipelineTraceEnabled = Enabled;
}

/************************************************************************/

/**
 * @brief Stores the local boot date-time.
 * @param Time Source date-time structure.
 */
void SetKernelBootTime(LPDATETIME Time) {
    if (Time == NULL) return;
    Kernel.BootTime = *Time;
}

/************************************************************************/

/**
 * @brief Retrieves the active language code.
 * @return Pointer to language code string.
 */
LPCSTR GetLanguageCode(void) {
    return Kernel.LanguageCode;
}

/************************************************************************/

/**
 * @brief Updates the active language code.
 * @param LanguageCode Null-terminated language code.
 */
void SetLanguageCode(LPCSTR LanguageCode) {
    SAFE_USE(LanguageCode) { StringCopy(Kernel.LanguageCode, LanguageCode); }
}

/************************************************************************/

/**
 * @brief Retrieves the active keyboard code.
 * @return Pointer to keyboard code string.
 */
LPCSTR GetKeyboardCode(void) {
    return Kernel.KeyboardCode;
}

/************************************************************************/

/**
 * @brief Updates the active keyboard code.
 * @param KeyboardCode Null-terminated keyboard code.
 */
void SetKeyboardCode(LPCSTR KeyboardCode) {
    SAFE_USE(KeyboardCode) { StringCopy(Kernel.KeyboardCode, KeyboardCode); }
}

/************************************************************************/

/**
 * @brief Retrieves the configured minimum scheduler quantum.
 * @return Minimum quantum in milliseconds.
 */
UINT GetMinimumQuantum(void) {
    return Kernel.MinimumQuantum;
}

/************************************************************************/

/**
 * @brief Updates the configured minimum scheduler quantum.
 * @param MinimumQuantum Quantum in milliseconds.
 */
void SetMinimumQuantum(UINT MinimumQuantum) {
    Kernel.MinimumQuantum = MinimumQuantum;
}

/************************************************************************/

/**
 * @brief Retrieves the configured maximum scheduler quantum.
 * @return Maximum quantum in milliseconds.
 */
UINT GetMaximumQuantum(void) {
    return Kernel.MaximumQuantum;
}

/************************************************************************/

/**
 * @brief Updates the configured maximum scheduler quantum.
 * @param MaximumQuantum Quantum in milliseconds.
 */
void SetMaximumQuantum(UINT MaximumQuantum) {
    Kernel.MaximumQuantum = MaximumQuantum;
}

/************************************************************************/

/**
 * @brief Retrieve the current desktop.
 * @return Active desktop pointer or NULL if none is set.
 */
LPDESKTOP GetActiveDesktop(void) {
    return Kernel.ActiveDesktop;
}

/************************************************************************/

/**
 * @brief Set the current desktop.
 * @param Desktop Desktop to activate, may be NULL to clear it.
 */
void SetActiveDesktop(LPDESKTOP Desktop) {
    LPDESKTOP PreviousDesktop = Kernel.ActiveDesktop;
    LPPROCESS FocusedProcess = Kernel.FocusedProcess;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        Kernel.ActiveDesktop = Desktop;
    } else {
        Kernel.ActiveDesktop = NULL;
        if (FocusedProcess == NULL || FocusedProcess->TypeID != KOID_PROCESS ||
            FocusedProcess->Status == PROCESS_STATUS_DEAD) {
            Kernel.FocusedProcess = &KernelProcess;
        }
    }

    if (Kernel.ActiveDesktop != PreviousDesktop) {
        ClearKeyboardBuffer();
    }
}

/************************************************************************/

/**
 * @brief Retrieve the process currently holding input focus.
 * @return Focused process pointer or NULL if none is set.
 */
LPPROCESS GetFocusedProcess(void) {
    SAFE_USE_VALID_ID(Kernel.FocusedProcess, KOID_PROCESS) {
        if (Kernel.FocusedProcess->Status == PROCESS_STATUS_DEAD) {
            Kernel.FocusedProcess = &KernelProcess;
            return &KernelProcess;
        }
        return Kernel.FocusedProcess;
    }

    return &KernelProcess;
}

/************************************************************************/

LPDESKTOP_THEME GetGlobalThemeState(void) {
    return &Kernel.Theme;
}

/************************************************************************/

/**
 * @brief Set the process that holds input focus.
 * @param Process Process to focus, may be NULL to clear focus.
 */
void SetFocusedProcess(LPPROCESS Process) {
    LPDESKTOP PreviousDesktop = Kernel.ActiveDesktop;
    LPPROCESS PreviousProcess = Kernel.FocusedProcess;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        Kernel.FocusedProcess = Process;
        if (Process->Desktop != NULL && Process->Desktop->TypeID == KOID_DESKTOP) {
            Kernel.ActiveDesktop = Process->Desktop;
        } else {
            Kernel.ActiveDesktop = NULL;
        }
    } else {
        Kernel.FocusedProcess = &KernelProcess;
        if (KernelProcess.Desktop != NULL && KernelProcess.Desktop->TypeID == KOID_DESKTOP) {
            Kernel.ActiveDesktop = KernelProcess.Desktop;
        } else {
            Kernel.ActiveDesktop = NULL;
        }
    }

    if (Kernel.ActiveDesktop != PreviousDesktop || PreviousProcess != Process) {
        ClearKeyboardBuffer();
    }
}

/************************************************************************/

static void ReadCPUIDLeaf(U32 Leaf, U32 SubLeaf, LPCPUIDREGISTERS Registers) {
    if (Registers == NULL) {
        return;
    }

#if defined(__EXOS_ARCH_X86_32__) || defined(__EXOS_ARCH_X86_64__)
    U32 EAXValue;
    U32 EBXValue;
    U32 ECXValue;
    U32 EDXValue;

    __asm__ __volatile__("cpuid"
                         : "=a"(EAXValue), "=b"(EBXValue), "=c"(ECXValue), "=d"(EDXValue)
                         : "a"(Leaf), "c"(SubLeaf));

    Registers->reg_EAX = EAXValue;
    Registers->reg_EBX = EBXValue;
    Registers->reg_ECX = ECXValue;
    Registers->reg_EDX = EDXValue;
#else
    UNUSED(Leaf);
    UNUSED(SubLeaf);

    Registers->reg_EAX = 0;
    Registers->reg_EBX = 0;
    Registers->reg_ECX = 0;
    Registers->reg_EDX = 0;
#endif
}

/************************************************************************/

static U32 DetectCPUBaseFrequencyMHz(void) {
    CPUIDREGISTERS Leaf0;
    CPUIDREGISTERS Leaf15;
    CPUIDREGISTERS Leaf16;
    U32 MaximumBasicLeaf;

    MemorySet(&Leaf0, 0, sizeof(Leaf0));
    MemorySet(&Leaf15, 0, sizeof(Leaf15));
    MemorySet(&Leaf16, 0, sizeof(Leaf16));

    ReadCPUIDLeaf(0, 0, &Leaf0);
    MaximumBasicLeaf = Leaf0.reg_EAX;

    if (MaximumBasicLeaf >= 0x16) {
        ReadCPUIDLeaf(0x16, 0, &Leaf16);
        if (Leaf16.reg_EAX != 0) {
            return Leaf16.reg_EAX;
        }
    }

    if (MaximumBasicLeaf >= 0x15) {
        ReadCPUIDLeaf(0x15, 0, &Leaf15);
        if (Leaf15.reg_EAX != 0 && Leaf15.reg_EBX != 0 && Leaf15.reg_ECX != 0) {
            U64 FrequencyHz = U64_DIV_U32(U64_MUL_U32(Leaf15.reg_ECX, Leaf15.reg_EBX), Leaf15.reg_EAX, NULL);
            U64 FrequencyMHz = U64_DIV_U32(FrequencyHz, 1000000, NULL);

            if (U64_Cmp(FrequencyMHz, U64_FromU32(MAX_U32)) > 0) {
                return MAX_U32;
            }

            return U64_ToU32_Clip(FrequencyMHz);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieves basic CPU identification data.
 *
 * Populates the provided structure using CPUID information, including
 * vendor string, model and feature flags.
 *
 * @param Info Pointer to structure that receives CPU information.
 * @return TRUE on success.
 */
BOOL GetCPUInformation(LPCPU_INFORMATION Info) {
    CPUIDREGISTERS Regs[8];

    MemorySet(Info, 0, sizeof(CPU_INFORMATION));

    GetCPUID(Regs);

    //-------------------------------------
    // Fill name with register contents

    *((U32*)(Info->Name + 0)) = Regs[0].reg_EBX;
    *((U32*)(Info->Name + 4)) = Regs[0].reg_EDX;
    *((U32*)(Info->Name + 8)) = Regs[0].reg_ECX;
    Info->Name[12] = '\0';

    //-------------------------------------
    // Get model information if available

    Info->Type = (Regs[1].reg_EAX & INTEL_CPU_MASK_TYPE) >> INTEL_CPU_SHFT_TYPE;
    Info->Family = (Regs[1].reg_EAX & INTEL_CPU_MASK_FAMILY) >> INTEL_CPU_SHFT_FAMILY;
    Info->Model = (Regs[1].reg_EAX & INTEL_CPU_MASK_MODEL) >> INTEL_CPU_SHFT_MODEL;
    Info->Stepping = (Regs[1].reg_EAX & INTEL_CPU_MASK_STEPPING) >> INTEL_CPU_SHFT_STEPPING;
    Info->Features = Regs[1].reg_EDX;
    Info->BaseFrequencyMHz = DetectCPUBaseFrequencyMHz();

    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieves the mouse driver descriptor.
 * @return Pointer to the mouse driver.
 */
LPDRIVER GetMouseDriver(void) {
    return MouseSelectorGetDriver();
}

/************************************************************************/

/**
 * @brief Retrieves the graphics driver descriptor.
 * @return Pointer to the graphics driver.
 */
LPDRIVER GetGraphicsDriver(void) {
    return GraphicsSelectorGetDriver();
}

/************************************************************************/

/**
 * @brief Retrieves the default file system driver descriptor.
 * @return Pointer to the default file system driver.
 */
LPDRIVER GetDefaultFileSystemDriver(void) {
    return EXFSGetDriver();
}

/************************************************************************/

/**
 * @brief Retrieves the active display session state.
 * @return Pointer to display session data.
 */
LPDISPLAY_SESSION GetDisplaySession(void) {
    return &Kernel.DisplaySession;
}
