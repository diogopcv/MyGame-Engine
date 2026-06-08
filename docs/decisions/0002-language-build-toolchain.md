# ADR-0002 — Language, build & toolchain

> Status: Accepted
> Date: (set on commit)

## Context
A from-scratch engine targeting Windows with DirectX 12, developed in Visual
Studio, that must scale to many modules without hand-maintained project files.

## Decision
- **Language:** C++20. We use `<format>`, concepts, designated initializers,
  `constexpr`. No exceptions in core or hot paths; minimal RTTI — a custom
  reflection system (ADR-0007) replaces `typeid` / `dynamic_cast`.
- **Build:** CMake (3.20+) generating a Visual Studio solution, or opened
  directly as a CMake folder in VS. One `STATIC` library per module; the app is
  the only executable (plus per-module test executables).
- **Warnings/flags (MSVC):** `/W4 /permissive- /MP`. Warnings are treated as
  bugs to fix, not noise to ignore.
- **Compile time:** jumbo / unity builds are deferred until build time actually
  hurts; not premature.

## Consequences
- Modern standard-library features are available everywhere.
- CMake adds a layer of indirection but keeps the module graph explicit and the
  project portable to other generators/CI.
- Third-party code that relies on exceptions must be confined to higher layers
  or wrapped, never pulled into core.
