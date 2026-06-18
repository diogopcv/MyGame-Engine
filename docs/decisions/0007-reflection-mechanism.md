# ADR-0007 — Reflection / type-system mechanism

> Status: Accepted
> Date: 2026-06-17

## Context
The reflection/type system is the keystone: one source of truth that feeds
serialization, editor property panels, network replication, and scripting
bindings. Its mechanism must be chosen before serialization depends on it.

## Options considered
- **Annotation macros + inline registration** — mark types/fields with macros
  that expand to a static `ge_type_info()` method returning a `const TypeInfo*`
  with field metadata computed via `offsetof`. No codegen tool, no build step.
  Simple, debuggable, explicit. Compile-time cost is low (no heavy templates).
  Tradeoff: each reflected type must be manually annotated.
- **Annotation macros + codegen step** — mark types/fields with macros; a
  separate tool (CMake pre-build step) parses them and generates registration
  code. More powerful (can auto-discover types), but adds build complexity and
  a tool to maintain.
- **Pure template / `constexpr`** — no codegen, but heavy compile times and
  awkward field enumeration in C++. Requires C++26 reflection for ergonomic
  field enumeration.
- **External parser (libclang)** — most powerful, most complex to integrate and
  maintain. Overkill for our scope.

## Decision
**Annotation macros with inline registration.** `GE_REFLECT_BEGIN(Type)`,
`GE_FIELD(Type, field, FieldType)`, and `GE_REFLECT_END(Type)` expand to a
static `ge_type_info()` method that returns a compile-time-initialized
`TypeInfo` struct. A global type registry (hash map of `StringId -> TypeInfo*`)
enables runtime lookup by name. No separate codegen tool or build step.

If annotating every type becomes burdensome after Phase 4 (hundreds of
components), a codegen step can be added on top of the same macro surface
without breaking existing code.

## Consequences
- Serialization, the editor, and replication all build on `TypeInfo` and
  `FieldInfo`. The field-metadata model (name, type, offset, size) serves all
  three use cases.
- Each reflected type must be manually annotated — a tradeoff for simplicity.
- The macro surface is the stable contract; the implementation behind it can
  evolve (to codegen, for instance) without changing user code.
- Compile-time overhead is minimal (no heavy template instantiation).
