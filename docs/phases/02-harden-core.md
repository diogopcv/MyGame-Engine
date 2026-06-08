# Phase 2 — Harden the core

> Status: Stub — expand before starting.

## Goal
With real consumers now exercising the foundation, build the production-grade
core: a job system, the full allocator suite, the reflection/type system,
serialization, the resource/handle system, a file-system abstraction, and
strings. Stand up the lightweight dev tools that accelerate every later phase.

## Scope
- In scope: job/task system; pool/stack/general-purpose allocators (joining the
  Phase 0 arena); reflection (ADR-0007); reflection-driven binary serialization;
  handle-based resource system; file-system abstraction; string system. Build
  early: a lightweight profiler, debug-draw, and a CVar/console.
- Out of scope: the offline cooker (Phase 11); GPU resource streaming (Phase 3).

## Tech decisions
- **Reflection mechanism** — ADR-0007 (decide here; serialization depends on it).
- **Job system shape** — thread pool with a task graph; work-stealing likely;
  fibers only if a concrete need appears. Record specifics when expanded.
- Build dev tools early per the project principle (compound payoff).

## Design
(To expand.) Handle = index + generation. Serialization walks reflected fields.
Containers take an allocator (ADR-0004).

## Milestones
1. Allocator suite + tests.
2. Job system + stress tests.
3. Reflection + serialization round-trip.
4. Resource/handle system, file system, strings.
5. Profiler + debug-draw + CVar/console.

## Test plan
Per `plan.md`: allocator integrity, job-system completion/race stress,
serialization round-trip, reflection field enumeration, container correctness.

## Open questions
- Reflection mechanism (ADR-0007); job-system internals.

## Depends on
- Phase 0 (core, math), Phase 1 (a consumer to validate against).
