# ADR-0009 — Scripting / iteration model

> Status: Accepted
> Date: 2026-07-01

## Context
Iteration speed on gameplay logic is a major quality-of-life factor. The choice
also affects the reflection/binding surface and the build.

## Options considered
- **Embedded Lua** (e.g. via sol2) — fast iteration, mature, easy hot-reload;
  adds a runtime and a binding layer generated off the reflection system
  (ADR-0007) that doesn't exist yet, and a third-party dependency for
  something not yet proven necessary.
- **Custom bytecode VM** — full control, large effort, easy to over-invest in
  before there's enough gameplay logic to justify it.
- **Hot-reloadable C++ gameplay DLLs** — native speed, no second language;
  requires careful state hand-off across reloads (vtables, static data,
  in-flight component pointers) — real complexity, worth it once gameplay
  code churns often enough to make edit-compile-relink painful.
- **Pure compiled C++** — simplest, slowest iteration, but Phase 4's gameplay
  surface is small (one movement system, one camera-follow system) and the
  engine already rebuilds fast enough that "slow iteration" isn't yet an
  observed problem.

## Decision
Pure compiled C++ for Phase 4. Systems are plain free functions operating on
`World&` (see `docs/phases/04-world-gameplay.md`) — no interpreted language,
no hot-reload DLL boundary, no bytecode VM. This is the smallest thing that
lets Phase 4 hit its DoD (a controllable entity in a rendered scene) without
building a scripting runtime speculatively.

Revisit when gameplay code volume/iteration pain actually appears — most
likely triggered by Phase 6-9 content work (animation state machines, AI
behavior trees) rather than Phase 4 itself. When that happens, prefer
hot-reloadable C++ DLLs over embedding Lua, since bindings would otherwise
have to be generated and kept in sync with the reflection system for no
native-speed benefit; only reach for Lua if non-programmer content authors
need to script without recompiling.

## Consequences
- No binding layer to design or maintain yet; reflection (ADR-0007) stays
  scoped to serialization/editor use for now.
- Gameplay logic changes require a full recompile — acceptable at current
  scope; tracked as the trigger condition above for revisiting this ADR.
- Systems as plain functions keep Phase 4 minimal: no scheduler abstraction,
  no per-system lifecycle — just functions called in sequence from the
  fixed-update loop, matching the "don't design for hypothetical future
  requirements" project convention.
