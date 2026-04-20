/************************************************************************\

    EXOS Kernel
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


    Mouse common

\************************************************************************/

#include "input/MouseCommon.h"

#include "Arch.h"
#include "input/MouseDispatcher.h"
#include "log/Log.h"
#include "sync/DeferredWork.h"

static void MouseCommonDeferredWork(LPVOID Context);

/************************************************************************/

/**
 * @brief Initialize mouse state and deferred dispatch.
 * @param Context Mouse common context.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL MouseCommonInitialize(LPMOUSE_COMMON_CONTEXT Context) {
    if (Context == NULL) {
        return FALSE;
    }

    if (Context->Initialized == FALSE) {
        InitMutex(&(Context->Mutex));

        if (InitializeMouseDispatcher() == FALSE) {
            return FALSE;
        }

        Context->DeltaX = 0;
        Context->DeltaY = 0;
        Context->Buttons = 0;
        Context->Packet.DeltaX = 0;
        Context->Packet.DeltaY = 0;
        Context->Packet.Buttons = 0;
        Context->Packet.Pending = FALSE;

        if (DeferredWorkTokenIsValid(Context->DeferredWorkToken) == FALSE) {
            DEFERRED_WORK_REGISTRATION Registration;
            Registration.WorkCallback = MouseCommonDeferredWork;
            Registration.PollCallback = NULL;
            Registration.Context = Context;
            Registration.Name = TEXT("MouseDispatch");

            Context->DeferredWorkToken = DeferredWorkRegisterForQueue(DEFERRED_WORK_QUEUE_FAST, &Registration);
            if (DeferredWorkTokenIsValid(Context->DeferredWorkToken) == FALSE) {
                return FALSE;
            }
        }

        Context->Initialized = TRUE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Queue a mouse packet for deferred dispatch.
 * @param Context Mouse common context.
 * @param DeltaX Signed X delta.
 * @param DeltaY Signed Y delta.
 * @param Buttons Button bitmask.
 */
void MouseCommonQueuePacket(LPMOUSE_COMMON_CONTEXT Context, I32 DeltaX, I32 DeltaY, U32 Buttons) {
    if (Context == NULL) {
        return;
    }

    UINT Flags;
    SaveFlags(&Flags);
    DisableInterrupts();

    Context->Packet.DeltaX += DeltaX;
    Context->Packet.DeltaY += DeltaY;
    Context->Packet.Buttons = Buttons;
    Context->Packet.Pending = TRUE;

    RestoreFlags(&Flags);

    if (DeferredWorkTokenIsValid(Context->DeferredWorkToken) != FALSE) {
        DeferredWorkSignal(Context->DeferredWorkToken);
    }
}

/************************************************************************/

/**
 * @brief Get the latest X delta.
 * @param Context Mouse common context.
 * @return Unsigned X delta.
 */
U32 MouseCommonGetDeltaX(LPMOUSE_COMMON_CONTEXT Context) {
    if (Context == NULL) {
        return 0;
    }

    U32 Result = 0;
    LockMutex(&(Context->Mutex), INFINITY);
    Result = UNSIGNED(Context->DeltaX);
    UnlockMutex(&(Context->Mutex));
    return Result;
}

/************************************************************************/

/**
 * @brief Get the latest Y delta.
 * @param Context Mouse common context.
 * @return Unsigned Y delta.
 */
U32 MouseCommonGetDeltaY(LPMOUSE_COMMON_CONTEXT Context) {
    if (Context == NULL) {
        return 0;
    }

    U32 Result = 0;
    LockMutex(&(Context->Mutex), INFINITY);
    Result = UNSIGNED(Context->DeltaY);
    UnlockMutex(&(Context->Mutex));
    return Result;
}

/************************************************************************/

/**
 * @brief Get the current mouse button state.
 * @param Context Mouse common context.
 * @return Button bitmask.
 */
U32 MouseCommonGetButtons(LPMOUSE_COMMON_CONTEXT Context) {
    if (Context == NULL) {
        return 0;
    }

    U32 Result = 0;
    LockMutex(&(Context->Mutex), INFINITY);
    Result = UNSIGNED(Context->Buttons);
    UnlockMutex(&(Context->Mutex));
    return Result;
}

/************************************************************************/

/**
 * @brief Deferred work handler for mouse packet dispatch.
 * @param Context Unused.
 */
static void MouseCommonDeferredWork(LPVOID Context) {
    LPMOUSE_COMMON_CONTEXT MouseContext = (LPMOUSE_COMMON_CONTEXT)Context;
    if (MouseContext == NULL) {
        return;
    }

    I32 DeltaX = 0;
    I32 DeltaY = 0;
    U32 Buttons = 0;
    BOOL Pending = FALSE;

    UINT Flags;
    SaveFlags(&Flags);
    DisableInterrupts();

    if (MouseContext->Packet.Pending) {
        DeltaX = MouseContext->Packet.DeltaX;
        DeltaY = MouseContext->Packet.DeltaY;
        Buttons = MouseContext->Packet.Buttons;
        MouseContext->Packet.DeltaX = 0;
        MouseContext->Packet.DeltaY = 0;
        MouseContext->Packet.Pending = FALSE;
        Pending = TRUE;
    }

    RestoreFlags(&Flags);

    if (Pending == FALSE) {
        return;
    }

    LockMutex(&(MouseContext->Mutex), INFINITY);
    MouseContext->DeltaX = DeltaX;
    MouseContext->DeltaY = DeltaY;
    MouseContext->Buttons = Buttons;
    UnlockMutex(&(MouseContext->Mutex));

    MouseDispatcherOnInput(DeltaX, DeltaY, Buttons);
}

/************************************************************************/
