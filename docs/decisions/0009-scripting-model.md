# ADR-0009 — Scripting / iteration model

> Status: Proposed (decide before Phase 4 gameplay work)
> Date: (set on commit)

## Context
Iteration speed on gameplay logic is a major quality-of-life factor. The choice
also affects the reflection/binding surface and the build.

## Options considered
- **Embedded Lua** (e.g. via sol2) — fast iteration, mature, easy hot-reload;
  adds a runtime and a binding layer.
- **Custom bytecode VM** — full control, large effort, easy to over-invest in.
- **Hot-reloadable C++ gameplay DLLs** — native speed, no second language;
  requires careful state hand-off across reloads.
- **Pure compiled C++** — simplest, slowest iteration.

## Decision
TBD before gameplay work begins in Phase 4. Record the choice, the binding
mechanism (likely driven by ADR-0007 reflection), and the hot-reload strategy.

## Consequences
To be filled when decided. Bindings depend on the reflection system; pick that
(ADR-0007) first.
