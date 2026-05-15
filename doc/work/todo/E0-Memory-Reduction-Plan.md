# E0 Memory Reduction Plan

## Goal

Reduce E0 memory usage drastically during parse and execution, with priority on x86-32 shell usage.

The target is not a cosmetic improvement. The objective is to make typical shell scripts cheap enough that they do not pressure the reserved shell heap and do not require heap growth for normal inspection and automation scenarios.

## Why E0 is expensive today

The current design favors implementation simplicity over memory density.

Main cost sources identified from the code:

- `AST_NODE` is large and duplicated in high volume.
- Expression and assignment nodes embed fixed-size inline text buffers (`MAX_TOKEN_LENGTH`, `MAX_VAR_NAME`) even when most nodes do not need them.
- Shell command statements copy the whole command line into AST-owned memory.
- Each block owns a dynamically grown pointer array for its statements.
- String evaluation duplicates strings frequently.
- Function-call argument building duplicates stringified arguments again into stable copies.
- Array reads allocate a temporary `SCRIPT_VARIABLE`.
- The interpreter always builds a full AST before execution.
- The shell keeps one persistent script context, so some memory remains live across commands.

## Expected impact areas

The first wins are structural, not algorithmic.

The highest-impact reductions should come from:

1. shrinking AST node size
2. removing unnecessary string duplication
3. removing temporary variable allocations
4. avoiding full-script AST materialization when not needed

## Phase 1: Measure precisely

Before refactoring, add hard numbers in the engine itself.

- Add optional internal counters for:
  - number of AST nodes by kind
  - total bytes allocated for AST nodes
  - total bytes allocated for block statement vectors
  - total bytes allocated for shell command copies
  - total bytes allocated for temporary strings
  - total bytes allocated for function-call argument vectors
  - total bytes allocated for temporary array-read variables
- Expose one debug dump path for script memory statistics after `ScriptExecute()`.
- Validate on:
  - `system/scripts/test.e0`
  - `system/scripts/test-objects.e0`
  - `system/scripts/test-exposed-objects.e0`

This phase must produce one baseline table per script and per architecture.

## Phase 2: Shrink AST nodes aggressively

This is the highest priority refactor.

### 2.1 Remove inline text buffers from generic nodes

Today many node variants carry fixed buffers even when unused.

Plan:

- Replace generic inline `Value[MAX_TOKEN_LENGTH]` storage with compact per-kind payload.
- Keep numeric literals as numeric fields only.
- Keep identifiers and property names as external shared strings or interned strings.
- Keep operator nodes as one small enum or one byte token kind, not a full text buffer.

Expected result:

- large reduction in per-node size
- especially strong win on expression-heavy scripts

### 2.2 Split expression node kinds

The current expression node is too generic.

Plan:

- Replace the single large expression payload with specialized node layouts:
  - literal number
  - literal string
  - identifier
  - binary operator
  - unary operator
  - function call
  - array access
  - property access
  - shell command
- Store only the fields each layout needs.

Expected result:

- smaller average node
- less dead storage per parsed token

### 2.3 Stop storing token text when semantic form is enough

Plan:

- For operators and comparisons, store enum values instead of text like `"=="`, `"&&"`, `"<<"`.
- For `{}` empty object literal, store only a literal kind, no string text.

## Phase 3: Deduplicate strings

### 3.1 Add a script-local string interning table

Many identifiers and property names repeat heavily.

Plan:

- Introduce a string pool owned by `SCRIPT_CONTEXT`.
- Intern:
  - variable names
  - property names
  - function names
  - command names
- AST nodes then store pointers to interned strings instead of private inline copies.

Expected result:

- strong reduction for repeated names in long scripts
- simpler equality checks by pointer in some paths

### 3.2 Stop copying shell command source into AST nodes

Today shell command statements allocate a private command-line buffer.

Plan:

- Store source offsets `(start, length)` into the original script text instead of copying command text during parse.
- Materialize a temporary null-terminated buffer only if execution strictly requires it.
- Prefer direct span-based execution APIs if possible.

Expected result:

- large win for command-heavy scripts

### 3.3 Reduce stringification churn

Plan:

- Review `ScriptValueToString`, `ScriptConcatStrings`, `ScriptBuildFunctionArguments`.
- Avoid converting numeric values to temporary strings unless the caller strictly needs text.
- Remove the extra “stable copy” pass in function arguments when the source string lifetime is already valid for the callback duration.

## Phase 4: Remove wasteful temporary allocations

### 4.1 Eliminate temporary `SCRIPT_VARIABLE` on array reads

Current path:

- `ScriptGetArrayElement()` allocates a temporary `SCRIPT_VARIABLE`
- caller reads it
- caller frees it

Plan:

- Replace that path with direct typed output:
  - `SCRIPT_ERROR ScriptGetArrayElementValue(..., SCRIPT_VAR_TYPE* Type, SCRIPT_VAR_VALUE* Value)`
- return borrowed or retained values directly, matching the ownership rules already used elsewhere.

Expected result:

- removes many small allocations in array-heavy scripts

### 4.2 Avoid transient allocation for simple numeric/string temporaries

Plan:

- Review all interpreter paths that allocate tiny heap objects only to free them immediately after one expression step.
- Prefer stack-local `SCRIPT_VALUE` and borrowed references whenever ownership does not escape the frame.

## Phase 5: Make arrays and objects denser

### 5.1 Rework array storage

Current array design stores:

- `LPVOID* Elements`
- `SCRIPT_VAR_TYPE* ElementTypes`
- boxed integers/floats as separate heap allocations

This is costly.

Plan:

- Replace boxed numeric elements with one inline `SCRIPT_VALUE` array.
- Store element type and payload together.
- Keep string/object ownership rules explicit.

Expected result:

- fewer allocations
- better locality
- smaller overhead per element

### 5.2 Rework object property storage

Current object properties store:

- `Name[MAX_TOKEN_LENGTH]`
- full `SCRIPT_VALUE`

Plan:

- Store interned property-name pointer instead of fixed inline `Name`.
- Consider a compact property entry with:
  - interned name pointer
  - value payload
- Keep linear scan first if property counts stay low; density matters more than sophistication here.

## Phase 6: Stop building the whole AST for everything

This is the biggest architectural win, but it is more invasive.

### 6.1 Introduce direct execution for simple statements

Plan:

- Parse and execute simple statement forms immediately when safe:
  - assignment
  - function call statement
  - shell command statement
  - return
- Keep full AST only for control flow bodies and expressions that need structure.

Expected result:

- lower peak memory
- smaller parse-time footprint

### 6.2 Evaluate whether E0 should become single-pass

Longer-term option:

- replace the current “parse full AST, then execute” model with a single-pass or hybrid parser/interpreter
- keep recursive descent parsing, but execute as syntax is recognized

This should only be done after Phase 2 to Phase 5. A smaller AST may already be sufficient.

## Phase 7: Control shell-context lifetime better

The shell keeps one persistent script context by design.

Plan:

- decide what must remain persistent and what can be reset between scripts
- separate:
  - user variables that are intentionally persistent
  - parse/evaluation scratch state that should never survive a command
- verify that temporary memory always returns to the shell reserved heap freelists quickly

This does not reduce gross allocation volume by itself, but it helps make retained memory predictable.

## Recommended execution order

Implement in this order:

1. measurement and counters
2. remove temporary array-read variable allocation
3. remove shell command text copies
4. remove stable function-argument copy pass where not required
5. shrink AST nodes and split node layouts
6. intern strings
7. inline array storage with `SCRIPT_VALUE`
8. compact object property entries
9. hybrid or single-pass execution if still needed

## Validation criteria

Each phase must be validated on the same script set and report before/after numbers.

Success criteria:

- `test-exposed-objects.e0` peak memory reduced by at least 50% on x86-32
- no shell heap growth for normal inspection scripts
- no semantic regression in:
  - string concatenation
  - string subtraction
  - function calls
  - property access
  - array access
  - `if`
  - `for`
  - `return`
  - `continue`
- no change in shell-visible behavior for embedded command statements

## Open design rules

The refactor must preserve these principles:

- no bidirectional coupling
- no dependency on external libraries
- ownership rules must stay explicit
- no hidden heap allocations in hot paths unless justified
- parsing and evaluation code should become simpler, not more magical

## Immediate first task

Start with the smallest high-value change:

- replace `ScriptGetArrayElement()` temporary variable allocation with direct typed output

This is low-risk, local, and removes pure waste.
