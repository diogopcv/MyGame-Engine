# ADR-0004 — Memory management (arenas / allocators)

> Status: Accepted
> Date: (set on commit)

## Context
A data-oriented engine wants control over allocation patterns and cache
behavior, and wants to avoid the unpredictable cost and fragmentation of
scattered `new` / `malloc`.

## Decision
Explicit allocators built on the platform's virtual memory:
- **Arena (linear/bump)** — the default and first allocator; O(1) alloc, freed
  all at once via `reset()`. Backbone for per-frame scratch, per-level data, and
  transient work. (Implemented in Phase 0.)
- **Allocator suite** — pool (fixed-size), stack (LIFO scopes), and a
  general-purpose allocator, added in Phase 2 as real consumers appear.
- A per-frame scratch arena is `reset()` once per frame.
- Containers and subsystems take an allocator rather than allocating globally.
- Backing memory comes from `platform::mem_alloc` (VirtualAlloc), not the CRT
  heap, so the platform layer owns the lowest-level allocation.

## Consequences
- Excellent cache behavior and no hidden allocations in hot paths.
- Manual lifetime discipline; a Debug leak-tracking allocator (or ASan) is part
  of the verify step.
- Most systems never call `new` directly — they sub-allocate from an arena or
  pool passed in.
