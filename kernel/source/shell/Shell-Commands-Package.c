
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

#include "shell/Shell-Commands-Private.h"
#include "package/PackageFS.h"
#include "package/PackageManifest.h"
#include "package/PackageNamespace.h"
#include "utils/KernelPath.h"
#include "utils/SizeFormat.h"

static void ShellSkipSpaces(LPCSTR Text, UINT* InOutIndex);
static BOOL ShellParseRawToken(LPCSTR Text, UINT* InOutIndex, STR OutToken[MAX_PATH_NAME]);
static BOOL ShellResolvePackageFilePath(LPSHELLCONTEXT Context,
                                        LPCSTR PackageName,
                                        STR OutQualifiedPackage[MAX_PATH_NAME]);
static U32 ShellPackageList(LPSHELLCONTEXT Context, LPCSTR PackageNameOrPath);
static U32 ShellPackageAdd(LPSHELLCONTEXT Context, LPCSTR PackageNameOrPath);
static BOOL ShellIsPackageFileName(LPCSTR FileName);
static BOOL ShellLaunchPackage(LPSHELLCONTEXT Context,
                               LPCSTR QualifiedCommandLine,
                               LPCSTR QualifiedCommand,
                               LPCSTR PreferredCommandName,
                               LPCSTR PreferredCommandArguments,
                               BOOL Background);

/***************************************************************************/

/**
 * @brief Print one script return value as plain output.
 * @param ReturnType Return value type.
 * @param ReturnValue Return value storage.
 */
static void ShellPrintScriptReturnValue(SCRIPT_VAR_TYPE ReturnType, SCRIPT_VAR_VALUE ReturnValue) {
    STR ReturnText[64];

    if (ReturnType == SCRIPT_VAR_STRING) {
        StringCopy(ReturnText, ReturnValue.String ? ReturnValue.String : TEXT(""));
    } else if (ReturnType == SCRIPT_VAR_INTEGER) {
        StringPrintFormat(ReturnText, TEXT("%d"), ReturnValue.Integer);
    } else if (ReturnType == SCRIPT_VAR_FLOAT) {
        StringPrintFormat(ReturnText, TEXT("%f"), ReturnValue.Float);
    } else if (ReturnType == SCRIPT_VAR_OBJECT) {
        StringCopy(ReturnText, TEXT("[object]"));
    } else {
        StringCopy(ReturnText, TEXT("unsupported"));
    }

    ConsolePrint(TEXT("%s\n"), ReturnText);
    TEST(TEXT("%s"), ReturnText);
}

/***************************************************************************/

/**
 * @brief Run one script file and print its returned value when present.
 * @param Context Shell context.
 * @param ScriptFileName Qualified script file name.
 * @return TRUE when the script completed successfully, FALSE otherwise.
 */
BOOL RunScriptFile(LPSHELLCONTEXT Context, LPCSTR ScriptFileName) {
    FILE_OPEN_INFO FileOpenInfo;
    FILE_OPERATION FileOperation;
    HANDLE Handle = NULL;
    U32 FileSize = 0;
    U32 BytesRead = 0;
    U8* Buffer = NULL;
    SCRIPT_VAR_TYPE ReturnType;
    SCRIPT_VAR_VALUE ReturnValue;
    SCRIPT_ERROR Error = SCRIPT_OK;
    BOOL Success = FALSE;

    if (Context == NULL || ScriptFileName == NULL || Context->ScriptContext == NULL) {
        return FALSE;
    }

    FileOpenInfo.Header.Size = sizeof(FILE_OPEN_INFO);
    FileOpenInfo.Header.Version = EXOS_ABI_VERSION;
    FileOpenInfo.Header.Flags = 0;
    FileOpenInfo.Name = ScriptFileName;
    FileOpenInfo.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;

    Handle = DoSystemCall(SYSCALL_OpenFile, SYSCALL_PARAM(&FileOpenInfo));

    if (Handle == NULL) {
        ConsolePrint(TEXT("Unable to open script file: %s\n"), ScriptFileName);
        goto Out;
    }

    FileSize = DoSystemCall(SYSCALL_GetFileSize, SYSCALL_PARAM(Handle));
    if (FileSize == 0) {
        ConsolePrint(TEXT("Empty script file: %s\n"), ScriptFileName);
        goto Out;
    }

    Buffer = (U8*)AllocatorAlloc(&Context->Allocator, FileSize + 1);
    if (Buffer == NULL) {
        STR SizeText[32];
        SizeFormatBytesText(U64_FromUINT(FileSize + 1), SizeText);
        ConsolePrint(TEXT("Unable to allocate script buffer: %s\n"), SizeText);
        goto Out;
    }

    FileOperation.Header.Size = sizeof(FILE_OPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = Handle;
    FileOperation.NumBytes = FileSize;
    FileOperation.Buffer = Buffer;

    BytesRead = DoSystemCall(SYSCALL_ReadFile, SYSCALL_PARAM(&FileOperation));
    if (BytesRead != FileSize) {
        ConsolePrint(TEXT("Failed to read script file: %s\n"), ScriptFileName);
        goto Out;
    }

    Buffer[FileSize] = STR_NULL;

    Error = ScriptExecute(Context->ScriptContext, (LPCSTR)Buffer);
    if (Error != SCRIPT_OK) {
        ConsolePrint(TEXT("Error: %s\n"), ScriptGetErrorMessage(Context->ScriptContext));
        goto Out;
    }

    if (ScriptGetReturnValue(Context->ScriptContext, &ReturnType, &ReturnValue)) {
        ShellPrintScriptReturnValue(ReturnType, ReturnValue);
    }

    Success = TRUE;

Out:
    if (Buffer != NULL) {
        AllocatorFree(&Context->Allocator, Buffer);
    }

    if (Handle != NULL) {
        DoSystemCall(SYSCALL_DeleteObject, SYSCALL_PARAM(Handle));
    }

    return Success;
}

/***************************************************************************/

/**
 * @brief Run one embedded E0 script from a static kernel string.
 * @param Context Shell context.
 * @param ScriptText E0 source text.
 * @return `DF_RETURN_*` status code. When the script returns an integer,
 * that integer becomes the command status.
 */
UINT RunEmbeddedScript(LPSHELLCONTEXT Context, LPCSTR ScriptText) {
    SCRIPT_VAR_TYPE ReturnType;
    SCRIPT_VAR_VALUE ReturnValue;
    SCRIPT_ERROR Error = SCRIPT_OK;

    if (Context == NULL || ScriptText == NULL || Context->ScriptContext == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Error = ScriptExecute(Context->ScriptContext, ScriptText);
    if (Error != SCRIPT_OK) {
        return DF_RETURN_GENERIC;
    }

    if (ScriptGetReturnValue(Context->ScriptContext, &ReturnType, &ReturnValue)) {
        if (ReturnType == SCRIPT_VAR_INTEGER) {
            return (UINT)ReturnValue.Integer;
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Launch an executable specified on the command line.
 *
 * @param Context Shell context containing parsed arguments.
 */
U32 CMD_run(LPSHELLCONTEXT Context) {
    STR TargetName[MAX_PATH_NAME];
    BOOL Background = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command)) {
        StringCopy(TargetName, Context->Command);

        while (Context->Input.CommandLine[Context->CommandChar] != STR_NULL) {
            ParseNextCommandLineComponent(Context);
        }

        Background = HasOption(Context, TEXT("b"), TEXT("background"));
        SpawnExecutable(Context, TargetName, Background);
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Run one package by name with optional manifest command selector.
 *
 * Syntax: package run <package-name> [command-name] [args...]
 *
 * @param Context Shell context containing command line text.
 * @return Command status.
 */
U32 CMD_package(LPSHELLCONTEXT Context) {
    UINT Index;
    STR SubCommand[MAX_PATH_NAME];
    STR PackageName[MAX_PATH_NAME];
    STR UsageText[MAX_PATH_NAME];

    if (Context == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Index = Context->CommandChar;
    if (!ShellParseRawToken(Context->Input.CommandLine, &Index, SubCommand)) {
        ConsolePrint(TEXT("Usage: package run|list|add ...\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!ShellParseRawToken(Context->Input.CommandLine, &Index, PackageName)) {
        if (StringCompareNC(SubCommand, TEXT("run")) == 0) {
            StringCopy(UsageText, TEXT("Usage: package run <package-name> [command-name] [args...]\n"));
        } else if (StringCompareNC(SubCommand, TEXT("list")) == 0) {
            StringCopy(UsageText, TEXT("Usage: package list <package-name|path.epk>\n"));
        } else if (StringCompareNC(SubCommand, TEXT("add")) == 0) {
            StringCopy(UsageText, TEXT("Usage: package add <package-name|path.epk>\n"));
        } else {
            StringCopy(UsageText, TEXT("Usage: package run|list|add ...\n"));
        }
        ConsolePrint(UsageText);
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(SubCommand, TEXT("run")) == 0) {
        UINT RemainderIndex;
        STR FirstArgumentToken[MAX_PATH_NAME];
        STR QualifiedPackage[MAX_PATH_NAME];
        STR QualifiedCommandLine[MAX_PATH_NAME];
        LPCSTR RemainderArguments;
        LPCSTR CommandArguments;
        BOOL HasFirstArgumentToken;

        if (!ShellResolvePackageFilePath(Context, PackageName, QualifiedPackage)) {
            ConsolePrint(TEXT("Invalid package name: %s\n"), PackageName);
            return DF_RETURN_SUCCESS;
        }

        StringCopy(QualifiedCommandLine, QualifiedPackage);
        RemainderArguments = Context->Input.CommandLine + Index;
        while (*RemainderArguments != STR_NULL && *RemainderArguments <= STR_SPACE) {
            RemainderArguments++;
        }

        if (!STRING_EMPTY(RemainderArguments)) {
            StringConcat(QualifiedCommandLine, TEXT(" "));
            StringConcat(QualifiedCommandLine, RemainderArguments);
        }

        RemainderIndex = 0;
        HasFirstArgumentToken = ShellParseRawToken(RemainderArguments, &RemainderIndex, FirstArgumentToken);
        CommandArguments = RemainderArguments + RemainderIndex;
        while (*CommandArguments != STR_NULL && *CommandArguments <= STR_SPACE) {
            CommandArguments++;
        }

        if (!ShellLaunchPackage(Context,
                QualifiedCommandLine,
                QualifiedPackage,
                HasFirstArgumentToken ? FirstArgumentToken : NULL,
                HasFirstArgumentToken ? CommandArguments : NULL,
                FALSE)) {
            ConsolePrint(TEXT("Package run failed: %s\n"), QualifiedPackage);
        }
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(SubCommand, TEXT("list")) == 0) {
        return ShellPackageList(Context, PackageName);
    }

    if (StringCompareNC(SubCommand, TEXT("add")) == 0) {
        return ShellPackageAdd(Context, PackageName);
    }

    ConsolePrint(TEXT("Usage: package run|list|add ...\n"));
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_exit(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

static void ShellSkipSpaces(LPCSTR Text, UINT* InOutIndex) {
    if (Text == NULL || InOutIndex == NULL) {
        return;
    }

    while (Text[*InOutIndex] != STR_NULL && Text[*InOutIndex] <= STR_SPACE) {
        (*InOutIndex)++;
    }
}

/***************************************************************************/

/**
 * @brief Parse one raw token without option stripping.
 * @param Text Source command line.
 * @param InOutIndex Index cursor to advance.
 * @param OutToken Destination token buffer.
 * @return TRUE when one token is parsed.
 */
static BOOL ShellParseRawToken(LPCSTR Text, UINT* InOutIndex, STR OutToken[MAX_PATH_NAME]) {
    UINT SourceIndex = 0;
    BOOL InQuotes = FALSE;

    if (Text == NULL || InOutIndex == NULL || OutToken == NULL) {
        return FALSE;
    }

    OutToken[0] = STR_NULL;
    ShellSkipSpaces(Text, InOutIndex);
    if (Text[*InOutIndex] == STR_NULL) {
        return FALSE;
    }

    if (Text[*InOutIndex] == STR_QUOTE) {
        InQuotes = TRUE;
        (*InOutIndex)++;
    }

    while (Text[*InOutIndex] != STR_NULL) {
        STR Character = Text[*InOutIndex];

        if (InQuotes) {
            if (Character == STR_QUOTE) {
                (*InOutIndex)++;
                break;
            }
        } else if (Character <= STR_SPACE) {
            break;
        }

        if (SourceIndex + 1 < MAX_PATH_NAME) {
            OutToken[SourceIndex++] = Character;
        }

        (*InOutIndex)++;
    }

    OutToken[SourceIndex] = STR_NULL;
    ShellSkipSpaces(Text, InOutIndex);
    return (SourceIndex > 0);
}

/***************************************************************************/

/**
 * @brief Resolve one package run token to an absolute package file path.
 * @param Context Shell context.
 * @param PackageName Package name or path token.
 * @param OutQualifiedPackage Receives absolute package file path.
 * @return TRUE on success.
 */
static BOOL ShellResolvePackageFilePath(LPSHELLCONTEXT Context,
                                        LPCSTR PackageName,
                                        STR OutQualifiedPackage[MAX_PATH_NAME]) {
    STR AppsRoot[MAX_PATH_NAME];
    STR LocalToken[MAX_PATH_NAME];

    if (Context == NULL || STRING_EMPTY(PackageName) || OutQualifiedPackage == NULL) {
        return FALSE;
    }

    if (!KernelPathResolve(
            KERNEL_PATH_KEY_SYSTEM_APPS_ROOT,
            KERNEL_PATH_DEFAULT_SYSTEM_APPS_ROOT,
            AppsRoot,
            MAX_PATH_NAME)) {
        return FALSE;
    }

    if (StringFindChar(PackageName, PATH_SEP) != NULL) {
        if (!QualifyFileName(Context, PackageName, OutQualifiedPackage)) {
            return FALSE;
        }

        if (!ShellIsPackageFileName(OutQualifiedPackage)) {
            if (StringLength(OutQualifiedPackage) + StringLength(KERNEL_FILE_EXTENSION_PACKAGE) >= MAX_PATH_NAME) {
                return FALSE;
            }
            StringConcat(OutQualifiedPackage, KERNEL_FILE_EXTENSION_PACKAGE);
        }
        return TRUE;
    }

    StringCopy(LocalToken, PackageName);
    if (ShellIsPackageFileName(LocalToken)) {
        return KernelPathBuildFile(
            KERNEL_PATH_KEY_SYSTEM_APPS_ROOT,
            KERNEL_PATH_DEFAULT_SYSTEM_APPS_ROOT,
            LocalToken,
            NULL,
            OutQualifiedPackage,
            MAX_PATH_NAME);
    }

    return KernelPathBuildFile(
        KERNEL_PATH_KEY_SYSTEM_APPS_ROOT,
        KERNEL_PATH_DEFAULT_SYSTEM_APPS_ROOT,
        LocalToken,
        KERNEL_FILE_EXTENSION_PACKAGE,
        OutQualifiedPackage,
        MAX_PATH_NAME);
}

/***************************************************************************/

/**
 * @brief List internal content of one package file.
 * @param Context Shell context.
 * @param PackageNameOrPath Package name or path token.
 * @return Command status.
 */
static U32 ShellPackageList(LPSHELLCONTEXT Context, LPCSTR PackageNameOrPath) {
    UINT PackageSize = 0;
    U8* PackageBytes = NULL;
    PACKAGE_MANIFEST Manifest;
    U32 Status;
    LPFILESYSTEM PackageFileSystem = NULL;
    STR QualifiedPackage[MAX_PATH_NAME];
    STR MountName[MAX_FILE_NAME];
    STR PrivatePackageAlias[MAX_PATH_NAME];
    U32 NumListed = 0;
    BOOL CleanupBound = FALSE;
    BOOL Success = FALSE;

    if (Context == NULL || STRING_EMPTY(PackageNameOrPath)) {
        ConsolePrint(TEXT("Usage: package list <package-name|path.epk>\n"));
        TEST(TEXT("package list : KO"));
        return DF_RETURN_SUCCESS;
    }

    if (!ShellResolvePackageFilePath(Context, PackageNameOrPath, QualifiedPackage)) {
        ConsolePrint(TEXT("Invalid package target: %s\n"), PackageNameOrPath);
        TEST(TEXT("package list %s : KO"), PackageNameOrPath);
        return DF_RETURN_SUCCESS;
    }

    PackageBytes = (U8*)FileReadAll(QualifiedPackage, &PackageSize);
    if (PackageBytes == NULL || PackageSize == 0) {
        ConsolePrint(TEXT("Cannot read package file: %s\n"), QualifiedPackage);
        TEST(TEXT("package list %s : KO"), QualifiedPackage);
        return DF_RETURN_SUCCESS;
    }

    Status = PackageManifestParseFromPackageBuffer(PackageBytes, PackageSize, &Manifest);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        ConsolePrint(TEXT("Package manifest error: %s (%u)\n"),
            PackageManifestStatusToString(Status),
            Status);
        TEST(TEXT("package list %s : KO"), QualifiedPackage);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    StringPrintFormat(MountName, TEXT("pkg-list-%s-%u"), Manifest.Name, GetSystemTime());
    Status = PackageFSMountFromBuffer(PackageBytes, PackageSize, MountName, NULL, &PackageFileSystem);
    if (Status != DF_RETURN_SUCCESS || PackageFileSystem == NULL) {
        ConsolePrint(TEXT("Package mount failed: %u\n"), Status);
        TEST(TEXT("package list %s : KO"), QualifiedPackage);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    if (!PackageNamespaceBindCurrentProcessPackageView(PackageFileSystem, Manifest.Name)) {
        ConsolePrint(TEXT("Package namespace bind failed\n"));
        TEST(TEXT("package list %s : KO"), QualifiedPackage);
        PackageFSUnmount(PackageFileSystem);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }
    CleanupBound = TRUE;

    if (!KernelPathResolve(
            KERNEL_PATH_KEY_PRIVATE_PACKAGE_ALIAS,
            KERNEL_PATH_DEFAULT_PRIVATE_PACKAGE_ALIAS,
            PrivatePackageAlias,
            MAX_PATH_NAME)) {
        ConsolePrint(TEXT("Package path resolution failed\n"));
        goto Exit;
    }

    ConsolePrint(TEXT("Package: %s (%s) arch=%s kernel_api=%s\n"),
        Manifest.Name,
        Manifest.Version,
        Manifest.Arch,
        Manifest.KernelApi);
    ConsolePrint(TEXT("Default entry: %s\n"), Manifest.Entry);
    if (Manifest.CommandCount > 0) {
        ConsolePrint(TEXT("Commands:\n"));
        for (UINT Index = 0; Index < Manifest.CommandCount; Index++) {
            ConsolePrint(TEXT("  %s -> %s\n"),
                Manifest.Commands[Index].Name,
                Manifest.Commands[Index].Target);
        }
    }
    ConsolePrint(TEXT("Content:\n"));
    ListDirectory(Context, PrivatePackageAlias, 0, FALSE, TRUE, &NumListed);
    Success = TRUE;

Exit:
    if (CleanupBound) {
        PackageNamespaceUnbindCurrentProcessPackageView();
    }
    if (PackageFileSystem != NULL) {
        PackageFSUnmount(PackageFileSystem);
    }
    PackageManifestRelease(&Manifest);
    KernelHeapFree(PackageBytes);
    if (Success) {
        TEST(TEXT("package list %s : OK"), QualifiedPackage);
    } else {
        TEST(TEXT("package list %s : KO"), QualifiedPackage);
    }
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Add one package file into configured system applications folder.
 * @param Context Shell context.
 * @param PackageNameOrPath Source package name or path token.
 * @return Command status.
 */
static U32 ShellPackageAdd(LPSHELLCONTEXT Context, LPCSTR PackageNameOrPath) {
    UINT PackageSize = 0;
    U8* PackageBytes = NULL;
    PACKAGE_MANIFEST Manifest;
    U32 Status;
    STR SourcePackagePath[MAX_PATH_NAME];
    STR DestinationPackagePath[MAX_PATH_NAME];
    BOOL Success = FALSE;

    if (Context == NULL || STRING_EMPTY(PackageNameOrPath)) {
        ConsolePrint(TEXT("Usage: package add <package-name|path.epk>\n"));
        TEST(TEXT("package add : KO"));
        return DF_RETURN_SUCCESS;
    }

    if (!ShellResolvePackageFilePath(Context, PackageNameOrPath, SourcePackagePath)) {
        ConsolePrint(TEXT("Invalid package target: %s\n"), PackageNameOrPath);
        TEST(TEXT("package add %s : KO"), PackageNameOrPath);
        return DF_RETURN_SUCCESS;
    }

    PackageBytes = (U8*)FileReadAll(SourcePackagePath, &PackageSize);
    if (PackageBytes == NULL || PackageSize == 0) {
        ConsolePrint(TEXT("Cannot read package file: %s\n"), SourcePackagePath);
        TEST(TEXT("package add %s : KO"), SourcePackagePath);
        return DF_RETURN_SUCCESS;
    }

    Status = PackageManifestParseFromPackageBuffer(PackageBytes, PackageSize, &Manifest);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        ConsolePrint(TEXT("Package manifest error: %s (%u)\n"),
            PackageManifestStatusToString(Status),
            Status);
        TEST(TEXT("package add %s : KO"), SourcePackagePath);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    if (!KernelPathBuildFile(
            KERNEL_PATH_KEY_SYSTEM_APPS_ROOT,
            KERNEL_PATH_DEFAULT_SYSTEM_APPS_ROOT,
            Manifest.Name,
            KERNEL_FILE_EXTENSION_PACKAGE,
            DestinationPackagePath,
            MAX_PATH_NAME)) {
        ConsolePrint(TEXT("Destination path build failed for package %s\n"), Manifest.Name);
        TEST(TEXT("package add %s : KO"), SourcePackagePath);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    if (StringCompare(SourcePackagePath, DestinationPackagePath) == 0) {
        ConsolePrint(TEXT("Package already installed: %s\n"), DestinationPackagePath);
        TEST(TEXT("package add %s : OK"), DestinationPackagePath);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    if (FileWriteAll(DestinationPackagePath, PackageBytes, PackageSize) != PackageSize) {
        ConsolePrint(TEXT("Package add failed while writing: %s\n"), DestinationPackagePath);
        TEST(TEXT("package add %s : KO"), SourcePackagePath);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return DF_RETURN_SUCCESS;
    }

    ConsolePrint(TEXT("Package added: %s -> %s\n"), SourcePackagePath, DestinationPackagePath);
    Success = TRUE;

    PackageManifestRelease(&Manifest);
    KernelHeapFree(PackageBytes);
    if (Success) {
        TEST(TEXT("package add %s : OK"), DestinationPackagePath);
    } else {
        TEST(TEXT("package add %s : KO"), SourcePackagePath);
    }
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Check whether one command path targets an EPK package file.
 * @param FileName Qualified executable or package file path.
 * @return TRUE when extension is ".epk".
 */
static BOOL ShellIsPackageFileName(LPCSTR FileName) {
    UINT Length;

    if (STRING_EMPTY(FileName)) {
        return FALSE;
    }

    Length = StringLength(FileName);
    if (Length < 4) {
        return FALSE;
    }

    return (StringCompareNC(FileName + Length - 4, TEXT(".epk")) == 0);
}

/***************************************************************************/

/**
 * @brief Build launch command line from package entry and trailing arguments.
 * @param EntryPath Package-relative executable path from manifest.
 * @param Arguments Optional trailing arguments from original command.
 * @param OutCommandLine Receives full launch command line.
 * @return TRUE on success.
 */
static BOOL ShellBuildPackageLaunchCommandLine(LPCSTR EntryPath, LPCSTR Arguments, STR OutCommandLine[MAX_PATH_NAME]) {
    STR Prefix[MAX_PATH_NAME];

    if (STRING_EMPTY(EntryPath) || OutCommandLine == NULL) {
        return FALSE;
    }

    StringCopy(Prefix, TEXT("/package"));
    if (EntryPath[0] != PATH_SEP) {
        StringConcat(Prefix, TEXT("/"));
    }
    StringConcat(Prefix, EntryPath);

    StringCopy(OutCommandLine, Prefix);

    if (!STRING_EMPTY(Arguments)) {
        StringConcat(OutCommandLine, TEXT(" "));
        StringConcat(OutCommandLine, Arguments);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepare one process launch from qualified command line.
 * @param Context Shell context.
 * @param QualifiedCommandLine Absolute command line.
 * @param Background TRUE to launch in background.
 * @param OutProcess Optional process pointer when background launch succeeds.
 * @return TRUE on success.
 */
static BOOL ShellLaunchCommandLine(LPSHELLCONTEXT Context,
                                   LPCSTR QualifiedCommandLine,
                                   BOOL Background,
                                   LPPROCESS* OutProcess) {
    if (Context == NULL || STRING_EMPTY(QualifiedCommandLine)) {
        return FALSE;
    }

    if (Background) {
        PROCESS_INFO ProcessInfo;

        MemorySet(&ProcessInfo, 0, sizeof(ProcessInfo));
        ProcessInfo.Header.Size = sizeof(PROCESS_INFO);
        ProcessInfo.Header.Version = EXOS_ABI_VERSION;
        ProcessInfo.Header.Flags = 0;
        ProcessInfo.Flags = 0;
        StringCopy(ProcessInfo.CommandLine, QualifiedCommandLine);
        StringCopy(ProcessInfo.WorkFolder, Context->CurrentFolder);
        ProcessInfo.StdOut = NULL;
        ProcessInfo.StdIn = NULL;
        ProcessInfo.StdErr = NULL;
        ProcessInfo.Process = NULL;
        ProcessInfo.Task = NULL;

        if (!CreateProcess(&ProcessInfo)) {
            return FALSE;
        }

        if (OutProcess != NULL) {
            *OutProcess = (LPPROCESS)ProcessInfo.Process;
        }
        return TRUE;
    }

    return (Spawn(QualifiedCommandLine, Context->CurrentFolder) != MAX_UINT);
}

/***************************************************************************/

/**
 * @brief Launch one package by validating, mounting and executing its entry.
 * @param Context Shell context.
 * @param QualifiedCommandLine Full qualified command line (package + args).
 * @param QualifiedCommand Qualified package file path.
 * @param Background TRUE for background launch.
 * @return TRUE on success.
 */
static BOOL ShellLaunchPackage(LPSHELLCONTEXT Context,
                               LPCSTR QualifiedCommandLine,
                               LPCSTR QualifiedCommand,
                               LPCSTR PreferredCommandName,
                               LPCSTR PreferredCommandArguments,
                               BOOL Background) {
    UINT PackageSize = 0;
    U8* PackageBytes = NULL;
    PACKAGE_MANIFEST Manifest;
    U32 Status;
    LPFILESYSTEM PackageFileSystem = NULL;
    STR MountName[MAX_FILE_NAME];
    STR LaunchCommandLine[MAX_PATH_NAME];
    STR FallbackArguments[MAX_PATH_NAME];
    LPCSTR Arguments;
    LPCSTR LaunchTarget;
    BOOL LaunchResult;
    LPPROCESS Process = NULL;
    BOOL KeepMountedForBackground = FALSE;

    if (Context == NULL || STRING_EMPTY(QualifiedCommandLine) || STRING_EMPTY(QualifiedCommand)) {
        return FALSE;
    }

    PackageBytes = (U8*)FileReadAll(QualifiedCommand, &PackageSize);
    if (PackageBytes == NULL || PackageSize == 0) {
        ConsolePrint(TEXT("Cannot read package file: %s\n"), QualifiedCommand);
        return FALSE;
    }

    Status = PackageManifestParseFromPackageBuffer(PackageBytes, PackageSize, &Manifest);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        ConsolePrint(TEXT("Package manifest error: %s (%u)\n"),
            PackageManifestStatusToString(Status),
            Status);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    Status = PackageManifestCheckCompatibility(&Manifest);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        ConsolePrint(TEXT("Package compatibility error: %s (%u)\n"),
            PackageManifestStatusToString(Status),
            Status);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    StringPrintFormat(MountName, TEXT("pkg-%s-%u"), Manifest.Name, GetSystemTime());
    Status = PackageFSMountFromBuffer(PackageBytes, PackageSize, MountName, NULL, &PackageFileSystem);
    if (Status != DF_RETURN_SUCCESS || PackageFileSystem == NULL) {
        ConsolePrint(TEXT("Package mount failed: %u\n"), Status);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    if (!PackageNamespaceBindCurrentProcessPackageView(PackageFileSystem, Manifest.Name)) {
        ConsolePrint(TEXT("Package namespace bind failed\n"));
        PackageFSUnmount(PackageFileSystem);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    Arguments = QualifiedCommandLine + StringLength(QualifiedCommand);
    while (*Arguments == STR_SPACE) Arguments++;

    LaunchTarget = Manifest.Entry;
    if (!STRING_EMPTY(PreferredCommandName)) {
        LPCSTR CommandTarget = PackageManifestFindCommandTarget(&Manifest, PreferredCommandName);

        if (!STRING_EMPTY(CommandTarget)) {
            LaunchTarget = CommandTarget;
            Arguments = PreferredCommandArguments != NULL ? PreferredCommandArguments : TEXT("");
        } else {
            if (!STRING_EMPTY(PreferredCommandArguments)) {
                StringCopy(FallbackArguments, PreferredCommandName);
                StringConcat(FallbackArguments, TEXT(" "));
                StringConcat(FallbackArguments, PreferredCommandArguments);
                Arguments = FallbackArguments;
            } else {
                Arguments = PreferredCommandName;
            }
        }
    }

    if (!ShellBuildPackageLaunchCommandLine(LaunchTarget, Arguments, LaunchCommandLine)) {
        ConsolePrint(TEXT("Package launch command build failed\n"));
        PackageNamespaceUnbindCurrentProcessPackageView();
        PackageFSUnmount(PackageFileSystem);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    LaunchResult = ShellLaunchCommandLine(Context, LaunchCommandLine, Background, &Process);
    if (!LaunchResult) {
        PackageNamespaceUnbindCurrentProcessPackageView();
        PackageFSUnmount(PackageFileSystem);
        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
        return FALSE;
    }

    if (Background) {
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            Process->PackageFileSystem = PackageFileSystem;
            KeepMountedForBackground = TRUE;
        }
    }

    if (!Background || !KeepMountedForBackground) {
        PackageNamespaceUnbindCurrentProcessPackageView();
        PackageFSUnmount(PackageFileSystem);
    }

    PackageManifestRelease(&Manifest);
    KernelHeapFree(PackageBytes);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Common function to launch an executable or an E0 script.
 *
 * @param Context Shell context.
 * @param CommandName Name of the command/executable to spawn.
 * @param Background TRUE to run in background, FALSE for foreground.
 */
BOOL SpawnExecutable(LPSHELLCONTEXT Context, LPCSTR CommandName, BOOL Background) {
    STR QualifiedCommandLine[MAX_PATH_NAME];
    STR QualifiedCommand[MAX_PATH_NAME];
    U32 CommandIndex = 0;

    if (QualifyCommandLine(Context, CommandName, QualifiedCommandLine)) {
        while (QualifiedCommandLine[CommandIndex] != STR_NULL &&
               QualifiedCommandLine[CommandIndex] > STR_SPACE &&
               CommandIndex < MAX_PATH_NAME - 1) {
            QualifiedCommand[CommandIndex] = QualifiedCommandLine[CommandIndex];
            CommandIndex++;
        }
        QualifiedCommand[CommandIndex] = STR_NULL;

        if (ScriptIsE0FileName(QualifiedCommand)) {
            if (Background) {
                ConsolePrint(TEXT("E0 scripts cannot be started in background mode.\n"));
                return FALSE;
            }
            return RunScriptFile(Context, QualifiedCommand);
        }

        if (ShellIsPackageFileName(QualifiedCommand)) {
            return ShellLaunchPackage(Context, QualifiedCommandLine, QualifiedCommand, NULL, NULL, Background);
        }

        return ShellLaunchCommandLine(Context, QualifiedCommandLine, Background, NULL);
    }

    return FALSE;
}

/***************************************************************************/
