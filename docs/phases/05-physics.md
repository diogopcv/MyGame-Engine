# Phase 5 — Physics from scratch

> Status: Stub — expand before starting.

## Goal
A hand-written rigid-body physics engine: broadphase, narrowphase, contact
generation, a constraint solver, and integration — exposed through
rigidbody/collider components — ending in stacking boxes and a physics-driven,
controllable character.

## Scope
- In scope: broadphase (sweep-and-prune or dynamic BVH); narrowphase (GJK/EPA,
  SAT for boxes); contact manifold generation; constraint solver (sequential
  impulses / PGS); semi-implicit Euler integration; rigidbody/collider
  components. Continuous collision (CCD) later.
- Out of scope: soft bodies, fluids, vehicles.

## Tech decisions
- **Solver** — sequential impulses (the pragmatic, robust standard).
- **Broadphase** — start with sweep-and-prune; move to a dynamic BVH if needed.
- **Determinism** — keep the step fixed and seeded so simulation-baseline tests
  reproduce (relies on the fixed-timestep loop and ADR-0005 math).
- **Integration with ECS** — physics reads/writes rigidbody + collider
  components; it never depends on other subsystems sideways (ADR-0001).

## Design
(To expand.) Pipeline per step: broadphase pairs → narrowphase contacts →
manifold → solver iterations → integrate → write transforms back to components.

## Milestones
1. Collision primitives + narrowphase math (+ unit tests).
2. Broadphase + pair generation.
3. Contact manifolds.
4. Constraint solver + integration.
5. Rigidbody/collider components; controllable character.

## Test plan
Per `plan.md`: collision math (GJK distance, SAT/AABB overlap) unit tests; a
seeded box-stack settles to a recorded resting state within tolerance
(simulation baseline); penetration/restitution sanity.

## Open questions
- Broadphase structure; solver iteration counts; when CCD becomes necessary.

## Cross-phase watch item
Same cascaded-shadow trigger noted in Phase 4: a physics-scale level built here
is another likely point where the single-frustum shadow map stops being
enough. Whichever phase (4 or 5) hits it first is where cascaded shadows get
built in `ge_gfx`. See `docs/phases/03-renderer.md` and `plan.md`.

## Depends on
- Phase 4 (ECS/world), Phase 0 (math).
