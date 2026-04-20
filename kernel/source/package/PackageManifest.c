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


    Package manifest parser and compatibility model

\************************************************************************/

#include "package/PackageManifest.h"

#include "text/CoreString.h"
#include "memory/Heap.h"
#include "log/Log.h"
#include "User.h"
#include "package/EpkParser.h"
#include "utils/TOML.h"

/***************************************************************************/

/**
 * @brief Reset manifest model to empty state.
 * @param Manifest Manifest output structure.
 */
static void PackageManifestReset(LPPACKAGE_MANIFEST Manifest) {
    if (Manifest == NULL) return;

    MemorySet(Manifest->Name, 0, sizeof(Manifest->Name));
    MemorySet(Manifest->Version, 0, sizeof(Manifest->Version));
    MemorySet(Manifest->Arch, 0, sizeof(Manifest->Arch));
    MemorySet(Manifest->KernelApi, 0, sizeof(Manifest->KernelApi));
    MemorySet(Manifest->Entry, 0, sizeof(Manifest->Entry));
    Manifest->CommandCount = 0;
    Manifest->Commands = NULL;
}

/***************************************************************************/

/**
 * @brief Check whether one string starts with one prefix.
 * @param Text Source string.
 * @param Prefix Prefix to match.
 * @return TRUE when Prefix is at start of Text.
 */
static BOOL PackageManifestStartsWith(LPCSTR Text, LPCSTR Prefix) {
    UINT Index = 0;

    if (Text == NULL || Prefix == NULL) {
        return FALSE;
    }

    while (Prefix[Index] != STR_NULL) {
        if (Text[Index] != Prefix[Index]) {
            return FALSE;
        }

        Index++;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one manifest key from top-level or [package] section.
 * @param Toml Parsed TOML object.
 * @param KeyName Base key name.
 * @return Key value when present.
 */
static LPCSTR PackageManifestGetKey(LPTOML Toml, LPCSTR KeyName) {
    STR ScopedKey[64];
    LPCSTR Value;

    if (Toml == NULL || STRING_EMPTY(KeyName)) {
        return NULL;
    }

    Value = TomlGet(Toml, KeyName);
    if (Value != NULL && Value[0] != STR_NULL) {
        return Value;
    }

    StringPrintFormat(ScopedKey, TEXT("package.%s"), KeyName);
    Value = TomlGet(Toml, ScopedKey);
    if (Value != NULL && Value[0] != STR_NULL) {
        return Value;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Parse one strict "major.minor" version string.
 * @param Text Source string.
 * @param OutMajor Receives major value.
 * @param OutMinor Receives minor value.
 * @return TRUE on valid parse.
 */
static BOOL PackageManifestParseVersionMajorMinor(LPCSTR Text, U32* OutMajor, U32* OutMinor) {
    LPCSTR Dot;
    UINT MajorLength;
    UINT MinorLength;
    STR MajorText[16];
    STR MinorText[16];
    UINT Index;

    if (STRING_EMPTY(Text) || OutMajor == NULL || OutMinor == NULL) {
        return FALSE;
    }

    Dot = StringFindChar(Text, '.');
    if (Dot == NULL) {
        return FALSE;
    }

    MajorLength = (UINT)(Dot - Text);
    MinorLength = StringLength(Dot + 1);
    if (MajorLength == 0 || MinorLength == 0 || MajorLength >= sizeof(MajorText) || MinorLength >= sizeof(MinorText)) {
        return FALSE;
    }

    for (Index = 0; Index < MajorLength; Index++) {
        if (!IsNumeric(Text[Index])) return FALSE;
    }

    for (Index = 0; Index < MinorLength; Index++) {
        if (!IsNumeric(Dot[1 + Index])) return FALSE;
    }

    StringCopyNum(MajorText, Text, MajorLength);
    MajorText[MajorLength] = STR_NULL;
    StringCopyNum(MinorText, Dot + 1, MinorLength);
    MinorText[MinorLength] = STR_NULL;

    *OutMajor = StringToU32(MajorText);
    *OutMinor = StringToU32(MinorText);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Return architecture name for the active kernel build.
 * @return Architecture name used in package manifests.
 */
static LPCSTR PackageManifestGetCurrentArchitecture(void) {
#if defined(__EXOS_ARCH_X86_32__)
    return TEXT("x86-32");
#elif defined(__EXOS_ARCH_X86_64__)
    return TEXT("x86-64");
#else
    return TEXT("unknown");
#endif
}

/***************************************************************************/

/**
 * @brief Validate one manifest architecture string.
 * @param Arch Architecture string from manifest.
 * @return TRUE when syntax is accepted.
 */
static BOOL PackageManifestIsSupportedManifestArch(LPCSTR Arch) {
    if (STRING_EMPTY(Arch)) {
        return FALSE;
    }

    if (StringCompare(Arch, TEXT("x86-32")) == 0) {
        return TRUE;
    }

    if (StringCompare(Arch, TEXT("x86-64")) == 0) {
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Validate one package-relative path.
 * @param Path Path string.
 * @return TRUE when path follows package-relative policy.
 */
static BOOL PackageManifestIsValidPackageRelativePath(LPCSTR Path) {
    LPCSTR SegmentStart;
    LPCSTR Cursor;

    if (STRING_EMPTY(Path)) {
        return FALSE;
    }

    if (Path[0] != PATH_SEP || Path[1] == STR_NULL) {
        return FALSE;
    }

    if (PackageManifestStartsWith(Path, TEXT("/package"))) {
        if (Path[8] == STR_NULL || Path[8] == PATH_SEP) {
            return FALSE;
        }
    }

    SegmentStart = Path + 1;
    Cursor = SegmentStart;

    FOREVER {
        BOOL AtSeparator = (*Cursor == PATH_SEP);
        BOOL AtEnd = (*Cursor == STR_NULL);

        if (AtSeparator || AtEnd) {
            UINT SegmentLength = (UINT)(Cursor - SegmentStart);

            if (SegmentLength == 0) {
                return FALSE;
            }

            if (SegmentLength == 1 && SegmentStart[0] == '.') {
                return FALSE;
            }

            if (SegmentLength == 2 && SegmentStart[0] == '.' && SegmentStart[1] == '.') {
                return FALSE;
            }

            if (AtEnd) {
                break;
            }

            SegmentStart = Cursor + 1;
        }

        Cursor++;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate one command name token.
 * @param Name Command name.
 * @return TRUE when syntax is accepted.
 */
static BOOL PackageManifestIsValidCommandName(LPCSTR Name) {
    UINT Index = 0;

    if (STRING_EMPTY(Name)) {
        return FALSE;
    }

    for (Index = 0; Name[Index] != STR_NULL; Index++) {
        STR Character = Name[Index];
        BOOL IsNameCharacter =
            IsAlphaNumeric(Character) || Character == '-' || Character == '_' || Character == '.';

        if (!IsNameCharacter) {
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve command name pointer from one TOML key.
 * @param Key TOML key.
 * @return Command name suffix when key belongs to command table.
 */
static LPCSTR PackageManifestGetCommandNameFromTomlKey(LPCSTR Key) {
    static const STR CommandsPrefix[] = "commands.";
    static const STR PackageCommandsPrefix[] = "package.commands.";

    if (STRING_EMPTY(Key)) {
        return NULL;
    }

    if (PackageManifestStartsWith(Key, CommandsPrefix)) {
        return Key + sizeof(CommandsPrefix) - 1;
    }

    if (PackageManifestStartsWith(Key, PackageCommandsPrefix)) {
        return Key + sizeof(PackageCommandsPrefix) - 1;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Parse optional manifest command map.
 * @param Toml Parsed TOML object.
 * @param OutManifest Manifest output.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
static U32 PackageManifestParseCommands(LPTOML Toml, LPPACKAGE_MANIFEST OutManifest) {
    LPTOMLITEM Item;
    UINT CommandCount = 0;
    UINT CommandIndex = 0;

    if (Toml == NULL || OutManifest == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    for (Item = Toml->First; Item != NULL; Item = Item->Next) {
        LPCSTR CommandName = PackageManifestGetCommandNameFromTomlKey(Item->Key);

        if (CommandName != NULL) {
            if (CommandName[0] == STR_NULL) {
                return PACKAGE_MANIFEST_STATUS_INVALID_COMMAND_MAP;
            }

            CommandCount++;
        }
    }

    if (CommandCount == 0) {
        return PACKAGE_MANIFEST_STATUS_OK;
    }

    OutManifest->Commands =
        (LPPACKAGE_MANIFEST_COMMAND)KernelHeapAlloc(sizeof(PACKAGE_MANIFEST_COMMAND) * CommandCount);
    if (OutManifest->Commands == NULL) {
        return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
    }

    MemorySet(OutManifest->Commands, 0, sizeof(PACKAGE_MANIFEST_COMMAND) * CommandCount);

    for (Item = Toml->First; Item != NULL; Item = Item->Next) {
        LPCSTR CommandName = PackageManifestGetCommandNameFromTomlKey(Item->Key);
        UINT ExistingIndex;

        if (CommandName == NULL) {
            continue;
        }

        if (!PackageManifestIsValidCommandName(CommandName)) {
            ERROR(TEXT("Invalid command name=%s"), CommandName);
            return PACKAGE_MANIFEST_STATUS_INVALID_COMMAND_MAP;
        }

        if (!PackageManifestIsValidPackageRelativePath(Item->Value)) {
            ERROR(TEXT("Invalid command target name=%s target=%s"),
                CommandName,
                Item->Value);
            return PACKAGE_MANIFEST_STATUS_INVALID_COMMAND_MAP;
        }

        for (ExistingIndex = 0; ExistingIndex < CommandIndex; ExistingIndex++) {
            if (StringCompare(OutManifest->Commands[ExistingIndex].Name, CommandName) == 0) {
                ERROR(TEXT("Duplicate command name=%s"), CommandName);
                return PACKAGE_MANIFEST_STATUS_DUPLICATE_COMMAND_NAME;
            }
        }

        StringCopyLimit(OutManifest->Commands[CommandIndex].Name,
            CommandName,
            sizeof(OutManifest->Commands[CommandIndex].Name) - 1);
        StringCopyLimit(OutManifest->Commands[CommandIndex].Target,
            Item->Value,
            sizeof(OutManifest->Commands[CommandIndex].Target) - 1);

        CommandIndex++;
    }

    OutManifest->CommandCount = CommandIndex;
    return PACKAGE_MANIFEST_STATUS_OK;
}

/***************************************************************************/

/**
 * @brief Parse manifest TOML text into model.
 * @param ManifestText Null-terminated manifest text.
 * @param OutManifest Receives parsed manifest model.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
U32 PackageManifestParseText(LPCSTR ManifestText, LPPACKAGE_MANIFEST OutManifest) {
    LPTOML Toml;
    LPCSTR Name;
    LPCSTR Version;
    LPCSTR Arch;
    LPCSTR KernelApi;
    LPCSTR Entry;
    LPCSTR Provides;
    LPCSTR Requires;
    U32 Status;

    if (ManifestText == NULL || OutManifest == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    PackageManifestReset(OutManifest);

    Toml = TomlParse(ManifestText);
    if (Toml == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_TOML;
    }

    Name = PackageManifestGetKey(Toml, TEXT("name"));
    if (STRING_EMPTY(Name)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_NAME;
    }

    Version = PackageManifestGetKey(Toml, TEXT("version"));
    if (STRING_EMPTY(Version)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_VERSION;
    }

    Arch = PackageManifestGetKey(Toml, TEXT("arch"));
    if (STRING_EMPTY(Arch)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_ARCH;
    }

    KernelApi = PackageManifestGetKey(Toml, TEXT("kernel_api"));
    if (STRING_EMPTY(KernelApi)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_KERNEL_API;
    }

    Entry = PackageManifestGetKey(Toml, TEXT("entry"));
    if (STRING_EMPTY(Entry)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_ENTRY;
    }

    if (!PackageManifestIsValidPackageRelativePath(Entry)) {
        ERROR(TEXT("Invalid entry path=%s"), Entry);
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_INVALID_ENTRY_PATH;
    }

    Provides = PackageManifestGetKey(Toml, TEXT("provides"));
    Requires = PackageManifestGetKey(Toml, TEXT("requires"));
    if (!STRING_EMPTY(Provides) || !STRING_EMPTY(Requires)) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_FORBIDDEN_DEPENDENCY_GRAPH;
    }

    StringCopyLimit(OutManifest->Name, Name, MAX_FILE_NAME - 1);
    StringCopyLimit(OutManifest->Version, Version, sizeof(OutManifest->Version) - 1);
    StringCopyLimit(OutManifest->Arch, Arch, sizeof(OutManifest->Arch) - 1);
    StringCopyLimit(OutManifest->KernelApi, KernelApi, sizeof(OutManifest->KernelApi) - 1);
    StringCopyLimit(OutManifest->Entry, Entry, sizeof(OutManifest->Entry) - 1);

    Status = PackageManifestParseCommands(Toml, OutManifest);
    TomlFree(Toml);

    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        PackageManifestRelease(OutManifest);
    }

    return Status;
}

/***************************************************************************/

/**
 * @brief Parse manifest from package bytes by validating EPK sections.
 * @param PackageBytes Package byte buffer.
 * @param PackageSize Package size.
 * @param OutManifest Receives parsed manifest model.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
U32 PackageManifestParseFromPackageBuffer(LPCVOID PackageBytes,
                                          U32 PackageSize,
                                          LPPACKAGE_MANIFEST OutManifest) {
    EPK_PARSER_OPTIONS Options = {
        .VerifyPackageHash = TRUE,
        .VerifySignature = TRUE,
        .RequireSignature = FALSE};
    EPK_VALIDATED_PACKAGE Package;
    U8* ManifestText;
    U32 Status;
    U32 ParserStatus;

    if (PackageBytes == NULL || PackageSize == 0 || OutManifest == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    PackageManifestReset(OutManifest);
    MemorySet(&Package, 0, sizeof(Package));

    ParserStatus = EpkValidatePackageBuffer(PackageBytes, PackageSize, &Options, &Package);
    if (ParserStatus != EPK_VALIDATION_OK) {
        return PACKAGE_MANIFEST_STATUS_INVALID_PACKAGE;
    }

    if (Package.ManifestSize == 0 || Package.ManifestOffset >= Package.PackageSize ||
        Package.ManifestOffset + Package.ManifestSize < Package.ManifestOffset ||
        Package.ManifestOffset + Package.ManifestSize > Package.PackageSize) {
        EpkReleaseValidatedPackage(&Package);
        return PACKAGE_MANIFEST_STATUS_INVALID_MANIFEST_BLOB;
    }

    ManifestText = (U8*)KernelHeapAlloc(Package.ManifestSize + 1);
    if (ManifestText == NULL) {
        EpkReleaseValidatedPackage(&Package);
        return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
    }

    MemoryCopy(ManifestText, Package.PackageBytes + Package.ManifestOffset, Package.ManifestSize);
    ManifestText[Package.ManifestSize] = STR_NULL;

    Status = PackageManifestParseText((LPCSTR)ManifestText, OutManifest);

    KernelHeapFree(ManifestText);
    EpkReleaseValidatedPackage(&Package);
    return Status;
}

/***************************************************************************/

/**
 * @brief Validate package manifest compatibility policy.
 * @param Manifest Parsed manifest model.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
U32 PackageManifestCheckCompatibility(const PACKAGE_MANIFEST* Manifest) {
    LPCSTR CurrentArchitecture;
    U32 RequiredMajor;
    U32 RequiredMinor;
    U32 CurrentMajor = EXOS_VERSION_MAJOR;
    U32 CurrentMinor = EXOS_VERSION_MINOR;

    if (Manifest == NULL) {
        ERROR(TEXT("Invalid argument"));
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    if (!PackageManifestIsSupportedManifestArch(Manifest->Arch)) {
        ERROR(TEXT("Invalid arch value=%s"), Manifest->Arch);
        return PACKAGE_MANIFEST_STATUS_INVALID_ARCH;
    }

    CurrentArchitecture = PackageManifestGetCurrentArchitecture();
    if (StringCompare(Manifest->Arch, CurrentArchitecture) != 0) {
        ERROR(TEXT("Incompatible arch required=%s current=%s"),
            Manifest->Arch,
            CurrentArchitecture);
        return PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_ARCH;
    }

    if (!PackageManifestParseVersionMajorMinor(Manifest->KernelApi, &RequiredMajor, &RequiredMinor)) {
        ERROR(TEXT("Invalid kernel_api value=%s"), Manifest->KernelApi);
        return PACKAGE_MANIFEST_STATUS_INVALID_KERNEL_API;
    }

    if (RequiredMajor != CurrentMajor || RequiredMinor > CurrentMinor) {
        ERROR(TEXT("Incompatible kernel_api required=%u.%u current=%u.%u"),
            RequiredMajor,
            RequiredMinor,
            CurrentMajor,
            CurrentMinor);
        return PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_KERNEL_API;
    }

    return PACKAGE_MANIFEST_STATUS_OK;
}

/***************************************************************************/

/**
 * @brief Find one command target path by command name.
 * @param Manifest Parsed manifest model.
 * @param CommandName Command key to resolve.
 * @return Package-relative executable path or NULL when not found.
 */
LPCSTR PackageManifestFindCommandTarget(const PACKAGE_MANIFEST* Manifest, LPCSTR CommandName) {
    UINT Index;

    if (Manifest == NULL || STRING_EMPTY(CommandName)) {
        return NULL;
    }

    for (Index = 0; Index < Manifest->CommandCount; Index++) {
        if (StringCompare(Manifest->Commands[Index].Name, CommandName) == 0) {
            return Manifest->Commands[Index].Target;
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Return one deterministic message for manifest status code.
 * @param Status Manifest status value.
 * @return Static status string.
 */
LPCSTR PackageManifestStatusToString(U32 Status) {
    switch (Status) {
        case PACKAGE_MANIFEST_STATUS_OK:
            return TEXT("ok");
        case PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT:
            return TEXT("invalid_argument");
        case PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY:
            return TEXT("out_of_memory");
        case PACKAGE_MANIFEST_STATUS_INVALID_TOML:
            return TEXT("invalid_toml");
        case PACKAGE_MANIFEST_STATUS_MISSING_NAME:
            return TEXT("missing_name");
        case PACKAGE_MANIFEST_STATUS_MISSING_VERSION:
            return TEXT("missing_version");
        case PACKAGE_MANIFEST_STATUS_MISSING_ARCH:
            return TEXT("missing_arch");
        case PACKAGE_MANIFEST_STATUS_MISSING_KERNEL_API:
            return TEXT("missing_kernel_api");
        case PACKAGE_MANIFEST_STATUS_MISSING_ENTRY:
            return TEXT("missing_entry");
        case PACKAGE_MANIFEST_STATUS_INVALID_PACKAGE:
            return TEXT("invalid_package");
        case PACKAGE_MANIFEST_STATUS_INVALID_MANIFEST_BLOB:
            return TEXT("invalid_manifest_blob");
        case PACKAGE_MANIFEST_STATUS_FORBIDDEN_DEPENDENCY_GRAPH:
            return TEXT("forbidden_dependency_graph");
        case PACKAGE_MANIFEST_STATUS_INVALID_ARCH:
            return TEXT("invalid_arch");
        case PACKAGE_MANIFEST_STATUS_INVALID_KERNEL_API:
            return TEXT("invalid_kernel_api");
        case PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_ARCH:
            return TEXT("incompatible_arch");
        case PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_KERNEL_API:
            return TEXT("incompatible_kernel_api");
        case PACKAGE_MANIFEST_STATUS_INVALID_ENTRY_PATH:
            return TEXT("invalid_entry_path");
        case PACKAGE_MANIFEST_STATUS_INVALID_COMMAND_MAP:
            return TEXT("invalid_command_map");
        case PACKAGE_MANIFEST_STATUS_DUPLICATE_COMMAND_NAME:
            return TEXT("duplicate_command_name");
        default:
            return TEXT("unknown_status");
    }
}

/***************************************************************************/

/**
 * @brief Release dynamic manifest model allocations.
 * @param Manifest Manifest model.
 */
void PackageManifestRelease(LPPACKAGE_MANIFEST Manifest) {
    if (Manifest == NULL) return;

    if (Manifest->Commands != NULL) {
        KernelHeapFree(Manifest->Commands);
    }

    PackageManifestReset(Manifest);
}
