
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


    Path utilities

\************************************************************************/

#include "utils/Path.h"

#include "memory/Heap.h"
#include "utils/List.h"
#include "log/Log.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Destructor function for path components.
 *
 * @param This Pointer to the path component to destroy.
 */
static void PathComponentDestructor(LPVOID This) { KernelHeapFree(This); }

/************************************************************************/

/**
 * @brief Decomposes a file path into individual components.
 *
 * @param Path File path string to decompose.
 * @return List of path components, or NULL on failure.
 */
LPLIST DecomposePath(LPCSTR Path) {
    STR Component[MAX_FILE_NAME];
    U32 PathIndex = 0;
    U32 ComponentIndex = 0;
    LPLIST List = NULL;
    LPPATH_NODE Node = NULL;

    if (Path == NULL) {
        ERROR(TEXT("Path is NULL"));
        return NULL;
    }

    List = NewList(PathComponentDestructor, KernelHeapAlloc, KernelHeapFree);
    if (List == NULL) {
        ERROR(TEXT("Failed to create list"));
        return NULL;
    }

    FOREVER {
        ComponentIndex = 0;

        FOREVER {
            if (Path[PathIndex] == STR_SLASH) {
                Component[ComponentIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (Path[PathIndex] == STR_NULL) {
                Component[ComponentIndex] = STR_NULL;
                break;
            } else {
                if (ComponentIndex >= MAX_FILE_NAME - 1) {
                    ERROR(TEXT("Component too long at index %u"), ComponentIndex);
                    goto Error;
                }
                Component[ComponentIndex++] = Path[PathIndex++];
            }
        }

        Node = KernelHeapAlloc(sizeof(PATH_NODE));
        if (Node == NULL) {
            ERROR(TEXT("Failed to allocate node"));
            goto Error;
        }
        StringCopy(Node->Name, Component);
        ListAddItem(List, Node);

        if (Path[PathIndex] == STR_NULL) break;
    }

    return List;

Error:
    SAFE_USE(List) {
        DeleteList(List);
    }
    DEBUG(TEXT("Error occurred, returning NULL"));
    return NULL;
}

/***************************************************************************/

/**
 * @brief Checks if a name starts with a given part (case-insensitive).
 *
 * @param Name The full name to check.
 * @param Part The starting part to match.
 * @return TRUE if Name starts with Part, FALSE otherwise.
 */
BOOL MatchStart(LPCSTR Name, LPCSTR Part) {
    U32 Index = 0;
    while (Part[Index] != STR_NULL) {
        if (CharToLower(Name[Index]) != CharToLower(Part[Index])) return FALSE;
        Index++;
    }
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Builds a list of path completion matches for a given path.
 *
 * @param Context Path completion context to store matches.
 * @param Path Base path to find completions for.
 */
void BuildMatches(LPPATHCOMPLETION Context, LPCSTR Path) {
    STR Dir[MAX_PATH_NAME];
    STR Part[MAX_FILE_NAME];
    STR Pattern[MAX_PATH_NAME];
    LPSTR Slash;
    FILE_INFO Find;
    LPFILE File;

    Context->Matches.Count = 0;
    StringCopy(Context->Base, Path);
    Context->Index = 0;

    Slash = StringFindCharR(Path, PATH_SEP);
    if (Slash) {
        U32 DirectoryLength = Slash - Path + 1;
        StringCopyNum(Dir, Path, DirectoryLength);
        Dir[DirectoryLength] = STR_NULL;
        StringCopy(Part, Slash + 1);
    } else {
        Dir[0] = STR_NULL;
        StringCopy(Part, Path);
    }

    StringCopy(Pattern, Dir);
    StringConcat(Pattern, TEXT("*"));

    Find.Size = sizeof(FILE_INFO);
    Find.FileSystem = Context->FileSystem;
    Find.Attributes = MAX_U32;
    Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    StringCopy(Find.Name, Pattern);

    if (Context->FileSystem == NULL) {
        DEBUG(TEXT("CORRUPTION: Context->FileSystem is NULL"));
        return;
    }
    if (Context->FileSystem->Driver == NULL) {
        DEBUG(TEXT("CORRUPTION: Context->FileSystem->Driver is NULL"));
        return;
    }
    if (Context->FileSystem->Driver->Command == NULL) {
        DEBUG(TEXT("CORRUPTION: Context->FileSystem->Driver->Command is NULL"));
        return;
    }

    File = (LPFILE)Context->FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (File == NULL) return;

    do {
        if (MatchStart(File->Name, Part)) {
            STR Full[MAX_PATH_NAME];
            StringCopy(Full, Dir);
            StringConcat(Full, File->Name);
            StringArrayAddUnique(&Context->Matches, Full);
        }
    } while (Context->FileSystem != NULL && Context->FileSystem->Driver != NULL &&
             Context->FileSystem->Driver->Command != NULL &&
             Context->FileSystem->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RETURN_SUCCESS);

    if (Context->FileSystem != NULL && Context->FileSystem->Driver != NULL &&
        Context->FileSystem->Driver->Command != NULL) {
        Context->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
    } else {
        DEBUG(TEXT("CORRUPTION: Context->FileSystem corrupted during file operations"));
    }
}

/***************************************************************************/

/**
 * @brief Initializes a path completion context.
 *
 * @param Context Path completion context to initialize.
 * @param FileSystem File system to use for path completion.
 * @return TRUE on success, FALSE on failure.
 */
BOOL PathCompletionInit(LPPATHCOMPLETION Context, LPFILESYSTEM FileSystem) {
    return PathCompletionInitA(Context, FileSystem, NULL);
}

/***************************************************************************/

BOOL PathCompletionInitA(LPPATHCOMPLETION Context, LPFILESYSTEM FileSystem, LPCALLOCATOR Allocator) {
    if (FileSystem == NULL) {
        ERROR(TEXT("PathCompletionInit called with NULL FileSystem"));
        return FALSE;
    }
    if (FileSystem->Driver == NULL) {
        ERROR(TEXT("PathCompletionInit - FileSystem->Driver is NULL"));
        return FALSE;
    }

    Context->FileSystem = FileSystem;
    Context->Base[0] = STR_NULL;
    Context->Index = 0;
    return StringArrayInitA(&Context->Matches, 32, Allocator);
}

/***************************************************************************/

/**
 * @brief Deinitializes a path completion context.
 *
 * @param Context Path completion context to deinitialize.
 */
void PathCompletionDeinit(LPPATHCOMPLETION Context) { StringArrayDeinit(&Context->Matches); }

/***************************************************************************/

/**
 * @brief Gets the next path completion match for a given path.
 *
 * This function takes a path (which can be absolute or relative) and finds
 * the next matching file or directory name for tab completion. The path
 * is expected to be already processed according to the completion rules:
 * - Empty string or no slash: complete in current directory
 * - Starts with "/": absolute path completion
 * - Contains slash: complete in the specified directory
 *
 * @param Context Path completion context containing matches and state.
 * @param Path Directory path where completion should occur (already processed).
 * @param Output Buffer to store the next completion match (full path).
 * @return TRUE if a match was found, FALSE otherwise.
 */
BOOL PathCompletionNext(LPPATHCOMPLETION Context, LPCSTR Path, LPSTR Output) {
    U32 Index;
    BOOL SameStart = TRUE;
    U32 BaseLength = StringLength(Context->Base);

    // Check if we're continuing with the same base path or starting fresh
    for (Index = 0; Index < BaseLength; Index++) {
        if (CharToLower(Path[Index]) != CharToLower(Context->Base[Index])) {
            SameStart = FALSE;
            break;
        }
    }

    // Build new matches if this is a new path or we have no matches yet
    if (Context->Matches.Count == 0 || SameStart == FALSE) {
        BuildMatches(Context, Path);
    } else {
        // We're cycling through existing matches for the same base path
        // Find the current match in our list and advance to the next one
        for (Index = 0; Index < Context->Matches.Count; Index++) {
            if (StringCompare(StringArrayGet(&Context->Matches, Index), Path) == 0) {
                Context->Index = Index + 1;
                if (Context->Index >= Context->Matches.Count) Context->Index = 0;
                break;
            }
        }
        // If we didn't find the current path in matches, rebuild
        if (Index == Context->Matches.Count) {
            BuildMatches(Context, Path);
        }
    }

    // No matches found
    if (Context->Matches.Count == 0) return FALSE;

    // Return the current match and advance index for next call
    StringCopy(Output, StringArrayGet(&Context->Matches, Context->Index));
    Context->Index++;
    if (Context->Index >= Context->Matches.Count) Context->Index = 0;

    return TRUE;
}
