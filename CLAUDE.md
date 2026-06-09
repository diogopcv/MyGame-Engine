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
Layer 4: Frameworks         [ECS, world, gameplay, UI] — not yet implemented
Layer 3: Subsystems         [graphics, physics, audio, animation, input, networking, AI] — not yet implemented
Layer 2: Resources          [asset system, cooker] — not yet implemented
Layer 1: Core foundation    ← platform → core (types, math, log, assert, arena)
Layer 0: Platform           ← window, timer, virtual memory (Win32 only)
```

**The one rule:** Dependencies point downward only. The build enforces it — if a lower layer tries to include a higher one, it fails to compile. This is the discipline that keeps the codebase from turning to mud as it grows.

### Current modules

| Path | Layer | Purpose |
|------|-------|---------|
| `engine/platform/` | 0 | OS abstraction: `window.h`, `timer.h`, `memory.h` |
| `engine/core/` | 1 | Foundation: `types.h`, `log.h`, `assert.h`, `arena.h` |
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

## Code Style & Conventions

- **Namespaces:** everything under `ge`. Modules nest: `ge::platform`, `ge::core`, `ge::gfx`, etc.
- **Types:** fixed-width aliases in core+above (`u8`, `u32`, `f32`, `usize`); platform uses `std::` types
- **Naming:** `snake_case` functions/variables/files; `PascalCase` types; `k`-prefix for constants; trailing `_` for private members
- **Headers:** `#pragma once`; include paths are module-qualified: `#include "core/arena.h"`, `#include "platform/window.h"`
- **Formatting:** 4-space indent, opening brace same line, ~100-column soft limit
- **Logging:** `GE_TRACE/INFO/WARN/ERROR("fmt {}", args)` (backed by `std::format`, checked at compile time). Never call from platform.
- **Asserts:** `GE_ASSERT(cond)` for invariants; validate inputs at module boundaries
- **Errors:** Prefer return values (`bool`, status enum, `std::optional`, `std::expected`) over exceptions

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

### Test framework
- Lightweight **doctest** (header-only) for unit tests; GoogleTest if heavier fixtures or mocking are needed — pull via CMake `FetchContent` or vendor under `third_party/`
- Each module owns its tests in `engine/<module>/tests/`, building a `ge_<module>_tests` executable registered with `enable_testing()` + CTest
- Tests are tiered by risk:
  - **Unit tests:** deterministic pure logic (math, allocators, containers, serialization)
  - **Simulation baselines:** run a fixed, seeded sim and assert end state vs. recorded baseline
  - **Golden-image tests:** render a known scene offscreen, compare to reference PNG within tolerance
  - **Smoke tests:** boot, run N frames, shut down; assert no crash/leak/assert
  - **Manual review:** feel, latency, audio, look — human sign-off against a checklist

### Build to be testable
Prefer free functions and explicit dependencies over hidden globals; keep simulation deterministic and RNG seedable; abstract the clock so tests can feed time. Keep the DX12 debug layer + DRED and a debug leak-tracking allocator (or ASan) on in Debug builds as always-on correctness nets.

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

## Current Phase: Phase 1 — First Triangle (DX12 Gauntlet)

Status: Not started.

Phase 0 is **complete**: math library (vec2/3/4, mat4, quat, transform/projection
helpers, look-at/perspective), doctest + CTest harness, 41 unit tests all green.

Phase 1 goal: DX12 device, swapchain, command queue/list/allocators, fences,
descriptor heaps, root signature, PSO, vertex buffer, draw call — ending with a
spinning textured cube using the math library's MVP matrix.

See `docs/phases/01-dx12-triangle.md` for full design details.

## Golden Rules (Never Violate)

1. **Dependencies point downward only.** Layer order: `platform → core → resources → subsystems → frameworks → application`. A module includes only layers strictly below it. Sibling subsystems never depend on each other — they communicate through data owned by the world/framework layer.

2. **No OS headers in public headers.** `<windows.h>` and friends appear only in `*_win32.cpp`. Expose OS handles as `void*`.

3. **Platform depends on nothing** but the OS — not even core. It uses raw `std::` types, never `ge::` aliases.

4. **Data-oriented by default.** Plain structs, contiguous arrays, struct-of-arrays where it pays. No deep inheritance, no virtual calls in hot loops. ECS is for the world/gameplay layer only.

5. **Explicit memory.** Allocate through arenas/pools, not scattered `new`/`malloc`. Per-frame data lives in a scratch arena that is `reset()` once per frame.

6. **If a task would break any rule above, stop and say so** instead of doing it. Flag the conflict and propose an alternative.

## Third-Party Libraries

Allowed for **solved, non-core problems only:**
- Asset import: cgltf / assimp
- Image decode: stb_image
- Debug/editor UI: Dear ImGui
- Test framework: doctest / GoogleTest

**Not allowed:** replacing the from-scratch goals (DX12 renderer and physics solver are hand-written).

Vendor third-party code under `third_party/` and wrap each behind an engine interface so it never leaks into subsystem APIs.

## Documentation Map

- `plan.md` — roadmap, scope, and source of truth for status (read at every session start)
- `CLAUDE.md` — this file: rules, conventions, definition of done
- `docs/phases/NN-*.md` — per-phase design depth (expanded just-in-time)
- `docs/decisions/NNNN-*.md` — Architecture Decision Records (ADRs) capturing rationale
- `docs/decisions/README.md` — index of all ADRs

Upkeep rules:
- Expand a phase's design doc before coding that phase
- Record cross-cutting decisions as ADRs and link them
- Keep each decision in exactly one place (link, never duplicate)
- Update statuses in phase docs and `plan.md` as work lands
