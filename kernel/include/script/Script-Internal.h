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


    Script Engine - Internal API

\************************************************************************/

#ifndef SCRIPT_INTERNAL_H_INCLUDED
#define SCRIPT_INTERNAL_H_INCLUDED

#include "script/Script.h"

/************************************************************************/

U32 ScriptHashVariable(LPCSTR Name);
void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable);
LPVOID ScriptAlloc(LPSCRIPT_CONTEXT Context, UINT Size);
LPVOID ScriptRealloc(LPSCRIPT_CONTEXT Context, LPVOID Pointer, UINT Size);
void ScriptFree(LPSCRIPT_CONTEXT Context, LPVOID Pointer);

void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_CONTEXT Context);
void ScriptNextToken(LPSCRIPT_PARSER Parser);
void ScriptParseStringToken(LPSCRIPT_PARSER Parser, LPCSTR Input, U32* Pos, STR QuoteChar);
LPAST_NODE ScriptParseExpressionAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseComparisonAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseLogicalOrAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseLogicalAndAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseTermAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseFactorAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseAssignmentAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseBlockAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseIfStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseForStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseReturnStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseContinueStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
LPAST_NODE ScriptParseShellCommandExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error);
BOOL ScriptShouldParseShellCommand(LPSCRIPT_PARSER Parser);
BOOL ScriptIsKeyword(LPCSTR Str);

SCRIPT_ERROR ScriptPrepareHostValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VALUE* Value,
    const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor,
    LPVOID DefaultContext);
BOOL ScriptValueToFloat(const SCRIPT_VALUE* Value, F32* OutValue);
BOOL ScriptValueToInteger(const SCRIPT_VALUE* Value, INT* OutValue);
BOOL ScriptValueIsTrue(const SCRIPT_VALUE* Value, BOOL* OutValue);
SCRIPT_ERROR ScriptValueToString(
    const SCRIPT_VALUE* Value,
    LPSCRIPT_CONTEXT Context,
    LPCSTR* OutText,
    BOOL* OutOwnsText);
SCRIPT_ERROR ScriptConcatStrings(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result);
SCRIPT_ERROR ScriptRemoveStringOccurrences(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result);
SCRIPT_VALUE ScriptEvaluateExpression(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
SCRIPT_VALUE ScriptEvaluateHostProperty(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
SCRIPT_VALUE ScriptEvaluateArrayAccess(LPSCRIPT_PARSER Parser, LPAST_NODE Expr, SCRIPT_ERROR* Error);
SCRIPT_ERROR ScriptExecuteAssignment(LPSCRIPT_PARSER Parser, LPAST_NODE Node);
SCRIPT_ERROR ScriptExecuteBlock(LPSCRIPT_PARSER Parser, LPAST_NODE Node);

BOOL IsInteger(F32 Value);
void ScriptCalculateLineColumn(LPCSTR Input, U32 Position, U32* Line, U32* Column);
void ScriptClearReturnValue(LPSCRIPT_CONTEXT Context);
BOOL ScriptStoreReturnValue(LPSCRIPT_CONTEXT Context, const SCRIPT_VALUE* Value);
SCRIPT_ERROR ScriptStoreObjectValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VAR_TYPE Type,
    const SCRIPT_VAR_VALUE* SourceValue,
    SCRIPT_VAR_VALUE* DestinationValue);
void ScriptReleaseStoredValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VAR_TYPE Type,
    SCRIPT_VAR_VALUE* Value);

U32 ScriptHashHostSymbol(LPCSTR Name);
BOOL ScriptInitHostRegistry(LPSCRIPT_CONTEXT Context, LPSCRIPT_HOST_REGISTRY Registry);
void ScriptClearHostRegistryInternal(LPSCRIPT_HOST_REGISTRY Registry);
LPSCRIPT_HOST_SYMBOL ScriptFindHostSymbol(LPSCRIPT_HOST_REGISTRY Registry, LPCSTR Name);
void ScriptReleaseHostSymbol(LPSCRIPT_HOST_SYMBOL Symbol);
LPAST_NODE ScriptCreateASTNode(LPSCRIPT_CONTEXT Context, AST_NODE_TYPE Type);

/************************************************************************/

#endif
