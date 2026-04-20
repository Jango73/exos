
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


    Circular Buffer - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "utils/CircularBuffer.h"
#include "memory/Heap.h"
#include "text/CoreString.h"

/************************************************************************/

#define CANARY_VALUE ((U32)0xC1A5C0DE)

/************************************************************************/

static void FillPattern(U8* Buffer, U32 Length, U32* State) {
    if (!Buffer || !State) {
        return;
    }

    U32 LocalState = *State;
    U32 Index = 0;

    for (Index = 0; Index < Length; Index++) {
        LocalState = (LocalState * 1664525U) + 1013904223U;
        Buffer[Index] = (U8)(LocalState >> 24);
    }

    *State = LocalState;
}

/************************************************************************/

static BOOL CheckCanaries(const U32* Front, const U32* Back, LPCSTR Context) {
    UNUSED(Context);
    if (!Front || !Back) {
        return FALSE;
    }

    if (*Front != CANARY_VALUE || *Back != CANARY_VALUE) {
        DEBUG(TEXT("Canary corrupted in %s (front=%08X back=%08X)"), Context, *Front, *Back);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

void TestCircularBuffer(TEST_RESULTS* Results) {
    if (!Results) {
        return;
    }

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Basic write and read
    Results->TestsRun++;
    {
        struct {
            U32 FrontCanary;
            U8 Data[256];
            U32 BackCanary;
        } Storage = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[256];
            U32 BackCanary;
        } Input = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[256];
            U32 BackCanary;
        } Output = {CANARY_VALUE, {0}, CANARY_VALUE};

        CIRCULAR_BUFFER Buffer;
        U32 PatternState = 0x13579BDFU;
        FillPattern(Input.Data, 200, &PatternState);

        CircularBuffer_Initialize(&Buffer, Storage.Data, sizeof(Storage.Data), sizeof(Storage.Data));

        U32 Written = CircularBuffer_Write(&Buffer, Input.Data, 200);
        U32 AvailableAfterWrite = CircularBuffer_GetAvailableData(&Buffer);
        U32 Read = CircularBuffer_Read(&Buffer, Output.Data, 200);
        U32 AvailableAfterRead = CircularBuffer_GetAvailableData(&Buffer);

        if (Written == 200 &&
            Read == 200 &&
            AvailableAfterWrite == 200 &&
            AvailableAfterRead == 0 &&
            MemoryCompare(Input.Data, Output.Data, 200) == 0 &&
            CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("basic storage")) &&
            CheckCanaries(&Input.FrontCanary, &Input.BackCanary, TEXT("basic input")) &&
            CheckCanaries(&Output.FrontCanary, &Output.BackCanary, TEXT("basic output"))) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Basic write/read failed (written=%u read=%u data=%u/%u)"),
                  Written,
                  Read,
                  AvailableAfterWrite,
                  AvailableAfterRead);
        }
    }

    // Test 2: Wrap-around behaviour
    Results->TestsRun++;
    {
        struct {
            U32 FrontCanary;
            U8 Data[128];
            U32 BackCanary;
        } Storage = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[128];
            U32 BackCanary;
        } SourceA = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[128];
            U32 BackCanary;
        } SourceB = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[192];
            U32 BackCanary;
        } Output = {CANARY_VALUE, {0}, CANARY_VALUE};

        CIRCULAR_BUFFER Buffer;
        U32 PatternState = 0x2468ACE0U;

        FillPattern(SourceA.Data, 96, &PatternState);
        FillPattern(SourceB.Data, 80, &PatternState);

        CircularBuffer_Initialize(&Buffer, Storage.Data, sizeof(Storage.Data), sizeof(Storage.Data));

        U32 FirstWrite = CircularBuffer_Write(&Buffer, SourceA.Data, 96);
        U32 FirstRead = CircularBuffer_Read(&Buffer, Output.Data, 64);
        U32 SecondWrite = CircularBuffer_Write(&Buffer, SourceB.Data, 80);
        U32 CombinedAvailable = CircularBuffer_GetAvailableData(&Buffer);
        U32 SecondRead = CircularBuffer_Read(&Buffer, Output.Data + 64, 128);

        BOOL DataValid = TRUE;

        if (MemoryCompare(Output.Data, SourceA.Data, 64) != 0) {
            DataValid = FALSE;
        }
        if (MemoryCompare(Output.Data + 64, SourceA.Data + 64, 32) != 0) {
            DataValid = FALSE;
        }
        if (MemoryCompare(Output.Data + 96, SourceB.Data, 80) != 0) {
            DataValid = FALSE;
        }

        if (FirstWrite == 96 &&
            FirstRead == 64 &&
            SecondWrite == 80 &&
            CombinedAvailable == 112 &&
            SecondRead == 112 &&
            DataValid &&
            CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("wrap storage")) &&
            CheckCanaries(&SourceA.FrontCanary, &SourceA.BackCanary, TEXT("wrap source A")) &&
            CheckCanaries(&SourceB.FrontCanary, &SourceB.BackCanary, TEXT("wrap source B")) &&
            CheckCanaries(&Output.FrontCanary, &Output.BackCanary, TEXT("wrap output"))) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Wrap-around failed (W1=%u R1=%u W2=%u avail=%u R2=%u valid=%u)"),
                  FirstWrite,
                  FirstRead,
                  SecondWrite,
                  CombinedAvailable,
                  SecondRead,
                  DataValid);
        }
    }

    // Test 3: Automatic growth preserves content
    Results->TestsRun++;
    {
        struct {
            U32 FrontCanary;
            U8 Data[64];
            U32 BackCanary;
        } Storage = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[192];
            U32 BackCanary;
        } Input = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[192];
            U32 BackCanary;
        } Output = {CANARY_VALUE, {0}, CANARY_VALUE};

        CIRCULAR_BUFFER Buffer;
        U32 PatternState = 0x0F1E2D3CU;

        FillPattern(Input.Data, 192, &PatternState);

        CircularBuffer_Initialize(&Buffer, Storage.Data, sizeof(Storage.Data), 256);

        U32 Written = CircularBuffer_Write(&Buffer, Input.Data, 192);
        U32 SizeAfterGrowth = Buffer.Size;
        BOOL AllocationSucceeded = (Buffer.AllocatedData != NULL);
        U32 Read = CircularBuffer_Read(&Buffer, Output.Data, 192);
        BOOL DataValid = (MemoryCompare(Input.Data, Output.Data, 192) == 0);

        if (Written == 192 &&
            Read == 192 &&
            SizeAfterGrowth >= 192 &&
            AllocationSucceeded &&
            DataValid &&
            CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("growth storage")) &&
            CheckCanaries(&Input.FrontCanary, &Input.BackCanary, TEXT("growth input")) &&
            CheckCanaries(&Output.FrontCanary, &Output.BackCanary, TEXT("growth output"))) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Growth failed (written=%u read=%u size=%u alloc=%u valid=%u)"),
                  Written,
                  Read,
                  SizeAfterGrowth,
                  AllocationSucceeded,
                  DataValid);
        }

        if (Buffer.AllocatedData) {
            KernelHeapFree(Buffer.AllocatedData);
            Buffer.AllocatedData = NULL;
            Buffer.Data = Storage.Data;
            Buffer.Size = sizeof(Storage.Data);
        }
    }

    // Test 4: Overflow detection and reset
    Results->TestsRun++;
    {
        struct {
            U32 FrontCanary;
            U8 Data[64];
            U32 BackCanary;
        } Storage = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[64];
            U32 BackCanary;
        } Input = {CANARY_VALUE, {0}, CANARY_VALUE};

        CIRCULAR_BUFFER Buffer;
        U32 PatternState = 0x89ABCDEFU;

        FillPattern(Input.Data, 64, &PatternState);

        CircularBuffer_Initialize(&Buffer, Storage.Data, sizeof(Storage.Data), sizeof(Storage.Data));

        U32 Written = CircularBuffer_Write(&Buffer, Input.Data, 64);
        U32 OverflowAttempt = CircularBuffer_Write(&Buffer, Input.Data, 32);
        BOOL OverflowFlagged = Buffer.Overflowed;

        CircularBuffer_Reset(&Buffer);

        if (Written == 64 &&
            OverflowAttempt == 0 &&
            OverflowFlagged &&
            Buffer.DataLength == 0 &&
            Buffer.Overflowed == FALSE &&
            Buffer.ReadOffset == 0 &&
            Buffer.WriteOffset == 0 &&
            CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("overflow storage")) &&
            CheckCanaries(&Input.FrontCanary, &Input.BackCanary, TEXT("overflow input"))) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Overflow/reset failed (written=%u overflow=%u flag=%u length=%u)"),
                  Written,
                  OverflowAttempt,
                  OverflowFlagged,
                  Buffer.DataLength);
        }
    }

    // Test 5: Stress alternating writes and reads
    Results->TestsRun++;
    {
        struct {
            U32 FrontCanary;
            U8 Data[128];
            U32 BackCanary;
        } Storage = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[1024];
            U32 BackCanary;
        } Reference = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[256];
            U32 BackCanary;
        } WriteChunk = {CANARY_VALUE, {0}, CANARY_VALUE};

        struct {
            U32 FrontCanary;
            U8 Data[256];
            U32 BackCanary;
        } ReadChunk = {CANARY_VALUE, {0}, CANARY_VALUE};

        CIRCULAR_BUFFER Buffer;
        U32 PatternState = 0x10203040U;
        U32 Pending = 0;
        BOOL StressValid = TRUE;
        U32 Iteration = 0;

        CircularBuffer_Initialize(&Buffer, Storage.Data, sizeof(Storage.Data), 1024);

        for (Iteration = 0; Iteration < 64 && StressValid; Iteration++) {
            U32 WriteSize = ((Iteration * 37U) % 200U) + 1U;
            if (WriteSize + Pending > sizeof(Reference.Data)) {
                WriteSize = sizeof(Reference.Data) - Pending;
            }

            if (WriteSize > 0) {
                FillPattern(WriteChunk.Data, WriteSize, &PatternState);
                U32 Written = CircularBuffer_Write(&Buffer, WriteChunk.Data, WriteSize);
                if (Written != WriteSize) {
                    StressValid = FALSE;
                }

                MemoryCopy(&Reference.Data[Pending], WriteChunk.Data, WriteSize);
                Pending += WriteSize;

                if (!CheckCanaries(&WriteChunk.FrontCanary, &WriteChunk.BackCanary, TEXT("stress write chunk"))) {
                    StressValid = FALSE;
                }
            }

            U32 Available = CircularBuffer_GetAvailableData(&Buffer);
            U32 ReadSize = ((Iteration * 19U) % 180U) + 1U;
            if (ReadSize > Available) {
                ReadSize = Available;
            }

            if (ReadSize > 0) {
                U32 Read = CircularBuffer_Read(&Buffer, ReadChunk.Data, ReadSize);
                if (Read != ReadSize) {
                    StressValid = FALSE;
                }

                if (MemoryCompare(ReadChunk.Data, Reference.Data, ReadSize) != 0) {
                    StressValid = FALSE;
                }

                if (Pending >= ReadSize) {
                    MemoryMove(Reference.Data, &Reference.Data[ReadSize], Pending - ReadSize);
                    Pending -= ReadSize;
                } else {
                    StressValid = FALSE;
                    Pending = 0;
                }

                if (!CheckCanaries(&ReadChunk.FrontCanary, &ReadChunk.BackCanary, TEXT("stress read chunk"))) {
                    StressValid = FALSE;
                }
            }

            if (!CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("stress storage"))) {
                StressValid = FALSE;
            }

            if (!CheckCanaries(&Reference.FrontCanary, &Reference.BackCanary, TEXT("stress reference"))) {
                StressValid = FALSE;
            }
        }

        while (Pending > 0 && StressValid) {
            U32 ReadSize = (Pending > sizeof(ReadChunk.Data)) ? sizeof(ReadChunk.Data) : Pending;
            U32 Read = CircularBuffer_Read(&Buffer, ReadChunk.Data, ReadSize);

            if (Read != ReadSize) {
                StressValid = FALSE;
                break;
            }

            if (MemoryCompare(ReadChunk.Data, Reference.Data, ReadSize) != 0) {
                StressValid = FALSE;
                break;
            }

            if (Pending >= ReadSize) {
                MemoryMove(Reference.Data, &Reference.Data[ReadSize], Pending - ReadSize);
                Pending -= ReadSize;
            } else {
                StressValid = FALSE;
                Pending = 0;
            }
        }

        if (CircularBuffer_GetAvailableData(&Buffer) != 0 || Pending != 0) {
            StressValid = FALSE;
        }

        if (StressValid &&
            CheckCanaries(&Storage.FrontCanary, &Storage.BackCanary, TEXT("final stress storage")) &&
            CheckCanaries(&Reference.FrontCanary, &Reference.BackCanary, TEXT("final stress reference")) &&
            CheckCanaries(&WriteChunk.FrontCanary, &WriteChunk.BackCanary, TEXT("final stress write")) &&
            CheckCanaries(&ReadChunk.FrontCanary, &ReadChunk.BackCanary, TEXT("final stress read"))) {
            Results->TestsPassed++;
        } else {
            ERROR(TEXT("Stress scenario failed (pending=%u available=%u iteration=%u)"),
                  Pending,
                  CircularBuffer_GetAvailableData(&Buffer),
                  Iteration);
        }

        if (Buffer.AllocatedData) {
            KernelHeapFree(Buffer.AllocatedData);
            Buffer.AllocatedData = NULL;
            Buffer.Data = Storage.Data;
            Buffer.Size = sizeof(Storage.Data);
        }
    }
}
