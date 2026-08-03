
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


    Shell main

\************************************************************************/

#include "shell/Shell-Shared.h"

/************************************************************************/

typedef struct tag_SESSION_LOCK_BACKEND_INTERFACE {
    BOOL (*CaptureState)(LPVOID BackendContext, LPVOID* OutState);
    BOOL (*ShowAndUnlock)(LPVOID BackendContext, LPUSER_SESSION Session);
    BOOL (*RestoreState)(LPVOID BackendContext, LPVOID State);
    void (*ReleaseState)(LPVOID BackendContext, LPVOID State);
} SESSION_LOCK_BACKEND_INTERFACE, *LPSESSION_LOCK_BACKEND_INTERFACE;

/************************************************************************/

/**
 * @brief Print one centered line in the console.
 * @param Text Text line to center.
 */
static void ConsolePrintCenteredLine(LPCSTR Text) {
    U32 Width = Console.Width;
    U32 Length = 0;
    U32 Padding = 0;

    if (Text == NULL) {
        ConsolePrint(TEXT("\n"));
        return;
    }

    Length = StringLength(Text);
    if (Width > Length) {
        Padding = (Width - Length) / 2;
    }

    for (U32 Index = 0; Index < Padding; Index++) {
        ConsolePrintChar(STR_SPACE);
    }

    ConsolePrint(TEXT("%s\n"), Text);
}

/************************************************************************/

/**
 * @brief Read one line while temporarily disabling editor idle callback.
 * @param Context Shell context.
 * @param Buffer Destination buffer.
 * @param BufferSize Destination size.
 * @param MaskCharacters TRUE to mask typed characters.
 * @return TRUE on success.
 */
static BOOL ReadLineWithIdleDisabled(
    LPSHELLCONTEXT Context,
    LPSTR Buffer,
    U32 BufferSize,
    BOOL MaskCharacters) {
    COMMANDLINEEDITOR_IDLE_CALLBACK PreviousCallback;
    LPVOID PreviousUserData;
    BOOL Result;

    if (Context == NULL || Buffer == NULL || BufferSize == 0) {
        return FALSE;
    }

    PreviousCallback = Context->Input.Editor.IdleCallback;
    PreviousUserData = Context->Input.Editor.IdleUserData;

    CommandLineEditorSetIdleCallback(&Context->Input.Editor, NULL, NULL);
    Result = CommandLineEditorReadLine(
        &Context->Input.Editor,
        Buffer,
        BufferSize,
        MaskCharacters);
    CommandLineEditorSetIdleCallback(
        &Context->Input.Editor,
        PreviousCallback,
        PreviousUserData);

    return Result;
}

/************************************************************************/

/**
 * @brief Capture current console state before lock screen display.
 * @param BackendContext Unused.
 * @param OutState Captured state pointer.
 * @return TRUE on success.
 */
static BOOL CaptureConsoleLockState(LPVOID BackendContext, LPVOID* OutState) {
    UNUSED(BackendContext);
    return ConsoleCaptureActiveRegionSnapshot(OutState);
}

/************************************************************************/

/**
 * @brief Release one console lock snapshot.
 * @param BackendContext Unused.
 * @param State Snapshot pointer.
 */
static void ReleaseConsoleLockState(LPVOID BackendContext, LPVOID State) {
    UNUSED(BackendContext);
    ConsoleReleaseActiveRegionSnapshot(State);
}

/************************************************************************/

/**
 * @brief Restore one previously captured console state.
 * @param BackendContext Unused.
 * @param State Captured snapshot.
 * @return TRUE on success.
 */
static BOOL RestoreConsoleLockState(LPVOID BackendContext, LPVOID State) {
    UNUSED(BackendContext);
    return ConsoleRestoreActiveRegionSnapshot(State);
}

/************************************************************************/

/**
 * @brief Prompt credentials and switch active shell session user.
 * @param Context Shell context.
 * @param Session Existing session.
 * @return TRUE when switch is successful.
 */
static BOOL SwitchLockedSessionUser(LPSHELLCONTEXT Context, LPUSER_SESSION Session) {
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_PASSWORD];
    LPUSER_ACCOUNT Account;
    LPUSER_SESSION NewSession;

    if (Context == NULL || Session == NULL) {
        return FALSE;
    }

    ConsolePrint(TEXT("Username: "));
    if (!ReadLineWithIdleDisabled(Context, UserName, sizeof(UserName), FALSE)) {
        return FALSE;
    }

    if (StringEmpty(UserName)) {
        ConsolePrint(TEXT("Invalid user name\n"));
        return FALSE;
    }

    ConsolePrint(TEXT("Password: "));
    if (!ReadLineWithIdleDisabled(Context, Password, sizeof(Password), TRUE)) {
        return FALSE;
    }

    Account = FindAccount(UserName);
    if (Account == NULL || !VerifyPassword(Password, Account->PasswordHash)) {
        ConsolePrint(TEXT("Invalid credentials\n"));
        return FALSE;
    }

    NewSession = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
    if (NewSession == NULL) {
        ConsolePrint(TEXT("Failed to create user session\n"));
        return FALSE;
    }

    if (!SetCurrentSession(NewSession)) {
        DestroyUserSession(NewSession);
        ConsolePrint(TEXT("Failed to switch session\n"));
        return FALSE;
    }

    DestroyUserSession(Session);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run the console lock screen interaction and unlock one session.
 * @param BackendContext Shell context.
 * @param Session Session to unlock.
 * @return TRUE when unlock succeeds.
 */
static BOOL ShowConsoleLockScreenAndUnlock(LPVOID BackendContext, LPUSER_SESSION Session) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)BackendContext;
    LPUSER_ACCOUNT Account = NULL;
    STR Selection[32];
    STR Password[MAX_PASSWORD];
    UINT WaitRemaining;

    if (Context == NULL || Session == NULL) {
        return FALSE;
    }

    Account = FindAccountByID(Session->UserID);
    if (Account == NULL) {
        return FALSE;
    }

    FOREVER {
        ClearConsole();
        ConsolePrint(TEXT("\n"));
        ConsolePrintCenteredLine(TEXT("Session locked"));
        ConsolePrint(TEXT("\n"));
        {
            STR UserLine[64];
            StringPrintFormat(UserLine, TEXT("User: %s"), Account->UserName);
            ConsolePrintCenteredLine(UserLine);
        }
        ConsolePrint(TEXT("\n"));
        ConsolePrintCenteredLine(TEXT("Press ENTER to unlock current user"));
        ConsolePrintCenteredLine(TEXT("Type 'switch' to change user"));
        ConsolePrint(TEXT("\n"));
        ConsolePrint(TEXT("Action: "));

        Selection[0] = STR_NULL;
        if (!ReadLineWithIdleDisabled(Context, Selection, sizeof(Selection), FALSE)) {
            return FALSE;
        }

        if (StringEmpty(Selection) || STRINGS_EQUAL(Selection, TEXT("unlock"))) {
            WaitRemaining = 0;
            if (!CanAttemptSessionUnlock(Session, &WaitRemaining)) {
                ConsolePrint(TEXT("Too many attempts. Retry in %u ms\n"), (U32)WaitRemaining);
                Sleep((WaitRemaining > 1000) ? 1000 : (U32)WaitRemaining);
                continue;
            }

            ConsolePrint(TEXT("Password: "));
            if (!ReadLineWithIdleDisabled(Context, Password, sizeof(Password), TRUE)) {
                return FALSE;
            }

            if (VerifySessionUnlockPassword(Session, Password)) {
                UnlockUserSession(Session);
                return TRUE;
            }

            ConsolePrint(TEXT("Invalid password\n"));
            Sleep(800);
            continue;
        }

        if (STRINGS_EQUAL(Selection, TEXT("switch"))) {
            if (SwitchLockedSessionUser(Context, Session)) {
                return TRUE;
            }

            Sleep(800);
            continue;
        }

        ConsolePrint(TEXT("Unknown action\n"));
        Sleep(800);
    }
}

/************************************************************************/

/**
 * @brief Process lock/unlock with one UI backend.
 * @param Session Locked session.
 * @param Backend Backend interface.
 * @param BackendContext Backend context.
 * @return TRUE when session ends unlocked.
 */
static BOOL ProcessLockedSessionWithBackend(
    LPUSER_SESSION Session,
    LPSESSION_LOCK_BACKEND_INTERFACE Backend,
    LPVOID BackendContext) {
    LPVOID State = NULL;
    BOOL CaptureResult = FALSE;
    BOOL Result = FALSE;

    if (Session == NULL || Backend == NULL) {
        return FALSE;
    }

    if (Backend->CaptureState != NULL) {
        CaptureResult = Backend->CaptureState(BackendContext, &State);
    }

    if (Backend->ShowAndUnlock != NULL) {
        Result = Backend->ShowAndUnlock(BackendContext, Session);
    }

    if (Result && CaptureResult && Backend->RestoreState != NULL) {
        Backend->RestoreState(BackendContext, State);
    }

    if (Backend->ReleaseState != NULL) {
        Backend->ReleaseState(BackendContext, State);
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Ensure current session is unlocked, locking on inactivity timeout.
 * @param Context Shell context.
 * @return TRUE when shell can continue.
 */
static BOOL EnsureUnlockedSessionForShell(LPSHELLCONTEXT Context) {
    LPUSER_SESSION Session = GetCurrentSession();
    SESSION_LOCK_BACKEND_INTERFACE Backend;

    if (Session == NULL) {
        return TRUE;
    }

    if (!SessionUserRequiresPassword(Session)) {
        if (IsUserSessionLocked(Session)) {
            UnlockUserSession(Session);
        }
        return TRUE;
    }

    if (!IsUserSessionLocked(Session) && IsUserSessionTimedOut(Session)) {
        LockUserSession(Session, USER_SESSION_LOCK_REASON_TIMEOUT);
    }

    if (!IsUserSessionLocked(Session)) {
        return TRUE;
    }

    Backend.CaptureState = CaptureConsoleLockState;
    Backend.ShowAndUnlock = ShowConsoleLockScreenAndUnlock;
    Backend.RestoreState = RestoreConsoleLockState;
    Backend.ReleaseState = ReleaseConsoleLockState;

    return ProcessLockedSessionWithBackend(Session, &Backend, Context);
}

/************************************************************************/

/**
 * @brief Idle callback for shell command line input.
 * @param UserData Shell context.
 * @return TRUE when processed.
 */
static BOOL ShellSessionIdleCallback(LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    if (Context == NULL) {
        return FALSE;
    }

    EnsureUnlockedSessionForShell(Context);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run interactive login bootstrap flow.
 * @return TRUE when login succeeds.
 */
static BOOL HandleUserLoginProcess(void) {
    SHELLCONTEXT TempContext;
    UINT AccountCount = 0;
    BOOL HasUsers = FALSE;

    InitShellContext(&TempContext);
    if (!ShellGetAccountCount(&TempContext, &AccountCount)) {
        ConsolePrint(TEXT("ERROR: Failed to query accounts. System will exit.\n"));
        DeinitShellContext(&TempContext);
        return FALSE;
    }

    HasUsers = (AccountCount != 0);

    if (!HasUsers) {
        ConsolePrint(TEXT("No existing user account. You need to create the first admin user.\n"));

        CMD_addUser(&TempContext);

        if (!ShellGetAccountCount(&TempContext, &AccountCount)) {
            ConsolePrint(TEXT("ERROR: Failed to query accounts after creation. System will exit.\n"));
            DeinitShellContext(&TempContext);
            return FALSE;
        }

        HasUsers = (AccountCount != 0);

        if (HasUsers == FALSE) {
            ConsolePrint(TEXT("ERROR: Failed to create user account. System will exit.\n"));
            DeinitShellContext(&TempContext);
            return FALSE;
        }
    }

    DeinitShellContext(&TempContext);

    ConsolePrint(TEXT("Login\n"));

    for (U32 LoginAttempts = 1; LoginAttempts <= 5; LoginAttempts++) {
        SHELLCONTEXT TempContext;
        LPUSER_SESSION Session = NULL;
        LPUSER_ACCOUNT Account = NULL;
        BOOL LoggedIn = FALSE;

        InitShellContext(&TempContext);
        CMD_login(&TempContext);

        Session = GetCurrentSession();
        SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) {
            Account = FindAccountByID(Session->UserID);
            SAFE_USE_VALID_ID(Account, KOID_USER_ACCOUNT) {
                ConsolePrint(TEXT("Logged in as: %s (%s)\n"), Account->UserName,
                    Account->Privilege == EXOS_PRIVILEGE_ADMIN ? TEXT("Administrator") : TEXT("User"));
                LoggedIn = TRUE;
            }
        }

        DeinitShellContext(&TempContext);
        if (LoggedIn) {
            return TRUE;
        }

        ConsolePrint(TEXT("Login failed. Please try again. (Attempt %u/5)\n\n"), LoginAttempts);
    }

    ConsolePrint(TEXT("Too many failed login attempts.\n"));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Entry point for the interactive shell.
 *
 * Initializes the shell context, runs configured executables and processes
 * user commands until termination.
 *
 * @param Param Unused parameter.
 * @return Exit code of the shell.
 */
U32 Shell(LPVOID Param) {
    TRACED_FUNCTION;

    UNUSED(Param);
    SHELLCONTEXT Context;


    InitShellContext(&Context);
    CommandLineEditorSetIdleCallback(&Context.Input.Editor, ShellSessionIdleCallback, &Context);

    if (GetDoLogin() && !HandleUserLoginProcess()) { return 0; }

    ExecuteStartupCommands();

    while (EnsureUnlockedSessionForShell(&Context) && ParseCommand(&Context)) {
    }

    ConsolePrint(TEXT("Exiting shell\n"));

    DeinitShellContext(&Context);


    TRACED_EPILOGUE("Shell");
    return 1;
}
