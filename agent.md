# agent.md — Working agreement for agents on MyEngine

Read this in full before changing any code. The roadmap and the list of what to
build next live in `plan.md` — read it too, work the smallest correct increment
of the current phase, and tick its checkboxes when a deliverable is finished.
Detailed per-phase design lives in `docs/phases/`, and the rationale for
cross-cutting decisions in `docs/decisions/` — see `docs/README.md` for how it
all fits together, and section 10 below for the upkeep rules you must follow.

> Tooling note: this file is named `agent.md` by request. Some tools auto-load a
> differently-named file from the repo root — Claude Code reads `CLAUDE.md`,
> several other agents read `AGENTS.md`. If you want auto-discovery, rename or
> symlink this file accordingly; the content is the same.

## 1. Project

MyEngine is a from-scratch game engine.

- Language: C++20. Platform: Windows. Toolchain: Visual Studio via CMake.
- Graphics: DirectX 12, written by hand. Physics: written by hand.
- Goals: modular, scalable, data-oriented, runnable at every step.
- Free third-party libraries are allowed for solved, non-core problems only
  (see section 8). The renderer and the physics solver are NOT among them.

## 2. Golden rules — never violate

1. **Dependencies point downward only.** Layer order, lowest first:
   `platform -> core -> resources -> subsystems -> frameworks -> application`.
   A module may include and link only layers strictly *below* it. Sibling
   subsystems never depend on each other — graphics must not include physics,
   audio must not include input, etc. They communicate through data owned by
   the world/framework layer.
2. **No OS headers in public headers.** `<windows.h>` and friends appear only in
   `*_win32.cpp`. Expose OS handles as `void*` (e.g. `Window::native_handle()`).
3. **Platform depends on nothing** but the OS — not even core. It uses raw
   `std::` types (`std::size_t`, `std::uint32_t`), never the `ge::` aliases, and
   it never logs through core (that would invert the dependency).
4. **Data-oriented by default.** Plain structs, contiguous arrays, struct-of-
   arrays where it pays. No deep inheritance, no virtual calls in hot loops. ECS
   is for the world/gameplay layer only — do not "entity-ify" core systems.
5. **Explicit memory.** Allocate through arenas/pools, not scattered
   `new`/`malloc`. Per-frame data lives in a scratch arena that is `reset()`
   once per frame.
6. **If a task would break any rule above, stop and say so** in your response
   instead of doing it. Flag the conflict and propose an alternative.

## 3. Module layout

| Path                | Layer        | Purpose                                   |
|---------------------|--------------|-------------------------------------------|
| `engine/platform/`  | 0 platform   | window, timer, virtual memory (Win32)     |
| `engine/core/`      | 1 core       | types, log, assert, arena (math: pending) |
| `sandbox/`          | 5 app        | test bed: window + game loop              |

Higher layers (`resources`, `subsystems`, `frameworks`) do not exist yet; create
them per section 7 as the roadmap reaches them.

Inside every module: public interface in `include/<module>/`, implementation in
`src/`. OS- or detail-heavy code stays in `src/`.

## 4. Conventions

- **Namespaces:** everything under `ge`. Modules nest: `ge::platform`,
  `ge::gfx`, `ge::phys`, and so on. Core primitive types live directly in `ge`.
- **Types:** use the `ge::` fixed-width aliases (`u8 u16 u32 u64`, `i8`..`i64`,
  `f32 f64`, `usize`) in core and above. Platform uses `std::` types.
- **Naming:** `snake_case` for functions, variables, and file names;
  `PascalCase` for types; `k`-prefixed for constants (`kClassName`); trailing
  underscore for private data members (`offset_`).
- **Headers:** `#pragma once`. Include via module-qualified paths:
  `#include "core/arena.h"`, `#include "platform/window.h"`.
- **Formatting:** 4-space indent, opening brace on the same line, ~100-column
  soft limit. (Add a `.clang-format` to enforce when convenient.)
- **Logging:** `GE_TRACE/INFO/WARN/ERROR("fmt {}", args)` — backed by
  `std::format`, format string checked at compile time. Never call these from
  the platform layer.
- **Asserts:** `GE_ASSERT(cond)` for invariants; validate inputs at module
  boundaries.
- **Errors:** prefer return values (`bool`, a status enum, or `std::optional`/
  `std::expected`) over exceptions. No exceptions in core or hot paths. Avoid
  RTTI in core — the custom reflection system will replace it.

## 5. Build, test, verify (Windows)

```
cmake -B build -G "Visual Studio 17 2022"            # configure (testing enabled)
cmake --build build --config Debug                    # build engine + tests
ctest --test-dir build -C Debug --output-on-failure   # run ALL tests
build/bin/Debug/sandbox.exe                            # run the app
```

Or open the folder directly in Visual Studio (CMake is auto-detected); tests
appear in the Test Explorer.

**Definition of done — a task or phase counts as done only when ALL of these hold:**
- compiles clean at `/W4 /permissive-`;
- every CTest suite is green, and the new or changed logic has tests that assert
  real behavior (not tautologies);
- the current phase's *critical tests* (listed in `plan.md`) exist and pass;
- runs without tripping an assert and without leaks (debug allocator / ASan clean);
- any golden image or simulation baseline was updated deliberately with the diff
  reviewed — never silently regenerated just to make a test pass;
- every golden rule in section 2 still holds;
- the phase's manual-review items (if any) are checked off.

If the tests will not pass, the phase is **not** done — stop and report. Do not
lower the bar to declare victory.

## 6. Testing & review

Most of an engine is not unit-testable by `assertEquals` — rendering, audio,
input feel, and animation blending need a human or a recorded reference. So
testing is tiered, and each phase leans on the tiers that fit it:

- **Unit tests** — the bulk of the "critical tests". Deterministic, pure logic:
  math, allocators, containers, serialization round-trips, reflection, hashing,
  pathfinding, collision math, packet (de)serialization. Fast (the whole suite
  well under a second); run on every build via CTest.
- **Simulation baselines** — for systems too complex to assert piecewise (e.g.
  physics): run a fixed, seeded simulation and assert the end state reproduces a
  recorded baseline within tolerance. Relies on the fixed-timestep determinism
  the loop already provides.
- **Golden-image tests** — the only sane automated check for rendering: render a
  known scene offscreen and compare to a reference PNG within a per-pixel
  tolerance.
- **Smoke tests** — boot, run N frames, shut down; assert no crash, no assert, no
  leak, and zero DX12 debug-layer / validation errors.
- **Manual review** — feel, latency, audio, look. A human signs off against a
  short per-phase checklist and records the result.

**Framework & layout.** Use a lightweight C++ test framework — `doctest` is the
default (header-only, near-zero compile cost, trivial to embed); GoogleTest is
the alternative if you need heavier fixtures or mocking. It is an allowed
third-party dependency (a solved, non-core problem): pull it via CMake
`FetchContent` or vendor the header under `third_party/`. Each module owns its
tests in `engine/<module>/tests/`, building a `ge_<module>_tests` executable that
links only that module and is registered with `enable_testing()` + CTest.

**Build to be testable.** Prefer free functions and explicit dependencies over
hidden globals; keep the simulation deterministic and the RNG seedable; abstract
the clock so tests can feed time. Keep the DX12 debug layer + DRED and a debug
leak-tracking allocator (or ASan) on in Debug builds as always-on correctness
nets.

**Proportionality — read this.** Tests are a means, not a ritual. Write them
where a bug is costly or hard to spot (math correctness, memory safety,
serialization/format compatibility, netcode, determinism); skip trivial getters
and coverage-padding. Never let test-writing stall the vertical slice — match
test depth to risk.

**Self-review checklist** (run before claiming a task done, and report the answers):
1. Did new logic get a test that asserts behavior, not implementation trivia?
2. Are all CTest suites green locally?
3. If a golden image or baseline changed, did you review the diff and confirm the
   change is intended?
4. Any new sideways/upward dependency, or `<windows.h>` leaking into a public
   header?
5. Is the ownership and lifetime of every allocation clear, and is the Debug
   build leak-clean?
6. State plainly: what you tested, the results, and any baselines you updated.

## 7. Adding code

- **New file in an existing module:** add it to that module's `CMakeLists.txt`
  source list.
- **New module `foo`:**
  1. `engine/foo/include/foo/` (public headers) and `engine/foo/src/`.
  2. A `CMakeLists.txt` declaring `add_library(ge_foo STATIC ...)`,
     `target_include_directories(ge_foo PUBLIC include)`, and
     `target_link_libraries(ge_foo PUBLIC ...)` linking **only lower layers**.
  3. `add_subdirectory(engine/foo)` in the top-level `CMakeLists.txt`, placed in
     layer order.
- Keep the public-header / hidden-impl split (the `Window` pImpl pattern is the
  reference example).

## 8. Third-party libraries

- **Allowed** for solved, non-core problems: asset import (cgltf / assimp),
  image decode (stb_image), debug/editor UI (Dear ImGui), and the test framework
  (doctest / GoogleTest — see section 6).
- **Not allowed** to replace the from-scratch goals: the DX12 renderer and the
  physics solver are hand-written.
- Vendor under `third_party/` and wrap each behind an engine interface so it
  never leaks into subsystem public APIs.

## 9. How to work

1. Read `plan.md`; confirm the current phase and task. Open its design doc in
   `docs/phases/`; if it is still a stub, **expand it first** (scope, tech
   decisions, design, milestones, test plan) before writing code.
2. Stay within the current layer unless the task explicitly spans layers.
3. Implement, then write or update the tests that cover the change (the phase's
   critical tests are listed in `plan.md`).
4. Run `ctest`. Red or missing tests means the task is not finished — fix it
   before continuing.
5. If you made a cross-cutting decision, record it as an ADR in
   `docs/decisions/` (from `_TEMPLATE.md`), set its status, and link it from the
   phase doc and the `plan.md` decisions index.
6. Report: what changed, which files, the test results, and an explicit
   confirmation that the golden rules still hold.
7. When something is ambiguous or would violate a rule, ask or flag — don't
   guess.
8. When the deliverable is done by the section 5 definition (tests green
   included), update statuses: the phase-doc header, the `plan.md` Status log,
   and the module checklist.

## 10. Documentation map & upkeep

Planning and design are split by how often each part changes (full explanation
in `docs/README.md`):

- `plan.md` — light, scannable roadmap; the source of truth for scope and status.
- `agent.md` — this file: rules and conventions.
- `docs/phases/NN-*.md` — per-phase design depth, expanded just-in-time.
- `docs/decisions/NNNN-*.md` — append-only ADRs capturing the rationale for
  cross-cutting choices; indexed in `docs/decisions/README.md`.

Upkeep rules: expand a phase's design doc before coding that phase; record
cross-cutting decisions as ADRs and link them; keep each decision in exactly one
place (link, never duplicate); and update statuses in the phase doc and `plan.md`
as work lands.
