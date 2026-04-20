
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


    Autotest Manager - Unit Testing Framework

\************************************************************************/

#include "autotest/Autotest.h"

#include "Base.h"
#include "core/Kernel.h"
#include "log/Log.h"

/************************************************************************/

// Test registry structure
typedef struct {
    LPCSTR Name;        // Test name for logging
    TestFunction Func;  // Test function pointer
    BOOL RunOnBoot;     // Include in RunAllTests at boot time
} TESTENTRY;

/************************************************************************/

// Test functions are declared in Autotest.h

// Test registry - add new tests here
static TESTENTRY TestRegistry[] = {
    {TEXT("TestCopyStack"), TestCopyStack, FALSE},
    {TEXT("TestCircularBuffer"), TestCircularBuffer, TRUE},
    {TEXT("TestBlockList"), TestBlockList, TRUE},
    {TEXT("TestRadixTree"), TestRadixTree, TRUE},
    {TEXT("TestRegex"), TestRegex, TRUE},
    {TEXT("TestX86_32Disassembler"), TestX86_32Disassembler, TRUE},
    {TEXT("TestBcrypt"), TestBcrypt, TRUE},
    {TEXT("TestIPv4"), TestIPv4, TRUE},
    {TEXT("TestMacros"), TestMacros, TRUE},
    {TEXT("TestPackageManifest"), TestPackageManifest, TRUE},
    {TEXT("TestTCP"), TestTCP, TRUE},
    {TEXT("TestScript"), TestScript, TRUE},
    // Add new tests here following the same pattern
    // { TEXT("TestName"), TestFunctionName },
    {NULL, NULL, FALSE}  // End marker
};

/************************************************************************/

/**
 * @brief Counts the number of registered tests.
 *
 * @return Number of tests in the registry
 */
static U32 CountTests(void) {
    U32 Count = 0;

    while (TestRegistry[Count].Name != NULL) {
        Count++;
    }

    return Count;
}

/************************************************************************/

/**
 * @brief Runs a single test and reports the result.
 *
 * @param Entry Test registry entry containing name and function pointer
 * @param Results Pointer to TEST_RESULTS structure to be filled
 */
static void RunSingleTest(const TESTENTRY* Entry, TEST_RESULTS* Results) {
    TEST_RESULTS TestResults = {0, 0};

    DEBUG(TEXT("Running test: %s"), Entry->Name);

    // Run the test function
    Entry->Func(&TestResults);

    // Update overall results
    Results->TestsRun += TestResults.TestsRun;
    Results->TestsPassed += TestResults.TestsPassed;

    // Log results
    DEBUG(TEXT("%s: %u/%u passed"), Entry->Name, TestResults.TestsPassed, TestResults.TestsRun);
}

/************************************************************************/

/**
 * @brief Runs all registered unit tests.
 *
 * This function discovers and executes all tests in the autotest registry.
 * It provides a summary of test results including pass/fail counts and
 * overall test suite status.
 *
 * @return TRUE if all tests passed, FALSE if any test failed
 */
BOOL RunAllTests(void) {
    U32 TotalTestModules = CountTests();
    U32 Index = 0;
    TEST_RESULTS OverallResults = {0, 0};
    BOOL AllPassed = TRUE;

    UNUSED(TotalTestModules);

    DEBUG(TEXT("==========================================================================="));
    DEBUG(TEXT("Starting Test Suite"));
    DEBUG(TEXT("Found %u test modules to run"), TotalTestModules);
    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_BEGIN"));

    // Run each test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        if (TestRegistry[Index].RunOnBoot == FALSE) {
            continue;
        }
        RunSingleTest(&TestRegistry[Index], &OverallResults);
    }

    DEBUG(TEXT("AUTOTEST_ERROR_SCOPE_END"));

    // Determine overall pass/fail status
    AllPassed = (OverallResults.TestsRun == OverallResults.TestsPassed);

    // Print summary
    DEBUG(TEXT("Test Suite Complete"));
    DEBUG(TEXT("Tests Run: %u, Tests Passed: %u"), OverallResults.TestsRun, OverallResults.TestsPassed);

    if (AllPassed) {
        DEBUG(TEXT("ALL TESTS PASSED"));
    } else {
        WARNING(TEXT("SOME TESTS FAILED (%u failures)"), OverallResults.TestsRun - OverallResults.TestsPassed);
    }

    DEBUG(TEXT("==========================================================================="));

    return AllPassed;
}

/************************************************************************/

/**
 * @brief Runs a specific test by name.
 *
 * Searches the test registry for a test with the specified name and runs it.
 * Useful for debugging specific failing tests or running tests selectively.
 *
 * @param TestName Name of the test to run
 * @return TRUE if test exists and passed, FALSE if not found or failed
 */
BOOL RunSingleTestByName(LPCSTR TestName) {
    U32 Index = 0;
    TEST_RESULTS TestResults = {0, 0};

    if (TestName == NULL) {
        DEBUG(TEXT("Test name is NULL"));
        return FALSE;
    }

    DEBUG(TEXT("Looking for test: %s"), TestName);

    // Search for the test in the registry
    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        if (STRINGS_EQUAL(TestRegistry[Index].Name, TestName)) {
            DEBUG(TEXT("Found test: %s"), TestName);
            RunSingleTest(&TestRegistry[Index], &TestResults);
            return (TestResults.TestsRun == TestResults.TestsPassed);
        }
    }

    DEBUG(TEXT("Test not found: %s"), TestName);
    return FALSE;
}

/************************************************************************/
/**
 * @brief Lists all available tests in the registry.
 *
 * Prints the names of all registered tests to the kernel log.
 * Useful for discovering what tests are available to run.
 */
void ListAllTests(void) {
    U32 TotalTests = CountTests();
    U32 Index = 0;

    (void)TotalTests;

    DEBUG(TEXT("Available tests (%u total):"), TotalTests);

    for (Index = 0; TestRegistry[Index].Name != NULL; Index++) {
        DEBUG(TEXT("%u. %s"), Index + 1, TestRegistry[Index].Name);
    }
}
