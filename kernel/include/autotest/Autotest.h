
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

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


    Autotest Manager - Unit Testing Framework Header

\************************************************************************/

#ifndef AUTOTEST_H
#define AUTOTEST_H

/************************************************************************/

#include "Base.h"

/************************************************************************/

// Test results structure
typedef struct tag_TEST_RESULTS {
    U32 TestsRun;     // Number of tests/assertions executed
    U32 TestsPassed;  // Number of successful tests/assertions
} TEST_RESULTS, *LPTEST_RESULTS;

// Main testing functions
BOOL RunAllTests(void);
BOOL RunSingleTestByName(LPCSTR TestName);
void ListAllTests(void);

// Individual test function declarations - new signature
typedef void (*TestFunction)(TEST_RESULTS*);

void TestCopyStack(TEST_RESULTS* Results);
void TestCircularBuffer(TEST_RESULTS* Results);
void TestBlockList(TEST_RESULTS* Results);
void TestRadixTree(TEST_RESULTS* Results);
void TestRegex(TEST_RESULTS* Results);
void TestX86_32Disassembler(TEST_RESULTS* Results);
void TestBcrypt(TEST_RESULTS* Results);
void TestE1000(TEST_RESULTS* Results);
void TestIPv4(TEST_RESULTS* Results);
void TestMacros(TEST_RESULTS* Results);
void TestPackageManifest(TEST_RESULTS* Results);
void TestFileWriteAllOrFail(TEST_RESULTS* Results);
void TestTCP(TEST_RESULTS* Results);
void TestUDP(TEST_RESULTS* Results);
void TestDNS(TEST_RESULTS* Results);
void TestScript(TEST_RESULTS* Results);

/************************************************************************/

#endif
