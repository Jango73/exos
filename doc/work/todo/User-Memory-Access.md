# Memory Manager Plan - Robust Recoverable User Memory Access

## Goal
Build a robust memory-management path where kernel code can safely access user memory from any execution context (including DeferredWork and SMP) without relying on the current CR3, while preserving process isolation.

## Problem Statement
The current kernel sometimes dereferences user virtual addresses directly from kernel contexts that are not executing with the target process page directory. In those cases, ring 0 execution still page-faults because the virtual mapping does not exist in the active address space. A robust kernel must treat this as a controlled recoverable path, not as undefined behavior.

## Design Principles
- No direct user-pointer dereference in generic kernel code.
- All user-memory accesses go through one explicit subsystem.
- Recoverable page faults are only allowed inside explicit access scopes.
- Fault resolution must be tied to an explicit target process ownership.
- Outside that scope, kernel faults keep strict existing fatal policy.

## Proposed Subsystem
Name proposal: `UserMemoryAccess` (avoid Linux-specific wording).

### Public API
- `BOOL UserMemoryBegin(LPUSER_MEMORY_ACCESS Access, LPPROCESS Process, U32 Flags);`
- `void UserMemoryEnd(LPUSER_MEMORY_ACCESS Access);`
- `BOOL ProbeUserRange(LPUSER_MEMORY_ACCESS Access, LINEAR UserAddress, UINT Size, U32 Permissions);`
- `BOOL CopyFromUser(LPUSER_MEMORY_ACCESS Access, LPVOID KernelDestination, LINEAR UserSource, UINT Size);`
- `BOOL CopyToUser(LPUSER_MEMORY_ACCESS Access, LINEAR UserDestination, LPCVOID KernelSource, UINT Size);`

### Access Context
`USER_MEMORY_ACCESS` must carry:
- Target process pointer.
- Read/write intent.
- Recoverable-fault enabled marker.
- Optional diagnostics (fault count, last-fault address, last-status).

Task-local active-access context is required so the page-fault handler can know whether a kernel fault is expected and recoverable.

## Fault-Handler Integration
When a page fault occurs in ring 0:
1. Check if a `UserMemoryAccess` context is active for current task.
2. If no active context: keep current fatal behavior.
3. If active context exists:
   - Validate faulting address belongs to allowed requested user range.
   - Resolve page through VM/VMA policy for target process.
   - Validate permissions (read/write/user, NX policy as relevant).
   - If resolved: resume instruction.
   - If not resolvable: mark access as failed and return clean error to caller path.

Important: recovery must never map pages into the wrong process context.

## CR3 Strategy
Keep DeferredWork and generic kernel tasks CR3-agnostic by policy:
- Kernel code does not assume active CR3 can resolve target user VA.
- `UserMemoryAccess` internals may perform temporary controlled page-directory switch for bounded copy/probe operations when needed.
- Switch/restore must be encapsulated and never leaked to callers.

## VM Requirements
The VM layer must expose explicit helpers used by `UserMemoryAccess`:
- Resolve VMA for `(Process, Address)`.
- Commit/fault-in page for that process if legal.
- Permission check API independent of ad-hoc page-table reads.
- Return deterministic status codes (`OK`, `NOT_MAPPED`, `PERMISSION_DENIED`, `INVALID_RANGE`, etc.).

## Concurrency and SMP Requirements
- Per-task access context (not global) to be CPU-safe.
- Strict lock ordering between fault path, process VM metadata, and page-table updates.
- Avoid holding unrelated subsystem locks across copy loops.
- TLB invalidation correctness for mappings changed during resolution.

## Integration Plan (Phased)
1. Add `UserMemoryAccess` API and task-local context plumbing.
2. Wire kernel page-fault handler to recover only inside active access context.
3. Implement `ProbeUserRange` and small-size copy primitives.
4. Convert high-risk callsites first:
   - Task/Process message queue paths touching process-owned memory.
   - Syscall argument buffer reads/writes.
5. Convert remaining direct user dereferences in kernel.
6. Add guardrail checks/macros to block new direct user-pointer dereference in reviews.

## Tests
### Unit / Component
- Copy from mapped user page succeeds.
- Copy to mapped writable user page succeeds.
- Read-only violation rejected.
- Unmapped but valid VMA page faults in and succeeds.
- Invalid range fails without kernel crash.

### Runtime / Stress
- DeferredWork access to user buffers with unrelated active CR3.
- Multi-core concurrent copy/fault-in on different processes.
- Fault storm throttling and diagnostics remain stable.

## Observability
Add focused logs/counters:
- Recoverable kernel faults count.
- Fault-recovery success/failure reasons.
- Per-process user-access failure counters.
- Optional debug tag filter integration for targeted tracing.

## Non-Goals
- No global implicit policy "kernel faults may map anything".
- No silent recovery outside explicit `UserMemoryAccess` scope.

## Expected Outcome
A robust MM path where recoverable faults are intentional, bounded, process-owned, and safe under SMP/DeferredWork, while preserving strict failure behavior for unexpected kernel faults.
