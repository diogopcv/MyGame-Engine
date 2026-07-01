# Phase 5 — Physics from scratch

> Status: **Complete.** All 5 milestones done: collision primitives + GJK,
> broadphase, contact manifolds, the sequential-impulse solver +
> integration, and the ECS-facing rigidbody glue + a physics-driven capsule
> character in the sandbox.

## Goal
A hand-written rigid-body physics engine: broadphase, narrowphase, contact
generation, a constraint solver, and integration — exposed through
rigidbody/collider components — ending in stacking boxes and a physics-driven,
controllable character.

## Scope
- In scope: broadphase (sweep-and-prune, moving to a dynamic BVH only if
  profiling shows it's needed); narrowphase (GJK for general convex distance,
  SAT for box-box); contact manifold generation (clipping for face contacts,
  closest-point for vertex/edge); constraint solver (sequential impulses /
  PGS with Baumgarte or split-impulse position correction); semi-implicit
  Euler integration; rigidbody/collider components on the Phase 4 ECS.
  Continuous collision (CCD) is deferred — flagged as a later addition once
  fast-moving objects (e.g. a thrown/character-launched object) expose
  tunneling, not built speculatively.
- Out of scope: soft bodies, fluids, vehicles, joints/constraints beyond
  what the character controller needs (a full constraint/joint system is a
  post-Phase-5 stretch item if gameplay demands it).
- Shape set: **sphere**, **axis-aligned/oriented box**, and **capsule**
  (added in Milestone 5 for the controllable character). Convex hulls are
  out of scope — the shape set stays small and purpose-built, per the
  no-speculative-generality principle. Within the capsule shape, only
  capsule-vs-box narrowphase/manifold generation is implemented — the
  Milestone-5 scene only ever needs one capsule (the player) against box
  terrain, so capsule-vs-sphere and capsule-vs-capsule are deliberately
  unbuilt (`PhysicsWorld`'s dispatch falls back to "no collision" for those
  pairs rather than guessing); add them if a future scene actually needs
  more than one capsule.

## Module layout
New `engine/physics` module, **Layer 3 (Subsystems)** — same layer as
`ge_gfx` and `ge_input`. It depends on `ge_core` only (math, containers,
allocators) and, per ADR-0001, never depends sideways on `ge_world` or
`ge_gfx`. This mirrors how `ge_gfx` stays engine-agnostic of the ECS:

- `engine/physics/include/physics/shapes.h` — `AABB`, `Sphere`, `Box` (OBB:
  center + half-extents + orientation quaternion), `Capsule` (segment + radius,
  added Milestone 5). Pure data, no logic.
- `engine/physics/include/physics/collision.h` + `src/collision.cpp` —
  narrowphase: `aabb_overlap`, `sphere_sphere`, `sphere_box`, `box_box` (SAT),
  `capsule_box` (Milestone 5), each returning a `Contact` (intersecting flag,
  world-space normal, penetration depth). Milestone 1 (capsule_box added
  Milestone 5).
- `engine/physics/include/physics/gjk.h` + `src/gjk.cpp` — support-function-
  based GJK distance query between two convex shapes, for the general
  (non-box-box) separated case; `support_sphere`/`support_box`/
  `support_capsule`. Milestone 1 (support_capsule added Milestone 5, used to
  cross-validate the new capsule_box() against an independent algorithm —
  see Milestone 5's test notes).
- `engine/physics/include/physics/broadphase.h` + `src/broadphase.cpp` —
  sweep-and-prune pair generation over a set of AABBs. Milestone 2.
- `engine/physics/include/physics/manifold.h` + `src/manifold.cpp` — contact
  manifold generation (up to 4 points per pair) from narrowphase results;
  `manifold_capsule_box` added Milestone 5. Milestone 3.
- `engine/physics/include/physics/solver.h` + `src/solver.cpp` — sequential-
  impulse velocity solver + position correction; `PhysicsWorld::step(dt)`
  drives broadphase → narrowphase → manifold → solve → integrate. Milestone 4.
- `engine/physics/include/physics/rigidbody.h` — `RigidBody` **data** (shape,
  transform, velocities, inverse mass/inertia, material) plus
  `make_sphere_body`/`make_box_body`/`make_capsule_body` factories and
  `apply_inv_inertia()`. Milestone 4 (capsule support added Milestone 5).
- `engine/physics/include/physics/physics_world.h` + `src/physics_world.cpp`
  — `PhysicsWorld`: owns bodies, runs `step(dt)`. Milestone 4.

The physics core (`ge_physics`) stays a standalone simulation library with no
knowledge of `Entity`/`World`. Milestone 5 settled the ECS-facing glue's
final placement: `ge::world::components.h` gained one new component,
`RigidBodyRef { u32 body_id }` — a bare opaque index, not a copy of or
pointer into any `ge::physics` type, so `ge_world` still doesn't need to
depend on `ge_physics` at all (ADR-0001 would *permit* that downward
dependency, since Layer 4 may depend on Layer 3, but nothing in the ECS
itself needs to). The system functions that actually resolve that id against
a `PhysicsWorld` (`system_move_player_physics`, driving the character from
input, and `system_sync_transform_from_rigidbody`, copying the resulting
`RigidBody` state back into `Transform` for rendering) live in the sandbox,
exactly where `system_move_player` already lived in Phase 4 — the app is the
one place that legitimately owns both a `World` and a `PhysicsWorld`.

## Tech decisions
- **Solver** — sequential impulses (the pragmatic, robust standard used by
  Box2D/Bullet-class engines): iterate over contacts applying velocity-level
  impulses until converged, rather than a full LCP solve.
- **Broadphase** — start with sweep-and-prune (sort-and-sweep on AABB
  endpoints along one axis, coherent frame-to-frame); move to a dynamic BVH
  only if profiling shows pair generation is a bottleneck. No speculative
  BVH before that's measured.
- **Narrowphase** — SAT (15-axis: 3 + 3 face normals + 9 edge-cross-products)
  for box-box, since it gives an exact minimum-translation-vector directly
  (no EPA needed) and boxes are the Milestone-5 stacking-test shape. GJK
  (support-function based, Ericson-style closest-point-to-origin simplex
  reduction) for the general separated-distance query between any two convex
  shapes in the shape set — this is what plan.md's "GJK distance" critical
  test targets. EPA (penetration depth from GJK) is deferred: it's only
  needed for sphere/capsule vs. box penetration depth, and those cases are
  cheap to solve directly with closest-point math instead (sphere-sphere,
  sphere-box), so EPA isn't built until a shape pair actually needs it.
- **Determinism** — the step is fixed-timestep and seeded so simulation-
  baseline tests reproduce (relies on the fixed-timestep loop already in the
  sandbox and ADR-0005 math conventions: left-handed, row-major, row-vector).
- **Integration with ECS** — physics reads/writes rigidbody + collider
  components; it never depends on other subsystems sideways (ADR-0001). The
  physics core is engine-agnostic (see Module layout); only the Milestone-5
  glue touches `ge::world`.
- **Contact representation** — world-space contact normal points from shape
  A to shape B by convention (matches the `Contact::normal` doc comment), so
  every downstream consumer (manifold, solver) has one fixed sign convention
  to reason about.

## Design
Pipeline per fixed step (Milestones 2-4 build this up incrementally; Milestone
1 only delivers the narrowphase math the rest of the pipeline calls into):

```
broadphase (AABB sweep-and-prune) -> candidate pairs
  -> narrowphase (SAT for box-box; GJK distance / closest-point math for
     sphere-sphere, sphere-box) -> Contact (normal, penetration)
  -> manifold generation (clip / reduce to <=4 stable contact points)
  -> sequential-impulse solver (velocity iterations, then position
     correction) -> updated linear/angular velocities
  -> semi-implicit Euler integration -> write Transform back to the ECS
```

## Milestones
All 5 done:
1. **Collision primitives + narrowphase math.** `AABB`, `Sphere`, `Box`
   shapes; `aabb_overlap`, `sphere_sphere`, `sphere_box`, `box_box` (SAT);
   GJK distance for the general convex case.
2. **Broadphase + pair generation** (sweep-and-prune over AABBs).
3. **Contact manifolds** (clipping for face-face, closest-point for
   vertex/edge cases).
4. **Constraint solver** (sequential impulses) + semi-implicit Euler
   integration; `PhysicsWorld::step(dt)` end-to-end.
5. **Rigidbody/collider ECS components + system glue; controllable
   character.** Added the `Capsule` shape + `capsule_box` narrowphase/
   manifold (Milestone 1-3 style additions, extended for the new shape);
   `RigidBody` gained `ShapeType::Capsule` + `make_capsule_body()`
   (rotation-locked: `inv_inertia_local` is always zero, so a
   player-controlled capsule can't be tipped over by solver torques —
   its facing is driven directly by the input system instead, same as any
   kinematic-feeling character controller); `world::RigidBodyRef` component
   + sandbox-hosted sync systems (see Module layout); the sandbox's player
   is now a physics-driven capsule falling under gravity and walking on a
   static ground box, verified both by a `PhysicsWorld` unit test and by
   running the actual sandbox (dropped from y=3, settled at y=0.89 against
   a hand-computed expected 0.9).

## Test plan (as built)
Per `plan.md`'s Phase 5 critical-test line — 54 physics test cases / 178
assertions across `ge_physics_tests`, all green:
- **Unit (Milestone 1):** `aabb_overlap` true/false cases including edge-
  touching; `sphere_sphere`/`sphere_box` penetration depth + normal on known
  geometry; `box_box` SAT on axis-aligned and rotated boxes (separated,
  touching, penetrating); GJK distance on known separated convex pairs
  (sphere-sphere, sphere-box, box-box) cross-checked against the closed-form
  answer, plus GJK intersecting cases. Compound-rotation SAT cases (the ones
  needing the 9 edge-cross-product axes, too easy to get wrong by hand) are
  cross-validated against GJK's independent algorithm instead of hand-derived
  — same strategy reused for the new capsule_box() in Milestone 5.
- **Unit (Milestone 2):** broadphase pair-generation correctness (no
  missed/duplicate pairs vs. a brute-force O(n²) reference).
- **Unit (Milestone 3):** manifold point count/position, including two
  hand-derived exact-corner-position face-clipping cases.
- **Unit (Milestone 4):** solver-level exact hand-derived outcomes (elastic/
  inelastic collision, static-body immovability, Baumgarte bias) on
  head-on constraints isolated from manifold generation.
- **Unit (Milestone 5):** capsule_box narrowphase (standing/separated/flush
  cases) cross-validated against GJK; manifold_capsule_box surface-midpoint
  position.
- **Baseline (Milestone 4-5):** dropped sphere, dropped box, a 2-box stack,
  and a dropped character capsule each settle to their analytic resting
  height within tolerance with near-zero residual velocity — the box-stack
  and capsule cases are literally Phase 5's DoD scenarios. The capsule
  result was additionally confirmed by running the actual sandbox (not just
  the unit test): dropped from y=3, settled at y=0.89 against a
  hand-computed expected 0.9.

## Resolved / no-longer-open questions
- Solver iteration counts: 8 velocity iterations was sufficient for every
  baseline test (single contacts through a 2-box stack) without tuning;
  revisit only if a deeper stack or a stiffer scene shows instability.
- Sweep-and-prune vs. BVH: never became a bottleneck at this milestone's
  scene scale; still deferred to profiling data, unchanged.
- CCD: still deferred; no scene in Phase 5 exposed tunneling.

## Cross-phase watch item
Same cascaded-shadow trigger noted in Phase 4: a physics-scale level built
here is another likely point where the single-frustum shadow map stops being
enough. Whichever phase (4 or 5) hits it first is where cascaded shadows get
built in `ge_gfx`. See `docs/phases/03-renderer.md` and `plan.md`.

## Depends on
- Phase 4 (ECS/world), Phase 0 (math).
