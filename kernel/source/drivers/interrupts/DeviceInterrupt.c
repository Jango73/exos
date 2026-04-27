
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


    Device interrupt management

\************************************************************************/

#include "drivers/interrupts/DeviceInterrupt.h"

#include "User.h"
#include "drivers/interrupts/InterruptController.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "sync/Deferred-Work.h"
#include "text/CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

#define DEVICE_INTERRUPT_VER_MAJOR 1
#define DEVICE_INTERRUPT_VER_MINOR 0

#define DEVICE_INTERRUPT_SPURIOUS_THRESHOLD 64

/************************************************************************/

typedef struct tag_DEVICE_INTERRUPT_SLOT {
    BOOL InUse;
    LPDEVICE Device;
    U32 DeviceTypeID;
    U8 LegacyIRQ;
    U8 TargetCPU;
    DEVICE_INTERRUPT_ISR InterruptHandler;
    DEVICE_INTERRUPT_BOTTOM_HALF DeferredCallback;
    DEVICE_INTERRUPT_POLL PollCallback;
    LPVOID Context;
    DEFERRED_WORK_TOKEN DeferredToken;
    BOOL InterruptEnabled;
    STR Name[32];
} DEVICE_INTERRUPT_SLOT, *LPDEVICE_INTERRUPT_SLOT;

typedef struct tag_DEVICE_INTERRUPT_ENTRY {
    DEVICE_INTERRUPT_SLOT Slot;
    U32 InterruptCount;
    U32 DeferredCount;
    U32 PollCount;
    U32 SuppressedCount;
} DEVICE_INTERRUPT_ENTRY, *LPDEVICE_INTERRUPT_ENTRY;

/************************************************************************/

static LPDEVICE_INTERRUPT_ENTRY DATA_SECTION g_DeviceInterruptEntries = NULL;
static U32 DATA_SECTION g_DeviceInterruptEntriesSize = 0;
static U8 g_DeviceInterruptSlotCount = DEVICE_INTERRUPT_VECTOR_DEFAULT;

/************************************************************************/

static void DeviceInterruptDeferredThunk(LPVOID Context);
static void DeviceInterruptPollThunk(LPVOID Context);
static void DeviceInterruptApplyConfiguration(void);
static BOOL DeviceInterruptAllocateEntries(void);
static LPDEVICE_INTERRUPT_ENTRY DeviceInterruptGetEntry(U32 SlotIndex);
static UINT DeviceInterruptDriverCommands(UINT Function, UINT Parameter);

/************************************************************************/

DRIVER DeviceInterruptDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INTERRUPT,
    .VersionMajor = DEVICE_INTERRUPT_VER_MAJOR,
    .VersionMinor = DEVICE_INTERRUPT_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "DeviceInterrupts",
    .Alias = "device_interrupt",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = DeviceInterruptDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the device interrupt driver descriptor.
 * @return Pointer to the device interrupt driver.
 */
LPDRIVER DeviceInterruptGetDriver(void) { return &DeviceInterruptDriver; }

/************************************************************************/

/**
 * @brief Returns the number of device interrupt slots available.
 *
 * Clamps the configured slot count to the supported range to avoid invalid
 * vector indices.
 *
 * @return Active slot count.
 */
U8 DeviceInterruptGetSlotCount(void) {
    U8 SlotCount = g_DeviceInterruptSlotCount;

    if (SlotCount == 0) {
        SlotCount = 1;
    }

    if (SlotCount > DEVICE_INTERRUPT_VECTOR_MAX) {
        SlotCount = DEVICE_INTERRUPT_VECTOR_MAX;
    }

    return SlotCount;
}

/************************************************************************/

/**
 * @brief Applies configuration to determine the active slot count.
 *
 * Reads the configured slot count, clamps it to the supported range, and
 * initializes the global slot count used by the interrupt dispatcher.
 */
static void DeviceInterruptApplyConfiguration(void) {
    g_DeviceInterruptSlotCount = DEVICE_INTERRUPT_VECTOR_DEFAULT;

    LPCSTR SlotCountValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEVICE_INTERRUPT_SLOTS));
    if (STRING_EMPTY(SlotCountValue) == FALSE) {
        U32 Requested = StringToU32(SlotCountValue);

        if (Requested == 0) {
            WARNING(TEXT("Requested slot count is zero, forcing minimum of 1"));
            Requested = 1;
        }

        if (Requested > DEVICE_INTERRUPT_VECTOR_MAX) {
            WARNING(TEXT("Requested slot count %u exceeds capacity %u"), Requested, DEVICE_INTERRUPT_VECTOR_MAX);
            Requested = DEVICE_INTERRUPT_VECTOR_MAX;
        }

        g_DeviceInterruptSlotCount = (U8)Requested;
    }

    if (g_DeviceInterruptSlotCount == 0) {
        g_DeviceInterruptSlotCount = 1;
    }

    DEBUG(TEXT("Active slots=%u (capacity=%u)"), g_DeviceInterruptSlotCount, DEVICE_INTERRUPT_VECTOR_MAX);
}

/************************************************************************/

/**
 * @brief Allocates or clears device interrupt slot storage.
 *
 * @return TRUE when storage is ready for use, FALSE on allocation failure or
 * invalid configuration.
 */
static BOOL DeviceInterruptAllocateEntries(void) {
    const U8 SlotCount = g_DeviceInterruptSlotCount;
    if (SlotCount == 0) {
        ERROR(TEXT("Slot count is zero"));
        return FALSE;
    }

    if (g_DeviceInterruptEntries != NULL) {
        MemorySet(g_DeviceInterruptEntries, 0, g_DeviceInterruptEntriesSize);
        return TRUE;
    }

    U32 AllocationSize = (U32)SlotCount * (U32)sizeof(DEVICE_INTERRUPT_ENTRY);
    U32 PageMask = PAGE_SIZE - 1;
    AllocationSize = (AllocationSize + PageMask) & ~PageMask;

    LINEAR Buffer =
        AllocKernelRegion(0, AllocationSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("DeviceInterrupt"));
    if (Buffer == 0) {
        ERROR(TEXT("AllocKernelRegion failed (size=%u)"), AllocationSize);
        return FALSE;
    }

    g_DeviceInterruptEntries = (LPDEVICE_INTERRUPT_ENTRY)Buffer;
    g_DeviceInterruptEntriesSize = AllocationSize;
    MemorySet(g_DeviceInterruptEntries, 0, g_DeviceInterruptEntriesSize);

    DEBUG(TEXT("Allocated %u bytes for %u slots"), AllocationSize, SlotCount);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieves a slot entry by index.
 *
 * @param SlotIndex Slot index to fetch.
 * @return Pointer to slot entry or NULL if out of range/uninitialized.
 */
static LPDEVICE_INTERRUPT_ENTRY DeviceInterruptGetEntry(U32 SlotIndex) {
    if (g_DeviceInterruptEntries == NULL) {
        return NULL;
    }

    if (SlotIndex >= (U32)g_DeviceInterruptSlotCount) {
        return NULL;
    }

    return &g_DeviceInterruptEntries[SlotIndex];
}

/***************************************************************************/

/**
 * @brief Initializes device interrupt subsystem.
 *
 * Applies configuration and ensures slot storage is ready for registrations.
 */
void InitializeDeviceInterrupts(void) {
    DeviceInterruptApplyConfiguration();
    if (!DeviceInterruptAllocateEntries()) {
        ERROR(TEXT("Failed to allocate slot storage"));
        return;
    }
    DEBUG(TEXT("Device interrupt slots cleared"));
}

/************************************************************************/

/**
 * @brief Registers a device interrupt slot.
 *
 * @param Registration Registration parameters for the device.
 * @param AssignedSlot Optional output for the allocated slot index.
 * @return TRUE on success, FALSE when no slot is available or setup fails.
 */
BOOL DeviceInterruptRegister(const DEVICE_INTERRUPT_REGISTRATION *Registration, U8 *AssignedSlot) {
    if (Registration == NULL || Registration->Device == NULL || Registration->InterruptHandler == NULL) {
        ERROR(TEXT("Invalid registration parameters"));
        return FALSE;
    }

    if (g_DeviceInterruptEntries == NULL) {
        ERROR(TEXT("Slot storage not initialized"));
        return FALSE;
    }

    const U8 SlotCount = DeviceInterruptGetSlotCount();

    for (U32 Index = 0; Index < SlotCount; Index++) {
        LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(Index);
        if (Entry == NULL) {
            continue;
        }

        LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;

        if (Slot->InUse) {
            continue;
        }

        MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
        Entry->InterruptCount = 0;
        Entry->DeferredCount = 0;
        Entry->PollCount = 0;
        Entry->SuppressedCount = 0;
        Slot->InUse = TRUE;
        Slot->Device = Registration->Device;
        Slot->DeviceTypeID = ((LPLISTNODE)Registration->Device)->TypeID;
        Slot->LegacyIRQ = Registration->LegacyIRQ;
        Slot->TargetCPU = Registration->TargetCPU;
        Slot->InterruptHandler = Registration->InterruptHandler;
        Slot->DeferredCallback = Registration->DeferredCallback;
        Slot->PollCallback = Registration->PollCallback;
        Slot->Context = Registration->Context;
        MemorySet(Slot->Name, 0, sizeof(Slot->Name));
        if (Registration->Name != NULL) {
            StringCopyLimit(Slot->Name, Registration->Name, sizeof(Slot->Name));
        }
        Slot->InterruptEnabled = FALSE;

        DEFERRED_WORK_REGISTRATION WorkReg = {
            .WorkCallback = DeviceInterruptDeferredThunk,
            .PollCallback = DeviceInterruptPollThunk,
            .Context = (LPVOID)Entry,
            .Name = Slot->Name,
        };

        Slot->DeferredToken = DeferredWorkRegister(&WorkReg);
        if (DeferredWorkTokenIsValid(Slot->DeferredToken) == FALSE) {
            ERROR(TEXT("Failed to register deferred work for slot %u"), Index);
            MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
            return FALSE;
        }

        BOOL HasLegacyIRQ = (Registration->LegacyIRQ != 0xFFU);
        BOOL InterruptConfigured = FALSE;
        BOOL PollingMode = DeferredWorkIsPollingMode();
        BOOL ShouldConfigureInterrupt = (HasLegacyIRQ && !PollingMode);

        if (ShouldConfigureInterrupt) {
            const U8 Vector = GetDeviceInterruptVector((U8)Index);

            if (ConfigureDeviceInterrupt(Registration->LegacyIRQ, Vector, Registration->TargetCPU)) {
                if (EnableDeviceInterrupt(Registration->LegacyIRQ)) {
                    InterruptConfigured = TRUE;
                } else {
                    WARNING(TEXT("Failed to enable IRQ %u"), Registration->LegacyIRQ);
                }
            } else {
                WARNING(TEXT("Failed to configure IRQ %u for vector %u"), Registration->LegacyIRQ, Vector);
            }
        }

        Slot->InterruptEnabled = InterruptConfigured;

        DEBUG(
            TEXT("Slot %u assigned to device %p IRQ %u vector %u"), Index, Registration->Device,
            Registration->LegacyIRQ, GetDeviceInterruptVector((U8)Index));

        if (!ShouldConfigureInterrupt) {
            DEBUG(TEXT("Slot %u operating in polling mode (IRQ setup skipped)"), Index);
        } else if (!InterruptConfigured) {
            DEBUG(TEXT("Slot %u operating in polling mode"), Index);
        }

        if (AssignedSlot != NULL) {
            *AssignedSlot = (U8)Index;
        }

        return TRUE;
    }

    ERROR(TEXT("No free device interrupt slots"));
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Releases a previously registered device interrupt slot.
 *
 * @param SlotIndex Slot index to release.
 * @return TRUE on success, FALSE for invalid slot index or unused slot.
 */
BOOL DeviceInterruptUnregister(U8 SlotIndex) {
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        return FALSE;
    }

    if (Slot->InterruptEnabled) {
        DisableDeviceInterrupt(Slot->LegacyIRQ);
    }
    DeferredWorkUnregister(Slot->DeferredToken);

    DEBUG(TEXT("Slot %u released (IRQ %u)"), SlotIndex, Slot->LegacyIRQ);

    MemorySet(Slot, 0, sizeof(DEVICE_INTERRUPT_SLOT));
    Entry->InterruptCount = 0;
    Entry->DeferredCount = 0;
    Entry->PollCount = 0;
    Entry->SuppressedCount = 0;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Top-half handler for device interrupt vectors.
 *
 * Dispatches to the registered handler and signals deferred work when needed.
 *
 * @param SlotIndex Slot index associated with the interrupt vector.
 */
void DeviceInterruptHandler(U8 SlotIndex) {
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        static U32 DATA_SECTION SpuriousCount = 0;
        if (SpuriousCount < INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("Spurious device interrupt on slot %u"), SlotIndex);
        }
        SpuriousCount++;
        return;
    }

    Entry->InterruptCount++;
    if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
        DEBUG(
            TEXT("Slot=%u IRQ=%u Device=%p Count=%u Enabled=%s"), SlotIndex, Slot->LegacyIRQ, Slot->Device,
            Entry->InterruptCount, Slot->InterruptEnabled ? TEXT("YES") : TEXT("NO"));
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        BOOL ShouldSignal = TRUE;

        if (Slot->InterruptHandler != NULL) {
            ShouldSignal = Slot->InterruptHandler(Slot->Device, Slot->Context);
        }

        if (!ShouldSignal) {
            if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
                DEBUG(TEXT("Slot=%u top-half suppressed deferred execution"), SlotIndex);
            }

            if (Slot->InterruptEnabled && Slot->InterruptHandler != NULL) {
                Entry->SuppressedCount++;
                BOOL ShouldWarn = (Entry->InterruptCount <= 8);
                if (!ShouldWarn && (Entry->InterruptCount & 0xFF) == 0) {
                    ShouldWarn = TRUE;
                }

                if (ShouldWarn) {
                    WARNING(
                        TEXT("Slot=%u IRQ=%u handler suppressed signal while IRQ still armed "
                             "(count=%u)"),
                        SlotIndex, Slot->LegacyIRQ, Entry->InterruptCount);
                }

                if (Entry->SuppressedCount >= DEVICE_INTERRUPT_SPURIOUS_THRESHOLD && Slot->LegacyIRQ != 0xFF) {
                    WARNING(
                        TEXT("Slot=%u IRQ=%u disabled after %u suppressed signals"), SlotIndex, Slot->LegacyIRQ,
                        Entry->SuppressedCount);
                    DisableDeviceInterrupt(Slot->LegacyIRQ);
                    Slot->InterruptEnabled = FALSE;
                    Entry->SuppressedCount = 0;
                    if (Slot->PollCallback != NULL) {
                        WARNING(TEXT("Slot=%u falling back to polling"), SlotIndex);
                    }
                }
            }
        } else {
            Entry->SuppressedCount = 0;
            if (Entry->InterruptCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
                DEBUG(
                    TEXT("Slot=%u signaling deferred queue=%u slot=%u"), SlotIndex, Slot->DeferredToken.QueueID,
                    Slot->DeferredToken.SlotID);
            }
            DeferredWorkSignal(Slot->DeferredToken);
        }
    }
}

/***************************************************************************/

/**
 * @brief Checks whether a slot has interrupts enabled.
 *
 * @param SlotIndex Slot index to query.
 * @return TRUE if the slot is active and interrupt delivery is enabled.
 */
BOOL DeviceInterruptSlotIsEnabled(U8 SlotIndex) {
    if (SlotIndex >= DeviceInterruptGetSlotCount()) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_ENTRY Entry = DeviceInterruptGetEntry(SlotIndex);
    if (Entry == NULL) {
        return FALSE;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse) {
        return FALSE;
    }

    return Slot->InterruptEnabled;
}

/***************************************************************************/

/**
 * @brief Deferred work wrapper for device interrupt bottom halves.
 *
 * @param Context Entry pointer supplied by deferred work dispatcher.
 */
static void DeviceInterruptDeferredThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_ENTRY Entry = (LPDEVICE_INTERRUPT_ENTRY)Context;
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse || Slot->DeferredCallback == NULL) {
        return;
    }

    const U32 SlotIndex = (U32)(Entry - g_DeviceInterruptEntries);
    if (SlotIndex < (U32)DeviceInterruptGetSlotCount()) {
        Entry->DeferredCount++;
        if (Entry->DeferredCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("Slot=%u Name=%s Count=%u"), SlotIndex, Slot->Name, Entry->DeferredCount);
        }
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) {
        Slot->DeferredCallback(Slot->Device, Slot->Context);
    }
}

/***************************************************************************/

/**
 * @brief Polling wrapper executed when interrupts are disabled or unavailable.
 *
 * @param Context Entry pointer supplied by the polling dispatcher.
 */
static void DeviceInterruptPollThunk(LPVOID Context) {
    LPDEVICE_INTERRUPT_ENTRY Entry = (LPDEVICE_INTERRUPT_ENTRY)Context;
    if (Entry == NULL) {
        return;
    }

    LPDEVICE_INTERRUPT_SLOT Slot = &Entry->Slot;
    if (!Slot->InUse || Slot->PollCallback == NULL) {
        return;
    }

    const U32 SlotIndex = (U32)(Entry - g_DeviceInterruptEntries);
    if (SlotIndex < (U32)DeviceInterruptGetSlotCount()) {
        Entry->PollCount++;
        if (Entry->PollCount <= INTERRUPT_LOG_SAMPLE_LIMIT) {
            DEBUG(TEXT("Slot=%u Name=%s Count=%u"), SlotIndex, Slot->Name, Entry->PollCount);
        }
    }

    SAFE_USE_VALID_ID((LPLISTNODE)Slot->Device, Slot->DeviceTypeID) { Slot->PollCallback(Slot->Device, Slot->Context); }
}

/************************************************************************/

/**
 * @brief Driver command handler for device interrupt management.
 *
 * DF_LOAD initializes slot storage and configuration once; DF_UNLOAD only
 * clears readiness.
 */
static UINT DeviceInterruptDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((DeviceInterruptDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeDeviceInterrupts();
            if (g_DeviceInterruptEntries != NULL) {
                DeviceInterruptDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;

        case DF_UNLOAD:
            if ((DeviceInterruptDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            DeviceInterruptDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(DEVICE_INTERRUPT_VER_MAJOR, DEVICE_INTERRUPT_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
