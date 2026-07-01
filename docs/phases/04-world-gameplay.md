# Phase 4 — World & gameplay

> Status: Stub — expand before starting.

## Goal
The data-oriented world backbone: an ECS, scene/world management, integration of
the fixed-update loop with simulation systems, and input wired through to
components — ending in a controllable entity in a rendered scene.

## Scope
- In scope: ECS storage + queries (ADR-0008); scene/world management;
  fixed-timestep simulation systems; input mapping to components; the
  scripting/iteration layer (ADR-0009).
- Out of scope: physics simulation (Phase 5); AI behaviors (Phase 9).

## Cross-phase watch item
Carried over from Phase 3 (gfx): if a scene's world extent outgrows the
current single-frustum directional shadow map, that's the signal to build
cascaded shadow maps in `ge_gfx` (not a deliverable of this phase itself — just
flag it if hit while building test scenes here). See
`docs/phases/03-renderer.md` Out of scope notes and `plan.md`.

## Tech decisions
- **ECS storage model** — ADR-0008 (archetype vs sparse set vs hybrid).
- **Scripting / iteration model** — ADR-0009 (decide before gameplay logic).
- Entity handles are index + generation; systems iterate component arrays.

## Design
(To expand.) Components are POD data; systems are stateless passes over
component storage. The render and sim halves stay decoupled (fixed update +
interpolated render, alpha from the loop).

## Milestones
1. ECS storage + entity/component lifecycle + queries.
2. Scene/world management + serialization of entities (uses Phase 2 reflection).
3. Systems integrated into the fixed-update loop.
4. Input → components; a controllable entity.

## Test plan
Per `plan.md`: entity create/destroy; component add/remove; query iteration
correctness; storage/archetype invariants; input-mapping resolution.

## Open questions
- ECS storage (ADR-0008); scripting model (ADR-0009).

## Depends on
- Phases 2–3 (reflection/serialization, renderer), Phase 0 input/platform.
