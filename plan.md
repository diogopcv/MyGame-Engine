# plan.md — MyEngine implementation plan

The single source of truth for scope, architecture, and what to build next.
Agents: confirm the current phase here before starting, and update the Status
log when a deliverable lands. Conventions and hard rules are in `agent.md`.

This file stays high-level on purpose. Per-phase design detail lives in
`docs/phases/NN-*.md` (linked from each phase below), and the rationale for
cross-cutting decisions in `docs/decisions/` (ADRs). See `docs/README.md` for how
the docs fit together and the rules for keeping them current.

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
- [x] File I/O primitives
- [ ] Threading primitives
- [ ] Dynamic library loading

Core (Layer 1)
- [x] Primitive types + size macros
- [x] Logging
- [x] Asserts
- [x] Arena (linear) allocator
- [x] Math library (vec2/3/4, mat4, quat, transforms)
- [x] Allocator suite (pool, stack + Allocator interface)
- [x] Job / task system
- [x] Containers (Array<T>, HashMap<K,V>, HandleTable<T>)
- [x] Reflection / type system  ← keystone (ADR-0007 accepted)
- [x] Serialization
- [x] String system (StringId / FNV-1a)
- [x] Config / CVar system
- [ ] Event / message bus

Resources (Layer 2)
- [ ] Runtime asset system (handles, refcount, streaming, hot reload)
- [ ] Offline pipeline / cooker (DCC formats -> engine-native binary)

Subsystems (Layer 3)
- [x] Graphics: DX12 RHI (handle-based, render/frame graph, mesh/material/
      texture, model import, camera, forward lighting, shadow mapping)
- [x] Graphics: render / frame graph
- [x] Input (keyboard + mouse polling in Window; `ge_input` action/axis
      mapping on top, Phase 4)
- [x] Physics (from scratch): `ge_physics` — collision primitives + GJK,
      sweep-and-prune broadphase, contact manifolds, sequential-impulse
      solver + integration (`PhysicsWorld::step()`), sphere/box/capsule
      rigidbody shapes
- [ ] Audio
- [ ] Animation
- [ ] Networking / multiplayer
- [ ] AI

Frameworks (Layer 4)
- [x] ECS / world management (`ge_world`: sparse-set storage, ADR-0008)
- [x] Scene (entity roster + name registry + Transform save/load)
- [ ] Gameplay framework (beyond the Phase 4 movement system)
- [ ] UI / HUD
- [x] Scripting / gameplay-iteration layer (ADR-0009: plain C++ systems, not
      an interpreted/hot-reload layer — deliberately deferred, see ADR)

Application (Layer 5)
- [x] Sandbox test bed
- [ ] Editor / tools
- [ ] Profiling & debugging tools (start lightweight in Phase 2)

## Key technical decisions

Decisions with lasting impact are recorded as ADRs in `docs/decisions/` (full
index in `docs/decisions/README.md`). This list is just the pointer; the
rationale and consequences live in the records.

Accepted:
- ADR-0001 — Layered architecture & downward-only dependencies.
- ADR-0002 — Language, build & toolchain (C++20, CMake → VS, no exceptions in core).
- ADR-0003 — Testing strategy & framework (tiers + doctest + CTest).
- ADR-0004 — Memory management (arenas/pools over platform virtual memory).
- ADR-0005 — Math conventions (handedness, matrix storage, depth range).
- ADR-0006 — DX12 descriptor & binding model (classic root signature for now).
- ADR-0007 — Reflection / type-system mechanism (Phase 2).
- ADR-0008 — ECS storage model (Phase 4): sparse-set storage.
- ADR-0009 — Scripting / iteration model (Phase 4): plain compiled C++ systems.
- ADR-0010 — Renderer draw submission & generalized RHI binding layout
  (between Phases 5 and 6): per-frame `submit(ModelId, mat4)` draw list +
  upload-ring constants; `BindingLayout` replaces hardcoded root-signature
  variants.

Open:
- Editor scope: engine+editor vs engine-only (open) — until decided, design as
  if the editor is coming (reflection, serialization, undo, hot-reload).

## Roadmap

Every phase's DoD listed below also requires its **critical tests** (see the
table after the roadmap) to be green. A phase with red or missing tests is not
done, regardless of how complete the feature looks.

### Phase 0 — Foundation skeleton  [COMPLETE]
- Detail: [`docs/phases/00-foundation.md`](docs/phases/00-foundation.md)
- Done: build scaffolding; platform (window, timer, memory); core (types, log,
  assert, arena); fixed-timestep game loop; math library (vec2/3/4, mat4, quat,
  transform/projection helpers); doctest + CTest harness (41 tests, all green).
- DoD: window opens, clears to a color, closes cleanly; stable loop with precise
  timing; all critical math and arena tests pass.

### Phase 1 — First triangle (the DX12 gauntlet)  [COMPLETE]
- Detail: [`docs/phases/01-dx12-triangle.md`](docs/phases/01-dx12-triangle.md)
- Device, swapchain, command queue/list/allocators, fences, descriptor heaps,
  root signature, PSO, vertex buffer, draw call.
- Progression: triangle -> textured quad -> spinning cube driven by an MVP
  matrix from the math library.
- DoD: a spinning, textured cube at a stable, vsynced frame rate.

### Phase 2 — Harden the core  [COMPLETE]
- Detail: [`docs/phases/02-harden-core.md`](docs/phases/02-harden-core.md)
- Allocator suite (pool, stack, Allocator interface); containers (Array,
  HashMap); string system (StringId); file I/O; reflection/type system
  (ADR-0007); binary serialization; job system; resource/handle system;
  profiler; CVar system.
- DoD: subsystems allocate via arenas/pools; types round-trip through
  serialization; the in-engine profiler shows per-frame timings.

### Phase 3 — Renderer proper  [COMPLETE]
- Detail: [`docs/phases/03-renderer.md`](docs/phases/03-renderer.md)
- Clean RHI abstraction; render/frame graph; mesh/material/texture resources;
  model loader (use cgltf/assimp for *import* only); camera; forward or deferred
  lighting; shadows.
- DoD: load and render a real 3D model with lighting and a fly-through camera.

### Phase 4 — World & gameplay  [COMPLETE]
- Detail: [`docs/phases/04-world-gameplay.md`](docs/phases/04-world-gameplay.md)
- ECS; scene/world management; fixed-update integration; input wired to
  components; scripting/iteration layer.
- DoD: a controllable entity in a rendered scene.
- Watch item (carried from Phase 3): if the scene's world extent outgrows the
  single-frustum directional shadow, that's the trigger to build cascaded
  shadow maps in `ge_gfx` — not before. Not a hard deliverable of this phase.

### Phase 5 — Physics from scratch  [COMPLETE]
- Detail: [`docs/phases/05-physics.md`](docs/phases/05-physics.md)
- Broadphase (sweep-and-prune or BVH) -> narrowphase (GJK/EPA or SAT) -> contact
  manifolds -> constraint solver (sequential impulses) -> integration; CCD
  later. Exposed via rigidbody/collider components.
- DoD: stacking boxes plus a physics-driven, controllable character.
- Same cascaded-shadow watch item as Phase 4: a physics-scale level is another
  likely trigger point, whichever phase hits it first.

### Phase 6 — Animation
- Detail: [`docs/phases/06-animation.md`](docs/phases/06-animation.md)
- Skeletal animation with GPU skinning; clip sampling and looping; blend trees;
  animation state machines; root motion; inverse kinematics (later).
- DoD: a skinned character plays and smoothly blends clips (idle / walk / run)
  driven by gameplay state.

### Phase 7 — Audio
- Detail: [`docs/phases/07-audio.md`](docs/phases/07-audio.md)
- Mixing graph; 3D spatialization and distance attenuation; streaming vs
  in-memory sounds; buses/groups (master / SFX / music); basic DSP (volume,
  pitch, reverb send). Backend from scratch (WASAPI) or via miniaudio.
- DoD: positional sound effects and music play in the scene, attenuated by
  distance and routed through separate buses.

### Phase 8 — UI / HUD
- Detail: [`docs/phases/08-ui-hud.md`](docs/phases/08-ui-hud.md)
- Batched quad + text/font-atlas rendering; layout; input routing and focus;
  core widgets (panel, button, slider, text input); HUD anchoring. Dear ImGui
  for debug/editor UI; the game UI uses the engine's own system.
- DoD: an interactive in-game HUD and menu that respond to input, plus debug
  overlays.

### Phase 9 — AI
- Detail: [`docs/phases/09-ai.md`](docs/phases/09-ai.md)
- Navmesh generation + A* pathfinding; steering and local avoidance; behavior
  trees and/or state machines; perception (line-of-sight via physics raycasts);
  a blackboard for shared state.
- DoD: agents navigate around obstacles and react to the player through a
  behavior tree.

### Phase 10 — Networking & multiplayer
- Detail: [`docs/phases/10-networking.md`](docs/phases/10-networking.md)
- Transport (reliable + unreliable UDP); connection/session management;
  reflection-driven state replication; snapshot + delta compression;
  client-side prediction and server reconciliation; lag compensation; RPCs.
- DoD: two clients share an authoritative server simulation with smooth,
  predicted movement.
- Note: replication leans on the reflection/type system (Phase 2) and the ECS
  (Phase 4) — keep both replication-aware as they are built.

### Phase 11 — Asset pipeline & content cooker (offline)
- Detail: [`docs/phases/11-pipeline.md`](docs/phases/11-pipeline.md)
- Robust importers for every content type; an asset build graph with dependency
  tracking and incremental, parallel cooking; conversion to engine-native
  binary; stable asset GUIDs/references; validation. Pairs with the runtime
  resource system from Phases 2–3, which has been growing alongside it.
- Carried over from Phase 3 (gfx): HDR-cubemap IBL prefiltering (equirect/
  cubemap import → prefiltered radiance mips + irradiance map + BRDF LUT),
  baked offline through this phase's build graph. See
  `docs/phases/03-renderer.md` Addendum 2.
- DoD: one command cooks a raw content folder into loadable engine assets,
  rebuilding only what changed.

### Phase 12 — Profiling, debugging & developer tooling
- Detail: [`docs/phases/12-tooling.md`](docs/phases/12-tooling.md)
- Grow the lightweight Phase 2 tools into a CPU/GPU profiler with frame capture;
  memory tracking and leak detection; crash reporting/dumps; a mature console +
  CVar/config system; debug draw; and code/asset hot-reload.
- DoD: capture and inspect a frame's CPU/GPU timeline and memory usage live from
  inside the engine.

### Phase 13 — Editor & tools framework
- Detail: [`docs/phases/13-editor.md`](docs/phases/13-editor.md)
- Scene editing with transform gizmos; reflection-driven property panels; asset
  browser; undo/redo; play-in-editor; serialization-backed save/load;
  multi-window docking (ImGui).
- DoD: build and save a playable scene entirely in the editor, then play it
  without leaving the app.

### Phase 14 — Vertical-slice game & shipping hardening
- Detail: [`docs/phases/14-vertical-slice.md`](docs/phases/14-vertical-slice.md)
- Build a small but complete game with the engine as the forcing function;
  optimization passes (CPU, GPU, memory); Release/Shipping build configs;
  packaging and distribution; documentation.
- Graphics-polish pass carried over from Phase 3 (gfx): soft shadows, ambient
  occlusion, and a post-processing stack (tone mapping is already in place from
  the PBR addendum). Order-independent transparency (OIT) only if the vertical-
  slice scene actually exposes a transparency-sorting failure — no speculative
  build. See `docs/phases/03-renderer.md` Out of scope notes.
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

## Backlog / stretch goals (not phase-scheduled)

Items intentionally deferred with no fixed phase, because they're conditional
on a requirement the project doesn't currently have. Listed here so they're not
lost, but they do not block any phase above.

- **Vulkan / multi-backend renderer** (from Phase 3 scope). The engine targets
  Windows/DX12 only (see `CLAUDE.md`); there's no cross-platform requirement
  driving this today. If one ever appears, do it *after* the RHI surface stops
  churning (post Phase 6 or so) — adding a second backend earlier means every
  subsequent RHI change has to be mirrored twice. Until then, this stays
  unscheduled by design, not by oversight.

## Status log

- _(set date)_ Phase 0 skeleton committed: platform + core + sandbox loop.
- _(set date)_ Docs structure added: `docs/phases/` (per-phase design) and
  `docs/decisions/` (ADRs 0001–0009); plan/agent files point to them.
- 2026-06-08 Phase 0 complete: math library (vec2/3/4, mat4, quat, transform,
  perspective/look-at), doctest + CTest harness, 41 unit tests all green.
- 2026-06-09 Phase 1 Milestone 1 done: `ge_gfx` module (DX12 device, command
  queue, flip-model swapchain, RTV heap, fence sync, double buffering),
  sandbox clears to a color every frame, smoke test (60 frames, zero debug-
  layer errors) green. ADR-0006 accepted (classic root signature).
- 2026-06-09 Phase 1 Milestone 2 done: first triangle. HLSL shaders compiled
  offline via DXC (CMake custom command), empty root signature, PSO with a
  position+color input layout, upload-heap vertex buffer, drawn every frame.
  Smoke test green (60 frames, zero debug-layer errors).
- 2026-06-09 Phase 1 Milestone 3 done: textured quad. Root signature extended
  with a one-SRV descriptor table + static sampler; quad as vertex+index
  buffers; procedural 4x4 checkerboard texture uploaded to a default-heap
  resource via an upload-heap staging buffer and SRV in a shader-visible
  heap. Smoke test green (60 frames, zero debug-layer errors).
- 2026-06-09 Phase 1 Milestone 4 done (Phase 1 complete): spinning textured
  cube. Root signature gained a root CBV (b0, vertex-visible) backing
  per-frame MVP constant buffers; cube geometry (24 verts/36 indices) reuses
  the checkerboard texture; D32_FLOAT depth buffer with depth test/write in
  the PSO; `cube_{vs,ps}.hlsl` apply `mvp = model(t) * view * proj`.
  `Renderer::begin_frame(dt)` drives the spin; `capture_back_buffer()` +
  vendored stb_image/stb_image_write back a new golden-image CTest (90
  frames @ fixed dt vs. `engine/gfx/tests/golden/cube.png`). Smoke + golden
  tests green, zero debug-layer errors.
- 2026-06-17 Phase 2 complete: allocator suite (pool, stack, Allocator
  function-pointer interface), containers (Array<T>, HashMap<K,V>), string
  system (StringId/FNV-1a), platform file I/O, reflection (ADR-0007 accepted:
  annotation macros, inline registration), binary serialization with round-trip,
  job system (thread pool + counter sync), resource/handle system (Handle<Tag>
  + HandleTable with generation tracking), profiler (GE_PROFILE_SCOPE macro,
  ring buffer), CVar system. 109 core + 4 platform tests, all green. Profiler
  wired into sandbox main loop with per-frame timing output.
- 2026-06-17 Doxygen API documentation added to all 26 public header files
  across platform (4), core (17), and gfx (1) modules plus the 4 remaining
  core math headers. Build clean, all tests green.
- 2026-06-18 Phase 3 Milestone 1 done: RHI abstraction. Handle-based
  RhiDevice wrapping DX12 backend with HandleTables for buffers, textures,
  shaders, pipelines. 4MB arena-backed descriptor heaps. Root signature with
  2 CBVs + 2 SRV tables + 2 static samplers (configurable s0 filter per
  pipeline). Renderer refactored to use RhiDevice. All tests green.
- 2026-06-18 Phase 3 Milestone 2 done: Render/frame graph. RenderGraph class
  with compile-time barrier insertion, pass execution callbacks. 5 unit tests
  (barrier insertion, state tracking, multi-pass). All tests green.
- 2026-06-18 Phase 3 Milestone 3 done: Mesh/material/texture + model import.
  LitVertex (pos+normal+uv), SubMesh, Mesh, Material types. cgltf vendored
  for glTF 2.0 parsing. Model loader extracts PBR base_color, images via
  stb_image, merged vertex/index GPU buffers. Model pipeline with model_vs/ps
  shaders (per-frame view/proj + per-object model/normal CBs, N·L directional
  lighting). Smoke test loads and renders box.glb for 30 frames, 0 debug
  errors. gen_box_glb.cpp tool creates test models. All tests green.
- 2026-06-18 Phase 3 Milestone 4 done: Camera + forward lighting. Camera
  struct with Euler angles, perspective projection, fly-through controller.
  Platform Window extended with keyboard (KeyCode enum matching VK_*),
  mouse buttons, mouse delta, and raw mouse capture (RAWINPUT). Blinn-Phong
  shaders (directional + up to 4 point lights, specular highlights).
  Expanded per-frame CB (384 bytes: view/proj/camera_pos/sun/point_lights/
  light_view_proj). Sandbox: WASD + mouse-look (right-click to capture),
  Shift for speed, Escape to quit, two colored point lights. 5 camera unit
  tests + 1 window input test + model smoke test all green, 0 debug errors.
- 2026-06-18 Phase 3 Milestone 5 done (Phase 3 complete): Shadow mapping.
  2048x2048 D32_FLOAT shadow map with DepthStencil|ShaderResource (R32_TYPELESS
  resource, typed DSV/SRV views). Depth-only shadow pipeline (shadow_vs, no PS,
  depth bias 1000 + slope 1.0). Orthographic light projection (mat4_ortho_lh).
  Shadow pass added to render graph before model pass, automatic
  DepthWrite→PixelShaderResource barrier. 3x3 PCF in model_ps via
  SamplerComparisonState. shadow_enabled flag in per-frame CB. RHI extended:
  optional PS in pipeline creation, configurable depth bias. Ortho projection
  unit test + all existing tests green, 0 debug errors.
- 2026-06-29 PBR renderer upgrade (Phase 3 addendum): Cook-Torrance BRDF
  (GGX/D, Smith/G, Schlick/F), tangent-space normal maps, metallic-roughness
  workflow. New `RootLayout::PBR` root signature (3 CBVs + 6 SRV tables).
  `LitVertex` gains `tangent[4]` (stride 32 → 48 bytes). Model loader now
  extracts tangents and all five PBR maps (base color, metallic-roughness,
  normal, occlusion, emissive). `pbr_vs.hlsl` + `pbr_ps.hlsl` added.
  Sandbox loads DamagedHelmet.glb as the PBR reference scene.
  `Renderer::set_model_transform(const mat4&)` added so callers can set a
  per-frame model-to-world matrix (used by shadow pass, PBR pass, and Blinn-
  Phong pass). Dedicated PBR smoke test added: loads DamagedHelmet.glb,
  renders 300 frames (~5 s at 60 fps) with white directional + white fill
  light, model rotating via `mat4_rotation_y(time)`, asserts 0 DX12 debug
  errors. Fixed `SetBreakOnSeverity(ERROR, TRUE)` crash in headless CTest:
  the INT3 breakpoint fires an unhandled SEH when no debugger is attached;
  gated it on `IsDebuggerPresent()` so it only fires in interactive
  debugging. All 3 CTest suites green (14 gfx test cases, 0 debug errors).
- 2026-06-30 glTF completeness & transparency (Phase 3 addendum 2, complete):
  closed loader-fallback and PBR-transparency gaps found in an audit.
  Loader — node-hierarchy/instancing baking (`cgltf_node_transform_world`),
  flat-normal generation, UV-derived tangent generation, inverse-transpose
  normal matrices, second UV set + vertex colors, `alpha_mode`/`alpha_cutoff`/
  `double_sided`, `normal_texture.scale`/`occlusion.strength`/
  `KHR_materials_emissive_strength`, `KHR_texture_transform` + per-texture
  sampler wrap. PBR — blend state in the RHI/PSO, cull×blend PSO variants,
  alpha-test for MASK, back-to-front transparent sorting, and analytic
  procedural environment IBL (hemispheric diffuse + Karis analytic EnvBRDF
  specular). HDR-cubemap IBL and OIT documented as remaining gaps. See
  `docs/phases/03-renderer.md` (Addendum 2).
- 2026-07-01 Phase 4 complete: World & gameplay. ADR-0008 accepted
  (sparse-set ECS storage: `Entity` reuses `core/handle.h`'s
  `HandleTable<u8, EntityTag>` for lifecycle; `ComponentPool<T>` is a dense
  `Array<T>` + sparse index array with O(1) add/remove; `World::each<T>` /
  `each<A,B>` are the query API — no archetype/query cache). ADR-0009
  accepted (plain compiled C++ systems for now — no scripting language or
  hot-reload DLL boundary yet). New `ge_world` module (Milestone 1: ECS core;
  Milestone 2: `Scene` wraps a `World` + name registry, `save`/`load`
  round-trips `Transform` through Phase 2's reflection/serialization). New
  `ge_input` module (Milestone 1b: `InputMap` binds named actions/axes to
  `platform::KeyCode`, `resolve()` templated over the key source so mapping
  logic is unit-testable without a real OS window). Sandbox (Milestone 3+4):
  the DamagedHelmet is now a `Scene`-owned "player" entity with `Transform` +
  `CharacterController`; a fixed-update `system_move_player` free function
  reads `InputMap` axes and updates its Transform; the free-fly camera is
  replaced by a chase camera derived each frame from the entity's transform
  and look angles. 21 new unit tests (12 world entity/component/query + 4
  scene save-load + 5 input-mapping resolution), all green; full CTest suite
  (5 executables) green, 0 DX12 debug-layer errors, sandbox smoke-run clean.
- 2026-07-01 Phase 5 Milestone 1 done: collision primitives + narrowphase
  math. New `ge_physics` module (Layer 3, depends only on `ge_core` — no
  sideways dependency on `ge_world`/`ge_gfx`, per ADR-0001). `shapes.h`:
  `AABB`, `Sphere`, `Box` (OBB: center + half-extents + quaternion
  orientation) plus AABB helpers (`aabb_union`, `aabb_from_sphere`,
  `aabb_from_box`). `collision.h/.cpp`: `aabb_overlap`; `sphere_sphere` and
  `sphere_box` closest-point tests with normal + penetration depth (the
  sphere_box degenerate/embedded-center case falls back to the axis of
  least penetration); `box_box` via the 15-axis SAT (3+3 face normals, 9
  edge cross-products), tracking the minimum-overlap axis as the
  minimum-translation-vector normal. `gjk.h/.cpp`: support-function-based
  GJK distance query (`support_sphere`, `support_box`) using Ericson-style
  closest-point-to-origin simplex reduction (segment/triangle/tetrahedron
  Voronoi-region cases) for the general convex-vs-convex separated-distance
  case plan.md's critical-test line calls for. `Contact::normal` convention
  fixed as "points from the first shape argument to the second" throughout.
  24 unit tests (54 assertions): AABB overlap incl. touching; sphere-sphere
  incl. coincident-center degenerate case; sphere-box incl. a rotated-box
  corner case and an embedded-center fallback case; box-box incl. touching/
  overlapping/separated axis-aligned and single-axis-rotated cases, plus a
  cross-validation test comparing SAT's boolean outcome against GJK across
  compound-rotated configurations (the two algorithms are independent code
  paths, so agreement is meaningful evidence — hand-deriving exact
  penetration depths for compound rotations by hand was judged too
  error-prone to hardcode directly); GJK distance tests for sphere-sphere,
  sphere-box, and box-box (incl. a diagonal corner-to-corner case), all
  cross-checked against closed-form geometry. Full CTest suite (6
  executables) green, 0 warnings at `/W4 /permissive-`.
- 2026-07-01 Phase 5 Milestone 2 done: broadphase + pair generation.
  `broadphase.h/.cpp`: `Broadphase` class (`Proxy` = caller id + `AABB`,
  `Pair` = confirmed-overlapping id pair) doing a fresh sort-and-sweep each
  step — sorts proxies by AABB `min.x` (`std::sort`, same pattern already
  used for transparent-object sorting in `ge_gfx`), then sweeps forward with
  the standard early-out (`min.x[j] > max.x[i] -> break`), confirming true
  3D overlap via `aabb_overlap` for every X-interval candidate before
  emitting a pair. 6 unit tests (empty/no-pairs/one-pair/clear() cases plus
  an X-overlaps-but-Y-separated regression case for the "X overlap alone
  isn't 3D overlap" bug class), plus a cross-check against a brute-force
  O(n^2) `aabb_overlap` reference over a 10-box mixed scene (dense
  overlapping cluster + isolated singletons + an X/Z-overlapping-but-
  Y-separated pair), comparing the full sorted pair sets for exact equality
  rather than spot-checking individual pairs. Full CTest suite (6
  executables, 30 physics test cases / 62 assertions) green, 0 warnings at
  `/W4 /permissive-`.
- 2026-07-01 Phase 5 Milestone 3 done: contact manifold generation.
  Refactored `box_box()`'s 15-axis SAT loop into a shared private
  `detail::box_box_sat` (`engine/physics/src/sat_internal.h`, not a public
  header) that also classifies which axis won (face of A, face of B, or one
  of the 9 edge cross-products) — `collision.h`'s `box_box()` is now a thin
  wrapper over it, so the public single-normal API is unchanged and its
  existing tests kept passing through the refactor untouched. New
  `manifold.h/.cpp`: `Manifold` (up to 4 `ManifoldPoint`s: world position +
  per-point penetration). `manifold_sphere_sphere`/`manifold_sphere_box` are
  single-point (surface midpoint along the narrowphase normal).
  `manifold_box_box` branches on the winning SAT axis: face contacts pick a
  reference face (the box owning the axis) and an incident face (the other
  box's face most anti-parallel to the reference normal), then clip the
  incident face's 4 corners against the reference face's 4 side planes
  (Sutherland-Hodgman), keeping points that land behind the reference plane
  (>4 clipped points falls back to keeping the 4 deepest rather than a full
  area-maximizing reduction — noted as a possible revisit if the box-stack
  baseline test needs better spread). Edge contacts compute the closest
  points between the two SAT-identified witness edges (Ericson-style
  clamped segment-segment closest point) and reduce to their midpoint. 13
  new unit tests: sphere manifolds' exact surface-midpoint positions; two
  hand-derived box-box face-contact cases (a flush 4-corner resting contact
  and a sideways-shifted one where clipping must trim to the true overlap
  footprint) with exact expected corner positions and penetration depths;
  and an invariant-based test (valid manifold, 1-4 points, unit normal,
  non-negative penetration) across axis-aligned/single-axis/compound-
  rotated configurations reused from Milestone 1's SAT/GJK cross-validation
  set, since hand-deriving exact contact geometry for compound rotations
  was judged too error-prone to hardcode (same reasoning as Milestone 1).
  Full CTest suite (6 executables, 37 physics test cases / 128 assertions)
  green, 0 warnings at `/W4 /permissive-`.
- 2026-07-01 Phase 5 Milestone 4 done: constraint solver + integration.
  New `rigidbody.h`: `RigidBody` (shape + transform + velocities + inverse
  mass/inertia + material), `make_sphere_body`/`make_box_body` factories
  computing solid-shape inertia tensors (`inv_mass == 0` marks static), and
  `apply_inv_inertia()` (rotates a torque-like vector into body space via
  the same "world axes = rows of quat_to_matrix" convention used throughout
  `ge_physics`, scales by the diagonal inverse tensor, rotates back). New
  `solver.h/.cpp`: `ContactConstraint` + `prepare_constraints()` (lever
  arms, tangent basis, effective masses, restitution/Baumgarte velocity
  bias — precomputed once per step since positions don't change during
  velocity iteration) + `solve_velocity_constraints()` (sequential-impulse
  passes: non-penetration normal impulse clamped >=0, two independent
  tangential friction impulses clamped to a box approximation of the
  Coulomb cone). New `physics_world.h/.cpp`: `PhysicsWorld::step(dt)` wires
  gravity -> broadphase -> narrowphase/manifold dispatch (by shape-type
  pair) -> constraint building -> solve -> semi-implicit Euler integration
  (linear position directly; angular via quaternion integration, `dq =
  orientation * spin_quat` — orientation applied first per this engine's
  documented "apply p first, then q" `quat` operator* / row-vector `mat4`
  convention, *not* the more commonly-seen `spin * orientation` ordering).
  16 new unit tests: solver-level exact hand-derived outcomes (equal-mass
  elastic collision exchanges velocities, inelastic collision stops both
  bodies at the momentum-weighted average, a static body is never moved,
  Baumgarte bias on an at-rest penetrating pair matches the closed-form
  bias formula) using head-on sphere-sphere constraints where the contact
  point lies on the line through both centers (r parallel to the normal,
  so every impulse is provably non-rotational — isolates the linear-
  collision math from manifold/torque complexity); PhysicsWorld-level
  tests: free-fall matches the semi-implicit Euler recurrence, a static
  body never moves, angular-velocity integration direction cross-checked
  against `quat_from_axis_angle` (this is what caught and fixed a
  backwards composition order before it shipped — see below), and three
  simulation-baseline settling tests (dropped sphere and dropped box each
  come to rest on static ground within 0.05 units of the analytic contact
  height and near-zero residual velocity after 5s; a 2-box stack settles
  to both expected resting heights within 0.1 units after 6s) — the last
  one is the literal Phase 5 DoD scenario (stacking boxes) and is the
  strongest end-to-end validation yet that broadphase, manifold generation,
  and the solver are all correctly wired together. Full CTest suite (6
  executables, 47 physics test cases / 152 assertions) green, 0 warnings
  at `/W4 /permissive-`.
  Process note: derived the quaternion angular-velocity integration order
  by hand from the row-vector/`operator*` conventions (documented in the
  commit above), but treated that derivation as a hypothesis, not a given —
  wrote the `quat_from_axis_angle` cross-check test specifically to verify
  it empirically before trusting it, since composition-order bugs are easy
  to get backwards and hard to notice without a targeted test (the test
  passed on the first run, confirming the derivation).
- 2026-07-01 Phase 5 Milestone 5 done (**Phase 5 complete**): rigidbody/
  collider ECS components + system glue; physics-driven controllable
  character. Added `Capsule` (segment + radius) to `shapes.h`, `capsule_box`
  narrowphase to `collision.h/.cpp` (reused a new shared `point_radius_vs_box`
  helper factored out of `sphere_box`'s existing logic — the closest point on
  the capsule's core segment stands in for a sphere center), `support_capsule`
  to `gjk.h/.cpp`, and `manifold_capsule_box` to `manifold.h/.cpp`. New
  private `src/closest_point_internal.h`: `closest_point_on_box`,
  `closest_point_on_segment`, and `closest_points_segment_box` (alternating
  projection between the two convex sets — segment and box — converges to
  the true closest-point pair in a handful of iterations; this is new,
  unverified-by-precedent code, so it was cross-validated against GJK using
  `support_capsule`/`support_box`, the same strategy Milestone 1 used for
  `box_box()`'s compound-rotation cases). `RigidBody` gained
  `ShapeType::Capsule` + `capsule_half_height` + `make_capsule_body()` —
  always rotation-locked (`inv_inertia_local = 0`), a deliberate character-
  controller simplification: a player capsule shouldn't tip over from solver
  torques, so its facing is driven directly by input instead (see below).
  `physics_world.cpp`'s shape-pair dispatch now covers capsule-box (both
  orderings); capsule-sphere/capsule-capsule are undispatched by design (no
  such pair exists in this scene) rather than guessed at.
  ECS side: `world::components.h` gained `RigidBodyRef { u32 body_id }` — a
  bare opaque index, not a copy of or pointer into any `ge::physics` type, so
  `ge_world` still has zero dependency on `ge_physics` (ADR-0001 would permit
  it, since Layer 4 may depend on Layer 3, but nothing in the ECS itself
  needs to). The system functions that resolve that id against a
  `PhysicsWorld` live in the sandbox, not `ge_world` — `system_move_player_
  physics()` (sets yaw-relative horizontal velocity from WASD, leaves
  vertical velocity to gravity/the solver, sets facing directly from yaw
  since rotation is locked) and `system_sync_transform_from_rigidbody()`
  (copies the post-`step()` RigidBody state into Transform for rendering) —
  exactly where `system_move_player` already lived in Phase 4, since the
  sandbox is the one place that legitimately owns both a `World` and a
  `PhysicsWorld`. Sandbox: added an invisible static ground box (no renderer
  support for primitive meshes yet — a Phase 3/13 concern, not Phase 5's);
  the player is now a capsule (radius 0.4, half-height 0.5) dropped at y=3,
  replacing the old free-fly Transform-writing controller; removed the
  MoveUp (E/Q) binding since vertical motion is physics-owned now.
  9 new unit tests (capsule_box hand-derived standing/separated/flush cases
  plus GJK cross-validation across tilted configurations; manifold_
  capsule_box surface-midpoint; a PhysicsWorld baseline settling test for a
  dropped character capsule). Full CTest suite (6 executables, 54 physics
  test cases / 178 assertions) green, 0 warnings at `/W4 /permissive-`.
  Verification went beyond unit tests: launched the actual sandbox.exe and
  read its console log — the capsule (dropped from y=3) settled at
  `player.y: 0.89` against a hand-computed expected resting height of
  exactly 0.9 (ground top 0 + radius 0.4 + half-height 0.5), ran cleanly for
  ~27 seconds with zero DX12 debug-layer errors, and exited cleanly. This is
  the strongest evidence yet that broadphase, capsule-box narrowphase/
  manifold, the solver, and the ECS sync systems are all correctly wired
  together in the real application, not just in isolated tests.
  No new ADR: this milestone extended the established Layer-3-subsystem
  pattern (shapes/collision/gjk/manifold/rigidbody, same shape as Milestones
  1-4) rather than introducing a new cross-cutting architectural decision;
  the `RigidBodyRef`-as-opaque-handle placement decision is a direct
  continuation of ADR-0009's "plain compiled C++ systems, glue lives with
  whichever code owns both sides" precedent, not a new one.
- 2026-07-09 Renderer architecture hardening (pre-Phase-6, ADR-0010 accepted):
  multi-model draw-submission API (`load_model` returns a `ModelId`; per-frame
  `submit(ModelId, mat4)` between `begin_frame`/`end_frame`; up to 16 loaded
  models with per-model material CBs and lazily-built per-sampler-address PBR
  PSO blocks). All dynamic constants now come from a per-frame-in-flight
  persistently-mapped upload ring (256-aligned bump allocation, new
  `RhiDevice::set_constant_buffer(slot, buffer, offset)`), fixing two latent
  bugs: per-object CB last-write-wins between the shadow and PBR passes, and
  frame slots being derived from a HandleTable index (`back_buffer().index %
  2`) instead of the new `RhiDevice::frame_index()`. RHI: `RootLayout` enum
  replaced by a generic `BindingLayout` (CBV + SRV-table counts) with a
  root-signature dedupe cache; non-CPU-visible buffers moved to the DEFAULT
  heap with staged uploads on a dedicated upload command list (also used for
  texture uploads/readback, so resource creation can't clobber the frame
  command list); swapchain descriptors use reserved heap indices so resize
  can't collide with live user RTs/DSVs; GE_ASSERTs on invalid handles in
  command recording. Platform: new `platform::executable_dir()` (UTF-8);
  `renderer_dx12.cpp` renamed `renderer.cpp` — no `<windows.h>`/D3D usage
  outside `rhi_dx12.cpp` in ge_gfx anymore (sandbox and gfx tests also
  dropped their Win32 includes). Retired the Phase-3 Blinn-Phong fallback
  path (`model_vs/ps.hlsl` deleted); PBR is the model path, the cube remains
  the empty-scene/golden baseline. New multi-instance/multi-model smoke test
  (5 spinning boxes + helmet in one frame). Verified: full CTest suite (6
  executables) green including the unchanged golden cube image; sandbox run
  ~8 s — helmet renders, capsule settles at player.y 0.89, ~100 FPS, clean
  exit, 0 debug-layer errors.
- 2026-07-09 Phase 6 design doc expanded (`docs/phases/06-animation.md`):
  new `ge_anim` Layer 3 module plan (skeleton/clip/pose/state-machine/root
  motion, depends on `ge_core` only), sibling-isolation contract with
  `ge_gfx` (palettes cross as bare `mat4*` via the app, joint order = glTF
  skin order), GPU-skinning decisions on top of ADR-0010 (`SkinnedVertex`,
  `BindingLayout{4,6}`, palette from the upload ring, skinned shadow pass),
  5 milestones, test plan, and the rigged-asset plan (gen_skinned_glb tool +
  Khronos Fox).
- 2026-07-14 Phase 6 Milestone 1 done: animation core + sampling math (no
  rendering). New `ge_anim` module (Layer 3, depends only on `ge_core` —
  never sideways on `ge_gfx`/`ge_world`, per the phase doc's sibling-isolation
  contract). `skeleton.h`: `Skeleton` (parent-index array with the
  parents-precede-children invariant asserted, per-joint inverse bind
  matrices, local-space rest pose) + `JointPose` (TRS — cheaper to blend than
  matrices, slerp stays on the rotation manifold) + `kMaxJoints = 256` (u8
  GPU joint indices). `clip.h/.cpp`: `AnimationClip` — raw per-joint SoA
  keyframe tracks (times + values per channel; empty track = channel falls
  back to the skeleton rest pose), clip-owned track storage via
  `alloc_*_keys()` helpers; `sample()` with binary-search keyframe lookup,
  vec3 lerp / quat slerp interpolation, looping wrap via fmod (negative
  times wrap forward) vs. non-looping clamp, and first/last-key clamping
  inside a track. `pose.h/.cpp`: `Pose` (local TRS array); `blend(a,b,t)`
  (weights sum to 1 by construction); `blend_1d_weights()` (the 1D locomotion
  blend's pure weight math, ahead of Milestone 4's state machine per the test
  plan: zero except the two bracketing anchors, clamped outside the range);
  `pose_to_model_space()` (single forward pass, `model[j] = local[j] *
  model[parent]` in the engine's row-vector convention) and
  `pose_to_palette()` (`palette[j] = inverse_bind[j] * model[j]`, so the
  rest pose yields identity palettes). Added `lerp(vec3, vec3, f32)` to
  `core/vec.h` (the one math primitive blending needed that core lacked).
  11 unit tests / 300 assertions: exact keyframe hits, mid-key lerp/slerp
  (slerp half of 90° = exact 45° axis-angle), loop wraparound incl. negative
  time, non-looping clamp, rest-pose fallback for untracked channels/joints,
  single-key constant tracks; 1D blend weights at/between/outside anchors +
  sum-to-1 sweep; hand-computed 2-bone chain (translate + rotate) through
  local→model→palette incl. the rest-pose⇒identity-palette round trip and
  skinning bind-pose vertices to hand-derived animated positions. Full CTest
  suite (7 executables) green, 0 warnings at `/W4 /permissive-`.
- 2026-07-14 Phase 6 Milestone 2 done: glTF import + test assets.
  `ge_anim` side — new `animation_loader.h/.cpp` (cgltf linked PRIVATE, per
  the third-party wrapping rule): `AnimationSet` (Skeleton + clips + a
  clip-name table with `find_clip()`), `load_animation_gltf()` imports skin
  0's joint hierarchy (validating the parents-precede-children invariant
  `pose_to_model_space` relies on, and the <=256-joint cap), rest pose from
  joint-node TRS (matrix nodes decomposed), IBMs converted column-major →
  row-vector (same transpose-copy as the gfx loader), and every animation's
  translation/rotation/scale channels into SoA tracks (LINEAR; STEP read as
  linear; CUBICSPLINE skipped with a warning; morph weights out of scope).
  `ge_gfx` side — `SkinnedVertex` (LitVertex prefix + u8 joints[4] +
  f32 weights[4], stride 92, static_assert'd): the prefix keeps static-path
  attribute offsets valid so non-skinned pipelines can draw a skinned buffer
  (bind pose) with the larger stride. Loader reads JOINTS_0/WEIGHTS_0
  (u8/u16 via read_uint, >255 rejected), skips node-transform baking for
  skinned nodes (per glTF the skinned node's transform is ignored — the test
  asset carries a deliberate translation(5,0,0) to catch regressions),
  interleaves and uploads SkinnedVertex when any primitive is skinned;
  `Mesh::skinned` + `vertex_stride` now drive the renderer's vertex binding
  (two hardcoded `sizeof(LitVertex)` strides fixed). New optional `ModelCpu`
  out-param returns the merged CPU-side vertex/index copy for tests/tooling
  without a GPU readback. Assets — new `tools/gen_skinned_glb.cpp`
  (deterministic 2-bone strip: 3 weight rows, IBMs, "bend" 90°-about-Z
  rotation clip + "move" (0,0,0)→(2,0,0) root-translation clip for
  Milestone 5); vendored Khronos Fox.glb (CC-BY 4.0, same situation as
  DamagedHelmet; verified parents-first joints + Survey/Walk/Run clips).
  Tests — anim: strip import round-trip against the generator's exact
  values (parents/rest/IBMs/key times+values), end-to-end sample→palette
  skinning a bind vertex to the hand-computed (-1,1,0), no-skin file
  rejected, Fox sanity (3 clips by name, unit rotations across all joints);
  gfx: strip SkinnedVertex joints/weights/bind-positions exact, box.glb
  still LitVertex, Fox skinned with normalized weights. 15 anim cases (440
  assertions) + 2 new gfx cases; full CTest suite (7 executables) green
  incl. the unchanged golden cube, 0 warnings at `/W4 /permissive-`.
- 2026-07-14 Phase 6 Milestone 3 done: GPU skinning render path. RHI —
  `Format::R8G8B8A8_UINT` (joint-index input element; the one RHI addition
  the phase doc predicted). Shaders — `pbr_skinned_vs.hlsl` (bone palette as
  a 256-entry root-CBV cbuffer at b3; linear blend skinning of position +
  normal + tangent, then the identical world/view/proj path; pixel shader
  shared with the static PBR pipeline) and `shadow_skinned_vs.hlsl` (palette
  at b2 — the depth-only root signature has no PerMaterial slot, so
  `BindingLayout{3,2}` instead of the sketched b3; same ring allocation, a
  deliberate small deviation noted in the phase doc header). Renderer —
  skinned PBR PSO block `[address][cull][blend]` mirroring the static 2×2,
  built lazily by the first skinned model per address mode
  (`BindingLayout{4,6}`, so SRV table slots shift to 4..9 on skinned draws);
  skinned shadow pipeline built with the static one; new
  `Renderer::submit_skinned(id, transform, palette, joint_count)` copies the
  palette into the per-frame upload ring at submit time (caller may reuse
  its array immediately; one allocation feeds both the shadow and PBR
  passes); shadow pass now switches pipelines per item and rebinds b0 on
  root-signature changes; a skinned mesh submitted through plain `submit()`
  deliberately falls back to the static PSO and draws its bind pose (the
  SkinnedVertex LitVertex-prefix makes this valid). Sandbox — links
  `ge_anim`; loads Fox.glb twice (mesh via renderer, skeleton+clips via
  ge_anim — the sibling-isolation contract in action), primes the palette at
  load, advances the looping Walk clip on the fixed timestep (same
  accumulator as physics per the phase doc), rebuilds pose→model→palette per
  tick, and submits with a cm→scene scale transform. Tests — new gfx smoke
  case: strip via `submit_skinned` with a hand-animated 2-joint palette
  (swing about Z), Fox with an identity palette, plus the static-submit
  bind-pose fallback, 60 frames, 0 debug-layer errors. Full CTest suite (7
  executables) green incl. the unchanged golden cube; sandbox verified live:
  Fox loads (24 joints, 3 clips, skinned=1), Walk plays looping, ~120 FPS,
  capsule still rests at 0.89, clean exit, no errors. 0 warnings at
  `/W4 /permissive-`.
- 2026-07-14 Phase 6 Milestone 4 done: blending + state machine. New
  `anim/state_machine.h/.cpp`: `AnimState` (a single looping clip or a 1D
  blend of up to 4 clips over strictly-ascending parameter anchors — clip
  pointers borrowed from the owning `AnimationSet`) and `AnimStateMachine`
  (fixed capacity 8, hand-rolled per the phase doc's ADR-0009-style "plain
  C++ until churn demands more" decision — not a data-driven graph):
  `add_state()` (first added = initial), `transition_to(id, fade_time)`
  (crossfade; incoming state's clock restarts; <=0 fade switches instantly;
  transitioning mid-fade drops the old outgoing pose — documented small pop,
  revisit only if the manual pass flags it), `tick(dt, param, out_pose)`
  (advances the current and fading-out state clocks, evaluates the 1D blend
  via Milestone 1's `blend_1d_weights` — at most two adjacent non-zero
  weights, so evaluation is sample-two-clips-and-blend — then ramps the
  incoming pose 0→1 over the fade; weights sum to 1 at every stage by
  construction). Sandbox — the Fox is now the *player's* visible character
  riding the physics capsule (scaled cm→scene, feet at capsule bottom,
  facing from player yaw; the DamagedHelmet demoted to a static PBR prop):
  a two-state machine [Idle = Survey] <-> [Moving = Walk@3 / Run@9 blended
  over the capsule's actual horizontal speed — physics as the source of
  truth, so wall collisions read as idling] with 0.25 s crossfades on a
  0.5-speed threshold, ticked on the fixed timestep; sprint (Shift, 3x)
  naturally pushes the blend toward Run. 6 new unit tests: single-state ==
  direct clip sample (clock accumulation), 1D blend exact at/between/outside
  anchors, crossfade weight ramp hit at exact quarter/half/complete points,
  same-state transition no-op (clock keeps running), incoming clock restart
  (caught-by-design via a non-looping ramp clip), and crossfade-into-blend
  compounding both weight systems. 21 anim test cases (464 assertions);
  full CTest suite (7 executables) green, 0 warnings at `/W4 /permissive-`;
  sandbox verified live (Fox idles in Survey at rest, ~120 FPS, clean
  exit). Manual blend-feel review (idle<->walk<->run under real input)
  rolls into Milestone 5's phase-DoD sign-off.
- 2026-07-14 Phase 6 Milestone 5 done: root motion. New
  `anim/root_motion.h/.cpp`: `RootMotionDelta` (horizontal translation +
  yaw, asset model space — the consumer rotates/scales it into world) and
  `extract_root_motion(clip, skel, prev_time, new_time, looping)`, which
  samples the root (joint 0) tracks at both tick ends and differences them;
  looping uses an unwrapped-position formula (P(t) = floor(t/d)·per-cycle +
  S(t mod d)), so seam-crossing and multi-loop ticks yield the exact
  travelled delta, never the snap-back. Yaw is the root rotation's +Y twist
  (half-angle atan2(q.y, q.w) — scale-invariant, no normalize), with the
  through-360° wrap documented as a non-defended limitation until a real
  turning asset exists. `strip_root_motion(skel, pose)` is the exact
  complement: root x/z and yaw twist reset to the *rest* values (not zero —
  in-place clips strip to a no-op), animated vertical + swing + children
  untouched, via q·q_y(rest_yaw − anim_yaw) on the swing∗twist
  decomposition. `sample_track()` (vec3/quat) promoted from clip.cpp's
  internals to the public clip.h API for the root-track sampling. Asset
  discovery that shaped the milestone: the Fox's three clips are authored
  **in place** — their only translation channel is the hip bob (node
  b_Hip_01); the root joint has no tracks — so the sandbox keeps its
  input-driven capsule velocity, adds the strip call to the fixed-tick
  pipeline (a documented no-op for this asset), and the extract → velocity
  feed is proven headlessly instead: new app-layer `sandbox/tests`
  executable (`sandbox_tests`, the one place a test links sibling
  subsystems ge_anim + ge_physics, mirroring the sandbox glue it verifies).
  Baselines there (deterministic, fixed step): the Phase 5 capsule on the
  ground box driven per-tick by the strip's authored "move" clip
  (2 u/s) lands within 0.05 of the analytic 8-unit travel after 4 s while
  still resting at y≈0.89 under gravity; a wall in the path stops it at
  x≈3.0 despite the animation still feeding forward velocity — physics
  stays the authority, root motion never teleports the body. 6 new unit
  tests (anim suite now 27 cases / 487 assertions): exact per-tick deltas
  with vertical excluded, seam + whole-loop unwrap, non-looping clamps,
  yaw twist extraction incl. across the seam, strip preserving
  vertical/swing/children while zeroing x/z + yaw, and the in-place/
  rest-pose case. Full CTest suite (now 8 executables) green; 0 warnings
  at `/W4 /permissive-`; sandbox verified live (Fox 24 joints/3 clips,
  ~120 FPS, capsule at 0.89, clean exit).
- Current: Phase 6 — Animation, all 5 milestones code complete. Remaining
  for phase DoD: manual blend-feel review (idle↔walk↔run crossfades under
  real input) and sign-off. See `docs/phases/06-animation.md`.
