
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


    Script Engine - Unit Tests

\************************************************************************/

#include "autotest/Autotest.h"
#include "Base.h"
#include "log/Log.h"
#include "script/Script.h"
#include "text/CoreString.h"

/************************************************************************/

typedef struct {
    STR Name[16];
    I32 Value;
} TEST_HOST_ITEM;

typedef struct {
    TEST_HOST_ITEM* Items;
    U32 Count;
} TEST_HOST_ARRAY;

typedef struct {
    I32 Value;
} TEST_HOST_PROPERTY;

typedef struct {
    UINT ArgumentCount;
    STR Arguments[3][32];
} TEST_SCRIPT_CALL_CAPTURE;

static const SCRIPT_HOST_DESCRIPTOR TestHostObjectDescriptor;
static const SCRIPT_HOST_DESCRIPTOR TestHostArrayDescriptor;
static const SCRIPT_HOST_DESCRIPTOR TestHostValueDescriptor;

/************************************************************************/

/**
 * @brief Test function callback used by numeric semantics unit tests.
 * @param FuncName Function name requested by the script.
 * @param ArgumentCount Number of serialized function arguments.
 * @param Arguments Serialized function arguments.
 * @param UserData Callback capture storage.
 * @return Native-width integer status.
 */
static INT TestScriptCallFunction(
    LPCSTR FuncName,
    UINT ArgumentCount,
    LPCSTR* Arguments,
    LPVOID UserData) {
    TEST_SCRIPT_CALL_CAPTURE* Capture = (TEST_SCRIPT_CALL_CAPTURE*)UserData;

    if (Capture != NULL) {
        Capture->ArgumentCount = ArgumentCount;
        for (UINT Index = 0; Index < 3; Index++) {
            Capture->Arguments[Index][0] = STR_NULL;
        }

        for (UINT Index = 0; Index < ArgumentCount && Index < 3; Index++) {
            StringCopyLimit(
                Capture->Arguments[Index],
                Arguments[Index] != NULL ? Arguments[Index] : TEXT(""),
                sizeof(Capture->Arguments[Index]));
        }
    }

    if (StringCompareNC(FuncName, TEXT("native_status")) == 0) {
        return 123;
    }

    if (StringCompareNC(FuncName, TEXT("native_failure")) == 0) {
        if (UserData != NULL) {
            LPSCRIPT_CONTEXT Context = (LPSCRIPT_CONTEXT)UserData;
            Context->ErrorCode = SCRIPT_ERROR_TYPE_MISMATCH;
            StringCopy(Context->ErrorMessage, TEXT("native_failure rejected the call"));
        }
        return SCRIPT_FUNCTION_STATUS_ERROR;
    }

    return SCRIPT_FUNCTION_STATUS_UNKNOWN;
}

/************************************************************************/
/**
 * @brief Populate the static host items used by the exposure unit tests.
 * @param Items Array of host items to initialize
 * @param Count Number of elements available in the array
 */
static void TestHostPopulateItems(TEST_HOST_ITEM* Items, U32 Count) {
    if (Items == NULL) {
        return;
    }

    if (Count > 0) {
        StringCopy(Items[0].Name, TEXT("Alpha"));
        Items[0].Value = 100;
    }

    if (Count > 1) {
        StringCopy(Items[1].Name, TEXT("Beta"));
        Items[1].Value = 200;
    }

    if (Count > 2) {
        StringCopy(Items[2].Name, TEXT("Gamma"));
        Items[2].Value = 300;
    }
}

/************************************************************************/
/**
 * @brief Host object property accessor for the script exposure tests.
 * @param Context Callback context (unused)
 * @param Parent Handle to the requested host item
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the resulting value
 * @return SCRIPT_OK on success, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
static SCRIPT_ERROR TestHostObjectGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    if (OutValue == NULL || Parent == NULL || Property == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    TEST_HOST_ITEM* Item = (TEST_HOST_ITEM*)Parent;

    if (StringCompareNC(Property, TEXT("value")) == 0) {
        OutValue->Type = SCRIPT_VAR_INTEGER;
        OutValue->Value.Integer = Item->Value;
        OutValue->OwnsValue = FALSE;
        OutValue->HostDescriptor = NULL;
        OutValue->HostContext = NULL;
        return SCRIPT_OK;
    }

    if (StringCompareNC(Property, TEXT("name")) == 0) {
        OutValue->Type = SCRIPT_VAR_STRING;
        OutValue->Value.String = Item->Name;
        OutValue->OwnsValue = FALSE;
        OutValue->HostDescriptor = NULL;
        OutValue->HostContext = NULL;
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/
/**
 * @brief Host array accessor for the script exposure tests.
 * @param Context Callback context (unused)
 * @param Parent Handle to the array exposed to the script engine
 * @param Index Requested index inside the host array
 * @param OutValue Output holder for the resulting host handle
 * @return SCRIPT_OK when the element exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
static SCRIPT_ERROR TestHostArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    if (OutValue == NULL || Parent == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    TEST_HOST_ARRAY* Array = (TEST_HOST_ARRAY*)Parent;
    if (Array->Items == NULL || Index >= Array->Count) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    TEST_HOST_ITEM* Item = &Array->Items[Index];

    OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
    OutValue->Value.HostHandle = Item;
    OutValue->HostDescriptor = &TestHostObjectDescriptor;
    OutValue->HostContext = NULL;
    OutValue->OwnsValue = FALSE;

    return SCRIPT_OK;
}

/************************************************************************/
/**
 * @brief Host property accessor returning scalar values for exposure tests.
 * @param Context Callback context (unused)
 * @param Parent Handle to the scalar value container
 * @param Property Property name requested by the script (unused)
 * @param OutValue Output holder for the resulting scalar value
 * @return SCRIPT_OK on success, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
static SCRIPT_ERROR TestHostValueGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);
    UNUSED(Property);

    if (OutValue == NULL || Parent == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    TEST_HOST_PROPERTY* Value = (TEST_HOST_PROPERTY*)Parent;

    OutValue->Type = SCRIPT_VAR_INTEGER;
    OutValue->Value.Integer = Value->Value;
    OutValue->OwnsValue = FALSE;
    OutValue->HostDescriptor = NULL;
    OutValue->HostContext = NULL;

    return SCRIPT_OK;
}

/************************************************************************/

static const SCRIPT_HOST_DESCRIPTOR TestHostObjectDescriptor = {
    TestHostObjectGetProperty,
    NULL,
    NULL,
    NULL
};

static const SCRIPT_HOST_DESCRIPTOR TestHostArrayDescriptor = {
    NULL,
    TestHostArrayGetElement,
    NULL,
    NULL
};

static const SCRIPT_HOST_DESCRIPTOR TestHostValueDescriptor = {
    TestHostValueGetProperty,
    NULL,
    NULL,
    NULL
};

/************************************************************************/

/**
 * @brief Test simple arithmetic expression.
 *
 * This function tests basic arithmetic operations like addition.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptSimpleArithmetic(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple addition
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 1 + 2;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 3) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: a = %d (expected 3)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Simple subtraction
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 10 - 3;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 7) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: b = %d (expected 7)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 4 * 5;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: c = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: Division
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 20 / 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: d = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test complex arithmetic expressions with operator precedence.
 *
 * This function tests expressions involving multiple operators and
 * proper operator precedence.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComplexArithmetic(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Operator precedence
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 2 + 3 * 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 14) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: a = %d (expected 14)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Parentheses
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = (2 + 3) * 4;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: b = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Using variables in expressions
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("x = 5; y = 10; z = x + y * 2;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("z"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 25) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: z = %d (expected 25)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test comparison operators.
 *
 * This function tests all comparison operators: <, <=, >, >=, ==, !=
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComparisons(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Less than (true)
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 5 < 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: a = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Greater than (false)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 5 > 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: b = %d (expected 0)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 10 == 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: c = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: Not equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 5 != 10;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: d = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 5: String equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("e = \"hello\" == \"hello\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("e"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 5 failed: e = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 5 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 6: String not equal (true)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("f = \"hello\" != \"world\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("f"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 6 failed: f = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 6 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test if/else statements.
 *
 * This function tests conditional execution with if/else.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptIfElse(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple if (true condition)
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("a = 0; if (5 > 3) { a = 10; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("a"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 10) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: a = %d (expected 10)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Simple if (false condition)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("b = 5; if (3 > 5) { b = 10; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("b"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: b = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: If-else (true branch)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("c = 0; if (10 == 10) { c = 100; } else { c = 200; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("c"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 100) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: c = %d (expected 100)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: If-else (false branch)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("d = 0; if (10 != 10) { d = 100; } else { d = 200; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("d"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 200) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: d = %d (expected 200)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test simple for loops.
 *
 * This function tests basic for loop functionality.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptSimpleForLoop(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple counting loop
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("sum = 0; for (i = 0; i < 10; i = i + 1) { sum = sum + i; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 45) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: sum = %d (expected 45)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Loop with multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("product = 1; for (j = 1; j <= 5; j = j + 1) { product = product * j; }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("product"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 120) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: product = %d (expected 120)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test nested for loops.
 *
 * This function tests nested loop functionality.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptNestedForLoops(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Nested loops with counter
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("count = 0; for (i = 0; i < 5; i = i + 1) { for (j = 0; j < 3; j = j + 1) { count = count + 1; } }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("count"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 15) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: count = %d (expected 15)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Nested loops with multiplication
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("result = 0; for (x = 1; x <= 3; x = x + 1) { for (y = 1; y <= 4; y = y + 1) { result = result + x * y; } }"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        // Expected: (1*1 + 1*2 + 1*3 + 1*4) + (2*1 + 2*2 + 2*3 + 2*4) + (3*1 + 3*2 + 3*3 + 3*4) = 10 + 20 + 30 = 60
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 60) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: result = %d (expected 60)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test array operations.
 *
 * This function tests array creation and element access.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptArrays(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Simple array assignment and retrieval
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("arr[0] = 10; arr[1] = 20; arr[2] = 30; val = arr[1]"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("val"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 20) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: val = %d (expected 20)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Array with loop
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("for (k = 0; k < 5; k = k + 1) { data[k] = k * 10; } result = data[3];"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 30) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: result = %d (expected 30)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test string operators in script expressions.
 *
 * This function validates string concatenation coercion and string subtraction.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptStringOperators(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: String concatenation with +
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    SCRIPT_ERROR Error = ScriptExecute(Context, TEXT("value = \"foo\" + \"bar\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
            StringCompare(Var->Value.String, TEXT("foobar")) == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: value = %s (expected foobar)"),
                  (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Integer + string concatenates as text
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 1 + \"bar\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
            StringCompare(Var->Value.String, TEXT("1bar")) == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: value = %s (expected 1bar)"),
                  (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: A mixed + chain switches to text concatenation once a string appears
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 1 + 2 + \"x\" + 3;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
            StringCompare(Var->Value.String, TEXT("3x3")) == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: value = %s (expected 3x3)"),
                  (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 4: String subtraction removes all occurrences
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = \"foobarfoo\" - \"foo\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
            StringCompare(Var->Value.String, TEXT("bar")) == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: value = %s (expected bar)"),
                  (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 5: Removing an empty pattern keeps source unchanged
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = \"hello\" - \"\";"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
            StringCompare(Var->Value.String, TEXT("hello")) == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 5 failed: value = %s (expected hello)"),
                  (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
        }
    } else {
        DEBUG(TEXT("Test 5 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test host-exposed variables and properties.
 *
 * This function validates property symbols, array element bindings, and
 * assignment guards for host data exposed to the script engine.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptHostExposure(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Property symbol returns integer value
    Results->TestsRun++;
    LPSCRIPT_CONTEXT PropertyContext = ScriptCreateContext(NULL);
    if (PropertyContext == NULL) {
        DEBUG(TEXT("Failed to create context for property test"));
        return;
    }

    TEST_HOST_PROPERTY HostProperty = {42};
    if (!ScriptRegisterHostSymbol(
            PropertyContext,
            TEXT("hostValue"),
            SCRIPT_HOST_SYMBOL_PROPERTY,
            &HostProperty,
            &TestHostValueDescriptor,
            NULL)) {
        DEBUG(TEXT("Failed to register hostValue property symbol"));
    } else {
        SCRIPT_ERROR Error = ScriptExecute(PropertyContext, TEXT("result = hostValue;"));
        if (Error == SCRIPT_OK) {
            LPSCRIPT_VARIABLE Var = ScriptGetVariable(PropertyContext, TEXT("result"));
            if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 42) {
                Results->TestsPassed++;
            } else {
                DEBUG(TEXT("Property test failed: result = %d (expected 42)"),
                      Var ? Var->Value.Integer : -1);
            }
        } else {
            DEBUG(TEXT("Property test failed with error %d"), Error);
        }
    }

    ScriptDestroyContext(PropertyContext);

    // Tests 2 & 3: Host array exposes handles and string properties
    Results->TestsRun++;
    Results->TestsRun++;
    LPSCRIPT_CONTEXT ArrayContext = ScriptCreateContext(NULL);
    if (ArrayContext == NULL) {
        DEBUG(TEXT("Failed to create context for array tests"));
        return;
    }

    TEST_HOST_ITEM Items[3];
    TestHostPopulateItems(Items, 3);
    TEST_HOST_ARRAY Array = {Items, 3};

    if (!ScriptRegisterHostSymbol(
            ArrayContext,
            TEXT("hosts"),
            SCRIPT_HOST_SYMBOL_ARRAY,
            &Array,
            &TestHostArrayDescriptor,
            NULL)) {
        DEBUG(TEXT("Failed to register hosts array symbol"));
    } else {
        SCRIPT_ERROR Error = ScriptExecute(ArrayContext, TEXT("value = hosts[1].value;"));
        if (Error == SCRIPT_OK) {
            LPSCRIPT_VARIABLE Var = ScriptGetVariable(ArrayContext, TEXT("value"));
            if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 200) {
                Results->TestsPassed++;
            } else {
                DEBUG(TEXT("Array value test failed: value = %d (expected 200)"),
                      Var ? Var->Value.Integer : -1);
            }
        } else {
            DEBUG(TEXT("Array value test failed with error %d"), Error);
        }

        Error = ScriptExecute(ArrayContext, TEXT("name = hosts[2].name;"));
        if (Error == SCRIPT_OK) {
            LPSCRIPT_VARIABLE Var = ScriptGetVariable(ArrayContext, TEXT("name"));
            if (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String &&
                StringCompare(Var->Value.String, TEXT("Gamma")) == 0) {
                Results->TestsPassed++;
            } else {
                DEBUG(TEXT("Array string test failed: name = %s (expected Gamma)"),
                      (Var && Var->Type == SCRIPT_VAR_STRING && Var->Value.String) ? Var->Value.String : TEXT("(null)"));
            }
        } else {
            DEBUG(TEXT("Array string test failed with error %d"), Error);
        }
    }

    ScriptDestroyContext(ArrayContext);

    // Test 4: Guard against assigning to host symbols
    Results->TestsRun++;
    LPSCRIPT_CONTEXT GuardContext = ScriptCreateContext(NULL);
    if (GuardContext == NULL) {
        DEBUG(TEXT("Failed to create context for guard test"));
        return;
    }

    TEST_HOST_PROPERTY GuardProperty = {55};
    if (!ScriptRegisterHostSymbol(
            GuardContext,
            TEXT("hostValue"),
            SCRIPT_HOST_SYMBOL_PROPERTY,
            &GuardProperty,
            &TestHostValueDescriptor,
            NULL)) {
        DEBUG(TEXT("Failed to register hostValue for guard test"));
    } else {
        SCRIPT_ERROR Error = ScriptExecute(GuardContext, TEXT("hostValue = 99;"));
        if (Error == SCRIPT_ERROR_SYNTAX) {
            LPSCRIPT_VARIABLE Var = ScriptGetVariable(GuardContext, TEXT("hostValue"));
            if (Var == NULL) {
                Results->TestsPassed++;
            } else {
                DEBUG(TEXT("Guard test failed: hostValue variable should not exist"));
            }
        } else {
            DEBUG(TEXT("Guard test failed with error %d (expected syntax error)"), Error);
        }
    }

    ScriptDestroyContext(GuardContext);
}

/************************************************************************/

/**
 * @brief Test complex script with multiple features.
 *
 * This function tests a complex script combining loops, conditionals,
 * arrays, and arithmetic operations.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptComplex(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Fibonacci-like calculation with array
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    LPCSTR Script = TEXT(
        "fib[0] = 0;\n"
        "fib[1] = 1;\n"
        "for (n = 2; n < 10; n = n + 1) {\n"
        "  n1 = n - 1;\n"
        "  n2 = n - 2;\n"
        "  fib[n] = fib[n1] + fib[n2];\n"
        "}\n"
        "result = fib[9];"
    );

    SCRIPT_ERROR Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("result"));
        // Fibonacci sequence: 0, 1, 1, 2, 3, 5, 8, 13, 21, 34
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 34) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: result = %d (expected 34)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Complex nested loops with conditionals
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Script = TEXT(
        "total = 0;\n"
        "for (i = 1; i <= 10; i = i + 1) {\n"
        "  for (j = 1; j <= 10; j = j + 1) {\n"
        "    prod = i * j;\n"
        "    if (prod > 20) {\n"
        "      if (prod < 30) {\n"
        "        total = total + 1;\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("total"));
        // Count products where 20 < i*j < 30
        // This includes: 21, 22, 24, 25, 27, 28 appearing multiple times
        // (3,7), (3,8), (3,9), (4,6), (4,7), (5,5), (6,4), (7,3), (7,4), (8,3), (9,3) = 11
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 11) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: total = %d (expected 11)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 3: Prime number checking (simplified)
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Script = TEXT(
        "num = 17;\n"
        "isPrime = 1;\n"
        "if (num < 2) {\n"
        "  isPrime = 0;\n"
        "} else {\n"
        "  for (i = 2; i < num; i = i + 1) {\n"
        "    div = num / i;\n"
        "    prod = div * i;\n"
        "    if (prod == num) {\n"
        "      isPrime = 0;\n"
        "    }\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("isPrime"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: isPrime = %d (expected 1)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test loop with if inside.
 *
 * This function tests combining loops with conditional statements.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScriptLoopWithIf(TEST_RESULTS* Results) {
    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Test 1: Count even numbers
    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    LPCSTR Script = TEXT(
        "count = 0;\n"
        "for (i = 0; i < 10; i = i + 1) {\n"
        "  div = i / 2;\n"
        "  prod = div * 2;\n"
        "  if (prod == i) {\n"
        "    count = count + 1;\n"
        "  }\n"
        "}"
    );

    SCRIPT_ERROR Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("count"));
        // Even numbers from 0 to 9: 0, 2, 4, 6, 8 = 5 numbers
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 5) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: count = %d (expected 5)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    // Test 2: Sum of numbers greater than threshold
    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Script = TEXT(
        "threshold = 5;\n"
        "sum = 0;\n"
        "for (i = 0; i <= 10; i = i + 1) {\n"
        "  if (i > threshold) {\n"
        "    sum = sum + i;\n"
        "  }\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        // Sum of 6 + 7 + 8 + 9 + 10 = 40
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 40) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: sum = %d (expected 40)"), Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test continue statements inside for loops.
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestScriptContinue(TEST_RESULTS* Results) {
    SCRIPT_ERROR Error = SCRIPT_OK;
    LPCSTR Script = NULL;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Script = TEXT(
        "sum = 0;\n"
        "for (i = 0; i < 6; i = i + 1) {\n"
        "  if (i == 2) {\n"
        "    continue;\n"
        "  }\n"
        "  sum = sum + i;\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 13) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: sum = %d (expected 13)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create second context"));
        return;
    }

    Script = TEXT(
        "sum = 0;\n"
        "for (i = 0; i < 5; i = i + 1) {\n"
        "  if (i == 1) {\n"
        "    continue;\n"
        "  }\n"
        "  if (i == 3) {\n"
        "    continue;\n"
        "  }\n"
        "  sum = sum + i;\n"
        "}"
    );

    Error = ScriptExecute(Context, Script);
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("sum"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 6) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: sum = %d (expected 6)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create third context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("continue;"));
    if (Error == SCRIPT_ERROR_SYNTAX) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test script return value storage and visibility.
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestScriptReturnValues(TEST_RESULTS* Results) {
    SCRIPT_ERROR Error = SCRIPT_OK;
    SCRIPT_VAR_TYPE ReturnType = SCRIPT_VAR_INTEGER;
    SCRIPT_VAR_VALUE ReturnValue;

    MemorySet(&ReturnValue, 0, sizeof(ReturnValue));

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("return 123;"));
    if (Error == SCRIPT_OK &&
        ScriptGetReturnValue(Context, &ReturnType, &ReturnValue) &&
        ReturnType == SCRIPT_VAR_INTEGER &&
        ReturnValue.Integer == 123) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 1 failed: error = %d has_return = %d type = %d value = %d"),
            Error,
            ScriptHasReturnValue(Context),
            ReturnType,
            ReturnValue.Integer);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create second context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 7;"));
    if (Error == SCRIPT_OK && ScriptHasReturnValue(Context) == FALSE) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 2 failed: error = %d has_return = %d"),
            Error,
            ScriptHasReturnValue(Context));
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test logical operators, precedence, and short-circuit evaluation.
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestScriptLogicalOperators(TEST_RESULTS* Results) {
    SCRIPT_ERROR Error = SCRIPT_OK;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 1 || 0 && 0;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: value = %d (expected 1)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create second context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = !(0);"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: value = %d (expected 1)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create third context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 0 && missing_value;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 0) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: value = %d (expected 0)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create fourth context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 1 || missing_value;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 1) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: value = %d (expected 1)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test native-width integer semantics in parser, evaluator and callbacks.
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results.
 */
void TestScriptIntegerSemantics(TEST_RESULTS* Results) {
    SCRIPT_ERROR Error = SCRIPT_OK;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Results->TestsRun++;
    LPSCRIPT_CONTEXT Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 1.5 + 2.25;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_FLOAT && Var->Value.Float == 3.75f) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 1 failed: wrong float result"));
        }
    } else {
        DEBUG(TEXT("Test 1 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("value = 7 / 2;"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("value"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 3) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 2 failed: value = %d (expected 3)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 2 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    SCRIPT_CALLBACKS Callbacks;
    TEST_SCRIPT_CALL_CAPTURE Capture;

    MemorySet(&Capture, 0, sizeof(TEST_SCRIPT_CALL_CAPTURE));
    MemorySet(&Callbacks, 0, sizeof(SCRIPT_CALLBACKS));
    Callbacks.CallFunction = TestScriptCallFunction;
    Callbacks.UserData = &Capture;
    Context = ScriptCreateContext(&Callbacks);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create callback context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("status = native_status(42, \"alpha\", 7 + 1);"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("status"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 123 &&
            Capture.ArgumentCount == 3 &&
            STRINGS_EQUAL(Capture.Arguments[0], TEXT("42")) &&
            STRINGS_EQUAL(Capture.Arguments[1], TEXT("alpha")) &&
            STRINGS_EQUAL(Capture.Arguments[2], TEXT("8"))) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 3 failed: status = %d (expected 123), argc = %u"),
                Var ? Var->Value.Integer : -1,
                Capture.ArgumentCount);
            DEBUG(TEXT("Test 3 args: [%s] [%s] [%s]"),
                Capture.Arguments[0],
                Capture.Arguments[1],
                Capture.Arguments[2]);
        }
    } else {
        DEBUG(TEXT("Test 3 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    MemorySet(&Callbacks, 0, sizeof(SCRIPT_CALLBACKS));
    Callbacks.CallFunction = TestScriptCallFunction;
    Context = ScriptCreateContext(&Callbacks);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create zero-argument callback context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("status = native_status();"));
    if (Error == SCRIPT_OK) {
        LPSCRIPT_VARIABLE Var = ScriptGetVariable(Context, TEXT("status"));
        if (Var && Var->Type == SCRIPT_VAR_INTEGER && Var->Value.Integer == 123) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: status = %d (expected 123)"),
                Var ? Var->Value.Integer : -1);
        }
    } else {
        DEBUG(TEXT("Test 4 failed with error %d"), Error);
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    MemorySet(&Callbacks, 0, sizeof(SCRIPT_CALLBACKS));
    Callbacks.CallFunction = TestScriptCallFunction;
    Context = ScriptCreateContext(&Callbacks);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create unknown-function callback context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("status = unknown_function(1);"));
    if (Error == SCRIPT_ERROR_UNDEFINED_VAR &&
        StringCompare(ScriptGetErrorMessage(Context), TEXT("Unknown function: unknown_function")) == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 5 failed: error = %d message = %s"),
            Error,
            ScriptGetErrorMessage(Context));
    }

    ScriptDestroyContext(Context);

    Results->TestsRun++;
    MemorySet(&Callbacks, 0, sizeof(SCRIPT_CALLBACKS));
    Callbacks.CallFunction = TestScriptCallFunction;
    Context = ScriptCreateContext(&Callbacks);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create function-failure callback context"));
        return;
    }

    Context->Callbacks.UserData = Context;
    Error = ScriptExecute(Context, TEXT("status = native_failure(1);"));
    if (Error == SCRIPT_ERROR_TYPE_MISMATCH &&
        StringCompare(ScriptGetErrorMessage(Context), TEXT("native_failure rejected the call")) == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 6 failed: error = %d message = %s"),
            Error,
            ScriptGetErrorMessage(Context));
    }

    ScriptDestroyContext(Context);
}

/************************************************************************/

/**
 * @brief Test native E0 object creation, reads, writes, and ownership semantics.
 * @param Results Pointer to TEST_RESULTS structure to populate.
 */
void TestScriptObjects(TEST_RESULTS* Results) {
    LPSCRIPT_CONTEXT Context;
    LPSCRIPT_VARIABLE Variable;
    SCRIPT_VALUE PropertyValue;
    SCRIPT_ERROR Error;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {};"));
    Variable = ScriptGetVariable(Context, TEXT("user"));
    if (Error == SCRIPT_OK &&
        Variable != NULL &&
        Variable->Type == SCRIPT_VAR_OBJECT &&
        Variable->Value.Object != NULL &&
        Variable->Value.Object->PropertyCount == 0) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 1 failed: error=%d type=%d"), Error, Variable ? Variable->Type : -1);
    }
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for property write"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; user.name = \"alice\";"));
    Variable = ScriptGetVariable(Context, TEXT("user"));
    ScriptValueInit(&PropertyValue);
    if (Error == SCRIPT_OK &&
        Variable != NULL &&
        Variable->Type == SCRIPT_VAR_OBJECT &&
        ScriptGetObjectProperty(Variable->Value.Object, TEXT("name"), &PropertyValue) == SCRIPT_OK &&
        PropertyValue.Type == SCRIPT_VAR_STRING &&
        STRINGS_EQUAL(PropertyValue.Value.String, TEXT("alice"))) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 2 failed: error=%d"), Error);
    }
    ScriptValueRelease(&PropertyValue);
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for overwrite"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; user.name = \"alice\"; user.name = \"bob\";"));
    Variable = ScriptGetVariable(Context, TEXT("user"));
    ScriptValueInit(&PropertyValue);
    if (Error == SCRIPT_OK &&
        Variable != NULL &&
        Variable->Type == SCRIPT_VAR_OBJECT &&
        ScriptGetObjectProperty(Variable->Value.Object, TEXT("name"), &PropertyValue) == SCRIPT_OK &&
        PropertyValue.Type == SCRIPT_VAR_STRING &&
        STRINGS_EQUAL(PropertyValue.Value.String, TEXT("bob"))) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 3 failed: error=%d"), Error);
    }
    ScriptValueRelease(&PropertyValue);
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for nested object"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; user.settings = {}; user.settings.theme = \"light\";"));
    Variable = ScriptGetVariable(Context, TEXT("user"));
    ScriptValueInit(&PropertyValue);
    if (Error == SCRIPT_OK &&
        Variable != NULL &&
        Variable->Type == SCRIPT_VAR_OBJECT &&
        ScriptGetObjectProperty(Variable->Value.Object, TEXT("settings"), &PropertyValue) == SCRIPT_OK &&
        PropertyValue.Type == SCRIPT_VAR_OBJECT &&
        PropertyValue.Value.Object != NULL) {
        SCRIPT_VALUE ThemeValue;
        ScriptValueInit(&ThemeValue);
        if (ScriptGetObjectProperty(PropertyValue.Value.Object, TEXT("theme"), &ThemeValue) == SCRIPT_OK &&
            ThemeValue.Type == SCRIPT_VAR_STRING &&
            STRINGS_EQUAL(ThemeValue.Value.String, TEXT("light"))) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 4 failed: nested theme lookup"));
        }
        ScriptValueRelease(&ThemeValue);
    } else {
        DEBUG(TEXT("Test 4 failed: error=%d"), Error);
    }
    ScriptValueRelease(&PropertyValue);
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for mixed types"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("item = {}; item.name = \"alpha\"; item.count = 7; item.ratio = 1.5;"));
    Variable = ScriptGetVariable(Context, TEXT("item"));
    if (Error == SCRIPT_OK && Variable != NULL && Variable->Type == SCRIPT_VAR_OBJECT) {
        SCRIPT_VALUE NameValue;
        SCRIPT_VALUE CountValue;
        SCRIPT_VALUE RatioValue;
        ScriptValueInit(&NameValue);
        ScriptValueInit(&CountValue);
        ScriptValueInit(&RatioValue);
        if (ScriptGetObjectProperty(Variable->Value.Object, TEXT("name"), &NameValue) == SCRIPT_OK &&
            ScriptGetObjectProperty(Variable->Value.Object, TEXT("count"), &CountValue) == SCRIPT_OK &&
            ScriptGetObjectProperty(Variable->Value.Object, TEXT("ratio"), &RatioValue) == SCRIPT_OK &&
            NameValue.Type == SCRIPT_VAR_STRING &&
            STRINGS_EQUAL(NameValue.Value.String, TEXT("alpha")) &&
            CountValue.Type == SCRIPT_VAR_INTEGER &&
            CountValue.Value.Integer == 7 &&
            RatioValue.Type == SCRIPT_VAR_FLOAT &&
            RatioValue.Value.Float == 1.5f) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 5 failed: mixed type checks"));
        }
        ScriptValueRelease(&NameValue);
        ScriptValueRelease(&CountValue);
        ScriptValueRelease(&RatioValue);
    } else {
        DEBUG(TEXT("Test 5 failed: error=%d"), Error);
    }
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for missing property"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; name = user.name;"));
    if (Error == SCRIPT_ERROR_UNDEFINED_VAR) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 6 failed: error=%d"), Error);
    }
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for invalid intermediate"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; user.name = 7; user.name.value = 1;"));
    if (Error == SCRIPT_ERROR_TYPE_MISMATCH) {
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 7 failed: error=%d"), Error);
    }
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for reference semantics"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; alias = user; alias.name = \"shared\";"));
    Variable = ScriptGetVariable(Context, TEXT("user"));
    if (Error == SCRIPT_OK &&
        Variable != NULL &&
        Variable->Type == SCRIPT_VAR_OBJECT &&
        Variable->Value.Object != NULL &&
        Variable->Value.Object->RefCount == 2) {
        SCRIPT_VALUE SharedValue;
        ScriptValueInit(&SharedValue);
        if (ScriptGetObjectProperty(Variable->Value.Object, TEXT("name"), &SharedValue) == SCRIPT_OK &&
            SharedValue.Type == SCRIPT_VAR_STRING &&
            STRINGS_EQUAL(SharedValue.Value.String, TEXT("shared"))) {
            Results->TestsPassed++;
        } else {
            DEBUG(TEXT("Test 8 failed: shared property lookup"));
        }
        ScriptValueRelease(&SharedValue);
    } else {
        DEBUG(TEXT("Test 8 failed: error=%d refcount=%u"),
            Error,
            (Variable != NULL && Variable->Value.Object != NULL) ? Variable->Value.Object->RefCount : 0);
    }
    ScriptDestroyContext(Context);

    Results->TestsRun++;
    Context = ScriptCreateContext(NULL);
    if (Context == NULL) {
        DEBUG(TEXT("Failed to create context for destruction"));
        return;
    }

    Error = ScriptExecute(Context, TEXT("user = {}; nested = {}; user.nested = nested; alias = user;"));
    if (Error == SCRIPT_OK) {
        ScriptDestroyContext(Context);
        Context = NULL;
        Results->TestsPassed++;
    } else {
        DEBUG(TEXT("Test 9 failed: error=%d"), Error);
    }

    if (Context != NULL) {
        ScriptDestroyContext(Context);
    }
}

/************************************************************************/

/**
 * @brief Main Script test function that runs all Script unit tests.
 *
 * This function coordinates all Script unit tests and aggregates their results.
 * It tests arithmetic, comparisons, control flow, loops, arrays, and complex
 * script combinations.
 *
 * @param Results Pointer to TEST_RESULTS structure to be filled with test results
 */
void TestScript(TEST_RESULTS* Results) {
    TEST_RESULTS SubResults;

    Results->TestsRun = 0;
    Results->TestsPassed = 0;

    // Run simple arithmetic tests
    TestScriptSimpleArithmetic(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run complex arithmetic tests
    TestScriptComplexArithmetic(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run comparison tests
    TestScriptComparisons(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run if/else tests
    TestScriptIfElse(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run simple for loop tests
    TestScriptSimpleForLoop(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run nested for loop tests
    TestScriptNestedForLoops(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run array tests
    TestScriptArrays(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run string operator tests
    TestScriptStringOperators(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run host exposure tests
    TestScriptHostExposure(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run loop with if tests
    TestScriptLoopWithIf(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run continue tests
    TestScriptContinue(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run complex script tests
    TestScriptComplex(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run return value tests
    TestScriptReturnValues(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run logical operator tests
    TestScriptLogicalOperators(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run native-width integer semantic tests
    TestScriptIntegerSemantics(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;

    // Run native object tests
    TestScriptObjects(&SubResults);
    Results->TestsRun += SubResults.TestsRun;
    Results->TestsPassed += SubResults.TestsPassed;
}
