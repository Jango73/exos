
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


    Shell commands

\************************************************************************/

#include "process/Process-Control.h"
#include "shell/Shell-Commands-Private.h"
#include "utils/KernelPath.h"
#include "utils/SizeFormat.h"

#define DIR_RECURSIVE_STRESS_ENTRY_COUNT 1200

/************************************************************************/

static BOOL ShellCommandLineCompletion(
    const COMMANDLINE_COMPLETION_CONTEXT* CompletionContext, LPSTR Output, U32 OutputSize);
static BOOL ShellFileExists(LPCSTR FileName);
static BOOL ShellBuildBinarySearchPath(LPCSTR FolderPath, LPCSTR LeafName, STR OutPath[MAX_PATH_NAME]);
static BOOL ShellResolveBinarySearchPath(LPCSTR LeafName, STR OutPath[MAX_PATH_NAME]);

/************************************************************************/

static U32 DirStressNext(U32* State) {
    if (State == NULL) return 0;

    *State = (*State * 1664525) + 1013904223;
    return *State;
}

/************************************************************************/

static void DirStressListRecursive(LPSHELLCONTEXT Context, LPCSTR BasePath) {
    U32 Seed = 0x6D2B79F5;
    U32 Index;
    LPPROCESS CurrentProcess = GetCurrentProcess();
    STR Name[MAX_FILE_NAME];
    STR SizeText[32];
    U32 Month;
    U32 Day;
    U32 Year;
    U32 Hour;
    U32 Minute;
    U32 AttrMask;

    UNUSED(Context);

    if (ProcessControlIsInterruptRequested(CurrentProcess)) {
        return;
    }

    ConsolePrint(
        TEXT("Stress listing (temporary): %u synthetic entries under %s\n"), DIR_RECURSIVE_STRESS_ENTRY_COUNT,
        BasePath != NULL ? BasePath : TEXT("/"));

    for (Index = 0; Index < DIR_RECURSIVE_STRESS_ENTRY_COUNT; Index++) {
        U32 RandomA = DirStressNext(&Seed);
        U32 RandomB = DirStressNext(&Seed);
        U32 RandomC = DirStressNext(&Seed);
        U32 EntrySize = 512 + (RandomA % (8 * N_1MB));
        BOOL IsFolder = (RandomB & 0x7) == 0;

        StringPrintFormat(
            Name, TEXT("%s%sentry_%04u_%08x"), BasePath != NULL ? BasePath : TEXT("/"),
            ((Index % 6) == 0) ? TEXT("sub/") : TEXT(""), Index, RandomA);

        SizeFormatBytesText(U64_FromUINT(EntrySize), SizeText);

        Month = 1 + (RandomC % 12);
        Day = 1 + ((RandomC >> 4) % 28);
        Year = 2018 + ((RandomC >> 9) % 9);
        Hour = (RandomC >> 13) % 24;
        Minute = (RandomC >> 18) % 60;
        AttrMask = (RandomC >> 24) & 0xF;

        ConsolePrint(
            TEXT("%s %-12s %u-%u-%u %u:%u "), Name, IsFolder ? TEXT("<Folder>") : SizeText, Day, Month, Year, Hour,
            Minute);
        ConsolePrint(
            TEXT("%s%s%s%s\n"), (AttrMask & 1) ? TEXT("R") : TEXT("-"), (AttrMask & 2) ? TEXT("H") : TEXT("-"),
            (AttrMask & 4) ? TEXT("S") : TEXT("-"), (AttrMask & 8) ? TEXT("X") : TEXT("-"));

        if (ProcessControlIsInterruptRequested(CurrentProcess)) {
            break;
        }
    }
}

/************************************************************************/

void InitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    MemorySet(This, 0, sizeof(SHELLCONTEXT));

    if (!ReservedHeapInit(
            &This->ReservedHeap, GetCurrentProcess(), SHELL_RESERVED_HEAP_INITIAL_SIZE,
            SHELL_RESERVED_HEAP_MAXIMUM_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, TEXT("ShellHeap"))) {
        WARNING(TEXT("Reserved shell heap unavailable, using process heap"));
        AllocatorInitProcess(&This->Allocator, GetCurrentProcess());
    } else {
        ReservedHeapInitAllocator(&This->ReservedHeap, &This->Allocator);
    }

    This->Component = 0;
    This->CommandChar = 0;

    CommandLineEditorInitA(&This->Input.Editor, HISTORY_SIZE, &This->Allocator);
    CommandLineEditorSetCompletionCallback(&This->Input.Editor, ShellCommandLineCompletion, This);
    StringArrayInitA(&This->Options, 8, &This->Allocator);
    PathCompletionInitA(&This->PathCompletion, GetSystemFS(), &This->Allocator);

    for (Index = 0; Index < SHELL_NUM_BUFFERS; Index++) {
        This->Buffer[Index] = (LPSTR)AllocatorAlloc(&This->Allocator, BUFFER_SIZE);
    }

    {
        STR Root[2] = {PATH_SEP, STR_NULL};
        StringCopy(This->CurrentFolder, Root);
    }

    // Initialize persistent script context
    SCRIPT_CALLBACKS Callbacks = {
        ShellScriptOutput, ShellScriptExecuteCommand, ShellScriptResolveVariable, ShellScriptCallFunction, This};
    This->ScriptContext = ScriptCreateContextA(&Callbacks, &This->Allocator);

    if (!ExposeRegisterDefaultScriptHostObjects(This->ScriptContext)) {
        WARNING(TEXT("Failed to register default script host objects"));
    }
}

/************************************************************************/

void DeinitShellContext(LPSHELLCONTEXT This) {
    U32 Index;

    for (Index = 0; Index < SHELL_NUM_BUFFERS; Index++) {
        if (This->Buffer[Index]) AllocatorFree(&This->Allocator, This->Buffer[Index]);
    }

    CommandLineEditorDeinit(&This->Input.Editor);
    StringArrayDeinit(&This->Options);
    PathCompletionDeinit(&This->PathCompletion);

    // Cleanup persistent script context
    if (This->ScriptContext) {
        ScriptDestroyContext(This->ScriptContext);
        This->ScriptContext = NULL;
    }

    ReservedHeapDeinit(&This->ReservedHeap);
}

/************************************************************************/

void ClearOptions(LPSHELLCONTEXT Context) {
    U32 Index;
    for (Index = 0; Index < Context->Options.Count; Index++) {
        if (Context->Options.Items[Index]) AllocatorFree(&Context->Options.Allocator, Context->Options.Items[Index]);
    }
    Context->Options.Count = 0;
}

/************************************************************************/

/*
static void RotateBuffers(LPSHELLCONTEXT This) {
    U32 Index = 0;

    if (This->BufferBase) {
        for (Index = 1; Index < SHELL_NUM_BUFFERS; Index++) {
            MemoryCopy(This->Buffer[Index - 1], This->Buffer[Index],
                       BUFFER_SIZE);
        }
        MemoryCopy(This->Buffer[SHELL_NUM_BUFFERS - 1], This->Input.CommandLine,
                   BUFFER_SIZE);
    }
}
*/

/************************************************************************/

BOOL ShowPrompt(LPSHELLCONTEXT Context) {
    ConsolePrint(TEXT("%s>"), Context->CurrentFolder);
    return TRUE;
}

/************************************************************************/

BOOL ParseNextCommandLineComponent(LPSHELLCONTEXT Context) {
    U32 Quotes = 0;
    U32 d = 0;

    Context->Command[d] = STR_NULL;

    if (Context->Input.CommandLine[Context->CommandChar] == STR_NULL) return TRUE;

    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL &&
           Context->Input.CommandLine[Context->CommandChar] <= STR_SPACE) {
        Context->CommandChar++;
    }

    FOREVER {
        if (Context->Input.CommandLine[Context->CommandChar] == STR_NULL) {
            break;
        } else if (Context->Input.CommandLine[Context->CommandChar] <= STR_SPACE) {
            if (Quotes == 0) {
                Context->CommandChar++;
                break;
            }
        } else if (Context->Input.CommandLine[Context->CommandChar] == STR_QUOTE) {
            Context->CommandChar++;
            if (Quotes == 0)
                Quotes = 1;
            else
                break;
        }

        Context->Command[d] = Context->Input.CommandLine[Context->CommandChar];

        Context->CommandChar++;
        d++;

        // Prevent buffer overflow
        if (d >= 255) {
            break;
        }
    }

    Context->Component++;
    Context->Command[d] = STR_NULL;

    if (Context->Command[0] == STR_MINUS) {
        U32 Offset = 1;
        if (Context->Command[1] == STR_MINUS) Offset = 2;
        if (Context->Command[Offset] != STR_NULL) {
            StringArrayAddUnique(&Context->Options, Context->Command + Offset);
        }
        return ParseNextCommandLineComponent(Context);
    }

    return TRUE;
}

/************************************************************************/

BOOL HasOption(LPSHELLCONTEXT Context, LPCSTR ShortName, LPCSTR LongName) {
    U32 Index;
    LPCSTR Option;
    for (Index = 0; Index < Context->Options.Count; Index++) {
        Option = StringArrayGet(&Context->Options, Index);
        if (ShortName && StringCompareNC(Option, ShortName) == 0) return TRUE;
        if (LongName && StringCompareNC(Option, LongName) == 0) return TRUE;
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Provide path-based completion for the command line editor.
 * @param CompletionContext Details about the token to complete.
 * @param Output Buffer receiving the replacement token.
 * @param OutputSize Size of the output buffer in characters.
 * @return TRUE when a completion was produced, FALSE otherwise.
 */
static BOOL ShellCommandLineCompletion(
    const COMMANDLINE_COMPLETION_CONTEXT* CompletionContext, LPSTR Output, U32 OutputSize) {
    LPSHELLCONTEXT Context;
    STR Token[MAX_PATH_NAME];
    STR Full[MAX_PATH_NAME];
    STR Completed[MAX_PATH_NAME];
    STR Display[MAX_PATH_NAME];
    STR Temp[MAX_PATH_NAME];
    U32 DisplayLength;

    if (CompletionContext == NULL) return FALSE;
    if (Output == NULL) return FALSE;
    if (OutputSize == 0) return FALSE;

    Context = (LPSHELLCONTEXT)CompletionContext->UserData;
    if (Context == NULL) return FALSE;

    if (CompletionContext->TokenLength >= MAX_PATH_NAME) return FALSE;

    StringCopyNum(Token, CompletionContext->Token, CompletionContext->TokenLength);
    Token[CompletionContext->TokenLength] = STR_NULL;

    if (Token[0] == PATH_SEP) {
        StringCopy(Full, Token);
    } else {
        if (!QualifyFileName(Context, Token, Full)) return FALSE;
    }

    if (!PathCompletionNext(&Context->PathCompletion, Full, Completed)) {
        return FALSE;
    }

    if (Token[0] == PATH_SEP) {
        StringCopy(Display, Completed);
    } else {
        U32 FolderLength = StringLength(Context->CurrentFolder);
        StringCopyNum(Temp, Completed, FolderLength);
        Temp[FolderLength] = STR_NULL;
        if (StringCompareNC(Temp, Context->CurrentFolder) == 0) {
            STR* DisplayPtr = Completed + FolderLength;
            if (DisplayPtr[0] == PATH_SEP) DisplayPtr++;
            StringCopy(Display, DisplayPtr);
        } else {
            StringCopy(Display, Completed);
        }
    }

    DisplayLength = StringLength(Display);
    if (DisplayLength >= OutputSize) return FALSE;

    StringCopy(Output, Display);

    return TRUE;
}

/************************************************************************/

BOOL QualifyFileName(LPSHELLCONTEXT Context, LPCSTR RawName, LPSTR FileName) {
    STR Sep[2] = {PATH_SEP, STR_NULL};
    STR Temp[MAX_PATH_NAME];
    LPSTR Ptr;
    LPSTR Token;
    U32 Length;
    STR Save;

    if (RawName[0] == PATH_SEP) {
        StringCopy(Temp, RawName);
    } else {
        StringCopy(Temp, Context->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP) StringConcat(Temp, Sep);
        StringConcat(Temp, TEXT(RawName));
    }

    FileName[0] = PATH_SEP;
    FileName[1] = STR_NULL;

    Ptr = Temp;
    if (Ptr[0] == PATH_SEP) Ptr++;

    while (*Ptr) {
        Token = Ptr;
        while (*Ptr && *Ptr != PATH_SEP) Ptr++;
        Length = Ptr - Token;

        if (Length == 1 && Token[0] == STR_DOT) {
            // Skip current directory component
        } else if (Length == 2 && Token[0] == STR_DOT && Token[1] == STR_DOT) {
            // Remove previous component while preserving root
            LPSTR Slash = StringFindCharR(FileName, PATH_SEP);
            if (Slash) {
                if (Slash != FileName)
                    *Slash = STR_NULL;
                else
                    FileName[1] = STR_NULL;
            }
        } else if (Length > 0) {
            if (StringLength(FileName) > 1) StringConcat(FileName, Sep);
            Save = Token[Length];
            Token[Length] = STR_NULL;
            StringConcat(FileName, Token);
            Token[Length] = Save;
        }

        if (*Ptr == PATH_SEP) Ptr++;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Checks whether a file can be opened for reading.
 * @param FileName Absolute or process-relative file path.
 * @return TRUE when the file exists and can be opened.
 */
static BOOL ShellFileExists(LPCSTR FileName) {
    FILE_OPEN_INFO OpenInfo;
    LPFILE File;

    if (STRING_EMPTY(FileName)) {
        return FALSE;
    }

    OpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
    OpenInfo.Name = (LPSTR)FileName;
    OpenInfo.Flags = FILE_OPEN_READ;

    File = OpenFile(&OpenInfo);
    if (File == NULL) {
        return FALSE;
    }

    CloseFile(File);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Builds a candidate command path from a binary search folder.
 * @param FolderPath Configured binary search folder.
 * @param LeafName Command leaf name.
 * @param OutPath Destination path buffer.
 * @return TRUE on success.
 */
static BOOL ShellBuildBinarySearchPath(LPCSTR FolderPath, LPCSTR LeafName, STR OutPath[MAX_PATH_NAME]) {
    if (STRING_EMPTY(FolderPath) || STRING_EMPTY(LeafName) || OutPath == NULL) {
        return FALSE;
    }

    if (StringLength(FolderPath) + StringLength(LeafName) + 1 >= MAX_PATH_NAME) {
        WARNING(TEXT("Binary search path too long folder=%s leaf=%s"), FolderPath, LeafName);
        return FALSE;
    }

    StringCopy(OutPath, FolderPath);
    if (OutPath[StringLength(OutPath) - 1] != PATH_SEP) {
        StringConcat(OutPath, TEXT("/"));
    }
    StringConcat(OutPath, LeafName);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolves a command leaf name through configured binary folders.
 * @param LeafName Command leaf name without folder components.
 * @param OutPath Destination path buffer.
 * @return TRUE when a configured candidate exists.
 */
static BOOL ShellResolveBinarySearchPath(LPCSTR LeafName, STR OutPath[MAX_PATH_NAME]) {
    UINT Index = 0;

    if (STRING_EMPTY(LeafName) || OutPath == NULL) {
        return FALSE;
    }

    FOREVER {
        STR FolderPath[MAX_PATH_NAME];
        STR CandidatePath[MAX_PATH_NAME];

        if (KernelPathResolveListEntry(KERNEL_PATH_LIST_BINARY, Index, FolderPath, MAX_PATH_NAME) == FALSE) {
            break;
        }

        if (ShellBuildBinarySearchPath(FolderPath, LeafName, CandidatePath) && ShellFileExists(CandidatePath)) {
            StringCopy(OutPath, CandidatePath);
            return TRUE;
        }

        Index++;
    }

    return FALSE;
}

/************************************************************************/

BOOL QualifyCommandLine(LPSHELLCONTEXT Context, LPCSTR RawCommandLine, LPSTR QualifiedCommandLine) {
    U32 Quotes = 0;
    U32 s = 0;  // source index
    U32 d = 0;  // destination index
    STR ExecutableName[MAX_PATH_NAME];
    STR QualifiedPath[MAX_PATH_NAME];
    U32 e = 0;  // executable name index
    BOOL InExecutableName = TRUE;

    QualifiedCommandLine[0] = STR_NULL;

    // Skip leading spaces
    while (RawCommandLine[s] != STR_NULL && RawCommandLine[s] <= STR_SPACE) {
        s++;
    }

    if (RawCommandLine[s] == STR_NULL) return FALSE;

    // Parse the executable name (first word, handling quotes)
    while (RawCommandLine[s] != STR_NULL && InExecutableName) {
        if (RawCommandLine[s] == STR_QUOTE) {
            if (Quotes == 0) {
                Quotes = 1;
            } else {
                Quotes = 0;
                InExecutableName = FALSE;
            }
        } else if (RawCommandLine[s] <= STR_SPACE && Quotes == 0) {
            InExecutableName = FALSE;
        } else {
            if (e < MAX_PATH_NAME - 1) {
                ExecutableName[e++] = RawCommandLine[s];
            }
        }
        if (InExecutableName || RawCommandLine[s] == STR_QUOTE) {
            s++;
        }
    }
    ExecutableName[e] = STR_NULL;

    // Qualify the executable name
    if (!QualifyFileName(Context, ExecutableName, QualifiedPath)) {
        return FALSE;
    }

    if (StringFindChar(ExecutableName, PATH_SEP) == NULL && ShellFileExists(QualifiedPath) == FALSE) {
        ShellResolveBinarySearchPath(ExecutableName, QualifiedPath);
    }

    // Build the qualified command line
    StringCopy(QualifiedCommandLine, QualifiedPath);
    d = StringLength(QualifiedCommandLine);

    // Copy the rest of the command line (arguments)
    if (RawCommandLine[s] != STR_NULL) {
        QualifiedCommandLine[d++] = STR_SPACE;
        while (RawCommandLine[s] != STR_NULL && d < MAX_PATH_NAME - 1) {
            QualifiedCommandLine[d++] = RawCommandLine[s++];
        }
    }
    QualifiedCommandLine[d] = STR_NULL;

    return TRUE;
}

/************************************************************************/

static void ChangeFolder(LPSHELLCONTEXT Context) {
    FILESYSTEM_PATHCHECK Control;
    STR NewPath[MAX_PATH_NAME];

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return;
    }

    if (QualifyFileName(Context, Context->Command, NewPath) == 0) return;

    Control.CurrentFolder[0] = STR_NULL;
    StringCopy(Control.SubFolder, NewPath);

    if (GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Control)) {
        StringCopy(Context->CurrentFolder, NewPath);
    } else {
        ConsolePrint(TEXT("Unknown folder : %s\n"), NewPath);
    }
}

/************************************************************************/

static BOOL MakeFolder(LPSHELLCONTEXT Context, LPSTR QualifiedName) {
    LPFILESYSTEM FileSystem;
    FILE_INFO FileInfo;
    STR FileName[MAX_PATH_NAME];
    UINT Result;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Missing argument\n"));
        return FALSE;
    }

    FileSystem = GetSystemFS();
    if (FileSystem == NULL) return FALSE;

    if (QualifyFileName(Context, Context->Command, FileName)) {
        FileInfo.Size = sizeof(FILE_INFO);
        FileInfo.FileSystem = FileSystem;
        FileInfo.Attributes = MAX_U32;
        FileInfo.Flags = 0;
        StringCopy(FileInfo.Name, FileName);
        Result = FileSystem->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&FileInfo);
        if (QualifiedName != NULL) {
            StringCopy(QualifiedName, FileName);
        }
        return (Result == DF_RETURN_SUCCESS);
    }

    return FALSE;
}

/************************************************************************/

static void ListFile(LPFILE File, U32 Indent) {
    STR Name[MAX_FILE_NAME];
    U32 MaxWidth = Console.Width;
    U32 Length;
    U32 Index;

    //-------------------------------------
    // Eliminate the . and .. files

    if (StringCompare(File->Name, TEXT(".")) == 0) return;
    if (StringCompare(File->Name, TEXT("..")) == 0) return;

    StringCopy(Name, File->Name);

    if (StringLength(Name) > ((MaxWidth - Indent) / 2)) {
        Index = ((MaxWidth - Indent) / 2) - 4;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_DOT;
        Name[Index++] = STR_NULL;
    }

    Length = ((MaxWidth - Indent) / 2) - StringLength(Name);

    // Print name

    for (Index = 0; Index < Indent; Index++) ConsolePrint(TEXT(" "));
    ConsolePrint(Name);
    for (Index = 0; Index < Length; Index++) ConsolePrint(TEXT(" "));

    // Print size

    if (File->Attributes & FS_ATTR_FOLDER) {
        ConsolePrint(TEXT("%12s"), TEXT("<Folder>"));
    } else {
        STR SizeText[32];
        SizeFormatBytesText(U64_Make(File->SizeHigh, File->SizeLow), SizeText);
        ConsolePrint(TEXT("%12s"), SizeText);
    }

    ConsolePrint(
        TEXT(" %d-%d-%d %d:%d "), (I32)File->Creation.Day, (I32)File->Creation.Month, (I32)File->Creation.Year,
        (I32)File->Creation.Hour, (I32)File->Creation.Minute);

    // Print attributes

    if (File->Attributes & FS_ATTR_READONLY)
        ConsolePrint(TEXT("R"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_HIDDEN)
        ConsolePrint(TEXT("H"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_SYSTEM)
        ConsolePrint(TEXT("S"));
    else
        ConsolePrint(TEXT("-"));
    if (File->Attributes & FS_ATTR_EXECUTABLE)
        ConsolePrint(TEXT("X"));
    else
        ConsolePrint(TEXT("-"));

    ConsolePrint(Text_NewLine);
}

/************************************************************************/

void ListDirectory(LPSHELLCONTEXT Context, LPCSTR Base, U32 Indent, BOOL Pause, BOOL Recurse, U32* NumListed) {
    FILE_INFO Find;
    LPFILESYSTEM FileSystem;
    LPFILE File;
    LPPROCESS CurrentProcess = GetCurrentProcess();
    FILESYSTEM_PATHCHECK PathCheck;
    STR DiskName[MAX_FILE_NAME];
    LPCSTR Reason = TEXT("unknown");
    STR Pattern[MAX_PATH_NAME];
    STR Sep[2] = {PATH_SEP, STR_NULL};

    UNUSED(Context);
    if (ProcessControlIsInterruptRequested(CurrentProcess)) {
        return;
    }

    FileSystem = GetSystemFS();

    Find.Size = sizeof(FILE_INFO);
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;
    Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    StringCopy(Pattern, Base);
    if (Pattern[StringLength(Pattern) - 1] != PATH_SEP) StringConcat(Pattern, Sep);
    StringConcat(Pattern, TEXT("*"));
    StringCopy(Find.Name, Pattern);

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (File == NULL) {
        StringCopy(Find.Name, Base);
        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        if (File == NULL) {
            StringCopy(DiskName, Base);
            if (Base[0] == PATH_SEP && Base[1] == 'f' && Base[2] == 's' && Base[3] == PATH_SEP) {
                UINT ReadIndex = 4;
                UINT WriteIndex = 0;
                while (Base[ReadIndex] != STR_NULL && Base[ReadIndex] != PATH_SEP && WriteIndex < MAX_FILE_NAME - 1) {
                    DiskName[WriteIndex++] = Base[ReadIndex++];
                }
                DiskName[WriteIndex] = STR_NULL;
            }

            PathCheck.CurrentFolder[0] = STR_NULL;
            StringCopy(PathCheck.SubFolder, Base);
            if (FileSystem->Driver->Command(DF_FS_PATHEXISTS, (UINT)&PathCheck)) {
                Reason = TEXT("file system driver refused open/list");
            } else {
                Reason = TEXT("path not found");
            }
            ConsolePrint(TEXT("Unable to read on volume %s, reason : %s\n"), DiskName, Reason);
            WARNING(
                TEXT("Unable to read on volume %s, reason : %s (path=%s fs=%s driver=%s)"), DiskName, Reason, Base,
                FileSystem->Name, FileSystem->Driver->Product);
            return;
        }
        ListFile(File, Indent);
        FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
        return;
    }

    do {
        if (ProcessControlIsInterruptRequested(CurrentProcess)) {
            break;
        }

        ListFile(File, Indent);
        if (Recurse && (File->Attributes & FS_ATTR_FOLDER)) {
            if (StringCompare(File->Name, TEXT(".")) != 0 && StringCompare(File->Name, TEXT("..")) != 0) {
                STR NewBase[MAX_PATH_NAME];
                StringCopy(NewBase, Base);
                if (NewBase[StringLength(NewBase) - 1] != PATH_SEP) StringConcat(NewBase, Sep);
                StringConcat(NewBase, File->Name);
                ListDirectory(Context, NewBase, Indent + 2, Pause, Recurse, NumListed);
                if (ProcessControlIsInterruptRequested(CurrentProcess)) {
                    break;
                }
            }
        }
        if (Pause) {
            (*NumListed)++;
            if (*NumListed >= Console.Height - 2) {
                *NumListed = 0;
                WaitKey();
            }
        }
    } while (FileSystem->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RETURN_SUCCESS);

    FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
}

/***************************************************************************/

U32 CMD_commands(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    U32 Index;

    for (Index = 0; COMMANDS[Index].Command != NULL; Index++) {
        ConsolePrint(
            TEXT("%s (%s) %s - %s\n"), COMMANDS[Index].Name, COMMANDS[Index].AltName, COMMANDS[Index].Usage,
            COMMANDS[Index].Description);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_clearScreen(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ClearConsole();

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_consoleMode(LPSHELLCONTEXT Context) {
    GRAPHICS_MODE_INFO Info;
    U32 Columns;
    U32 Rows;
    U32 Result;
    U32 ModeCount;

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: consoleMode Columns Rows | consoleMode list\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("list")) == 0) {
        CONSOLE_MODE_INFO ModeInfo;
        ModeCount = DoSystemCall(SYSCALL_ConsoleGetModeCount, SYSCALL_PARAM(0));
        ConsolePrint(TEXT("VGA text modes:\n"));
        for (U32 Index = 0; Index < ModeCount; Index++) {
            ModeInfo.Header.Size = sizeof ModeInfo;
            ModeInfo.Header.Version = EXOS_ABI_VERSION;
            ModeInfo.Header.Flags = 0;
            ModeInfo.Index = Index;
            if (DoSystemCall(SYSCALL_ConsoleGetModeInfo, SYSCALL_PARAM(&ModeInfo)) != DF_RETURN_SUCCESS) {
                continue;
            }
            ConsolePrint(
                TEXT("  %u: %ux%u (char height %u)\n"), Index, ModeInfo.Columns, ModeInfo.Rows, ModeInfo.CharHeight);
        }
        return DF_RETURN_SUCCESS;
    }

    Columns = StringToU32(Context->Command);

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: consoleMode Columns Rows | consoleMode list\n"));
        return DF_RETURN_SUCCESS;
    }
    Rows = StringToU32(Context->Command);

    if (Columns == 0 || Rows == 0) {
        ConsolePrint(TEXT("Invalid console size\n"));
        return DF_RETURN_SUCCESS;
    }

    Info.Header.Size = sizeof Info;
    Info.Header.Version = EXOS_ABI_VERSION;
    Info.Header.Flags = 0;
    Info.ModeIndex = INFINITY;
    Info.Width = Columns;
    Info.Height = Rows;
    Info.BitsPerPixel = 0;

    Result = DoSystemCall(SYSCALL_ConsoleSetMode, SYSCALL_PARAM(&Info));

    if (Result != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Console mode %ux%u unavailable (err=%u)\n"), Columns, Rows, Result);
    } else {
        ConsolePrint(TEXT("Console mode set to %ux%u\n"), Columns, Rows);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Update or display the active keyboard layout.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
U32 CMD_keyboard(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Keyboard layout: %s\n"), GetKeyboardCode());
        return DF_RETURN_SUCCESS;
    }

    if (HasOption(Context, TEXT("l"), TEXT("layout"))) {
        SelectKeyboard(Context->Command);
        ConsolePrint(TEXT("Keyboard layout set to %s\n"), GetKeyboardCode());
        TEST(TEXT("keyboard : OK"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Usage: keyboard --layout Code\n"));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_pause(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Pause is %s\n"), ConsoleGetPagingEnabled() ? TEXT("on") : TEXT("off"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("on")) == 0) {
        ConsoleSetPagingEnabled(TRUE);
        ConsolePrint(TEXT("Pause on\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("off")) == 0) {
        ConsoleSetPagingEnabled(FALSE);
        ConsolePrint(TEXT("Pause off\n"));
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Usage: pause on|off\n"));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_listFolder(LPSHELLCONTEXT Context) {
    STR Target[MAX_PATH_NAME];
    STR Base[MAX_PATH_NAME];
    LPFILESYSTEM FileSystem = NULL;
    LPPROCESS CurrentProcess = GetCurrentProcess();
    BOOL Pause;
    BOOL Recurse;
    BOOL Stress;
    U32 NumListed = 0;

    Target[0] = STR_NULL;

    // Parse all command line components (including options) first
    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command)) {
        QualifyFileName(Context, Context->Command, Target);
    }

    // Continue parsing any remaining components to capture all options
    while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
        ParseNextCommandLineComponent(Context);
    }

    // Now check for options after all parsing is complete
    Pause = HasOption(Context, TEXT("p"), TEXT("pause"));
    Recurse = HasOption(Context, TEXT("r"), TEXT("recursive"));
    Stress = HasOption(Context, TEXT("s"), TEXT("stress"));

    if (Stress) {
        if (StringLength(Target) == 0) {
            StringCopy(Base, Context->CurrentFolder);
        } else {
            StringCopy(Base, Target);
        }
        ProcessControlConsumeInterrupt(CurrentProcess);
        DirStressListRecursive(Context, Base);
        if (ProcessControlCheckpoint(CurrentProcess)) {
            ConsolePrint(TEXT("Command interrupted\n"));
        }
        return DF_RETURN_SUCCESS;
    }

    FileSystem = GetSystemFS();

    if (FileSystem == NULL || FileSystem->Driver == NULL) {
        ConsolePrint(TEXT("No file system mounted !\n"));
        TEST(TEXT("listFolder : KO (No file system mounted)"));
        return DF_RETURN_SUCCESS;
    }

    if (StringLength(Target) == 0) {
        StringCopy(Base, Context->CurrentFolder);
    } else {
        StringCopy(Base, Target);
    }

    ProcessControlConsumeInterrupt(CurrentProcess);
    ListDirectory(Context, Base, 0, Pause, Recurse, &NumListed);
    if (ProcessControlCheckpoint(CurrentProcess)) {
        ConsolePrint(TEXT("Command interrupted\n"));
    }

    TEST(TEXT("listFolder : OK"));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_changeFolder(LPSHELLCONTEXT Context) {
    ChangeFolder(Context);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_makeFolder(LPSHELLCONTEXT Context) {
    STR FolderName[MAX_PATH_NAME];

    FolderName[0] = STR_NULL;

    if (MakeFolder(Context, FolderName)) {
        TEST(TEXT("md %s : OK"), FolderName);
    } else {
        TEST(TEXT("md %s : KO"), FolderName);
    }

    return DF_RETURN_SUCCESS;
}
