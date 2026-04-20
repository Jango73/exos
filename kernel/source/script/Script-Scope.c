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


    Script Engine - Scope

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
 * @brief Create a new scope with optional parent.
 * @param Parent Parent scope or NULL for root scope
 * @return Pointer to new scope or NULL on failure
 */
LPSCRIPT_SCOPE ScriptCreateScope(LPSCRIPT_CONTEXT Context, LPSCRIPT_SCOPE Parent) {
    if (Context == NULL && Parent != NULL) {
        Context = Parent->Context;
    }

    LPSCRIPT_SCOPE Scope = (LPSCRIPT_SCOPE)ScriptAlloc(Context, sizeof(SCRIPT_SCOPE));
    if (Scope == NULL) {
        DEBUG(TEXT("[ScriptCreateScope] Failed to allocate scope"));
        return NULL;
    }

    MemorySet(Scope, 0, sizeof(SCRIPT_SCOPE));
    Scope->Context = Context;

    // Initialize hash table
    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        Scope->Buckets[i] = NewListEx(NULL, &Context->Allocator, AllocatorListAlloc, AllocatorListFree);
        if (Scope->Buckets[i] == NULL) {
            DEBUG(TEXT("[ScriptCreateScope] Failed to create bucket %d"), i);
            ScriptDestroyScope(Scope);
            return NULL;
        }
    }

    Scope->Parent = Parent;
    Scope->ScopeLevel = Parent ? Parent->ScopeLevel + 1 : 0;
    Scope->Count = 0;

    return Scope;
}

/************************************************************************/

/**
 * @brief Destroy a scope and all its variables.
 * @param Scope Scope to destroy
 */
void ScriptDestroyScope(LPSCRIPT_SCOPE Scope) {
    if (Scope == NULL) return;

    // Free all variables in this scope
    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        if (Scope->Buckets[i]) {
            LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Scope->Buckets[i]->First;
            while (Variable) {
                LPSCRIPT_VARIABLE Next = (LPSCRIPT_VARIABLE)Variable->Next;
                ScriptFreeVariable(Variable);
                Variable = Next;
            }
            DeleteList(Scope->Buckets[i]);
        }
    }

    ScriptFree(Scope->Context, Scope);
}

/************************************************************************/

/**
 * @brief Push a new scope onto the context scope stack.
 * @param Context Script context
 * @return Pointer to new scope or NULL on failure
 */
LPSCRIPT_SCOPE ScriptPushScope(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) return NULL;

    LPSCRIPT_SCOPE NewScope = ScriptCreateScope(Context, Context->CurrentScope);
    if (NewScope == NULL) {
        return NULL;
    }

    Context->CurrentScope = NewScope;

    return NewScope;
}

/************************************************************************/

/**
 * @brief Pop the current scope and return to parent.
 * @param Context Script context
 */
void ScriptPopScope(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL || Context->CurrentScope == NULL) return;

    LPSCRIPT_SCOPE OldScope = Context->CurrentScope;
    Context->CurrentScope = OldScope->Parent;

    // Don't destroy the global scope
    if (OldScope != Context->GlobalScope) {
        ScriptDestroyScope(OldScope);
    }
}

/************************************************************************/

/**
 * @brief Find a variable in a scope, optionally searching parent scopes.
 * @param Scope Starting scope
 * @param Name Variable name
 * @param SearchParents TRUE to search parent scopes
 * @return Pointer to variable or NULL if not found
 */
LPSCRIPT_VARIABLE ScriptFindVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, BOOL SearchParents) {
    if (Scope == NULL || Name == NULL) return NULL;

    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Scope->Buckets[Hash];

    // Search in current scope
    for (LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)Bucket->First; Variable; Variable = (LPSCRIPT_VARIABLE)Variable->Next) {
        if (STRINGS_EQUAL(Variable->Name, Name)) {
            return Variable;
        }
    }

    // Search in parent scopes if requested
    if (SearchParents && Scope->Parent) {
        return ScriptFindVariableInScope(Scope->Parent, Name, TRUE);
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Set a variable in a specific scope.
 * @param Scope Target scope
 * @param Name Variable name
 * @param Type Variable type
 * @param Value Variable value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetVariableInScope(LPSCRIPT_SCOPE Scope, LPCSTR Name, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Scope == NULL || Name == NULL) return NULL;

    // First check if variable exists in current or parent scopes
    LPSCRIPT_VARIABLE ExistingVar = ScriptFindVariableInScope(Scope, Name, TRUE);

    SAFE_USE(ExistingVar) {
        // Update existing variable in whichever scope it was found
        ScriptReleaseStoredValue(Scope->Context, ExistingVar->Type, &ExistingVar->Value);

        ExistingVar->Type = Type;
        if (ScriptStoreObjectValue(Scope->Context, Type, &Value, &ExistingVar->Value) != SCRIPT_OK) {
            MemorySet(&ExistingVar->Value, 0, sizeof(SCRIPT_VAR_VALUE));
            return NULL;
        }

        return ExistingVar;
    }

    // Variable doesn't exist anywhere, create new variable in current scope
    U32 Hash = ScriptHashVariable(Name);
    LPLIST Bucket = Scope->Buckets[Hash];

    LPSCRIPT_VARIABLE Variable = (LPSCRIPT_VARIABLE)ScriptAlloc(Scope->Context, sizeof(SCRIPT_VARIABLE));
    if (Variable == NULL) return NULL;

    MemorySet(Variable, 0, sizeof(SCRIPT_VARIABLE));
    Variable->Context = Scope->Context;
    StringCopy(Variable->Name, Name);
    Variable->Type = Type;
    Variable->RefCount = 1;
    if (ScriptStoreObjectValue(Scope->Context, Type, &Value, &Variable->Value) != SCRIPT_OK) {
        ScriptFree(Scope->Context, Variable);
        return NULL;
    }

    ListAddItem(Bucket, Variable);
    Scope->Count++;

    return Variable;
}
