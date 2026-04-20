
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

#include "user/Account.h"

#include "system/Clock.h"
#include "core/Driver.h"
#include "utils/Crypt.h"
#include "utils/Database.h"
#include "utils/KernelPath.h"
#include "memory/Heap.h"
#include "utils/Helpers.h"
#include "core/Kernel.h"
#include "utils/List.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "sync/Mutex.h"
#include "text/CoreString.h"
#include "system/System.h"
#include "User.h"

/************************************************************************/

static U32 NextSessionID = 1;
static const U32 USER_DATABASE_CAPACITY = 1000;

static UINT UserAccountDriverCommands(UINT Function, UINT Parameter);

/************************************************************************/

typedef struct tag_SESSION_ID_ENTROPY {
    U32 Sequence;
    U32 UpTimeMilliseconds;
    UINT CurrentTask;
    UINT CurrentProcess;
    UINT StackMarker;
    DATETIME LocalTime;
    U64 PreviousState;
} SESSION_ID_ENTROPY, *LPSESSION_ID_ENTROPY;

/************************************************************************/

/**
 * @brief Initialize transient login throttling state for one user account.
 * @param Account User account storage.
 */
static void InitializeUserAuthenticationPolicy(LPUSER_ACCOUNT Account) {
    SAFE_USE(Account) {
        (void)AuthPolicyInit(
            &(Account->AuthenticationPolicy),
            AUTH_POLICY_FAILURE_DELAY_MS,
            AUTH_POLICY_LOCKOUT_THRESHOLD);
    }
}

/************************************************************************/

DRIVER DATA_SECTION UserAccountDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = USER_SYSTEM_VER_MAJOR,
    .VersionMinor = USER_SYSTEM_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "UserSystem",
    .Alias = "account",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = UserAccountDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the user account driver descriptor.
 * @return Pointer to the user account driver.
 */
LPDRIVER UserAccountGetDriver(void) {
    return &UserAccountDriver;
}

/************************************************************************/

/**
 * @brief Initialize the user account system.
 * @return TRUE on success, FALSE on failure.
 */
BOOL InitializeUserSystem(void) {
    LPLIST AccountList = GetAccountList();
    if (AccountList == NULL) {
        ERROR(TEXT("User account list not initialized in kernel"));
        return FALSE;
    }

    // Try to load existing user database
    if (!LoadUserDatabase()) {
        DEBUG(TEXT("No existing user database found - will let shell handle user creation"));
    }

    if (!InitializeSessionSystem()) {
        ERROR(TEXT("Failed to initialize session management system"));
        return FALSE;
    }

    DEBUG(TEXT("User account system initialized"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shutdown the user account system.
 */
void ShutdownUserSystem(void) {
    LPLIST AccountList = GetAccountList();
    SAFE_USE(AccountList) {
        SaveUserDatabase();
        ListReset(AccountList);
    }
}

/************************************************************************/

/**
 * @brief Count registered user accounts through the owner-side account API.
 * @return Number of user accounts.
 */
UINT GetAccountCount(void) {
    UINT Count = 0;
    LPLIST AccountList = GetAccountList();

    if (AccountList == NULL) {
        return 0;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);
    Count = ListGetSize(AccountList);
    UnlockMutex(MUTEX_ACCOUNTS);

    return Count;
}

/************************************************************************/

/**
 * @brief Retrieve one user account by zero-based index.
 * @param Index Zero-based account index.
 * @return User account pointer or NULL when out of range.
 */
LPUSER_ACCOUNT GetAccountByIndex(UINT Index) {
    LPUSER_ACCOUNT Account = NULL;
    LPLIST AccountList = GetAccountList();

    if (AccountList == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);
    if (Index < ListGetSize(AccountList)) {
        Account = (LPUSER_ACCOUNT)ListGetItem(AccountList, Index);
    }
    UnlockMutex(MUTEX_ACCOUNTS);

    return Account;
}

/************************************************************************/

/**
 * @brief Driver command handler for the user system.
 */
static UINT UserAccountDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((UserAccountDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeUserSystem()) {
                UserAccountDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;

        case DF_UNLOAD:
            if ((UserAccountDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ShutdownUserSystem();
            UserAccountDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(USER_SYSTEM_VER_MAJOR, USER_SYSTEM_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Create a new user account.
 * @param UserName Username for the account.
 * @param Password Plain text password.
 * @param Privilege Privilege level (EXOS_PRIVILEGE_USER or EXOS_PRIVILEGE_ADMIN).
 * @return Pointer to created user account or NULL on failure.
 */
LPUSER_ACCOUNT CreateAccount(LPCSTR UserName, LPCSTR Password, U32 Privilege) {
    DEBUG(TEXT("[CreateAccount] Enter - UserName=%s"), UserName ? UserName : TEXT("NULL"));

    if (UserName == NULL || Password == NULL) {
        DEBUG(TEXT("[CreateAccount] NULL parameters - UserName=%p, Password=%p"), UserName, Password);
        return NULL;
    }

    U32 UserNameLen = StringLength(UserName);
    if (UserNameLen == 0 || UserNameLen >= 32) {
        DEBUG(TEXT("[CreateAccount] Invalid username length: %d"), UserNameLen);
        return NULL;
    }

    DEBUG(TEXT("[CreateAccount] Attempting to lock mutex"));
    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    // Check if user already exists
    DEBUG(TEXT("[CreateAccount] Checking if user exists"));

    if (FindAccount(UserName) != NULL) {
        DEBUG(TEXT("[CreateAccount] User already exists"));
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    // Allocate new user account
    DEBUG(TEXT("[CreateAccount] Allocating memory for new user"));
    LPUSER_ACCOUNT NewUser = (LPUSER_ACCOUNT)KernelHeapAlloc(sizeof(USER_ACCOUNT));
    if (NewUser == NULL) {
        DEBUG(TEXT("[CreateAccount] Memory allocation failed"));
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    // Initialize user account
    MemorySet(NewUser, 0, sizeof(USER_ACCOUNT));
    NewUser->TypeID = KOID_USER_ACCOUNT;
    NewUser->References = 1;

    StringCopy(NewUser->UserName, UserName);
    NewUser->UserID = HashString(UserName);
    NewUser->PasswordHash = HashPassword(Password);
    NewUser->Privilege = Privilege;
    NewUser->Status = USER_STATUS_ACTIVE;

    GetLocalTime(&NewUser->CreationTime);
    NewUser->LastLoginTime = NewUser->CreationTime;
    InitializeUserAuthenticationPolicy(NewUser);

    // Add to list and database
    DEBUG(TEXT("[CreateAccount] Adding to user list"));
    LPLIST AccountList = GetAccountList();
    if (AccountList == NULL || ListAddTail(AccountList, NewUser) == 0) {
        DEBUG(TEXT("[CreateAccount] Failed to add to user list"));
        KernelHeapFree(NewUser);
        UnlockMutex(MUTEX_ACCOUNTS);
        return NULL;
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    if (!SaveUserDatabase()) {
        ERROR(TEXT("[CreateAccount] Failed to save user database after creating user %s"), UserName);
    }

    DEBUG(TEXT("[CreateAccount] User created successfully"));
    VERBOSE(TEXT("Created user account: %s"), UserName);
    return NewUser;
}

/************************************************************************/

/**
 * @brief Delete a user account.
 * @param UserName Username to delete.
 * @return TRUE on success, FALSE on failure.
 */
BOOL DeleteAccount(LPCSTR UserName) {
    if (UserName == NULL) {
        return FALSE;
    }

    // Don't allow deleting root user
    if (StringCompare(UserName, TEXT("root")) == 0) {
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    LPUSER_ACCOUNT User = FindAccount(UserName);
    if (User == NULL) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    LPLIST AccountList = GetAccountList();
    ListErase(AccountList, User);

    UnlockMutex(MUTEX_ACCOUNTS);

    if (!SaveUserDatabase()) {
        ERROR(TEXT("[DeleteAccount] Failed to save user database after deleting user %s"), UserName);
    }

    VERBOSE(TEXT("Deleted user account: %s"), UserName);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Find a user account by username.
 * @param UserName Username to search for.
 * @return Pointer to user account or NULL if not found.
 */
LPUSER_ACCOUNT FindAccount(LPCSTR UserName) {
    LPLIST AccountList = GetAccountList();
    if (UserName == NULL || AccountList == NULL) {
        return NULL;
    }

    U32 Count = ListGetSize(AccountList);
    for (U32 i = 0; i < Count; i++) {
        LPUSER_ACCOUNT User = (LPUSER_ACCOUNT)ListGetItem(AccountList, i);
        if (User != NULL && STRINGS_EQUAL(User->UserName, UserName)) {
            return User;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Find a user account by user ID.
 * @param UserID User ID hash to search for.
 * @return Pointer to user account or NULL if not found.
 */
LPUSER_ACCOUNT FindAccountByID(U64 UserID) {
    LPLIST AccountList = GetAccountList();
    if (AccountList == NULL) {
        return NULL;
    }

    U32 Count = ListGetSize(AccountList);
    for (U32 i = 0; i < Count; i++) {
        LPUSER_ACCOUNT User = (LPUSER_ACCOUNT)ListGetItem(AccountList, i);
        if (User != NULL && U64_Cmp(User->UserID, UserID) == 0) {
            return User;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Change a user's password.
 * @param UserName Username.
 * @param OldPassword Current password.
 * @param NewPassword New password.
 * @return TRUE on success, FALSE on failure.
 */
BOOL ChangeUserPassword(LPCSTR UserName, LPCSTR OldPassword, LPCSTR NewPassword) {
    if (UserName == NULL || OldPassword == NULL || NewPassword == NULL) {
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    LPUSER_ACCOUNT User = FindAccount(UserName);
    if (User == NULL) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    // Verify old password
    if (!VerifyPassword(OldPassword, User->PasswordHash)) {
        UnlockMutex(MUTEX_ACCOUNTS);
        return FALSE;
    }

    // Set new password
    User->PasswordHash = HashPassword(NewPassword);

    UnlockMutex(MUTEX_ACCOUNTS);

    VERBOSE(TEXT("Password changed for user: %s"), UserName);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Load user database from persistent storage.
 * @return TRUE on success, FALSE on failure.
 */
BOOL LoadUserDatabase(void) {
    STR DatabasePath[MAX_PATH_NAME];
    DATABASE* Database = DatabaseCreate(sizeof(USER_ACCOUNT), (U32)((U8*)&((USER_ACCOUNT*)0)->UserID - (U8*)0), USER_DATABASE_CAPACITY);
    if (Database == NULL) {
        ERROR(TEXT("Failed to allocate temporary user database"));
        return FALSE;
    }

    if (KernelPathResolve(
            KERNEL_PATH_KEY_USERS_DATABASE,
            KERNEL_PATH_DEFAULT_USERS_DATABASE,
            DatabasePath,
            MAX_PATH_NAME) == FALSE) {
        DatabaseFree(Database);
        return FALSE;
    }

    I32 Result = DatabaseLoad(Database, DatabasePath);
    if (Result != 0) {
        DatabaseFree(Database);
        return FALSE;
    }

    LPLIST AccountList = GetAccountList();
    if (AccountList == NULL) {
        DatabaseFree(Database);
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    ListReset(AccountList);

    for (U32 i = 0; i < Database->Count; i++) {
        LPUSER_ACCOUNT User = (LPUSER_ACCOUNT)((U8*)Database->Records + i * Database->RecordSize);
        LPUSER_ACCOUNT NewUser = (LPUSER_ACCOUNT)KernelHeapAlloc(sizeof(USER_ACCOUNT));

        SAFE_USE(NewUser) {
            MemoryCopy(NewUser, User, sizeof(USER_ACCOUNT));
            NewUser->Next = NULL;
            NewUser->Prev = NULL;
            NewUser->References = 1;
            NewUser->TypeID = KOID_USER_ACCOUNT;
            InitializeUserAuthenticationPolicy(NewUser);

            if (ListAddTail(AccountList, NewUser) == 0) {
                KernelHeapFree(NewUser);
            }
        }
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    DEBUG(TEXT("Loaded %u user accounts from database"), Database->Count);

    DatabaseFree(Database);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Save user database to persistent storage.
 * @return TRUE on success, FALSE on failure.
 */
BOOL SaveUserDatabase(void) {
    STR DatabasePath[MAX_PATH_NAME];
    DATABASE* Database = DatabaseCreate(sizeof(USER_ACCOUNT), (U32)((U8*)&((USER_ACCOUNT*)0)->UserID - (U8*)0), USER_DATABASE_CAPACITY);
    if (Database == NULL) {
        ERROR(TEXT("Failed to allocate temporary user database"));
        return FALSE;
    }

    LPLIST AccountList = GetAccountList();
    if (AccountList == NULL) {
        DatabaseFree(Database);
        return FALSE;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    SAFE_USE(AccountList) {
        U32 Count = ListGetSize(AccountList);
        for (U32 i = 0; i < Count && Database->Count < Database->Capacity; i++) {
            LPUSER_ACCOUNT User = (LPUSER_ACCOUNT)ListGetItem(AccountList, i);

            SAFE_USE(User) {
                USER_ACCOUNT PersistentUser = *User;
                MemorySet(&(PersistentUser.AuthenticationPolicy), 0, sizeof(PersistentUser.AuthenticationPolicy));
                DatabaseAdd(Database, &PersistentUser);
            }
        }
    }

    UnlockMutex(MUTEX_ACCOUNTS);

    if (KernelPathResolve(
            KERNEL_PATH_KEY_USERS_DATABASE,
            KERNEL_PATH_DEFAULT_USERS_DATABASE,
            DatabasePath,
            MAX_PATH_NAME) == FALSE) {
        DatabaseFree(Database);
        return FALSE;
    }

    I32 Result = DatabaseSave(Database, DatabasePath);

    DatabaseFree(Database);

    if (Result != 0) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Hash a password using CRC64.
 * @param Password Plain text password.
 * @return 64-bit hash of the password.
 */
U64 HashPassword(LPCSTR Password) {
    if (Password == NULL) {
        return U64_FromU32(0);
    }

    // Add salt to password
    STR SaltedPassword[128];
    StringCopy(SaltedPassword, TEXT("EXOS_SALT_"));
    StringConcat(SaltedPassword, Password);
    StringConcat(SaltedPassword, TEXT("_TLAS_SOXE"));

    return CRC64_Hash(SaltedPassword, StringLength(SaltedPassword));
}

/************************************************************************/

/**
 * @brief Verify a password against a stored hash.
 * @param Password Plain text password to verify.
 * @param StoredHash Stored password hash.
 * @return TRUE if password matches, FALSE otherwise.
 */
BOOL VerifyPassword(LPCSTR Password, U64 StoredHash) {
    if (Password == NULL) {
        return FALSE;
    }

    U64 PasswordHash = HashPassword(Password);
    return U64_Cmp(PasswordHash, StoredHash) == 0;
}

/************************************************************************/

/**
 * @brief Generate a unique session ID.
 * @return New session ID.
 */
U64 GenerateSessionID(void) {
    static U64 SessionIdState = U64_0;
    SESSION_ID_ENTROPY Entropy;

    MemorySet(&Entropy, 0, sizeof(Entropy));

    Entropy.Sequence = NextSessionID++;
    Entropy.UpTimeMilliseconds = (U32)GetSystemTime();
    Entropy.CurrentTask = (UINT)GetCurrentTask();
    Entropy.CurrentProcess = (UINT)GetCurrentProcess();
    Entropy.StackMarker = (UINT)&Entropy;
    Entropy.PreviousState = SessionIdState;
    GetLocalTime(&Entropy.LocalTime);

    SessionIdState = CRC64_Hash(&Entropy, sizeof(Entropy));

    if (U64_Cmp(SessionIdState, U64_FromU32(0)) == 0) {
        SessionIdState = CRC64_Hash(&NextSessionID, sizeof(NextSessionID));
        if (U64_Cmp(SessionIdState, U64_FromU32(0)) == 0) {
            SessionIdState = U64_FromU32(1);
        }
    }

    return SessionIdState;
}

/************************************************************************/

/**
 * @brief Query whether one account may attempt authentication.
 * @param Account Account to inspect.
 * @param WaitRemainingOut Receives remaining wait time when blocked.
 * @return TRUE when the attempt may proceed.
 */
BOOL CanAttemptUserAuthentication(LPUSER_ACCOUNT Account, UINT* WaitRemainingOut) {
    BOOL Result = FALSE;

    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = 0;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        Result = AuthPolicyCanAttempt(&(Account->AuthenticationPolicy), GetSystemTime(), WaitRemainingOut);
    }

    UnlockMutex(MUTEX_ACCOUNTS);
    return Result;
}

/************************************************************************/

/**
 * @brief Record one failed authentication attempt for one account.
 * @param Account Account to update.
 * @param WaitRemainingOut Receives remaining delay or lockout time.
 * @return TRUE when the account entered temporary lockout.
 */
BOOL RecordUserAuthenticationFailure(LPUSER_ACCOUNT Account, UINT* WaitRemainingOut) {
    BOOL IsLocked = FALSE;

    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = 0;
    }

    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        IsLocked = AuthPolicyRecordFailure(&(Account->AuthenticationPolicy), GetSystemTime(), WaitRemainingOut);
    }

    UnlockMutex(MUTEX_ACCOUNTS);
    return IsLocked;
}

/************************************************************************/

/**
 * @brief Reset failed authentication state after one successful login.
 * @param Account Account to update.
 */
void RecordUserAuthenticationSuccess(LPUSER_ACCOUNT Account) {
    LockMutex(MUTEX_ACCOUNTS, INFINITY);

    SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
        AuthPolicyRecordSuccess(&(Account->AuthenticationPolicy));
    }

    UnlockMutex(MUTEX_ACCOUNTS);
}
