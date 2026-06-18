# Phase 2 — Harden the core

> Status: Complete — all 7 milestones done. Allocator suite (pool, stack,
> Allocator interface), containers (Array, HashMap), string system (StringId),
> platform file I/O, reflection + serialization (ADR-0007 accepted), job system,
> resource/handle system, profiler, CVar system. 109 core + 4 platform tests
> green. Profiler wired into sandbox with per-frame timing output.

## Goal
With real consumers now exercising the foundation, build the production-grade
core: a job system, the full allocator suite, the reflection/type system,
serialization, the resource/handle system, a file-system abstraction, and
strings. Stand up the lightweight dev tools that accelerate every later phase.

## Scope
- In scope: job/task system; pool/stack allocators (joining the Phase 0 arena);
  Allocator function-pointer interface; Array<T> and HashMap<K,V> containers;
  reflection (ADR-0007 — annotation macros, inline); reflection-driven binary
  serialization; handle-based resource system; file-system abstraction; string
  system (StringId / FNV-1a); profiler; CVar system.
- Out of scope: the offline cooker (Phase 11); GPU resource streaming (Phase 3);
  general-purpose allocator (TLSF — deferred until profiling justifies it);
  debug-draw rendering (deferred to Phase 3; API-only placeholder possible).

## Tech decisions
- **Allocator interface** — function-pointer struct (`AllocFn` + `FreeFn` + `ctx`),
  not virtual. All allocators (Arena, Pool, Stack) expose `Allocator allocator()`.
- **Reflection mechanism** — ADR-0007 accepted: annotation macros with inline
  registration. `GE_REFLECT_BEGIN`/`GE_FIELD`/`GE_REFLECT_END` expand to a
  static `ge_type_info()` method. Global registry via `type_register`/`type_find`.
- **Job system** — `std::thread`-based worker pool with central mutex queue.
  `JobCounter` (atomic) for completion tracking. No work-stealing or fibers yet.
- **Containers** — `Array<T>` and `HashMap<K,V>` take `Allocator` at construction.
  No `std::vector`/`std::unordered_map` in engine code.
- **Platform file I/O** — Win32 `CreateFile`/`ReadFile`/`WriteFile`. Returns
  buffers from `platform::mem_alloc`; caller frees with `platform::mem_free`.
  Keeps layering clean (platform depends on nothing but the OS).
- **StringId** — u64 FNV-1a hash. `constexpr string_id()` for compile-time
  computation. Debug reverse-lookup table guarded by `#ifndef NDEBUG`.

## Design

### Module layout (new files in existing modules)
```
engine/core/include/core/
├─ allocator.h       Allocator struct + align_up()
├─ pool.h            Pool allocator (fixed-size blocks, embedded free list)
├─ stack_alloc.h     Stack allocator (LIFO with header-tracked rollback)
├─ array.h           Array<T> dynamic array (template, header-only)
├─ hash_map.h        HashMap<K,V> open-addressing (template, header-only)
├─ string_id.h       StringId + constexpr hashing + GE_SID macro
├─ reflect.h         TypeInfo, FieldInfo, GE_REFLECT macros
├─ serialize.h       BinaryWriter, BinaryReader, serialize/deserialize
├─ job_system.h      JobSystem, JobDecl, JobCounter
├─ handle.h          Handle<Tag>, HandleTable<T> (template, header-only)
├─ profiler.h        Profiler, GE_PROFILE_SCOPE macro
└─ cvar.h            CVar, CVarSystem

engine/core/src/
├─ pool.cpp          stack_alloc.cpp  string_id.cpp
├─ reflect.cpp       serialize.cpp    job_system.cpp
├─ profiler.cpp      cvar.cpp

engine/platform/include/platform/
└─ file.h            FileData, file_read_all, file_write_all, file_exists

engine/platform/src/
└─ file_win32.cpp

engine/platform/tests/
├─ test_runner.cpp   test_file.cpp
```

## Milestones
1. **Done.** Allocator suite + Allocator interface.
2. **Done.** Containers (Array + HashMap).
3. **Done.** String system + Platform file I/O.
4. **Done.** Reflection + Serialization (ADR-0007 accepted).
5. **Done.** Job system.
6. **Done.** Resource/Handle system.
7. **Done.** Dev tools (Profiler + CVar) + sandbox integration.

## Test plan
- **Allocators:** pool free-list integrity, stack LIFO, alignment, polymorphic interface.
- **Containers:** push/pop/resize, iteration, hash collisions, remove, capacity growth.
- **StringId:** hash consistency, constexpr, debug reverse lookup.
- **File I/O:** write/read round-trip, exists, missing file handling.
- **Reflection:** field enumeration (name, offset, type), nested structs, type registry.
- **Serialization:** binary round-trip for primitives, nested structs, truncated data detection.
- **Job system:** single job, 100 jobs, 1000-job stress test, multiple batches.
- **Handles:** create/lookup, stale detection, generation mismatch, free-list recycling.
- **Profiler:** init/shutdown, frame timing, push/pop scope, nested scopes, macro.
- **CVar:** register/find int/float/bool, execute command, unknown name handling.

## Depends on
- Phase 0 (core, math), Phase 1 (a consumer to validate against).
