
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


    Deferred work dispatcher infrastructure

\************************************************************************/

#ifndef DEFERREDWORK_H_INCLUDED
#define DEFERREDWORK_H_INCLUDED

/***************************************************************************/

#include "core/Driver.h"
#include "sync/Deferred-Work-Queue.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#define DEFERRED_WORK_QUEUE_STANDARD 0
#define DEFERRED_WORK_QUEUE_FAST 1
#define DEFERRED_WORK_QUEUE_COUNT 2
#define DEFERRED_WORK_QUEUE_INVALID 0xFFFFFFFF
#define DEFERRED_WORK_FAST_DELAY_MS 5

/***************************************************************************/

typedef struct tag_DEFERRED_WORK_TOKEN {
    U32 QueueID;
    U32 SlotID;
} DEFERRED_WORK_TOKEN, *LPDEFERRED_WORK_TOKEN;

/***************************************************************************/

BOOL InitializeDeferredWork(void);
void ShutdownDeferredWork(void);
DEFERRED_WORK_TOKEN DeferredWorkRegister(const DEFERRED_WORK_REGISTRATION *Registration);
DEFERRED_WORK_TOKEN DeferredWorkRegisterForQueue(U32 QueueID, const DEFERRED_WORK_REGISTRATION *Registration);
DEFERRED_WORK_TOKEN DeferredWorkRegisterPollOnly(DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name);
DEFERRED_WORK_TOKEN DeferredWorkRegisterPollOnlyForQueue(
    U32 QueueID, DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name);
void DeferredWorkUnregister(DEFERRED_WORK_TOKEN Token);
void DeferredWorkSignal(DEFERRED_WORK_TOKEN Token);
BOOL DeferredWorkIsPollingMode(void);
BOOL DeferredWorkIsPollingModeForQueue(U32 QueueID);
BOOL DeferredWorkTokenIsValid(DEFERRED_WORK_TOKEN Token);
void DeferredWorkUpdateMode(void);

/***************************************************************************/

#pragma pack(pop)

#endif  // DEFERREDWORK_H_INCLUDED
