# E0 Script Language — Developer Manual

> Version: Phase 1 — Core syntax, variables, control flow  
> For EXOS Shell — Lightweight Embedded Command Language

E0 is the internal **shell scripting language** of the EXOS kernel.
It is designed for automation in EXOS shell.
Its syntax is intentionally minimal — inspired by C and early JS — and can be embedded or extended through host callbacks.

---

## 1) Lexical elements

### Identifiers
- Letters, digits, and `_`, starting with a letter or `_`.
- Case-sensitive.

### Literals
- **Number**: floating-point; integers are a subset (e.g., `0`, `42`, `3.14`).
- **String**: single `'...'` or double quotes `"..."` with escapes: `\n`, `\r`, `\t`, `\\`, `\'`, `\"`.
- **Path**: a token starting with `/` and not immediately followed by whitespace or `//`. Treated as a shell command token.

### Tokens / Operators
- Arithmetic: `+`, `-`, `*`, `/`
- Bitwise: `&`, `|`, `^`, `~`, `<<`, `>>`
- Logical: `!`, `&&`, `||`
- Comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- Indexing: `[` `]`
- Grouping: `(` `)`, block `{` `}`
- Assignment: `=`
- Member access: `.`
- Statement terminator: `;`

### Keywords
- `if`, `else`, `for`, `return`, `continue`

> **Comments:** not implemented yet. A `/` at start of a line may be parsed as a shell command.

---

## 2) Types & values

Runtime types:
- **float** (default numeric)
- **int** (tracked when integer-valued)
- **string**
- **array** (sparse, auto-grown)
- **object** (dynamic name/value container, reference semantics)
- **host-handle** (opaque reference provided by the host)

Numeric coercion:
- Arithmetic uses float unless both operands are int.
- `0` is **false**, nonzero is **true**.
- Comparisons yield `1.0` or `0.0`.
- Logical operators return integer truth values (`1` or `0`).
- Bitwise and shift operators require integer operands.

Strings are immutable. Arrays can contain mixed values. Objects can contain mixed values and nested objects.

String operators:
- `+`: text concatenation when either operand is a string
- `string - string`: removes all occurrences of the right string from the left string (`"foobarfoo" - "foo"` gives `"bar"`)

Truthiness constraints:
- `if`, `for`, `!`, `&&`, and `||` require numeric-compatible operands.
- Strings, arrays, objects, and host handles are not valid truth-test operands unless the host exposes numeric properties first.

---

## 3) Variables and assignment

- `x = expression;`
- `array[index] = expression;`
- `obj.property = expression;`
- `root.child.leaf = expression;`
- Variables auto-declare in current scope.
- Lookups search upward through scopes.
- Host-registered symbols cannot be reassigned.

Semicolons:
- Required after assignments.
- Required after `return` and `continue`.
- Optional after blocks, control flow, and expression statements.

```text
x = 1 + 2*3;
y = (x > 5);
msg = "hi\nthere";
```

Blocks `{ ... }` create a new scope. Assigning an existing name updates the parent.

---

## 4) Arrays

- Arrays are created on first indexed assignment.
- Index must be numeric.
- Reading an unset element triggers an error.

```text
a[0] = "zero";
a[1] = 10;
i = 2;
a[i] = a[1] * 3;
val = a[0];
```

---

## 5) Objects

- `{}` creates an empty native E0 object.
- Properties are created on first assignment.
- Property reads require the property to exist.
- Property writes require every intermediate value in the dotted path to already be an object.
- Objects use reference semantics: assigning one object to another variable copies the reference, not the whole content.

```text
user = {};
user.name = "alice";
user.settings = {};
user.settings.theme = "light";
alias = user;
alias.name = "shared";
```

Reading `user.missing` raises `UNDEFINED_VAR`.

Writing through a non-object intermediate raises `TYPE_MISMATCH`:

```text
user = {};
user.name = 7;
user.name.value = 1;
```

---

## 6) Expressions

Operator precedence (high → low):
1. Parentheses
2. Index/property: `expr[index]`, `expr.property`
3. Unary `+`, `-`, `!`, `~`
4. `*`, `/`
5. `+`, `-`
6. Shifts: `<<`, `>>`
7. Bitwise AND: `&`
8. Bitwise XOR: `^`
9. Bitwise OR: `|`
10. Comparisons
11. Logical AND: `&&`
12. Logical OR: `||`

Left-associative.

```text
x = 1 + 2 * 3;
ok = (x >= 7) == 1;
z = (10 / 4);
name = "foo" + "bar";
label = 1 + "x";
mixed = 1 + 2 + "x" + 3;
trimmed = "foobarfoo" - "foo";
mask = (flags & 128) != 0;
combined = (1 << 4) | 3;
inverted = ~maskBits;
ready = (count > 0) && (enabled != 0);
```

Bitwise and shift rules:
- `&`, `|`, `^`, `~`, `<<`, and `>>` operate on integers only.
- Shift counts must be integers in the native `INT` bit range.
- Violations raise `TYPE_MISMATCH`.

---

## 7) Host interop: functions, properties, and commands

### Function calls
Syntax: `name(expr1, expr2, ...)`
- Zero or more arguments are supported.
- Each argument is evaluated, then stringified before calling the host.

```text
echo("hello");
ping(123);
copy("/system/config.toml", "/data/config.toml", 1);
kill(task[0].handle);
```

### Property access
`base.property` resolves in two branches:
- native E0 objects use their internal dynamic property table
- host handles resolve through the host descriptor callbacks

```text
user.name;
disk.size;
```

### Shell command statements
At statement start:
- Quoted string → command  
- Path starting with `/` → command  
- Bare identifier without `=` or `(` → command

```text
"/usr/bin/echo hello world"
/usr/bin/echo hello
echo hello
```

---

## 8) Control flow

### If / Else
```text
if (x > 0) {
    pos = 1;
} else {
    pos = 0;
}
```

### For loop
```text
for (i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        continue;
    }
    sum = sum + i;
}
```
Capped at 1000 iterations.

`continue;` is valid only inside a loop body. It skips the rest of the current iteration and proceeds with the loop increment.

### Return
```text
return 42;
return "done";
```

`return;` without an expression is not supported. The returned value is stored in the script context and stops script execution immediately.

---

## 9) Errors

- `SYNTAX_ERROR`
- `UNMATCHED_BRACE`
- `OUT_OF_MEMORY`
- `TYPE_MISMATCH`
- `DIVISION_BY_ZERO`
- `UNDEFINED_VAR`
- `UNAUTHORIZED`

Common cases:
- reading an unknown variable, array element, object property, or host property: `UNDEFINED_VAR`
- applying arithmetic, logical, bitwise, property, or indexing operations to an unsupported type: `TYPE_MISMATCH`
- dividing by zero: `DIVISION_BY_ZERO`
- malformed syntax or unsupported expression form: `SYNTAX_ERROR`

API:
```c
ScriptGetLastError(ctx);
ScriptGetErrorMessage(ctx);
```

---

## 10) Embedding API (C/C++)

### Context
```c
LPSCRIPT_CONTEXT ScriptCreateContext(const SCRIPT_CALLBACKS* cb);
void ScriptDestroyContext(LPSCRIPT_CONTEXT ctx);
```

### Callbacks
```c
typedef struct SCRIPT_CALLBACKS {
    U32 (*ExecuteCommand)(LPCSTR cmdline, void* user);
    INT (*CallFunction)(LPCSTR name, UINT argc, LPCSTR* argv, void* user);
    void* UserData;
} SCRIPT_CALLBACKS;
```

Reserved function return values:
- `SCRIPT_FUNCTION_STATUS_UNKNOWN`: host symbol not found
- `SCRIPT_FUNCTION_STATUS_ERROR`: host symbol found, but the callback rejected the call and set an explicit script error

Shell host functions exposed in the kernel integration include:
- `print(...)`
- `exec(...)`
- `kill(handle)`
- `smokeTestMultiArgs(a, b, c, d)` for automated smoke validation of multi-argument host calls
- `setGraphicsDriver(driverAlias, width, height, bpp)`

### Variables
```c
ScriptSetVariable(...);
ScriptGetVariable(...);
ScriptPushScope(...);
ScriptPopScope(...);
```

### Arrays
```c
ScriptSetArrayElement(...);
ScriptGetArrayElement(...);
```

### Objects
```c
ScriptCreateObject(...);
ScriptSetObjectProperty(...);
ScriptGetObjectProperty(...);
```

### Host symbols
```c
ScriptRegisterHostSymbol(...);
ScriptUnregisterHostSymbol(...);
```

---

## 11) Example embedding

```c
static U32 Exec(const char* cmd, void* u) {
    return 0;
}
static INT Call(const char* name, UINT argc, const char** argv, void* u) {
    return 0;
}

void run(const char* code) {
    SCRIPT_CALLBACKS cb = { Exec, Call, NULL };
    LPSCRIPT_CONTEXT ctx = ScriptCreateContext(&cb);
    if (ScriptExecute(ctx, code))
        printf("%s\n", ScriptGetErrorMessage(ctx));
    ScriptDestroyContext(ctx);
}
```

---

## 11) Grammar (simplified)

```
program    := { statement [ ';' ] }
statement  := assignment ';'
           | returnStmt ';'
           | continueStmt ';'
           | ifStmt
           | forStmt
           | block
           | exprStmt
block      := '{' { statement [ ';' ] } '}'

assignment := IDENT '=' expr
           | IDENT '[' expr ']' '=' expr
           | IDENT { '.' IDENT } '=' expr
ifStmt     := 'if' '(' expr ')' statement [ 'else' statement ]
forStmt    := 'for' '(' assignment ';' expr ';' assignment ')' statement
returnStmt := 'return' expr
continueStmt := 'continue'
exprStmt   := expression
expression := logicalOr
```

---

## 12) File extension

- Scripts use the `.e0` extension.  
  Example: `startup.e0`, `mount.e0`, `nettest.e0`

---

## 13) Example scripts

```text
// System setup (no comment support yet, pseudo-comment)
echo("Booting EXOS...");
/bin/init --fast

if (mem.free < 100) {
    echo("Warning: low memory");
}

for (i = 0; i < 5; i = i + 1) {
    echo("Tick " + i);
}

flags = 144;
if ((flags & 128) != 0) {
    print("input endpoint");
}

return flags >> 4;
```

---

## 14) Philosophy

E0 is not a general-purpose language.  
It’s a **shell-oriented control language** — small, embeddable, deterministic — meant for task automation and quick scripting inside EXOS subsystems.
