# Syscall Return ABI Normalization

## Goal

Define one coherent syscall return contract to avoid caller-side mistakes and reduce ABI ambiguity.

## Problem Summary

The syscall layer mixes several return styles:

- `0/1` boolean success status
- value-or-zero semantics (for example: handle on success, `0` on failure)
- `DF_RETURN_*` status codes

This increases integration risk because callers must remember per-syscall conventions instead of using one predictable rule.

## Target Contract

Use one single family only:

1. Status syscalls:
- Return `DF_RETURN_*`
- Any produced value is written in an explicit output field of the user buffer structure
- No direct value return from syscall register

## Canonical Rule For New/Updated Syscalls

- Input/Output goes through one `*_INFO` structure with `ABI_HEADER`
- Syscall return is always `DF_RETURN_*`
- No semantic payload in the return register
- Caller checks:
  - `Result == DF_RETURN_SUCCESS`
  - then reads structured output fields

## High-Risk Mismatches Observed

- `SYSCALL_CreateProcess` returns boolean (`1`/`0`), not `DF_RETURN_*`.
- `SYSCALL_CreateTask` returns handle-or-zero.
- `SYSCALL_LoadModule` already follows `DF_RETURN_*` + output handle field (`MODULE_LOAD_INFO.Module`).

These mixed semantics are sufficient to create real caller bugs when code assumes one common convention.

## Proposed Standardized Signatures

### CreateProcess

Keep existing syscall id and structure. Normalize return to `DF_RETURN_*`.

- Input: `PROCESS_INFO`
- Output fields:
  - `Process`
  - `Task`
- Return:
  - `DF_RETURN_SUCCESS` on success
  - `DF_RETURN_BAD_PARAMETER` on invalid pointer/header/content
  - `DF_RETURN_GENERIC` on creation failure

### CreateTask

Normalize the existing syscall, no legacy variant.

- Syscall: `SYSCALL_CreateTask`
- Input/Output: `TASK_CREATE_INFO` (or extend `TASK_INFO` with `Task` output field)
- Return: `DF_RETURN_*`
- Output: created task handle in output structure

## Migration Strategy

## Phase 1: Define Policy And Helpers

- Add this normalization policy to kernel guide section for syscall ABI.
- Introduce runtime helpers:
  - `ExosIsSuccess(UINT Status)` for `DF_RETURN_SUCCESS`
  - thin wrappers for normalized syscalls

## Phase 2: Normalize Existing Writable Syscalls First

- Convert `SYSCALL_CreateProcess` kernel return to `DF_RETURN_*`.
- Update runtime wrapper call sites in system apps to check `DF_RETURN_SUCCESS`.
- Keep data outputs in `PROCESS_INFO` unchanged.

## Phase 3: Normalize All Remaining Syscalls

- Convert all direct-value syscalls (`handle/0`, `bool`) to `DF_RETURN_*` + explicit output fields.
- Update all runtime wrappers and userland callers in one convergent pass.
- Remove old return-style assumptions from tests and helpers.

## Phase 4: Hard Cutover And Cleanup

- Audit all userland checks that compare syscall return against ad-hoc constants.
- Remove dead compatibility paths and stale comments that mention mixed semantics.
- Reject any new direct-value syscall pattern in code review.

## Suggested Mapping Table

| Syscall | Current Return | Target Return | Output Value Carrier |
|---|---|---|---|
| `CreateProcess` | `1/0` | `DF_RETURN_*` | `PROCESS_INFO.Process`, `PROCESS_INFO.Task` |
| `CreateTask` | `handle/0` | `DF_RETURN_*` | `TASK_CREATE_INFO.Task` (or equivalent) |
| `LoadModule` | `DF_RETURN_*` | unchanged | `MODULE_LOAD_INFO.Module` |
| `GetModuleSymbol` | `DF_RETURN_*` | unchanged | `MODULE_SYMBOL_INFO.Address` |
| `Wait` | wait code (`WAIT_*`) | unchanged (domain-specific) | `WAIT_INFO.ExitCodes[]` |

## Rationale

- Fewer caller-side convention branches.
- Better static review signal: status handling becomes uniform.
- Easier cross-language bindings (C, scripting hosts, tooling).
- Lower probability of silent logic bugs from incorrect success checks.

## Non-Goals

- No redesign of domain-specific return domains (`WAIT_*`).
