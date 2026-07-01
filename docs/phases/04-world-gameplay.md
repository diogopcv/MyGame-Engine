# Phase 4 — World & gameplay

> Status: Complete

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
flag it if hit while building test scenes here). Phase 4's test scene is a
single small entity near the origin, so this is not expected to trigger.
See `docs/phases/03-renderer.md` Out of scope notes and `plan.md`.

## Tech decisions
- **ECS storage model** — ADR-0008 (Accepted): sparse-set storage, entity =
  generational handle reusing `core/handle.h`.
- **Scripting / iteration model** — ADR-0009 (Accepted): plain compiled C++
  systems (free functions); no embedded language or hot-reload DLL yet.

## Design

### New modules
Two new modules land in this phase:

- `engine/input` (**Layer 3 — Subsystems**): the input-mapping layer promised
  by `plan.md`'s subsystem checklist ("Input — full input system Phase 4").
  Depends only on `core` (for `StringId`) — it does *not* depend on
  `platform::Window` directly in its resolution logic, so it stays unit
  testable without a real OS window (see below).
- `engine/world` (**Layer 4 — Frameworks**): the ECS, scene, and baseline
  gameplay components. Depends only on `core`. It deliberately does **not**
  depend on `ge_gfx` — component data (e.g. `Transform`) is subsystem-agnostic;
  the sandbox (Layer 5, which already links `ge_gfx`) is what bridges a
  `Transform` to `Renderer::set_model_transform` and a `Camera`. This keeps
  `ge_world` reusable by any future rendering/physics backend and avoids a
  sideways coupling that isn't needed yet.

### ECS core (`ge_world`)
- `Entity` = `Handle<EntityTag>` (index + generation, from `core/handle.h`).
  Entity lifecycle (create/destroy/is_alive) is `HandleTable<u8, EntityTag>` —
  the stored byte is unused; we reuse the table purely for its already-tested
  free-list + generation-check machinery rather than re-deriving it.
- `ComponentPool<T>`: dense `Array<T> dense_`, parallel dense `Array<Entity>
  dense_entities_`, sparse `Array<u32> sparse_` (entity index -> dense index,
  `kInvalid = ~0u` sentinel). `add`/`remove`/`get`/`has` are O(1); `remove` is
  a swap-with-last plus a sparse-index fix-up for the moved entity.
- `IComponentPool` is a two-method virtual interface (`remove_entity`,
  `~IComponentPool`) used only by `World::destroy_entity` to strip a
  destroyed entity from every pool that might hold it — an O(#component
  types) sweep, not a hot per-frame loop, so a virtual call here doesn't
  violate Golden Rule 4.
- `World` owns the entity allocator plus a `HashMap<u64, void*> pools_` keyed
  by a per-type id (address of a function-local `static`, no RTTI) and an
  `Array<IComponentPool*> all_pools_` for the destroy sweep. API:
  - `Entity create_entity()`, `void destroy_entity(Entity)`, `bool is_alive(Entity) const`
  - `template<T> T& add_component(Entity, const T&)`
  - `template<T> void remove_component(Entity)`
  - `template<T> T* get_component(Entity)`
  - `template<T> bool has_component(Entity) const`
  - `template<T, Fn> void each(Fn&&)` — dense iteration over one pool
  - `template<A, B, Fn> void each(Fn&&)` — join over the smaller pool's dense
    array, `has<B>()`-checked against the other (see ADR-0008)

### Baseline components (`world/components.h`)
- `Transform { vec3 position; quat rotation; vec3 scale; }`, reflected via
  `GE_REFLECT_BEGIN/GE_FIELD/GE_REFLECT_END` (Phase 2 reflection, ADR-0007) so
  it round-trips through `core/serialize.h` unmodified.
- `CharacterController { f32 move_speed; f32 look_sensitivity; f32 yaw; f32 pitch; }`
  — tags an entity as player-movable and carries its tunables; the movement
  system (below) reads/writes it alongside the entity's `Transform`.

### Scene (`world/scene.h`)
`Scene` wraps a `World` plus a `HashMap<StringId, Entity>` name registry (for
looking up well-known entities like `"player"`). `Scene::save`/`Scene::load`
walk every live entity and, for each registered component type present, write
a presence flag followed by the reflected component via
`serialize()`/`deserialize()`. Phase 4 registers exactly one component type
(`Transform`) — enough to prove the reflection-to-ECS serialization path end
to end. Extending the registered-component list is a mechanical follow-up
once more component types exist; not speculatively built now.

### Input mapping (`ge_input`)
`InputMap` binds a `StringId` action to a `platform::KeyCode` (digital) or a
`StringId` axis to a positive/negative `KeyCode` pair (analog, e.g.
`"MoveForward"` = W(+)/S(-)). Resolution is a template function:

```cpp
template <typename KeySource>
void resolve(const KeySource& keys); // keys.key_down(KeyCode) -> bool
```

so tests supply a trivial fake key source (no OS window needed) while the
sandbox calls `resolve(window)` directly against `platform::Window`, which
already exposes a matching `key_down`. `action_down(name)` / `axis(name)`
read the resolved cache. This is the "input-mapping resolution" critical test
from `plan.md`.

### Systems in the fixed-update loop
Per ADR-0009, systems are plain functions, not a scheduler class:
`world::system_move_player(World&, const InputMap&, f32 dt)` reads the
player's `CharacterController` + `InputMap` axes and updates its `Transform`.
The sandbox's existing fixed-timestep accumulator (already in `main.cpp`)
calls this once per fixed tick, exactly where it currently calls
`camera_fly_update` — no new loop/scheduler construct introduced.

### Controllable entity (sandbox integration)
The renderer currently supports one loaded model and one `set_model_transform`
call per frame (Phase 3 scope) — Phase 4 does not extend the renderer to
multi-entity rendering, since the DoD only requires *a* controllable entity,
not a populated scene. Sandbox creates a `Scene`, spawns a `"player"` entity
with `Transform` + `CharacterController`, and each fixed tick: resolves
`InputMap` against the window, runs `system_move_player`, then converts the
entity's `Transform` to a matrix (`trs()`, Phase 2) for
`renderer.set_model_transform()`. The camera follows the entity at a fixed
third-person offset computed from the entity's yaw, replacing the free-fly
camera as the primary control scheme.

## Milestones
1. [x] ECS storage + entity/component lifecycle + queries. **(`ge_world`)**
2. [x] Input-mapping subsystem. **(`ge_input`)**
3. [x] Scene/world management + serialization of entities (uses Phase 2 reflection).
4. [x] Systems integrated into the fixed-update loop; input → components; a
   controllable entity replaces the free-fly camera in the sandbox.

## Test plan
Per `plan.md`: entity create/destroy; component add/remove; query iteration
correctness; storage/archetype invariants (swap-remove doesn't corrupt a
live entity's dense slot); input-mapping resolution. Added for this phase's
own scope: scene save/load round-trip for `Transform`. All green — 12 tests
in `ge_world` (entity lifecycle, component add/remove/swap-remove, `each<T>`/
`each<A,B>` queries) + 4 in `ge_world` for `Scene` save/load + 5 in
`ge_input` for action/axis resolution.

## Open questions
None outstanding — ADR-0008 and ADR-0009 are both Accepted (see above).
Deferred, not blocking: Scene's serialized-component list is Transform-only
(see Scene section) — extend it mechanically as more component types earn a
place in saved scenes.

## Depends on
- Phases 2–3 (reflection/serialization, renderer), Phase 0 input/platform.
