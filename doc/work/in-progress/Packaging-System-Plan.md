# Packaging System Implementation Roadmap

## Prerequisites (one-time)

- [x] **Read-only mount layer**: stable virtual filesystem mount/unmount flow for package volumes.
- [x] **Compression primitives**: zlib inflate path available in kernel for on-demand block decompression.
- [x] **Hashing primitives**: SHA-256 helper for package, file, and block validation.
- [x] **Signature primitives**: optional detached signature verification path.

## Step 1 --- Freeze `.epk` binary format

**Goal**: define a strict, versioned package format that can be validated at mount time.

- [x] Finalize fixed header fields (`Magic`, `Version`, offsets, sizes, flags).
- [x] Finalize table of contents entry layout (file, folder, folder alias, metadata).
- [x] Finalize block table layout for chunked compressed content.
- [x] Finalize manifest blob embedding and optional signature block.
- [x] Define compatibility policy for unknown flags and future versions.

**Success**: one specification file and one parser contract produce deterministic validation results.

## Step 2 --- Build package image tooling

**Goal**: generate valid `.epk` archives from input folders.

- [x] Implement host-side packaging tools in `tools/` (Linux first, cross-oriented workflow).
- [x] Add a pack command that walks input folders and writes deterministic table of contents ordering.
- [x] Add chunking and zlib compression with configurable chunk size.
- [x] Compute file hashes, block hashes, and package hash.
- [x] Embed `manifest.toml` as a dedicated section.
- [x] Add optional signature generation hook.

**Success**: repeated packaging of identical input produces byte-identical `.epk` output.

## Step 3 --- Kernel parser and validator

**Goal**: safely open and verify package metadata without exposing file content yet.

- [x] Implement header parsing and structural bounds checks.
- [x] Implement package hash validation.
- [x] Implement optional signature verification flow.
- [x] Parse table of contents and block table into kernel structures.
- [x] Reject malformed package files with explicit error logs.

**Success**: kernel can classify one `.epk` as valid or invalid deterministically.

## Step 4 --- `packagefs` readonly mount

**Goal**: mount one valid package as a virtual readonly filesystem tree.

- [x] Create package filesystem driver module and mount entry points.
- [x] Expose files, folders, and folder aliases from table of contents.
- [x] Enforce readonly semantics for every operation.
- [x] Add unmount cleanup path with object lifetime safety.

**Success**: browsing a mounted package shows expected tree and forbids writes.

## Step 5 --- On-demand block decompression

**Goal**: stream file bytes from compressed blocks without extracting package content in storage.

- [x] Map file reads to block table ranges.
- [x] Decompress required chunks only, with bounds and hash validation.
- [x] Add reusable cache module for decompressed package chunks in `kernel/include/utils` + `kernel/source/utils`.
- [x] Integrate cache eviction and invalidation on unmount.

**Success**: executables and libraries load directly from mounted packages with no temporary files.

## Step 6 --- Per-process namespace integration

**Goal**: bind package views only for the launched process.

- [x] Keep `/package` as a private process mount.
- [x] Map `/user-data` to `/current-user/<package-name>/data`.
- [x] Ensure process teardown unmounts or releases package view cleanly.
- [x] Keep package launch path independent from global package scans.

**Success**: packaged process sees private mount aliases without requiring global package activation.

## Step 7 --- Manifest compatibility checks (no dependencies)

**Goal**: validate launch compatibility without dependency resolution.

- [x] Parse `manifest.toml` model (`name`, `version`, `arch`, `kernel_api`, `entry`, optional `[commands]`).
- [x] Validate architecture compatibility.
- [x] Validate kernel API compatibility policy.
- [x] Enforce policy: no `provides/requires` dependency graph behavior.
- [x] Produce deterministic diagnostics for compatibility failures.

**Success**: incompatible package launch is rejected with explicit reasons.

## Step 8 --- Launch-time package activation flow

**Goal**: execute packaged applications through one simple pipeline.

- [x] Implement pipeline: read `.epk` -> validate -> private mount -> launch entry.
- [x] Avoid persistent global activation sets (`active/staged/rollback`).
- [x] Keep failure behavior fail-fast with no partial mounted leftovers.
- [x] Ensure explicit unmount/release on launch failure and process exit.
- [x] Support `package run <package-name> [command-name]` where omitted command falls back to `entry`.

**Success**: package launch is deterministic and stateless at system level.

## Step 9 --- Userland package manager commands

**Goal**: expose basic package operations to users and automation.

- [ ] Keep `tools/` as the primary package creation path while native EXOS package management matures.
- [x] Implement `package list <package-name|path.epk>` command (lists package internal content).
- [x] Implement `package add <package-name|path.epk>` command.
- [ ] Implement `package remove <package-name|path.epk>` command.
- [ ] Implement `package verify <package-name|path.epk>` command.
- [ ] Implement package command index for `run <command-name>` resolution without package name.
- [ ] Enforce deterministic ambiguity handling for command-name collisions.
- [ ] Report validation, compatibility, and launch status in machine-readable and human-readable output.

**Success**: package lifecycle operations are scriptable and predictable.

## Step 9.1 --- Remote repository integration (planned, not implemented now)

**Goal**: prepare explicit package fetch workflows without adding dependency solver behavior.

- [ ] Define signed repository index format.
- [ ] Add explicit `package fetch <package-name>` flow (user-triggered only).
- [ ] Verify downloaded package hash and optional signature before local storage.
- [ ] Keep launch path identical to local package add.
- [ ] Enforce policy: no transitive auto-fetch, no background auto-resolution.

**Success**: remote installation is an explicit distribution channel, not a dependency-resolution system.

## Step 10 --- Loader and runtime boundary integration

**Goal**: guarantee deterministic runtime lookup for packaged applications.

- [ ] Implement loader search order:
  - [ ] `/package/binary` first
  - [ ] fixed system runtime boundary second
- [ ] Validate runtime symbol resolution with embedded package libraries.
- [ ] Validate default packaged application work folder as `/user-data`.

**Success**: packaged applications launch without extracted binaries and without global dependency resolution.

## Step 11 --- Security hardening

**Goal**: reduce attack surface in package parsing and mounting.

- [ ] Fuzz package parser with malformed headers and table entries.
- [ ] Enforce strict integer overflow checks for offsets and sizes.
- [ ] Enforce signature policy modes (disabled, optional, required).
- [ ] Add rate-limited warning/error logs for repeated invalid packages.

**Success**: malformed or hostile package files fail closed without kernel instability.

## Step 12 --- Observability and diagnostics

**Goal**: make package launch failures easy to inspect.

- [ ] Add structured kernel logs for validate, compatibility, mount, unmount, launch decisions.
- [ ] Add shell command for mounted package view inspection.
- [ ] Add shell command for package validation/compatibility report.

**Success**: support and debugging workflows identify package failures without source code tracing.

## Step 13 --- Validation matrix and release criteria

**Goal**: certify packaging behavior across supported architectures.

- [ ] Validate full flow on `x86-32` and `x86-64`.
- [ ] Validate packaged application launch with embedded runtime payload.
- [ ] Validate stop/relaunch cycles without leaked mounts.
- [ ] Validate negative paths (corrupt hash, bad signature, incompatible kernel_api, invalid folder alias).

**Success**: packaging passes automated smoke coverage and manual fault-injection scenarios.

## Step Decoupling (each step runs without the next)

- [ ] Steps 1-3: format and validation only, no mounted file access.
- [ ] Steps 4-5: readonly package filesystem and streaming reads.
- [ ] Steps 6-8: private process mount model and launch pipeline.
- [ ] Steps 9-10: user commands and runtime boundary behavior.
- [ ] Steps 11-13: hardening, observability, and certification.
