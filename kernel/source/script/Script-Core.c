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


    Script Engine - Core

\************************************************************************/

#include "Base.h"
#include "memory/Heap.h"
#include "utils/List.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "script/Script.h"
#include "script/Script-Internal.h"

/************************************************************************/

LPVOID ScriptAlloc(LPSCRIPT_CONTEXT Context, UINT Size) {
    if (Context == NULL) {
        return NULL;
    }

    return AllocatorAlloc(&Context->Allocator, Size);
}

/************************************************************************/

LPVOID ScriptRealloc(LPSCRIPT_CONTEXT Context, LPVOID Pointer, UINT Size) {
    if (Context == NULL) {
        return NULL;
    }

    return AllocatorRealloc(&Context->Allocator, Pointer, Size);
}

/************************************************************************/

void ScriptFree(LPSCRIPT_CONTEXT Context, LPVOID Pointer) {
    if (Context == NULL) {
        return;
    }

    AllocatorFree(&Context->Allocator, Pointer);
}

/************************************************************************/
/**
 * @brief Check whether a file name targets an E0 script.
 * @param FileName File name or path to inspect.
 * @return TRUE when the file name ends with .e0 (case-insensitive), FALSE otherwise.
 */
BOOL ScriptIsE0FileName(LPCSTR FileName) {
    UINT ExtensionLength;
    UINT FileNameLength;
    LPCSTR ExtensionPosition;

    if (FileName == NULL) {
        return FALSE;
    }

    ExtensionLength = StringLength(E0_SCRIPT_FILE_EXTENSION);
    FileNameLength = StringLength(FileName);

    if (FileNameLength < ExtensionLength) {
        return FALSE;
    }

    ExtensionPosition = FileName + (FileNameLength - ExtensionLength);
    return StringCompareNC(ExtensionPosition, E0_SCRIPT_FILE_EXTENSION) == 0;
}

/************************************************************************/

/**
 * @brief Create a new script context with callback bindings.
 * @param Callbacks Pointer to callback structure for external integration
 * @return Pointer to new script context or NULL on failure
 */
LPSCRIPT_CONTEXT ScriptCreateContext(LPSCRIPT_CALLBACKS Callbacks) {
    return ScriptCreateContextA(Callbacks, NULL);
}

/************************************************************************/

LPSCRIPT_CONTEXT ScriptCreateContextA(LPSCRIPT_CALLBACKS Callbacks, LPCALLOCATOR Allocator) {
    ALLOCATOR LocalAllocator;
    LPSCRIPT_CONTEXT Context;

    if (Allocator == NULL) {
        AllocatorInitProcess(&LocalAllocator, GetCurrentProcess());
        Allocator = &LocalAllocator;
    }

    Context = (LPSCRIPT_CONTEXT)AllocatorAlloc(Allocator, sizeof(SCRIPT_CONTEXT));
    if (Context == NULL) {
        DEBUG(TEXT("Failed to allocate context"));
        return NULL;
    }

    MemorySet(Context, 0, sizeof(SCRIPT_CONTEXT));
    Context->Allocator = *Allocator;

    if (!ScriptInitHostRegistry(Context, &Context->HostRegistry)) {
        DEBUG(TEXT("Failed to initialize host registry"));
        ScriptDestroyContext(Context);
        return NULL;
    }

    // Initialize global scope
    Context->GlobalScope = ScriptCreateScope(Context, NULL);
    if (Context->GlobalScope == NULL) {
        DEBUG(TEXT("Failed to create global scope"));
        ScriptDestroyContext(Context);
        return NULL;
    }
    Context->CurrentScope = Context->GlobalScope;

    if (Callbacks) {
        Context->Callbacks = *Callbacks;
    }

    Context->ErrorCode = SCRIPT_OK;

    return Context;
}

/************************************************************************/

/**
 * @brief Destroy a script context and free all resources.
 * @param Context Script context to destroy
 */
void ScriptDestroyContext(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) return;

    ScriptClearReturnValue(Context);
    ScriptClearHostRegistryInternal(&Context->HostRegistry);

    // Free global scope and all child scopes
    if (Context->GlobalScope) {
        ScriptDestroyScope(Context->GlobalScope);
    }

    ScriptFree(Context, Context);
}

/************************************************************************/

/**
 * @brief Execute a script (can contain multiple lines) - Two-pass architecture.
 * @param Context Script context to use
 * @param Script Script text to execute (may contain newlines)
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecute(LPSCRIPT_CONTEXT Context, LPCSTR Script) {
    if (Context == NULL || Script == NULL) {
        DEBUG(TEXT("NULL parameters"));
        return SCRIPT_ERROR_SYNTAX;
    }

    Context->ErrorCode = SCRIPT_OK;
    Context->ErrorMessage[0] = STR_NULL;
    ScriptClearReturnValue(Context);

    SCRIPT_PARSER Parser;
    ScriptInitParser(&Parser, Script, Context);

    // PASS 1: Parse script and build AST
    LPAST_NODE Root = ScriptCreateASTNode(Context, AST_BLOCK);
    if (Root == NULL) {
        StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
        Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    Root->Data.Block.Capacity = 16;
    Root->Data.Block.Statements = (LPAST_NODE*)ScriptAlloc(Context, Root->Data.Block.Capacity * sizeof(LPAST_NODE));
    if (Root->Data.Block.Statements == NULL) {
        ScriptDestroyAST(Root);
        StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
        Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }
    Root->Data.Block.Count = 0;

    SCRIPT_ERROR Error = SCRIPT_OK;

    // Parse all statements until EOF
    while (Parser.CurrentToken.Type != TOKEN_EOF) {
        LPAST_NODE Statement = ScriptParseStatementAST(&Parser, &Error);
        if (Error != SCRIPT_OK) {
            StringPrintFormat(Context->ErrorMessage, TEXT("Syntax error (l:%d,c:%d)"), Parser.CurrentToken.Line, Parser.CurrentToken.Column);
            Context->ErrorCode = Error;
            ScriptDestroyAST(Root);
            return Error;
        }

        // Add statement to root block
        if (Root->Data.Block.Count >= Root->Data.Block.Capacity) {
            Root->Data.Block.Capacity *= 2;
            LPAST_NODE* NewStatements = (LPAST_NODE*)ScriptAlloc(Context, Root->Data.Block.Capacity * sizeof(LPAST_NODE));
            if (NewStatements == NULL) {
                ScriptDestroyAST(Statement);
                ScriptDestroyAST(Root);
                StringCopy(Context->ErrorMessage, TEXT("Out of memory"));
                Context->ErrorCode = SCRIPT_ERROR_OUT_OF_MEMORY;
                return SCRIPT_ERROR_OUT_OF_MEMORY;
            }
            for (U32 i = 0; i < Root->Data.Block.Count; i++) {
                NewStatements[i] = Root->Data.Block.Statements[i];
            }
            ScriptFree(Context, Root->Data.Block.Statements);
            Root->Data.Block.Statements = NewStatements;
        }

        Root->Data.Block.Statements[Root->Data.Block.Count++] = Statement;

        // Semicolon is mandatory after assignments and control-flow leaf statements.
        if (Statement->Type == AST_ASSIGNMENT || Statement->Type == AST_RETURN || Statement->Type == AST_CONTINUE) {
            if (Parser.CurrentToken.Type != TOKEN_SEMICOLON && Parser.CurrentToken.Type != TOKEN_EOF) {
                StringPrintFormat(Context->ErrorMessage, TEXT("Expected semicolon (l:%d,c:%d)"), Parser.CurrentToken.Line, Parser.CurrentToken.Column);
                Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(Root);
                return SCRIPT_ERROR_SYNTAX;
            }
            if (Parser.CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(&Parser);
            }
        } else {
            // For blocks, if, for: semicolon is optional
            if (Parser.CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(&Parser);
            }
        }
    }

    // PASS 2: Execute AST - Execute statements directly without creating a new scope
    for (U32 i = 0; i < Root->Data.Block.Count; i++) {
        Error = ScriptExecuteAST(&Parser, Root->Data.Block.Statements[i]);
        if (Error != SCRIPT_OK || Context->ReturnTriggered || Context->ContinueTriggered) {
            break;
        }
    }

    ScriptDestroyAST(Root);

    if (Error == SCRIPT_OK && Context->ErrorCode != SCRIPT_OK) {
        Error = Context->ErrorCode;
    }

    if (Error != SCRIPT_OK) {
        if (Context->ErrorMessage[0] == STR_NULL) {
            StringCopy(Context->ErrorMessage, TEXT("Execution error"));
        }
        Context->ErrorCode = Error;
    }

    return Error;
}

/************************************************************************/

/**
 * @brief Set a variable value in the script context.
 * @param Context Script context
 * @param Name Variable name
 * @param Type Variable type
 * @param Value Variable value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Context == NULL || Name == NULL) return NULL;

    return ScriptSetVariableInScope(Context->CurrentScope, Name, Type, Value);
}

/************************************************************************/

/**
 * @brief Get a variable from the script context.
 * @param Context Script context
 * @param Name Variable name
 * @return Pointer to variable or NULL if not found
 */
LPSCRIPT_VARIABLE ScriptGetVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL) return NULL;

    return ScriptFindVariableInScope(Context->CurrentScope, Name, TRUE);
}

/************************************************************************/

/**
 * @brief Delete a variable from the script context.
 * @param Context Script context
 * @param Name Variable name to delete
 */
void ScriptDeleteVariable(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL || Context->CurrentScope == NULL) return;

    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Context->CurrentScope->Buckets[Hash];

    // Only delete from current scope, not parent scopes
    for (LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Bucket->First; Variable; Variable = (LPSCRIPT_VARIABLE)Variable->Next) {
        if (STRINGS_EQUAL(Variable->Name, Name)) {
            ListRemove(Bucket, Variable);
            ScriptFreeVariable(Variable);
            Context->CurrentScope->Count--;
            break;
        }
    }
}

/************************************************************************/

/**
 * @brief Get the last error code from script execution.
 * @param Context Script context
 * @return Error code
 */
SCRIPT_ERROR ScriptGetLastError(LPSCRIPT_CONTEXT Context) {
    return Context ? Context->ErrorCode : SCRIPT_ERROR_SYNTAX;
}

/************************************************************************/

/**
 * @brief Get the last error message from script execution.
 * @param Context Script context
 * @return Error message string
 */
LPCSTR ScriptGetErrorMessage(LPSCRIPT_CONTEXT Context) {
    return Context ? Context->ErrorMessage : TEXT("Invalid context");
}

/************************************************************************/

BOOL ScriptHasReturnValue(LPSCRIPT_CONTEXT Context) {
    return (Context != NULL && Context->HasReturnValue);
}

/************************************************************************/

BOOL ScriptGetReturnValue(LPSCRIPT_CONTEXT Context, SCRIPT_VAR_TYPE* Type, SCRIPT_VAR_VALUE* Value) {
    if (Context == NULL || Type == NULL || Value == NULL || Context->HasReturnValue == FALSE) {
        return FALSE;
    }

    *Type = Context->ReturnType;
    *Value = Context->ReturnValue;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Create a new AST node.
 * @param Type Node type
 * @return Pointer to new node or NULL on failure
 */
LPAST_NODE ScriptCreateASTNode(LPSCRIPT_CONTEXT Context, AST_NODE_TYPE Type) {
    LPAST_NODE Node = (LPAST_NODE)ScriptAlloc(Context, sizeof(AST_NODE));
    if (Node == NULL) {
        ERROR(TEXT("Failed to allocate AST node"));
        return NULL;
    }

    MemorySet(Node, 0, sizeof(AST_NODE));
    Node->Context = Context;
    Node->Type = Type;
    Node->Next = NULL;

    return Node;
}

/************************************************************************/

/**
 * @brief Destroy an AST node and all its children.
 * @param Node Node to destroy
 */
void ScriptDestroyAST(LPAST_NODE Node) {
    if (Node == NULL) return;

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            if (Node->Data.Assignment.Expression) {
                ScriptDestroyAST(Node->Data.Assignment.Expression);
            }
            if (Node->Data.Assignment.ArrayIndexExpr) {
                ScriptDestroyAST(Node->Data.Assignment.ArrayIndexExpr);
            }
            if (Node->Data.Assignment.PropertyBaseExpression) {
                ScriptDestroyAST(Node->Data.Assignment.PropertyBaseExpression);
            }
            break;

        case AST_IF:
            if (Node->Data.If.Condition) {
                ScriptDestroyAST(Node->Data.If.Condition);
            }
            if (Node->Data.If.Then) {
                ScriptDestroyAST(Node->Data.If.Then);
            }
            if (Node->Data.If.Else) {
                ScriptDestroyAST(Node->Data.If.Else);
            }
            break;

        case AST_FOR:
            if (Node->Data.For.Init) {
                ScriptDestroyAST(Node->Data.For.Init);
            }
            if (Node->Data.For.Condition) {
                ScriptDestroyAST(Node->Data.For.Condition);
            }
            if (Node->Data.For.Increment) {
                ScriptDestroyAST(Node->Data.For.Increment);
            }
            if (Node->Data.For.Body) {
                ScriptDestroyAST(Node->Data.For.Body);
            }
            break;

        case AST_BLOCK:
            if (Node->Data.Block.Statements) {
                for (U32 i = 0; i < Node->Data.Block.Count; i++) {
                    ScriptDestroyAST(Node->Data.Block.Statements[i]);
                }
                ScriptFree(Node->Context, Node->Data.Block.Statements);
            }
            break;

        case AST_RETURN:
            if (Node->Data.Return.Expression) {
                ScriptDestroyAST(Node->Data.Return.Expression);
            }
            break;

        case AST_CONTINUE:
            break;

        case AST_EXPRESSION:
            if (Node->Data.Expression.BaseExpression) {
                ScriptDestroyAST(Node->Data.Expression.BaseExpression);
            }
            if (Node->Data.Expression.ArrayIndexExpr) {
                ScriptDestroyAST(Node->Data.Expression.ArrayIndexExpr);
            }
            if (Node->Data.Expression.FirstArgument) {
                ScriptDestroyAST(Node->Data.Expression.FirstArgument);
            }
            if (Node->Data.Expression.NextArgument) {
                ScriptDestroyAST(Node->Data.Expression.NextArgument);
            }
            if (Node->Data.Expression.Left) {
                ScriptDestroyAST(Node->Data.Expression.Left);
            }
            if (Node->Data.Expression.Right) {
                ScriptDestroyAST(Node->Data.Expression.Right);
            }
            if (Node->Data.Expression.IsShellCommand && Node->Data.Expression.CommandLine) {
                ScriptFree(Node->Context, Node->Data.Expression.CommandLine);
            }
            break;

        default:
            break;
    }

    // Destroy next node in generic chain
    if (Node->Next) {
        ScriptDestroyAST(Node->Next);
    }

    ScriptFree(Node->Context, Node);
}

/************************************************************************/

/**
 * @brief Hash function for variable names.
 * @param Name Variable name to hash
 * @return Hash value
 */
U32 ScriptHashVariable(LPCSTR Name) {
    U32 Hash = 5381;
    while (*Name) {
        Hash = ((Hash << 5) + Hash) + *Name++;
    }
    return Hash % SCRIPT_VAR_HASH_SIZE;
}

/************************************************************************/

/**
 * @brief Check if a floating point value represents an integer.
 * @param Value The value to check
 * @return TRUE if value has no fractional part
 */
BOOL IsInteger(F32 Value) {
    return Value == (F32)(INT)Value;
}

/************************************************************************/

void ScriptClearReturnValue(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) {
        return;
    }

    if (Context->HasReturnValue) {
        ScriptReleaseStoredValue(Context, Context->ReturnType, &Context->ReturnValue);
    }

    Context->HasReturnValue = FALSE;
    Context->ReturnTriggered = FALSE;
    Context->ContinueTriggered = FALSE;
    Context->ReturnType = SCRIPT_VAR_FLOAT;
    MemorySet(&Context->ReturnValue, 0, sizeof(SCRIPT_VAR_VALUE));
}

/************************************************************************/

BOOL ScriptStoreReturnValue(LPSCRIPT_CONTEXT Context, const SCRIPT_VALUE* Value) {
    if (Context == NULL || Value == NULL) {
        return FALSE;
    }

    ScriptClearReturnValue(Context);

    if (Value->Type == SCRIPT_VAR_HOST_HANDLE || Value->Type == SCRIPT_VAR_ARRAY) {
        return FALSE;
    }

    Context->ReturnType = Value->Type;
    Context->HasReturnValue = TRUE;
    Context->ReturnTriggered = TRUE;

    if (ScriptStoreObjectValue(
            Context,
            Value->Type,
            &Value->Value,
            &Context->ReturnValue) != SCRIPT_OK) {
        ScriptClearReturnValue(Context);
        return FALSE;
    }
    return TRUE;
}

/************************************************************************/

/**
 * @brief Calculate line and column number from position in input.
 * @param Input Input string
 * @param Position Position in input string
 * @param Line Pointer to receive line number (1-based)
 * @param Column Pointer to receive column number (1-based)
 */
void ScriptCalculateLineColumn(LPCSTR Input, U32 Position, U32* Line, U32* Column) {
    U32 CurrentLine = 1;
    U32 CurrentColumn = 1;

    for (U32 i = 0; i < Position && Input[i] != STR_NULL; i++) {
        if (Input[i] == '\n') {
            CurrentLine++;
            CurrentColumn = 1;
        } else {
            CurrentColumn++;
        }
    }

    *Line = CurrentLine;
    *Column = CurrentColumn;
}
/**
 * @brief Execute an assignment AST node.
 * @param Parser Parser state
 * @param Node Assignment node
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecuteAssignment(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    SCRIPT_VAR_VALUE VarValue;
    SCRIPT_VAR_TYPE VarType;
    LPSCRIPT_CONTEXT Context;

    if (Node == NULL || Node->Type != AST_ASSIGNMENT) {
        return SCRIPT_ERROR_SYNTAX;
    }

    Context = Parser->Context;

    // Prevent assignment to host-exposed identifiers
    if (!Node->Data.Assignment.IsPropertyAccess &&
        ScriptFindHostSymbol(&Context->HostRegistry, Node->Data.Assignment.VarName)) {
        return SCRIPT_ERROR_SYNTAX;
    }

    // Evaluate expression
    SCRIPT_ERROR Error = SCRIPT_OK;
    SCRIPT_VALUE EvaluatedValue = ScriptEvaluateExpression(Parser, Node->Data.Assignment.Expression, &Error);
    if (Error != SCRIPT_OK) {
        ScriptValueRelease(&EvaluatedValue);
        return Error;
    }

    if (EvaluatedValue.Type == SCRIPT_VAR_HOST_HANDLE) {
        ScriptValueRelease(&EvaluatedValue);
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    if (EvaluatedValue.Type == SCRIPT_VAR_STRING) {
        VarType = SCRIPT_VAR_STRING;
        VarValue.String = EvaluatedValue.Value.String;
    } else if (EvaluatedValue.Type == SCRIPT_VAR_INTEGER) {
        VarType = SCRIPT_VAR_INTEGER;
        VarValue.Integer = EvaluatedValue.Value.Integer;
    } else if (EvaluatedValue.Type == SCRIPT_VAR_OBJECT) {
        VarType = SCRIPT_VAR_OBJECT;
        VarValue.Object = EvaluatedValue.Value.Object;
    } else {
        F32 Numeric = (EvaluatedValue.Type == SCRIPT_VAR_FLOAT) ? EvaluatedValue.Value.Float : 0.0f;
        if (IsInteger(Numeric)) {
            VarType = SCRIPT_VAR_INTEGER;
            VarValue.Integer = (INT)Numeric;
        } else {
            VarType = SCRIPT_VAR_FLOAT;
            VarValue.Float = Numeric;
        }
    }

    if (Node->Data.Assignment.IsArrayAccess) {
        // Evaluate array index
        SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Node->Data.Assignment.ArrayIndexExpr, &Error);
        if (Error != SCRIPT_OK) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&IndexValue);
            return Error;
        }

        INT IndexNumeric;
        if (!ScriptValueToInteger(&IndexValue, &IndexNumeric) || IndexNumeric < 0) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&IndexValue);
            return SCRIPT_ERROR_TYPE_MISMATCH;
        }

        U32 ArrayIndex = (U32)IndexNumeric;
        ScriptValueRelease(&IndexValue);

        // Set array element
        if (ScriptSetArrayElement(Context, Node->Data.Assignment.VarName, ArrayIndex, VarType, VarValue) == NULL) {
            ScriptValueRelease(&EvaluatedValue);
            return SCRIPT_ERROR_SYNTAX;
        }
    } else if (Node->Data.Assignment.IsPropertyAccess) {
        SCRIPT_VALUE BaseValue = ScriptEvaluateExpression(
            Parser,
            Node->Data.Assignment.PropertyBaseExpression,
            &Error);
        if (Error != SCRIPT_OK) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&BaseValue);
            return Error;
        }

        if (BaseValue.Type != SCRIPT_VAR_OBJECT || BaseValue.Value.Object == NULL) {
            ScriptValueRelease(&EvaluatedValue);
            ScriptValueRelease(&BaseValue);
            return SCRIPT_ERROR_TYPE_MISMATCH;
        }

        Error = ScriptSetObjectProperty(
            BaseValue.Value.Object,
            Node->Data.Assignment.PropertyName,
            &EvaluatedValue);
        ScriptValueRelease(&BaseValue);
        if (Error != SCRIPT_OK) {
            ScriptValueRelease(&EvaluatedValue);
            return Error;
        }
    } else {
        // Set regular variable in current scope
        if (ScriptSetVariableInScope(Parser->CurrentScope, Node->Data.Assignment.VarName, VarType, VarValue) == NULL) {
            ScriptValueRelease(&EvaluatedValue);
            return SCRIPT_ERROR_SYNTAX;
        }
    }

    ScriptValueRelease(&EvaluatedValue);

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Execute a block AST node.
 * @param Parser Parser state
 * @param Node Block node
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecuteBlock(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    if (Node == NULL || Node->Type != AST_BLOCK) {
        return SCRIPT_ERROR_SYNTAX;
    }

    // Execute all statements in the block without creating a new scope
    // This allows variables created in loops/if bodies to persist
    SCRIPT_ERROR Error = SCRIPT_OK;
    for (U32 i = 0; i < Node->Data.Block.Count; i++) {
        Error = ScriptExecuteAST(Parser, Node->Data.Block.Statements[i]);
        if (Error != SCRIPT_OK || Parser->Context->ReturnTriggered || Parser->Context->ContinueTriggered) {
            break;
        }
    }

    return Error;
}

/************************************************************************/

/**
 * @brief Execute an AST node.
 * @param Parser Parser state
 * @param Node AST node to execute
 * @return Script error code
 */
SCRIPT_ERROR ScriptExecuteAST(LPSCRIPT_PARSER Parser, LPAST_NODE Node) {
    if (Node == NULL) {
        return SCRIPT_OK;
    }

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            return ScriptExecuteAssignment(Parser, Node);

        case AST_BLOCK:
            return ScriptExecuteBlock(Parser, Node);

        case AST_IF: {
            // Evaluate condition
            SCRIPT_ERROR Error = SCRIPT_OK;
            SCRIPT_VALUE ConditionValue = ScriptEvaluateExpression(Parser, Node->Data.If.Condition, &Error);
            if (Error != SCRIPT_OK) {
                ScriptValueRelease(&ConditionValue);
                return Error;
            }

            F32 ConditionNumeric;
            if (!ScriptValueToFloat(&ConditionValue, &ConditionNumeric)) {
                ScriptValueRelease(&ConditionValue);
                return SCRIPT_ERROR_TYPE_MISMATCH;
            }

            ScriptValueRelease(&ConditionValue);

            // Execute then or else branch
            if (ConditionNumeric != 0.0f) {
                return ScriptExecuteAST(Parser, Node->Data.If.Then);
            } else if (Node->Data.If.Else != NULL) {
                return ScriptExecuteAST(Parser, Node->Data.If.Else);
            }

            return SCRIPT_OK;
        }

        case AST_FOR: {
            // Execute initialization
            SCRIPT_ERROR Error = ScriptExecuteAST(Parser, Node->Data.For.Init);
            if (Error != SCRIPT_OK) return Error;
            if (Parser->Context->ReturnTriggered) return SCRIPT_OK;

            // Execute loop
            U32 LoopCount = 0;
            const U32 MAX_ITERATIONS = 1000; // Safety limit

            while (LoopCount < MAX_ITERATIONS) {
                // Evaluate condition
                SCRIPT_VALUE ConditionValue = ScriptEvaluateExpression(Parser, Node->Data.For.Condition, &Error);
                if (Error != SCRIPT_OK) {
                    ScriptValueRelease(&ConditionValue);
                    return Error;
                }

                F32 ConditionNumeric;
                if (!ScriptValueToFloat(&ConditionValue, &ConditionNumeric)) {
                    ScriptValueRelease(&ConditionValue);
                    return SCRIPT_ERROR_TYPE_MISMATCH;
                }

                ScriptValueRelease(&ConditionValue);

                if (ConditionNumeric == 0.0f) break;

                // Execute body
                Error = ScriptExecuteAST(Parser, Node->Data.For.Body);
                if (Error != SCRIPT_OK) return Error;
                if (Parser->Context->ReturnTriggered) return SCRIPT_OK;

                if (Parser->Context->ContinueTriggered) {
                    Parser->Context->ContinueTriggered = FALSE;
                }

                // Execute increment
                Error = ScriptExecuteAST(Parser, Node->Data.For.Increment);
                if (Error != SCRIPT_OK) return Error;
                if (Parser->Context->ReturnTriggered) return SCRIPT_OK;

                LoopCount++;
            }

            if (LoopCount >= MAX_ITERATIONS) {
                ERROR(TEXT("Loop exceeded maximum iterations"));
            }

            return SCRIPT_OK;
        }

        case AST_RETURN: {
            SCRIPT_ERROR Error = SCRIPT_OK;
            SCRIPT_VALUE ReturnValue = ScriptEvaluateExpression(Parser, Node->Data.Return.Expression, &Error);
            if (Error != SCRIPT_OK) {
                ScriptValueRelease(&ReturnValue);
                return Error;
            }

            if (!ScriptStoreReturnValue(Parser->Context, &ReturnValue)) {
                ScriptValueRelease(&ReturnValue);
                return SCRIPT_ERROR_TYPE_MISMATCH;
            }

            ScriptValueRelease(&ReturnValue);
            return SCRIPT_OK;
        }

        case AST_CONTINUE:
            Parser->Context->ContinueTriggered = TRUE;
            return SCRIPT_OK;

        case AST_EXPRESSION: {
            // Standalone expression - evaluate it (for function calls)
            SCRIPT_ERROR Error = SCRIPT_OK;
            SCRIPT_VALUE Temp = ScriptEvaluateExpression(Parser, Node, &Error);
            ScriptValueRelease(&Temp);
            return Error;
        }

        default:
            return SCRIPT_ERROR_SYNTAX;
    }
}
