codex resume 019c9567-fca4-7b63-a9ea-858d0afc28dd

# Intel iGPU Driver Roadmap (native, windowing-focused)

## Scope
- Build a native Intel integrated graphics driver for display and 2D windowing.
- Reuse the existing graphics stack (`kernel/include/GFX.h`, windowing already present).
- No 3D API, no shader pipeline, no render command submission.
- Support model differences through explicit capabilities, not ad-hoc per-model branching.

## Non-goals
- No OpenGL/Vulkan/Direct3D equivalent.
- No media decode/encode blocks.
- No overclocking or advanced power tuning.

## Design Rules
- Keep one generic graphics API and one Intel backend.
- Prefer capability-driven behavior over hardcoded model checks.
- Keep source files split by responsibility (under 1000 lines each).
- Every unimplemented driver command returns `DF_RETURN_NOT_IMPLEMENTED`.
- Keep logs actionable and rate-limited for polling paths.
- MVP may target one known platform (for example PCI device id `0x3e9b`), but implementation must stay multi-family ready:
  - keep per-family behavior in capability tables / family helpers,
  - do not hardwire one device id in core flow control,
  - keep extension path explicit for additional Intel generations.

## Step 0 - Baseline and contract freeze
- [x] Record exactly which `DF_GFX_*` commands are used by Desktop/Console/SYSCall.
- [x] Document what `VESA.c` already guarantees (mode set, linear memory access, pixel ops).
- [x] Define the minimal contract required by your window manager to be backend-agnostic.

Deliverable:
- A short compatibility note: "what every graphics driver must provide to run the window manager".

## Step 1 - Evolve `GFX.h` into a stable backend interface
The existing API is enough for software drawing, but too poor for clean native scanout management.

- [x] Add capability and output query commands:
  - `DF_GFX_GETCAPABILITIES`
  - `DF_GFX_ENUMOUTPUTS`
  - `DF_GFX_GETOUTPUTINFO`
- [x] Add present/synchronization commands:
  - `DF_GFX_PRESENT`
  - `DF_GFX_WAITVBLANK`
- [x] Add optional page-flip support:
  - `DF_GFX_ALLOCSURFACE`
  - `DF_GFX_FREESURFACE`
  - `DF_GFX_SETSCANOUT`
- [x] Keep existing commands (`SETPIXEL`, `LINE`, `RECTANGLE`) for compatibility.
- [x] Require legacy drivers (VESA) to return `DF_RETURN_NOT_IMPLEMENTED` for new optional commands.

Suggested new structures:
- `GFX_CAPABILITIES`
  - `HasHardwareModeset`
  - `HasPageFlip`
  - `HasVBlankInterrupt`
  - `HasCursorPlane`
  - `SupportsTiledSurface`
  - `MaxWidth`, `MaxHeight`
  - `PreferredFormat`
- `GFX_OUTPUT_INFO`
  - `OutputId`
  - `Type` (eDP, HDMI, DisplayPort, VGA)
  - `IsConnected`
  - `NativeWidth`, `NativeHeight`, `RefreshRate`
- `GFX_SURFACE_INFO`
  - `Width`, `Height`, `Format`, `Pitch`, `MemoryBase`, `Flags`

Deliverable:
- `GFX.h` update proposal that stays backward-compatible with VESA.
- Implemented in `kernel/include/GFX.h` and `kernel/source/drivers/graphics/vesa/VESA-Base.c`.

## Step 2 - Intel driver skeleton and PCI attach
Create a dedicated Intel graphics driver module.

- [x] Add `kernel/source/drivers/graphics/IntelGfx.c` as entry/dispatch.
- [x] Detect Intel GPU on PCI (`VendorId = 0x8086`, display class).
- [x] Enable MMIO + bus master on PCI command register.
- [x] Read BARs and map the MMIO BAR with `MapIOMemory`.
- [x] Validate MMIO access through a harmless identity register read.

Deliverable:
- Driver loads, identifies Intel GPU, maps MMIO, prints concise identification logs.
- Delivered by `IntelGfx.c`, with backend selection/fallback managed by `Graphics-Selector.c`.

## Step 3 - Capability model by generation
Avoid branching everywhere by centralizing capabilities.

- [x] Add `INTEL_GFX_CAPS` and fill it at init from:
  - PCI device id family table.
  - Key register probes (display version, pipe/port presence).
- [x] Expose normalized values to generic layer through `DF_GFX_GETCAPABILITIES`.

Example capability fields:
- `Generation`
- `DisplayVersion`
- `PipeCount`
- `TranscoderCount`
- `PortMask`
- `SupportsFBC`
- `SupportsPSR`
- `SupportsAsyncFlip`

Deliverable:
- One capability object that drives all later code paths.
- Implemented in `kernel/source/drivers/graphics/IntelGfx.c` with table-based defaults + MMIO probes.

## Step 4 - Mode takeover path (first usable milestone)
First milestone should avoid full native modesetting complexity.

- [x] Read active scanout state from Intel display registers (pipe, plane, stride, base).
- [x] Build a `GRAPHICS_CONTEXT` from the active mode.
- [x] Map the active framebuffer memory and expose it as `MemoryBase`.
- [x] Present through CPU blit to active scanout buffer.

Why this step:
- Produces a native Intel backend with minimal risk.
- Gives immediate value for your existing windowing pipeline.

Deliverable:
- Desktop can draw through Intel backend using existing graphics API.
- Implemented in `kernel/source/drivers/graphics/IntelGfx.c` through active-mode takeover and CPU primitives.

## Step 5 - Native modeset (controlled expansion)
After takeover works, add explicit mode programming.

- [x] Implement mode validation (`width/height/format/refresh` against capabilities).
- [x] Implement pipe disable/enable sequence.
- [x] Program timings, stride, surface base, pixel format.
- [x] Bring panel/backlight handling only where needed for internal panel stability.
- [x] Keep one conservative mode path first (for example: one pipe, one output).
- [x] Add explicit transcoder routing policy for the selected pipe/output pair.
- [x] Add clock programming steps (DPLL / clock source selection) required by each targeted generation.
- [x] Add connector link configuration and training path (eDP/DP first, then HDMI).
- [x] Add atomic-style programming order checks (disable, program, enable, verify) per stage.
- [x] Add failure rollback sequence for partial modeset programming.

Deliverable:
- `DF_GFX_SETMODE` programs Intel display pipeline without firmware fallback.

Status note:
- Conservative native modeset is implemented in `kernel/source/drivers/graphics/igpu/iGPU-Mode.c` on one active pipe/output.
- Validation enforces XRGB8888 path and active-mode dimensions while API lacks explicit refresh/format fields in `GRAPHICS_MODE_INFO`.
- Stage-ordered native modeset applies explicit pipe/output/transcoder policy, conservative clock-source programming (`DPLL_CTRL1` reuse), connector-link enable, internal panel stabilization, and rollback to captured hardware state on partial failure in `kernel/source/drivers/graphics/igpu/iGPU-Mode.c`.
- Cold modeset bootstrap is implemented for the `no active Intel scanout` path: load keeps the Intel backend available, `SETMODE` builds conservative timings from the requested mode, programs pipe/output/link, and rebuilds context from the programmed state when takeover readback is unavailable.

### Step 5.b - Spec alignment for all iGPU display modes
Objective: remove single-format assumptions and make iGPU modeset path spec-compliant for all declared pixel formats/modes.

- [ ] Define explicit supported mode matrix in code and documentation:
  - resolution constraints (`MinWidth/MinHeight`, `MaxWidth/MaxHeight`, alignment requirements),
  - pixel formats (`RGB565`, `RGB888/BGR888`, `XRGB8888/ARGB8888` if supported),
  - refresh and link constraints per output/family.
- [ ] Replace implicit "32 bpp only" policy with explicit format validation and deterministic error reporting.
- [ ] Extend mode programming path to map requested format -> hardware plane format bits per family.
- [ ] Propagate real channel layout (bit position/mask) from programmed/readback format to `GRAPHICS_CONTEXT` for all iGPU modes.
- [ ] Ensure text and 2D primitives use context channel layout, never hardcoded color packing assumptions.
- [ ] Add per-family format capability flags in `INTEL_GFX_CAPS` and keep selection capability-driven.
- [ ] Add strict fallback rules when requested format is unsupported:
  - either reject with actionable code (`DF_RETURN_IGFX_UNSUPPORTED_FORMAT`),
  - or convert through a documented compatibility mode with explicit warning.
- [ ] Add one canonical "mode normalization" helper:
  - resolve aliases and incomplete requests,
  - enforce alignment/pitch constraints,
  - produce one fully specified internal mode descriptor.
- [ ] Expose diagnostics for format/mode decisions:
  - requested mode,
  - normalized mode,
  - programmed hardware format,
  - effective context channel masks.

Deliverable:
- `DF_GFX_SETMODE` supports the full declared iGPU mode matrix without hidden fixed-format assumptions.
- `DF_GFX_GETMODEINFO` returns effective mode/format consistent with actual hardware programming.
- Console and windowing rendering remain color-correct across every supported iGPU format.

## Step 6 - Buffer management and present model
Introduce a clean surface model for future growth.

- [x] Implement software front/back surface allocation in driver-controlled memory.
- [x] Add `DF_GFX_PRESENT` semantics:
  - If page flip supported: flip.
  - Else: blit dirty regions.
- [x] Keep a generic dirty-rectangle input path for the window manager.

Deliverable:
- Flicker-free present path with clear fallback behavior.

Status note:
- `IntelGfx.c` implements `DF_GFX_ALLOCSURFACE`, `DF_GFX_FREESURFACE`, `DF_GFX_SETSCANOUT` with software surfaces in kernel heap.
- `DF_GFX_PRESENT` uses dirty-rectangle blit from selected surface to active scanout as conservative fallback path.
- Hardware page-flip path is intentionally deferred; `PRESENT` uses blit semantics for stability on this milestone.

## Step 7 - VBlank and synchronization
- [x] Add optional vblank interrupt handling.
- [x] Implement `DF_GFX_WAITVBLANK` with timeout safety.
- [x] Protect present path with lightweight locking + frame sequence counters.

Deliverable:
- Stable frame pacing and reduced tearing on supported configurations.

Status note:
- `iGPU-Interrupt.c` adds optional vblank interrupt-status handling (`PIPESTAT`) with automatic scanline polling fallback when interrupt status is unavailable.
- `DF_GFX_WAITVBLANK` is implemented with bounded wait (`HasOperationTimedOut`) and timeout diagnostics protected by shared `RateLimiter`.
- Present and vblank sequencing are protected by a dedicated lightweight mutex in `INTEL_GFX_STATE` (`PresentMutex`) with frame counters (`PresentFrameSequence`, `VBlankFrameSequence`).

## Step 8 - Output management (multi-output ready)
- [ ] Implement `ENUMOUTPUTS` and `GETOUTPUTINFO`.
- [ ] Start with one active output policy; keep data model ready for many outputs.
- [ ] Add connector hotplug detection only after single-output stability.
- [ ] Define connector-to-pipe/transcoder policy (priority, allowed combinations, fallback order).
- [ ] Track per-output requirements (clock limits, lane count, link rate, color format limits).
- [ ] Add explicit internal-panel-first policy for hybrid laptop targets.

Deliverable:
- Output enumeration API stable even before full multi-monitor policy is enabled.

## Step 9 - Integration with existing VESA backend
- [x] Keep VESA as fallback backend.
- [x] Ensure Desktop chooses backend by capability/priority policy.
- [ ] Add a boot option or kernel config key to force one backend for debugging.
- [ ] Define backend failover contract: conditions that trigger fallback and when to stay on Intel path.
- [ ] Keep last failure reason exposed through diagnostics to avoid silent fallback loops.

Deliverable:
- Safe fallback path and deterministic backend selection.

## Step 10 - Centralized mode selection policy (outside drivers)
- [x] Move mode selection policy out of backend-specific `DF_GFX_SETMODE` implementations.
- [x] Define one kernel-side selector that chooses the best mode using:
  - `DF_GFX_GETCAPABILITIES`,
  - `DF_GFX_GETMODECOUNT`,
  - `DF_GFX_GETMODEINFO`.
- [x] Keep `DF_GFX_SETMODE` backend role limited to applying an explicit requested mode.
- [x] Keep backend auto-select behavior only as a legacy fallback path while all drivers are migrated.
- [x] Define deterministic tie-break rules (resolution, color depth/format, refresh preference, backend constraints).
- [x] Expose selected-mode decision diagnostics (requested mode, selected mode, rejection reasons for candidates).

Deliverable:
- Desktop mode selection is backend-agnostic, deterministic, and capability-driven.

## Step 11 - iGPU hardware cursor support
- [ ] Add explicit iGPU capability exposure for hardware cursor plane support through `DF_GFX_GETCAPABILITIES`.
- [ ] Define one backend contract for cursor operations (set shape/format, set position, show/hide) without coupling cursor ownership to userland.
- [ ] Implement Intel hardware cursor plane programming path for supported generations and active output/pipe routing.
- [ ] Validate cursor clipping, hotspot handling, and deterministic behavior across mode changes and backend reinitialization.
- [ ] Add safe fallback signaling so compositor can switch to software cursor overlay when hardware cursor is unavailable or fails.
- [ ] Add concise diagnostics for active cursor path and cursor-plane failure reason.

## Step 12 - Diagnostics and shell tooling
- [ ] Add concise commands (or debug paths) to print:
  - GPU identification and capabilities
  - Active pipe/plane/output
  - Active transcoder/connector/link state
  - Clock source / PLL selection summary
  - Current mode and stride
  - Present path stats (flip/blit counters, suppressed warnings)
  - Last modeset failure stage + return code
- [ ] Use shared rate limiting in polling loops.

Deliverable:
- Repeatable diagnostics for bring-up and regression tracking.

## Step 13 - Test matrix and acceptance gates
Minimum gates before considering the driver stable:

- [ ] Boot x86-32 and x86-64 with Intel backend enabled.
- [ ] Desktop starts and draws correctly.
- [ ] 15-second stress loop of window moves/invalidations without fault.
- [ ] Mode set success and rollback behavior validated.
- [ ] Cold modeset validated on at least one hybrid laptop profile where takeover is unavailable.
- [ ] Fallback policy validated when Intel path fails mid-sequence.
- [ ] Suspend/resume not required for first stable release unless explicitly targeted.

## Suggested code split
Keep one file per concern:
- `IntelGfx.c` (driver entry, dispatch, lifecycle)
- `IntelGfxPci.c` (PCI detection/attach)
- `IntelGfxCaps.c` (capability model)
- `IntelGfxMode.c` (mode takeover + native modeset)
- `IntelGfxSurface.c` (surface allocation/present)
- `IntelGfxInterrupt.c` (vblank/interrupts)
- `IntelGfxOutput.c` (connector/output info)

## Suggested incremental milestones
1. `M1`: Intel detect + MMIO map + capability dump.
2. `M2`: Mode takeover + desktop draw path works.
3. `M3`: Native `SETMODE` for one output.
4. `M4`: Present with page-flip fallback model.
5. `M5`: VBlank sync + output enumeration.

## Notes for PH317-52 target
- Expect hybrid graphics platform behavior (integrated graphics + GeForce present).
- Prioritize internal panel path on Intel backend first.
- Treat discrete GeForce as independent future backend/offload topic.

## Follow-up documentation updates
When implementation starts (not only planning), update:
- `doc/guides/Kernel.md` with the Intel graphics backend architecture.
- Doxygen pages for new driver modules and exported `GFX.h` structures.

## Mandatory implementation direction
- Native modeset implementation must stay multi-family by construction.
- Do not implement machine-specific paths (for example one laptop profile only).
- Introduce explicit per-family operations (`INTEL_DISPLAY_FAMILY_OPS`) for routing, clock programming, link configuration/training, plane/pipe programming, stride/tile encoding, and stage verification.
- Keep one shared atomic-style pipeline (`detect family -> validate -> disable -> program -> enable -> verify -> commit/rollback`) and delegate hardware details only to family operations.
- If one family path is incomplete, fail explicitly with diagnostic stage/family details; do not silently fallback to heuristic programming.

Implementation status:
- `iGPU-Mode.c` resolves one explicit family operations entry from display version and refuses unsupported families with explicit failure code (`DF_RETURN_IGFX_UNSUPPORTED_FAMILY`).
- Native modeset records failure stage/code in `INTEL_GFX_STATE` (`LastModesetFailureStage`, `LastModesetFailureCode`) and emits stage-scoped diagnostics for rollback and verification.
- Stride/plane behavior is driven by per-family descriptors (read/write mask, alignment, tiling policy) instead of one global hardcoded path.
