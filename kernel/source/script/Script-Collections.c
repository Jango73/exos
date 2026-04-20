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


    Script Engine - Values, Arrays and Host Symbols

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
 * @brief Free a variable and its resources.
 * @param Variable Variable to free
 */
void ScriptFreeVariable(LPSCRIPT_VARIABLE Variable) {
    if (Variable == NULL) return;

    ScriptReleaseStoredValue(Variable->Context, Variable->Type, &Variable->Value);

    ScriptFree(Variable->Context, Variable);
}

/************************************************************************/

void ScriptValueInit(SCRIPT_VALUE* Value) {
    if (Value == NULL) {
        return;
    }

    MemorySet(Value, 0, sizeof(SCRIPT_VALUE));
    Value->Type = SCRIPT_VAR_FLOAT;
    Value->Value.Float = 0.0f;
    Value->ContextOwner = NULL;
    Value->OwnsValue = FALSE;
    Value->HostDescriptor = NULL;
    Value->HostContext = NULL;
}

/************************************************************************/

void ScriptValueRelease(SCRIPT_VALUE* Value) {
    if (Value == NULL) {
        return;
    }

    if (Value->Type == SCRIPT_VAR_STRING && Value->OwnsValue && Value->Value.String) {
        ScriptFree(Value->ContextOwner, Value->Value.String);
    } else if (Value->Type == SCRIPT_VAR_ARRAY && Value->OwnsValue && Value->Value.Array) {
        ScriptDestroyArray(Value->Value.Array);
    } else if (Value->Type == SCRIPT_VAR_OBJECT && Value->OwnsValue && Value->Value.Object) {
        ScriptReleaseObject(Value->Value.Object);
    } else if (Value->Type == SCRIPT_VAR_HOST_HANDLE && Value->OwnsValue &&
               Value->Value.HostHandle && Value->HostDescriptor &&
               Value->HostDescriptor->ReleaseHandle) {
        LPVOID HostCtx = Value->HostContext ? Value->HostContext : Value->HostDescriptor->Context;
        Value->HostDescriptor->ReleaseHandle(HostCtx, Value->Value.HostHandle);
    }

    Value->Type = SCRIPT_VAR_FLOAT;
    Value->Value.Float = 0.0f;
    Value->ContextOwner = NULL;
    Value->OwnsValue = FALSE;
    Value->HostDescriptor = NULL;
    Value->HostContext = NULL;
}

/************************************************************************/
/**
 * @brief Release one stored script value owned by a variable, array, or return slot.
 * @param Context Owning script context.
 * @param Type Stored value type.
 * @param Value Stored value payload.
 */
void ScriptReleaseStoredValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VAR_TYPE Type,
    SCRIPT_VAR_VALUE* Value) {
    if (Value == NULL) {
        return;
    }

    if (Type == SCRIPT_VAR_STRING && Value->String != NULL) {
        ScriptFree(Context, Value->String);
    } else if (Type == SCRIPT_VAR_ARRAY && Value->Array != NULL) {
        ScriptDestroyArray(Value->Array);
    } else if (Type == SCRIPT_VAR_OBJECT && Value->Object != NULL) {
        ScriptReleaseObject(Value->Object);
    }

    MemorySet(Value, 0, sizeof(SCRIPT_VAR_VALUE));
}

/************************************************************************/
/**
 * @brief Copy one runtime value into owned variable-style storage.
 * @param Context Owning script context.
 * @param Type Source value type.
 * @param SourceValue Source value payload.
 * @param DestinationValue Destination owned payload.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptStoreObjectValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VAR_TYPE Type,
    const SCRIPT_VAR_VALUE* SourceValue,
    SCRIPT_VAR_VALUE* DestinationValue) {
    if (Context == NULL || SourceValue == NULL || DestinationValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    MemorySet(DestinationValue, 0, sizeof(SCRIPT_VAR_VALUE));

    if (Type == SCRIPT_VAR_STRING) {
        U32 Length;

        if (SourceValue->String == NULL) {
            DestinationValue->String = NULL;
            return SCRIPT_OK;
        }

        Length = StringLength(SourceValue->String) + 1;
        DestinationValue->String = (LPSTR)ScriptAlloc(Context, Length);
        if (DestinationValue->String == NULL) {
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        StringCopy(DestinationValue->String, SourceValue->String);
        return SCRIPT_OK;
    }

    if (Type == SCRIPT_VAR_OBJECT) {
        DestinationValue->Object = SourceValue->Object;
        if (DestinationValue->Object != NULL) {
            ScriptRetainObject(DestinationValue->Object);
        }
        return SCRIPT_OK;
    }

    if (Type == SCRIPT_VAR_HOST_HANDLE) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    *DestinationValue = *SourceValue;
    return SCRIPT_OK;
}

/************************************************************************/

U32 ScriptHashHostSymbol(LPCSTR Name) {
    return ScriptHashVariable(Name);
}

/************************************************************************/

BOOL ScriptInitHostRegistry(LPSCRIPT_CONTEXT Context, LPSCRIPT_HOST_REGISTRY Registry) {
    if (Registry == NULL || Context == NULL) {
        return FALSE;
    }

    MemorySet(Registry, 0, sizeof(SCRIPT_HOST_REGISTRY));
    Registry->Context = Context;

    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        Registry->Buckets[i] = NewListEx(NULL, &Context->Allocator, AllocatorListAlloc, AllocatorListFree);
        if (Registry->Buckets[i] == NULL) {
            for (U32 j = 0; j < i; j++) {
                if (Registry->Buckets[j]) {
                    DeleteList(Registry->Buckets[j]);
                    Registry->Buckets[j] = NULL;
                }
            }
            return FALSE;
        }
    }

    Registry->Count = 0;
    return TRUE;
}

/************************************************************************/

void ScriptReleaseHostSymbol(LPSCRIPT_HOST_SYMBOL Symbol) {
    if (Symbol == NULL) {
        return;
    }

    if (Symbol->Descriptor && Symbol->Descriptor->ReleaseHandle && Symbol->Handle) {
        LPVOID HostCtx = Symbol->Context ? Symbol->Context : Symbol->Descriptor->Context;
        Symbol->Descriptor->ReleaseHandle(HostCtx, Symbol->Handle);
    }

    ScriptFree(Symbol->ContextOwner, Symbol);
}

/************************************************************************/

void ScriptClearHostRegistryInternal(LPSCRIPT_HOST_REGISTRY Registry) {
    if (Registry == NULL) {
        return;
    }

    for (U32 i = 0; i < SCRIPT_VAR_HASH_SIZE; i++) {
        if (Registry->Buckets[i]) {
            LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Registry->Buckets[i]->First;
            while (Symbol) {
                LPSCRIPT_HOST_SYMBOL Next = (LPSCRIPT_HOST_SYMBOL)Symbol->Next;
                ScriptReleaseHostSymbol(Symbol);
                Symbol = Next;
            }
            DeleteList(Registry->Buckets[i]);
            Registry->Buckets[i] = NULL;
        }
    }

    Registry->Count = 0;
}

/************************************************************************/

LPSCRIPT_HOST_SYMBOL ScriptFindHostSymbol(LPSCRIPT_HOST_REGISTRY Registry, LPCSTR Name) {
    if (Registry == NULL || Name == NULL) {
        return NULL;
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Registry->Buckets[Hash];
    if (Bucket == NULL) {
        return NULL;
    }

    for (LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Bucket->First;
         Symbol;
         Symbol = (LPSCRIPT_HOST_SYMBOL)Symbol->Next) {
        if (STRINGS_EQUAL(Symbol->Name, Name)) {
            return Symbol;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Resolve the owning script context for one public value access.
 * @param Value Source value.
 * @return Script context or NULL when unavailable.
 */
static LPSCRIPT_CONTEXT ScriptGetValueContext(const SCRIPT_VALUE* Value) {
    if (Value == NULL) {
        return NULL;
    }

    return Value->ContextOwner;
}

/************************************************************************/

/**
 * @brief Resolve one registered host symbol into a reusable script value.
 * @param Context Script context that owns the host registry.
 * @param Name Host symbol name.
 * @param OutValue Destination script value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptGetHostSymbolValue(
    LPSCRIPT_CONTEXT Context,
    LPCSTR Name,
    LPSCRIPT_VALUE OutValue) {
    LPSCRIPT_HOST_SYMBOL HostSymbol;
    LPVOID HostContext;
    SCRIPT_ERROR Result;

    if (Context == NULL || Name == NULL || OutValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    ScriptValueInit(OutValue);

    HostSymbol = ScriptFindHostSymbol(&Context->HostRegistry, Name);
    if (HostSymbol == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    HostContext = HostSymbol->Context ? HostSymbol->Context : HostSymbol->Descriptor->Context;

    if (HostSymbol->Kind == SCRIPT_HOST_SYMBOL_PROPERTY) {
        if (HostSymbol->Descriptor == NULL || HostSymbol->Descriptor->GetProperty == NULL) {
            return SCRIPT_ERROR_TYPE_MISMATCH;
        }

        Result = HostSymbol->Descriptor->GetProperty(
            HostContext,
            HostSymbol->Handle,
            HostSymbol->Name,
            OutValue);
        if (Result != SCRIPT_OK) {
            ScriptValueRelease(OutValue);
            return Result;
        }

        Result = ScriptPrepareHostValue(
            Context,
            OutValue,
            HostSymbol->Descriptor,
            HostContext);
        if (Result != SCRIPT_OK) {
            ScriptValueRelease(OutValue);
            return Result;
        }

        if (OutValue->ContextOwner == NULL) {
            OutValue->ContextOwner = Context;
        }

        return SCRIPT_OK;
    }

    OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
    OutValue->Value.HostHandle = HostSymbol->Handle;
    OutValue->ContextOwner = Context;
    OutValue->HostDescriptor = HostSymbol->Descriptor;
    OutValue->OwnsValue = FALSE;
    OutValue->HostContext = HostContext;
    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Resolve one exposed host property from a script host value.
 * @param ParentValue Parent host value.
 * @param Property Property name.
 * @param OutValue Destination script value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptGetHostPropertyValue(
    const SCRIPT_VALUE* ParentValue,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {
    LPSCRIPT_CONTEXT Context;
    LPVOID HostContext;
    SCRIPT_ERROR Result;

    if (ParentValue == NULL || Property == NULL || OutValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (ParentValue->Type != SCRIPT_VAR_HOST_HANDLE ||
        ParentValue->HostDescriptor == NULL ||
        ParentValue->HostDescriptor->GetProperty == NULL) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    Context = ScriptGetValueContext(ParentValue);
    if (Context == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    ScriptValueInit(OutValue);

    HostContext = ParentValue->HostContext
        ? ParentValue->HostContext
        : ParentValue->HostDescriptor->Context;

    Result = ParentValue->HostDescriptor->GetProperty(
        HostContext,
        ParentValue->Value.HostHandle,
        Property,
        OutValue);
    if (Result != SCRIPT_OK) {
        ScriptValueRelease(OutValue);
        return Result;
    }

    Result = ScriptPrepareHostValue(
        Context,
        OutValue,
        ParentValue->HostDescriptor,
        HostContext);
    if (Result != SCRIPT_OK) {
        ScriptValueRelease(OutValue);
        return Result;
    }

    if (OutValue->ContextOwner == NULL) {
        OutValue->ContextOwner = Context;
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Resolve one exposed host array element from a script host value.
 * @param ParentValue Parent host value.
 * @param Index Element index.
 * @param OutValue Destination script value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptGetHostElementValue(
    const SCRIPT_VALUE* ParentValue,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {
    LPSCRIPT_CONTEXT Context;
    LPVOID HostContext;
    SCRIPT_ERROR Result;

    if (ParentValue == NULL || OutValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (ParentValue->Type != SCRIPT_VAR_HOST_HANDLE ||
        ParentValue->HostDescriptor == NULL ||
        ParentValue->HostDescriptor->GetElement == NULL) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    Context = ScriptGetValueContext(ParentValue);
    if (Context == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    ScriptValueInit(OutValue);

    HostContext = ParentValue->HostContext
        ? ParentValue->HostContext
        : ParentValue->HostDescriptor->Context;

    Result = ParentValue->HostDescriptor->GetElement(
        HostContext,
        ParentValue->Value.HostHandle,
        Index,
        OutValue);
    if (Result != SCRIPT_OK) {
        ScriptValueRelease(OutValue);
        return Result;
    }

    Result = ScriptPrepareHostValue(
        Context,
        OutValue,
        ParentValue->HostDescriptor,
        HostContext);
    if (Result != SCRIPT_OK) {
        ScriptValueRelease(OutValue);
        return Result;
    }

    if (OutValue->ContextOwner == NULL) {
        OutValue->ContextOwner = Context;
    }

    return SCRIPT_OK;
}

/************************************************************************/
/**
 * @brief Create one native E0 object container.
 * @param Context Owning script context.
 * @param InitialCapacity Initial property capacity.
 * @return Allocated object, or NULL on failure.
 */
LPSCRIPT_OBJECT ScriptCreateObject(LPSCRIPT_CONTEXT Context, U32 InitialCapacity) {
    LPSCRIPT_OBJECT Object;

    if (Context == NULL) {
        return NULL;
    }

    if (InitialCapacity == 0) {
        InitialCapacity = 4;
    }

    Object = (LPSCRIPT_OBJECT)ScriptAlloc(Context, sizeof(SCRIPT_OBJECT));
    if (Object == NULL) {
        return NULL;
    }

    MemorySet(Object, 0, sizeof(SCRIPT_OBJECT));
    Object->Context = Context;
    Object->RefCount = 1;
    Object->PropertyCapacity = InitialCapacity;
    Object->Properties = (LPSCRIPT_OBJECT_PROPERTY)ScriptAlloc(
        Context,
        sizeof(SCRIPT_OBJECT_PROPERTY) * InitialCapacity);
    if (Object->Properties == NULL) {
        ScriptFree(Context, Object);
        return NULL;
    }

    MemorySet(
        Object->Properties,
        0,
        sizeof(SCRIPT_OBJECT_PROPERTY) * InitialCapacity);
    return Object;
}

/************************************************************************/
/**
 * @brief Increment one native object reference count.
 * @param Object Object to retain.
 */
void ScriptRetainObject(LPSCRIPT_OBJECT Object) {
    if (Object == NULL) {
        return;
    }

    Object->RefCount++;
}

/************************************************************************/
/**
 * @brief Release one native object reference.
 * @param Object Object to release.
 */
void ScriptReleaseObject(LPSCRIPT_OBJECT Object) {
    if (Object == NULL) {
        return;
    }

    if (Object->RefCount > 1) {
        Object->RefCount--;
        return;
    }

    for (U32 Index = 0; Index < Object->PropertyCount; Index++) {
        ScriptValueRelease(&Object->Properties[Index].Value);
    }

    ScriptFree(Object->Context, Object->Properties);
    ScriptFree(Object->Context, Object);
}

/************************************************************************/
/**
 * @brief Find one native object property by name.
 * @param Object Native object to inspect.
 * @param Name Property name.
 * @return Matching property, or NULL when absent.
 */
static LPSCRIPT_OBJECT_PROPERTY ScriptFindObjectProperty(
    LPSCRIPT_OBJECT Object,
    LPCSTR Name) {
    if (Object == NULL || Name == NULL) {
        return NULL;
    }

    for (U32 Index = 0; Index < Object->PropertyCount; Index++) {
        if (STRINGS_EQUAL(Object->Properties[Index].Name, Name)) {
            return &Object->Properties[Index];
        }
    }

    return NULL;
}

/************************************************************************/
/**
 * @brief Ensure one native object can store at least one more property.
 * @param Object Native object to grow.
 * @return SCRIPT_OK on success, otherwise an allocation error.
 */
static SCRIPT_ERROR ScriptEnsureObjectPropertyCapacity(LPSCRIPT_OBJECT Object) {
    LPSCRIPT_OBJECT_PROPERTY NewProperties;
    U32 NewCapacity;

    if (Object == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (Object->PropertyCount < Object->PropertyCapacity) {
        return SCRIPT_OK;
    }

    NewCapacity = Object->PropertyCapacity == 0 ? 4 : (Object->PropertyCapacity * 2);
    NewProperties = (LPSCRIPT_OBJECT_PROPERTY)ScriptAlloc(
        Object->Context,
        sizeof(SCRIPT_OBJECT_PROPERTY) * NewCapacity);
    if (NewProperties == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    MemorySet(NewProperties, 0, sizeof(SCRIPT_OBJECT_PROPERTY) * NewCapacity);
    for (U32 Index = 0; Index < Object->PropertyCount; Index++) {
        NewProperties[Index] = Object->Properties[Index];
    }

    ScriptFree(Object->Context, Object->Properties);
    Object->Properties = NewProperties;
    Object->PropertyCapacity = NewCapacity;
    return SCRIPT_OK;
}

/************************************************************************/
/**
 * @brief Copy one runtime value into one object property slot.
 * @param Object Native object that will own the stored value.
 * @param TargetValue Property storage slot.
 * @param SourceValue Source runtime value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
static SCRIPT_ERROR ScriptStoreValueInObjectProperty(
    LPSCRIPT_OBJECT Object,
    LPSCRIPT_VALUE TargetValue,
    const SCRIPT_VALUE* SourceValue) {
    SCRIPT_VAR_VALUE StoredValue;
    SCRIPT_ERROR Result;

    if (Object == NULL || TargetValue == NULL || SourceValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (SourceValue->Type == SCRIPT_VAR_HOST_HANDLE) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    Result = ScriptStoreObjectValue(
        Object->Context,
        SourceValue->Type,
        &SourceValue->Value,
        &StoredValue);
    if (Result != SCRIPT_OK) {
        return Result;
    }

    ScriptValueRelease(TargetValue);
    ScriptValueInit(TargetValue);
    TargetValue->Type = SourceValue->Type;
    TargetValue->Value = StoredValue;
    TargetValue->ContextOwner = Object->Context;
    TargetValue->OwnsValue = (
        SourceValue->Type == SCRIPT_VAR_STRING ||
        SourceValue->Type == SCRIPT_VAR_ARRAY ||
        SourceValue->Type == SCRIPT_VAR_OBJECT);
    return SCRIPT_OK;
}

/************************************************************************/
/**
 * @brief Set or create one native object property.
 * @param Object Native object to mutate.
 * @param Name Property name.
 * @param Value Source runtime value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptSetObjectProperty(
    LPSCRIPT_OBJECT Object,
    LPCSTR Name,
    const SCRIPT_VALUE* Value) {
    LPSCRIPT_OBJECT_PROPERTY Property;
    SCRIPT_ERROR Result;

    if (Object == NULL || Name == NULL || Value == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    Property = ScriptFindObjectProperty(Object, Name);
    if (Property == NULL) {
        Result = ScriptEnsureObjectPropertyCapacity(Object);
        if (Result != SCRIPT_OK) {
            return Result;
        }

        Property = &Object->Properties[Object->PropertyCount++];
        MemorySet(Property, 0, sizeof(SCRIPT_OBJECT_PROPERTY));
        StringCopy(Property->Name, Name);
        ScriptValueInit(&Property->Value);
    }

    return ScriptStoreValueInObjectProperty(Object, &Property->Value, Value);
}

/************************************************************************/
/**
 * @brief Read one native object property by reference.
 * @param Object Native object to inspect.
 * @param Name Property name.
 * @param OutValue Destination runtime value.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptGetObjectProperty(
    LPSCRIPT_OBJECT Object,
    LPCSTR Name,
    LPSCRIPT_VALUE OutValue) {
    LPSCRIPT_OBJECT_PROPERTY Property;

    if (Object == NULL || Name == NULL || OutValue == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    Property = ScriptFindObjectProperty(Object, Name);
    if (Property == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    ScriptValueInit(OutValue);
    OutValue->Type = Property->Value.Type;
    OutValue->Value = Property->Value.Value;
    OutValue->ContextOwner = Object->Context;
    OutValue->HostDescriptor = Property->Value.HostDescriptor;
    OutValue->HostContext = Property->Value.HostContext;
    OutValue->OwnsValue = FALSE;
    return SCRIPT_OK;
}

/************************************************************************/
/**
 * @brief Release one array element payload.
 * @param Array Array that owns the element.
 * @param Index Element index to release.
 */
static void ScriptArrayReleaseElement(LPSCRIPT_ARRAY Array, U32 Index) {
    if (Array == NULL || Index >= Array->Size) {
        return;
    }

    if (Array->Elements[Index] == NULL) {
        return;
    }

    if (Array->ElementTypes[Index] == SCRIPT_VAR_STRING ||
        Array->ElementTypes[Index] == SCRIPT_VAR_INTEGER ||
        Array->ElementTypes[Index] == SCRIPT_VAR_FLOAT) {
        ScriptFree(Array->Context, Array->Elements[Index]);
    } else if (Array->ElementTypes[Index] == SCRIPT_VAR_OBJECT) {
        ScriptReleaseObject((LPSCRIPT_OBJECT)Array->Elements[Index]);
    }

    Array->Elements[Index] = NULL;
}
/**
 * @brief Create a new array with initial capacity.
 * @param InitialCapacity Initial capacity of the array
 * @return Pointer to new array or NULL on failure
 */
LPSCRIPT_ARRAY ScriptCreateArray(LPSCRIPT_CONTEXT Context, U32 InitialCapacity) {
    LPSCRIPT_ARRAY Array;

    if (InitialCapacity == 0) InitialCapacity = 4;

    Array = (LPSCRIPT_ARRAY)ScriptAlloc(Context, sizeof(SCRIPT_ARRAY));
    if (Array == NULL) return NULL;

    MemorySet(Array, 0, sizeof(SCRIPT_ARRAY));
    Array->Context = Context;
    Array->Elements = (LPVOID*)ScriptAlloc(Context, InitialCapacity * sizeof(LPVOID));
    Array->ElementTypes = (SCRIPT_VAR_TYPE*)ScriptAlloc(Context, InitialCapacity * sizeof(SCRIPT_VAR_TYPE));

    if (Array->Elements == NULL || Array->ElementTypes == NULL) {
        if (Array->Elements) ScriptFree(Context, Array->Elements);
        if (Array->ElementTypes) ScriptFree(Context, Array->ElementTypes);
        ScriptFree(Context, Array);
        return NULL;
    }

    Array->Size = 0;
    Array->Capacity = InitialCapacity;

    return Array;
}

/************************************************************************/

/**
 * @brief Destroy an array and free all resources.
 * @param Array Array to destroy
 */
void ScriptDestroyArray(LPSCRIPT_ARRAY Array) {
    if (Array == NULL) return;

    for (U32 i = 0; i < Array->Size; i++) {
        ScriptArrayReleaseElement(Array, i);
    }

    ScriptFree(Array->Context, Array->Elements);
    ScriptFree(Array->Context, Array->ElementTypes);
    ScriptFree(Array->Context, Array);
}

/************************************************************************/

/**
 * @brief Set an array element value.
 * @param Array Array to modify
 * @param Index Element index
 * @param Type Element type
 * @param Value Element value
 * @return Script error code
 */
SCRIPT_ERROR ScriptArraySet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Array == NULL) return SCRIPT_ERROR_SYNTAX;

    // Resize array if necessary
    if (Index >= Array->Capacity) {
        U32 NewCapacity = Index + 1;
        if (NewCapacity < Array->Capacity * 2) NewCapacity = Array->Capacity * 2;

        LPVOID* NewElements = (LPVOID*)ScriptAlloc(Array->Context, NewCapacity * sizeof(LPVOID));
        SCRIPT_VAR_TYPE* NewTypes = (SCRIPT_VAR_TYPE*)ScriptAlloc(Array->Context, NewCapacity * sizeof(SCRIPT_VAR_TYPE));

        if (NewElements == NULL || NewTypes == NULL) {
            if (NewElements) ScriptFree(Array->Context, NewElements);
            if (NewTypes) ScriptFree(Array->Context, NewTypes);
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        // Copy existing elements
        for (U32 i = 0; i < Array->Size; i++) {
            NewElements[i] = Array->Elements[i];
            NewTypes[i] = Array->ElementTypes[i];
        }

        ScriptFree(Array->Context, Array->Elements);
        ScriptFree(Array->Context, Array->ElementTypes);
        Array->Elements = NewElements;
        Array->ElementTypes = NewTypes;
        Array->Capacity = NewCapacity;
    }

    if (Index < Array->Size) {
        ScriptArrayReleaseElement(Array, Index);
    }

    Array->ElementTypes[Index] = Type;

    if (Type == SCRIPT_VAR_STRING && Value.String) {
        U32 Len = StringLength(Value.String) + 1;
        Array->Elements[Index] = ScriptAlloc(Array->Context, Len);
        if (Array->Elements[Index] == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        StringCopy((LPSTR)Array->Elements[Index], Value.String);
    } else if (Type == SCRIPT_VAR_INTEGER) {
        INT* IntPtr = (INT*)ScriptAlloc(Array->Context, sizeof(INT));
        if (IntPtr == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        *IntPtr = Value.Integer;
        Array->Elements[Index] = IntPtr;
    } else if (Type == SCRIPT_VAR_FLOAT) {
        F32* FloatPtr = (F32*)ScriptAlloc(Array->Context, sizeof(F32));
        if (FloatPtr == NULL) return SCRIPT_ERROR_OUT_OF_MEMORY;
        *FloatPtr = Value.Float;
        Array->Elements[Index] = FloatPtr;
    } else if (Type == SCRIPT_VAR_OBJECT) {
        Array->Elements[Index] = Value.Object;
        if (Value.Object != NULL) {
            ScriptRetainObject(Value.Object);
        }
    } else {
        Array->Elements[Index] = NULL;
    }

    if (Index >= Array->Size) Array->Size = Index + 1;

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Get an array element value.
 * @param Array Array to query
 * @param Index Element index
 * @param Type Pointer to receive element type
 * @param Value Pointer to receive element value
 * @return Script error code
 */
SCRIPT_ERROR ScriptArrayGet(LPSCRIPT_ARRAY Array, U32 Index, SCRIPT_VAR_TYPE* Type, SCRIPT_VAR_VALUE* Value) {
    if (Array == NULL || Type == NULL || Value == NULL) return SCRIPT_ERROR_SYNTAX;
    if (Index >= Array->Size) return SCRIPT_ERROR_UNDEFINED_VAR;

    *Type = Array->ElementTypes[Index];

    if (*Type == SCRIPT_VAR_STRING) {
        Value->String = (LPSTR)Array->Elements[Index];
    } else if (*Type == SCRIPT_VAR_INTEGER) {
        Value->Integer = *(INT*)Array->Elements[Index];
    } else if (*Type == SCRIPT_VAR_FLOAT) {
        Value->Float = *(F32*)Array->Elements[Index];
    } else if (*Type == SCRIPT_VAR_OBJECT) {
        Value->Object = (LPSCRIPT_OBJECT)Array->Elements[Index];
    } else {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Set an array element in a script variable.
 * @param Context Script context
 * @param Name Variable name
 * @param Index Array index
 * @param Type Element type
 * @param Value Element value
 * @return Pointer to variable or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptSetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index, SCRIPT_VAR_TYPE Type, SCRIPT_VAR_VALUE Value) {
    if (Context == NULL || Name == NULL) return NULL;

    LPSCRIPT_VARIABLE Variable = ScriptGetVariable(Context, Name);

    // Create array variable if it doesn't exist
    if (Variable == NULL) {
        SCRIPT_VAR_VALUE ArrayValue;
        ArrayValue.Array = ScriptCreateArray(Context, 0);
        if (ArrayValue.Array == NULL) return NULL;

        Variable = ScriptSetVariable(Context, Name, SCRIPT_VAR_ARRAY, ArrayValue);
        if (Variable == NULL) {
            ScriptDestroyArray(ArrayValue.Array);
            return NULL;
        }
    }

    // Ensure variable is an array
    if (Variable->Type != SCRIPT_VAR_ARRAY) {
        return NULL;
    }

    SCRIPT_ERROR Error = ScriptArraySet(Variable->Value.Array, Index, Type, Value);
    if (Error != SCRIPT_OK) return NULL;

    return Variable;
}

/************************************************************************/

/**
 * @brief Get an array element from a script variable.
 * @param Context Script context
 * @param Name Variable name
 * @param Index Array index
 * @return Pointer to temporary variable containing element value, or NULL on failure
 */
LPSCRIPT_VARIABLE ScriptGetArrayElement(LPSCRIPT_CONTEXT Context, LPCSTR Name, U32 Index) {
    if (Context == NULL || Name == NULL) return NULL;

    LPSCRIPT_VARIABLE Variable = ScriptGetVariable(Context, Name);
    if (Variable == NULL || Variable->Type != SCRIPT_VAR_ARRAY) return NULL;

    SCRIPT_VAR_TYPE ElementType;
    SCRIPT_VAR_VALUE ElementValue;

    SCRIPT_ERROR Error = ScriptArrayGet(Variable->Value.Array, Index, &ElementType, &ElementValue);
    if (Error != SCRIPT_OK) return NULL;

    // Create a temporary variable to hold the element value
    LPSCRIPT_VARIABLE TempVar = (LPSCRIPT_VARIABLE)ScriptAlloc(Context, sizeof(SCRIPT_VARIABLE));
    if (TempVar == NULL) return NULL;

    MemorySet(TempVar, 0, sizeof(SCRIPT_VARIABLE));
    TempVar->Context = Context;
    TempVar->Type = ElementType;
    TempVar->Value = ElementValue;
    TempVar->RefCount = 1;

    return TempVar;
}

/************************************************************************/

BOOL ScriptRegisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name, SCRIPT_HOST_SYMBOL_KIND Kind, SCRIPT_HOST_HANDLE Handle, const SCRIPT_HOST_DESCRIPTOR* Descriptor, LPVOID ContextPointer) {
    if (Context == NULL || Name == NULL || Descriptor == NULL) {
        return FALSE;
    }

    if (Context->HostRegistry.Buckets[0] == NULL) {
        if (!ScriptInitHostRegistry(Context, &Context->HostRegistry)) {
            return FALSE;
        }
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Context->HostRegistry.Buckets[Hash];
    if (Bucket == NULL) {
        Bucket = NewListEx(NULL, &Context->Allocator, AllocatorListAlloc, AllocatorListFree);
        if (Bucket == NULL) {
            return FALSE;
        }
        Context->HostRegistry.Buckets[Hash] = Bucket;
    }

    LPSCRIPT_HOST_SYMBOL Existing = ScriptFindHostSymbol(&Context->HostRegistry, Name);
    if (Existing) {
        ListRemove(Bucket, Existing);
        ScriptReleaseHostSymbol(Existing);
        if (Context->HostRegistry.Count > 0) {
            Context->HostRegistry.Count--;
        }
    }

    LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)ScriptAlloc(Context, sizeof(SCRIPT_HOST_SYMBOL));
    if (Symbol == NULL) {
        return FALSE;
    }

    MemorySet(Symbol, 0, sizeof(SCRIPT_HOST_SYMBOL));
    Symbol->ContextOwner = Context;
    StringCopy(Symbol->Name, Name);
    Symbol->Kind = Kind;
    Symbol->Handle = Handle;
    Symbol->Descriptor = Descriptor;
    Symbol->Context = ContextPointer;

    ListAddItem(Bucket, Symbol);
    Context->HostRegistry.Count++;

    return TRUE;
}

/************************************************************************/

void ScriptUnregisterHostSymbol(LPSCRIPT_CONTEXT Context, LPCSTR Name) {
    if (Context == NULL || Name == NULL) {
        return;
    }

    if (Context->HostRegistry.Buckets[0] == NULL) {
        return;
    }

    U32 Hash = ScriptHashHostSymbol(Name);
    LPLIST Bucket = Context->HostRegistry.Buckets[Hash];
    if (Bucket == NULL) {
        return;
    }

    for (LPSCRIPT_HOST_SYMBOL Symbol = (LPSCRIPT_HOST_SYMBOL)Bucket->First;
         Symbol;
         Symbol = (LPSCRIPT_HOST_SYMBOL)Symbol->Next) {
        if (STRINGS_EQUAL(Symbol->Name, Name)) {
            ListRemove(Bucket, Symbol);
            ScriptReleaseHostSymbol(Symbol);
            if (Context->HostRegistry.Count > 0) {
                Context->HostRegistry.Count--;
            }
            return;
        }
    }
}

/************************************************************************/

void ScriptClearHostSymbols(LPSCRIPT_CONTEXT Context) {
    if (Context == NULL) {
        return;
    }

    ScriptClearHostRegistryInternal(&Context->HostRegistry);
    ScriptInitHostRegistry(Context, &Context->HostRegistry);
}

/************************************************************************/

SCRIPT_ERROR ScriptPrepareHostValue(
    LPSCRIPT_CONTEXT Context,
    SCRIPT_VALUE* Value,
    const SCRIPT_HOST_DESCRIPTOR* DefaultDescriptor,
    LPVOID DefaultContext) {
    if (Value == NULL || Context == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (Value->Type == SCRIPT_VAR_STRING && Value->Value.String && !Value->OwnsValue) {
        U32 Len = StringLength(Value->Value.String) + 1;
        LPSTR Copy = (LPSTR)ScriptAlloc(Context, Len);
        if (Copy == NULL) {
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }
        StringCopy(Copy, Value->Value.String);
        Value->Value.String = Copy;
        Value->ContextOwner = Context;
        Value->OwnsValue = TRUE;
    }

    if (Value->Type == SCRIPT_VAR_HOST_HANDLE) {
        if (Value->HostDescriptor == NULL) {
            Value->HostDescriptor = DefaultDescriptor;
        }
        if (Value->HostContext == NULL) {
            Value->HostContext = DefaultContext;
        }
    }

    return SCRIPT_OK;
}

/************************************************************************/

BOOL ScriptValueToFloat(const SCRIPT_VALUE* Value, F32* OutValue) {
    if (Value == NULL || OutValue == NULL) {
        return FALSE;
    }

    if (Value->Type == SCRIPT_VAR_FLOAT) {
        *OutValue = Value->Value.Float;
        return TRUE;
    }

    if (Value->Type == SCRIPT_VAR_INTEGER) {
        *OutValue = (F32)Value->Value.Integer;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL ScriptValueToInteger(const SCRIPT_VALUE* Value, INT* OutValue) {
    if (Value == NULL || OutValue == NULL) {
        return FALSE;
    }

    if (Value->Type == SCRIPT_VAR_INTEGER) {
        *OutValue = Value->Value.Integer;
        return TRUE;
    }

    if (Value->Type == SCRIPT_VAR_FLOAT && IsInteger(Value->Value.Float)) {
        *OutValue = (INT)Value->Value.Float;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Convert one script value to text for string-oriented operations.
 * @param Value Source value.
 * @param Context Allocator owner for temporary text conversion.
 * @param OutText Receives converted text pointer.
 * @param OutOwnsText Receives TRUE when caller must free `OutText`.
 * @return SCRIPT_OK on success, otherwise an error code.
 */
SCRIPT_ERROR ScriptValueToString(
    const SCRIPT_VALUE* Value,
    LPSCRIPT_CONTEXT Context,
    LPCSTR* OutText,
    BOOL* OutOwnsText) {
    LPSTR Buffer = NULL;

    if (Value == NULL || Context == NULL || OutText == NULL || OutOwnsText == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    *OutText = NULL;
    *OutOwnsText = FALSE;

    if (Value->Type == SCRIPT_VAR_STRING) {
        *OutText = Value->Value.String ? Value->Value.String : TEXT("");
        return SCRIPT_OK;
    }

    if (Value->Type == SCRIPT_VAR_INTEGER) {
        Buffer = (LPSTR)ScriptAlloc(Context, 32);
        if (Buffer == NULL) {
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        StringPrintFormat(Buffer, TEXT("%d"), Value->Value.Integer);
        *OutText = Buffer;
        *OutOwnsText = TRUE;
        return SCRIPT_OK;
    }

    if (Value->Type == SCRIPT_VAR_FLOAT) {
        Buffer = (LPSTR)ScriptAlloc(Context, 64);
        if (Buffer == NULL) {
            return SCRIPT_ERROR_OUT_OF_MEMORY;
        }

        StringPrintFormat(Buffer, TEXT("%f"), Value->Value.Float);
        *OutText = Buffer;
        *OutOwnsText = TRUE;
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_TYPE_MISMATCH;
}

/************************************************************************/

/**
 * @brief Concatenate two script values and store the result as text.
 * @param LeftValue Left operand (string-convertible)
 * @param RightValue Right operand (string-convertible)
 * @param Result Destination value
 * @return SCRIPT_OK on success, otherwise an error code
 */
SCRIPT_ERROR ScriptConcatStrings(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result) {
    LPCSTR LeftText = NULL;
    LPCSTR RightText = NULL;
    BOOL OwnsLeftText = FALSE;
    BOOL OwnsRightText = FALSE;
    LPSCRIPT_CONTEXT ResultContext = NULL;
    UINT LeftLength = 0;
    UINT RightLength = 0;
    UINT TotalLength = 0;
    LPSTR NewString = NULL;
    SCRIPT_ERROR Error = SCRIPT_OK;

    if (LeftValue == NULL || RightValue == NULL || Result == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    ResultContext = (LeftValue->ContextOwner != NULL)
        ? LeftValue->ContextOwner
        : RightValue->ContextOwner;
    if (ResultContext == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    Error = ScriptValueToString(LeftValue, ResultContext, &LeftText, &OwnsLeftText);
    if (Error != SCRIPT_OK) {
        return Error;
    }

    Error = ScriptValueToString(RightValue, ResultContext, &RightText, &OwnsRightText);
    if (Error != SCRIPT_OK) {
        if (OwnsLeftText) {
            ScriptFree(ResultContext, (LPVOID)LeftText);
        }
        return Error;
    }

    LeftLength = StringLength(LeftText);
    RightLength = StringLength(RightText);
    TotalLength = LeftLength + RightLength + 1;

    NewString = (LPSTR)ScriptAlloc(ResultContext, TotalLength);
    if (NewString == NULL) {
        if (OwnsLeftText) {
            ScriptFree(ResultContext, (LPVOID)LeftText);
        }
        if (OwnsRightText) {
            ScriptFree(ResultContext, (LPVOID)RightText);
        }
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    StringCopy(NewString, LeftText);
    StringConcat(NewString, RightText);

    Result->Type = SCRIPT_VAR_STRING;
    Result->Value.String = NewString;
    Result->ContextOwner = ResultContext;
    Result->OwnsValue = TRUE;

    if (OwnsLeftText) {
        ScriptFree(ResultContext, (LPVOID)LeftText);
    }
    if (OwnsRightText) {
        ScriptFree(ResultContext, (LPVOID)RightText);
    }

    return SCRIPT_OK;
}

/************************************************************************/

/**
 * @brief Remove all occurrences of a substring from a script string.
 * @param LeftValue Source string value
 * @param RightValue Substring value to remove
 * @param Result Destination value
 * @return SCRIPT_OK on success, otherwise an error code
 */
SCRIPT_ERROR ScriptRemoveStringOccurrences(const SCRIPT_VALUE* LeftValue, const SCRIPT_VALUE* RightValue, SCRIPT_VALUE* Result) {
    UINT SourceIndex;
    UINT WriteIndex;
    UINT SourceLength;
    UINT PatternLength;
    LPCSTR SourceText;
    LPCSTR PatternText;
    LPSTR NewString;

    if (LeftValue == NULL || RightValue == NULL || Result == NULL) {
        return SCRIPT_ERROR_SYNTAX;
    }

    if (LeftValue->Type != SCRIPT_VAR_STRING || RightValue->Type != SCRIPT_VAR_STRING) {
        return SCRIPT_ERROR_TYPE_MISMATCH;
    }

    SourceText = LeftValue->Value.String ? LeftValue->Value.String : TEXT("");
    PatternText = RightValue->Value.String ? RightValue->Value.String : TEXT("");

    SourceLength = StringLength(SourceText);
    PatternLength = StringLength(PatternText);

    LPSCRIPT_CONTEXT ResultContext = (LeftValue->ContextOwner != NULL) ? LeftValue->ContextOwner : RightValue->ContextOwner;
    NewString = (LPSTR)ScriptAlloc(ResultContext, SourceLength + 1);
    if (NewString == NULL) {
        return SCRIPT_ERROR_OUT_OF_MEMORY;
    }

    if (PatternLength == 0) {
        StringCopy(NewString, SourceText);
        Result->Type = SCRIPT_VAR_STRING;
        Result->Value.String = NewString;
        Result->ContextOwner = ResultContext;
        Result->OwnsValue = TRUE;
        return SCRIPT_OK;
    }

    SourceIndex = 0;
    WriteIndex = 0;

    while (SourceIndex < SourceLength) {
        if (SourceIndex + PatternLength <= SourceLength &&
            MemoryCompare(SourceText + SourceIndex, PatternText, PatternLength) == 0) {
            SourceIndex += PatternLength;
            continue;
        }

        NewString[WriteIndex++] = SourceText[SourceIndex++];
    }

    NewString[WriteIndex] = STR_NULL;

    Result->Type = SCRIPT_VAR_STRING;
    Result->Value.String = NewString;
    Result->ContextOwner = ResultContext;
    Result->OwnsValue = TRUE;

    return SCRIPT_OK;
}

/************************************************************************/
