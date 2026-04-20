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


    Kernel logical path resolver

\************************************************************************/

#include "utils/KernelPath.h"

#include "text/CoreString.h"
#include "core/Kernel.h"
#include "log/Log.h"

/************************************************************************/

/**
 * @brief Validates a kernel path string for configuration use.
 * @param Path Candidate absolute VFS path.
 * @param OutPathSize Destination buffer size used by caller.
 * @return TRUE when path is non-empty, absolute, and fits the buffer.
 */
static BOOL IsValidKernelPath(LPCSTR Path, UINT OutPathSize) {
    if (STRING_EMPTY(Path)) {
        return FALSE;
    }

    if (Path[0] != PATH_SEP) {
        return FALSE;
    }

    if (StringLength(Path) >= OutPathSize) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolves a logical kernel path from configuration with fallback.
 * @param Name Logical key name under the `KernelPath` section.
 * @param DefaultPath Built-in absolute fallback path.
 * @param OutPath Destination buffer for resolved path.
 * @param OutPathSize Size of destination buffer.
 * @return TRUE on success, FALSE when arguments are invalid.
 */
BOOL KernelPathResolve(LPCSTR Name, LPCSTR DefaultPath, LPSTR OutPath, UINT OutPathSize) {
    STR Key[0x100];
    LPTOML Configuration = NULL;
    LPCSTR ConfiguredPath = NULL;

    if (Name == NULL || DefaultPath == NULL || OutPath == NULL || OutPathSize == 0) {
        return FALSE;
    }

    if (IsValidKernelPath(DefaultPath, OutPathSize) == FALSE) {
        ERROR(TEXT("Invalid default path for key=%s path=%s"), Name, DefaultPath);
        OutPath[0] = STR_NULL;
        return FALSE;
    }

    StringCopy(Key, KERNEL_PATH_CONFIG_PREFIX);
    StringConcat(Key, Name);

    Configuration = GetConfiguration();
    if (Configuration != NULL) {
        ConfiguredPath = TomlGet(Configuration, Key);
    }

    if (ConfiguredPath != NULL) {
        if (IsValidKernelPath(ConfiguredPath, OutPathSize)) {
            StringCopyLimit(OutPath, ConfiguredPath, OutPathSize);
            return TRUE;
        }

        WARNING(TEXT("Invalid configured path for key=%s path=%s, using default"),
            Name,
            ConfiguredPath);
    }

    StringCopyLimit(OutPath, DefaultPath, OutPathSize);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Builds a file path from a configured logical folder and file parts.
 * @param FolderName Logical folder key name under `KernelPath`.
 * @param DefaultFolder Built-in absolute fallback folder path.
 * @param LeafName File name without extension.
 * @param Extension Optional extension to append.
 * @param OutPath Destination buffer for resulting absolute path.
 * @param OutPathSize Size of destination buffer.
 * @return TRUE on success, FALSE on validation or size failure.
 */
BOOL KernelPathBuildFile(
    LPCSTR FolderName,
    LPCSTR DefaultFolder,
    LPCSTR LeafName,
    LPCSTR Extension,
    LPSTR OutPath,
    UINT OutPathSize) {
    STR FolderPath[MAX_PATH_NAME];
    U32 Length = 0;
    U32 LeafLength = 0;
    U32 ExtensionLength = 0;

    if (FolderName == NULL || DefaultFolder == NULL || LeafName == NULL || OutPath == NULL || OutPathSize == 0) {
        return FALSE;
    }

    if (STRING_EMPTY(LeafName)) {
        return FALSE;
    }

    if (KernelPathResolve(FolderName, DefaultFolder, FolderPath, MAX_PATH_NAME) == FALSE) {
        return FALSE;
    }

    Length = StringLength(FolderPath);
    LeafLength = StringLength(LeafName);
    ExtensionLength = (Extension != NULL) ? StringLength(Extension) : 0;

    if (Length == 0) {
        return FALSE;
    }

    StringCopyLimit(OutPath, FolderPath, OutPathSize);

    if (OutPath[StringLength(OutPath) - 1] != PATH_SEP) {
        if (StringLength(OutPath) + 1 >= OutPathSize) {
            WARNING(TEXT("Path too long while appending separator to folder=%s"), FolderPath);
            return FALSE;
        }
        StringConcat(OutPath, TEXT("/"));
    }

    if (StringLength(OutPath) + LeafLength + ExtensionLength >= OutPathSize) {
        WARNING(TEXT("Path too long for folder=%s leaf=%s ext=%s"),
            FolderPath,
            LeafName,
            Extension != NULL ? Extension : TEXT(""));
        return FALSE;
    }

    StringConcat(OutPath, LeafName);
    if (Extension != NULL) {
        StringConcat(OutPath, Extension);
    }

    return TRUE;
}
