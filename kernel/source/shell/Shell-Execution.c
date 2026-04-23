
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


    Shell execution

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "core/ID.h"
#include "system/SYSCall.h"
#include "utils/Pipe.h"

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

#define SHELL_PIPELINE_MAX_STAGES 8

typedef struct tag_SHELL_PIPELINE_STAGE {
    STR CommandLine[MAX_PATH_NAME];
    STR InputPath[MAX_PATH_NAME];
    STR OutputPath[MAX_PATH_NAME];
    STR ErrorPath[MAX_PATH_NAME];
    BOOL OutputAppend;
    BOOL ErrorAppend;
    BOOL ErrorToOutput;
} SHELL_PIPELINE_STAGE, *LPSHELL_PIPELINE_STAGE;

typedef struct tag_SHELL_PIPELINE_PLAN {
    SHELL_PIPELINE_STAGE Stages[SHELL_PIPELINE_MAX_STAGES];
    UINT StageCount;
} SHELL_PIPELINE_PLAN, *LPSHELL_PIPELINE_PLAN;

/************************************************************************/

typedef enum tag_SHELL_REDIRECTION_STREAM {
    SHELL_REDIRECTION_STREAM_NONE = 0,
    SHELL_REDIRECTION_STREAM_IN = 1,
    SHELL_REDIRECTION_STREAM_OUT = 2,
    SHELL_REDIRECTION_STREAM_ERROR = 3
} SHELL_REDIRECTION_STREAM;

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

static BOOL ShellCommandLineHasRedirectionOrPipe(LPCSTR CommandLine) {
    BOOL InQuotes = FALSE;
    UINT Index = 0;

    if (CommandLine == NULL) {
        return FALSE;
    }

    while (CommandLine[Index] != STR_NULL) {
        if (CommandLine[Index] == STR_QUOTE) {
            InQuotes = !InQuotes;
        } else if (!InQuotes &&
                (CommandLine[Index] == '|' || CommandLine[Index] == '<' || CommandLine[Index] == '>')) {
            return TRUE;
        }

        Index++;
    }

    return FALSE;
}

/************************************************************************/

static BOOL ShellIsTokenBoundary(STR Character) {
    return Character == STR_NULL || Character <= STR_SPACE || Character == '|' || Character == '<' ||
           Character == '>';
}

/************************************************************************/

static SHELL_REDIRECTION_STREAM ShellParseRedirectionStream(LPCSTR Token) {
    if (Token == NULL) {
        return SHELL_REDIRECTION_STREAM_NONE;
    }

    if (StringCompare(Token, TEXT("0")) == 0 || StringCompare(Token, TEXT("in")) == 0) {
        return SHELL_REDIRECTION_STREAM_IN;
    }

    if (StringCompare(Token, TEXT("1")) == 0 || StringCompare(Token, TEXT("out")) == 0) {
        return SHELL_REDIRECTION_STREAM_OUT;
    }

    if (StringCompare(Token, TEXT("2")) == 0 || StringCompare(Token, TEXT("error")) == 0) {
        return SHELL_REDIRECTION_STREAM_ERROR;
    }

    return SHELL_REDIRECTION_STREAM_NONE;
}

/************************************************************************/

static BOOL ShellTokenIsOutputAlias(LPCSTR Token) {
    SHELL_REDIRECTION_STREAM Stream;

    Stream = ShellParseRedirectionStream(Token);
    return Stream == SHELL_REDIRECTION_STREAM_OUT;
}

/************************************************************************/

static BOOL ShellHasUpcomingRedirectionOperator(LPCSTR Text, UINT Index) {
    if (Text == NULL) {
        return FALSE;
    }

    while (Text[Index] != STR_NULL && Text[Index] <= STR_SPACE) {
        Index++;
    }

    if (Text[Index] == '<' || Text[Index] == '>') {
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL ShellReadPipelineToken(LPCSTR Text, UINT* InOutIndex, STR Token[MAX_PATH_NAME]) {
    UINT ReadIndex;
    UINT WriteIndex = 0;
    BOOL InQuotes = FALSE;

    if (Text == NULL || InOutIndex == NULL || Token == NULL) {
        return FALSE;
    }

    ReadIndex = *InOutIndex;
    while (Text[ReadIndex] != STR_NULL && Text[ReadIndex] <= STR_SPACE) {
        ReadIndex++;
    }

    if (Text[ReadIndex] == STR_NULL) {
        *InOutIndex = ReadIndex;
        return FALSE;
    }

    if (Text[ReadIndex] == '|') {
        Token[0] = '|';
        Token[1] = STR_NULL;
        *InOutIndex = ReadIndex + 1;
        return TRUE;
    }

    if (Text[ReadIndex] == '<') {
        Token[0] = '<';
        Token[1] = STR_NULL;
        *InOutIndex = ReadIndex + 1;
        return TRUE;
    }

    if (Text[ReadIndex] == '>') {
        Token[0] = '>';
        if (Text[ReadIndex + 1] == '>') {
            Token[1] = '>';
            Token[2] = STR_NULL;
            *InOutIndex = ReadIndex + 2;
        } else {
            Token[1] = STR_NULL;
            *InOutIndex = ReadIndex + 1;
        }
        return TRUE;
    }

    if (Text[ReadIndex] == '2' && Text[ReadIndex + 1] == '>' && Text[ReadIndex + 2] == '&' && Text[ReadIndex + 3] == '1') {
        Token[0] = '2';
        Token[1] = '>';
        Token[2] = '&';
        Token[3] = '1';
        Token[4] = STR_NULL;
        *InOutIndex = ReadIndex + 4;
        return TRUE;
    }

    if (Text[ReadIndex] == '2' && Text[ReadIndex + 1] == '>' && Text[ReadIndex + 2] == '>') {
        Token[0] = '2';
        Token[1] = '>';
        Token[2] = '>';
        Token[3] = STR_NULL;
        *InOutIndex = ReadIndex + 3;
        return TRUE;
    }

    if (Text[ReadIndex] == '2' && Text[ReadIndex + 1] == '>') {
        Token[0] = '2';
        Token[1] = '>';
        Token[2] = STR_NULL;
        *InOutIndex = ReadIndex + 2;
        return TRUE;
    }

    while (Text[ReadIndex] != STR_NULL && (InQuotes || !ShellIsTokenBoundary(Text[ReadIndex]))) {
        if (Text[ReadIndex] == STR_QUOTE) {
            InQuotes = !InQuotes;
        } else if (WriteIndex < MAX_PATH_NAME - 1) {
            Token[WriteIndex] = Text[ReadIndex];
            WriteIndex++;
        }

        ReadIndex++;
    }

    Token[WriteIndex] = STR_NULL;
    *InOutIndex = ReadIndex;
    return WriteIndex > 0;
}

/************************************************************************/

static BOOL ShellAppendTokenToCommandLine(STR CommandLine[MAX_PATH_NAME], LPCSTR Token) {
    BOOL NeedsQuotes = FALSE;
    UINT Index = 0;
    UINT CurrentLength;
    UINT TokenLength;

    if (CommandLine == NULL || Token == NULL) {
        return FALSE;
    }

    TokenLength = StringLength(Token);
    CurrentLength = StringLength(CommandLine);

    while (Token[Index] != STR_NULL) {
        if (Token[Index] <= STR_SPACE) {
            NeedsQuotes = TRUE;
            break;
        }
        Index++;
    }

    if (CurrentLength > 0) {
        if (CurrentLength + 1 >= MAX_PATH_NAME) {
            return FALSE;
        }

        CommandLine[CurrentLength++] = STR_SPACE;
        CommandLine[CurrentLength] = STR_NULL;
    }

    if (!NeedsQuotes) {
        if (CurrentLength + TokenLength >= MAX_PATH_NAME) {
            return FALSE;
        }

        StringConcat(CommandLine, Token);
        return TRUE;
    }

    if (CurrentLength + TokenLength + 2 >= MAX_PATH_NAME) {
        return FALSE;
    }

    CommandLine[CurrentLength++] = STR_QUOTE;
    CommandLine[CurrentLength] = STR_NULL;
    StringConcat(CommandLine, Token);
    CurrentLength = StringLength(CommandLine);
    CommandLine[CurrentLength++] = STR_QUOTE;
    CommandLine[CurrentLength] = STR_NULL;
    return TRUE;
}

/************************************************************************/

static BOOL ShellParsePipelinePlan(LPCSTR CommandLine, LPSHELL_PIPELINE_PLAN Plan) {
    UINT Index = 0;
    STR Token[MAX_PATH_NAME];
    UINT StageIndex = 0;
    BOOL ExpectInputPath = FALSE;
    BOOL ExpectOutputPath = FALSE;
    BOOL ExpectErrorPath = FALSE;
    BOOL OutputAppend = FALSE;
    BOOL ErrorAppend = FALSE;
    SHELL_REDIRECTION_STREAM PendingStream = SHELL_REDIRECTION_STREAM_NONE;

    if (CommandLine == NULL || Plan == NULL) {
        return FALSE;
    }

    MemorySet(Plan, 0, sizeof(SHELL_PIPELINE_PLAN));
    Plan->StageCount = 1;

    while (ShellReadPipelineToken(CommandLine, &Index, Token)) {
        LPSHELL_PIPELINE_STAGE Stage = &Plan->Stages[StageIndex];

        if (ExpectInputPath) {
            if (ShellParseRedirectionStream(Token) != SHELL_REDIRECTION_STREAM_NONE) {
                return FALSE;
            }
            StringCopy(Stage->InputPath, Token);
            ExpectInputPath = FALSE;
            continue;
        }

        if (ExpectOutputPath) {
            if (ShellParseRedirectionStream(Token) != SHELL_REDIRECTION_STREAM_NONE) {
                return FALSE;
            }
            StringCopy(Stage->OutputPath, Token);
            Stage->OutputAppend = OutputAppend;
            ExpectOutputPath = FALSE;
            continue;
        }

        if (ExpectErrorPath) {
            if (ShellTokenIsOutputAlias(Token)) {
                Stage->ErrorToOutput = TRUE;
                ExpectErrorPath = FALSE;
                continue;
            }

            if (ShellParseRedirectionStream(Token) != SHELL_REDIRECTION_STREAM_NONE) {
                return FALSE;
            }
            StringCopy(Stage->ErrorPath, Token);
            Stage->ErrorAppend = ErrorAppend;
            ExpectErrorPath = FALSE;
            continue;
        }

        if (StringCompare(Token, TEXT("|")) == 0) {
            if (StringEmpty(Stage->CommandLine) || StageIndex + 1 >= SHELL_PIPELINE_MAX_STAGES) {
                return FALSE;
            }

            StageIndex++;
            Plan->StageCount = StageIndex + 1;
            PendingStream = SHELL_REDIRECTION_STREAM_NONE;
            continue;
        }

        if (StringCompare(Token, TEXT("<")) == 0) {
            SHELL_REDIRECTION_STREAM TargetStream = PendingStream;

            if (TargetStream == SHELL_REDIRECTION_STREAM_NONE) {
                TargetStream = SHELL_REDIRECTION_STREAM_IN;
            }

            if (TargetStream != SHELL_REDIRECTION_STREAM_IN) {
                return FALSE;
            }

            PendingStream = SHELL_REDIRECTION_STREAM_NONE;
            ExpectInputPath = TRUE;
            continue;
        }

        if (StringCompare(Token, TEXT(">")) == 0 || StringCompare(Token, TEXT(">>")) == 0) {
            SHELL_REDIRECTION_STREAM TargetStream = PendingStream;

            if (TargetStream == SHELL_REDIRECTION_STREAM_NONE) {
                TargetStream = SHELL_REDIRECTION_STREAM_OUT;
            }

            OutputAppend = StringCompare(Token, TEXT(">>")) == 0;
            PendingStream = SHELL_REDIRECTION_STREAM_NONE;

            if (TargetStream == SHELL_REDIRECTION_STREAM_OUT) {
                ExpectOutputPath = TRUE;
            } else if (TargetStream == SHELL_REDIRECTION_STREAM_ERROR) {
                ExpectErrorPath = TRUE;
                ErrorAppend = OutputAppend;
            } else {
                return FALSE;
            }
            continue;
        }

        if (StringCompare(Token, TEXT("2>")) == 0 || StringCompare(Token, TEXT("2>>")) == 0) {
            ExpectErrorPath = TRUE;
            ErrorAppend = StringCompare(Token, TEXT("2>>")) == 0;
            continue;
        }

        if (StringCompare(Token, TEXT("2>&1")) == 0) {
            Stage->ErrorToOutput = TRUE;
            continue;
        }

        SHELL_REDIRECTION_STREAM Stream = ShellParseRedirectionStream(Token);
        if (Stream != SHELL_REDIRECTION_STREAM_NONE && ShellHasUpcomingRedirectionOperator(CommandLine, Index)) {
            PendingStream = Stream;
            continue;
        }

        if (!ShellAppendTokenToCommandLine(Stage->CommandLine, Token)) {
            return FALSE;
        }
    }

    if (ExpectInputPath || ExpectOutputPath || ExpectErrorPath) {
        return FALSE;
    }

    for (Index = 0; Index < Plan->StageCount; Index++) {
        if (StringEmpty(Plan->Stages[Index].CommandLine)) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static HANDLE ShellOpenRedirectionHandle(LPSHELLCONTEXT Context, LPCSTR RawPath, UINT OpenFlags) {
    FILE_OPEN_INFO FileOpenInfo;
    STR QualifiedPath[MAX_PATH_NAME];
    LPFILE File = NULL;
    HANDLE FileHandle = 0;

    if (Context == NULL || STRING_EMPTY(RawPath)) {
        return 0;
    }

    if (!QualifyFileName(Context, RawPath, QualifiedPath)) {
        return 0;
    }

    MemorySet(&FileOpenInfo, 0, sizeof(FILE_OPEN_INFO));
    FileOpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = QualifiedPath;
    FileOpenInfo.Flags = OpenFlags;

    File = OpenFile(&FileOpenInfo);
    SAFE_USE_VALID_ID(File, KOID_FILE) {
        FileHandle = PointerToHandle((LINEAR)File);
        if (FileHandle == 0) {
            CloseFile(File);
        }
    }

    return FileHandle;
}

/************************************************************************/

static BOOL ShellRunPipelinePlan(LPSHELLCONTEXT Context, LPSHELL_PIPELINE_PLAN Plan) {
    HANDLE PreviousReadHandle = 0;
    WAIT_INFO WaitInfo;
    UINT StageIndex;

    if (Context == NULL || Plan == NULL || Plan->StageCount == 0) {
        return FALSE;
    }

    MemorySet(&WaitInfo, 0, sizeof(WaitInfo));
    WaitInfo.Header.Size = sizeof(WAIT_INFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 0;
    WaitInfo.MilliSeconds = INFINITY;

    for (StageIndex = 0; StageIndex < Plan->StageCount; StageIndex++) {
        LPSHELL_PIPELINE_STAGE Stage = &Plan->Stages[StageIndex];
        STR QualifiedCommandLine[MAX_PATH_NAME];
        HANDLE InputHandle = 0;
        HANDLE OutputHandle = 0;
        HANDLE ErrorHandle = 0;
        HANDLE NextReadHandle = 0;
        HANDLE NextWriteHandle = 0;
        PROCESS_INFO ProcessInfo;

        if (!QualifyCommandLine(Context, Stage->CommandLine, QualifiedCommandLine)) {
            ConsolePrint(TEXT("Unknown command: %s\n"), Stage->CommandLine);
            return FALSE;
        }

        if (!STRING_EMPTY(Stage->InputPath)) {
            InputHandle = ShellOpenRedirectionHandle(Context, Stage->InputPath, FILE_OPEN_READ | FILE_OPEN_EXISTING);
            if (InputHandle == 0) {
                ConsolePrint(TEXT("Cannot open input redirection: %s\n"), Stage->InputPath);
                return FALSE;
            }
        } else if (PreviousReadHandle != 0) {
            InputHandle = PreviousReadHandle;
        }

        if (!STRING_EMPTY(Stage->OutputPath)) {
            UINT OpenFlags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS;
            if (Stage->OutputAppend) {
                OpenFlags |= FILE_OPEN_SEEK_END;
            } else {
                OpenFlags |= FILE_OPEN_TRUNCATE;
            }

            OutputHandle = ShellOpenRedirectionHandle(Context, Stage->OutputPath, OpenFlags);
            if (OutputHandle == 0) {
                if (InputHandle != 0) {
                    CloseHandle(InputHandle);
                }
                ConsolePrint(TEXT("Cannot open output redirection: %s\n"), Stage->OutputPath);
                return FALSE;
            }
        } else if (StageIndex + 1 < Plan->StageCount) {
            if (PipeCreateHandles(&NextReadHandle, &NextWriteHandle) != DF_RETURN_SUCCESS) {
                if (InputHandle != 0) {
                    CloseHandle(InputHandle);
                }
                ConsolePrint(TEXT("Cannot create pipe\n"));
                return FALSE;
            }
            OutputHandle = NextWriteHandle;
        }

        if (Stage->ErrorToOutput) {
            ErrorHandle = OutputHandle;
        } else if (!STRING_EMPTY(Stage->ErrorPath)) {
            UINT OpenFlags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS;
            if (Stage->ErrorAppend) {
                OpenFlags |= FILE_OPEN_SEEK_END;
            } else {
                OpenFlags |= FILE_OPEN_TRUNCATE;
            }

            ErrorHandle = ShellOpenRedirectionHandle(Context, Stage->ErrorPath, OpenFlags);
            if (ErrorHandle == 0) {
                if (InputHandle != 0) {
                    CloseHandle(InputHandle);
                }
                if (OutputHandle != 0) {
                    CloseHandle(OutputHandle);
                }
                if (NextReadHandle != 0) {
                    CloseHandle(NextReadHandle);
                }
                ConsolePrint(TEXT("Cannot open error redirection: %s\n"), Stage->ErrorPath);
                return FALSE;
            }
        }

        MemorySet(&ProcessInfo, 0, sizeof(PROCESS_INFO));
        ProcessInfo.Header.Size = sizeof(PROCESS_INFO);
        ProcessInfo.Header.Version = EXOS_ABI_VERSION;
        ProcessInfo.Header.Flags = 0;
        ProcessInfo.Flags = 0;
        StringCopy(ProcessInfo.CommandLine, QualifiedCommandLine);
        StringCopy(ProcessInfo.WorkFolder, Context->CurrentFolder);
        ProcessInfo.StdOut = OutputHandle;
        ProcessInfo.StdIn = InputHandle;
        ProcessInfo.StdErr = ErrorHandle;
        ProcessInfo.Process = NULL;
        ProcessInfo.Task = NULL;

        if (!CreateProcess(&ProcessInfo) || ProcessInfo.Process == NULL) {
            if (InputHandle != 0) {
                CloseHandle(InputHandle);
            }

            if (OutputHandle != 0) {
                CloseHandle(OutputHandle);
            }

            if (ErrorHandle != 0 && ErrorHandle != OutputHandle) {
                CloseHandle(ErrorHandle);
            }

            if (NextReadHandle != 0) {
                CloseHandle(NextReadHandle);
            }

            ConsolePrint(TEXT("Process launch failed: %s\n"), QualifiedCommandLine);
            return FALSE;
        }

        if (WaitInfo.Count < WAIT_INFO_MAX_OBJECTS) {
            WaitInfo.Objects[WaitInfo.Count] = ProcessInfo.Process;
            WaitInfo.Count++;
        }

        if (InputHandle != 0) {
            CloseHandle(InputHandle);
        }

        if (OutputHandle != 0) {
            CloseHandle(OutputHandle);
        }

        if (ErrorHandle != 0 && ErrorHandle != OutputHandle) {
            CloseHandle(ErrorHandle);
        }

        PreviousReadHandle = NextReadHandle;
    }

    if (PreviousReadHandle != 0) {
        CloseHandle(PreviousReadHandle);
    }

    if (WaitInfo.Count > 0) {
        Wait(&WaitInfo);
    }

    return TRUE;
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
        ERROR(TEXT("Null pointer\n"));
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
    SHELL_PIPELINE_PLAN PipelinePlan;

    if (Context == NULL || Command == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }


    StringCopy(Context->Input.CommandLine, Command);

    if (ShellCommandLineHasRedirectionOrPipe(Command)) {
        if (!ShellParsePipelinePlan(Command, &PipelinePlan)) {
            if (Context->ScriptContext) {
                Context->ScriptContext->ErrorCode = SCRIPT_ERROR_SYNTAX;
                StringCopy(Context->ScriptContext->ErrorMessage, TEXT("Invalid redirection or pipeline syntax"));
            }
            return DF_RETURN_GENERIC;
        }

        if (!ShellRunPipelinePlan(Context, &PipelinePlan)) {
            if (Context->ScriptContext) {
                Context->ScriptContext->ErrorCode = SCRIPT_ERROR_SYNTAX;
                StringCopy(Context->ScriptContext->ErrorMessage, TEXT("Pipeline execution failed"));
            }
            return DF_RETURN_GENERIC;
        }

        return DF_RETURN_SUCCESS;
    }

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
