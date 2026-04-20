
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


    Shell execution

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "core/ID.h"
#include "system/SYSCall.h"

/************************************************************************/

typedef INT (*SHELL_SCRIPT_FUNCTION_HANDLER)(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments);

typedef struct tag_SHELL_SCRIPT_FUNCTION_ENTRY {
    LPCSTR Name;
    SHELL_SCRIPT_FUNCTION_HANDLER Handler;
} SHELL_SCRIPT_FUNCTION_ENTRY;

/************************************************************************/

/**
 * @brief Check if a script text already contains a line break.
 * @param Text Text to inspect.
 * @return TRUE when the text contains '\r' or '\n', FALSE otherwise.
 */
static BOOL ShellScriptContainsLineBreak(LPCSTR Text) {
    if (Text == NULL) {
        return FALSE;
    }

    if (StringFindChar(Text, '\n') != NULL) {
        return TRUE;
    }

    if (StringFindChar(Text, '\r') != NULL) {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Launch executables listed in the kernel configuration.
 *
 * Each [[Run]] item of exos.toml is checked and the command is executed
 * using the same pipeline as interactive shell commands.
 */
void ExecuteStartupCommands(void) {
    U32 ConfigIndex = 0;
    STR Key[MAX_USER_NAME];
    LPCSTR CommandLine;
    SHELLCONTEXT Context;


    // Wait 2 seconds for network stack to stabilize (ARP, etc.)
    Sleep(2000);

    LPTOML Configuration = GetConfiguration();
    if (Configuration == NULL) {
        return;
    }

    InitShellContext(&Context);

    FOREVER {
        StringPrintFormat(Key, TEXT("Run.%u.Command"), ConfigIndex);
        CommandLine = TomlGet(Configuration, Key);
        if (CommandLine == NULL) break;

        ExecuteCommandLine(&Context, CommandLine);

        ConfigIndex++;
    }

    DeinitShellContext(&Context);

}

/************************************************************************/

/**
 * @brief Execute a command line string.
 *
 * Parses and executes a command line
 *
 * @param Context Shell context to use for execution
 * @param CommandLine Command line string to execute
 */
void ExecuteCommandLine(LPSHELLCONTEXT Context, LPCSTR CommandLine) {
    SAFE_USE_3(Context, Context->ScriptContext, CommandLine) {

        SCRIPT_ERROR Error = ScriptExecute(Context->ScriptContext, CommandLine);

        if (Error != SCRIPT_OK) {
            ConsolePrint(TEXT("Error: %s\n"), ScriptGetErrorMessage(Context->ScriptContext));
        }
    } else {
        ERROR(TEXT("[ExecuteCommandLine] Null pointer\n"));
    }
}

/************************************************************************/

/**
 * @brief Parse and execute a single command line from user input.
 *
 * @param Context Shell context to fill and execute.
 * @return TRUE to continue the shell loop, FALSE otherwise.
 */
BOOL ParseCommand(LPSHELLCONTEXT Context) {

    ShowPrompt(Context);

    Context->Component = 0;
    Context->CommandChar = 0;
    MemorySet(Context->Input.CommandLine, 0, sizeof Context->Input.CommandLine);

    CommandLineEditorReadLine(&Context->Input.Editor, Context->Input.CommandLine, sizeof Context->Input.CommandLine, FALSE);

    if (Context->Input.CommandLine[0] != STR_NULL) {
        LPUSER_SESSION Session = NULL;

        CommandLineEditorRemember(&Context->Input.Editor, Context->Input.CommandLine);
        ConsoleResetPaging();
        ExecuteCommandLine(Context, Context->Input.CommandLine);
        Session = GetCurrentSession();
        SAFE_USE_VALID_ID(Session, KOID_USER_SESSION) { UpdateSessionActivity(Session); }
    }


    return TRUE;
}

/************************************************************************/

/**
 * @brief Shell callback for script output.
 * @param Message Message to output
 * @param UserData Shell context (unused)
 */
void ShellScriptOutput(LPCSTR Message, LPVOID UserData) {
    UNUSED(UserData);
    ConsolePrint(Message);
}

/************************************************************************/

/**
 * @brief Shell callback for script command execution.
 * @param Command Command to execute
 * @param UserData Shell context
 * @return DF_RETURN_SUCCESS on success or an error code on failure
 */
UINT ShellScriptExecuteCommand(LPCSTR Command, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    U32 Index;
    U32 Result = DF_RETURN_GENERIC;

    if (Context == NULL || Command == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }


    StringCopy(Context->Input.CommandLine, Command);

    ClearOptions(Context);

    Context->Component = 0;
    Context->CommandChar = 0;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        Result = DF_RETURN_SUCCESS;
        return Result;
    }

    {
        STR CommandName[MAX_FILE_NAME];
        StringCopy(CommandName, Context->Command);

        for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
            if (StringCompareNC(CommandName, COMMANDS[Index].Name) == 0 ||
                StringCompareNC(CommandName, COMMANDS[Index].AltName) == 0) {
                Result = COMMANDS[Index].Command(Context);
                return Result;
            }
        }

        if (SpawnExecutable(Context, Context->Input.CommandLine, FALSE) == TRUE) {
            Result = DF_RETURN_SUCCESS;
            return Result;
        }

        if (Context->ScriptContext) {
            Context->ScriptContext->ErrorCode = SCRIPT_ERROR_SYNTAX;
            StringPrintFormat(
                Context->ScriptContext->ErrorMessage,
                TEXT("Unknown command: %s"),
                CommandName);
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Shell callback for script variable resolution.
 * @param VarName Variable name to resolve
 * @param UserData Shell context (unused)
 * @return Variable value or NULL if not found
 */
LPCSTR ShellScriptResolveVariable(LPCSTR VarName, LPVOID UserData) {
    UNUSED(VarName);
    UNUSED(UserData);
    return NULL;
}

/************************************************************************/

/**
 * @brief Concatenate script callback arguments into one shell-style string.
 * @param Context Shell context used for buffer storage.
 * @param ArgumentCount Number of arguments to concatenate.
 * @param Arguments Argument vector.
 * @return Pointer to an internal shell buffer, or NULL on failure.
 */
static LPCSTR ShellScriptJoinArguments(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    UINT BufferIndex = 0;
    UINT Index;

    if (Context == NULL || Context->Buffer[0] == NULL || ArgumentCount == 0 || Arguments == NULL) {
        return NULL;
    }

    for (Index = 0; Index < ArgumentCount; Index++) {
        LPCSTR Argument = Arguments[Index];
        U32 ArgumentLength;

        if (Argument == NULL) {
            Argument = TEXT("");
        }

        ArgumentLength = StringLength(Argument);
        if (BufferIndex + ArgumentLength + 2 >= BUFFER_SIZE) {
            return NULL;
        }

        if (Index > 0) {
            Context->Buffer[0][BufferIndex++] = STR_SPACE;
        }

        MemoryCopy(Context->Buffer[0] + BufferIndex, Argument, ArgumentLength);
        BufferIndex += ArgumentLength;
    }

    Context->Buffer[0][BufferIndex] = STR_NULL;
    return Context->Buffer[0];
}

/************************************************************************/

/**
 * @brief Store one script error for a shell host function and return failure sentinel.
 * @param Context Shell context owning the script context.
 * @param ErrorCode Script error code to store.
 * @param Message Human-readable error message.
 * @return SCRIPT_FUNCTION_STATUS_ERROR.
 */
static INT ShellScriptFailFunction(
    LPSHELLCONTEXT Context,
    SCRIPT_ERROR ErrorCode,
    LPCSTR Message) {
    if (Context != NULL && Context->ScriptContext != NULL) {
        Context->ScriptContext->ErrorCode = ErrorCode;
        if (Message != NULL) {
            StringCopy(Context->ScriptContext->ErrorMessage, Message);
        }
    }

    return SCRIPT_FUNCTION_STATUS_ERROR;
}

/************************************************************************/

/**
 * @brief Kill one process or task referenced by a user-visible handle.
 * @param Context Shell context owning the script context.
 * @param HandleValue Serialized handle value.
 * @return Non-zero on success or SCRIPT_FUNCTION_STATUS_ERROR on failure.
 */
static INT ShellScriptKillHandle(LPSHELLCONTEXT Context, LPCSTR HandleValue) {
    UINT Handle = 0;
    LINEAR ObjectPointer = 0;
    LPOBJECT Object = NULL;
    UINT Status = 0;

    if (HandleValue == NULL || StringLength(HandleValue) == 0) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_SYNTAX, TEXT("kill(handle) expects one handle argument"));
    }

    Handle = StringToU32(HandleValue);
    if (Handle < HANDLE_MINIMUM) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, TEXT("kill(handle) expects a valid handle"));
    }

    ObjectPointer = HandleToPointer((HANDLE)Handle);
    if (ObjectPointer == 0) {
        return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNDEFINED_VAR, TEXT("kill(handle) received an unknown handle"));
    }

    Object = (LPOBJECT)ObjectPointer;
    SAFE_USE_VALID(Object) {
        if (Object->TypeID == KOID_PROCESS) {
            Status = SysCall_KillProcess(Handle);
            if (Status == 0) {
                return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNAUTHORIZED, TEXT("kill(handle) failed to terminate the process"));
            }
            return (INT)Status;
        }

        if (Object->TypeID == KOID_TASK) {
            Status = SysCall_KillTask(Handle);
            if (Status == 0) {
                return ShellScriptFailFunction(Context, SCRIPT_ERROR_UNAUTHORIZED, TEXT("kill(handle) failed to terminate the task"));
            }
            return (INT)Status;
        }
    }

    return ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, TEXT("kill(handle) only supports process or task handles"));
}

/************************************************************************/

/**
 * @brief Parse one positive integer argument for a shell host function.
 * @param Context Shell context owning the script state.
 * @param FunctionName Name of the host function.
 * @param ParameterName Logical parameter name.
 * @param ValueText Serialized argument text.
 * @param OutValue Parsed integer value.
 * @return TRUE on success.
 */
static BOOL ShellScriptParsePositiveInteger(
    LPSHELLCONTEXT Context,
    LPCSTR FunctionName,
    LPCSTR ParameterName,
    LPCSTR ValueText,
    U32* OutValue) {
    U32 Index = 0;
    U32 Value = 0;

    if (OutValue == NULL) {
        return FALSE;
    }

    if (ValueText == NULL || StringLength(ValueText) == 0) {
        STR Message[MAX_ERROR_MESSAGE];

        StringPrintFormat(
            Message,
            TEXT("%s() expects %s to be a positive integer"),
            FunctionName,
            ParameterName);
        ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, Message);
        return FALSE;
    }

    for (Index = 0; ValueText[Index] != STR_NULL; Index++) {
        if (!IsNumeric(ValueText[Index])) {
            STR Message[MAX_ERROR_MESSAGE];

            StringPrintFormat(
                Message,
                TEXT("%s() expects %s to be a positive integer"),
                FunctionName,
                ParameterName);
            ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, Message);
            return FALSE;
        }
    }

    Value = StringToU32(ValueText);
    if (Value == 0) {
        STR Message[MAX_ERROR_MESSAGE];

        StringPrintFormat(
            Message,
            TEXT("%s() expects %s to be a positive integer"),
            FunctionName,
            ParameterName);
        ShellScriptFailFunction(Context, SCRIPT_ERROR_TYPE_MISMATCH, Message);
        return FALSE;
    }

    *OutValue = Value;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate one dedicated smoke-test multi-argument host call.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return One deterministic magic value on success.
 */
static INT ShellScriptSmokeTestMultiArgs(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    if (ArgumentCount != 4 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("smokeTestMultiArgs(a, b, c, d) expects exactly four arguments"));
    }

    if (StringCompare(Arguments[0], TEXT("alpha")) != 0 ||
        StringCompare(Arguments[1], TEXT("17")) != 0 ||
        StringCompare(Arguments[2], TEXT("23")) != 0 ||
        StringCompare(Arguments[3], TEXT("1")) != 0) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("smokeTestMultiArgs() received unexpected serialized arguments"));
    }

    return 42023171;
}

/************************************************************************/

/**
 * @brief Execute one shell command line from a script host function.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return Shell command status or one script function error sentinel.
 */
static INT ShellScriptExecFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    LPCSTR JoinedArguments;

    if (Context == NULL || ArgumentCount == 0 || Arguments == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    JoinedArguments = ShellScriptJoinArguments(Context, ArgumentCount, Arguments);
    if (JoinedArguments == NULL) {
        return DF_RETURN_GENERIC;
    }

    // Execute the provided command line using the standard shell command flow
    return (INT)ShellScriptExecuteCommand(JoinedArguments, Context);
}

/************************************************************************/

/**
 * @brief Print serialized script arguments to the active console.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return Zero on success or an error code on failure.
 */
static INT ShellScriptPrintFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    LPCSTR JoinedArguments;

    if (ArgumentCount == 0 || Arguments == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    JoinedArguments = ShellScriptJoinArguments(Context, ArgumentCount, Arguments);
    if (JoinedArguments == NULL) {
        return DF_RETURN_GENERIC;
    }

    ConsolePrint(TEXT("%s"), JoinedArguments);
    if (!ShellScriptContainsLineBreak(JoinedArguments)) {
        ConsolePrint(TEXT("\r\n"));
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Terminate one process or task from a script host function.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return Handle termination status or one script function error sentinel.
 */
static INT ShellScriptKillFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    if (ArgumentCount != 1 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("kill(handle) expects exactly one handle argument"));
    }

    return ShellScriptKillHandle(Context, Arguments[0]);
}

/************************************************************************/

/**
 * @brief Validate and apply one graphics driver selection request.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return System call status or one script function error sentinel.
 */
static INT ShellScriptSetGraphicsDriverFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    GRAPHICS_DRIVER_SELECTION_INFO SelectionInfo;
    U32 Width = 0;
    U32 Height = 0;
    U32 BitsPerPixel = 0;
    UINT Status = 0;

    if (ArgumentCount != 4 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("setGraphicsDriver(driverAlias, width, height, bpp) expects exactly four arguments"));
    }

    if (Arguments[0] == NULL || StringLength(Arguments[0]) == 0) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("setGraphicsDriver() expects a non-empty driver alias"));
    }

    if (StringLength(Arguments[0]) >= MAX_NAME) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("setGraphicsDriver() driverAlias exceeds MAX_NAME"));
    }

    if (!ShellScriptParsePositiveInteger(Context, TEXT("setGraphicsDriver"), TEXT("width"), Arguments[1], &Width) ||
        !ShellScriptParsePositiveInteger(Context, TEXT("setGraphicsDriver"), TEXT("height"), Arguments[2], &Height) ||
        !ShellScriptParsePositiveInteger(Context, TEXT("setGraphicsDriver"), TEXT("bpp"), Arguments[3], &BitsPerPixel)) {
        return SCRIPT_FUNCTION_STATUS_ERROR;
    }

    MemorySet(&SelectionInfo, 0, sizeof(SelectionInfo));
    SelectionInfo.Header.Size = sizeof(SelectionInfo);
    SelectionInfo.Header.Version = EXOS_ABI_VERSION;
    SelectionInfo.Header.Flags = 0;
    StringCopyLimit(SelectionInfo.DriverAlias, Arguments[0], MAX_NAME);
    SelectionInfo.Width = Width;
    SelectionInfo.Height = Height;
    SelectionInfo.BitsPerPixel = BitsPerPixel;

    Status = DoSystemCall(SYSCALL_SetGraphicsDriver, SYSCALL_PARAM(&SelectionInfo));
    if (Status != DF_RETURN_SUCCESS) {
        STR ErrorMessage[MAX_ERROR_MESSAGE];

        if (Status == DF_RETURN_BAD_PARAMETER) {
            StringPrintFormat(
                ErrorMessage,
                TEXT("setGraphicsDriver() could not select '%s'"),
                SelectionInfo.DriverAlias);
        } else if (Status == DF_RETURN_UNEXPECTED) {
            StringCopy(ErrorMessage, TEXT("setGraphicsDriver() failed to update the display session"));
        } else {
            StringPrintFormat(
                ErrorMessage,
                TEXT("setGraphicsDriver() failed to apply %ux%ux%u on '%s' (%u)"),
                Width,
                Height,
                BitsPerPixel,
                SelectionInfo.DriverAlias,
                Status);
        }

        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            ErrorMessage);
    }

    return (INT)Status;
}

/************************************************************************/

/**
 * @brief Create one user account from a script host function.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return DF_RETURN_SUCCESS on success or one script function error sentinel.
 */
static INT ShellScriptCreateAccountFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    U32 Privilege;
    UINT Status;

    if (ArgumentCount != 3 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("createAccount(userName, password, privilege) expects exactly three arguments"));
    }

    if (!ShellScriptParsePositiveInteger(Context, TEXT("createAccount"), TEXT("privilege"), Arguments[2], &Privilege)) {
        return SCRIPT_FUNCTION_STATUS_ERROR;
    }

    Status = ShellCreateAccount(Arguments[0], Arguments[1], Privilege);
    if (Status != TRUE) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("createAccount() failed"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Delete one user account from a script host function.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return DF_RETURN_SUCCESS on success or one script function error sentinel.
 */
static INT ShellScriptDeleteAccountFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    UINT Status;

    if (ArgumentCount != 1 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("deleteAccount(userName) expects exactly one argument"));
    }

    Status = ShellDeleteAccount(Arguments[0]);
    if (Status != TRUE) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("deleteAccount() failed"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Change the active user password from a script host function.
 * @param Context Shell context owning the script state.
 * @param ArgumentCount Number of serialized arguments.
 * @param Arguments Serialized arguments.
 * @return DF_RETURN_SUCCESS on success or one script function error sentinel.
 */
static INT ShellScriptChangePasswordFunction(
    LPSHELLCONTEXT Context,
    UINT ArgumentCount,
    LPCSTR* Arguments) {
    UINT Status;

    if (ArgumentCount != 2 || Arguments == NULL) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_SYNTAX,
            TEXT("changePassword(oldPassword, newPassword) expects exactly two arguments"));
    }

    Status = ShellChangePassword(Arguments[0], Arguments[1]);
    if (Status != TRUE) {
        return ShellScriptFailFunction(
            Context,
            SCRIPT_ERROR_TYPE_MISMATCH,
            TEXT("changePassword() failed"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static const SHELL_SCRIPT_FUNCTION_ENTRY ShellScriptFunctionTable[] = {
    {TEXT("exec"), ShellScriptExecFunction},
    {TEXT("print"), ShellScriptPrintFunction},
    {TEXT("kill"), ShellScriptKillFunction},
    {TEXT("smokeTestMultiArgs"), ShellScriptSmokeTestMultiArgs},
    {TEXT("setGraphicsDriver"), ShellScriptSetGraphicsDriverFunction},
    {TEXT("createAccount"), ShellScriptCreateAccountFunction},
    {TEXT("deleteAccount"), ShellScriptDeleteAccountFunction},
    {TEXT("changePassword"), ShellScriptChangePasswordFunction},
    {NULL, NULL}
};

/************************************************************************/

/**
 * @brief Retrieve the exposed account count from one shell script context.
 * @param Context Shell context that owns the script host registry.
 * @param OutCount Destination count.
 * @return TRUE on success.
 */
BOOL ShellGetAccountCount(LPSHELLCONTEXT Context, UINT* OutCount) {
    SCRIPT_VALUE AccountValue;
    SCRIPT_VALUE CountValue;
    SCRIPT_ERROR Error;
    BOOL Success = FALSE;

    if (Context == NULL || Context->ScriptContext == NULL || OutCount == NULL) {
        return FALSE;
    }

    ScriptValueInit(&AccountValue);
    ScriptValueInit(&CountValue);

    Error = ScriptGetHostSymbolValue(Context->ScriptContext, TEXT("account"), &AccountValue);
    if (Error != SCRIPT_OK) {
        goto Cleanup;
    }

    Error = ScriptGetHostPropertyValue(&AccountValue, TEXT("count"), &CountValue);
    if (Error != SCRIPT_OK) {
        goto Cleanup;
    }

    if (CountValue.Type != SCRIPT_VAR_INTEGER || CountValue.Value.Integer < 0) {
        goto Cleanup;
    }

    *OutCount = (UINT)CountValue.Value.Integer;
    Success = TRUE;

Cleanup:
    ScriptValueRelease(&CountValue);
    ScriptValueRelease(&AccountValue);
    return Success;
}

/************************************************************************/

/**
 * @brief Shell callback for script function calls.
 * @param FuncName Function name to call
 * @param ArgumentCount Number of stringified arguments
 * @param Arguments String arguments for the function
 * @param UserData Shell context
 * @return Function result (U32)
 */
INT ShellScriptCallFunction(LPCSTR FuncName, UINT ArgumentCount, LPCSTR* Arguments, LPVOID UserData) {
    LPSHELLCONTEXT Context = (LPSHELLCONTEXT)UserData;
    UINT Index;

    if (FuncName == NULL) {
        return SCRIPT_FUNCTION_STATUS_UNKNOWN;
    }

    for (Index = 0; ShellScriptFunctionTable[Index].Name != NULL; Index++) {
        if (STRINGS_EQUAL(FuncName, ShellScriptFunctionTable[Index].Name)) {
            return ShellScriptFunctionTable[Index].Handler(Context, ArgumentCount, Arguments);
        }
    }

    return SCRIPT_FUNCTION_STATUS_UNKNOWN;
}
