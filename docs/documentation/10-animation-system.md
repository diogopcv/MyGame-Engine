# Animation System

This document covers the Phase 6 animation system: the `ge_anim` module (skeletons, poses, clips, blending, the animation state machine, root motion), the GPU-skinning render path in `ge_gfx`, and how the sandbox wires both to the Phase 5 physics character. Like the rest of the manual, it explains the concepts from first principles before showing what the engine actually implements.

## Table of Contents

- [Skeletal Animation Fundamentals](#skeletal-animation-fundamentals)
  - [Why Skeletons?](#why-skeletons)
  - [Joints, Bind Pose, and Inverse Bind Matrices](#joints-bind-pose-and-inverse-bind-matrices)
  - [The Skinning Equation](#the-skinning-equation)
- [The ge_anim Module](#the-ge_anim-module)
  - [Layer Placement and Sibling Isolation](#layer-placement-and-sibling-isolation)
  - [The Joint-Ordering Contract](#the-joint-ordering-contract)
- [Skeleton and Pose Data](#skeleton-and-pose-data)
  - [Skeleton](#skeleton)
  - [JointPose and Pose](#jointpose-and-pose)
  - [Local to Model Space](#local-to-model-space)
  - [Building the GPU Palette](#building-the-gpu-palette)
- [Animation Clips](#animation-clips)
  - [Keyframe Tracks](#keyframe-tracks)
  - [Sampling](#sampling)
  - [Looping vs Clamping](#looping-vs-clamping)
- [glTF Animation Import](#gltf-animation-import)
- [Pose Blending](#pose-blending)
  - [Two-Pose Blend](#two-pose-blend)
  - [The 1D Locomotion Blend](#the-1d-locomotion-blend)
- [The Animation State Machine](#the-animation-state-machine)
  - [States](#states)
  - [Crossfade Transitions](#crossfade-transitions)
  - [The Tick](#the-tick)
- [GPU Skinning Render Path](#gpu-skinning-render-path)
  - [SkinnedVertex](#skinnedvertex)
  - [Skinned Shaders and Palette Binding](#skinned-shaders-and-palette-binding)
  - [submit_skinned and the Upload Ring](#submit_skinned-and-the-upload-ring)
  - [PSO Variants and the Bind-Pose Fallback](#pso-variants-and-the-bind-pose-fallback)
- [Root Motion](#root-motion)
  - [What Root Motion Is](#what-root-motion-is)
  - [Extraction: Loop-Aware Deltas](#extraction-loop-aware-deltas)
  - [Yaw: The Twist About Y](#yaw-the-twist-about-y)
  - [Stripping the Pose](#stripping-the-pose)
  - [Feeding Physics: The Authority Rule](#feeding-physics-the-authority-rule)
- [The Sandbox Character Pipeline](#the-sandbox-character-pipeline)
- [Testing the Animation System](#testing-the-animation-system)

---

## Skeletal Animation Fundamentals

### Why Skeletons?

A character mesh might have tens of thousands of vertices. Animating each vertex individually — storing a position per vertex per keyframe — would be enormous and unworkable for artists. Skeletal animation solves both problems with indirection:

1. A **skeleton** of a few dozen **joints** (also called bones) is embedded in the mesh. The Fox test asset has 24; a film-quality character might have a few hundred.
2. Each vertex is attached to a small number of joints (up to 4 here) with **weights** that sum to 1 — a shoulder vertex might be 70% upper-arm, 30% torso.
3. The animation moves only the joints. Vertices follow the joints they are attached to, blending the influence of each.

An animation is then just a handful of joint transforms over time instead of a mesh-sized blob per frame, and an animator poses a rig, not individual vertices.

### Joints, Bind Pose, and Inverse Bind Matrices

Joints form a **hierarchy**: each joint has a parent, and its transform is expressed *relative to that parent* (local space). Rotating the shoulder automatically carries the elbow, wrist, and fingers with it, because their model-space transforms are built by walking up the parent chain.

The **bind pose** (or rest pose) is the pose the mesh was modeled in — arms out in a T or A shape, typically. Vertices are authored in model space *assuming the bind pose*. This creates a subtlety: to move a vertex with an animated joint, you cannot just apply the joint's animated transform — the vertex is not defined in the joint's space, it is defined in model space with the bind pose already "applied".

The fix is the **inverse bind matrix (IBM)**, one per joint: the inverse of the joint's model-space bind transform. It takes a model-space vertex *back into the joint's local (bind) space* — "undoing" the bind pose. From there, the joint's animated model-space transform carries it to where the animation put it:

```
skinning matrix for joint j = inverse_bind[j] * model_animated[j]
                               ^ undo bind pose   ^ apply animated pose
```

A useful sanity check falls out of this: if the animated pose *is* the bind pose, the two matrices cancel and every skinning matrix is identity — the mesh renders exactly as authored. The unit tests assert precisely this round trip.

### The Skinning Equation

With the per-joint skinning matrices collected into a **bone palette**, each vertex blends the matrices of the joints it follows (**linear blend skinning**). In the engine's row-vector convention:

```
v_skinned = Σ_i  weights[i] * (v_bind * palette[joints[i]])     i = 0..3
```

The same equation transforms the normal and tangent (rotation-only in practice, since the test rigs carry no shear). This runs per vertex, which is exactly the kind of embarrassingly parallel work GPUs exist for — hence **GPU skinning**: the CPU computes one palette per character per frame (24 matrices for the Fox), and the vertex shader does the per-vertex blend for free alongside the transforms it was already doing.

---

## The ge_anim Module

### Layer Placement and Sibling Isolation

`ge_anim` is a **Layer 3 subsystem**, a sibling of `ge_gfx`, `ge_input`, and `ge_physics`. Per the golden rule it depends only on `ge_core` (math, allocators, logging) and **never sideways** on `ge_gfx` or `ge_world`:

```
engine/anim/
  include/anim/
    skeleton.h          Skeleton, JointPose, kMaxJoints
    pose.h              Pose, blend(), blend_1d_weights(), pose_to_model_space(), pose_to_palette()
    clip.h              AnimationClip, tracks, sample(), sample_track()
    animation_loader.h  AnimationSet, load_animation_gltf()
    state_machine.h     AnimState, AnimStateMachine
    root_motion.h       RootMotionDelta, extract_root_motion(), strip_root_motion()
  src/                  implementations (cgltf linked PRIVATE, never in the public API)
  tests/                ge_anim_tests (unit suite)
```

This raises an obvious question: animation produces bone palettes and the renderer consumes them — how do two siblings that cannot include each other cooperate? The same way `ge_world` and `ge_physics` did in Phase 5: **through plain data, glued by the application layer.**

- `ge_anim` parses the .glb itself and produces a palette as a bare `mat4*` + count.
- `ge_gfx` parses the same .glb *separately* for the skinned mesh, and `Renderer::submit_skinned()` accepts the palette as `const mat4*` — no `ge_anim` type crosses the boundary.
- The sandbox owns both handles and passes the pointer across.

The dual parse of one file is accepted duplication for now; it disappears naturally into the Phase 11 asset pipeline (one importer, two cooked outputs).

### The Joint-Ordering Contract

A palette is just an array of matrices — the renderer needs to agree with the animation system on *which index is which joint*, or vertices follow the wrong bones. The contract: **both sides index joints in the glTF skin's `joints` array order.** `ge_anim`'s skeleton stores joints in that order; `ge_gfx`'s skinned vertices store `JOINTS_0` indices, which per the glTF spec index that same array. Because both loaders read the same file, the ordering is consistent *by construction* — there is no registration step that could drift.

---

## Skeleton and Pose Data

### Skeleton

```cpp
struct Skeleton {
    u32        joint_count = 0;
    i32*       parents      = nullptr; // parent index per joint; -1 = root
    mat4*      inverse_bind = nullptr; // model space -> joint bind space
    JointPose* rest_pose    = nullptr; // local-space bind TRS per joint
};
inline constexpr u32 kMaxJoints = 256;
```

Pure data, three parallel arrays. Two deliberate constraints:

- **Parents precede children** (`parents[i] < i` for every non-root). The importer validates this, and it is what lets local→model conversion be a *single forward loop* with no recursion and no visited flags — by the time joint `i` is processed, its parent's model matrix already exists.
- **`kMaxJoints = 256`**, because the GPU vertex format stores joint indices as `u8`. The importer rejects bigger skins rather than truncating.

### JointPose and Pose

```cpp
struct JointPose {
    vec3 translation{};
    quat rotation = quat_identity();
    vec3 scale{1, 1, 1};
};
```

A pose is stored as **TRS components, not matrices** — one `JointPose` per joint in a `Pose`. The reason is blending: interpolating two rotation matrices component-wise does not produce a rotation (the result shears and shrinks), while quaternion slerp stays on the rotation manifold. TRS is also smaller and cheaper to lerp. Matrices appear only at the very end of the pipeline, when the final pose is flattened for the GPU.

`Pose` is the currency of the whole system: clips *sample into* poses, blends *combine* poses, the state machine *outputs* a pose, root-motion stripping *edits* a pose, and only then does it become matrices.

### Local to Model Space

```cpp
void pose_to_model_space(const Skeleton& skel, const Pose& pose, mat4* out_model) {
    for (u32 i = 0; i < skel.joint_count; ++i) {
        mat4 local = trs(pose.joints[i].translation, pose.joints[i].rotation, pose.joints[i].scale);
        i32 parent = skel.parents[i];
        out_model[i] = parent < 0 ? local : local * out_model[parent];
    }
}
```

In the engine's row-vector convention (`v * M`, [04-math-library.md](04-math-library.md)), `local * model[parent]` applies the joint's own transform first, then the whole parent chain — a vertex owned by the wrist gets wrist, then elbow, then shoulder, then root. The parents-first invariant makes the single pass valid.

### Building the GPU Palette

```cpp
void pose_to_palette(const Skeleton& skel, const mat4* model_space, mat4* out_palette) {
    for (u32 i = 0; i < skel.joint_count; ++i)
        out_palette[i] = skel.inverse_bind[i] * model_space[i];
}
```

Row-vector reading: a bind-pose vertex is first taken *into* the joint's bind space (`inverse_bind`), then *out* through the animated joint (`model_space`). Sampling the rest pose therefore yields identity palettes — the test suite's round-trip check.

One important detail: the palette is **model-space only**. The entity's world transform is *not* baked into it — the per-object constant buffer applies it on top, exactly as for static meshes. This keeps one world-transform path for all draws and lets the shadow pass reuse the identical palette allocation.

---

## Animation Clips

### Keyframe Tracks

```cpp
struct Vec3Track { f32* times; vec3* values; u32 key_count; };  // translation or scale
struct QuatTrack { f32* times; quat* values; u32 key_count; };  // rotation

struct JointTracks { Vec3Track translation; QuatTrack rotation; Vec3Track scale; };

struct AnimationClip {
    f32          duration;     // seconds
    u32          joint_count;
    JointTracks* tracks;       // one per joint, skeleton joint order
};
```

A clip is raw keyframe data in **structure-of-arrays** form: per channel, an array of strictly-increasing key times and a parallel array of values. Channels that were not authored are simply empty (`key_count == 0`) and fall back to the skeleton's rest pose at sample time — a clip that only rotates an elbow stores exactly that and nothing else.

There is deliberately **no compression and no fixed-rate resampling**: keyframes stay as the exporter wrote them until memory or cache pressure is actually measured. (For scale: the Fox's three clips total a few hundred kilobytes.)

### Sampling

```cpp
void sample(const AnimationClip& clip, const Skeleton& skel, f32 time, bool looping, Pose& out);
vec3 sample_track(const Vec3Track& track, f32 t);   // also public, used by root motion
quat sample_track(const QuatTrack& track, f32 t);
```

Per track, sampling is: **binary search** for the last key at or before `t` (`O(log n)`, keys are sorted), then interpolate to the next key — linear for vec3, **slerp** for quaternions. Times before the first key clamp to the first value, after the last key to the last value, so a track does not need keys spanning the whole clip.

### Looping vs Clamping

Time outside `[0, duration]` is handled once, up front, for the whole clip:

- **Looping:** `t = fmod(t, duration)`, with negative results wrapped forward — a walk cycle repeats seamlessly and the caller can keep a monotonically growing clock.
- **Non-looping:** clamp to `[0, duration]` — a one-shot clip holds its last pose.

The state machine keeps per-state clocks that just accumulate `dt`; all wrap/clamp policy lives here.

---

## glTF Animation Import

`load_animation_gltf(path, alloc, out)` is `ge_anim`'s own cgltf parse of the model file (see the sibling-isolation section for why it does not share `ge_gfx`'s). It produces an `AnimationSet`: one `Skeleton` plus all clips, with a name table so callers can `find_clip("Walk")`.

What it imports, and the decisions baked in:

| Step | Detail |
|------|--------|
| Skeleton | Skin 0's `joints` array, in file order (the ordering contract). Validates parents-precede-children and the 256-joint cap; rejects files without a skin. |
| Rest pose | Each joint node's local TRS; matrix-form nodes are decomposed. |
| IBMs | Converted from glTF's column-major storage to the engine's row-vector convention (a transpose on copy — same as the gfx loader's node transforms). |
| Channels | Every animation's translation/rotation/scale channels into SoA tracks. `LINEAR` supported; `STEP` read as linear (a documented approximation); `CUBICSPLINE` skipped with a warning; morph-target weights out of scope. |

The two rigged test assets, both from Milestone 2:

- **`skinned_strip.glb`** — generated by `tools/gen_skinned_glb.cpp` with hand-picked values: a 2-joint vertical strip with three weight rows, a `"bend"` clip (tip joint 0→90° about Z) and a `"move"` clip (root translates (0,0,0)→(2,0,0) over 1 s — the root-motion test clip). Because every number is known, import tests are exact round trips, not "looks plausible" checks. The mesh node carries a deliberate `translation(5,0,0)`: per glTF a *skinned* node's transform must be ignored, and tests catch any importer that bakes it.
- **`Fox.glb`** — the Khronos sample model (CC-BY 4.0): 24 joints, Survey/Walk/Run clips, the visual definition-of-done asset. Notably its clips are authored **in place** — the root joint has no tracks at all (the one translation channel is the hip bob) — which shaped how root motion is verified (see below).

---

## Pose Blending

### Two-Pose Blend

```cpp
void blend(const Pose& a, const Pose& b, f32 t, Pose& out);  // out = (1-t)*a + t*b
```

Per joint: translation and scale lerp, rotation slerps (shortest arc). The two implicit weights are `1-t` and `t` — they **sum to 1 by construction**, which is the invariant that makes every blend in the system safe to nest: a crossfade of a 1D blend of clips is still a convex combination of valid poses, never an extrapolation.

### The 1D Locomotion Blend

The classic locomotion problem: idle, walk, and run clips, and a continuously varying speed. A 1D blend space maps a parameter (speed) onto clip weights:

```cpp
void blend_1d_weights(const f32* anchors, u32 anchor_count, f32 param, f32* out_weights);
```

Each clip sits at an **anchor** — the parameter value where it fully owns the pose (e.g. `{0, 3, 9}` for idle/walk/run at their natural speeds). For a given `param`, every weight is zero except the two anchors bracketing it, which split linearly; outside the range, the nearest end clamps to weight 1:

```
weight
 1 |  idle          walk                        run
   |  \            /    \                      /
   |   \          /      \                    /
   |    \        /        \                  /
 0 +-----+------+----------+----------------+------ param (speed)
         0      3                           9
```

Because at most two adjacent weights are non-zero, evaluating the blend is always "sample two clips, blend once" regardless of how many clips the space holds. The weight math is a pure function, written and unit-tested in Milestone 1 before the state machine that consumes it existed.

---

## The Animation State Machine

### States

```cpp
struct AnimState {
    const AnimationClip* clips[4];  // borrowed from the AnimationSet
    f32  anchors[4];                // strictly ascending; ignored for single-clip states
    u32  clip_count;
    bool looping = true;
};
```

A state is either a single clip or a 1D blend of up to 4 clips. That's the whole vocabulary — deliberately. `AnimStateMachine` is a **hand-rolled, fixed-capacity (8 states) machine**, not a data-driven graph editor: enum-style state ids, explicit `transition_to()` calls from gameplay code. This is the ADR-0009 reasoning applied to animation — plain C++ until churn demands more. A general blend-tree/graph asset format would be speculative machinery with exactly one two-state user today.

### Crossfade Transitions

`transition_to(state, fade_time)` starts a **crossfade**: the outgoing state keeps playing and the incoming state's pose ramps from weight 0 to 1 over `fade_time` (a plain `blend()` with the fade's progress as `t`). Rules with rationale:

- **The incoming state's clock restarts from 0** — re-entering Walk starts the cycle fresh rather than resuming a stale phase.
- **Transitioning to the current state is a no-op** — gameplay can call it every tick without stutter.
- **`fade_time <= 0` switches instantly.**
- **Transitioning mid-fade drops the old outgoing pose** (the blend holds at most two poses). This can produce a small visual pop when transitions arrive faster than fades complete; a pose snapshot would fix it and is deferred until the manual feel pass ever flags it.

### The Tick

```cpp
void tick(f32 dt, f32 param, Pose& out_pose);
```

One call per fixed update: advances the current (and any fading-out) state's clock, evaluates each — single clip: `sample()` at the state clock; 1D blend: `blend_1d_weights()` then sample the two bracketing clips and blend — and finally ramps the crossfade. Weights sum to 1 at every stage.

The machine ticks on the **fixed timestep**, the same accumulator as physics. Animation state feeds gameplay-visible values (root-motion velocity, transition decisions read from physics speed), so it must advance deterministically with the simulation, not with render framerate. The render loop just consumes the latest pose; interpolating poses across the fixed/render boundary is deferred until 60 Hz stepping shows visible judder.

---

## GPU Skinning Render Path

The `ge_gfx` half. Thanks to the ADR-0010 architecture (upload ring, `BindingLayout`-derived root signatures — see [06-graphics-and-rendering.md](06-graphics-and-rendering.md)), skinning needed **no new RHI machinery** beyond one pixel format.

### SkinnedVertex

```cpp
struct SkinnedVertex {
    LitVertex base;      // the full 72-byte static layout   (offset 0)
    u8  joints[4];       // skin-palette joint indices        (offset 72)
    f32 weights[4];      // skinning weights, sum to 1        (offset 76)
};
static_assert(sizeof(SkinnedVertex) == 92);
```

A separate vertex type, not a fattening of `LitVertex` — static meshes shouldn't pay 20 bytes/vertex for skinning they don't use. The layout trick: `SkinnedVertex` is a `LitVertex` **prefix** plus appended attributes, so every static-path attribute offset stays valid. A non-skinned pipeline can legally draw a skinned buffer (it just ignores the extra 20 bytes via the larger stride) — this is what makes the bind-pose fallback below free. Joint indices are `u8` (hence the 256-joint cap and the one RHI addition, `Format::R8G8B8A8_UINT`). The model loader reads `JOINTS_0`/`WEIGHTS_0`, rejects indices over 255, and — critically — does *not* bake node transforms into skinned vertices, since glTF defines a skinned node's transform as ignored.

### Skinned Shaders and Palette Binding

Two vertex-shader variants; **pixel shaders are unchanged** — skinning only changes where vertices end up, not how they are shaded:

- `pbr_skinned_vs.hlsl` — palette as a 256-entry `cbuffer` root CBV at **b3**; blends position, normal, and tangent through the palette, then continues into the identical world/view/projection path as the static VS.
- `shadow_skinned_vs.hlsl` — same skinning, palette at **b2**, because the depth-only root signature has no PerMaterial slot. Same ring allocation, different slot index. (Skinning the shadow pass matters: otherwise characters cast rest-pose shadows.)

Root signatures come from the ADR-0010 `BindingLayout` cache: the skinned PBR pipelines use `{4 CBVs, 6 SRVs}` (SRV table slots shift to 4..9), the skinned shadow pipeline `{3, 2}`.

### submit_skinned and the Upload Ring

```cpp
renderer.submit_skinned(model_id, world_transform, palette, joint_count);
```

The renderer copies the palette into the **per-frame upload ring** at submit time — the caller may reuse or overwrite its palette array immediately after the call returns, and one allocation feeds both the shadow and PBR passes. A 128-joint palette is 8 KB, far under the 64 KB root-CBV limit; the 1 MiB/frame ring absorbs dozens of characters before its size constant needs bumping.

### PSO Variants and the Bind-Pose Fallback

The skinned PBR PSOs mirror the static set — a 2×2 block of cull × blend variants per texture address mode — built lazily the first time a skinned model needs them. And because of the `LitVertex`-prefix layout, submitting a skinned model through plain `submit()` is *valid*: it draws with the static PSO in its bind pose. That is a feature (props made from skinned assets, debugging), not an accident.

---

## Root Motion

### What Root Motion Is

There are two ways to move an animated character through the world. **In-place**: the clip animates limbs but the root stays put, and gameplay code moves the entity at some speed it hopes matches the stride — mismatch reads as foot-sliding. **Root motion**: the animator moves the skeleton's root joint through space *inside the clip*, and the game extracts that motion each tick and applies it to the entity instead. The animation becomes the authority on how fast a walk covers ground, and feet plant where they visually should.

The catch: if you extract the root's motion *and* leave it in the pose, the character moves twice. Extraction and stripping are therefore two halves of one contract:

```cpp
struct RootMotionDelta {
    vec3 translation;  // horizontal (y always 0), asset model space
    f32  yaw;          // radians about +Y
};

RootMotionDelta extract_root_motion(const AnimationClip&, const Skeleton&,
                                    f32 prev_time, f32 new_time, bool looping);
void strip_root_motion(const Skeleton&, Pose&);
```

Whatever `extract_root_motion` reports is exactly what `strip_root_motion` removes — apply the delta to the body, render the stripped pose, and the authored motion is reproduced once. Deltas are in the asset's model space; the consumer rotates them by the character's world facing and applies any asset scale. Vertical root motion (jumps) is out of scope this phase, so `translation.y` is always 0 and the pose keeps its animated vertical (the hip bob stays visual).

### Extraction: Loop-Aware Deltas

Naively, the per-tick delta is `root(t_new) - root(t_prev)`. That breaks the moment a looping clip wraps: a walk cycle that carried the root forward 2 units snaps back to 0 at the seam, and the naive difference reports a violent 2-unit teleport *backwards*.

The fix is to difference an **unwrapped position** that is continuous across the seam:

```
P(t) = floor(t / duration) * per_cycle_displacement + root(t mod duration)
delta = P(t_new) - P(t_prev)
```

where `per_cycle_displacement = root(duration) - root(0)`. Crossing the seam now correctly adds the full cycle displacement, and the formula is exact even if one tick spans several whole loops. Non-looping clips just clamp both times to `[0, duration]` — once the clip ends, deltas are zero. An untracked root (an in-place clip) samples the rest pose at both ends and extracts exactly zero, so the same code path is safe on any asset.

### Yaw: The Twist About Y

For turning animations, the interesting part of the root's rotation is its **yaw** — the twist about the vertical axis — while lean and tilt should stay in the pose. Any rotation decomposes as `q = swing * twist_y` (in the engine's apply-left-first product: swing first, then a pure Y rotation applied in the root's parent space — the same space the character body's yaw lives in). The twist half-angle is simply `atan2(q.y, q.w)`, which is scale-invariant, so no normalization is needed. Yaw deltas go through the same unwrapping formula as translation. One documented, undefended limitation: a clip whose root yaw passes through exactly 360° mid-cycle wraps the twist angle — acceptable until a real turning asset exists.

### Stripping the Pose

`strip_root_motion` resets the root joint's horizontal translation (x, z) and its yaw twist **to the rest pose's values** — not to zero. Multiplying by `q_y(rest_yaw − anim_yaw)` cancels the animated twist and re-applies the rest twist while leaving the swing untouched. Resetting to *rest* rather than zero matters: on an in-place clip the sampled root already equals the rest pose, so stripping is a no-op, and a rig whose root is authored with a baked offset or orientation keeps it.

### Feeding Physics: The Authority Rule

Root motion does **not** move the entity's transform directly. The delta is converted to a desired velocity and fed to the Phase 5 capsule body through the same path as player input:

```cpp
body.linear_velocity.x = delta.translation.x / dt;
body.linear_velocity.z = delta.translation.z / dt;
// vertical velocity untouched — gravity and the solver own it
```

**The physics solver stays the authority on actual movement.** Walls still stop the character mid-walk-cycle, gravity still pulls it onto the ground, and animation never teleports the body. The `sandbox_tests` baselines pin both halves of this contract: a capsule driven by the strip's `"move"` clip (authored at 2 u/s) lands within tolerance of the analytic 8 units after 4 s while resting on the ground box — and with a wall in the path it stops at the wall, even though the clip keeps feeding forward velocity.

Because the Fox's clips are in-place (root untracked), the live sandbox keeps its input-driven velocity and its strip call is a no-op — the root-motion→physics feed is exercised by those headless baselines with the authored `"move"` clip instead.

---

## The Sandbox Character Pipeline

The sandbox is where the siblings meet (see [07-application-and-game-loop.md](07-application-and-game-loop.md) for the surrounding loop). At load, Fox.glb is parsed **twice on purpose**: once by `renderer.load_model()` for the skinned mesh, once by `anim::load_animation_gltf()` for the skeleton and clips. A two-state machine is built: **Idle** (Survey) and **Moving** (Walk anchored at speed 3, Run at 9 — sprint is a 3× input multiplier, so it naturally pushes the blend toward Run).

Per fixed tick (same accumulator as physics):

```
input -> system_move_player_physics -> PhysicsWorld::step -> sync Transform

speed = |capsule horizontal velocity|          // physics is the source of truth
speed vs threshold  -> transition_to(Idle/Moving, 0.25s crossfade)
fox_sm.tick(dt, speed, pose)                    // sample + blend + fade
strip_root_motion(skeleton, pose)               // no-op for the in-place Fox
pose_to_model_space -> pose_to_palette          // TRS -> mat4 palette
```

Reading the blend parameter and the idle/moving decision from the capsule's *actual* velocity — not raw input — is a small but load-bearing choice: run into a wall and the character correctly idles instead of jogging against the bricks.

Per rendered frame:

```
renderer.submit_skinned(fox_model, world, palette, joint_count)
// world = cm->scene scale * yaw facing * feet-at-capsule-bottom translation
```

---

## Testing the Animation System

The suite follows the tiered strategy (ADR-0003), with each tier doing the job it is actually good at:

- **Unit, hand-computed (`ge_anim_tests`)**: sampling at exact keys, mid-key lerp/slerp, loop wrap including negative time, non-looping clamps; 1D blend weights at/between/outside anchors with a sum-to-1 sweep; a hand-computed 2-bone chain through local→model→palette including the rest-pose⇒identity round trip; state-machine clocks, crossfade ramps hit at exact fractions, clock-restart semantics; root-motion per-tick deltas, seam/multi-loop unwrapping, yaw twist extraction, and strip behavior (vertical/swing/children preserved, rest values restored).
- **Unit, import round-trip**: loading `skinned_strip.glb` must reproduce the generator's exact joints, parents, IBMs, key times and values — and skinning a bind vertex through the imported `"bend"` clip lands on the hand-derived position. Fox import is sanity-checked (3 clips by name, unit rotations across all joints).
- **Smoke (`ge_gfx_tests`)**: skinned models render N frames with zero debug-layer errors, including a hand-animated palette and the static-submit bind-pose fallback.
- **Simulation baseline (`sandbox_tests`)**: the root-motion-driven capsule scenarios above. This executable lives at the **application layer** deliberately — it links `ge_anim` *and* `ge_physics`, which sibling modules' own test suites must not do, mirroring exactly the glue the sandbox owns.
- **Manual**: blend feel (idle↔walk↔run crossfade timing under real input) — a recorded sign-off, because no assertion can measure "feels right".
