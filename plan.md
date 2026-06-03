# plan.md — MyEngine implementation plan

The single source of truth for scope, architecture, and what to build next.
Agents: confirm the current phase here before starting, and update the Status
log when a deliverable lands. Conventions and hard rules are in `agent.md`.

## Vision

A complete, modular, data-oriented game engine in C++20, for Windows, built in
Visual Studio via CMake. DirectX 12 graphics and the physics solver are written
from scratch. Free libraries are allowed only for solved, non-core problems.

## Guiding principles

- **Strict downward-only layering** (see Architecture). Enforced by the build.
- **Vertical slices over horizontal completion.** Keep something runnable at
  every step; get pixels on screen early, then deepen each system.
- **Data-oriented.** Cache-friendly layouts, batch processing, struct-of-arrays
  where it pays. ECS is the *world/gameplay* backbone only — not the whole
  engine.
- **Build tools early.** A lightweight profiler, debug-draw, and a CVar/console
  pay compound interest across every later phase.
- **The reflection / type system is the keystone.** One source of truth that
  feeds serialization, the editor, networking, and scripting. Design it early.
- **Networking reshapes data.** Replication touches the type system and ECS, so
  keep replication-awareness in mind even though networking is built late.
- **Test-gated phases.** A phase is done only when its critical tests (see
  "Critical tests by phase" below) are written and green, alongside the rest of
  the definition of done in `agent.md`. Tests are sized to risk, not to coverage
  numbers — heavy on deterministic logic, golden-image/baseline/smoke for the
  visual and interactive parts.

## Architecture (each layer depends only on those below it)

```
Application      game · editor · tools
Frameworks       world/ECS · scene · gameplay · UI/HUD
Subsystems       graphics (RHI + render graph) · physics · audio ·
                 animation · input · networking · AI      (no sideways deps)
Resources        runtime asset system (handles, streaming, hot reload)
                 + offline pipeline / cooker
Core foundation  allocators · math · containers · reflection/type system ·
                 jobs/threads · serialization · logging · strings ·
                 config/CVars · event bus
Platform         window · OS events · timers · file I/O · virtual memory ·
                 dynamic libs · threading primitives
```

## Module checklist (full scope)

Platform (Layer 0)
- [x] Window + message pump (Win32)
- [x] High-resolution timer
- [x] Virtual memory (VirtualAlloc wrappers)
- [ ] File I/O primitives
- [ ] Threading primitives
- [ ] Dynamic library loading

Core (Layer 1)
- [x] Primitive types + size macros
- [x] Logging
- [x] Asserts
- [x] Arena (linear) allocator
- [ ] Math library (vec2/3/4, mat4, quat, transforms)
- [ ] Allocator suite (pool, stack, general-purpose)
- [ ] Job / task system
- [ ] Containers (data-oriented: arrays, hash maps, handle pools)
- [ ] Reflection / type system  ← keystone
- [ ] Serialization
- [ ] String system
- [ ] Config / CVar system
- [ ] Event / message bus

Resources (Layer 2)
- [ ] Runtime asset system (handles, refcount, streaming, hot reload)
- [ ] Offline pipeline / cooker (DCC formats -> engine-native binary)

Subsystems (Layer 3)
- [ ] Graphics: DX12 RHI
- [ ] Graphics: render / frame graph
- [ ] Input
- [ ] Physics (from scratch)
- [ ] Audio
- [ ] Animation
- [ ] Networking / multiplayer
- [ ] AI

Frameworks (Layer 4)
- [ ] ECS / world management
- [ ] Scene
- [ ] Gameplay framework
- [ ] UI / HUD
- [ ] Scripting / gameplay-iteration layer

Application (Layer 5)
- [x] Sandbox test bed
- [ ] Editor / tools
- [ ] Profiling & debugging tools (start lightweight in Phase 2)

## Key technical decisions

- **Language:** C++20. No exceptions in core/hot paths; minimal RTTI (custom
  reflection instead of `typeid`/`dynamic_cast`).
- **Build:** CMake generating a VS solution; one static lib per module; jumbo /
  unity builds added later for compile time.
- **Type system mechanism:** annotation macros + a codegen step. Decide the
  concrete design in Phase 2 before serialization depends on it.
- **Scripting / iteration:** TBD — Lua vs custom VM vs hot-reloadable C++ DLLs.
  Decide before the gameplay framework (Phase 4).
- **Editor:** TBD — engine+editor vs engine-only. Until decided, design the type
  system *as if* an editor is coming (reflection, serialization, undo, hot
  reload).

## Roadmap

Every phase's DoD listed below also requires its **critical tests** (see the
table after the roadmap) to be green. A phase with red or missing tests is not
done, regardless of how complete the feature looks.

### Phase 0 — Foundation skeleton  [IN PROGRESS]
- Done: build scaffolding; platform (window, timer, memory); core (types, log,
  assert, arena); fixed-timestep game loop.
- Remaining: **math library** — vec2/3/4, mat4, quat, transform helpers, and the
  MVP-matrix utilities Phase 1 needs.
- DoD: window opens, clears to a color, closes cleanly; stable loop with precise
  timing; math has a smoke test.

### Phase 1 — First triangle (the DX12 gauntlet)
- Device, swapchain, command queue/list/allocators, fences, descriptor heaps,
  root signature, PSO, vertex buffer, draw call.
- Progression: triangle -> textured quad -> spinning cube driven by an MVP
  matrix from the math library.
- DoD: a spinning, textured cube at a stable, vsynced frame rate.

### Phase 2 — Harden the core
- Job/task system; full allocator suite (arena, pool, stack, general-purpose);
  reflection/type system; serialization; resource/handle system; file system
  abstraction; strings.
- Build early here: lightweight profiler, debug-draw, CVar/console.
- DoD: subsystems allocate via arenas/pools; types round-trip through
  serialization; the in-engine profiler shows per-frame timings.

### Phase 3 — Renderer proper
- Clean RHI abstraction; render/frame graph; mesh/material/texture resources;
  model loader (use cgltf/assimp for *import* only); camera; forward or deferred
  lighting; shadows.
- DoD: load and render a real 3D model with lighting and a fly-through camera.

### Phase 4 — World & gameplay
- ECS; scene/world management; fixed-update integration; input wired to
  components; scripting/iteration layer.
- DoD: a controllable entity in a rendered scene.

### Phase 5 — Physics from scratch
- Broadphase (sweep-and-prune or BVH) -> narrowphase (GJK/EPA or SAT) -> contact
  manifolds -> constraint solver (sequential impulses) -> integration; CCD
  later. Exposed via rigidbody/collider components.
- DoD: stacking boxes plus a physics-driven, controllable character.

### Phase 6 — Animation
- Skeletal animation with GPU skinning; clip sampling and looping; blend trees;
  animation state machines; root motion; inverse kinematics (later).
- DoD: a skinned character plays and smoothly blends clips (idle / walk / run)
  driven by gameplay state.

### Phase 7 — Audio
- Mixing graph; 3D spatialization and distance attenuation; streaming vs
  in-memory sounds; buses/groups (master / SFX / music); basic DSP (volume,
  pitch, reverb send). Backend from scratch (WASAPI) or via miniaudio.
- DoD: positional sound effects and music play in the scene, attenuated by
  distance and routed through separate buses.

### Phase 8 — UI / HUD
- Batched quad + text/font-atlas rendering; layout; input routing and focus;
  core widgets (panel, button, slider, text input); HUD anchoring. Dear ImGui
  for debug/editor UI; the game UI uses the engine's own system.
- DoD: an interactive in-game HUD and menu that respond to input, plus debug
  overlays.

### Phase 9 — AI
- Navmesh generation + A* pathfinding; steering and local avoidance; behavior
  trees and/or state machines; perception (line-of-sight via physics raycasts);
  a blackboard for shared state.
- DoD: agents navigate around obstacles and react to the player through a
  behavior tree.

### Phase 10 — Networking & multiplayer
- Transport (reliable + unreliable UDP); connection/session management;
  reflection-driven state replication; snapshot + delta compression;
  client-side prediction and server reconciliation; lag compensation; RPCs.
- DoD: two clients share an authoritative server simulation with smooth,
  predicted movement.
- Note: replication leans on the reflection/type system (Phase 2) and the ECS
  (Phase 4) — keep both replication-aware as they are built.

### Phase 11 — Asset pipeline & content cooker (offline)
- Robust importers for every content type; an asset build graph with dependency
  tracking and incremental, parallel cooking; conversion to engine-native
  binary; stable asset GUIDs/references; validation. Pairs with the runtime
  resource system from Phases 2–3, which has been growing alongside it.
- DoD: one command cooks a raw content folder into loadable engine assets,
  rebuilding only what changed.

### Phase 12 — Profiling, debugging & developer tooling
- Grow the lightweight Phase 2 tools into a CPU/GPU profiler with frame capture;
  memory tracking and leak detection; crash reporting/dumps; a mature console +
  CVar/config system; debug draw; and code/asset hot-reload.
- DoD: capture and inspect a frame's CPU/GPU timeline and memory usage live from
  inside the engine.

### Phase 13 — Editor & tools framework
- Scene editing with transform gizmos; reflection-driven property panels; asset
  browser; undo/redo; play-in-editor; serialization-backed save/load;
  multi-window docking (ImGui).
- DoD: build and save a playable scene entirely in the editor, then play it
  without leaving the app.

### Phase 14 — Vertical-slice game & shipping hardening
- Build a small but complete game with the engine as the forcing function;
  optimization passes (CPU, GPU, memory); Release/Shipping build configs;
  packaging and distribution; documentation.
- DoD: a shippable playable demo that runs on a clean machine from a packaged
  build.

## Critical tests by phase

The minimum that must be green to call each phase done. Mechanics (framework,
layout, the tiers, the self-review checklist) live in `agent.md` section 6.
Where a phase is inherently visual/interactive, the "tests" are golden-image,
simulation-baseline, smoke, or a recorded manual sign-off rather than unit
asserts — that is expected, not a gap.

| Phase | Critical tests |
|-------|----------------|
| 0 Foundation | **Unit:** vector/matrix ops; quaternion normalize + compose; `mat4` inverse; transform compose/decompose; MVP round-trip; arena alignment, exhaustion assert, reset reuse. **Smoke:** boot → N frames → clean shutdown, no leaks. |
| 1 First triangle | **Smoke:** device init, swapchain present for N frames, clean teardown, zero DX12 debug-layer errors. **Golden image:** the reference cube. |
| 2 Harden core | **Unit:** pool free-list integrity, stack LIFO, alignment across allocators; job system — all tasks complete + stress for races; serialization round-trip; reflection field enumeration; container correctness. |
| 3 Renderer | **Golden image:** reference scene(s). **Unit:** render-graph resource-lifetime / barrier assertions; mesh/material/texture load round-trip. |
| 4 World & gameplay | **Unit:** entity create/destroy; component add/remove; query iteration correctness; storage/archetype invariants; input-mapping resolution. |
| 5 Physics | **Unit:** collision math (GJK distance, SAT/AABB overlap). **Baseline:** a seeded box-stack settles to a recorded resting state within tolerance; penetration/restitution sanity. |
| 6 Animation | **Unit:** clip sampling at known times; blend weights sum to 1; pose/skeleton math; root-motion extraction. **Manual:** blend feel. |
| 7 Audio | **Unit:** mixing + attenuation-curve math; bus gain; buffer integrity. **Manual:** listening pass. |
| 8 UI / HUD | **Unit:** layout math; hit-testing and focus routing. **Manual:** look/feel review. |
| 9 AI | **Unit:** A* optimality on known graphs; navmesh-generation invariants; behavior-tree tick order; perception raycast results. |
| 10 Networking | **Unit:** (de)serialization + delta round-trip; out-of-order / packet-loss handling. **Baseline:** prediction + reconciliation converge; client/server determinism. |
| 11 Pipeline / cooker | **Unit/integration:** cook idempotency; incremental rebuild touches only changed assets; reference/GUID resolution; graceful handling of corrupt input. |
| 12 Tooling | **Unit:** profiler timings track known sleeps; memory tracker detects an injected leak; CVar parse/set round-trip. |
| 13 Editor | **Unit:** undo/redo round-trip restores state; save → load yields an equal scene; gizmo transform math. **Manual:** editing workflow. |
| 14 Vertical slice | **Smoke:** full game boot → play → quit on a clean machine. **Perf:** frame-time and memory budgets met. **Manual:** playtest sign-off. |

## Status log

- _(set date)_ Phase 0 skeleton committed: platform + core + sandbox loop.
- Next up: Phase 0 math library, plus its unit tests and the test harness
  (doctest + CTest) that the project will use from here on.
