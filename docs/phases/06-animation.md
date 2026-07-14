# Phase 6 — Animation

> Status: **In progress.** Expanded 2026-07-09. Milestone 1 (animation core +
> sampling math) complete 2026-07-14; Milestone 2 next.

## Goal
Skeletal animation with GPU skinning: clip sampling and looping, pose
blending (idle/walk/run), an animation state machine with crossfades, and
root motion feeding the Phase 5 physics character — ending in a skinned
character in the sandbox that smoothly blends locomotion clips driven by
gameplay state.

## Scope
- In scope: skeleton + skinned-mesh import from glTF (skins, joints, inverse
  bind matrices, animation channels); clip sampling with looping; pose
  blending (two-pose lerp/slerp and a 1D locomotion blend over a speed
  parameter); a small animation state machine (states + crossfade
  transitions); GPU skinning (bone-palette vertex shader path in `ge_gfx`);
  root-motion extraction and application to the physics-driven character.
- Out of scope (deferred, not speculative): inverse kinematics (explicitly
  "later" in plan.md); full graph-editor-style blend trees (Phase 6 ships a
  1D blend — a general node graph waits for a real need); animation
  compression (keyframes stay raw float tracks until memory says otherwise);
  animation events/notifies (no gameplay consumes them yet); morph targets;
  retargeting; multithreaded pose evaluation (the job system exists — wire
  it in only if profiling shows pose sampling on the frame's critical path).

## Module layout
New `engine/anim` module (`ge_anim`), **Layer 3 (Subsystems)** — same layer
as `ge_gfx`, `ge_input`, `ge_physics`. It depends on `ge_core` only, and
per ADR-0001 never sideways on `ge_gfx` or `ge_world`:

- `engine/anim/include/anim/skeleton.h` — `Skeleton`: joint count, parent
  index array (parents precede children), inverse bind matrices, rest pose.
  Pure data.
- `engine/anim/include/anim/clip.h` + `src/clip.cpp` — `AnimationClip`:
  per-joint translation/rotation/scale keyframe tracks (times + values,
  SoA); `sample(clip, t, looping, Pose&)` with binary-search keyframe lookup,
  vec3 lerp / quat slerp interpolation. Milestone 1.
- `engine/anim/include/anim/pose.h` + `src/pose.cpp` — `Pose`: array of
  local-space TRS per joint; `blend(a, b, t, out)` (weights sum to 1 by
  construction); `pose_to_model_space()` (walk the parent chain) and
  `pose_to_palette()` (model-space × inverse bind → the mat4 array the GPU
  consumes). Milestone 1.
- `engine/anim/include/anim/animation_loader.h` + `src/animation_loader.cpp`
  — glTF import of skeleton + clips via cgltf (vendored third-party, private
  include, same as `ge_gfx`'s model loader). Milestone 2.
- `engine/anim/include/anim/state_machine.h` + `src/state_machine.cpp` —
  `AnimStateMachine`: small fixed set of states, each a clip or 1D blend;
  crossfade transitions (blend the outgoing and incoming poses over a
  fade time). Milestone 4.
- `engine/anim/include/anim/root_motion.h` + `src/root_motion.cpp` — root
  joint delta extraction per tick (translation, yaw), with the root track
  stripped from the sampled pose. Milestone 5.

**Sibling isolation (`ge_anim` vs `ge_gfx`).** Both are Layer 3, so neither
may include the other (golden rule 1). They meet only through plain data
passed by the application layer:

- `ge_anim` loads the *skeleton and clips* from the .glb (its own cgltf
  parse) and produces a bone palette (`mat4*` + count) per frame.
- `ge_gfx` loads the *skinned mesh* from the same .glb (JOINTS_0/WEIGHTS_0
  vertex attributes) and accepts the palette as a bare `const mat4*` in
  `submit_skinned()` — no `ge_anim` type crosses the boundary.
- The shared contract is joint ordering: **both sides index joints in the
  glTF skin's `joints` array order.** This is by-construction consistent
  (same file, same array) and documented in both loaders' headers.
- The glue — an `AnimationPlayer`-style component holding clip/state
  handles, ticked by a sandbox system that feeds palettes to the renderer —
  lives in the sandbox, exactly like the Phase 5 physics glue (ADR-0009
  precedent: the app owns both sides). This is ADR-0001 applied, not a new
  cross-cutting decision; no new ADR needed.

## Tech decisions
- **GPU skinning, not CPU.** Linear blend skinning in the vertex shader
  from a bone-matrix palette. The ADR-0010 infrastructure makes this a
  no-new-RHI-code feature: the palette is one more ring allocation bound as
  a root CBV (`b3`), and the skinned pipelines use `BindingLayout{4, 6}`.
  A 128-joint palette is 8 KB — far under the 64 KB CBV limit, and the
  1 MiB/frame upload ring absorbs dozens of characters (bump the constant
  when a scene proves otherwise).
- **Vertex format** — a separate `SkinnedVertex` (`LitVertex` + `u8
  joints[4]` + `f32 weights[4]`, stride 92), not a fattening of `LitVertex`:
  static meshes shouldn't pay 20 bytes/vertex for skinning they don't use.
  Requires one small RHI addition: `Format::R8G8B8A8_UINT` for the joint
  indices input element. Joint count per skin is capped at 256 (u8 indices);
  assert at import.
- **Palette space** — the palette carries model-space × inverse-bind
  matrices; the per-object CB (`b1`) still applies the entity's world
  transform on top, so skinned and static draws share the same
  world-transform path (and the shadow pass reuses the identical palette
  allocation).
- **Skinned shader variants** — `pbr_skinned_vs.hlsl` and
  `shadow_skinned_vs.hlsl` (shadow too, or characters cast rest-pose
  shadows). Pixel shaders are unchanged — skinning only changes where
  vertices end up. PSO variants: the skinned PBR block mirrors the static
  2×2 (cull × blend) per address mode, built lazily like ADR-0010's.
- **Clip storage** — raw keyframe tracks (times array + values array per
  channel), sampled with binary search + lerp/slerp. No compression, no
  fixed-rate resampling until memory or cache pressure is measured.
- **Blending scope** — two-pose blend plus a 1D locomotion blend
  (speed → idle/walk/run weights). The state machine is a hand-rolled
  fixed-state affair (enum states, crossfade timer), not a data-driven
  graph — same "plain C++ until churn demands more" reasoning as ADR-0009.
- **Root motion → physics** — the root track's horizontal translation delta
  is stripped from the pose and converted to a desired velocity for the
  Phase 5 capsule body (fed through the same path as
  `system_move_player_physics`'s input velocity). The physics solver stays
  the authority on actual movement (collisions still stop the character);
  animation never teleports the body. Vertical root motion (jumps) is out
  of scope this phase.
- **Fixed-timestep ticking** — animation state advances in the fixed update
  (same accumulator as physics), so gameplay-driven state changes and
  root-motion velocities stay deterministic; the render submit just uses
  the latest pose. Pose interpolation across the render/fixed boundary is
  deferred until visible stutter demands it.

## Design
Per fixed tick, per animated entity:

```
gameplay state (speed, moving?) -> state machine tick (transitions, fade timers)
  -> sample active clip(s) at their local times (looping)
  -> blend poses (1D locomotion blend; crossfade between states)
  -> extract + strip root motion  -> desired velocity for the physics body
  -> pose -> model space (parent chain) -> x inverse bind -> mat4 palette
```

Per rendered frame, per animated entity (sandbox glue system):

```
renderer.submit_skinned(model_id, world_transform, palette, joint_count)
  -> renderer copies the palette into the upload ring (b3), draws with the
     skinned PSO variants; shadow pass reuses the same palette allocation
```

## Milestones
1. **Animation core + sampling math (no rendering).** `Skeleton`, `Pose`,
   `AnimationClip`, `sample()`, `blend()`, `pose_to_model_space()`,
   `pose_to_palette()`. Unit tests against hand-computed 2-bone cases.
2. **glTF import + test assets.** `ge_anim`'s loader (skins, IBMs, clips);
   `ge_gfx`'s model loader gains the skinned path (`SkinnedVertex`,
   JOINTS_0/WEIGHTS_0, skinned flag — and *no* node-transform baking for
   skinned meshes); new `tools/gen_skinned_glb.cpp` generates a 2-bone
   strip with a known 90° rotation clip (the deterministic unit-test
   asset); vendor a rigged Khronos sample model (Fox — CC-BY 4.0, has
   Survey/Walk/Run clips, ~24 joints; same source and licensing situation
   as DamagedHelmet) for the visual DoD.
3. **GPU skinning render path.** `Format::R8G8B8A8_UINT`;
   `pbr_skinned_vs` / `shadow_skinned_vs`; skinned PSO blocks;
   `Renderer::submit_skinned()`; sandbox renders the Fox playing one
   looping clip. Smoke test: N frames, 0 debug-layer errors.
4. **Blending + state machine.** 1D locomotion blend (speed parameter);
   crossfade transitions; sandbox character switches idle/walk/run from
   input speed (walk vs sprint modifier).
5. **Root motion.** Extraction + strip; feed the capsule body's horizontal
   velocity; verify the character's world motion matches the authored clip
   (baseline test) and still collides with the ground box. Manual
   blend-feel review; phase DoD sign-off.

## Test plan
Per plan.md's Phase 6 critical-test line:
- **Unit (Milestone 1):** clip sampling at known times — exact keyframe
  hits, mid-keyframe interpolation, loop wraparound (t > duration), clamp
  at the last key for non-looping; blend weights sum to 1 across the 1D
  blend's parameter range (including at and between clip anchor points);
  pose/skeleton math — a hand-computed 2-bone chain (translate + rotate)
  checked through local→model→palette, including the inverse-bind round
  trip (rest pose ⇒ identity palette).
- **Unit (Milestone 2):** import round-trip on the generated 2-bone .glb —
  joint count/parents/IBMs and keyframe times/values match what the tool
  wrote; skinned vertices carry the expected joints/weights.
- **Unit (Milestone 5):** root-motion extraction — a clip authored with
  known root translation yields exactly that delta per tick, and the
  returned pose has the root track zeroed.
- **Smoke (Milestone 3):** skinned model loads and renders N frames with
  0 debug-layer errors (extends the existing gfx smoke suite).
- **Baseline (Milestone 5):** seeded run — the root-motion-driven character
  ends within tolerance of the analytically expected position after T
  seconds of a looping walk clip.
- **Manual:** blend feel (idle↔walk↔run transitions, crossfade timing) —
  recorded sign-off, per plan.md.

## Open questions
- Pose interpolation between fixed ticks for the render frame (only if
  60 Hz stepping shows visible judder on high-refresh displays).
- Whether the 1D blend needs sync markers (foot-phase matching) for
  walk↔run to look right — decide during Milestone 4's manual pass; add
  phase-normalized clip times only if the naive blend visibly slides.
- Palette double-buffering for pose interpolation vs. recomputing — moot
  until the first question is answered.

## Cross-phase watch items
- The cascaded-shadow trigger from Phases 4–5 carries: an animation test
  level big enough to outgrow the single-frustum directional shadow is the
  signal to build CSM in `ge_gfx`.
- Upload-ring sizing: many animated characters × (per-object CB + palette)
  is the first realistic pressure on the 1 MiB/frame ring (ADR-0010 notes
  the budget); bump the constant when a scene needs it.
- The `ge_anim`/`ge_gfx` dual-parse of the same .glb is acceptable now and
  disappears naturally into the Phase 11 asset pipeline (one importer, two
  cooked outputs).

## Depends on
- Phase 3 renderer + the ADR-0010 submission/binding architecture
  (`submit_skinned`, `BindingLayout`, upload ring).
- Phase 4 (ECS/world — the animated entity), Phase 5 (the physics character
  root motion drives), Phase 0 (math: quat slerp, mat4, TRS).
