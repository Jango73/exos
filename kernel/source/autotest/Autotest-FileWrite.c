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


    File write all-or-fail - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"

#include "text/CoreString.h"
#include "fs/File.h"
#include "memory/Heap.h"
#include "log/Log.h"

/************************************************************************/

/**
 * @brief Assert one condition in file write tests.
 * @param Condition Condition to evaluate.
 * @param Results Test results accumulator.
 * @param Label Assertion label used in logs.
 */
static void TestFileWriteAssert(BOOL Condition, LPTEST_RESULTS Results, LPCSTR Label) {
    if (Results == NULL) {
        return;
    }

    Results->TestsRun++;
    if (Condition) {
        Results->TestsPassed++;
    } else {
        ERROR(TEXT("Assertion failed: %s"), Label);
    }
}

/************************************************************************/

/**
 * @brief Validate file write all-or-fail contract with one large mono-write.
 * @param Results Test result accumulator.
 */
void TestFileWriteAllOrFail(TEST_RESULTS* Results) {
    STR Path[MAX_PATH_NAME];
    U8* WriteBuffer = NULL;
    U8* ReadBuffer = NULL;
    UINT ReadSize = 0;
    UINT Size = 0;
    UINT Written = 0;
    UINT Index = 0;
    BOOL Match = TRUE;

    if (Results == NULL) {
        return;
    }

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Size = (64 * 1024) + 123;
    StringCopy(Path, TEXT("/temp/autotest-file-write-all.bin"));

    WriteBuffer = (U8*)KernelHeapAlloc(Size);
    TestFileWriteAssert(WriteBuffer != NULL, Results, TEXT("allocate_write_buffer"));
    if (WriteBuffer == NULL) {
        return;
    }

    for (Index = 0; Index < Size; Index++) {
        WriteBuffer[Index] = (U8)((Index * 37 + 13) & 0xFF);
    }

    Written = FileWriteAll(Path, WriteBuffer, Size);
    TestFileWriteAssert(Written == Size, Results, TEXT("write_all_or_fail"));
    if (Written != Size) {
        KernelHeapFree(WriteBuffer);
        return;
    }

    ReadBuffer = (U8*)FileReadAll(Path, &ReadSize);
    TestFileWriteAssert(ReadBuffer != NULL, Results, TEXT("read_back_buffer"));
    if (ReadBuffer == NULL) {
        KernelHeapFree(WriteBuffer);
        return;
    }

    TestFileWriteAssert(ReadSize == Size, Results, TEXT("read_size_match"));

    for (Index = 0; Index < Size && Match; Index++) {
        if (WriteBuffer[Index] != ReadBuffer[Index]) {
            Match = FALSE;
        }
    }
    TestFileWriteAssert(Match, Results, TEXT("read_content_match"));

    KernelHeapFree(ReadBuffer);
    KernelHeapFree(WriteBuffer);
}

/************************************************************************/

