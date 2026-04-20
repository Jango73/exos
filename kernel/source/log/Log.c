
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


    Kernel log manager

\************************************************************************/

#include "log/Log.h"

#include "system/Clock.h"
#include "console/Console.h"
#include "core/Driver.h"
#include "memory/Memory.h"
#include "process/Process.h"
#include "system/SerialPort.h"
#include "text/CoreString.h"
#include "text/Text.h"
#include "VarArg.h"

/************************************************************************/

#define KERNEL_LOG_VER_MAJOR 1
#define KERNEL_LOG_VER_MINOR 0
#define KERNEL_LOG_TAG_FILTER_MAX_LENGTH 1024
#define KERNEL_LOG_RECENT_TEXT_BYTES (96 * 1024)
#define KERNEL_LOG_RECENT_MAX_LINES 500
#define KERNEL_LOG_ENTRY_BUFFER_SIZE (MAX_STRING_BUFFER + 160)

#ifndef KERNEL_LOG_DEFAULT_TAG_FILTER
#define KERNEL_LOG_DEFAULT_TAG_FILTER ""
#endif

typedef struct tag_KERNEL_LOG_RECENT_LINE {
    U32 StartOffset;
    U32 Length;
} KERNEL_LOG_RECENT_LINE, *LPKERNEL_LOG_RECENT_LINE;

typedef struct tag_KERNEL_LOG_STATE {
    STR DefaultTagFilter[KERNEL_LOG_TAG_FILTER_MAX_LENGTH];
    STR TagFilter[KERNEL_LOG_TAG_FILTER_MAX_LENGTH];
    U32 RecentSequence;
    U32 RecentWriteOffset;
    UINT RecentUsedBytes;
    UINT RecentLineHead;
    UINT RecentLineCount;
    U8 RecentText[KERNEL_LOG_RECENT_TEXT_BYTES];
    KERNEL_LOG_RECENT_LINE RecentLines[KERNEL_LOG_RECENT_MAX_LINES];
} KERNEL_LOG_STATE, *LPKERNEL_LOG_STATE;

static KERNEL_LOG_STATE DATA_SECTION KernelLogState = {
    .DefaultTagFilter = KERNEL_LOG_DEFAULT_TAG_FILTER,
    .TagFilter = {0},
    .RecentSequence = 0,
    .RecentWriteOffset = 0,
    .RecentUsedBytes = 0,
    .RecentLineHead = 0,
    .RecentLineCount = 0};
#if DEBUG_SPLIT == 1
static U32 DATA_SECTION KernelConsolePrintDepth = 0;
#endif

static UINT KernelLogDriverCommands(UINT Function, UINT Parameter);
static BOOL KernelLogIsTagSeparator(STR Char);
static BOOL KernelLogFilterContainsTag(LPCSTR Tag, U32 TagLength);
static BOOL KernelLogShouldEmit(LPCSTR FunctionName, LPCSTR Text);
static void KernelLogDropOldestRecentLine(void);
static void KernelLogAppendRecentLine(LPCSTR Text);
static BOOL KernelLogCopyRecentLineText(U8* Destination, UINT DestinationSize, UINT* Cursor, U32 StartOffset, U32 Length);
static void KernelLogWriteFormatted(U32 Type, LPCSTR FunctionName, LPCSTR Format, VarArgList Args);

DRIVER DATA_SECTION KernelLogDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = KERNEL_LOG_VER_MAJOR,
    .VersionMinor = KERNEL_LOG_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "N/A",
    .Product = "KernelLog",
    .Alias = "kernel_log",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = KernelLogDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the kernel log driver descriptor.
 * @return Pointer to the kernel log driver.
 */
LPDRIVER KernelLogGetDriver(void) {
    return &KernelLogDriver;
}

/**
 * @brief Initializes the kernel logging system.
 *
 * Sets up the serial port used for kernel log output by resetting
 * the designated communication port.
 */
void InitKernelLog(void) {
    SerialReset(LOG_COM_INDEX);
    KernelLogSetTagFilter(KernelLogState.DefaultTagFilter);
}

/************************************************************************/

/**
 * @brief Configure tag-based filtering for kernel logs.
 *
 * TagFilter accepts a list of tags separated with comma, semicolon, pipe,
 * or spaces. Tags can be written with or without brackets.
 *
 * @param TagFilter Filter string, empty to disable filtering.
 */
void KernelLogSetTagFilter(LPCSTR TagFilter) {
    U32 Flags;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    if (StringEmpty(TagFilter)) {
        StringClear(KernelLogState.TagFilter);
    } else {
        StringCopyLimit(
            KernelLogState.TagFilter,
            TagFilter,
            KERNEL_LOG_TAG_FILTER_MAX_LENGTH);
    }

    UnfreezeScheduler();
    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Return the current log tag filter string.
 * @return Active tag filter string (empty means no filter).
 */
LPCSTR KernelLogGetTagFilter(void) {
    return KernelLogState.TagFilter;
}

/************************************************************************/

/**
 * @brief Check whether a character separates filter tags.
 * @param Char Character to evaluate.
 * @return TRUE when Char is a separator, FALSE otherwise.
 */
static BOOL KernelLogIsTagSeparator(STR Char) {
    return Char == ',' || Char == ';' || Char == '|' ||
           Char == ' ' || Char == '\t' || Char == '\n' || Char == '\r';
}

/************************************************************************/

/**
 * @brief Check whether a tag is present in the active filter list.
 * @param Tag Tag extracted from a log line (without brackets).
 * @param TagLength Tag length in characters.
 * @return TRUE when tag is allowed by filter, FALSE otherwise.
 */
static BOOL KernelLogFilterContainsTag(LPCSTR Tag, U32 TagLength) {
    U32 Index = 0;

    if (Tag == NULL || TagLength == 0) {
        return FALSE;
    }

    while (KernelLogState.TagFilter[Index] != STR_NULL) {
        U32 Start;
        U32 End;
        U32 TokenLength;
        U32 TokenIndex;
        BOOL Match;

        while (KernelLogState.TagFilter[Index] != STR_NULL &&
               KernelLogIsTagSeparator(KernelLogState.TagFilter[Index])) {
            Index++;
        }
        if (KernelLogState.TagFilter[Index] == STR_NULL) {
            break;
        }

        Start = Index;
        while (KernelLogState.TagFilter[Index] != STR_NULL &&
               !KernelLogIsTagSeparator(KernelLogState.TagFilter[Index])) {
            Index++;
        }
        End = Index;

        if (KernelLogState.TagFilter[Start] == '[') {
            Start++;
        }
        if (End > Start && KernelLogState.TagFilter[End - 1] == ']') {
            End--;
        }

        TokenLength = End - Start;
        if (TokenLength != TagLength) {
            continue;
        }

        Match = TRUE;
        for (TokenIndex = 0; TokenIndex < TokenLength; TokenIndex++) {
            if (KernelLogState.TagFilter[Start + TokenIndex] != Tag[TokenIndex]) {
                Match = FALSE;
                break;
            }
        }

        if (Match) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Determines if a formatted log line passes the active tag filter.
 * @param FunctionName Function name supplied by the log macro.
 * @param Text Fully formatted log line.
 * @return TRUE when line should be emitted, FALSE otherwise.
 */
static BOOL KernelLogShouldEmit(LPCSTR FunctionName, LPCSTR Text) {
    LPSTR OpenBracket;
    LPSTR CloseBracket;
    U32 TagLength;

    if (StringEmpty(KernelLogState.TagFilter)) {
        return TRUE;
    }

    if (StringEmpty(FunctionName) == FALSE) {
        return KernelLogFilterContainsTag(FunctionName, (U32)StringLength(FunctionName));
    }

    if (StringEmpty(Text)) {
        return FALSE;
    }

    OpenBracket = StringFindChar(Text, '[');
    if (OpenBracket == NULL || OpenBracket[1] == STR_NULL) {
        return FALSE;
    }

    CloseBracket = StringFindChar(OpenBracket + 1, ']');
    if (CloseBracket == NULL) {
        return FALSE;
    }

    TagLength = (U32)(CloseBracket - (OpenBracket + 1));
    if (TagLength == 0) {
        return FALSE;
    }

    return KernelLogFilterContainsTag(OpenBracket + 1, TagLength);
}

/************************************************************************/

static void KernelPrintChar(STR Char) {
#if DEBUG_SPLIT == 1
    if (ConsoleIsDebugSplitEnabled() == TRUE &&
        ConsoleIsFramebufferMappingInProgress() == FALSE &&
        KernelConsolePrintDepth == 0) {
        KernelConsolePrintDepth++;
        ConsolePrintDebugChar(Char);
        KernelConsolePrintDepth--;
        SerialOut(LOG_COM_INDEX, Char);
        return;
    }
#endif

    SerialOut(LOG_COM_INDEX, Char);
}

/************************************************************************/

static void KernelPrintString(LPCSTR Text) {
    SAFE_USE(Text) {
        for (U32 Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }
}

/************************************************************************/

/**
 * @brief Remove the oldest retained recent log line.
 */
static void KernelLogDropOldestRecentLine(void) {
    KERNEL_LOG_RECENT_LINE* Line;

    if (KernelLogState.RecentLineCount == 0) return;

    Line = &(KernelLogState.RecentLines[KernelLogState.RecentLineHead]);
    if (KernelLogState.RecentUsedBytes >= Line->Length) {
        KernelLogState.RecentUsedBytes -= Line->Length;
    } else {
        KernelLogState.RecentUsedBytes = 0;
    }

    KernelLogState.RecentLineHead = (KernelLogState.RecentLineHead + 1) % KERNEL_LOG_RECENT_MAX_LINES;
    KernelLogState.RecentLineCount--;
}

/************************************************************************/

/**
 * @brief Append one fully formatted line to the retained recent log view.
 * @param Text Fully formatted line ending with newline.
 */
static void KernelLogAppendRecentLine(LPCSTR Text) {
    UINT Length;
    UINT TailIndex;
    UINT FirstChunk;

    if (StringEmpty(Text)) return;

    Length = StringLength(Text);
    if (Length == 0 || Length >= KERNEL_LOG_RECENT_TEXT_BYTES) return;

    while (KernelLogState.RecentLineCount >= KERNEL_LOG_RECENT_MAX_LINES ||
           (KernelLogState.RecentUsedBytes + Length) > KERNEL_LOG_RECENT_TEXT_BYTES) {
        KernelLogDropOldestRecentLine();
    }

    TailIndex = (KernelLogState.RecentLineHead + KernelLogState.RecentLineCount) % KERNEL_LOG_RECENT_MAX_LINES;
    KernelLogState.RecentLines[TailIndex].StartOffset = KernelLogState.RecentWriteOffset;
    KernelLogState.RecentLines[TailIndex].Length = (U32)Length;

    FirstChunk = KERNEL_LOG_RECENT_TEXT_BYTES - KernelLogState.RecentWriteOffset;
    if (FirstChunk > Length) {
        FirstChunk = Length;
    }

    MemoryCopy(&(KernelLogState.RecentText[KernelLogState.RecentWriteOffset]), Text, (U32)FirstChunk);
    if (Length > FirstChunk) {
        MemoryCopy(&(KernelLogState.RecentText[0]), ((const U8*)Text) + FirstChunk, (U32)(Length - FirstChunk));
    }

    KernelLogState.RecentWriteOffset = (KernelLogState.RecentWriteOffset + (U32)Length) % KERNEL_LOG_RECENT_TEXT_BYTES;
    KernelLogState.RecentUsedBytes += Length;
    KernelLogState.RecentLineCount++;
    KernelLogState.RecentSequence++;
}

/************************************************************************/

/**
 * @brief Copy one retained log line from circular storage to a linear buffer.
 * @param Destination Output byte buffer.
 * @param DestinationSize Output buffer size.
 * @param Cursor Current output cursor and updated output cursor.
 * @param StartOffset Circular buffer start offset.
 * @param Length Number of bytes to copy.
 * @return TRUE on success.
 */
static BOOL KernelLogCopyRecentLineText(U8* Destination, UINT DestinationSize, UINT* Cursor, U32 StartOffset, U32 Length) {
    UINT FirstChunk;

    if (Destination == NULL || Cursor == NULL) return FALSE;
    if ((*Cursor + Length + 1) > DestinationSize) return FALSE;

    FirstChunk = KERNEL_LOG_RECENT_TEXT_BYTES - StartOffset;
    if (FirstChunk > Length) {
        FirstChunk = Length;
    }

    MemoryCopy(Destination + *Cursor, &(KernelLogState.RecentText[StartOffset]), (U32)FirstChunk);
    if (Length > FirstChunk) {
        MemoryCopy(Destination + *Cursor + FirstChunk, &(KernelLogState.RecentText[0]), Length - FirstChunk);
    }

    *Cursor += Length;
    Destination[*Cursor] = 0;
    return TRUE;
}

/************************************************************************/

U32 KernelLogGetRecentSequence(void) {
    U32 Flags;
    U32 Sequence;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    Sequence = KernelLogState.RecentSequence;

    UnfreezeScheduler();
    RestoreFlags(&Flags);
    return Sequence;
}

/************************************************************************/

BOOL KernelLogCaptureRecentLines(LPKERNEL_LOG_RECENT_VIEW View) {
    U32 Flags;
    UINT Cursor = 0;
    UINT StartLine = 0;
    UINT Index;

    if (View == NULL || View->Text == NULL || View->TextBufferSize == 0) return FALSE;

    View->Sequence = 0;
    View->TotalLines = 0;
    View->CopiedLines = 0;
    View->Truncated = FALSE;
    View->Text[0] = 0;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    View->Sequence = KernelLogState.RecentSequence;
    View->TotalLines = KernelLogState.RecentLineCount;

    if (View->MaxLines != 0 && KernelLogState.RecentLineCount > View->MaxLines) {
        StartLine = KernelLogState.RecentLineCount - View->MaxLines;
    }

    for (Index = StartLine; Index < KernelLogState.RecentLineCount; Index++) {
        UINT LineIndex = (KernelLogState.RecentLineHead + Index) % KERNEL_LOG_RECENT_MAX_LINES;
        KERNEL_LOG_RECENT_LINE* Line = &(KernelLogState.RecentLines[LineIndex]);

        if (!KernelLogCopyRecentLineText((U8*)View->Text, View->TextBufferSize, &Cursor, Line->StartOffset, Line->Length)) {
            View->Truncated = TRUE;
            break;
        }

        View->CopiedLines++;
    }

    if (View->CopiedLines < (KernelLogState.RecentLineCount - StartLine)) {
        View->Truncated = TRUE;
    }

    UnfreezeScheduler();
    RestoreFlags(&Flags);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Driver command handler for the kernel log subsystem.
 *
 * DF_LOAD initializes the kernel logger once; DF_UNLOAD only clears readiness.
 */
static UINT KernelLogDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((KernelLogDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitKernelLog();
            KernelLogDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((KernelLogDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            KernelLogDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(KERNEL_LOG_VER_MAJOR, KERNEL_LOG_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Writes formatted text to the kernel log.
 *
 * Outputs timestamped log messages to the serial port. The function is
 * thread-safe and disables interrupts during output to ensure atomic logging.
 * Supports printf-style format strings with variable arguments.
 *
 * @param Type Log message type/severity level
 * @param FunctionName Function name to use as the log tag
 * @param Format Printf-style format string
 * @param Args Variable arguments for format string
 */
static void KernelLogWriteFormatted(U32 Type, LPCSTR FunctionName, LPCSTR Format, VarArgList Args) {
    if (StringEmpty(Format)) return;

    U32 Flags;
    LPCSTR Prefix = TEXT("VERBOSE > ");

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    STR TimeBuffer[128];
    STR TextBuffer[MAX_STRING_BUFFER];
    STR MessageBuffer[KERNEL_LOG_ENTRY_BUFFER_SIZE];
    STR EntryBuffer[KERNEL_LOG_ENTRY_BUFFER_SIZE];

    UINT Time = GetSystemTime();
    StringPrintFormat(TimeBuffer, TEXT("T%u> "), (U32)Time);

    StringPrintFormatArgs(TextBuffer, Format, Args);

    if (!KernelLogShouldEmit(FunctionName, TextBuffer)) {
        UnfreezeScheduler();
        RestoreFlags(&Flags);
        return;
    }

    if (StringEmpty(FunctionName) == FALSE) {
        StringPrintFormat(MessageBuffer, TEXT("[%s] %s"), FunctionName, TextBuffer);
    } else {
        StringCopy(MessageBuffer, TextBuffer);
    }

    switch (Type) {
        case LOG_DEBUG: {
            Prefix = TEXT("DEBUG > ");
        } break;

        case LOG_TEST: {
            Prefix = TEXT("TEST > ");
        } break;

        default:
        case LOG_VERBOSE: {
            ConsolePrint(MessageBuffer);
            ConsolePrint(Text_NewLine);
        } break;

        case LOG_WARNING: {
            Prefix = TEXT("WARNING > ");
        } break;

        case LOG_ERROR: {
            Prefix = TEXT("ERROR > ");
        } break;
    }

    StringPrintFormat(EntryBuffer, TEXT("%s%s%s\n"), TimeBuffer, Prefix, MessageBuffer);
    KernelPrintString(EntryBuffer);
    KernelLogAppendRecentLine(EntryBuffer);

    UnfreezeScheduler();
    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Logs formatted text to the kernel log with an explicit function tag.
 *
 * @param Type Log message type/severity level
 * @param FunctionName Function name to use as the log tag
 * @param Format Printf-style format string
 * @param ... Variable arguments for format string
 */
void KernelLogTextFromFunction(U32 Type, LPCSTR FunctionName, LPCSTR Format, ...) {
    VarArgList Args;

    VarArgStart(Args, Format);
    KernelLogWriteFormatted(Type, FunctionName, Format, Args);
    VarArgEnd(Args);
}

/************************************************************************/

/**
 * @brief Logs formatted text to the kernel log without an explicit function tag.
 *
 * This entry point serves non-C callers. C code uses the KernelLogText macro,
 * which injects the caller function name.
 *
 * @param Type Log message type/severity level
 * @param Format Printf-style format string
 * @param ... Variable arguments for format string
 */
void KernelLogText(U32 Type, LPCSTR Format, ...) {
    VarArgList Args;

    VarArgStart(Args, Format);
    KernelLogWriteFormatted(Type, NULL, Format, Args);
    VarArgEnd(Args);
}

/************************************************************************/

/**
 * @brief Logs a memory dump to the kernel log.
 *
 * Outputs a hexadecimal dump of memory contents to the kernel log.
 * The memory is displayed in lines of 8 32-bit values each, with
 * addresses and hex values formatted for easy reading.
 *
 * @param Type Log message type/severity level
 * @param Memory Starting address of memory to dump
 * @param Size Number of bytes to dump
 */
void KernelLogMem(U32 Type, LINEAR Memory, U32 Size) {
    U32* Pointer = (U32*)Memory;
    U32 LineCount = Size / (sizeof(U32) * 8);

    if (LineCount < 1) LineCount = 1;

    for (U32 Line = 0; Line < LineCount; Line++) {
        if (IsValidMemory((LINEAR)Pointer) == FALSE) return;
        if (IsValidMemory((LINEAR)(Pointer + 31)) == FALSE) return;

        KernelLogTextFromFunction(Type,
                                  TEXT(__func__),
                                  TEXT("%08x : %08x %08x %08x %08x %08x %08x %08x %08x"),
                                  Pointer,
                                  Pointer[0],
                                  Pointer[1],
                                  Pointer[2],
                                  Pointer[3],
                                  Pointer[4],
                                  Pointer[5],
                                  Pointer[6],
                                  Pointer[7]);
        Pointer += 4;
    }
}

/************************************************************************/
