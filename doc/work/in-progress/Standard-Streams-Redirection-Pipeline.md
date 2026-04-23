# Standard Streams, Redirection and Pipeline Plan

## Goal
Implement process-level `stdin`, `stdout`, and `stderr` in kernel/runtime with shell redirection and pipeline support.

## Scope and priorities
1. Add kernel-level process standard stream ownership and inheritance.
2. Add reusable kernel pipe primitive and syscall for pipe creation.
3. Route syscall read/write through file or pipe objects.
4. Initialize runtime standard streams from process metadata (`fd 0/1/2`).
5. Extend shell execution to support redirection and pipelines.
6. Add validation scenarios and keep ABI compatibility.

## Design constraints
- Keep dependencies unidirectional.
- Reuse shared helpers instead of local one-off logic.
- Preserve existing file APIs and behavior.
- Keep compatibility for existing apps using `OpenFile/ReadFile/WriteFile`.

## Implementation phases

### Phase 1: Handle/object lifecycle safety for shared stream objects
- Introduce last-handle semantics in close path:
  - releasing one handle must not destroy object when other handles still reference the same pointer.
- Add explicit handle duplication helper for child process stream inheritance.

### Phase 2: Process standard stream state in kernel
- Extend `PROCESS` with effective standard stream handles.
- In `CreateProcess`:
  - use `PROCESS_INFO.Std*` when provided,
  - otherwise inherit from parent,
  - duplicate inherited/provided handles for child ownership.
- In process teardown, close child-owned standard handles.
- In `GetProcessInfo`, return effective `StdIn/StdOut/StdErr` handles.

### Phase 3: Reusable pipe module and syscall
- Add generic pipe module under `kernel/include/utils` + `kernel/source/utils`.
- Add syscall and ABI struct for creating pipe endpoint handles.
- Pipe behavior:
  - bounded ring buffer,
  - blocking-like behavior by short waits in kernel loop,
  - EOF when write side is closed,
  - write failure when read side is closed.

### Phase 4: Read/write syscall integration
- Extend `SysCall_ReadFile` and `SysCall_WriteFile` to accept:
  - file object handles,
  - pipe endpoint handles.
- Keep existing file flow unchanged.

### Phase 5: Runtime stdio integration
- Expose and initialize real `stdin`, `stdout`, `stderr` streams from process info.
- Map `open/read/write/close/fdopen/fclose` correctly for descriptors `0/1/2`.
- Remove temporary TCC overrides that set `stdin/stdout/stderr` to `NULL`.

### Phase 6: Shell redirection and pipeline
- Add parser for:
  - `cmd > file`, `cmd >> file`, `cmd < file`,
  - `cmd1 | cmd2 | ...`,
  - `2> file`, `2>> file`, `2>&1`.
- Build process chain with per-command `PROCESS_INFO.Std*` handles.
- Close parent copies of pipe ends after process creation.

## Validation plan
- `echo hello | cat`
- `dir | find "kernel"`
- `run app > out.txt`
- `run app < in.txt`
- `run app 2> err.txt`
- `run app > out.txt 2>&1`
- Verify EOF behavior when producer exits.
- Verify both x86-32 and x86-64 smoke tests.

## Deliverable status tracking
- [x] Phase 1
- [x] Phase 2
- [x] Phase 3
- [x] Phase 4
- [x] Phase 5
- [x] Phase 6
- [ ] Validation
