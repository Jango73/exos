
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


    Session Management

\************************************************************************/

#ifndef USER_SESSION_H_INCLUDED
#define USER_SESSION_H_INCLUDED

/************************************************************************/

#include "../Base.h"
#include "utils/List.h"
#include "Account.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define SESSION_TIMEOUT_MINUTES 30
#define SESSION_TIMEOUT_MS (SESSION_TIMEOUT_MINUTES * 60 * 1000)

/************************************************************************/

// Functions in Session.c
BOOL InitializeSessionSystem(void);
void ShutdownSessionSystem(void);
LPUSER_SESSION CreateUserSession(U64 UserID, HANDLE ShellTask);
BOOL ValidateUserSession(LPUSER_SESSION Session);
void DestroyUserSession(LPUSER_SESSION Session);
void TimeoutInactiveSessions(void);
LPUSER_SESSION FindSessionByTask(HANDLE Task);
LPUSER_SESSION GetCurrentSession(void);
void UpdateSessionActivity(LPUSER_SESSION Session);
BOOL IsUserSessionTimedOut(LPUSER_SESSION Session);
BOOL IsUserSessionLocked(LPUSER_SESSION Session);
BOOL LockUserSession(LPUSER_SESSION Session, U32 Reason);
BOOL UnlockUserSession(LPUSER_SESSION Session);
BOOL VerifySessionUnlockPassword(LPUSER_SESSION Session, LPCSTR Password);
BOOL SessionUserRequiresPassword(LPUSER_SESSION Session);

/************************************************************************/

#pragma pack(pop)

#endif  // USER_SESSION_H_INCLUDED
