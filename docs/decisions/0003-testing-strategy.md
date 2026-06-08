# ADR-0003 — Testing strategy & framework

> Status: Accepted
> Date: (set on commit)

## Context
Most of an engine is not unit-testable by `assertEquals` — rendering, audio,
input feel, and animation blending need a human or a recorded reference. We
still want an objective "done" gate per phase.

## Decision
Tiered testing, each phase using the tiers that fit it:
- **Unit** — deterministic, pure logic (math, allocators, containers,
  serialization, collision math, pathfinding, netcode). The bulk of "critical
  tests"; whole suite runs in well under a second.
- **Simulation baselines** — fixed, seeded runs compared to a recorded end
  state within tolerance (e.g. physics). Relies on fixed-timestep determinism.
- **Golden-image** — render a known scene offscreen, compare to a reference PNG
  within per-pixel tolerance. The only sane automated check for rendering.
- **Smoke** — boot, run N frames, shut down; no crash, no assert, no leak, zero
  DX12 debug-layer errors.
- **Manual** — feel/look, recorded as a per-phase sign-off.

**Framework:** doctest (header-only, near-zero compile cost), via CMake
`FetchContent` or vendored under `third_party/`. Per-module `ge_<module>_tests`
executables registered with `enable_testing()` + CTest. GoogleTest is the
fallback if heavier fixtures are ever needed.

A phase is done only when its critical tests (listed in `plan.md`) are green.

## Consequences
- Fast feedback on the deterministic core; objective phase gates.
- Some infrastructure to build: the golden-image comparator (Phase 1) and the
  simulation-baseline harness (Phase 5).
- Tests are sized to risk, not coverage numbers; test-writing must never stall
  the vertical slice.
