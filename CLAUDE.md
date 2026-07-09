# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Quick Start

**MyEngine** is a from-scratch game engine written in C++20 for Windows with DirectX 12 graphics and hand-written physics. It's structured as a modular, layered architecture with strict downward-only dependencies.

Read these files when starting work:
1. `plan.md` — the roadmap and source of truth for scope and status
2. `docs/phases/NN-*.md` — the current phase's design doc (expanded just-in-time)
3. `docs/decisions/NNNN-*.md` — architecture decision records capturing the rationale

## Build & Run

### Setup (Windows + Visual Studio)

```powershell
# Configure the build
cmake -B build -G "Visual Studio 17 2022"
```

### Build

```powershell
# Build in Debug mode (recommended for development)
cmake --build build --config Debug

# Build in Release mode
cmake --build build --config Release
```

### Run

```powershell
# Run the sandbox application
build/bin/Debug/sandbox.exe

# Or open the folder directly in Visual Studio:
# File → Open → Folder, then set sandbox.exe as the startup target.
```

### Tests

```powershell
# Run all CTest suites (required before declaring a task done)
ctest --test-dir build -C Debug --output-on-failure

# Run tests from Visual Studio:
# Tests appear automatically in the Test Explorer.
```

All executables land in `build/bin/` so the working directory is predictable.

## Architecture & Layering

The engine is split into six **layers**, each depending only on those strictly below it:

```
Layer 5: Application        ← sandbox (test bed)
Layer 4: Frameworks         ← ge_world (ECS, Scene) — gameplay framework, UI not yet implemented
Layer 3: Subsystems         ← ge_gfx (DX12), ge_input, ge_physics — audio, animation, networking, AI not yet implemented
Layer 2: Resources          [asset system, cooker] — not yet implemented
Layer 1: Core foundation    ← platform → core (types, math, log, assert, arena, reflection, serialization, jobs, containers, cvar)
Layer 0: Platform           ← window, timer, virtual memory (Win32 only)
```

**The one rule:** Dependencies point downward only. The build enforces it — if a lower layer tries to include a higher one, it fails to compile. This is the discipline that keeps the codebase from turning to mud as it grows.

### Current modules

| Path | Layer | Purpose |
|------|-------|---------|
| `engine/platform/` | 0 | OS abstraction: `window.h`, `timer.h`, `memory.h` |
| `engine/core/` | 1 | Foundation: types, log, assert, arena/pool/stack allocators, math (vec/mat4/quat/transform), containers (`Array`, `HashMap`, `HandleTable`), reflection + serialization, job system, `StringId`, CVar system |
| `engine/gfx/` | 3 | DX12 RHI (handle-based), render graph, PBR forward renderer, shadow mapping, glTF model loading |
| `engine/input/` | 3 | `InputMap`: named action/axis binding on top of `platform::KeyCode` |
| `engine/physics/` | 3 | Hand-written rigid-body physics: shapes/collision/GJK, broadphase, contact manifolds, sequential-impulse solver, `PhysicsWorld` |
| `engine/world/` | 4 | Sparse-set ECS (`World`, `ComponentPool<T>`, generational `Entity`), `Scene` (roster + save/load) |
| `sandbox/` | 5 | Application / test bed: `main.cpp` |

### Module layout

Every module follows this structure:

```
engine/<module>/
├─ include/<module>/     public headers (no OS #includes)
├─ src/                  implementation (all platform-specific code lives here)
├─ tests/                unit tests (own executable, registered with CTest)
└─ CMakeLists.txt        build definition
```

**Isolation principle:** Platform headers use raw `std::size_t` / `std::uint32_t`, not engine aliases, so platform depends on nothing (not even core). OS handles are exposed as `void*` to keep `<windows.h>` out of public headers.

## Key Design Decisions

### Layering & dependencies (ADR-0001)
Strict downward-only: modules depend only on layers below them, siblings never depend on each other. Enforced by the build.

### Language & toolchain (ADR-0002)
- **C++20** (we use `<format>`, designated initializers, constexpr)
- **CMake 3.20+** → Visual Studio solution
- **No exceptions in core or hot paths**; no RTTI (custom reflection system replaces it)
- **Warnings as errors:** `/W4 /permissive-` (MSVC flags)

### Testing strategy (ADR-0003)
- Tiered testing matched to what's actually testable: **unit** (deterministic
  logic — math, allocators, collision, containers), **simulation-baseline**
  (seeded runs vs. a recorded end state within tolerance), **golden-image**
  (offscreen render vs. reference PNG), **smoke** (boot → N frames → clean
  shutdown, zero debug-layer errors), **manual** (feel/look review). Depth
  matched to risk, not coverage numbers — see `docs/testing.md`.

### Memory management (ADR-0004)
- **Arena (linear/bump allocator)** is the default: O(1) alloc, freed all at once via `reset()`.
- Most data lives in long-lived permanent arenas (per-level, per-subsystem) or per-frame scratch arenas.
- Containers and subsystems take an allocator pointer rather than allocating globally.
- Memory comes from `platform::mem_alloc` (Windows `VirtualAlloc`), not the CRT heap.

### Math conventions (ADR-0005)
- **Left-handed** coordinate system
- **Row-major** matrix storage, **row vectors**
- **[0, 1]** depth range
- Scalar math first (header-only); SIMD deferred until profiling justifies it.

### DX12 descriptor & binding model (ADR-0006)
- Classic root signature + descriptor tables (not bindless/SM6.6) for now:
  simpler to get correct first, revisit if descriptor-table churn shows up
  as a bottleneck.

### Reflection / type-system mechanism (ADR-0007)
- Annotation macros + inline registration (`GE_REFLECT_BEGIN`/`GE_FIELD`/
  `GE_REFLECT_END`), no RTTI, no codegen step. Each reflected type computes
  its own `TypeInfo*` (field metadata via `offsetof`) at static-init time.
  Feeds serialization directly; editor/network/scripting binding are future
  consumers of the same `TypeInfo`.

### ECS storage model (ADR-0008)
- Sparse-set component pools (`ComponentPool<T>`: dense `Array<T>` + sparse
  index array), not archetypes — simpler add/remove at Phase 4/5's entity
  counts. `Entity` reuses `core/handle.h`'s generational `HandleTable`.
  `World::each<T>` / `each<A,B>` are the query API; no archetype/query cache.

### Scripting / iteration model (ADR-0009)
- Plain compiled C++ systems (free functions taking `World&` + explicit
  arguments), not an embedded scripting language or hot-reloadable DLLs —
  deliberately deferred until gameplay code churns often enough to justify
  the edit-compile-relink cost that alternative would solve.

### Renderer submission & RHI binding layout (ADR-0010)
- Immediate-mode draw list: `load_model()` returns a `ModelId`; callers
  `submit(ModelId, mat4)` per frame between `begin_frame`/`end_frame`.
  All dynamic constants come from a per-frame upload ring
  (`set_constant_buffer(slot, buffer, 256-aligned offset)`); per-frame
  resources are indexed by `RhiDevice::frame_index()`, never by handle
  arithmetic. Root signatures are derived from `PipelineDesc::bindings`
  (`BindingLayout` CBV/SRV-table counts, deduped in a cache) — the RHI has
  no renderer-specific layout knowledge. The renderer (`renderer.cpp`) is
  backend-agnostic; only `rhi_dx12.cpp` may touch D3D12/Win32.

## Code Style & Conventions

- **Namespaces:** everything under `ge`. Modules nest: `ge::platform`, `ge::core`, `ge::gfx`, etc.
- **Types:** fixed-width aliases in core+above (`u8`, `u32`, `f32`, `usize`); platform uses `std::` types
- **Naming:** `snake_case` functions/variables/files; `PascalCase` types; `k`-prefix for constants; trailing `_` for private members
- **Headers:** `#pragma once`; include paths are module-qualified: `#include "core/arena.h"`, `#include "platform/window.h"`
- **Formatting:** 4-space indent, opening brace same line, ~100-column soft limit
- **Logging:** `GE_TRACE/INFO/WARN/ERROR("fmt {}", args)` (backed by `std::format`, checked at compile time). Never call from platform.
- **Asserts:** `GE_ASSERT(cond)` for invariants; validate inputs at module boundaries
- **Errors:** Prefer return values (`bool`, status enum, `std::optional`, `std::expected`) over exceptions
- **API docs:** Public headers use Doxygen-style `/** */` comments (`@file`, `@brief`, `@param`, `@return`, `@tparam`). Use `///` for inline member annotations. Keep docs on declarations only (not definitions in .cpp).

## Adding Code

### New file in existing module
Add it to that module's `CMakeLists.txt` source list.

### New module `foo`
1. Create `engine/foo/include/foo/` (public headers) and `engine/foo/src/`
2. Create `engine/foo/CMakeLists.txt` with `add_library(ge_foo STATIC ...)`, `target_include_directories(ge_foo PUBLIC include)`, and `target_link_libraries(ge_foo PUBLIC ...)` linking **only lower layers**
3. Add `add_subdirectory(engine/foo)` in the top-level `CMakeLists.txt` in the correct layer order

Keep the public-header / hidden-impl split: the `Window` pImpl pattern is the reference example.

## Testing & Verification

Every phase is done **only** when all of these hold:

- **Compiles clean** at `/W4 /permissive-`
- **All CTest suites are green** with real behavior tests (not tautologies)
- **Phase's critical tests pass** (listed in `plan.md`)
- **Runs without tripping an assert and without leaks** (debug allocator / ASan clean)
- **Golden image or simulation baselines updated deliberately** — diff reviewed, never silently regenerated just to make a test pass
- **Golden rules still hold** (see Golden Rules section below)
- **Phase's manual-review items are checked off**

Framework, test tiers, and "build to be testable" guidance: see
[`docs/testing.md`](docs/testing.md) (rationale in ADR-0003).

### Proportionality
Write tests where a bug is costly or hard to spot (math correctness, memory safety, serialization, netcode, determinism). Skip trivial getters and coverage-padding. Never let test-writing stall the vertical slice — match test depth to risk.

### Self-review checklist
Run before claiming a task done, and report the answers:
1. Did new logic get a test that asserts behavior, not implementation trivia?
2. Are all CTest suites green locally?
3. If a golden image or baseline changed, did you review the diff and confirm the change is intended?
4. Any new sideways/upward dependency, or `<windows.h>` leaking into a public header?
5. Is the ownership and lifetime of every allocation clear, and is the Debug build leak-clean?
6. State plainly: what you tested, the results, and any baselines you updated.

## How to Work

1. Read `plan.md`; confirm the current phase and task. Open its design doc in `docs/phases/`; if it is still a stub, **expand it first** (scope, tech decisions, design, milestones, test plan) before writing code.
2. Stay within the current layer unless the task explicitly spans layers. Work the smallest correct increment of the current phase.
3. Implement, then write or update the tests that cover the change (the phase's critical tests are listed in `plan.md`).
4. Run `ctest`. Red or missing tests means the task is not finished — fix it before continuing.
5. If you made a cross-cutting decision, record it as an ADR in `docs/decisions/` (from `_TEMPLATE.md`), set its status, and link it from the phase doc and the `plan.md` decisions index.
6. Report: what changed, which files, the test results, and an explicit confirmation that the golden rules still hold.
7. When something is ambiguous or would violate a rule, ask or flag — don't guess.
8. When the deliverable is done (tests green by the definition above), update statuses: the phase-doc header, the `plan.md` Status log, and the module checklist.

## Current Phase: Phase 6 — Animation

Phase 5 is **complete**: hand-written rigid-body physics (`ge_physics`,
Layer 3, depends only on `ge_core`). Collision primitives (`Sphere`, `Box`,
`Capsule`) with narrowphase (`sphere_sphere`/`sphere_box`/`box_box` SAT/
`capsule_box`) and a support-function GJK distance query for the general
convex case; sweep-and-prune broadphase; contact manifold generation
(Sutherland-Hodgman face clipping + edge-edge closest points, up to 4 points
per pair); a sequential-impulse velocity solver (Baumgarte position
correction, box-approximated Coulomb friction) plus semi-implicit Euler
integration, wired end-to-end as `PhysicsWorld::step(dt)`. ECS-facing glue:
`world::RigidBodyRef` (a bare opaque body-id handle — `ge_world` still has
zero dependency on `ge_physics`) plus sandbox-hosted sync systems, mirroring
where `system_move_player` already lived in Phase 4. The sandbox's player is
now a rotation-locked capsule rigid body, driven by yaw-relative input
velocity, falling under gravity and resting on a static ground box — a
physics-driven, controllable character. 54 physics unit/baseline tests,
including simulation-baseline settling tests for a box stack and the
character capsule (Phase 5's DoD scenarios).

Phase 6 goal: skeletal animation with GPU skinning, clip sampling/looping,
blend trees, animation state machines, root motion (IK later).

See `docs/phases/06-animation.md` for design details (expanded 2026-07-09:
module layout for the new `ge_anim` Layer 3 module, sibling-isolation
contract with `ge_gfx`, GPU-skinning tech decisions, 5 milestones, test
plan).

## Golden Rules (Never Violate)

1. **Dependencies point downward only.** Layer order: `platform → core → resources → subsystems → frameworks → application`. A module includes only layers strictly below it. Sibling subsystems never depend on each other — they communicate through data owned by the world/framework layer.

2. **No OS headers in public headers.** `<windows.h>` and friends appear only in `*_win32.cpp`. Expose OS handles as `void*`.

3. **Platform depends on nothing** but the OS — not even core. It uses raw `std::` types, never `ge::` aliases.

4. **Data-oriented by default.** Plain structs, contiguous arrays, struct-of-arrays where it pays. No deep inheritance, no virtual calls in hot loops. ECS is for the world/gameplay layer only.

5. **Explicit memory.** Allocate through arenas/pools, not scattered `new`/`malloc`. Per-frame data lives in a scratch arena that is `reset()` once per frame.

6. **If a task would break any rule above, stop and say so** instead of doing it. Flag the conflict and propose an alternative.

## Third-Party Libraries

Solved, non-core problems only (asset import, image decode, debug UI, test
framework) — never the from-scratch goals (DX12 renderer, physics solver).
Full policy and vendoring rule: [`docs/third-party-libraries.md`](docs/third-party-libraries.md).

## Documentation Map

- `plan.md` — roadmap, scope, and source of truth for status (read at every session start)
- `CLAUDE.md` — this file: rules, conventions, definition of done
- `docs/phases/NN-*.md` — per-phase design depth (expanded just-in-time)
- `docs/decisions/NNNN-*.md` — Architecture Decision Records (ADRs) capturing rationale
- `docs/decisions/README.md` — index of all ADRs
- `docs/documentation/NN-*.md` — reference manual / study material describing each
  implemented system **as it currently is** (indexed in `docs/documentation/README.md`)
- `docs/testing.md` — test framework, tiers, and testability guidance (read when setting up a module's tests)
- `docs/third-party-libraries.md` — allowed libraries and vendoring policy (read when adding a dependency)
- **API reference** — all public headers carry Doxygen `/** */` documentation
  (`@file`, `@brief`, `@param`, `@return`, `@tparam`).  Run `doxygen` from the
  repo root (once a Doxyfile is added) to generate HTML/XML output.

Upkeep rules:
- Expand a phase's design doc before coding that phase
- Record cross-cutting decisions as ADRs and link them
- Keep each decision in exactly one place (link, never duplicate)
- Update statuses in phase docs and `plan.md` as work lands
- When adding or changing a public API, update its Doxygen comment in the header
- When a change invalidates something in `docs/documentation/`, update the
  affected document in the same piece of work — phase docs record history and
  stay untouched, but the reference manual describes the present and must not rot
