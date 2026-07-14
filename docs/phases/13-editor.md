# Phase 13 — Editor & tools framework

> Status: Expanded **ahead of schedule** 2026-07-14 (full pre-expansion
> requested, deliberately overriding the template's just-in-time rule).
> Re-validate scope and tech decisions against the codebase when this phase
> actually begins — and note this phase *opens* by resolving plan.md's
> standing "editor scope" question as an ADR. Not started.

## Goal
An editor: scene editing with transform gizmos, reflection-driven property
panels, an asset browser, undo/redo, play-in-editor, and serialization-
backed save/load — build and save a playable scene entirely in the editor,
then play it without leaving the app.

## Scope
- In scope: a new `editor/` application target (Layer 5, alongside
  `sandbox/`) hosting the engine in-process; Dear ImGui with docking as
  the editor shell (multi-panel: viewport, hierarchy, inspector, asset
  browser, console/log — the Phase 12 views dock in); the 3D viewport as
  a render-to-texture with editor camera; mouse picking (ray from the
  camera through physics raycast, AABB fallback for collider-less
  entities); translate/rotate/scale gizmos with axis/plane constraints
  and snapping; reflection-driven inspector (walk `TypeInfo` fields →
  edit widgets by field type, writes back through the same reflection);
  an undo/redo command stack (property edits, entity create/delete,
  component add/remove, transform drags — coalesced per drag); scene
  save/load extended to *all* reflected component types (a component-type
  registry generalizing Phase 4's Transform-only `Scene::save/load`);
  asset browser over the Phase 11 manifest (+ thumbnails deferred);
  play-in-editor — snapshot the scene to memory, run the game systems,
  stop restores the snapshot.
- Out of scope (deferred): multi-viewport/multi-window beyond ImGui
  docking; prefabs/nested scenes; terrain/foliage or any bespoke asset
  editors; animation graph or material editors; multi-user editing;
  scripting console; build-from-editor (cooker stays CLI); thumbnails
  and asset previews; editor settings persistence beyond an ini.

## Module layout
- `editor/` — the app: `main.cpp`, panels (`panel_viewport.cpp`,
  `panel_hierarchy.cpp`, `panel_inspector.cpp`, `panel_assets.cpp`),
  editor camera, selection state. Application layer: it may link
  everything below, and all "editor knows world + physics + gfx" glue
  lives here, per the ADR-0009 precedent.
- `engine/world` additions (framework-level, engine-side so the runtime
  can load what the editor saves): the **component-type registry**
  (`register_component(TypeInfo*, pool accessors)`) and generalized
  `Scene::save/load` over every registered type — this is runtime
  functionality the editor merely exercises.
- `engine/core` addition: the undo *pattern* is editor-only (lives in
  `editor/`), but property write-back may need small reflection helpers
  (`set_field_by_offset` bounds-checked) worth placing in core.
- `engine/gfx`: render-to-texture viewport path — largely exists (golden
  tests render offscreen); needs an SRV-consumable color target for the
  ImGui image, plus an editor-camera hook. Gizmo rendering rides the
  Phase 12 debug-draw batcher.

Third-party: Dear ImGui (docking branch) — vendored per policy (debug/
editor UI is an explicitly allowed category); the Phase 8 backend is
reused. ImGuizmo is *not* vendored — gizmo math is exactly the kind of
math this engine writes itself, and it's a unit-test target in plan.md.

## Tech decisions
- **Resolve ADR "editor scope" first.** This phase's opening act:
  in-process editor hosting the engine (not a separate process over IPC),
  ImGui shell, engine+editor in one repo/build. Record as an ADR with the
  rationale (velocity, reuse of debug tooling, single build) — plan.md's
  open-questions list has carried this since Phase 0.
- **Undo = command objects with serialized before/after payloads.** A
  property edit stores (entity, TypeInfo, field, old bytes, new bytes)
  via reflection — one generic command covers every component field ever
  added. Entity-level ops store full component snapshots (Phase 2
  serialization). Gizmo drags coalesce into one command on mouse-up.
- **Play-in-editor via serialize/restore, not world copy.** Enter play:
  serialize the scene to a memory buffer (the same path as save-to-disk —
  eating our own dog food catches serialization gaps early); tick the
  real game systems; stop: clear + deserialize. Pause/step are flags on
  the fixed-update loop. Editor-only state (selection, camera) lives
  outside the world and survives untouched.
- **Selection & picking:** physics raycast against colliders when
  present; otherwise a ray-vs-model-AABB fallback (math already exists in
  `ge_physics`/core — the app glue composes it, no sideways dep).
- **The inspector writes through reflection, not setters** — fields are
  plain data (golden rule 4), so writing bytes at `offsetof` is the
  system working as designed; components needing post-edit fixup get an
  optional per-type "edited" hook in the component registry.
- **Scene file format:** the existing binary serialization, wrapped in
  the Phase 11 blob container (a scene is just another asset with a
  GUID, cookable and hot-reloadable like everything else).

## Design
```
editor frame:
  input -> ImGui shell (docked panels)
  viewport panel: editor camera -> renderer -> RT -> ImGui::Image
      click -> ray -> physics raycast | AABB fallback -> selection
      gizmo: selected transform -> axis/plane drag math -> preview
             mouse-up -> UndoStack::push(TransformCommand)
  hierarchy: world entity roster (Scene name registry) -> select/rename/
             create/delete (undoable)
  inspector: selection -> per-component TypeInfo walk -> widgets
             edit -> old/new bytes -> UndoStack::push -> write field
  assets: Phase 11 manifest list -> drag into scene (mesh -> entity)
  play: [snapshot] -> game systems tick (fixed accumulator) -> [restore]
```

## Milestones
1. **Editor shell.** `editor/` target, ImGui docking, viewport RT +
   editor camera, log panel; the Phase 4 scene renders in a docked
   viewport. (Opens with the editor-scope ADR.)
2. **Hierarchy + inspector.** Component-type registry in `ge_world`;
   entity list, selection, reflection-driven inspector editing live
   component fields.
3. **Picking + gizmos.** Click-select in viewport; translate/rotate/scale
   gizmos with snapping; gizmo math unit-tested; debug-draw rendering.
4. **Undo/redo + scene save/load.** Command stack over property edits,
   entity ops, and gizmo drags; generalized `Scene::save/load` over all
   registered components; scene round-trips through the blob format.
5. **Asset browser + play-in-editor + sign-off.** Manifest browser +
   drag-to-instantiate; snapshot/play/stop/restore; the DoD scenario —
   build a scene from assets, save, play, stop, everything restored;
   manual editing-workflow review.

## Test plan
Per plan.md's Phase 13 critical-test line:
- **Unit (undo/redo round-trip):** for each command type — apply, undo,
  assert bit-equal restored state (via serialization compare); redo
  re-applies exactly; a scripted 20-op random sequence undone to the
  start matches the initial scene snapshot (the strong invariant).
- **Unit (save → load equality):** scenes with every registered component
  type, multiple entities, name registry — save, load into a fresh world,
  serialize both, byte-compare; empty scene; entity-with-every-component;
  version-mismatch rejection.
- **Unit (gizmo math):** screen-ray → axis/plane constraint projection
  against hand-computed cases (axis drag from a known camera, plane drag,
  rotation angle from drag arc, snapping quantization); degenerate
  view-parallel axis case falls back sanely.
- **Unit (component registry):** registered types enumerate; add/remove
  through the registry matches direct pool ops; the "edited" hook fires.
- **Smoke:** editor boots, opens the default scene, N frames, clean exit,
  0 debug-layer errors.
- **Manual:** the editing-workflow review (build/save/play the DoD scene)
  — recorded sign-off, per plan.md.

## Open questions
- Does `Entity` identity survive save/load (stable per-scene entity ids
  in the file, remapped on load) — must be answered in Milestone 4; the
  handle generations are runtime-only, so the file needs its own id
  space.
- Undo granularity for text-field edits (per keystroke vs per focus-loss)
  — decide by feel in Milestone 4.
- Whether play-in-editor ticks physics/AI in the *same* world memory or a
  scratch arena copy — serialize/restore makes same-memory safe, but
  measure restore time on a big scene before committing.

## Cross-phase watch items
- Phase 14's vertical slice authors its levels in this editor — the
  first real content pressure; missing conveniences discovered there feed
  back here rather than blocking the slice.
- The component registry + generalized scene serialization is exactly
  what Phase 10 replication also wants to enumerate — if net shipped
  first with its own registry, merge them here (one registry, two
  consumers).

## Depends on
- Phase 2 (reflection — the inspector *is* reflection; serialization —
  undo and save/load).
- Phase 4 (world/scene), Phase 5 (picking raycasts), Phase 8 (ImGui
  backend), Phase 11 (manifest/asset browser, blob scene format),
  Phase 12 (debug draw for gizmos, dockable tool views).
