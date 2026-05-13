
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


    Task messaging

\************************************************************************/

#ifndef TASK_MESSAGING_H_INCLUDED
#define TASK_MESSAGING_H_INCLUDED

/************************************************************************/

#include "process/Task.h"

/************************************************************************/
// Messaging lock contract with desktop/windowing:
//
// - Message queue operations are allowed under TaskMessageMutex.
// - Window tree/state lookups must use minimal desktop lock scope.
// - PostMessage and DispatchMessage must not execute window callbacks while
//   holding TaskMessageMutex.

BOOL InitMessageQueue(LPMESSAGEQUEUE Queue);
void DeleteMessageQueue(LPMESSAGEQUEUE Queue);
BOOL EnsureTaskMessageQueue(LPTASK Task, BOOL CreateIfMissing);
BOOL EnsureProcessMessageQueue(LPPROCESS Process, BOOL CreateIfMissing);
BOOL EnsureAllMessageQueues(LPTASK Task, BOOL CreateIfMissing);
BOOL EnqueueInputMessage(U32 Msg, U32 Param1, U32 Param2);
BOOL PostProcessMessage(LPPROCESS Process, U32 Msg, U32 Param1, U32 Param2);
BOOL BroadcastProcessMessage(U32 Msg, U32 Param1, U32 Param2);
BOOL PostMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2);
U32 SendMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2);
BOOL PeekMessage(LPMESSAGE_INFO Message);
BOOL GetMessage(LPMESSAGE_INFO Message);
BOOL DispatchMessage(LPMESSAGE_INFO Message);

/************************************************************************/

#endif  // TASK_MESSAGING_H_INCLUDED
