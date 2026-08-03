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


    Script Engine - Evaluation

\************************************************************************/

#include "Base.h"
#include "memory/Heap.h"
#include "utils/List.h"
#include "log/Log.h"
#include "text/CoreString.h"
#include "script/Script.h"
#include "script/Script-Internal.h"

/************************************************************************/
/**
 * @brief Check if a string is a script keyword.
 * @param Str String to check
 * @return TRUE if the string is a keyword
 */
BOOL ScriptIsKeyword(LPCSTR Str) {
    return (StringCompare(Str, TEXT("if")) == 0 ||
            StringCompare(Str, TEXT("else")) == 0 ||
            StringCompare(Str, TEXT("for")) == 0 ||
            StringCompare(Str, TEXT("return")) == 0 ||
            StringCompare(Str, TEXT("continue")) == 0);
}

/************************************************************************/

BOOL ScriptValueIsTrue(const SCRIPT_VALUE* Value, BOOL* OutValue) {
    F32 NumericValue = 0.0f;

    if (Value == NULL || OutValue == NULL) {
        return FALSE;
    }

    if (!ScriptValueToFloat(Value, &NumericValue)) {
        return FALSE;
    }

    *OutValue = (NumericValue != 0.0f);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Evaluate one binary integer-only operator.
 * @param LeftValue Left operand.
 * @param RightValue Right operand.
 * @param Operator Operator token text.
 * @param Result Receives the integer result.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
static SCRIPT_ERROR ScriptEvaluateBinaryIntegerOperator(
    const SCRIPT_VALUE* LeftValue,
    const SCRIPT_VALUE* RightValue,
    LPCSTR Operator,
    SCRIPT_VALUE* Result) {
    INT LeftInteger = 0;
    INT RightInteger = 0;
    UINT ShiftBits = (UINT)(sizeof(INT) * 8);

    if (LeftValue == NULL || RightValue == NULL || Operator == NULL || Result == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (!ScriptValueToInteger(LeftValue, &LeftInteger) ||
        !ScriptValueToInteger(RightValue, &RightInteger)) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    Result->Type = SCRIPT_VAR_INTEGER;

    if (StringCompare(Operator, TEXT("&")) == 0) {
        Result->Value.Integer = LeftInteger & RightInteger;
        return SCRIPT_OK;
    }

    if (StringCompare(Operator, TEXT("|")) == 0) {
        Result->Value.Integer = LeftInteger | RightInteger;
        return SCRIPT_OK;
    }

    if (StringCompare(Operator, TEXT("^")) == 0) {
        Result->Value.Integer = LeftInteger ^ RightInteger;
        return SCRIPT_OK;
    }

    if (RightInteger < 0 || (UINT)RightInteger >= ShiftBits) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    if (StringCompare(Operator, TEXT("<<")) == 0) {
        Result->Value.Integer = LeftInteger << RightInteger;
        return SCRIPT_OK;
    }

    if (StringCompare(Operator, TEXT(">>")) == 0) {
        Result->Value.Integer = LeftInteger >> RightInteger;
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_SYNTAX;
}

/************************************************************************/

/**
 * @brief Release one temporary function-call argument vector.
 * @param Context Script context that owns the temporary allocations.
 * @param Arguments Argument string array.
 * @param OwnedArguments Ownership flags for each argument string.
 * @param ArgumentCount Number of argument strings.
 */
static void ScriptReleaseFunctionArguments(
    LPSCRIPT_CONTEXT Context,
    LPCSTR* Arguments,
    BOOL* OwnedArguments,
    UINT ArgumentCount) {
    UINT Index;

    if (Context == NULL) {
        return;
    }

    if (Arguments != NULL && OwnedArguments != NULL) {
        for (Index = 0; Index < ArgumentCount; Index++) {
            if (OwnedArguments[Index] && Arguments[Index] != NULL) {
                ScriptFree(Context, (LPVOID)Arguments[Index]);
            }
        }
    }

    if (OwnedArguments != NULL) {
        ScriptFree(Context, OwnedArguments);
    }

    if (Arguments != NULL) {
        ScriptFree(Context, Arguments);
    }
}

/************************************************************************/

/**
 * @brief Evaluate and stringify one function-call argument list.
 * @param Parser Parser state.
 * @param Expr Function-call AST expression node.
 * @param OutArguments Receives the argument string vector.
 * @param OutOwnedArguments Receives ownership flags for each argument string.
 * @param OutArgumentCount Receives the number of stringified arguments.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
static SCRIPT_ERROR ScriptBuildFunctionArguments(
    LPSCRIPT_PARSER Parser,
    LPAST_NODE Expr,
    LPCSTR** OutArguments,
    BOOL** OutOwnedArguments,
    UINT* OutArgumentCount) {
    UINT ArgumentCount;
    LPCSTR* Arguments = NULL;
    BOOL* OwnedArguments = NULL;
    UINT Index = 0;
    LPAST_NODE ArgumentNode;

    if (Parser == NULL || Expr == NULL || OutArguments == NULL || OutOwnedArguments == NULL || OutArgumentCount == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    *OutArguments = NULL;
    *OutOwnedArguments = NULL;
    *OutArgumentCount = 0;

    ArgumentCount = Expr->Data.Expression.ArgumentCount;
    if (ArgumentCount == 0) {
        return SCRIPT_OK;
    }

    Arguments = (LPCSTR*)ScriptAlloc(Parser->Context, sizeof(LPCSTR) * ArgumentCount);
    OwnedArguments = (BOOL*)ScriptAlloc(Parser->Context, sizeof(BOOL) * ArgumentCount);
    if (Arguments == NULL || OwnedArguments == NULL) {
        if (Arguments != NULL) {
            ScriptFree(Parser->Context, Arguments);
        }
        if (OwnedArguments != NULL) {
            ScriptFree(Parser->Context, OwnedArguments);
        }
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemorySet(Arguments, 0, sizeof(LPCSTR) * ArgumentCount);
    MemorySet(OwnedArguments, 0, sizeof(BOOL) * ArgumentCount);

    ArgumentNode = Expr->Data.Expression.FirstArgument;
    while (ArgumentNode != NULL && Index < ArgumentCount) {
        SCRIPT_ERROR EvaluationError = SCRIPT_OK;
        LPSTR StableArgument = NULL;
        U32 StableLength = 0;
        SCRIPT_VALUE ArgumentValue = ScriptEvaluateExpression(Parser, ArgumentNode, &EvaluationError);
        if (EvaluationError != SCRIPT_OK) {
            ScriptValueRelease(&ArgumentValue);
            ScriptReleaseFunctionArguments(Parser->Context, Arguments, OwnedArguments, ArgumentCount);
            return EvaluationError;
        }

        SCRIPT_ERROR Result = ScriptValueToString(
            &ArgumentValue,
            Parser->Context,
            &Arguments[Index],
            &OwnedArguments[Index]);

        if (Result != SCRIPT_OK) {
            ScriptValueRelease(&ArgumentValue);
            ScriptReleaseFunctionArguments(Parser->Context, Arguments, OwnedArguments, ArgumentCount);
            return Result;
        }

        StableLength = StringLength(Arguments[Index]) + 1;
        StableArgument = (LPSTR)ScriptAlloc(Parser->Context, StableLength);
        if (StableArgument == NULL) {
            if (OwnedArguments[Index] && Arguments[Index] != NULL) {
                ScriptFree(Parser->Context, (LPVOID)Arguments[Index]);
            }
            ScriptValueRelease(&ArgumentValue);
            ScriptReleaseFunctionArguments(Parser->Context, Arguments, OwnedArguments, ArgumentCount);
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        StringCopy(StableArgument, Arguments[Index]);
        if (OwnedArguments[Index] && Arguments[Index] != NULL) {
            ScriptFree(Parser->Context, (LPVOID)Arguments[Index]);
        }
        Arguments[Index] = StableArgument;
        OwnedArguments[Index] = TRUE;

        ScriptValueRelease(&ArgumentValue);

        Index++;
        ArgumentNode = ArgumentNode->Data.Expression.NextArgument;
    }

    if (ArgumentNode != NULL || Index != ArgumentCount) {
        ScriptReleaseFunctionArguments(Parser->Context, Arguments, OwnedArguments, ArgumentCount);
        return SCRIPT_ERROR_SYNTAX;
    }

    *OutArguments = Arguments;
    *OutOwnedArguments = OwnedArguments;
    *OutArgumentCount = ArgumentCount;
    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Evaluate an expression AST node and return its value.
 * @param Parser Parser state (for variable/callback access)
 * @param Expr Expression node
 * @param Error Pointer to error code
 * @return Expression value
 */
SCRIPT_VALUE ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    if (Error) {
        *Error = SCRIPT_OK;
    }

    if (Expr == NULL || Expr->Type != AST_EXPRESSION) {
        if (Error) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return Result;
    }

    if (Expr->Data.Expression.IsPropertyAccess) {
        return ScriptEvaluateHostProperty(Parser, Expr, Error);
    }

    if (Expr->Data.Expression.IsArrayAccess && Expr->Data.Expression.BaseExpression) {
        return ScriptEvaluateArrayAccess(Parser, Expr, Error);
    }

    switch (Expr->Data.Expression.TokenType) {
        case TOKEN_NUMBER:
            if (Expr->Data.Expression.IsIntegerLiteral) {
                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = Expr->Data.Expression.IntegerValue;
            } else {
                Result.Type = SCRIPT_VAR_FLOAT;
                Result.Value.Float = Expr->Data.Expression.FloatValue;
            }
            return Result;

        case TOKEN_LBRACE:
            Result.Type = SCRIPT_VAR_OBJECT;
            Result.Value.Object = ScriptCreateObject(Parser->Context, 0);
            if (Result.Value.Object == NULL) {
                if (Error) {
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                }
                return Result;
            }
            Result.ContextOwner = Parser->Context;
            Result.OwnsValue = TRUE;
            return Result;

        case TOKEN_STRING: {
            U32 Length = StringLength(Expr->Data.Expression.Value) + 1;
            Result.Value.String = (LPSTR)ScriptAlloc(Parser->Context, Length);
            if (Result.Value.String == NULL) {
                if (Error) {
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                }
                return Result;
            }
            StringCopy(Result.Value.String, Expr->Data.Expression.Value);
            Result.Type = SCRIPT_VAR_STRING;
            Result.ContextOwner = Parser->Context;
            Result.OwnsValue = TRUE;
            return Result;
        }

        case TOKEN_IDENTIFIER:
        case TOKEN_PATH: {
            if (Expr->Data.Expression.IsFunctionCall) {
                if (Expr->Data.Expression.IsShellCommand) {
                    if (Parser->Callbacks && Parser->Callbacks->ExecuteCommand) {
                        LPCSTR CommandLine = Expr->Data.Expression.CommandLine ?
                            Expr->Data.Expression.CommandLine : Expr->Data.Expression.Value;
                        UINT Status = Parser->Callbacks->ExecuteCommand(CommandLine, Parser->Callbacks->UserData);

                        if (Status == DF_RETURN_SUCCESS) {
                            Result.Type = SCRIPT_VAR_INTEGER;
                            Result.Value.Integer = (INT)Status;
                            return Result;
                        }

                        LPSCRIPT_CONTEXT Context = Parser->Context;
                        if (Context) {
                            Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                            if (Context->ErrorMessage[0] == STR_NULL) {
                                StringPrintFormat(Context->ErrorMessage, TEXT("Command failed (0x%08X)"), Status);
                            }
                        }

                        if (Error) {
                            *Error = SCRIPT_ERROR_SYNTAX;
                        }
                        return Result;
                    }

                    LPSCRIPT_CONTEXT Context = Parser->Context;
                    if (Context) {
                        Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                        if (Context->ErrorMessage[0] == STR_NULL) {
                            StringCopy(Context->ErrorMessage, TEXT("No command callback registered"));
                        }
                    }

                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                    return Result;
                }

                if (Expr->Data.Expression.TokenType == TOKEN_PATH) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                    return Result;
                }

                if (Parser->Callbacks && Parser->Callbacks->CallFunction) {
                    LPCSTR* Arguments = NULL;
                    BOOL* OwnedArguments = NULL;
                    UINT ArgumentCount = 0;
                    SCRIPT_ERROR ArgumentError = ScriptBuildFunctionArguments(
                        Parser,
                        Expr,
                        &Arguments,
                        &OwnedArguments,
                        &ArgumentCount);
                    if (ArgumentError != SCRIPT_OK) {
                        if (Error) {
                            *Error = ArgumentError;
                        }
                        return Result;
                    }

                    INT Status = Parser->Callbacks->CallFunction(
                        Expr->Data.Expression.Value,
                        ArgumentCount,
                        (LPCSTR*)Arguments,
                        Parser->Callbacks->UserData);
                    ScriptReleaseFunctionArguments(Parser->Context, Arguments, OwnedArguments, ArgumentCount);

                    if (Status == SCRIPT_FUNCTION_STATUS_UNKNOWN) {
                        LPSCRIPT_CONTEXT Context = Parser->Context;
                        if (Context) {
                            Context->ErrorCode = SCRIPT_ERROR_UNDEFINED_VAR;
                            if (Context->ErrorMessage[0] == STR_NULL) {
                                StringPrintFormat(
                                    Context->ErrorMessage,
                                    TEXT("Unknown function: %s"),
                                    Expr->Data.Expression.Value);
                            }
                        }

                        if (Error) {
                            *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                        }
                        return Result;
                    }

                    if (Status == SCRIPT_FUNCTION_STATUS_ERROR) {
                        LPSCRIPT_CONTEXT Context = Parser->Context;
                        SCRIPT_ERROR FunctionError = SCRIPT_ERROR_SYNTAX;

                        if (Context) {
                            if (Context->ErrorCode != SCRIPT_OK) {
                                FunctionError = Context->ErrorCode;
                            } else {
                                Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                            }

                            if (Context->ErrorMessage[0] == STR_NULL) {
                                StringPrintFormat(
                                    Context->ErrorMessage,
                                    TEXT("Function call failed: %s"),
                                    Expr->Data.Expression.Value);
                            }
                        }

                        if (Error) {
                            *Error = FunctionError;
                        }
                        return Result;
                    }

                    Result.Type = SCRIPT_VAR_INTEGER;
                    Result.Value.Integer = (INT)Status;
                    return Result;
                }

                LPSCRIPT_CONTEXT Context = Parser->Context;
                if (Context) {
                    Context->ErrorCode = SCRIPT_ERROR_SYNTAX;
                    if (Context->ErrorMessage[0] == STR_NULL) {
                        StringCopy(Context->ErrorMessage, TEXT("No function callback registered"));
                    }
                }

                if (Error) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                }
                return Result;
            }

            if (Expr->Data.Expression.IsArrayAccess && Expr->Data.Expression.BaseExpression == NULL) {
                SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.ArrayIndexExpr, Error);
                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&IndexValue);
                    return Result;
                }

                INT IndexNumeric;
                if (!ScriptValueToInteger(&IndexValue, &IndexNumeric) || IndexNumeric < 0) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&IndexValue);
                    return Result;
                }

                U32 ArrayIndex = (U32)IndexNumeric;
                ScriptValueRelease(&IndexValue);

                LPSCRIPT_HOST_SYMBOL HostArray = ScriptFindHostSymbol(&Parser->Context->HostRegistry, Expr->Data.Expression.Value);
                if (HostArray) {
                    if (HostArray->Descriptor == NULL || HostArray->Descriptor->GetElement == NULL) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                        }
                        return Result;
                    }

                    SCRIPT_VALUE HostValue;
                    ScriptValueInit(&HostValue);
                    LPVOID HostCtx = HostArray->Context ? HostArray->Context : HostArray->Descriptor->Context;
                    SCRIPT_ERROR HostError = HostArray->Descriptor->GetElement(HostCtx, HostArray->Handle, ArrayIndex, &HostValue);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    HostError = ScriptPrepareHostValue(Parser->Context, &HostValue, HostArray->Descriptor, HostCtx);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    return HostValue;
                }

                LPSCRIPT_VARIABLE Element = ScriptGetArrayElement(Parser->Context, Expr->Data.Expression.Value, ArrayIndex);
                if (Element == NULL) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                    }
                    return Result;
                }

                Result.Type = Element->Type;
                Result.Value = Element->Value;
                Result.OwnsValue = FALSE;

                ScriptFree(Parser->Context, Element);
                return Result;
            }

            LPSCRIPT_HOST_SYMBOL HostSymbol = ScriptFindHostSymbol(&Parser->Context->HostRegistry, Expr->Data.Expression.Value);
            if (HostSymbol) {
                LPVOID HostCtx = HostSymbol->Context ? HostSymbol->Context : HostSymbol->Descriptor->Context;

                if (HostSymbol->Kind == SCRIPT_HOST_SYMBOL_PROPERTY) {
                    if (HostSymbol->Descriptor == NULL || HostSymbol->Descriptor->GetProperty == NULL) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                        }
                        return Result;
                    }

                    SCRIPT_VALUE HostValue;
                    ScriptValueInit(&HostValue);
                    SCRIPT_ERROR HostError = HostSymbol->Descriptor->GetProperty(
                        HostCtx,
                        HostSymbol->Handle,
                        HostSymbol->Name,
                        &HostValue);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    HostError = ScriptPrepareHostValue(Parser->Context, &HostValue, HostSymbol->Descriptor, HostCtx);
                    if (HostError != SCRIPT_OK) {
                        if (Error) {
                            *Error = HostError;
                        }
                        ScriptValueRelease(&HostValue);
                        return Result;
                    }

                    return HostValue;
                }

                Result.Type = SCRIPT_VAR_HOST_HANDLE;
                Result.Value.HostHandle = HostSymbol->Handle;
                Result.HostDescriptor = HostSymbol->Descriptor;
                Result.HostContext = HostCtx;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_PATH) {
                if (Error) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                }
                return Result;
            }

            LPSCRIPT_VARIABLE Variable = ScriptFindVariableInScope(Parser->CurrentScope, Expr->Data.Expression.Value, TRUE);
            if (Variable == NULL) {
                if (Error) {
                    *Error = SCRIPT_ERROR_UNDEFINED_VAR;
                }
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_INTEGER) {
                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = Variable->Value.Integer;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_FLOAT) {
                Result.Type = SCRIPT_VAR_FLOAT;
                Result.Value.Float = Variable->Value.Float;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_STRING) {
                Result.Type = SCRIPT_VAR_STRING;
                Result.Value.String = Variable->Value.String;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_OBJECT) {
                Result.Type = SCRIPT_VAR_OBJECT;
                Result.Value.Object = Variable->Value.Object;
                Result.ContextOwner = Parser->Context;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Variable->Type == SCRIPT_VAR_ARRAY) {
                Result.Type = SCRIPT_VAR_ARRAY;
                Result.Value.Array = Variable->Value.Array;
                Result.ContextOwner = Parser->Context;
                Result.OwnsValue = FALSE;
                return Result;
            }

            if (Error) {
                *Error = SCRIPT_ERROR_TYPE_MISMATCH;
            }
            return Result;
        }

        case TOKEN_OPERATOR:
        case TOKEN_COMPARISON: {
            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR &&
                StringCompare(Expr->Data.Expression.Value, TEXT("!")) == 0) {
                SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
                BOOL IsTrue = FALSE;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                if (!ScriptValueIsTrue(&RightValue, &IsTrue)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = IsTrue ? 0 : 1;
                ScriptValueRelease(&RightValue);
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR &&
                StringCompare(Expr->Data.Expression.Value, TEXT("~")) == 0) {
                SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
                INT RightInteger = 0;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                if (!ScriptValueToInteger(&RightValue, &RightInteger)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = ~RightInteger;
                ScriptValueRelease(&RightValue);
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR &&
                StringCompare(Expr->Data.Expression.Value, TEXT("&&")) == 0) {
                SCRIPT_VALUE LeftValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
                BOOL LeftIsTrue = FALSE;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                if (!ScriptValueIsTrue(&LeftValue, &LeftIsTrue)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                if (!LeftIsTrue) {
                    Result.Type = SCRIPT_VAR_INTEGER;
                    Result.Value.Integer = 0;
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
                BOOL RightIsTrue = FALSE;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                if (!ScriptValueIsTrue(&RightValue, &RightIsTrue)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = RightIsTrue ? 1 : 0;
                ScriptValueRelease(&LeftValue);
                ScriptValueRelease(&RightValue);
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR &&
                StringCompare(Expr->Data.Expression.Value, TEXT("||")) == 0) {
                SCRIPT_VALUE LeftValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
                BOOL LeftIsTrue = FALSE;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                if (!ScriptValueIsTrue(&LeftValue, &LeftIsTrue)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                if (LeftIsTrue) {
                    Result.Type = SCRIPT_VAR_INTEGER;
                    Result.Value.Integer = 1;
                    ScriptValueRelease(&LeftValue);
                    return Result;
                }

                SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
                BOOL RightIsTrue = FALSE;

                if (Error && *Error != SCRIPT_OK) {
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                if (!ScriptValueIsTrue(&RightValue, &RightIsTrue)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                Result.Type = SCRIPT_VAR_INTEGER;
                Result.Value.Integer = RightIsTrue ? 1 : 0;
                ScriptValueRelease(&LeftValue);
                ScriptValueRelease(&RightValue);
                return Result;
            }

            SCRIPT_VALUE LeftValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Left, Error);
            if (Error && *Error != SCRIPT_OK) {
                ScriptValueRelease(&LeftValue);
                return Result;
            }

            SCRIPT_VALUE RightValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.Right, Error);
            if (Error && *Error != SCRIPT_OK) {
                ScriptValueRelease(&LeftValue);
                ScriptValueRelease(&RightValue);
                return Result;
            }

            if (Expr->Data.Expression.TokenType == TOKEN_OPERATOR) {
                STR Operator = Expr->Data.Expression.Value[0];
                LPCSTR OperatorText = Expr->Data.Expression.Value;
                SCRIPT_ERROR StringError = SCRIPT_OK;

                if (Operator == '+') {
                    if (LeftValue.Type == SCRIPT_VAR_STRING || RightValue.Type == SCRIPT_VAR_STRING) {
                        StringError = ScriptConcatStrings(&LeftValue, &RightValue, &Result);
                        if (StringError != SCRIPT_OK && Error) {
                            *Error = StringError;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }
                } else if (Operator == '-') {
                    if (LeftValue.Type == SCRIPT_VAR_STRING || RightValue.Type == SCRIPT_VAR_STRING) {
                        StringError = ScriptRemoveStringOccurrences(&LeftValue, &RightValue, &Result);
                        if (StringError != SCRIPT_OK && Error) {
                            *Error = StringError;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }
                }

                if (StringCompare(OperatorText, TEXT("&")) == 0 ||
                    StringCompare(OperatorText, TEXT("|")) == 0 ||
                    StringCompare(OperatorText, TEXT("^")) == 0 ||
                    StringCompare(OperatorText, TEXT("<<")) == 0 ||
                    StringCompare(OperatorText, TEXT(">>")) == 0) {
                    SCRIPT_ERROR IntegerError = ScriptEvaluateBinaryIntegerOperator(
                        &LeftValue,
                        &RightValue,
                        OperatorText,
                        &Result);
                    if (IntegerError != SCRIPT_OK && Error) {
                        *Error = IntegerError;
                    }
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                F32 LeftNumeric;
                F32 RightNumeric;
                if (!ScriptValueToFloat(&LeftValue, &LeftNumeric) ||
                    !ScriptValueToFloat(&RightValue, &RightNumeric)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                if (LeftValue.Type == SCRIPT_VAR_INTEGER &&
                    RightValue.Type == SCRIPT_VAR_INTEGER) {
                    Result.Type = SCRIPT_VAR_INTEGER;

                    if (Operator == '+') {
                        Result.Value.Integer = LeftValue.Value.Integer + RightValue.Value.Integer;
                    } else if (Operator == '-') {
                        Result.Value.Integer = LeftValue.Value.Integer - RightValue.Value.Integer;
                    } else if (Operator == '*') {
                        Result.Value.Integer = LeftValue.Value.Integer * RightValue.Value.Integer;
                    } else if (Operator == '/') {
                        if (RightValue.Value.Integer == 0) {
                            if (Error) {
                                *Error = SCRIPT_ERROR_DIVISION_BY_ZERO;
                            }
                            ScriptValueRelease(&LeftValue);
                            ScriptValueRelease(&RightValue);
                            return Result;
                        }

                        Result.Value.Integer = LeftValue.Value.Integer / RightValue.Value.Integer;
                    } else {
                        if (Error) {
                            *Error = SCRIPT_ERROR_SYNTAX;
                        }
                    }
                } else {
                    Result.Type = SCRIPT_VAR_FLOAT;

                    if (Operator == '+') {
                        Result.Value.Float = LeftNumeric + RightNumeric;
                    } else if (Operator == '-') {
                        Result.Value.Float = LeftNumeric - RightNumeric;
                    } else if (Operator == '*') {
                        Result.Value.Float = LeftNumeric * RightNumeric;
                    } else if (Operator == '/') {
                        if (RightNumeric == 0.0f) {
                            if (Error) {
                                *Error = SCRIPT_ERROR_DIVISION_BY_ZERO;
                            }
                            ScriptValueRelease(&LeftValue);
                            ScriptValueRelease(&RightValue);
                            return Result;
                        }

                        Result.Value.Float = LeftNumeric / RightNumeric;
                    } else {
                        if (Error) {
                            *Error = SCRIPT_ERROR_SYNTAX;
                        }
                    }
                }
            } else {
                if ((LeftValue.Type == SCRIPT_VAR_STRING || RightValue.Type == SCRIPT_VAR_STRING) &&
                    (StringCompare(Expr->Data.Expression.Value, TEXT("==")) == 0 ||
                     StringCompare(Expr->Data.Expression.Value, TEXT("!=")) == 0)) {
                    if (LeftValue.Type != SCRIPT_VAR_STRING || RightValue.Type != SCRIPT_VAR_STRING) {
                        if (Error) {
                            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                        }
                        ScriptValueRelease(&LeftValue);
                        ScriptValueRelease(&RightValue);
                        return Result;
                    }

                    Result.Type = SCRIPT_VAR_INTEGER;

                    if (StringCompare(Expr->Data.Expression.Value, TEXT("==")) == 0) {
                        Result.Value.Integer = (StringCompare(
                            LeftValue.Value.String ? LeftValue.Value.String : TEXT(""),
                            RightValue.Value.String ? RightValue.Value.String : TEXT("")) == 0) ? 1 : 0;
                    } else {
                        Result.Value.Integer = (StringCompare(
                            LeftValue.Value.String ? LeftValue.Value.String : TEXT(""),
                            RightValue.Value.String ? RightValue.Value.String : TEXT("")) != 0) ? 1 : 0;
                    }

                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                F32 LeftNumeric;
                F32 RightNumeric;

                if (!ScriptValueToFloat(&LeftValue, &LeftNumeric) ||
                    !ScriptValueToFloat(&RightValue, &RightNumeric)) {
                    if (Error) {
                        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
                    }
                    ScriptValueRelease(&LeftValue);
                    ScriptValueRelease(&RightValue);
                    return Result;
                }

                Result.Type = SCRIPT_VAR_INTEGER;

                if (StringCompare(Expr->Data.Expression.Value, TEXT("<")) == 0) {
                    Result.Value.Integer = (LeftNumeric < RightNumeric) ? 1 : 0;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("<=")) == 0) {
                    Result.Value.Integer = (LeftNumeric <= RightNumeric) ? 1 : 0;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT(">")) == 0) {
                    Result.Value.Integer = (LeftNumeric > RightNumeric) ? 1 : 0;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT(">=")) == 0) {
                    Result.Value.Integer = (LeftNumeric >= RightNumeric) ? 1 : 0;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("==")) == 0) {
                    Result.Value.Integer = (LeftNumeric == RightNumeric) ? 1 : 0;
                } else if (StringCompare(Expr->Data.Expression.Value, TEXT("!=")) == 0) {
                    Result.Value.Integer = (LeftNumeric != RightNumeric) ? 1 : 0;
                } else {
                    if (Error) {
                        *Error = SCRIPT_ERROR_SYNTAX;
                    }
                }
            }

            ScriptValueRelease(&LeftValue);
            ScriptValueRelease(&RightValue);
            return Result;
        }

        default:
            if (Error) {
                *Error = SCRIPT_ERROR_SYNTAX;
            }
            return Result;
    }
}

/************************************************************************/

SCRIPT_VALUE ScriptEvaluateHostProperty(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    SCRIPT_VALUE BaseValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.BaseExpression, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    if (BaseValue.Type != SCRIPT_VAR_HOST_HANDLE || BaseValue.HostDescriptor == NULL ||
        BaseValue.HostDescriptor->GetProperty == NULL) {
        if (BaseValue.Type == SCRIPT_VAR_OBJECT && BaseValue.Value.Object != NULL) {
            SCRIPT_ERROR ObjectError = ScriptGetObjectProperty(
                BaseValue.Value.Object,
                Expr->Data.Expression.PropertyName,
                &Result);
            ScriptValueRelease(&BaseValue);
            if (ObjectError != SCRIPT_OK) {
                if (Error) {
                    *Error = ObjectError;
                }
            }
            return Result;
        }

        if (Error) {
            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
        }
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    LPVOID HostCtx = BaseValue.HostContext ? BaseValue.HostContext : BaseValue.HostDescriptor->Context;
    const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor = BaseValue.HostDescriptor;

    SCRIPT_VALUE HostValue;
    ScriptValueInit(&HostValue);
    SCRIPT_ERROR HostError = BaseValue.HostDescriptor->GetProperty(
        HostCtx,
        BaseValue.Value.HostHandle,
        Expr->Data.Expression.PropertyName,
        &HostValue);

    ScriptValueRelease(&BaseValue);

    if (HostError != SCRIPT_OK) {
        if (Error) {
            *Error = HostError;
        }
        ScriptValueRelease(&HostValue);
        return Result;
    }

    HostError = ScriptPrepareHostValue(
        Parser->Context,
        &HostValue,
        HostValue.HostDescriptor ? HostValue.HostDescriptor : DefaultDescriptor,
        HostCtx);
    if (HostError != SCRIPT_OK) {
        if (Error) {
            *Error = HostError;
        }
        ScriptValueRelease(&HostValue);
        return Result;
    }

    if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostDescriptor == NULL) {
        HostValue.HostDescriptor = DefaultDescriptor;
    }
    if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostContext == NULL) {
        HostValue.HostContext = HostCtx;
    }

    return HostValue;
}

/************************************************************************/

SCRIPT_VALUE ScriptEvaluateArrayAccess(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error) {
    SCRIPT_VALUE Result;
    ScriptValueInit(&Result);

    SCRIPT_VALUE BaseValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.BaseExpression, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        return Result;
    }

    SCRIPT_VALUE IndexValue = ScriptEvaluateExpression(Parser, Expr->Data.Expression.ArrayIndexExpr, Error);
    if (Error && *Error != SCRIPT_OK) {
        ScriptValueRelease(&BaseValue);
        ScriptValueRelease(&IndexValue);
        return Result;
    }

    INT IndexNumeric;
    if (!ScriptValueToInteger(&IndexValue, &IndexNumeric) || IndexNumeric < 0) {
        if (Error) {
            *Error = SCRIPT_ERROR_TYPE_MISMATCH;
        }
        ScriptValueRelease(&BaseValue);
        ScriptValueRelease(&IndexValue);
        return Result;
    }

    ScriptValueRelease(&IndexValue);

    if (BaseValue.Type == SCRIPT_VAR_HOST_HANDLE &&
        BaseValue.HostDescriptor && BaseValue.HostDescriptor->GetElement) {
        LPVOID HostCtx = BaseValue.HostContext ? BaseValue.HostContext : BaseValue.HostDescriptor->Context;
        const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor = BaseValue.HostDescriptor;

        SCRIPT_VALUE HostValue;
        ScriptValueInit(&HostValue);
        SCRIPT_ERROR HostError = BaseValue.HostDescriptor->GetElement(
            HostCtx,
            BaseValue.Value.HostHandle,
            (U32)IndexNumeric,
            &HostValue);

        ScriptValueRelease(&BaseValue);

        if (HostError != SCRIPT_OK) {
            if (Error) {
                *Error = HostError;
            }
            ScriptValueRelease(&HostValue);
            return Result;
        }

        HostError = ScriptPrepareHostValue(Parser->Context, &HostValue, DefaultDescriptor, HostCtx);
        if (HostError != SCRIPT_OK) {
            if (Error) {
                *Error = HostError;
            }
            ScriptValueRelease(&HostValue);
            return Result;
        }

        if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostDescriptor == NULL) {
            HostValue.HostDescriptor = DefaultDescriptor;
        }
        if (HostValue.Type == SCRIPT_VAR_HOST_HANDLE && HostValue.HostContext == NULL) {
            HostValue.HostContext = HostCtx;
        }

        return HostValue;
    }

    if (BaseValue.Type == SCRIPT_VAR_ARRAY && BaseValue.Value.Array != NULL) {
        SCRIPT_VAR_TYPE ElementType;
        SCRIPT_VAR_VALUE ElementValue;
        SCRIPT_ERROR ArrayError = ScriptArrayGet(
            BaseValue.Value.Array,
            (U32)IndexNumeric,
            &ElementType,
            &ElementValue);

        ScriptValueRelease(&BaseValue);

        if (ArrayError != SCRIPT_OK) {
            if (Error) {
                *Error = ArrayError;
            }
            return Result;
        }

        Result.Type = ElementType;
        Result.Value = ElementValue;
        Result.ContextOwner = Parser->Context;
        Result.OwnsValue = FALSE;
        return Result;
    }

    ScriptValueRelease(&BaseValue);

    if (Error) {
        *Error = SCRIPT_ERROR_TYPE_MISMATCH;
    }
    return Result;
}
