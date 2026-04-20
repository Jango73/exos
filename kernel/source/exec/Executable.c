
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


    Executable

\************************************************************************/

#include "exec/Executable.h"

#include "exec/ExecutableELF.h"
#include "exec/ExecutableEXOS.h"
#include "log/Log.h"
#include "text/CoreString.h"

/***************************************************************************/

/**
 * @brief Read one executable signature from the start of the file.
 * @param File Source file.
 * @param Signature Receives the first 4 bytes.
 * @return TRUE on success.
 */
static BOOL ReadExecutableSignature(LPFILE File, U32* Signature) {
    FILE_OPERATION FileOperation;
    U32 BytesTransferred;

    if (File == NULL || Signature == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILE_OPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;
    FileOperation.NumBytes = sizeof(U32);
    FileOperation.Buffer = (LPVOID)Signature;

    BytesTransferred = ReadFile(&FileOperation);
    File->Position = 0;
    return BytesTransferred == sizeof(U32);
}

/***************************************************************************/

/**
 * @brief Convert legacy image layout information into generic metadata.
 * @param Format Executable format identifier.
 * @param ImageInfo Legacy layout information.
 * @param Metadata Receives generic metadata.
 */
static void StoreLegacyImageMetadata(U32 Format, const EXECUTABLE_INFO* ImageInfo, LPEXECUTABLE_METADATA Metadata) {
    MemorySet(Metadata, 0, sizeof(EXECUTABLE_METADATA));

    Metadata->Format = Format;
    Metadata->Target = EXECUTABLE_TARGET_IMAGE;
#if defined(__EXOS_ARCH_X86_64__)
    Metadata->Architecture = EXECUTABLE_ARCHITECTURE_X86_64;
#else
    Metadata->Architecture = EXECUTABLE_ARCHITECTURE_X86_32;
#endif
    Metadata->EntryPoint = ImageInfo->EntryPoint;
    Metadata->Layout = *ImageInfo;
}

/***************************************************************************/

/**
 * @brief Inspect one main executable image and expose generic metadata.
 * @param File Open file handle.
 * @param Metadata Receives executable metadata.
 * @return TRUE on success.
 */
BOOL GetExecutableImageInfo(LPFILE File, LPEXECUTABLE_METADATA Metadata) {
    U32 Signature;
    EXECUTABLE_INFO ImageInfo;

    DEBUG(TEXT("Enter"));

    if (File == NULL) return FALSE;
    if (Metadata == NULL) return FALSE;
    if (!ReadExecutableSignature(File, &Signature)) return FALSE;

    if (Signature == EXOS_SIGNATURE) {
        if (!GetExecutableInfo_EXOS(File, &ImageInfo)) return FALSE;
        StoreLegacyImageMetadata(EXECUTABLE_FORMAT_EXOS, &ImageInfo, Metadata);
        return TRUE;
    }

    if (Signature == ELF_SIGNATURE) {
        return GetExecutableImageInfo_ELF(File, Metadata);
    }

    DEBUG(TEXT("Unknown signature %X"), Signature);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Inspect one loadable module image and expose generic metadata.
 * @param File Open file handle.
 * @param Metadata Receives module metadata.
 * @return TRUE on success.
 */
BOOL GetExecutableModuleInfo(LPFILE File, LPEXECUTABLE_METADATA Metadata) {
    U32 Signature;

    DEBUG(TEXT("Enter"));

    if (File == NULL) return FALSE;
    if (Metadata == NULL) return FALSE;
    if (!ReadExecutableSignature(File, &Signature)) return FALSE;

    if (Signature == ELF_SIGNATURE) {
        return GetExecutableModuleInfo_ELF(File, Metadata);
    }

    DEBUG(TEXT("Unsupported signature %X"), Signature);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Determine executable format and fill information structure.
 * @param File Open file handle.
 * @param Info Output structure to populate.
 * @return TRUE on success, FALSE on error or unknown format.
 */
BOOL GetExecutableInfo(LPFILE File, LPEXECUTABLE_INFO Info) {
    EXECUTABLE_METADATA Metadata;

    DEBUG(TEXT("Enter"));

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    if (!GetExecutableImageInfo(File, &Metadata)) return FALSE;

    *Info = Metadata.Layout;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Load an executable into memory based on its format.
 * @param Load Parameters describing the load operation.
 * @return TRUE on success, FALSE on failure.
 */
BOOL LoadExecutable(LPEXECUTABLE_LOAD Load) {
    U32 Signature;

    DEBUG(TEXT("Enter"));

    if (Load == NULL) return FALSE;
    if (Load->File == NULL) return FALSE;

    if (!ReadExecutableSignature(Load->File, &Signature)) return FALSE;

    if (Signature == EXOS_SIGNATURE) {
        return LoadExecutable_EXOS(Load->File, Load->Info, Load->CodeBase, Load->DataBase);
    } else if (Signature == ELF_SIGNATURE) {
        return LoadExecutable_ELF(Load->File, Load->Info, Load->CodeBase, Load->DataBase, Load->BssBase);
    }

    DEBUG(TEXT("Unknown signature %X"), Signature);

    return FALSE;
}
