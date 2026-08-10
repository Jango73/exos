# TTY Implementation Plan

## Current Kernel Hooks (Observed)

- Console output is a VGA text driver in `kernel/source/Console-Main.c` writing to 0xB8000 and exported through `ConsolePrint`, `ConsolePrintLine`, `SetConsoleCursorPosition`, and syscalls (`SYSCALL_Console*` in `kernel/source/SYSCall.c`).
- Keyboard input is handled by `kernel/source/drivers/Keyboard-PS2.c` which translates scancodes to `KEY_CODE` and routes to:
  - `EnqueueInputMessage` (focused window/process message queue in `kernel/source/process/TaskMessaging.c`), or
  - the legacy keyboard buffer (`Keyboard.Buffer`) when no message queue is available.
- Userland uses `runtime/include/exos.h` `Console*` functions that map to the console syscalls.
- `PROCESS_INFO` exposes `StdIn/StdOut/StdErr` but the `PROCESS` struct does not store them and the console path ignores them.
- `SystemFS` is the root pseudo filesystem (`kernel/source/fs/SystemFS.c`), but there are no TTY nodes yet.
- Configuration is provided via TOML (see `kernel/configuration/exos.ref.toml`) and is used for runtime paths.

## Goals

- Introduce a kernel TTY subsystem that owns line discipline, echo, and buffering.
- Keep compatibility with existing `Console*` syscalls and userland apps while migrating to TTY-backed I/O.
- Expose TTYs as file-like objects via SystemFS so standard read/write can target them.
- TTY root path is defined by configuration (default `/system/dev` in `exos.ref.toml`).
- Support at least one active TTY (TTY0) and allow more virtual terminals later.
- Bind exactly one TTY per user session (1:1).

## Proposed Architecture

- **TTY core**: a kernel object with `LISTNODE_FIELDS`, I/O buffers, line discipline flags, cursor state, and a link to a console device.
- **Console device**: a minimal backend for output (VGA text initially).
- **Input routing**: keyboard driver feeds the active/controlling TTY; fall back to the current message queue behavior when a TTY is not bound.
- **Userland access**: TTY nodes under the configured SystemFS path (default `/system/dev/tty0`) support read/write; process stdio handles point to TTY nodes.
- **Sessions**: each user session owns exactly one TTY.
- **Process inheritance**: a process does not own a TTY directly; it inherits the session pointer from `OwnerProcess`, and the session owns the TTY.

## [ ] Step 1 - Define TTY Structures and Core API

- Create `kernel/include/TTY.h` and `kernel/source/TTY.c`.
- Define `TTY` structure:
  - `LISTNODE_FIELDS`, `MUTEX`, `UINT Id`, `UINT Rows`, `UINT Cols`
  - cursor position, current attributes (fore/back), and flags (echo, canonical, crlf)
  - ring buffers for input (raw + cooked) and output
  - pointer to console device (initially VGA text)
- Provide core API:
  - `CreateTTY`, `DeleteTTY`, `TTYWrite`, `TTYRead`, `TTYPushKey`, `TTYFlush`
  - Keep all types from `Base.h` and use `SAFE_USE*` macros for pointers.

## [ ] Step 2 - Console Backend Adapter

- Wrap the existing VGA text console (`kernel/source/Console-Main.c`) behind a small interface:
  - `TTYConsolePutChar`, `TTYConsolePutRun`, `TTYConsoleSetCursor`, `TTYConsoleClear`.
- Create `TTY0` during boot and bind it to the VGA text backend.
- For compatibility, map `ConsolePrint*` and `ConsoleBlitBuffer` to TTY0 output internally.

## [ ] Step 3 - Input Routing Integration

- Add a `ControllingTTY` field to `PROCESS` (or to `TASK`) to indicate where console input should go.
- Update keyboard routing:
  - If a focused process has `ControllingTTY`, send keys to `TTYPushKey`.
  - Otherwise keep `EnqueueInputMessage` and the legacy keyboard buffer path.
- Map `ConsoleGetKey`, `ConsolePeekKey`, and `ConsoleGetKeyModifiers` to the controlling TTY when present.
- Ensure the session's TTY is used as the default controlling TTY for its processes.

## Session/TTY Coupling (1:1)

- Each `USER_SESSION` owns exactly one TTY (single source of truth).
- Processes inherit the session pointer from `OwnerProcess` in `NewProcess`.
- Input routing should resolve `Process->Session->TTY` (not a per-process TTY field).
- StdIO defaulting should also resolve `Process->Session->TTY` to avoid duplicates.

## [ ] Step 4 - Device Nodes for TTY

- Add TTY nodes into SystemFS under the configured TTY root path (`/system/dev` by default).
- Implement a TTY device driver with `DF_FS_READ`/`DF_FS_WRITE`:
  - `read` consumes cooked input (or raw if configured).
  - `write` goes through `TTYWrite` and updates the screen.
- Create nodes for `tty0`, `tty1`, etc. (even if only `tty0` is functional at first).

## [ ] Step 5 - Process StdIO Plumbing

- Extend `PROCESS` to store `StdIn`, `StdOut`, `StdErr` handles (matching `PROCESS_INFO`).
- On process creation:
  - If `StdIn/Out/Err` not provided, default to `tty0` or the caller's controlling TTY.
  - Bind `ControllingTTY` accordingly.
- Update shell and runtime helpers to use stdio handles when available.

## [ ] Step 6 - Virtual Terminal Switching

- Implement global hotkeys (e.g., Ctrl+Alt+F1..F4) in the keyboard driver.
- Switching updates the active TTY bound to the VGA console and changes the controlling TTY for the focused process (or kernel console).
- Preserve existing window focus and message queue behavior.

## [ ] Step 7 - Testing and Validation

- Boot and ensure TTY0 shows kernel banner and shell output.
- Validate input editing (backspace, enter, tab) through TTY.
- Verify userland apps can read/write the TTY device via file APIs.
- Confirm no regressions in message queue input for GUI windows.

## Open Questions

- Should the first TTY backend reuse `kernel/for-later/Console.*` or keep a minimal VGA adapter?
- How should userland change TTY behavior (explicit syscalls, per-process flags, or fixed defaults)?
