
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


    User Account Management

\************************************************************************/

#ifndef USER_ACCOUNT_H_INCLUDED
#define USER_ACCOUNT_H_INCLUDED

/************************************************************************/

#include "../Base.h"
#include "core/Driver.h"
#include "core/ID.h"
#include "utils/List.h"
#include "core/Security.h"
#include "../utils/AuthPolicy.h"

// Forward declarations
typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define USER_STATUS_ACTIVE 0x00000001
#define USER_STATUS_SUSPENDED 0x00000002
#define USER_STATUS_LOCKED 0x00000004
#define USER_SESSION_LOCK_REASON_TIMEOUT 1
#define USER_SESSION_LOCK_REASON_MANUAL 2

/************************************************************************/

typedef struct tag_USER_ACCOUNT {
    LISTNODE_FIELDS
    U64 UserID;              // Unique user hash
    STR UserName[32];        // Username
    U64 PasswordHash;        // Password hash
    U32 Privilege;           // Privilege level (0=user, 1=admin)
    DATETIME CreationTime;   // Creation date
    DATETIME LastLoginTime;  // Last login
    U32 Status;              // Account status (active/suspended)
    AUTH_POLICY AuthenticationPolicy; // In-memory login throttling policy
} USER_ACCOUNT, *LPUSER_ACCOUNT;

/************************************************************************/

typedef struct tag_USER_SESSION {
    LISTNODE_FIELDS
    U64 SessionID;          // Unique session ID
    U64 UserID;             // Logged in user
    DATETIME LoginTime;     // Login time
    DATETIME LastActivity;  // Last activity
    U32 LastActivityMs;     // Last activity uptime in milliseconds
    BOOL IsLocked;          // Session lock state
    U32 LockReason;         // USER_SESSION_LOCK_REASON_*
    DATETIME LockTime;      // Lock time
    U32 FailedUnlockCount;  // Failed unlock attempts
    HANDLE ShellTask;       // Associated shell task (HANDLE to TASK)
    AUTH_POLICY UnlockPolicy; // In-memory unlock throttling policy
} USER_SESSION, *LPUSER_SESSION;

/************************************************************************/

// Functions in Account.c
BOOL InitializeUserSystem(void);
void ShutdownUserSystem(void);
UINT GetAccountCount(void);
LPUSER_ACCOUNT GetAccountByIndex(UINT Index);
LPUSER_ACCOUNT CreateAccount(LPCSTR UserName, LPCSTR Password, U32 Privilege);
BOOL DeleteAccount(LPCSTR UserName);
LPUSER_ACCOUNT FindAccount(LPCSTR UserName);
LPUSER_ACCOUNT FindAccountByID(U64 UserID);
BOOL ChangeUserPassword(LPCSTR UserName, LPCSTR OldPassword, LPCSTR NewPassword);
BOOL LoadUserDatabase(void);
BOOL SaveUserDatabase(void);

// Hash and authentication functions
U64 HashPassword(LPCSTR Password);
BOOL VerifyPassword(LPCSTR Password, U64 StoredHash);
U64 GenerateSessionID(void);
BOOL CanAttemptUserAuthentication(LPUSER_ACCOUNT Account, UINT* WaitRemainingOut);
BOOL RecordUserAuthenticationFailure(LPUSER_ACCOUNT Account, UINT* WaitRemainingOut);
void RecordUserAuthenticationSuccess(LPUSER_ACCOUNT Account);
BOOL CanAttemptSessionUnlock(LPUSER_SESSION Session, UINT* WaitRemainingOut);

// Session management functions
LPUSER_SESSION CreateUserSession(U64 UserID, HANDLE ShellTask);
BOOL ValidateUserSession(LPUSER_SESSION Session);
void DestroyUserSession(LPUSER_SESSION Session);
void TimeoutInactiveSessions(void);
LPUSER_SESSION GetCurrentSession(void);
BOOL SetCurrentSession(LPUSER_SESSION Session);
BOOL IsUserSessionTimedOut(LPUSER_SESSION Session);
BOOL IsUserSessionLocked(LPUSER_SESSION Session);
BOOL LockUserSession(LPUSER_SESSION Session, U32 Reason);
BOOL UnlockUserSession(LPUSER_SESSION Session);
BOOL VerifySessionUnlockPassword(LPUSER_SESSION Session, LPCSTR Password);
BOOL SessionUserRequiresPassword(LPUSER_SESSION Session);

/************************************************************************/

#define USER_SYSTEM_VER_MAJOR 1
#define USER_SYSTEM_VER_MINOR 0

/************************************************************************/

#pragma pack(pop)

#endif  // USER_ACCOUNT_H_INCLUDED
