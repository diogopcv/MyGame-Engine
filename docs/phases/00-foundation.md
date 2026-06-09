# Phase 0 — Foundation skeleton

> Status: **Complete**

## Goal
A buildable engine skeleton: a window that opens, clears to a color, runs a
fixed-timestep loop with precise timing, and shuts down cleanly — plus the math
library the renderer will need. Establishes the module structure and the lowest
two layers (platform, core).

## Scope
- In scope (done): build scaffolding; platform window + message pump; high-res
  timer; virtual-memory wrappers; core types, logging, asserts, arena allocator;
  the fixed-timestep game loop.
- In scope (remaining): the **math library** — `vec2/3/4`, `mat4`, `quat`, and
  the transform/projection helpers needed to build an MVP matrix; plus standing
  up the **test harness** (doctest + CTest) that the whole project uses from here.
- Out of scope: anything touching the GPU (Phase 1); the wider allocator suite
  and job system (Phase 2).

## Tech decisions
- Memory via the arena allocator over platform virtual memory — ADR-0004.
- Math conventions (left-handed, row-major, row vectors, [0,1] depth) — ADR-0005.
- Logging is single-threaded for now; hardened once the job system exists (Phase 2).
- Math is scalar and header-mostly first; no SIMD until profiling justifies it.

## Design
Math types are plain, trivially copyable PODs with free-function operations (no
virtuals, no hidden state):
- `vec2/3/4` (f32): arithmetic, dot, cross (vec3), length, normalize.
- `mat4`: multiply, transpose, inverse, identity; constructors for
  translation/rotation/scale; `perspective_lh`, `look_at_lh`.
- `quat`: from axis-angle, normalize, multiply (compose), slerp, to-matrix.
- `transform` helper: compose (T·R·S per ADR-0005) and decompose.
The test harness: a `ge_core_tests` (and later per-module) executable linking
doctest, registered with CTest. The math tests are its first real content.

## Milestones (in-phase increments)
1. Stand up doctest + CTest with one trivial passing test (proves the harness).
2. `vec` types + ops + unit tests.
3. `mat4` + multiply/transpose/inverse + unit tests.
4. `quat` + normalize/compose/slerp + to-matrix + unit tests.
5. Transform + projection/view helpers; MVP round-trip test.

## Test plan
Per the `plan.md` critical-tests table:
- Unit: vector/matrix ops; quaternion normalize + compose; `mat4` inverse;
  transform compose/decompose; MVP round-trip; arena alignment, exhaustion
  assert, reset reuse.
- Smoke: the existing window boot → N frames → clean shutdown, no leaks.

## Open questions
- None blocking. (Handedness/storage resolved by ADR-0005; SIMD deferred.)

## Depends on
- Nothing above the platform layer.
