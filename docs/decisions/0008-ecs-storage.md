# ADR-0008 — ECS storage model

> Status: Accepted
> Date: 2026-07-01

## Context
The ECS is the data-oriented backbone of the world/gameplay layer. Its storage
model governs iteration speed, add/remove cost, and memory layout. Phase 4's
scenes are small (single-digit to low-hundreds of entities: a controllable
character, lights, markers) — there is no archetype-scale data set yet, and
none is anticipated before Phase 5 (physics bodies) or Phase 9 (AI agents).

## Options considered
- **Archetype (SoA chunks)** — entities grouped by component set into contiguous
  chunks. Excellent iteration/cache behavior for large, stable component sets;
  structural changes (add/remove a component) move an entity's entire row
  between chunks, which is the more complex path to implement and test
  correctly (chunk migration, archetype graph, moved-row invariants).
- **Sparse set** — one dense array per component type (packed, contiguous,
  cache-friendly for single- and dual-component iteration) plus a sparse
  `entity index -> dense index` lookup array. Add/remove is O(1) (swap-remove
  at the end of one array, no cross-component data movement). Multi-component
  queries (3+ types) cost an extra `has()` check per candidate versus an
  archetype's guaranteed contiguity, but at Phase 4/5 scene sizes this is
  immaterial.
- **Hybrid** — archetypes for hot components, sparse sets for rare ones. Right
  complexity for a mature engine with a profiled hot set; premature here since
  no component has been shown to need archetype-grade iteration yet.

## Decision
Sparse-set storage. `Entity` is a generational handle (index + generation),
implemented by reusing the existing `Handle<Tag>` / `HandleTable<T, Tag>`
machinery from `core/handle.h` (`HandleTable<u8, EntityTag>` — the payload
byte is unused; the table's free-list and generation-check logic is what we
want, and it is already tested).

Each component type `T` gets a `ComponentPool<T>`: a dense `Array<T>`, a
parallel dense `Array<Entity>` (owning entity per slot, for swap-remove
fix-up), and a sparse `Array<u32>` indexed by `entity.index` holding the dense
slot or a sentinel. `World` looks pools up by a per-type id obtained without
RTTI (a monotonic counter, assigned the first time each type is requested via
a `static const u64 id = ...` local inside a function template — each
instantiation gets its own distinct static-storage variable, so unlike an
address-of-shared-code-body trick this is safe under MSVC's default
`/OPT:ICF` in Release builds, per ADR-0002's "no RTTI" rule) in a
`HashMap<u64, void*>`, with a small virtual `IComponentPool` interface used
only for cross-type bookkeeping (entity-destroy cleanup), never in
per-component hot loops — consistent with Golden Rule 4 (no virtual calls in
hot paths; the paths that iterate component data call directly into
`ComponentPool<T>`, not through the virtual interface).

Queries: `World::each<T>(fn)` iterates a single pool's dense array directly
(fully contiguous, zero indirection beyond the pool). `World::each<A, B>(fn)`
iterates the smaller of the two pools' dense arrays and probes `has()` on the
other — the standard sparse-set join. No archetype/query cache is built; at
current scene sizes it isn't needed.

## Consequences
- Add/remove is cheap and simple to reason about and test; no chunk migration
  code to get wrong.
- Single- and dual-component iteration is dense and cache-friendly; 3+ way
  queries degrade gracefully (extra `has()` checks) rather than requiring an
  archetype graph — acceptable until profiling says otherwise.
- If a future phase (physics broad-phase, large-scale AI) needs guaranteed
  contiguous multi-component iteration, revisit as a hybrid: keep sparse sets
  for gameplay/editor-facing components, add archetype storage for the hot
  simulation set. This ADR does not preclude that; it defers it until there's
  a measured reason.
- Networking replication (Phase 10) iterates components by type — sparse-set
  dense arrays are a natural fit for gathering per-type replicated state.
