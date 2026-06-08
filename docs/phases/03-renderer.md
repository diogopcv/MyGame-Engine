# Phase 3 — Renderer proper

> Status: Stub — expand before starting.

## Goal
Turn the Phase 1 DX12 path into a real renderer: a clean RHI abstraction, a
render/frame graph, mesh/material/texture resources, model import, a camera, and
basic lighting — able to load and render a real 3D model with a fly-through
camera.

## Scope
- In scope: RHI abstraction over the Phase 1 device; render/frame graph
  (automatic barriers + transient resource management); mesh/material/texture
  resources; model loader; camera; forward (or forward+) lighting; shadows.
- Out of scope: advanced GI, post-processing stacks (later).

## Tech decisions
- **Binding model** finalized — ADR-0006 (bindless vs tables) decided no later
  than here.
- **Lighting path** — start with simple forward, consider forward+ before adding
  many lights. Record the choice when expanded.
- **Model import** — cgltf/assimp for parsing only (third-party, ADR-0002 policy);
  the renderer itself stays from scratch.

## Design
(To expand.) Render graph: passes declare reads/writes; the graph derives
barriers and aliases transient targets. Materials reference shaders + parameter
blocks.

## Milestones
1. RHI abstraction extracted from Phase 1.
2. Render graph with transient resources + auto barriers.
3. Mesh/material/texture resources + model import.
4. Camera + forward lighting.
5. Shadows.

## Test plan
Per `plan.md`: golden images for reference scenes; render-graph resource-lifetime
and barrier assertions; mesh/material/texture load round-trip.

## Open questions
- Final binding model (ADR-0006); lighting path; shadow technique.

## Depends on
- Phases 1–2 (DX12 path, resource/handle system, job system).
