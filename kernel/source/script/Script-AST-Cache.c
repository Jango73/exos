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


    Script Engine - AST Cache

\************************************************************************/

#include "Base.h"
#include "log/Log.h"
#include "memory/Heap.h"
#include "script/Script-Internal.h"
#include "script/Script.h"
#include "text/CoreString.h"
#include "utils/List.h"

/************************************************************************/

/**
 * @brief Compute the parse kind of a freshly parsed statement unit.
 * @param Node Statement node whose kind is requested.
 * @return Matching parse kind used for on-demand re-parsing.
 */
static SCRIPT_AST_PARSE_KIND ScriptASTParseKindFromNode(LPAST_NODE Node) {
    if (Node == NULL) {
        return AST_PARSE_EXPRESSION;
    }

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            return AST_PARSE_ASSIGNMENT;

        case AST_IF:
            return AST_PARSE_IF;

        case AST_FOR:
            return AST_PARSE_FOR;

        case AST_BLOCK:
            return AST_PARSE_BLOCK;

        case AST_RETURN:
            return AST_PARSE_RETURN;

        case AST_CONTINUE:
            return AST_PARSE_CONTINUE;

        case AST_EXPRESSION:
            return Node->Data.Expression.IsShellCommand ? AST_PARSE_SHELL_COMMAND : AST_PARSE_EXPRESSION;

        default:
            return AST_PARSE_EXPRESSION;
    }
}

/************************************************************************/

/**
 * @brief Measure the resident payload bytes of a node subtree.
 *
 * Mirrors ScriptDestroyAST semantics: cache entry children (then, else,
 * for body, block members) are owned by the cache and are not counted here.
 * @param Node Node whose payload bytes are requested.
 * @return Number of resident payload bytes.
 */
static U32 ScriptASTMeasureNode(LPAST_NODE Node) {
    U32 Bytes;

    if (Node == NULL) {
        return 0;
    }

    Bytes = sizeof(AST_NODE);

    switch (Node->Type) {
        case AST_ASSIGNMENT:
            if (Node->Data.Assignment.Expression) {
                Bytes += ScriptASTMeasureNode(Node->Data.Assignment.Expression);
            }
            if (Node->Data.Assignment.ArrayIndexExpr) {
                Bytes += ScriptASTMeasureNode(Node->Data.Assignment.ArrayIndexExpr);
            }
            if (Node->Data.Assignment.PropertyBaseExpression) {
                Bytes += ScriptASTMeasureNode(Node->Data.Assignment.PropertyBaseExpression);
            }
            break;

        case AST_IF:
            if (Node->Data.If.Condition) {
                Bytes += ScriptASTMeasureNode(Node->Data.If.Condition);
            }
            break;

        case AST_FOR:
            if (Node->Data.For.Init) {
                Bytes += ScriptASTMeasureNode(Node->Data.For.Init);
            }
            if (Node->Data.For.Condition) {
                Bytes += ScriptASTMeasureNode(Node->Data.For.Condition);
            }
            if (Node->Data.For.Increment) {
                Bytes += ScriptASTMeasureNode(Node->Data.For.Increment);
            }
            break;

        case AST_BLOCK:
            Bytes += Node->Data.Block.Capacity * sizeof(LPAST_ENTRY);
            break;

        case AST_RETURN:
            if (Node->Data.Return.Expression) {
                Bytes += ScriptASTMeasureNode(Node->Data.Return.Expression);
            }
            break;

        case AST_CONTINUE:
            break;

        case AST_EXPRESSION:
            if (Node->Data.Expression.BaseExpression) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.BaseExpression);
            }
            if (Node->Data.Expression.ArrayIndexExpr) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.ArrayIndexExpr);
            }
            if (Node->Data.Expression.FirstArgument) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.FirstArgument);
            }
            if (Node->Data.Expression.NextArgument) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.NextArgument);
            }
            if (Node->Data.Expression.Left) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.Left);
            }
            if (Node->Data.Expression.Right) {
                Bytes += ScriptASTMeasureNode(Node->Data.Expression.Right);
            }
            if (Node->Data.Expression.IsShellCommand && Node->Data.Expression.CommandLine != NULL) {
                Bytes += StringLength(Node->Data.Expression.CommandLine) + 1;
            }
            break;

        default:
            break;
    }

    return Bytes;
}

/************************************************************************/

/**
 * @brief Move an entry to the tail of the cache LRU list (most recently used).
 * @param Entry Entry to touch.
 */
void ScriptASTCacheTouch(LPAST_ENTRY Entry) {
    LPSCRIPT_AST_CACHE Cache;

    if (Entry == NULL || Entry->Cache == NULL) {
        return;
    }

    Cache = Entry->Cache;

    if (Cache->List->Last == (LPLISTNODE)Entry) {
        return;
    }

    ListRemove(Cache->List, Entry);
    ListAddTail(Cache->List, Entry);
}

/************************************************************************/

/**
 * @brief Find a cache entry by source span, parse kind and loop depth.
 * @param Cache Cache to search.
 * @param SourceOffset Statement start offset in the script source.
 * @param SourceLength Statement length in the script source.
 * @param Kind Parse kind of the statement.
 * @param LoopDepth Loop nesting depth at which the statement was parsed.
 * @return Matching entry or NULL when none exists.
 */
static LPAST_ENTRY ScriptASTCacheFindEntry(
    LPSCRIPT_AST_CACHE Cache, U32 SourceOffset, U32 SourceLength, SCRIPT_AST_PARSE_KIND Kind, U32 LoopDepth) {
    for (LPLISTNODE Node = Cache->List->First; Node != NULL; Node = Node->Next) {
        LPAST_ENTRY Entry = (LPAST_ENTRY)Node;
        if (Entry->SourceOffset == SourceOffset && Entry->SourceLength == SourceLength && Entry->ParseKind == Kind &&
            Entry->LoopDepth == LoopDepth) {
            return Entry;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Re-parse a statement unit from its source span using its parse kind.
 * @param Parser Parser positioned at the statement start.
 * @param Entry Entry describing the statement to re-parse.
 * @param Error Pointer to receive the parse error code.
 * @return Parsed statement node or NULL on failure.
 */
static LPAST_NODE ScriptASTCacheParseEntry(LPSCRIPT_PARSER Parser, LPAST_ENTRY Entry, SCRIPT_ERROR* Error) {
    switch (Entry->ParseKind) {
        case AST_PARSE_ASSIGNMENT:
            return ScriptParseAssignmentAST(Parser, Error);

        case AST_PARSE_IF:
            return ScriptParseIfStatementAST(Parser, Error);

        case AST_PARSE_FOR:
            return ScriptParseForStatementAST(Parser, Error);

        case AST_PARSE_BLOCK:
            return ScriptParseBlockAST(Parser, Error);

        case AST_PARSE_RETURN:
            return ScriptParseReturnStatementAST(Parser, Error);

        case AST_PARSE_CONTINUE:
            return ScriptParseContinueStatementAST(Parser, Error);

        case AST_PARSE_EXPRESSION:
            return ScriptParseComparisonAST(Parser, Error);

        case AST_PARSE_SHELL_COMMAND:
            return ScriptParseShellCommandExpression(Parser, Error);

        default:
            *Error = SCRIPT_ERROR_SYNTAX;
            return NULL;
    }
}

/************************************************************************/

/**
 * @brief Create an AST cache for a script execution.
 * @param Context Script context that owns the cache.
 * @param Source Script source text, valid for the whole execution.
 * @param SourceLength Length of the script source text.
 * @param BudgetBytes Resident payload budget before eviction kicks in.
 * @return New cache or NULL on allocation failure.
 */
LPSCRIPT_AST_CACHE ScriptASTCacheCreate(LPSCRIPT_CONTEXT Context, LPCSTR Source, U32 SourceLength, U32 BudgetBytes) {
    LPSCRIPT_AST_CACHE Cache;

    if (Context == NULL || Source == NULL) {
        return NULL;
    }

    Cache = (LPSCRIPT_AST_CACHE)ScriptAlloc(Context, sizeof(SCRIPT_AST_CACHE));
    if (Cache == NULL) {
        DEBUG(TEXT("[ScriptASTCacheCreate] Failed to allocate cache"));
        return NULL;
    }

    MemorySet(Cache, 0, sizeof(SCRIPT_AST_CACHE));
    Cache->Context = Context;
    Cache->Source = Source;
    Cache->SourceLength = SourceLength;
    Cache->BudgetBytes = BudgetBytes;

    Cache->List = NewListEx(NULL, &Context->Allocator, AllocatorListAlloc, AllocatorListFree);
    if (Cache->List == NULL) {
        DEBUG(TEXT("[ScriptASTCacheCreate] Failed to create cache list"));
        ScriptFree(Context, Cache);
        return NULL;
    }

    return Cache;
}

/************************************************************************/

/**
 * @brief Destroy an AST cache, freeing all entries and their resident nodes.
 * @param Cache Cache to destroy.
 */
void ScriptASTCacheDestroy(LPSCRIPT_AST_CACHE Cache) {
    LPLISTNODE Node;
    LPLISTNODE Next;

    if (Cache == NULL) {
        return;
    }

    if (Cache->List != NULL) {
        Node = Cache->List->First;
        while (Node != NULL) {
            LPAST_ENTRY Entry = (LPAST_ENTRY)Node;
            Next = Node->Next;
            if (Entry->Node != NULL) {
                ScriptDestroyAST(Entry->Node);
                Entry->Node = NULL;
            }
            ScriptFree(Cache->Context, Entry);
            Node = Next;
        }

        DeleteList(Cache->List);
        Cache->List = NULL;
    }

    Cache->ResidentBytes = 0;

    ScriptFree(Cache->Context, Cache);
}

/************************************************************************/

/**
 * @brief Register a freshly parsed statement unit and attach it to a cache entry.
 *
 * Reuses an existing entry with the same source span, parse kind and loop depth
 * so that re-parses never accumulate orphan entries. When an existing entry
 * already holds a resident node, the freshly parsed duplicate is discarded.
 * @param Context Script context whose active cache receives the statement.
 * @param Node Statement node just parsed.
 * @param SourceOffset Statement start offset in the script source.
 * @param SourceLength Statement length in the script source.
 * @param LoopDepth Loop nesting depth at which the statement was parsed.
 * @return Cache entry owning the statement, or NULL on failure.
 */
LPAST_ENTRY ScriptRegisterStatementEntry(
    LPSCRIPT_CONTEXT Context, LPAST_NODE Node, U32 SourceOffset, U32 SourceLength, U32 LoopDepth) {
    LPSCRIPT_AST_CACHE Cache;
    LPAST_ENTRY Entry;
    LPAST_ENTRY Existing;
    SCRIPT_AST_PARSE_KIND Kind;

    if (Context == NULL || Node == NULL || Context->AstCache == NULL) {
        return NULL;
    }

    Cache = Context->AstCache;
    Kind = ScriptASTParseKindFromNode(Node);

    Existing = ScriptASTCacheFindEntry(Cache, SourceOffset, SourceLength, Kind, LoopDepth);
    if (Existing != NULL) {
        if (Existing->Node == NULL) {
            Existing->Node = Node;
            Existing->NodeBytes = ScriptASTMeasureNode(Node);
            Cache->ResidentBytes += Existing->NodeBytes;
            ScriptASTCacheTouch(Existing);
        } else {
            ScriptDestroyAST(Node);
        }
        return Existing;
    }

    Entry = (LPAST_ENTRY)ScriptAlloc(Context, sizeof(SCRIPT_AST_ENTRY));
    if (Entry == NULL) {
        return NULL;
    }

    MemorySet(Entry, 0, sizeof(SCRIPT_AST_ENTRY));
    Entry->Cache = Cache;
    Entry->SourceOffset = SourceOffset;
    Entry->SourceLength = SourceLength;
    Entry->ParseKind = Kind;
    Entry->LoopDepth = LoopDepth;
    Entry->RefCount = 0;
    Entry->Node = Node;
    Entry->NodeBytes = ScriptASTMeasureNode(Node);
    Cache->ResidentBytes += Entry->NodeBytes;

    ListAddTail(Cache->List, Entry);

    return Entry;
}

/************************************************************************/

/**
 * @brief Ensure an entry has a resident node, re-parsing it from source on demand.
 * @param Cache Cache owning the entry.
 * @param Entry Entry to resolve.
 * @param Error Pointer to receive the parse error code.
 * @return Resident node or NULL on failure.
 */
LPAST_NODE ScriptASTEntryResolve(LPSCRIPT_AST_CACHE Cache, LPAST_ENTRY Entry, SCRIPT_ERROR* Error) {
    SCRIPT_PARSER LocalParser;
    LPAST_NODE Node;

    if (Cache == NULL || Entry == NULL || Error == NULL) {
        if (Error != NULL) {
            *Error = SCRIPT_ERROR_SYNTAX;
        }
        return NULL;
    }

    if (Entry->Node != NULL) {
        return Entry->Node;
    }

    if (Entry->Cache != Cache) {
        Entry->Cache = Cache;
    }

    // Re-parse must register nested statement entries into this cache, so it
    // must be visible as the context active cache even after a nested execution.
    Cache->Context->AstCache = Cache;

    ScriptInitParserAt(&LocalParser, Cache->Source, Cache->Context, Entry->SourceOffset);
    LocalParser.LoopDepth = Entry->LoopDepth;

    Cache->Resolving = TRUE;
    Node = ScriptASTCacheParseEntry(&LocalParser, Entry, Error);
    Cache->Resolving = FALSE;

    if (*Error != SCRIPT_OK || Node == NULL) {
        if (Node != NULL) {
            ScriptDestroyAST(Node);
        }
        return NULL;
    }

    Entry->Node = Node;
    Entry->NodeBytes = ScriptASTMeasureNode(Node);
    Cache->ResidentBytes += Entry->NodeBytes;
    ScriptASTCacheTouch(Entry);

    return Node;
}

/************************************************************************/

/**
 * @brief Evict the oldest unreferenced entries until resident payload is within budget.
 * @param Cache Cache to trim.
 * @param ProtectedEntry Entry that must survive this eviction pass.
 */
void ScriptASTCacheEvict(LPSCRIPT_AST_CACHE Cache, LPAST_ENTRY ProtectedEntry) {
    while (Cache != NULL && !Cache->Resolving && Cache->ResidentBytes > Cache->BudgetBytes) {
        LPAST_ENTRY Victim = NULL;

        for (LPLISTNODE Node = Cache->List->First; Node != NULL; Node = Node->Next) {
            LPAST_ENTRY Entry = (LPAST_ENTRY)Node;
            if (Entry->Node != NULL && Entry->RefCount == 0 && Entry != ProtectedEntry) {
                Victim = Entry;
                break;
            }
        }

        if (Victim == NULL) {
            break;
        }

        ScriptDestroyAST(Victim->Node);
        Cache->ResidentBytes -= Victim->NodeBytes;
        Victim->Node = NULL;
        Victim->NodeBytes = 0;
    }
}

/************************************************************************/
