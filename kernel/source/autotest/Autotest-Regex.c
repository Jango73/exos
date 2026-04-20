
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


    Regular Expression - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "utils/Regex.h"
#include "text/CoreString.h"

/************************************************************************/

/**
 * @brief Helper function to test a single regex pattern against text.
 *
 * Tests both regex matching (full text match) and searching (partial match).
 * Logs detailed results including match status and search span positions.
 *
 * @param Pattern Regular expression pattern to test
 * @param Text Input text to match against
 * @param ExpectedMatch Expected result for full match test
 * @param ExpectedSearch Expected result for search test
 * @return TRUE if both match and search results meet expectations
 */
static BOOL TestSingleRegex(LPCSTR Pattern, LPCSTR Text, BOOL ExpectedMatch, BOOL ExpectedSearch, TEST_RESULTS* Results) {
    REGEX Rx;
    BOOL TestPassed = TRUE;

    // Compile the regex pattern
    BOOL CompileOk = RegexCompile(Pattern, &Rx);
    if (!CompileOk) {
        DEBUG(TEXT("Regex compile failed: %s"), Pattern);
        return FALSE;
    }

    // Test full match
    BOOL Match = RegexMatch(&Rx, Text);
    if (Match != ExpectedMatch) {
        DEBUG(TEXT("Match test failed: pattern=\"%s\", text=\"%s\", expected=%s, got=%s"), Pattern,
            Text, ExpectedMatch ? TEXT("TRUE") : TEXT("FALSE"), Match ? TEXT("TRUE") : TEXT("FALSE"));
        TestPassed = FALSE;
    }

    // Test search with position tracking
    U32 Start = 0, End = 0;
    BOOL Search = RegexSearch(&Rx, Text, &Start, &End);
    if (Search != ExpectedSearch) {
        DEBUG(TEXT("Search test failed: pattern=\"%s\", text=\"%s\", expected=%s, got=%s"),
            Pattern, Text, ExpectedSearch ? TEXT("TRUE") : TEXT("FALSE"), Search ? TEXT("TRUE") : TEXT("FALSE"));
        TestPassed = FALSE;
    }

    Results->TestsRun++;
    if (TestPassed) Results->TestsPassed++;
    return TestPassed;
}

/**
 * @brief Comprehensive unit test for regular expression functionality.
 *
 * Tests various regex patterns including character classes, quantifiers,
 * anchors, and special characters. Validates both full matching and
 * substring searching capabilities of the regex engine.
 *
 * @return TRUE if all regex tests pass, FALSE if any test fails
 */
void TestRegex(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Valid identifier pattern (should match)
    TestSingleRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("Hello_123"), TRUE, TRUE, Results);

    // Test 2: Invalid identifier (starts with number - should not match)
    TestSingleRegex(TEXT("^[A-Za-z_][A-Za-z0-9_]*$"), TEXT("123Oops"), FALSE, FALSE, Results);

    // Test 3: Wildcard character '.' (should match any single character)
    TestSingleRegex(TEXT("^h.llo$"), TEXT("hello"), TRUE, TRUE, Results);
    TestSingleRegex(TEXT("^h.llo$"), TEXT("hallo"), TRUE, TRUE, Results);
    TestSingleRegex(TEXT("^h.llo$"), TEXT("hxllo"), TRUE, TRUE, Results);

    // Test 4: Kleene star '*' quantifier (zero or more)
    TestSingleRegex(TEXT("ab*c"), TEXT("ac"), TRUE, TRUE, Results);  // Zero 'b's
    TestSingleRegex(TEXT("ab*c"), TEXT("abc"), TRUE, TRUE, Results);  // One 'b'
    TestSingleRegex(TEXT("ab*c"), TEXT("abbbc"), TRUE, TRUE, Results);  // Multiple 'b's

    // Test 5: Optional '?' quantifier (zero or one)
    TestSingleRegex(TEXT("colou?r"), TEXT("color"), TRUE, TRUE, Results);  // No 'u'
    TestSingleRegex(TEXT("colou?r"), TEXT("colour"), TRUE, TRUE, Results);  // One 'u'
    TestSingleRegex(TEXT("colou?r"), TEXT("colouur"), FALSE, FALSE, Results);  // Two 'u's (search finds "colour")

    // Test 6: Character class [0-9] (should match any digit)
    TestSingleRegex(TEXT("a[0-9]b"), TEXT("a7b"), TRUE, TRUE, Results);
    TestSingleRegex(TEXT("a[0-9]b"), TEXT("ab"), FALSE, FALSE, Results);  // Missing digit

    // Test 7: Negated character class [^0-9] (should match any non-digit)
    TestSingleRegex(TEXT("a[^0-9]b"), TEXT("axb"), TRUE, TRUE, Results);
}
