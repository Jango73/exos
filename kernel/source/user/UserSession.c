
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


    Session Management

\************************************************************************/

#include "user/UserSession.h"

#include "core/Kernel.h"
#include "log/Log.h"
#include "memory/Heap.h"
#include "memory/Memory.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "sync/Deferred-Work.h"
#include "sync/Mutex.h"
#include "system/Clock.h"
#include "text/CoreString.h"
#include "user/Account.h"
#include "utils/Helpers.h"
#include "utils/List.h"

/************************************************************************/

#define SESSION_TIMEOUT_DISPATCH_PERIOD_MS 1000

static DEFERRED_WORK_TOKEN UserSessionDeferredToken = {
    .QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
static U32 UserSessionSchedulerTickHandle = SCHEDULER_TICK_INVALID_HANDLE;
static UINT UserSessionLastDispatchTime = 0;

/************************************************************************/

static void UserSessionDeferredTimeoutWork(LPVOID Context);
static void UserSessionSchedulerTick(LPVOID Context);

/************************************************************************/

/**
 * @brief Initialize transient unlock throttling state for one session.
 * @param Session Session storage.
 */
static void InitializeSessionUnlockPolicy(LPUSER_SESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        (void)AuthPolicyInit(&(Session->UnlockPolicy), AUTH_POLICY_FAILURE_DELAY_MS, AUTH_POLICY_LOCKOUT_THRESHOLD);
    }
}

/************************************************************************/

/**
 * @brief Deferred worker that applies session inactivity timeouts.
 * @param Context Unused.
 */
static void UserSessionDeferredTimeoutWork(LPVOID Context) {
    UNUSED(Context);
    TimeoutInactiveSessions();
}

/************************************************************************/

/**
 * @brief Lightweight scheduler tick hook for session timeout dispatch.
 * @param Context Unused.
 */
static void UserSessionSchedulerTick(LPVOID Context) {
    UINT CurrentTime;

    UNUSED(Context);

    if (DeferredWorkTokenIsValid(UserSessionDeferredToken) == FALSE) {
        return;
    }

    CurrentTime = GetSystemTime();
    if ((CurrentTime - UserSessionLastDispatchTime) < SESSION_TIMEOUT_DISPATCH_PERIOD_MS) {
        return;
    }

    UserSessionLastDispatchTime = CurrentTime;
    DeferredWorkSignal(UserSessionDeferredToken);
}

/************************************************************************/

/**
 * @brief Resolve configured inactivity timeout in milliseconds.
 * @return Timeout in milliseconds.
 */
static U32 GetSessionTimeoutMilliseconds(void) {
    LPCSTR TimeoutSecondsText = GetConfigurationValue(TEXT(CONFIG_SESSION_TIMEOUT_SECONDS));
    LPCSTR TimeoutMinutesText = GetConfigurationValue(TEXT(CONFIG_SESSION_TIMEOUT_MINUTES));
    U32 TimeoutMs = SESSION_TIMEOUT_MS;

    if (!STRING_EMPTY(TimeoutSecondsText)) {
        U32 ParsedSeconds = StringToU32(TimeoutSecondsText);
        if (ParsedSeconds > 0) {
            TimeoutMs = ParsedSeconds * 1000;
        }
    } else if (!STRING_EMPTY(TimeoutMinutesText)) {
        U32 ParsedMinutes = StringToU32(TimeoutMinutesText);
        if (ParsedMinutes > 0) {
            TimeoutMs = ParsedMinutes * 60 * 1000;
        }
    }

    return TimeoutMs;
}

/************************************************************************/

/**
 * @brief Test if a user account has a defined non-empty password.
 * @param Account User account to test.
 * @return TRUE when a non-empty password is configured.
 */
static BOOL AccountHasDefinedPassword(LPUSER_ACCOUNT Account) {
    SAFE_USE(Account) {
        if (VerifyPassword(TEXT(""), Account->PasswordHash)) {
            return FALSE;
        }
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize the session management system.
 * @return TRUE on success, FALSE on failure.
 */
BOOL InitializeSessionSystem(void) {
    DEFERRED_WORK_REGISTRATION Registration;
    LPLIST SessionList = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (SessionList == NULL) {
        ERROR(TEXT("Failed to create session list"));
        return FALSE;
    }

    SetUserSessionList(SessionList);
    UserSessionLastDispatchTime = GetSystemTime();

    MemorySet(&Registration, 0, sizeof(Registration));
    Registration.WorkCallback = UserSessionDeferredTimeoutWork;
    Registration.Context = NULL;
    Registration.Name = TEXT("UserSessionTimeout");

    UserSessionDeferredToken = DeferredWorkRegister(&Registration);
    if (DeferredWorkTokenIsValid(UserSessionDeferredToken) == FALSE) {
        ERROR(TEXT("Failed to register deferred session timeout work"));
        DeleteList(SessionList);
        SetUserSessionList(NULL);
        return FALSE;
    }

    UserSessionSchedulerTickHandle = SchedulerRegisterTickCallback(UserSessionSchedulerTick, NULL);
    if (UserSessionSchedulerTickHandle == SCHEDULER_TICK_INVALID_HANDLE) {
        ERROR(TEXT("Failed to register scheduler session timeout hook"));
        DeferredWorkUnregister(UserSessionDeferredToken);
        UserSessionDeferredToken =
            (DEFERRED_WORK_TOKEN){.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
        DeleteList(SessionList);
        SetUserSessionList(NULL);
        return FALSE;
    }

    DEBUG(TEXT("Session management system initialized"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shutdown the session management system.
 */
void ShutdownSessionSystem(void) {
    if (UserSessionSchedulerTickHandle != SCHEDULER_TICK_INVALID_HANDLE) {
        SchedulerUnregisterTickCallback(UserSessionSchedulerTickHandle);
        UserSessionSchedulerTickHandle = SCHEDULER_TICK_INVALID_HANDLE;
    }

    if (DeferredWorkTokenIsValid(UserSessionDeferredToken) != FALSE) {
        DeferredWorkUnregister(UserSessionDeferredToken);
        UserSessionDeferredToken =
            (DEFERRED_WORK_TOKEN){.QueueID = DEFERRED_WORK_QUEUE_INVALID, .SlotID = DEFERRED_WORK_INVALID_SLOT};
    }

    LPLIST SessionList = GetUserSessionList();
    SAFE_USE(SessionList) {
        // Clean up all active sessions
        LockMutex(MUTEX_SESSION, INFINITY);

        U32 Count = ListGetSize(SessionList);
        for (U32 i = 0; i < Count; i++) {
            LPUSER_SESSION Session = (LPUSER_SESSION)ListGetItem(SessionList, i);

            SAFE_USE(Session) {
                U32 UserIdHigh = U64_High32(Session->UserID);
                U32 UserIdLow = U64_Low32(Session->UserID);
                VERBOSE(TEXT("Cleaning up session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
                UNUSED(UserIdHigh);
                UNUSED(UserIdLow);
            }
        }

        DeleteList(SessionList);
        SetUserSessionList(NULL);
        UserSessionLastDispatchTime = 0;

        UnlockMutex(MUTEX_SESSION);
    }
}

/************************************************************************/

/**
 * @brief Create a new user session.
 * @param UserID User ID for the session.
 * @param ShellTask Associated shell task.
 * @return Pointer to created session or NULL on failure.
 */
LPUSER_SESSION CreateUserSession(U64 UserID, HANDLE ShellTask) {
    LockMutex(MUTEX_SESSION, INFINITY);
    LPLIST SessionList = GetUserSessionList();
    if (SessionList == NULL) {
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Allocate new session
    LPUSER_SESSION NewSession = (LPUSER_SESSION)KernelHeapAlloc(sizeof(USER_SESSION));
    if (NewSession == NULL) {
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Initialize session
    NewSession->TypeID = KOID_USER_SESSION;
    NewSession->References = 1;
    NewSession->Next = NULL;
    NewSession->Prev = NULL;

    NewSession->SessionID = GenerateSessionID();
    NewSession->UserID = UserID;
    NewSession->ShellTask = ShellTask;
    NewSession->IsLocked = FALSE;
    NewSession->LockReason = 0;
    NewSession->FailedUnlockCount = 0;

    GetLocalTime(&NewSession->LoginTime);
    NewSession->LastActivity = NewSession->LoginTime;
    NewSession->LastActivityMs = GetSystemTime();
    NewSession->LockTime = NewSession->LoginTime;
    InitializeSessionUnlockPolicy(NewSession);

    // Add to list
    if (ListAddTail(SessionList, NewSession) == 0) {
        KernelHeapFree(NewSession);
        UnlockMutex(MUTEX_SESSION);
        return NULL;
    }

    // Update user's last login time
    LPUSER_ACCOUNT User = FindAccountByID(UserID);

    SAFE_USE(User) { User->LastLoginTime = NewSession->LoginTime; }

    UnlockMutex(MUTEX_SESSION);

    return NewSession;
}

/************************************************************************/

/**
 * @brief Validate a user session.
 * @param Session Session to validate.
 * @return TRUE if session is valid, FALSE otherwise.
 */
BOOL ValidateUserSession(LPUSER_SESSION Session) {
    if (Session == NULL || Session->TypeID != KOID_USER_SESSION || Session->IsLocked) {
        return FALSE;
    }

    if (IsUserSessionTimedOut(Session)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Destroy a user session.
 * @param Session Session to destroy.
 */
void DestroyUserSession(LPUSER_SESSION Session) {
    if (Session == NULL) {
        return;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    // Remove from list
    LPLIST SessionList = GetUserSessionList();
    ListErase(SessionList, Session);

    UnlockMutex(MUTEX_SESSION);

    U32 UserIdHigh = U64_High32(Session->UserID);
    U32 UserIdLow = U64_Low32(Session->UserID);
    DEBUG(TEXT("Destroyed session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
    UNUSED(UserIdHigh);
    UNUSED(UserIdLow);
}

/************************************************************************/

/**
 * @brief Check whether a session inactivity timeout is reached.
 * @param Session Session to test.
 * @return TRUE when inactivity timeout is reached.
 */
BOOL IsUserSessionTimedOut(LPUSER_SESSION Session) {
    U32 TimeoutMs;
    U32 CurrentMs;
    U32 ElapsedMs;

    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        TimeoutMs = GetSessionTimeoutMilliseconds();
        CurrentMs = GetSystemTime();
        ElapsedMs = (U32)(CurrentMs - Session->LastActivityMs);
        return (ElapsedMs >= TimeoutMs);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Query session lock state.
 * @param Session Session to inspect.
 * @return TRUE when locked.
 */
BOOL IsUserSessionLocked(LPUSER_SESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) { return Session->IsLocked ? TRUE : FALSE; }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Lock one user session.
 * @param Session Session to lock.
 * @param Reason Lock reason code.
 * @return TRUE on success, FALSE on failure.
 */
BOOL LockUserSession(LPUSER_SESSION Session, U32 Reason) {
    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        if (Session->IsLocked) {
            return TRUE;
        }

        Session->IsLocked = TRUE;
        Session->LockReason = Reason;
        Session->FailedUnlockCount = 0;
        GetLocalTime(&Session->LockTime);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Unlock one user session.
 * @param Session Session to unlock.
 * @return TRUE on success, FALSE on failure.
 */
BOOL UnlockUserSession(LPUSER_SESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        Session->IsLocked = FALSE;
        Session->LockReason = 0;
        Session->FailedUnlockCount = 0;
        AuthPolicyRecordSuccess(&(Session->UnlockPolicy));
        UpdateSessionActivity(Session);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Verify one password attempt for unlocking a session.
 * @param Session Locked session.
 * @param Password Password text.
 * @return TRUE when password is valid.
 */
BOOL VerifySessionUnlockPassword(LPUSER_SESSION Session, LPCSTR Password) {
    LPUSER_ACCOUNT Account;
    UINT WaitRemaining;

    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        if (Password == NULL) {
            return FALSE;
        }

        WaitRemaining = 0;
        if (!AuthPolicyCanAttempt(&(Session->UnlockPolicy), GetSystemTime(), &WaitRemaining)) {
            return FALSE;
        }

        Account = FindAccountByID(Session->UserID);
        SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
            if (VerifyPassword(Password, Account->PasswordHash)) {
                AuthPolicyRecordSuccess(&(Session->UnlockPolicy));
                Session->FailedUnlockCount = 0;
                return TRUE;
            }
        }

        Session->FailedUnlockCount++;
        (void)AuthPolicyRecordFailure(&(Session->UnlockPolicy), GetSystemTime(), NULL);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Query whether one locked session may accept another unlock attempt.
 * @param Session Session to inspect.
 * @param WaitRemainingOut Receives remaining wait time when blocked.
 * @return TRUE when another attempt may proceed.
 */
BOOL CanAttemptSessionUnlock(LPUSER_SESSION Session, UINT* WaitRemainingOut) {
    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = 0;
    }

    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        return AuthPolicyCanAttempt(&(Session->UnlockPolicy), GetSystemTime(), WaitRemainingOut);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether the session owner has a defined password.
 * @param Session Session to inspect.
 * @return TRUE when lock should require a password.
 */
BOOL SessionUserRequiresPassword(LPUSER_SESSION Session) {
    LPUSER_ACCOUNT Account;

    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        Account = FindAccountByID(Session->UserID);
        return AccountHasDefinedPassword(Account);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Lock inactive sessions instead of deleting them.
 */
void TimeoutInactiveSessions(void) {
    LPLIST SessionList = GetUserSessionList();
    if (SessionList == NULL) {
        return;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(SessionList);
    for (U32 i = 0; i < Count; i++) {
        LPUSER_SESSION Session = (LPUSER_SESSION)ListGetItem(SessionList, i);
        if (Session != NULL && Session->IsLocked == FALSE && IsUserSessionTimedOut(Session)) {
            if (!SessionUserRequiresPassword(Session)) {
                UpdateSessionActivity(Session);
                continue;
            }

            U32 UserIdHigh = U64_High32(Session->UserID);
            U32 UserIdLow = U64_Low32(Session->UserID);
            DEBUG(TEXT("Locking session for user ID: %08X%08X"), UserIdHigh, UserIdLow);
            UNUSED(UserIdHigh);
            UNUSED(UserIdLow);

            LockUserSession(Session, USER_SESSION_LOCK_REASON_TIMEOUT);
        }
    }

    UnlockMutex(MUTEX_SESSION);
}

/************************************************************************/

/**
 * @brief Find session by associated task.
 * @param Task Task to search for.
 * @return Pointer to session or NULL if not found.
 */
LPUSER_SESSION FindSessionByTask(HANDLE Task) {
    LPLIST SessionList = GetUserSessionList();
    if (Task == NULL || SessionList == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_SESSION, INFINITY);

    U32 Count = ListGetSize(SessionList);
    for (U32 i = 0; i < Count; i++) {
        LPUSER_SESSION Session = (LPUSER_SESSION)ListGetItem(SessionList, i);
        if (Session != NULL && Session->ShellTask == Task) {
            UnlockMutex(MUTEX_SESSION);
            return Session;
        }
    }

    UnlockMutex(MUTEX_SESSION);
    return NULL;
}

/************************************************************************/

/**
 * @brief Get the current active session.
 * @return Pointer to current session or NULL if none.
 */
LPUSER_SESSION GetCurrentSession(void) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    if (CurrentProcess == NULL) {
        return NULL;
    }

    SAFE_USE_VALID_ID(CurrentProcess->Session, KOID_USER_SESSION) { return CurrentProcess->Session; }

    return NULL;
}

/************************************************************************/

/**
 * @brief Update session activity timestamp.
 * @param Session Session to update.
 */
void UpdateSessionActivity(LPUSER_SESSION Session) {
    SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
        if (Session->IsLocked) {
            return;
        }

        GetLocalTime(&Session->LastActivity);
        Session->LastActivityMs = GetSystemTime();
    }
}

/************************************************************************/

/**
 * @brief Set the current user session.
 * @param Session The session to set as current.
 * @return TRUE on success, FALSE on failure.
 */
BOOL SetCurrentSession(LPUSER_SESSION Session) {
    LPPROCESS CurrentProcess = GetCurrentProcess();
    if (CurrentProcess == NULL) {
        return FALSE;
    }

    // Find the session in the session list first
    SAFE_USE(Session) {
        LPLIST SessionList = GetUserSessionList();
        LPUSER_SESSION Found = (LPUSER_SESSION)(SessionList != NULL ? SessionList->First : NULL);
        BOOL SessionExists = FALSE;

        while (Found != NULL) {
            if (Found == Session) {
                SessionExists = TRUE;
                break;
            }
            Found = (LPUSER_SESSION)Found->Next;
        }

        if (!SessionExists) {
            return FALSE;
        }
    }

    // Associate the session with the current process
    CurrentProcess->Session = Session;
    if (Session != NULL) {
        CurrentProcess->UserID = Session->UserID;
    }

    return TRUE;
}
