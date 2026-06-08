# ADR-0008 — ECS storage model

> Status: Proposed (decide during Phase 4)
> Date: (set on commit)

## Context
The ECS is the data-oriented backbone of the world/gameplay layer. Its storage
model governs iteration speed, add/remove cost, and memory layout.

## Options considered
- **Archetype (SoA chunks)** — entities grouped by component set into contiguous
  chunks. Excellent iteration/cache behavior; structural changes (add/remove a
  component) move data between archetypes.
- **Sparse set** — per-component dense arrays with sparse lookup. Cheap
  add/remove; iteration slightly less cache-optimal across multiple components.
- **Hybrid** — archetypes for hot components, sparse sets for rare/optional ones.

## Decision
TBD in Phase 4. Record the chosen layout, the entity-handle scheme
(index + generation), and the query/iteration API here.

## Consequences
To be filled when decided. Networking replication (Phase 10) iterates components,
so the storage choice affects how replicated state is gathered.
