# ADR-0001 — Layered architecture & downward-only dependencies

> Status: Accepted
> Date: (set on commit)

## Context
The engine must stay modular and scalable across a dozen-plus subsystems. The
default failure mode for engines this size is tangled dependencies that make any
change ripple everywhere and make modules impossible to test or reason about in
isolation.

## Options considered
- **Flat / ad-hoc** — modules include whatever they need. Fast early, becomes
  mud quickly.
- **Strict layered, downward-only** — modules depend only on layers strictly
  below them; siblings never depend on each other.
- **Full plugin / dependency-injection** — everything behind interfaces. Maximal
  decoupling but heavy boilerplate and indirection for a from-scratch effort.

## Decision
Strict downward-only layering, lowest first:
`platform -> core -> resources -> subsystems -> frameworks -> application`.
A module includes and links only layers below it. Sibling subsystems (graphics,
physics, audio, …) never depend on each other; they communicate through data
owned by the world/framework layer. No OS headers (`<windows.h>`) in public
headers — OS handles are exposed as `void*`. Enforced by the build: a module
links only lower-layer targets, so an illegal include fails to compile.

## Consequences
- Predictable builds, independently testable units, clear ownership.
- Costs some boilerplate: interfaces, `void*` handles, pImpl at OS boundaries.
- Cross-subsystem behavior must be composed at the world/framework layer rather
  than by reaching sideways.
