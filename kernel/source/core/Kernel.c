
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


    Kernel main file

\************************************************************************/

#include "core/Kernel.h"

#include "DisplaySession.h"
#include "autotest/Autotest.h"
#include "console/Console.h"
#include "desktop/Desktop.h"
#include "drivers/input/Keyboard.h"
#include "drivers/platform/ACPI.h"
#include "fs/File.h"
#include "log/Log.h"
#include "memory/Buddy-Allocator.h"
#include "process/Process.h"
#include "process/Task.h"
#include "system/Clock.h"
#include "system/SerialPort.h"
#include "text/Lang.h"
#include "text/Quotes.h"
#include "utils/BusyWait.h"
#include "utils/Helpers.h"
#include "utils/Pipe.h"
#include "utils/TOML.h"
#include "utils/UUID.h"

/************************************************************************/

extern U32 DeadBeef;

/************************************************************************/

#define KERNEL_LAYOUT_OFFSET_OF(Type, Member) ((UINT) __builtin_offsetof(Type, Member))

#if defined(__EXOS_ARCH_X86_32__)
typedef char KERNEL_STARTUP_INFO_OFFSET_KERNELRESERVEDBYTES_X86_32
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, KernelReservedBytes) == 0x08) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_STACKTOP_X86_32
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, StackTop) == 0x0C) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_IRQMASK21PM_X86_32
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, IRQMask_21_PM) == 0x14) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_IRQMASKA1PM_X86_32
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, IRQMask_A1_PM) == 0x18) ? 1 : -1];
#endif

#if defined(__EXOS_ARCH_X86_64__)
typedef char KERNEL_STARTUP_INFO_OFFSET_KERNELRESERVEDBYTES_X86_64
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, KernelReservedBytes) == 0x10) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_STACKTOP_X86_64
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, StackTop) == 0x18) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_IRQMASK21PM_X86_64
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, IRQMask_21_PM) == 0x28) ? 1 : -1];
typedef char KERNEL_STARTUP_INFO_OFFSET_IRQMASKA1PM_X86_64
    [(KERNEL_LAYOUT_OFFSET_OF(KERNEL_STARTUP_INFO, IRQMask_A1_PM) == 0x2C) ? 1 : -1];
#endif

/************************************************************************/

void SystemDataViewMode(void);

U32 EXOS_End SECTION(".end_mark") = 0x534F5845;

void DoPageFault(void) {
    UINT* Table = (UINT*)0;
    for (UINT Index = 0; Index < KernelStartup.MemorySize / sizeof(UINT); Index++) {
        Table[Index] = 0;
    }
}

/************************************************************************/

/**
 * @brief Converts a kernel symbol address to its corresponding physical
 *        address.
 *
 * @param Symbol Linear address of the kernel symbol to translate.
 * @return Physical address associated with the provided symbol.
 */

PHYSICAL KernelToPhysical(LINEAR Symbol) {
    return KernelStartup.KernelPhysicalBase + (PHYSICAL)(Symbol - (LINEAR)VMA_KERNEL);
}

/************************************************************************/

/**
 * @brief Convert a kernel pointer into a user-visible handle.
 *
 * Allocates a new entry in the handle map and attaches the provided pointer
 * to it. Returns 0 when allocation or attachment fails.
 *
 * @param Pointer Kernel pointer that must be exposed to userland.
 * @return HANDLE Newly created handle or 0 on failure.
 */
HANDLE PointerToHandle(LINEAR Pointer) {
    if (Pointer == 0) {
        return 0;
    }

    LPHANDLE_MAP HandleMap = GetHandleMap();

    UINT ExistingHandle = 0;
    if (HandleMapFindHandleByPointer(HandleMap, Pointer, &ExistingHandle) == HANDLE_MAP_OK) {
        return ExistingHandle;
    }

    UINT Handle = 0;
    UINT Status = HandleMapAllocateHandle(HandleMap, &Handle);
    if (Status != HANDLE_MAP_OK) {
        return 0;
    }

    Status = HandleMapAttachPointer(HandleMap, Handle, Pointer);
    if (Status != HANDLE_MAP_OK) {
        HandleMapReleaseHandle(HandleMap, Handle);
        return 0;
    }

    return Handle;
}

/************************************************************************/

/**
 * @brief Duplicate an existing user-visible handle.
 *
 * Allocates one new handle entry attached to the same kernel pointer as the
 * input handle.
 *
 * @param Handle Existing handle.
 * @return HANDLE Duplicated handle or 0 on failure.
 */
HANDLE DuplicateHandle(HANDLE Handle) {
    LINEAR Pointer = HandleToPointer(Handle);
    LPHANDLE_MAP HandleMap = GetHandleMap();
    UINT NewHandle = 0;
    UINT Status;

    if (Pointer == 0) {
        return 0;
    }

    Status = HandleMapAllocateHandle(HandleMap, &NewHandle);
    if (Status != HANDLE_MAP_OK) {
        return 0;
    }

    Status = HandleMapAttachPointer(HandleMap, NewHandle, Pointer);
    if (Status != HANDLE_MAP_OK) {
        HandleMapReleaseHandle(HandleMap, NewHandle);
        return 0;
    }

    return NewHandle;
}

/************************************************************************/

/**
 * @brief Resolve a user-visible handle back to its kernel pointer.
 *
 * @param Handle Handle supplied by userland.
 * @return LINEAR Kernel pointer or 0 when the handle is invalid.
 */
LINEAR HandleToPointer(HANDLE Handle) {
    if (Handle == 0) {
        return 0;
    }

    LINEAR Pointer = 0;
    UINT Status = HandleMapResolveHandle(GetHandleMap(), Handle, &Pointer);
    if (Status != HANDLE_MAP_OK) {
        return 0;
    }

    return Pointer;
}

/************************************************************************/

/**
 * @brief Ensure that a value representing a kernel object is a pointer.
 *
 * If the value already lies within kernel space (>= VMA_KERNEL), it is
 * returned as-is. Otherwise it is treated as a handle and resolved to
 * its kernel pointer.
 *
 * @param Value Either a kernel pointer or a user-visible handle.
 * @return LINEAR Kernel pointer or 0 on failure.
 */
LINEAR EnsureKernelPointer(LINEAR Value) {
    if (Value == 0) return 0;
    if (Value >= VMA_KERNEL) return Value;

    LINEAR Pointer = HandleToPointer((HANDLE)Value);
    return Pointer;
}

/************************************************************************/

BOOL DeleteObject(HANDLE Object) {
    LINEAR ObjectAddress = (LINEAR)Object;

    SAFE_USE(ObjectAddress) {
        LPOBJECT KernelObject = (LPOBJECT)ObjectAddress;
        UINT Result = 0;

        SAFE_USE_VALID(KernelObject) {
            switch (KernelObject->TypeID) {
                case KOID_FILE:
                    Result = (UINT)CloseFile((LPFILE)KernelObject);
                    break;
                case KOID_PIPE_ENDPOINT:
                    Result = (UINT)PipeCloseEndpoint((LPVOID)KernelObject);
                    break;
                case KOID_DESKTOP:
                    Result = (UINT)DeleteDesktop((LPDESKTOP)KernelObject);
                    break;
                case KOID_WINDOW:
                    Result = (UINT)DeleteWindow((HANDLE)KernelObject);
                    break;
                case KOID_BRUSH:
                    KernelHeapFree(KernelObject);
                    Result = 1;
                    break;
                case KOID_PEN:
                    KernelHeapFree(KernelObject);
                    Result = 1;
                    break;
                default:
                    WARNING(TEXT("Unsupported object type=%u object=%p"), KernelObject->TypeID, ObjectAddress);
                    Result = 0;
                    break;
            }
        }
        else {
            WARNING(TEXT("Invalid object pointer object=%p"), ObjectAddress);
        }

        return Result != 0 ? TRUE : FALSE;
    }

    WARNING(TEXT("Invalid object address object=%p"), ObjectAddress);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Ensure that a value representing a kernel object is a handle.
 *
 * If the value already lies in user handle space (< VMA_KERNEL), it is
 * returned unchanged. Otherwise the kernel pointer is converted into a
 * handle via PointerToHandle().
 *
 * @param Value Either a kernel pointer or a user-visible handle.
 * @return HANDLE Handle value or 0 on failure.
 */
HANDLE EnsureHandle(LINEAR Value) {
    if (Value == 0) return 0;
    if (Value < VMA_KERNEL) {
        return (HANDLE)Value;
    }

    return PointerToHandle(Value);
}

/************************************************************************/

/**
 * @brief Close one handle and destroy the object only when it is the last handle.
 *
 * @param Handle Handle to close.
 * @return TRUE on success, FALSE on failure.
 */
BOOL CloseHandle(HANDLE Handle) {
    LINEAR ObjectPointer = HandleToPointer(Handle);
    UINT ExistingHandle = 0;

    if (ObjectPointer == 0) {
        return FALSE;
    }

    ReleaseHandle(Handle);

    if (HandleMapFindHandleByPointer(GetHandleMap(), ObjectPointer, &ExistingHandle) == HANDLE_MAP_OK) {
        return TRUE;
    }

    return DeleteObject((HANDLE)ObjectPointer);
}

/************************************************************************/

/**
 * @brief Detach and release a handle from the global handle map.
 *
 * @param Handle Handle to release; ignored when 0.
 */
void ReleaseHandle(HANDLE Handle) {
    if (Handle == 0) {
        return;
    }

    LINEAR Pointer = 0;
    UINT Status = HandleMapDetachPointer(GetHandleMap(), Handle, &Pointer);
    if (Status != HANDLE_MAP_OK && Status != HANDLE_MAP_ERROR_NOT_ATTACHED) {
        WARNING(TEXT("Detach failed handle=%u status=%u"), Handle, Status);
    }

    Status = HandleMapReleaseHandle(GetHandleMap(), Handle);
    if (Status != HANDLE_MAP_OK) {
        WARNING(TEXT("Release failed handle=%u status=%u"), Handle, Status);
    }
}

/************************************************************************/

/**
 * @brief Initialize focus defaults and the global input queue.
 */
static void InitializeFocusState(void) {
    LPLIST DesktopList = GetDesktopList();

    SetFocusedProcess(&KernelProcess);

    if (KernelProcess.Desktop == NULL) {
        if (DesktopList != NULL && DesktopList->First != NULL) {
            KernelProcess.Desktop = (LPDESKTOP)DesktopList->First;
        }
    }

    DisplaySessionInitialize();
}

/************************************************************************/

/**
 * @brief Initializes the kernel minimum quantum time.
 *
 * Accounts for debug output overhead which slows execution.
 * Sets Kernel.MinimumQuantum and Kernel.MaximumQuantum to appropriate value.
 */
void InitializeQuantumTime(void) {
    SetMinimumQuantum(1);

    if (SCHEDULING_DEBUG_OUTPUT == 1) {
        // Double quantum when scheduling debug is enabled (logs slow down execution)
        SetMinimumQuantum(GetMinimumQuantum() * 2);
    }

    SetMaximumQuantum(GetMinimumQuantum() * 8);
}

/************************************************************************/

/**
 * @brief Logs memory map and kernel startup information.
 */

void DumpCriticalInformation(void) {
    DEBUG(TEXT("  Multiboot entry count = %d"), KernelStartup.MultibootMemoryEntryCount);

    for (U32 Index = 0; Index < KernelStartup.MultibootMemoryEntryCount; Index++) {
        DEBUG(
            TEXT("Multiboot entry %d : %p, %d, %d"), Index, U64_Low32(KernelStartup.MultibootMemoryEntries[Index].Base),
            U64_Low32(KernelStartup.MultibootMemoryEntries[Index].Length),
            (U32)KernelStartup.MultibootMemoryEntries[Index].Type);
    }

    DEBUG(TEXT("Virtual addresses"));
    DEBUG(TEXT("  VMA_RAM = %p"), VMA_RAM);
    DEBUG(TEXT("  VMA_VIDEO = %p"), VMA_VIDEO);
    DEBUG(TEXT("  VMA_CONSOLE = %p"), VMA_CONSOLE);
    DEBUG(TEXT("  VMA_USER = %p"), VMA_USER);
    DEBUG(TEXT("  VMA_USER_LIMIT = %p"), VMA_USER_LIMIT);
    DEBUG(TEXT("  VMA_KERNEL = %p"), VMA_KERNEL);

    DEBUG(TEXT("Kernel startup info:"));
    DEBUG(TEXT("  KernelPhysicalBase = %p"), KernelStartup.KernelPhysicalBase);
    DEBUG(TEXT("  KernelSize = %d"), KernelStartup.KernelSize);
    DEBUG(TEXT("  KernelReservedBytes = %u"), KernelStartup.KernelReservedBytes);
    DEBUG(TEXT("  StackTop = %p"), KernelStartup.StackTop);
    DEBUG(TEXT("  IRQMask_21_RM = %x"), KernelStartup.IRQMask_21_RM);
    DEBUG(TEXT("  IRQMask_A1_RM = %x"), KernelStartup.IRQMask_A1_RM);
    DEBUG(TEXT("  MemorySize = %d"), KernelStartup.MemorySize);
    DEBUG(TEXT("  PageCount = %d"), KernelStartup.PageCount);
}

/************************************************************************/

/**
 * @brief Prints memory information and the operating system banner.
 */

static void Welcome(void) {
    /*
        ConsolePrint(TEXT("███████╗██╗  ██╗ ██████╗ ███████╗\n"));
        ConsolePrint(TEXT("██╔════╝╚██╗██╔╝██╔═══██╗██╔════╝\n"));
        ConsolePrint(TEXT("█████╗   ╚═╝╚═╝ ██║   ██║███████╗\n"));
        ConsolePrint(TEXT("██╔══╝   ██╔██╗ ██║   ██║╚════██║\n"));
        ConsolePrint(TEXT("███████╗██╔╝ ██╗╚██████╔╝███████║\n"));
        ConsolePrint(TEXT("╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝\n"));
    */

    /*
        ConsolePrint(TEXT("#######\\ ##\\  ##\\  ######\\  #######\\ \n"));
        ConsolePrint(TEXT("##<----/ \\##\\##// ##/---##\\ ##/----/ \n"));
        ConsolePrint(TEXT("#####\\    \\-/\\-/  ##|   ##| #######\\ \n"));
        ConsolePrint(TEXT("##/--/    ##/##\\  ##|   ##| \\----##|  \n"));
        ConsolePrint(TEXT("#######\\ ##// ##\\ \\######// #######| \n"));
        ConsolePrint(TEXT("\\------/ \\-/  \\-/  \\-----/  \\------/ \n\n"));
    */

    ConsolePrint(
        TEXT("Extensible Operating System for %s computers\n"
             "Version %u.%u.%u - Copyright (c) %u-%u Jango73\n"),
        Text_Architecture, EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR, EXOS_VERSION_PATCH, EXOS_COPYRIGHT_FROM,
        EXOS_COPYRIGHT_TO);

    ConsolePrint(TEXT("\n%s\n\n"), GetRandomQuote());

    /*
        ConsolePrint(TEXT("\nEXOS - "));
        SetConsoleBackColor(CONSOLE_BLUE);
        SetConsoleForeColor(CONSOLE_WHITE);
        ConsolePrint("Extensible");
        SetConsoleBackColor(CONSOLE_WHITE);
        SetConsoleForeColor(CONSOLE_BLACK);
        ConsolePrint(" Operating");
        SetConsoleBackColor(CONSOLE_RED);
        SetConsoleForeColor(CONSOLE_WHITE);
        ConsolePrint(" System   ");
        SetConsoleBackColor(0);
        SetConsoleForeColor(CONSOLE_GRAY);
        ConsolePrint(
            TEXT("\n"
            "EXOS - Extensible Operating System for %s computers\n"
            "Version %u.%u.%u - Copyright (c) 1999-2026 Jango73\n"),
            Text_Architecture,
            EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR, EXOS_VERSION_PATCH
            );
    */

    SetConsoleBackColor(0);
}

/************************************************************************/

/**
 * @brief Create a kernel object with standard LISTNODE_FIELDS initialization.
 *
 * This function allocates memory for a kernel object and initializes its
 * LISTNODE_FIELDS with the specified ID, References = 1, current process
 * as parent, and NULL for Next/Prev pointers.
 *
 * @param Size Size of the object to allocate (e.g., sizeof(TASK))
 * @param ObjectTypeID ID from ID.h to identify the object type
 * @return Pointer to the allocated and initialized object, or NULL on failure
 */
LPVOID CreateKernelObject(UINT Size, U32 ObjectTypeID) {
    LPLISTNODE Object;
    U8 Identifier[UUID_BINARY_SIZE];
    U64 ObjectInstanceID = U64_0;

    Object = (LPLISTNODE)KernelHeapAlloc(Size);

    if (Object == NULL) {
        ERROR(TEXT("Failed to allocate memory for object type %d"), ObjectTypeID);
        return NULL;
    }

    MemorySet(Object, 0, Size);

    // Initialize LISTNODE_FIELDS
    UUID_Generate(Identifier);
    ObjectInstanceID = UUID_ToU64(Identifier);

    Object->TypeID = ObjectTypeID;
    Object->References = 1;
    Object->OwnerProcess = GetCurrentProcess();
    Object->InstanceID = ObjectInstanceID;
    Object->Destructor = NULL;
    Object->Next = NULL;
    Object->Prev = NULL;
    Object->Parent = NULL;

    return Object;
}

/************************************************************************/

/**
 * @brief Store one type-specific destructor on one kernel object.
 *
 * @param Object Target kernel object.
 * @param Destructor Destructor function called when the object is purged.
 */
void SetKernelObjectDestructor(LPVOID Object, OBJECTDESTRUCTOR Destructor) {
    LPLISTNODE Node = (LPLISTNODE)Object;

    SAFE_USE(Node) { Node->Destructor = Destructor; }
}

/************************************************************************/

/**
 * @brief Destroy one kernel object through its registered destructor.
 *
 * @param Object Kernel object to destroy.
 */
void DestroyKernelObject(LPVOID Object) {
    LPLISTNODE Node = (LPLISTNODE)Object;
    OBJECTDESTRUCTOR Destructor = NULL;

    SAFE_USE_VALID(Node) {
        Destructor = Node->Destructor;

        if (Destructor != NULL) {
            Destructor(Node);
        } else {
            Node->TypeID = KOID_NONE;
            KernelHeapFree(Node);
        }
    }
}

/************************************************************************/

/**
 * @brief Destroy a kernel object.
 *
 * This function sets the object's ID to KOID_NONE and frees its memory.
 *
 * @param Object Pointer to the kernel object to destroy
 */
void ReleaseKernelObject(LPVOID Object) {
    LPLISTNODE Node = (LPLISTNODE)Object;

    SAFE_USE(Node) {
        if (Node->References) Node->References--;
    }
}

/************************************************************************/

/**
 * @brief Delete unreferenced kernel objects from all kernel lists.
 *
 * This function traverses all kernel object lists and removes objects
 * with a reference count of 0, setting their ID to KOID_NONE and freeing
 * their memory with KernelHeapFree.
 */
void DeleteUnreferencedObjects(void) {
    U32 DeletedCount = 0;

    // Helper function to process a single list
    auto void ProcessList(LPLIST List, LPCSTR ListName) {
        UNUSED(ListName);  // To avoid warnings in release

        if (List == NULL) return;

        LPLISTNODE Current = (LPLISTNODE)List->First;

        while (Current != NULL) {
            LPLISTNODE Next = (LPLISTNODE)Current->Next;

            // Check if object has no references
            if (Current->References == 0) {
                // Remove from list first
                ListRemove(List, Current);
                DestroyKernelObject(Current);

                DeletedCount++;
            }

            Current = Next;
        }
    }

    LockMutex(MUTEX_KERNEL, INFINITY);

    // Process all kernel object lists
    ProcessList(GetDesktopList(), TEXT("Desktop"));
    ProcessList(GetProcessList(), TEXT("Process"));
    ProcessList(GetTaskList(), TEXT("Task"));
    ProcessList(GetMutexList(), TEXT("Mutex"));
    ProcessList(GetDiskList(), TEXT("Disk"));
    ProcessList(GetUsbDeviceList(), TEXT("USBDevice"));
    ProcessList(GetUsbInterfaceList(), TEXT("USBInterface"));
    ProcessList(GetUsbEndpointList(), TEXT("USBEndpoint"));
    ProcessList(GetUsbStorageList(), TEXT("USBStorage"));
    ProcessList(GetPCIDeviceList(), TEXT("PCIDevice"));
    ProcessList(GetNetworkDeviceList(), TEXT("NetworkDevice"));
    ProcessList(GetEventList(), TEXT("KernelEvent"));
    ProcessList(GetFileSystemList(), TEXT("FileSystem"));
    ProcessList(GetFileList(), TEXT("File"));
    ProcessList(GetExecutableModuleImageList(), TEXT("ExecutableModuleImage"));
    ProcessList(GetTCPConnectionList(), TEXT("TCPConnection"));
    ProcessList(GetSocketList(), TEXT("Socket"));

    UnlockMutex(MUTEX_KERNEL);
}

/************************************************************************/

static void ReleaseProcessObjectsFromList(LPPROCESS Process, LPLIST List) {
    SAFE_USE(List) {
        LPLISTNODE Node = List->First;

        while (Node != NULL) {
            LPLISTNODE NextNode = Node->Next;

            SAFE_USE_VALID(Node) {
                if (Node->OwnerProcess == Process) {
                    ReleaseKernelObject(Node);
                }
            }

            Node = NextNode;
        }
    }
}

/************************************************************************/

/**
 * @brief Releases references held by a process on all kernel lists.
 *
 * Iterates every kernel-maintained list defined in KernelData.c and calls
 * ReleaseKernelObject() for each object owned by the specified process.
 * The caller must hold MUTEX_KERNEL to ensure list consistency while this
 * routine walks the structures.
 *
 * @param Process Process whose owned kernel objects must be released.
 */
void ReleaseProcessKernelObjects(struct tag_PROCESS* Process) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process == &KernelProcess) {
            return;
        }

        // Process all kernel object lists
        ReleaseProcessObjectsFromList(Process, GetDesktopList());
        ReleaseProcessObjectsFromList(Process, GetProcessList());
        ReleaseProcessObjectsFromList(Process, GetTaskList());
        ReleaseProcessObjectsFromList(Process, GetMutexList());
        ReleaseProcessObjectsFromList(Process, GetDiskList());
        ReleaseProcessObjectsFromList(Process, GetUsbDeviceList());
        ReleaseProcessObjectsFromList(Process, GetUsbStorageList());
        ReleaseProcessObjectsFromList(Process, GetPCIDeviceList());
        ReleaseProcessObjectsFromList(Process, GetNetworkDeviceList());
        ReleaseProcessObjectsFromList(Process, GetEventList());
        ReleaseProcessObjectsFromList(Process, GetFileSystemList());
        ReleaseProcessObjectsFromList(Process, GetFileList());
        ReleaseProcessObjectsFromList(Process, GetTCPConnectionList());
        ReleaseProcessObjectsFromList(Process, GetSocketList());
    }
}

/************************************************************************/

/**
 * @brief Store object termination state in cache.
 * @param Object Handle of the terminated object
 * @param ExitCode Exit code of the object
 */
void StoreObjectTerminationState(LPVOID Object, UINT ExitCode) {
    LPOBJECT KernelObject = (LPOBJECT)Object;

    SAFE_USE_VALID(KernelObject) {
        LPOBJECT_TERMINATION_STATE TermState =
            (LPOBJECT_TERMINATION_STATE)KernelHeapAlloc(sizeof(OBJECT_TERMINATION_STATE));

        SAFE_USE(TermState) {
            U32 InstanceIDHigh = U64_High32(KernelObject->InstanceID);
            U32 InstanceIDLow = U64_Low32(KernelObject->InstanceID);

            TermState->Object = KernelObject;
            TermState->ExitCode = ExitCode;
            TermState->InstanceID = KernelObject->InstanceID;
            CacheAdd(GetObjectTerminationCache(), TermState, OBJECT_TERMINATION_TTL_MS);

            UNUSED(InstanceIDHigh);
            UNUSED(InstanceIDLow);
        }

        return;
    }

    WARNING(TEXT("Invalid kernel object pointer %x"), Object);
}

/************************************************************************/

/**
 * @brief Selects keyboard layout based on configuration.
 *
 * Reads the layout from the parsed configuration and applies it with
 * SelectKeyboard.
 */

static void UseConfiguration(void) {
    LPTOML Configuration = GetConfiguration();

    SAFE_USE(Configuration) {
        LPCSTR Layout;
        LPCSTR QuantumMS;
        LPCSTR DoLogin;

        Layout = TomlGet(Configuration, TEXT("Keyboard.Layout"));

        if (Layout) {
            SetKeyboardCode(Layout);
        } else {
            ConsolePrint(TEXT("Keyboard layout not found in config, using default en-US\n"));
            SetKeyboardCode(TEXT("en-US"));
        }

        QuantumMS = TomlGet(Configuration, TEXT(CONFIG_GENERAL_QUANTUM_MS));

        if (STRING_EMPTY(QuantumMS) == FALSE) {
            SetMinimumQuantum(StringToU32(QuantumMS));
        }

        LPCSTR UseDeadlockMonitor = TomlGet(Configuration, TEXT("Debug.UseDeadlockMonitor"));

        if (STRING_EMPTY(UseDeadlockMonitor) == FALSE) {
            SetUseDeadlockMonitor((StringToU32(UseDeadlockMonitor) != 0));
        } else {
            SetUseDeadlockMonitor(FALSE);
        }

        DoLogin = TomlGet(Configuration, TEXT("General.DoLogin"));

        if (STRING_EMPTY(DoLogin) == FALSE) {
            SetDoLogin((StringToU32(DoLogin) != 0));
        } else {
            SetDoLogin(TRUE);
        }

        if (GetDoLogin() == FALSE) {
            ConsolePrint(TEXT("WARNING : Login sequence disabled\n"));
        }
    }

    // Ensure a keyboard code is always set, even if configuration failed
    if (StringEmpty(GetKeyboardCode())) {
        SetKeyboardCode(TEXT("en-US"));
    }

#if DEBUG_OUTPUT == 1 || SCHEDULING_DEBUG_OUTPUT == 1
    ConsolePrint(TEXT("WARNING : This is a debug build\n\n"));
#endif
}

/************************************************************************/

/**
 * @brief Calculates the amount of physical memory currently in use.
 *
 * Traverses the page bitmap and counts allocated pages.
 *
 * @return Number of bytes of physical memory used.
 */

UINT GetPhysicalMemoryUsed(void) {
    UINT NumPages = 0;

    LockMutex(MUTEX_MEMORY, INFINITY);
    NumPages = BuddyGetUsedPageCount();
    UnlockMutex(MUTEX_MEMORY);

    return (NumPages << PAGE_SIZE_MUL);
}

/************************************************************************/

/**
 * @brief Loads a driver and performs basic validation.
 *
 * Verifies the magic ID and invokes the load command.
 *
 * @param Driver Pointer to driver structure.
 */

void LoadDriver(LPDRIVER Driver) {
    SAFE_USE(Driver) {
        if (Driver->TypeID != KOID_DRIVER) {
            ERROR(
                TEXT("%s driver not valid (at address %X). ID = %X. Halting."), TEXT(Driver->Product), Driver,
                Driver->TypeID);

            // Wait forever
            DO_THE_SLEEPING_BEAUTY;
        }

        UINT Result = Driver->Command(DF_LOAD, 0);

        if (Result == DF_RETURN_SUCCESS && (Driver->Flags & DRIVER_FLAG_READY) != 0) {
            TEST(TEXT("%s.Load : OK"), TEXT(Driver->Product));
        } else {
            TEST(TEXT("%s.Load : KO"), TEXT(Driver->Product));
            if ((Driver->Flags & DRIVER_FLAG_CRITICAL)) {
                ConsolePanic(TEXT("Critical driver %s failed to load"), TEXT(Driver->Product));
            } else if (Result == DF_RETURN_HARDWARE_ABSENT) {
                WARNING(TEXT("%s driver not loaded: hardware absent"), TEXT(Driver->Product));
            } else {
                ERROR(TEXT("Failed to load %s driver (code = %x)"), TEXT(Driver->Product), Result);
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Unload a driver by invoking its DF_UNLOAD command.
 *
 * Logs the unload attempt, validates the driver descriptor and reports
 * failures while allowing shutdown to continue.
 *
 * @param Driver Pointer to driver structure.
 */
void UnloadDriver(LPDRIVER Driver) {
    SAFE_USE(Driver) {
        if (Driver->TypeID != KOID_DRIVER) {
            WARNING(
                TEXT("%s driver not valid (at address %X). ID = %X."), TEXT(Driver->Product), Driver, Driver->TypeID);
            return;
        }

        UINT Result = Driver->Command(DF_UNLOAD, 0);
        if (Result == DF_RETURN_SUCCESS) {
            TEST(TEXT("%s.Unload : OK"), TEXT(Driver->Product));
        } else {
            TEST(TEXT("%s.Unload : KO"), TEXT(Driver->Product));
            WARNING(TEXT("Failed to unload %s driver (code = %x)"), TEXT(Driver->Product), Result);
        }
    }
}

/************************************************************************/

void LoadAllDrivers(void) {
    DEBUG(TEXT("Start"));

    InitializeDriverList();
    DEBUG(TEXT("Driver list initialized"));

    LPLIST DriverList = GetStartupDriverList();
    if (DriverList == NULL || DriverList->First == NULL) {
        DEBUG(TEXT("No drivers to load"));
        return;
    }

    U32 DriverIndex = 0;
    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next, DriverIndex++) {
        LPDRIVER Driver = ((LPSTARTUP_DRIVER_ENTRY)Node)->Driver;
        SAFE_USE(Driver) { DEBUG(TEXT("Driver[%u] loading %s @ %p"), DriverIndex, Driver->Product, Driver); }
        LoadDriver(Driver);
        SAFE_USE(Driver) { DEBUG(TEXT("Driver[%u] load done flags=%x"), DriverIndex, Driver->Flags); }
    }

    DEBUG(TEXT("Complete (%u drivers)"), DriverIndex);
}

/************************************************************************/

/**
 * @brief Unloads all drivers in reverse initialization order.
 *
 * Walks the driver list from tail to head and dispatches DF_UNLOAD to
 * each registered driver.
 */
void UnloadAllDrivers(void) {
    LPLIST DriverList = GetStartupDriverList();
    if (DriverList == NULL || DriverList->Last == NULL) {
        return;
    }

    for (LPLISTNODE Node = DriverList->Last; Node; Node = Node->Prev) {
        UnloadDriver(((LPSTARTUP_DRIVER_ENTRY)Node)->Driver);
    }
}

/************************************************************************/

static void KillActiveUserlandProcesses(void);
static void KillActiveKernelTasks(void);

/**
 * @brief Common pre-shutdown sequence used by power actions.
 *
 * Kills active userland processes, then kernel tasks, then unloads all
 * drivers in reverse initialization order.
 */
static void PrepareForPowerTransition(void) {
    KillActiveUserlandProcesses();
    KillActiveKernelTasks();
    UnloadAllDrivers();
}

/************************************************************************/

static U32 KernelMonitor(LPVOID Parameter) {
    UNUSED(Parameter);
    U32 LogCounter = 0;

    FOREVER {
        DeleteDeadTasksAndProcesses();
        DeleteUnreferencedObjects();
        CacheCleanup(GetObjectTerminationCache(), GetSystemTime());

        LogCounter++;
        if (LogCounter >= 60) {  // 60 * 500ms = 30 seconds
            LogCounter = 0;
        }

        Sleep(500);
    }

    return 0;
}

/************************************************************************/

void KernelIdle(void) {
    ConsoleSetPagingActive(TRUE);

    FOREVER { Sleep(4000); }
}

/************************************************************************/

/**
 * @brief Terminates all userland processes without signaling.
 *
 * Collects active ring-3 processes and immediately kills them. This
 * routine builds a temporary list under MUTEX_PROCESS to avoid holding
 * the lock while issuing KillProcess().
 */
static void KillActiveUserlandProcesses(void) {
    LPLIST ProcessesToKill = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (ProcessesToKill == NULL) {
        WARNING(TEXT("Unable to allocate kill list"));
        return;
    }

    LockMutex(MUTEX_PROCESS, INFINITY);

    LPLIST ProcessList = GetProcessList();
    SAFE_USE(ProcessList) {
        for (LPPROCESS Process = (LPPROCESS)ProcessList->First; Process; Process = (LPPROCESS)Process->Next) {
            SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
                if (Process != &KernelProcess && Process->Privilege == CPU_PRIVILEGE_USER &&
                    Process->Status != PROCESS_STATUS_DEAD) {
                    ListAddTail(ProcessesToKill, Process);
                }
            }
        }
    }

    UnlockMutex(MUTEX_PROCESS);

    for (LPLISTNODE Node = ProcessesToKill->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) { KillProcess(Process); }
    }

    DeleteList(ProcessesToKill);
}

/************************************************************************/

/**
 * @brief Terminates all kernel tasks except the main kernel task.
 *
 * Collects tasks attached to the kernel process and flags them dead
 * through KernelKillTask(). The main kernel task is left running.
 */
static void KillActiveKernelTasks(void) {
    LPLIST TasksToKill = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (TasksToKill == NULL) {
        WARNING(TEXT("Unable to allocate kill list"));
        return;
    }

    LockMutex(MUTEX_TASK, INFINITY);

    LPLIST TaskList = GetTaskList();
    SAFE_USE(TaskList) {
        for (LPTASK Task = (LPTASK)TaskList->First; Task; Task = (LPTASK)Task->Next) {
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->OwnerProcess == &KernelProcess && Task->Type != TASK_TYPE_KERNEL_MAIN &&
                    Task->SchedulerState.Status != TASK_STATUS_DEAD) {
                    ListAddTail(TasksToKill, Task);
                }
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    for (LPLISTNODE Node = TasksToKill->First; Node; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            DEBUG(TEXT("Killing task %s"), Task->Name);
            KernelKillTask(Task);
        }
    }

    DeleteList(TasksToKill);
}

/************************************************************************/

/**
 * @brief Entry point for kernel initialization.
 *
 * Sets up core services, loads drivers, mounts file systems and starts
 * the initial shell task.
 *
 */

void InitializeKernel(void) {
    TASK_INFO TaskInfo;

    DEBUG(TEXT("Start"));

    GetCPUInformation(GetKernelCPUInfo());
    DEBUG(TEXT("CPU information captured"));
    BusyWaitSetFrequencyMHz(GetKernelCPUInfo()->BaseFrequencyMHz);
    DEBUG(
        TEXT("BusyWait profile base_mhz=%u loops_per_ms=%u"), GetKernelCPUInfo()->BaseFrequencyMHz,
        BusyWaitGetLoopsPerMillisecond());
    PreInitializeKernel();
    DEBUG(TEXT("Architecture pre-initialization complete"));
    //-------------------------------------
    // Load all drivers

    LoadAllDrivers();
    DEBUG(TEXT("Drivers loaded"));

    //-------------------------------------
    // Initialize stuff

    CacheInit(GetObjectTerminationCache(), CACHE_DEFAULT_CAPACITY);
    DEBUG(TEXT("Object cache initialized"));

    HandleMapInit(GetHandleMap());
    DEBUG(TEXT("Handle map initialized"));

    InitializeFocusState();
    DEBUG(TEXT("Focus state initialized"));

    InitializeQuantumTime();
    DEBUG(TEXT("Quantum initialized"));

    //-------------------------------------
    // Set configuration dependent stuff

    UseConfiguration();
    DEBUG(TEXT("Configuration applied"));

    //-------------------------------------
    // Run auto tests

    RunAllTests();
    DEBUG(TEXT("Tests completed"));

    //-------------------------------------
    // Print the EXOS banner

    Welcome();
    DEBUG(TEXT("Banner printed"));

    //-------------------------------------
    // Enable interrupts

    EnableInterrupts();
    MarkSystemTimeOperational();
    DEBUG(TEXT("Interrupts enabled"));

    // Load keyboard layout only after interrupts are active.
    // This avoids early boot fragility caused by filesystem access
    // during the pre-interrupt initialization phase.
    SelectKeyboard(GetKeyboardCode());
    DEBUG(TEXT("Keyboard layout applied"));

    //-------------------------------------

#if SYSTEM_DATA_VIEW == 1
    SystemDataViewMode();
#endif

    //-------------------------------------

    LPTOML Configuration = GetConfiguration();
    LPCSTR Mono = NULL;

    SAFE_USE(Configuration) { Mono = TomlGet(Configuration, TEXT("General.Mono")); }

    if (STRING_EMPTY(Mono) == FALSE && StringCompare(Mono, TEXT("1")) == 0) {
        Shell(NULL);
    } else {
        //-------------------------------------
        // Kernel monitor

        TaskInfo.Header.Size = sizeof(TASK_INFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = KernelMonitor;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("KernelMonitor"));

        KernelCreateTask(&KernelProcess, &TaskInfo);
        DEBUG(TEXT("KernelMonitor task created"));

        //-------------------------------------
        // Shell task

        TaskInfo.Header.Size = sizeof(TASK_INFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = Shell;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_HIGHER;
        TaskInfo.Flags = 0;
        StringCopy(TaskInfo.Name, TEXT("Shell"));

        KernelCreateTask(&KernelProcess, &TaskInfo);
        DEBUG(TEXT("Shell task created"));
    }

    //--------------------------------------
    // Enter idle

    DEBUG(TEXT("Entering idle loop"));
    KernelIdle();
}

/************************************************************************/

/**
 * @brief Performs a clean shutdown then powers off through ACPI.
 *
 * Drivers are unloaded in reverse initialization order before invoking
 * ACPIPowerOff().
 */
void ShutdownKernel(void) {
    PrepareForPowerTransition();
    ACPIPowerOff();
}

/************************************************************************/

/**
 * @brief Performs a clean shutdown then reboots through ACPI.
 *
 * Drivers are unloaded in reverse initialization order before invoking
 * ACPIReboot().
 */
void RebootKernel(void) {
    PrepareForPowerTransition();
    ACPIReboot();
}
