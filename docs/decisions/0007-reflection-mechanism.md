# ADR-0007 — Reflection / type-system mechanism

> Status: Proposed (decide during Phase 2)
> Date: (set on commit)

## Context
The reflection/type system is the keystone: one source of truth that feeds
serialization, editor property panels, network replication, and scripting
bindings. Its mechanism must be chosen before serialization depends on it.

## Options considered
- **Annotation macros + codegen step** — mark types/fields with macros; a build
  step generates the reflection tables. Explicit, debuggable, no heavy template
  machinery; costs a codegen tool in the build.
- **Pure template / `constexpr`** — no codegen, but heavy compile times and
  awkward field enumeration in C++.
- **External parser (libclang)** — most powerful, most complex to integrate and
  maintain.

## Decision
TBD. Leaning toward annotation macros + a small codegen step. Record the final
mechanism, the macro surface, and the generated-table format here.

## Consequences
To be filled when decided. Serialization (Phase 2), the editor (Phase 13), and
replication (Phase 10) all build on this, so the field-metadata model must serve
all three.
