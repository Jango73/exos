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


    Script Engine - Parser Expressions

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
 * @brief Parse one numeric token into native integer or floating-point storage.
 * @param Token Token to fill.
 */
static void ScriptFinalizeNumberToken(LPSCRIPT_TOKEN Token) {
    UINT Index = 0;
    BOOL HasDecimalPoint = FALSE;
    INT IntegerPart = 0;
    INT FractionPart = 0;
    INT FractionScale = 1;

    if (Token == NULL) {
        return;
    }

    while (Token->Value[Index] != STR_NULL) {
        STR Character = Token->Value[Index];

        if (Character == '.') {
            HasDecimalPoint = TRUE;
            Index++;
            continue;
        }

        if (!IsNumeric(Character)) {
            break;
        }

        if (!HasDecimalPoint) {
            IntegerPart = (IntegerPart * 10) + (INT)(Character - '0');
        } else {
            FractionPart = (FractionPart * 10) + (INT)(Character - '0');
            FractionScale *= 10;
        }

        Index++;
    }

    if (!HasDecimalPoint) {
        Token->IsInteger = TRUE;
        Token->IntegerValue = IntegerPart;
        Token->FloatValue = (F32)IntegerPart;
        return;
    }

    Token->IsInteger = FALSE;
    Token->IntegerValue = IntegerPart;
    Token->FloatValue = (F32)IntegerPart + ((F32)FractionPart / (F32)FractionScale);
}

/************************************************************************/
/**
 * @brief Initialize a script parser.
 * @param Parser Parser to initialize
 * @param Input Input string to parse
 * @param Variables Variable table
 * @param Callbacks Callback functions
 */
void ScriptInitParser(LPSCRIPT_PARSER Parser, LPCSTR Input, LPSCRIPT_CONTEXT Context) {
    Parser->Input = Input;
    Parser->Position = 0;
    Parser->Variables = &Context->Variables;
    Parser->Callbacks = &Context->Callbacks;
    Parser->CurrentScope = Context->CurrentScope;
    Parser->Context = Context;
    Parser->LoopDepth = 0;

    ScriptNextToken(Parser);
}

/************************************************************************/

/**
 * @brief Get the next token from input.
 * @param Parser Parser state
 */
void ScriptNextToken(LPSCRIPT_PARSER Parser) {
    LPCSTR Input = Parser->Input;
    U32* Pos = &Parser->Position;

    // Skip whitespace including newlines
    while (Input[*Pos] == ' ' || Input[*Pos] == '\t' || Input[*Pos] == '\n' || Input[*Pos] == '\r') (*Pos)++;

    Parser->CurrentToken.Position = *Pos;
    ScriptCalculateLineColumn(Input, *Pos, &Parser->CurrentToken.Line, &Parser->CurrentToken.Column);

    if (Input[*Pos] == STR_NULL) {
        Parser->CurrentToken.Type = TOKEN_EOF;
        return;
    }

    STR Ch = Input[*Pos];

    if (Ch >= '0' && Ch <= '9') {
        // Number
        Parser->CurrentToken.Type = TOKEN_NUMBER;
        U32 Start = *Pos;
        while ((Input[*Pos] >= '0' && Input[*Pos] <= '9') || Input[*Pos] == '.') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;
        ScriptFinalizeNumberToken(&Parser->CurrentToken);

    } else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || Ch == '_') {
        // Identifier
        Parser->CurrentToken.Type = TOKEN_IDENTIFIER;
        U32 Start = *Pos;
        while ((Input[*Pos] >= 'a' && Input[*Pos] <= 'z') ||
               (Input[*Pos] >= 'A' && Input[*Pos] <= 'Z') ||
               (Input[*Pos] >= '0' && Input[*Pos] <= '9') ||
               Input[*Pos] == '_') {
            (*Pos)++;
        }

        U32 Len = *Pos - Start;
        if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

        MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
        Parser->CurrentToken.Value[Len] = STR_NULL;

        // Check if this is a keyword
        if (ScriptIsKeyword(Parser->CurrentToken.Value)) {
            if (StringCompare(Parser->CurrentToken.Value, TEXT("if")) == 0) {
                Parser->CurrentToken.Type = TOKEN_IF;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("else")) == 0) {
                Parser->CurrentToken.Type = TOKEN_ELSE;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("for")) == 0) {
                Parser->CurrentToken.Type = TOKEN_FOR;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("return")) == 0) {
                Parser->CurrentToken.Type = TOKEN_RETURN;
            } else if (StringCompare(Parser->CurrentToken.Value, TEXT("continue")) == 0) {
                Parser->CurrentToken.Type = TOKEN_CONTINUE;
            }
        }

    } else if (Ch == '"') {
        ScriptParseStringToken(Parser, Input, Pos, '"');

    } else if (Ch == '\'') {
        ScriptParseStringToken(Parser, Input, Pos, '\'');

    } else if (Ch == '/') {
        BOOL TreatAsPath = TRUE;

        if (Input[*Pos + 1] == STR_NULL ||
            Input[*Pos + 1] == ' ' || Input[*Pos + 1] == '\t' ||
            Input[*Pos + 1] == '\n' || Input[*Pos + 1] == '\r') {
            TreatAsPath = FALSE;
        } else if (Input[*Pos + 1] == '/') {
            TreatAsPath = FALSE;
        }

        if (TreatAsPath) {
            BOOL HasValidStart = FALSE;

            if (*Pos == 0) {
                HasValidStart = TRUE;
            } else {
                I32 Prev = (I32)(*Pos) - 1;
                while (Prev >= 0) {
                    STR PrevCh = Input[Prev];
                    if (PrevCh == ' ' || PrevCh == '\t' || PrevCh == '\r') {
                        Prev--;
                        continue;
                    }

                    if (PrevCh == '\n' || PrevCh == ';' || PrevCh == '{' || PrevCh == '}') {
                        HasValidStart = TRUE;
                    } else {
                        HasValidStart = FALSE;
                    }
                    break;
                }

                if (Prev < 0) {
                    HasValidStart = TRUE;
                }
            }

            if (!HasValidStart) {
                TreatAsPath = FALSE;
            }
        }

        if (TreatAsPath) {
            Parser->CurrentToken.Type = TOKEN_PATH;
            U32 Start = *Pos;
            (*Pos)++;

            while (Input[*Pos] != STR_NULL) {
                STR Current = Input[*Pos];
                if (Current == ' ' || Current == '\t' || Current == '\n' ||
                    Current == '\r' || Current == ';') {
                    break;
                }
                (*Pos)++;
            }

            U32 Len = *Pos - Start;
            if (Len >= MAX_TOKEN_LENGTH) Len = MAX_TOKEN_LENGTH - 1;

            MemoryCopy(Parser->CurrentToken.Value, &Input[Start], Len);
            Parser->CurrentToken.Value[Len] = STR_NULL;
        } else {
            Parser->CurrentToken.Type = TOKEN_OPERATOR;
            Parser->CurrentToken.Value[0] = Ch;
            Parser->CurrentToken.Value[1] = STR_NULL;
            (*Pos)++;
        }

    } else if (Ch == '(' || Ch == ')') {
        Parser->CurrentToken.Type = (Ch == '(') ? TOKEN_LPAREN : TOKEN_RPAREN;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '[' || Ch == ']') {
        Parser->CurrentToken.Type = (Ch == '[') ? TOKEN_LBRACKET : TOKEN_RBRACKET;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == ';') {
        Parser->CurrentToken.Type = TOKEN_SEMICOLON;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == ',') {
        Parser->CurrentToken.Type = TOKEN_COMMA;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '{' || Ch == '}') {
        Parser->CurrentToken.Type = (Ch == '{') ? TOKEN_LBRACE : TOKEN_RBRACE;
        Parser->CurrentToken.Value[0] = Ch;
        Parser->CurrentToken.Value[1] = STR_NULL;
        (*Pos)++;

    } else if (Ch == '<' || Ch == '>' || Ch == '!') {
        Parser->CurrentToken.Value[0] = Ch;
        (*Pos)++;

        if ((Ch == '<' && Input[*Pos] == '=') ||
            (Ch == '>' && Input[*Pos] == '=') ||
            (Ch == '!' && Input[*Pos] == '=')) {
            Parser->CurrentToken.Type = TOKEN_COMPARISON;
            Parser->CurrentToken.Value[1] = Input[*Pos];
            Parser->CurrentToken.Value[2] = STR_NULL;
            (*Pos)++;
        } else if (Ch == '!') {
            Parser->CurrentToken.Type = TOKEN_OPERATOR;
            Parser->CurrentToken.Value[1] = STR_NULL;
        } else {
            Parser->CurrentToken.Type = TOKEN_COMPARISON;
            Parser->CurrentToken.Value[1] = STR_NULL;
        }

    } else if (Ch == '=') {
        // Handle = and == separately
        Parser->CurrentToken.Value[0] = Ch;
        (*Pos)++;

        if (Input[*Pos] == '=') {
            // == is comparison
            Parser->CurrentToken.Type = TOKEN_COMPARISON;
            Parser->CurrentToken.Value[1] = Input[*Pos];
            Parser->CurrentToken.Value[2] = STR_NULL;
            (*Pos)++;
        } else {
            // single = is operator (assignment)
            Parser->CurrentToken.Type = TOKEN_OPERATOR;
            Parser->CurrentToken.Value[1] = STR_NULL;
        }

    } else {
        Parser->CurrentToken.Type = TOKEN_OPERATOR;
        Parser->CurrentToken.Value[0] = Ch;
        (*Pos)++;

        if ((Ch == '&' && Input[*Pos] == '&') ||
            (Ch == '|' && Input[*Pos] == '|')) {
            Parser->CurrentToken.Value[1] = Input[*Pos];
            Parser->CurrentToken.Value[2] = STR_NULL;
            (*Pos)++;
        } else {
            Parser->CurrentToken.Value[1] = STR_NULL;
        }
    }
}

/************************************************************************/

/**
 * @brief Parse one function-call argument list and attach it to an AST expression node.
 * @param Parser Parser state.
 * @param FunctionNode Function-call expression node.
 * @param Error Pointer to error code.
 * @return TRUE on success, FALSE on syntax or allocation failure.
 */
static BOOL ScriptParseFunctionArguments(
    LPSCRIPT_PARSER Parser,
    LPAST_NODE FunctionNode,
    SCRIPT_ERROR* Error) {
    LPAST_NODE FirstArgument = NULL;
    LPAST_NODE LastArgument = NULL;

    if (Parser == NULL || FunctionNode == NULL || Error == NULL) {
        if (Error != NULL) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return FALSE;
    }

    FunctionNode->Data.Expression.FirstArgument = NULL;
    FunctionNode->Data.Expression.ArgumentCount = 0;

    if (Parser->CurrentToken.Type == TOKEN_RPAREN) {
        ScriptNextToken(Parser);
        *Error = SCRIPT_OK;
        return TRUE;
    }

    while (TRUE) {
        LPAST_NODE ArgumentNode = ScriptParseComparisonAST(Parser, Error);
        if (*Error != SCRIPT_OK || ArgumentNode == NULL) {
            ScriptDestroyAST(FirstArgument);
            return FALSE;
        }

        if (FirstArgument == NULL) {
            FirstArgument = ArgumentNode;
        } else {
            LastArgument->Data.Expression.NextArgument = ArgumentNode;
        }
        LastArgument = ArgumentNode;
        FunctionNode->Data.Expression.ArgumentCount++;

        if (Parser->CurrentToken.Type == TOKEN_COMMA) {
            ScriptNextToken(Parser);
            continue;
        }

        if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(FirstArgument);
            return FALSE;
        }

        ScriptNextToken(Parser);
        break;
    }

    FunctionNode->Data.Expression.FirstArgument = FirstArgument;
    *Error = SCRIPT_OK;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse a string literal token and handle escape sequences.
 * @param Parser Parser state
 * @param Input Original script input
 * @param Pos Current position pointer in the input
 * @param QuoteChar Quote character that delimits the string
 */
void ScriptParseStringToken(LPSCRIPT_PARSER Parser, LPCSTR Input, U32* Pos, STR QuoteChar) {
    Parser->CurrentToken.Type = TOKEN_STRING;
    (*Pos)++;

    U32 OutputIndex = 0;

    while (Input[*Pos] != STR_NULL) {
        STR Current = Input[*Pos];

        if (Current == QuoteChar) {
            (*Pos)++;
            break;
        }

        if (Current == '\\') {
            (*Pos)++;

            if (Input[*Pos] == STR_NULL) {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = '\\';
                }
                break;
            }

            STR Escaped = Input[*Pos];
            STR Resolved = STR_NULL;
            BOOL Recognized = TRUE;

            switch (Escaped) {
                case 'n':
                    Resolved = '\n';
                    break;
                case 'r':
                    Resolved = '\r';
                    break;
                case 't':
                    Resolved = '\t';
                    break;
                case '\\':
                    Resolved = '\\';
                    break;
                case '\'':
                    Resolved = '\'';
                    break;
                case '"':
                    Resolved = '"';
                    break;
                default:
                    Recognized = FALSE;
                    break;
            }

            if (Recognized) {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = Resolved;
                }
            } else {
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = '\\';
                }
                if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
                    Parser->CurrentToken.Value[OutputIndex++] = Escaped;
                }
            }

            (*Pos)++;
            continue;
        }

        if (OutputIndex < MAX_TOKEN_LENGTH - 1) {
            Parser->CurrentToken.Value[OutputIndex++] = Current;
        }
        (*Pos)++;
    }

    Parser->CurrentToken.Value[OutputIndex] = STR_NULL;
}

/************************************************************************/
/**
 * @brief Create one identifier expression node.
 * @param Parser Parser state.
 * @param Name Identifier text.
 * @return New identifier expression node, or NULL on allocation failure.
 */
static LPAST_NODE ScriptCreateIdentifierExpressionNode(
    LPSCRIPT_PARSER Parser,
    LPCSTR Name) {
    LPAST_NODE Node;

    if (Parser == NULL || Name == NULL) {
        return NULL;
    }

    Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
    if (Node == NULL) {
        return NULL;
    }

    Node->Data.Expression.TokenType = TOKEN_IDENTIFIER;
    Node->Data.Expression.IsVariable = TRUE;
    StringCopy(Node->Data.Expression.Value, Name);
    return Node;
}

/************************************************************************/
/**
 * @brief Create one property-access expression node.
 * @param Parser Parser state.
 * @param BaseExpression Base expression to read from.
 * @param PropertyName Property name to access.
 * @return New property-access expression node, or NULL on failure.
 */
static LPAST_NODE ScriptCreatePropertyAccessNode(
    LPSCRIPT_PARSER Parser,
    LPAST_NODE BaseExpression,
    LPCSTR PropertyName) {
    LPAST_NODE Node;

    if (Parser == NULL || BaseExpression == NULL || PropertyName == NULL) {
        ScriptDestroyAST(BaseExpression);
        return NULL;
    }

    Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
    if (Node == NULL) {
        ScriptDestroyAST(BaseExpression);
        return NULL;
    }

    Node->Data.Expression.TokenType = TOKEN_IDENTIFIER;
    Node->Data.Expression.IsVariable = FALSE;
    Node->Data.Expression.IsPropertyAccess = TRUE;
    Node->Data.Expression.BaseExpression = BaseExpression;
    StringCopy(Node->Data.Expression.PropertyName, PropertyName);
    return Node;
}

/************************************************************************/

/**
 * @brief Parse assignment statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseAssignmentAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE TargetBaseExpression = NULL;

    if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    LPAST_NODE Node = ScriptCreateASTNode(Parser->Context, AST_ASSIGNMENT);
    if (Node == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    StringCopy(Node->Data.Assignment.VarName, Parser->CurrentToken.Value);
    Node->Data.Assignment.IsArrayAccess = FALSE;
    Node->Data.Assignment.ArrayIndexExpr = NULL;
    Node->Data.Assignment.IsPropertyAccess = FALSE;
    Node->Data.Assignment.PropertyBaseExpression = NULL;

    ScriptNextToken(Parser);

    // Check for array access
    if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
        Node->Data.Assignment.IsArrayAccess = TRUE;
        ScriptNextToken(Parser);

        // Parse array index expression
        Node->Data.Assignment.ArrayIndexExpr = ScriptParseComparisonAST(Parser, Error);
        if (*Error != SCRIPT_OK || Node->Data.Assignment.ArrayIndexExpr == NULL) {
            ScriptDestroyAST(Node);
            return NULL;
        }

        if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(Node);
            return NULL;
        }
        ScriptNextToken(Parser);
    } else if (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
               Parser->CurrentToken.Value[0] == '.') {
        TargetBaseExpression = ScriptCreateIdentifierExpressionNode(
            Parser,
            Node->Data.Assignment.VarName);
        if (TargetBaseExpression == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Node);
            return NULL;
        }

        while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
               Parser->CurrentToken.Value[0] == '.') {
            STR PropertyName[MAX_TOKEN_LENGTH];

            ScriptNextToken(Parser);
            if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
                *Error = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(TargetBaseExpression);
                ScriptDestroyAST(Node);
                return NULL;
            }

            StringCopy(PropertyName, Parser->CurrentToken.Value);
            ScriptNextToken(Parser);

            if (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
                Parser->CurrentToken.Value[0] == '.') {
                TargetBaseExpression = ScriptCreatePropertyAccessNode(
                    Parser,
                    TargetBaseExpression,
                    PropertyName);
                if (TargetBaseExpression == NULL) {
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                    ScriptDestroyAST(Node);
                    return NULL;
                }
                continue;
            }

            Node->Data.Assignment.IsPropertyAccess = TRUE;
            Node->Data.Assignment.PropertyBaseExpression = TargetBaseExpression;
            StringCopy(Node->Data.Assignment.PropertyName, PropertyName);
            TargetBaseExpression = NULL;
            break;
        }
    }

    if (Parser->CurrentToken.Type != TOKEN_OPERATOR || Parser->CurrentToken.Value[0] != '=') {
        *Error = SCRIPT_ERROR_SYNTAX;
        if (TargetBaseExpression != NULL) {
            ScriptDestroyAST(TargetBaseExpression);
        }
        ScriptDestroyAST(Node);
        return NULL;
    }

    ScriptNextToken(Parser);

    // Parse expression
    Node->Data.Assignment.Expression = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || Node->Data.Assignment.Expression == NULL) {
        ScriptDestroyAST(Node);
        return NULL;
    }

    return Node;
}

/************************************************************************/

/**
 * @brief Parse comparison operators and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
static LPAST_NODE ScriptParseRelationalAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseExpressionAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_COMPARISON) {
        // Create comparison node
        LPAST_NODE CompNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (CompNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        CompNode->Data.Expression.TokenType = TOKEN_COMPARISON;
        StringCopy(CompNode->Data.Expression.Value, Parser->CurrentToken.Value);
        CompNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseExpressionAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(CompNode);
            return NULL;
        }

        CompNode->Data.Expression.Right = Right;
        Left = CompNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse logical AND operators and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseLogicalAndAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseRelationalAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           StringCompare(Parser->CurrentToken.Value, TEXT("&&")) == 0) {
        LPAST_NODE OperatorNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (OperatorNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OperatorNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        StringCopy(OperatorNode->Data.Expression.Value, Parser->CurrentToken.Value);
        OperatorNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseRelationalAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OperatorNode);
            return NULL;
        }

        OperatorNode->Data.Expression.Right = Right;
        Left = OperatorNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse logical OR operators and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseLogicalOrAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseLogicalAndAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           StringCompare(Parser->CurrentToken.Value, TEXT("||")) == 0) {
        LPAST_NODE OperatorNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (OperatorNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OperatorNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        StringCopy(OperatorNode->Data.Expression.Value, Parser->CurrentToken.Value);
        OperatorNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseLogicalAndAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OperatorNode);
            return NULL;
        }

        OperatorNode->Data.Expression.Right = Right;
        Left = OperatorNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse logical and comparison operators and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseComparisonAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    return ScriptParseLogicalOrAST(Parser, Error);
}

/************************************************************************/

/**
 * @brief Parse expression (addition/subtraction) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseExpressionAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseTermAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '+' || Parser->CurrentToken.Value[0] == '-')) {

        LPAST_NODE OpNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (OpNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OpNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        OpNode->Data.Expression.Value[0] = Parser->CurrentToken.Value[0];
        OpNode->Data.Expression.Value[1] = STR_NULL;
        OpNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseTermAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OpNode);
            return NULL;
        }

        OpNode->Data.Expression.Right = Right;
        Left = OpNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Parse term (multiplication/division) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseTermAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE Left = ScriptParseFactorAST(Parser, Error);
    if (*Error != SCRIPT_OK || Left == NULL) return NULL;

    while (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
           (Parser->CurrentToken.Value[0] == '*' || Parser->CurrentToken.Value[0] == '/')) {

        LPAST_NODE OpNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (OpNode == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            ScriptDestroyAST(Left);
            return NULL;
        }

        OpNode->Data.Expression.TokenType = TOKEN_OPERATOR;
        OpNode->Data.Expression.Value[0] = Parser->CurrentToken.Value[0];
        OpNode->Data.Expression.Value[1] = STR_NULL;
        OpNode->Data.Expression.Left = Left;
        ScriptNextToken(Parser);

        LPAST_NODE Right = ScriptParseFactorAST(Parser, Error);
        if (*Error != SCRIPT_OK || Right == NULL) {
            ScriptDestroyAST(OpNode);
            return NULL;
        }

        OpNode->Data.Expression.Right = Right;
        Left = OpNode;
    }

    return Left;
}

/************************************************************************/

/**
 * @brief Build a unary sign AST node using a zero literal as the left operand.
 * @param Parser Parser state.
 * @param Operator Unary operator character.
 * @param Operand Unary operand expression.
 * @param Error Pointer to error code.
 * @return AST expression node or NULL on failure.
 */
static LPAST_NODE ScriptCreateUnaryOperatorNode(
    LPSCRIPT_PARSER Parser,
    STR Operator,
    LPAST_NODE Operand,
    SCRIPT_ERROR* Error) {
    LPAST_NODE ZeroNode;
    LPAST_NODE OperatorNode;

    if (Parser == NULL || Operand == NULL || Error == NULL) {
        if (Error != NULL) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        ScriptDestroyAST(Operand);
        return NULL;
    }

    ZeroNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
    if (ZeroNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        ScriptDestroyAST(Operand);
        return NULL;
    }

    ZeroNode->Data.Expression.TokenType = TOKEN_NUMBER;
    ZeroNode->Data.Expression.IsIntegerLiteral = TRUE;
    ZeroNode->Data.Expression.IntegerValue = 0;
    ZeroNode->Data.Expression.FloatValue = 0.0f;
    StringCopy(ZeroNode->Data.Expression.Value, TEXT("0"));

    OperatorNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
    if (OperatorNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        ScriptDestroyAST(ZeroNode);
        ScriptDestroyAST(Operand);
        return NULL;
    }

    OperatorNode->Data.Expression.TokenType = TOKEN_OPERATOR;
    OperatorNode->Data.Expression.Value[0] = Operator;
    OperatorNode->Data.Expression.Value[1] = STR_NULL;
    OperatorNode->Data.Expression.Left = ZeroNode;
    OperatorNode->Data.Expression.Right = Operand;

    return OperatorNode;
}

/************************************************************************/

/**
 * @brief Parse factor (numbers, variables, parentheses) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST expression node or NULL on failure
 */
LPAST_NODE ScriptParseFactorAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    // UNARY +/-. and !.
    if (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
        (Parser->CurrentToken.Value[0] == '+' ||
         Parser->CurrentToken.Value[0] == '-' ||
         Parser->CurrentToken.Value[0] == '!')) {
        STR UnaryOperator = Parser->CurrentToken.Value[0];

        ScriptNextToken(Parser);

        LPAST_NODE Operand = ScriptParseFactorAST(Parser, Error);
        if (*Error != SCRIPT_OK || Operand == NULL) {
            return NULL;
        }

        if (UnaryOperator == '!') {
            LPAST_NODE OperatorNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
            if (OperatorNode == NULL) {
                *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                ScriptDestroyAST(Operand);
                return NULL;
            }

            OperatorNode->Data.Expression.TokenType = TOKEN_OPERATOR;
            OperatorNode->Data.Expression.Value[0] = UnaryOperator;
            OperatorNode->Data.Expression.Value[1] = STR_NULL;
            OperatorNode->Data.Expression.Left = NULL;
            OperatorNode->Data.Expression.Right = Operand;
            return OperatorNode;
        }

        return ScriptCreateUnaryOperatorNode(Parser, UnaryOperator, Operand, Error);
    }

    // NUMBER
    if (Parser->CurrentToken.Type == TOKEN_NUMBER) {
        LPAST_NODE Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_NUMBER;
        Node->Data.Expression.IsIntegerLiteral = Parser->CurrentToken.IsInteger;
        Node->Data.Expression.IntegerValue = Parser->CurrentToken.IntegerValue;
        Node->Data.Expression.FloatValue = Parser->CurrentToken.FloatValue;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        ScriptNextToken(Parser);
        return Node;
    }

    // IDENTIFIER (variable, function call, or array access)
    if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        LPAST_NODE Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_IDENTIFIER;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        Node->Data.Expression.IsVariable = TRUE;
        Node->Data.Expression.IsArrayAccess = FALSE;
        Node->Data.Expression.IsFunctionCall = FALSE;

        ScriptNextToken(Parser);

        // Check for function call
        if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
            Node->Data.Expression.IsFunctionCall = TRUE;
            ScriptNextToken(Parser);

            if (!ScriptParseFunctionArguments(Parser, Node, Error)) {
                ScriptDestroyAST(Node);
                return NULL;
            }
        }
        LPAST_NODE CurrentNode = Node;
        BOOL ContinueParsing = TRUE;

        while (ContinueParsing) {
            ContinueParsing = FALSE;

            if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
                ScriptNextToken(Parser);

                LPAST_NODE IndexExpr = ScriptParseComparisonAST(Parser, Error);
                if (*Error != SCRIPT_OK || IndexExpr == NULL) {
                    ScriptDestroyAST(CurrentNode);
                    return NULL;
                }

                if (Parser->CurrentToken.Type != TOKEN_RBRACKET) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(CurrentNode);
                    ScriptDestroyAST(IndexExpr);
                    return NULL;
                }
                ScriptNextToken(Parser);

                LPAST_NODE ArrayNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
                if (ArrayNode == NULL) {
                    ScriptDestroyAST(IndexExpr);
                    ScriptDestroyAST(CurrentNode);
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                    return NULL;
                }

                ArrayNode->Data.Expression.TokenType = TOKEN_IDENTIFIER;
                ArrayNode->Data.Expression.IsVariable = FALSE;
                ArrayNode->Data.Expression.IsArrayAccess = TRUE;
                ArrayNode->Data.Expression.BaseExpression = CurrentNode;
                ArrayNode->Data.Expression.ArrayIndexExpr = IndexExpr;
                CurrentNode = ArrayNode;

                ContinueParsing = TRUE;
            } else if (Parser->CurrentToken.Type == TOKEN_OPERATOR && Parser->CurrentToken.Value[0] == '.') {
                ScriptNextToken(Parser);

                if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
                    *Error = SCRIPT_ERROR_SYNTAX;
                    ScriptDestroyAST(CurrentNode);
                    return NULL;
                }

                LPAST_NODE PropertyNode = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
                if (PropertyNode == NULL) {
                    ScriptDestroyAST(CurrentNode);
                    *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                    return NULL;
                }

                PropertyNode->Data.Expression.TokenType = TOKEN_IDENTIFIER;
                PropertyNode->Data.Expression.IsVariable = FALSE;
                PropertyNode->Data.Expression.IsPropertyAccess = TRUE;
                PropertyNode->Data.Expression.BaseExpression = CurrentNode;
                StringCopy(PropertyNode->Data.Expression.PropertyName, Parser->CurrentToken.Value);

                ScriptNextToken(Parser);

                CurrentNode = PropertyNode;
                ContinueParsing = TRUE;
            }
        }

        return CurrentNode;
    }

    // STRING
    if (Parser->CurrentToken.Type == TOKEN_STRING) {
        LPAST_NODE Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_STRING;
        StringCopy(Node->Data.Expression.Value, Parser->CurrentToken.Value);
        ScriptNextToken(Parser);
        return Node;
    }

    // EMPTY OBJECT LITERAL
    if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
        LPAST_NODE Node;

        ScriptNextToken(Parser);
        if (Parser->CurrentToken.Type != TOKEN_RBRACE) {
            *Error = SCRIPT_ERROR_SYNTAX;
            return NULL;
        }

        Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
        if (Node == NULL) {
            *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        Node->Data.Expression.TokenType = TOKEN_LBRACE;
        StringCopy(Node->Data.Expression.Value, TEXT("{}"));
        ScriptNextToken(Parser);
        return Node;
    }

    // PARENTHESES
    if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
        ScriptNextToken(Parser);
        LPAST_NODE Expr = ScriptParseComparisonAST(Parser, Error);
        if (*Error != SCRIPT_OK || Expr == NULL) return NULL;

        if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
            *Error = SCRIPT_ERROR_SYNTAX;
            ScriptDestroyAST(Expr);
            return NULL;
        }

        ScriptNextToken(Parser);
        return Expr;
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return NULL;
}
