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


    Script Engine - Parser Statements

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
 * @brief Parse a return statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseReturnStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE ReturnNode;

    if (Parser == NULL || Error == NULL || Parser->CurrentToken.Type != TOKEN_RETURN) {
        if (Error != NULL) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return NULL;
    }

    ScriptNextToken(Parser);

    ReturnNode = ScriptCreateASTNode(Parser->Context, AST_RETURN);
    if (ReturnNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    ReturnNode->Data.Return.Expression = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || ReturnNode->Data.Return.Expression == NULL) {
        ScriptDestroyAST(ReturnNode);
        return NULL;
    }

    return ReturnNode;
}

/************************************************************************/

/**
 * @brief Parse a continue statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseContinueStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    LPAST_NODE ContinueNode;

    if (Parser == NULL || Error == NULL || Parser->CurrentToken.Type != TOKEN_CONTINUE) {
        if (Error != NULL) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return NULL;
    }

    if (Parser->LoopDepth == 0) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    ScriptNextToken(Parser);

    ContinueNode = ScriptCreateASTNode(Parser->Context, AST_CONTINUE);
    if (ContinueNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    return ContinueNode;
}

/************************************************************************/

/**
 * @brief Parse a statement (assignment, if, for, or block) and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type == TOKEN_IF) {
        return ScriptParseIfStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_FOR) {
        return ScriptParseForStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_RETURN) {
        return ScriptParseReturnStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_CONTINUE) {
        return ScriptParseContinueStatementAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_LBRACE) {
        return ScriptParseBlockAST(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_PATH) {
        return ScriptParseShellCommandExpression(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_STRING) {
        return ScriptParseShellCommandExpression(Parser, Error);
    } else if (Parser->CurrentToken.Type == TOKEN_IDENTIFIER) {
        // Could be assignment, expression statement (function call) or shell command
        U32 SavedPosition = Parser->Position;
        SCRIPT_TOKEN SavedToken = Parser->CurrentToken;
        BOOL IsAssignment = FALSE;

        ScriptNextToken(Parser);

        if (Parser->CurrentToken.Type == TOKEN_LPAREN) {
            Parser->Position = SavedPosition;
            Parser->CurrentToken = SavedToken;
            return ScriptParseComparisonAST(Parser, Error);
        }

        while (TRUE) {
            if (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
                Parser->CurrentToken.Value[0] == '=') {
                IsAssignment = TRUE;
                break;
            }

            if (Parser->CurrentToken.Type == TOKEN_LBRACKET) {
                ScriptNextToken(Parser);
                while (Parser->CurrentToken.Type != TOKEN_RBRACKET &&
                       Parser->CurrentToken.Type != TOKEN_EOF) {
                    ScriptNextToken(Parser);
                }

                if (Parser->CurrentToken.Type == TOKEN_RBRACKET) {
                    ScriptNextToken(Parser);
                    continue;
                }
                break;
            }

            if (Parser->CurrentToken.Type == TOKEN_OPERATOR &&
                Parser->CurrentToken.Value[0] == '.') {
                ScriptNextToken(Parser);
                if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
                    break;
                }
                ScriptNextToken(Parser);
                continue;
            }

            break;
        }

        if (IsAssignment) {
            Parser->Position = SavedPosition;
            Parser->CurrentToken = SavedToken;
            return ScriptParseAssignmentAST(Parser, Error);
        }

        Parser->Position = SavedPosition;
        Parser->CurrentToken = SavedToken;

        if (ScriptShouldParseShellCommand(Parser)) {
            return ScriptParseShellCommandExpression(Parser, Error);
        }

        return ScriptParseComparisonAST(Parser, Error);
    }

    *Error = SCRIPT_ERROR_SYNTAX;
    return NULL;
}

/************************************************************************/

/**
 * @brief Determine if the current token sequence should be parsed as a shell command.
 * @param Parser Parser state
 * @return TRUE if the statement should be handled as a shell command
 */
BOOL ScriptShouldParseShellCommand(LPSCRIPT_PARSER Parser) {
    if (Parser == NULL) return FALSE;

    if (Parser->CurrentToken.Type == TOKEN_STRING) {
        return TRUE;
    }

    if (Parser->CurrentToken.Type == TOKEN_PATH) {
        return TRUE;
    }

    if (Parser->CurrentToken.Type != TOKEN_IDENTIFIER) {
        return FALSE;
    }

    LPCSTR Input = Parser->Input;
    U32 Pos = Parser->Position;

    while (Input[Pos] == ' ' || Input[Pos] == '\t') {
        Pos++;
    }

    if (Input[Pos] == '(') {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse a shell command expression and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseShellCommandExpression(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser == NULL || Error == NULL) {
        return NULL;
    }

    LPCSTR Input = Parser->Input;
    U32 Start = Parser->CurrentToken.Position;
    U32 Scan = Start;
    BOOL InQuotes = FALSE;
    STR QuoteChar = STR_NULL;
    TOKEN_TYPE InitialTokenType = Parser->CurrentToken.Type;

    while (Input[Scan] != STR_NULL) {
        STR Ch = Input[Scan];

        if (!InQuotes && (Ch == ';' || Ch == '\n' || Ch == '\r')) {
            break;
        }

        if (Ch == '"' || Ch == '\'') {
            if (InQuotes && Ch == QuoteChar) {
                InQuotes = FALSE;
                QuoteChar = STR_NULL;
            } else if (!InQuotes) {
                InQuotes = TRUE;
                QuoteChar = Ch;
            }
        }

        Scan++;
    }

    U32 End = Scan;

    while (End > Start && (Input[End - 1] == ' ' || Input[End - 1] == '\t')) {
        End--;
    }

    if (End <= Start) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    LPAST_NODE Node = ScriptCreateASTNode(Parser->Context, AST_EXPRESSION);
    if (Node == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    U32 Length = End - Start;
    Node->Data.Expression.CommandLine = (LPSTR)ScriptAlloc(Parser->Context, Length + 1);
    if (Node->Data.Expression.CommandLine == NULL) {
        ScriptDestroyAST(Node);
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    MemoryCopy(Node->Data.Expression.CommandLine, &Input[Start], Length);
    Node->Data.Expression.CommandLine[Length] = STR_NULL;

    Node->Data.Expression.TokenType = (InitialTokenType == TOKEN_PATH) ? TOKEN_PATH : TOKEN_IDENTIFIER;
    Node->Data.Expression.IsVariable = FALSE;
    Node->Data.Expression.IsFunctionCall = TRUE;
    Node->Data.Expression.IsShellCommand = TRUE;

    // Extract command name for Value field (trim whitespace and quotes)
    U32 CmdIndex = 0;
    while (Node->Data.Expression.CommandLine[CmdIndex] == ' ' ||
           Node->Data.Expression.CommandLine[CmdIndex] == '\t') {
        CmdIndex++;
    }

    STR Quote = Node->Data.Expression.CommandLine[CmdIndex];
    BOOL Quoted = (Quote == '"' || Quote == '\'');
    if (Quoted) {
        CmdIndex++;
    }

    U32 NameStart = CmdIndex;
    while (Node->Data.Expression.CommandLine[CmdIndex] != STR_NULL) {
        STR Current = Node->Data.Expression.CommandLine[CmdIndex];
        if (Quoted) {
            if (Current == Quote) {
                break;
            }
        } else if (Current == ' ' || Current == '\t') {
            break;
        }
        CmdIndex++;
    }

    U32 NameLength = CmdIndex - NameStart;
    if (NameLength == 0) {
        ScriptDestroyAST(Node);
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }

    if (NameLength >= MAX_TOKEN_LENGTH) {
        NameLength = MAX_TOKEN_LENGTH - 1;
    }

    MemoryCopy(Node->Data.Expression.Value, &Node->Data.Expression.CommandLine[NameStart], NameLength);
    Node->Data.Expression.Value[NameLength] = STR_NULL;

    Parser->Position = Scan;
    ScriptNextToken(Parser);

    *Error = SCRIPT_OK;
    return Node;
}

/************************************************************************/

/**
 * @brief Parse a command block { ... } and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseBlockAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_LBRACE) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    LPAST_NODE BlockNode = ScriptCreateASTNode(Parser->Context, AST_BLOCK);
    if (BlockNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    BlockNode->Data.Block.Capacity = 16;
    BlockNode->Data.Block.Count = 0;
    BlockNode->Data.Block.Statements = (LPAST_NODE*)ScriptAlloc(
        Parser->Context,
        BlockNode->Data.Block.Capacity * sizeof(LPAST_NODE));
    if (BlockNode->Data.Block.Statements == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        ScriptDestroyAST(BlockNode);
        return NULL;
    }

    // Parse statements until we hit the closing brace
    while (Parser->CurrentToken.Type != TOKEN_RBRACE && Parser->CurrentToken.Type != TOKEN_EOF) {
        LPAST_NODE Statement = ScriptParseStatementAST(Parser, Error);
        if (*Error != SCRIPT_OK || Statement == NULL) {
            ScriptDestroyAST(BlockNode);
            return NULL;
        }

        // Add statement to block
        if (BlockNode->Data.Block.Count >= BlockNode->Data.Block.Capacity) {
            BlockNode->Data.Block.Capacity *= 2;
            LPAST_NODE* NewStatements = (LPAST_NODE*)ScriptAlloc(
                Parser->Context,
                BlockNode->Data.Block.Capacity * sizeof(LPAST_NODE));
            if (NewStatements == NULL) {
                *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
                ScriptDestroyAST(Statement);
                ScriptDestroyAST(BlockNode);
                return NULL;
            }
            for (U32 i = 0; i < BlockNode->Data.Block.Count; i++) {
                NewStatements[i] = BlockNode->Data.Block.Statements[i];
            }
            ScriptFree(Parser->Context, BlockNode->Data.Block.Statements);
            BlockNode->Data.Block.Statements = NewStatements;
        }

        BlockNode->Data.Block.Statements[BlockNode->Data.Block.Count++] = Statement;

        // Semicolon is mandatory after assignments and returns.
        if (Statement->Type == AST_ASSIGNMENT || Statement->Type == AST_RETURN || Statement->Type == AST_CONTINUE) {
            if (Parser->CurrentToken.Type != TOKEN_SEMICOLON && Parser->CurrentToken.Type != TOKEN_RBRACE) {
                *Error = SCRIPT_ERROR_SYNTAX;
                ScriptDestroyAST(BlockNode);
                return NULL;
            }
            if (Parser->CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(Parser);
            }
        } else {
            // For blocks, if, for: semicolon is optional
            if (Parser->CurrentToken.Type == TOKEN_SEMICOLON) {
                ScriptNextToken(Parser);
            }
        }
    }

    if (Parser->CurrentToken.Type != TOKEN_RBRACE) {
        *Error = SCRIPT_ERROR_UNMATCHED_BRACE;
        ScriptDestroyAST(BlockNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    return BlockNode;
}

/************************************************************************/

/**
 * @brief Parse an if statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseIfStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_IF) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Create IF node
    LPAST_NODE IfNode = ScriptCreateASTNode(Parser->Context, AST_IF);
    if (IfNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    // Parse condition
    IfNode->Data.If.Condition = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || IfNode->Data.If.Condition == NULL) {
        ScriptDestroyAST(IfNode);
        return NULL;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(IfNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse then branch
    IfNode->Data.If.Then = ScriptParseStatementAST(Parser, Error);
    if (*Error != SCRIPT_OK || IfNode->Data.If.Then == NULL) {
        ScriptDestroyAST(IfNode);
        return NULL;
    }

    // Parse else branch if present
    IfNode->Data.If.Else = NULL;
    if (Parser->CurrentToken.Type == TOKEN_ELSE) {
        ScriptNextToken(Parser);
        IfNode->Data.If.Else = ScriptParseStatementAST(Parser, Error);
        if (*Error != SCRIPT_OK || IfNode->Data.If.Else == NULL) {
            ScriptDestroyAST(IfNode);
            return NULL;
        }
    }

    return IfNode;
}

/************************************************************************/

/**
 * @brief Parse a for statement and build AST node.
 * @param Parser Parser state
 * @param Error Pointer to error code
 * @return AST node or NULL on failure
 */
LPAST_NODE ScriptParseForStatementAST(LPSCRIPT_PARSER Parser, SCRIPT_ERROR* Error) {
    if (Parser->CurrentToken.Type != TOKEN_FOR) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Expect opening parenthesis
    if (Parser->CurrentToken.Type != TOKEN_LPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        return NULL;
    }
    ScriptNextToken(Parser);

    // Create FOR node
    LPAST_NODE ForNode = ScriptCreateASTNode(Parser->Context, AST_FOR);
    if (ForNode == NULL) {
        *Error = SCRIPT_ERROR_OUT_OF_MEMORY;
        return NULL;
    }

    // Parse initialization (assignment)
    ForNode->Data.For.Init = ScriptParseAssignmentAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Init == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse condition
    ForNode->Data.For.Condition = ScriptParseComparisonAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Condition == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect semicolon
    if (Parser->CurrentToken.Type != TOKEN_SEMICOLON) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse increment
    ForNode->Data.For.Increment = ScriptParseAssignmentAST(Parser, Error);
    if (*Error != SCRIPT_OK || ForNode->Data.For.Increment == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    // Expect closing parenthesis
    if (Parser->CurrentToken.Type != TOKEN_RPAREN) {
        *Error = SCRIPT_ERROR_SYNTAX;
        ScriptDestroyAST(ForNode);
        return NULL;
    }
    ScriptNextToken(Parser);

    // Parse body
    Parser->LoopDepth++;
    ForNode->Data.For.Body = ScriptParseStatementAST(Parser, Error);
    Parser->LoopDepth--;
    if (*Error != SCRIPT_OK || ForNode->Data.For.Body == NULL) {
        ScriptDestroyAST(ForNode);
        return NULL;
    }

    return ForNode;
}
