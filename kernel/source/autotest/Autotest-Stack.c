
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


    Stack operations - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "memory/Memory.h"
#include "process/Stack.h"
#include "text/CoreString.h"

/************************************************************************/

#define TEST_STACK_SIZE 256

/************************************************************************/

/**
 * @brief Unit test for stack copying functionality.
 *
 * This function creates test stack frames with known EBP values and verifies
 * that CopyStackWithEBP correctly adjusts frame pointers while preserving
 * return addresses and other stack content. Tests both in-range and out-of-range
 * EBP values to ensure proper boundary handling.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestCopyStack(TEST_RESULTS* Results) {
    U8 SourceStack[TEST_STACK_SIZE];
    U8 DestStack[TEST_STACK_SIZE];
    U32 *SourcePtr, *DestPtr;
    LINEAR SourceStackTop, DestStackTop;
    I32 Delta;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    SourceStackTop = (LINEAR)(SourceStack + TEST_STACK_SIZE);
    DestStackTop = (LINEAR)(DestStack + TEST_STACK_SIZE);
    Delta = DestStackTop - SourceStackTop;

    MemorySet(SourceStack, 0xAA, TEST_STACK_SIZE);
    MemorySet(DestStack, 0x55, TEST_STACK_SIZE);

    SourcePtr = (U32 *)(SourceStackTop - 16);  // Frame 1 EBP
    *SourcePtr = (U32)(SourceStackTop - 32);   // Points to Frame 2
    SourcePtr = (U32 *)(SourceStackTop - 12);  // Frame 1 return addr
    *SourcePtr = 0x12345678;

    SourcePtr = (U32 *)(SourceStackTop - 32);  // Frame 2 EBP
    *SourcePtr = (U32)(SourceStackTop - 48);   // Points to Frame 3
    SourcePtr = (U32 *)(SourceStackTop - 28);  // Frame 2 return addr
    *SourcePtr = 0x9ABCDEF0;

    SourcePtr = (U32 *)(SourceStackTop - 48);  // Frame 3 EBP
    *SourcePtr = 0x1000;                       // Points outside stack (should not be adjusted)
    SourcePtr = (U32 *)(SourceStackTop - 44);  // Frame 3 return addr
    *SourcePtr = 0xDEADBEEF;

    // Test 1: CopyStackWithEBP operation
    Results->TestsRun++;
    if (!CopyStackWithEBP(DestStackTop, SourceStackTop, TEST_STACK_SIZE, (LINEAR)(SourceStackTop - 16))) {
        DEBUG(TEXT("CopyStack failed"));
        return;
    }
    Results->TestsPassed++;

    // Test 2: Frame 1 EBP adjustment
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 16);  // Frame 1 EBP in dest
    U32 ExpectedEbp1 = (SourceStackTop - 32) + Delta;
    if (*DestPtr == ExpectedEbp1) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 1 EBP: expected %X, got %X"), ExpectedEbp1, *DestPtr);
    }

    // Test 3: Frame 1 return address preservation
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 12);  // Frame 1 return addr
    if (*DestPtr == 0x12345678) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 1 return addr: expected 0x12345678, got %X"), *DestPtr);
    }

    // Test 4: Frame 2 EBP adjustment
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 32);  // Frame 2 EBP in dest
    U32 ExpectedEbp2 = (SourceStackTop - 48) + Delta;
    if (*DestPtr == ExpectedEbp2) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 2 EBP: expected %X, got %X"), ExpectedEbp2, *DestPtr);
    }

    // Test 5: Frame 2 return address preservation
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 28);  // Frame 2 return addr
    if (*DestPtr == 0x9ABCDEF0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 2 return addr: expected 0x9ABCDEF0, got %X"), *DestPtr);
    }

    // Test 6: Frame 3 EBP not adjusted (out of range)
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 48);  // Frame 3 EBP in dest (should NOT be adjusted)
    if (*DestPtr == 0x1000) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 3 EBP: expected 0x1000 (unchanged), got %X"), *DestPtr);
    }

    // Test 7: Frame 3 return address preservation
    Results->TestsRun++;
    DestPtr = (U32 *)(DestStackTop - 44);  // Frame 3 return addr
    if (*DestPtr == 0xDEADBEEF) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Frame 3 return addr: expected 0xDEADBEEF, got %X"), *DestPtr);
    }

    // Test 8: Non-frame data integrity
    Results->TestsRun++;
    BOOL DataIntact = TRUE;
    for (U32 i = 0; i < TEST_STACK_SIZE - 48; i++) {
        if (DestStack[i] != 0xAA) {
            DEBUG(TEXT(
                "[TestCopyStack] Non-frame data corrupted at offset %u: expected 0xAA, got %X"), i,
                DestStack[i]);
            DataIntact = FALSE;
            break;
        }
    }
    if (DataIntact) {
        Results->TestsPassed++;
    }
}
