# Architecture Overview

This document provides a comprehensive introduction to the MyEngine architecture: how the codebase is organized, why it is organized that way, and the foundational design principles that every other system builds upon.

## Table of Contents

- [What is MyEngine?](#what-is-myengine)
- [The Layered Architecture](#the-layered-architecture)
- [Why Strict Layering?](#why-strict-layering)
- [Module Layout Convention](#module-layout-convention)
- [The Build System](#the-build-system)
- [Language and Toolchain Choices](#language-and-toolchain-choices)
- [Naming Conventions](#naming-conventions)
- [Error Handling Philosophy](#error-handling-philosophy)
- [The pImpl Pattern](#the-pimpl-pattern)
- [Data-Oriented Design](#data-oriented-design)

---

## What is MyEngine?

MyEngine is a game engine written from scratch in C++20, targeting Windows with DirectX 12 graphics. The two defining constraints are:

1. **The renderer and physics solver are hand-written.** Third-party libraries are only allowed for "solved, non-core problems" like image decoding (stb_image), asset parsing (cgltf), and test frameworks (doctest).

2. **Everything is built in layers with strict dependency rules.** A module can only depend on layers strictly below it. This single rule prevents the architectural decay that plagues most large C++ codebases.

The engine is developed in phases, each building on the last. Phases 0-3 are complete:

| Phase | Name | What it delivered |
|-------|------|-------------------|
| 0 | Foundation | Window, timer, memory, types, logging, arena allocator, math library |
| 1 | First Triangle | DX12 device/swapchain, shaders, vertex buffers, spinning textured cube |
| 2 | Harden the Core | Allocator suite, containers, strings, reflection, serialization, job system, handles, profiler, CVars |
| 3 | Renderer Proper | RHI abstraction, render graph, mesh/material/texture, model loading, camera, Blinn-Phong lighting, shadow mapping |

---

## The Layered Architecture

The engine is split into six layers. Each layer may depend only on layers strictly below it. No sideways dependencies (between modules on the same layer) are allowed.

```
Layer 5: Application         sandbox (test bed), editor, tools
Layer 4: Frameworks          ECS, world/scene, gameplay, UI
Layer 3: Subsystems          graphics, physics, audio, animation, input, AI
Layer 2: Resources           asset system, pipeline/cooker
Layer 1: Core Foundation     types, math, log, assert, allocators, containers,
                             reflection, serialization, job system, handles,
                             profiler, CVars
Layer 0: Platform            window, timer, virtual memory, file I/O
```

### Currently Implemented Modules

| Module Path | Layer | Purpose |
|-------------|-------|---------|
| `engine/platform/` | 0 | OS abstraction: window, timer, memory, file I/O |
| `engine/core/` | 1 | Foundation: types, math, allocators, containers, reflection, serialization, jobs, handles, profiler, CVars |
| `engine/gfx/` | 3 | Graphics subsystem: DX12 RHI, render graph, mesh/material, camera, lighting, shadows |
| `sandbox/` | 5 | Application test bed |

### Dependency Graph

```
sandbox (Layer 5)
  |
  +-- ge_gfx (Layer 3)
  |     |
  |     +-- ge_core (Layer 1)
  |           |
  |           +-- ge_platform (Layer 0)
  |                 |
  |                 +-- Windows OS (VirtualAlloc, CreateWindow, QueryPerformanceCounter, ...)
  |
  +-- ge_core (Layer 1)
  +-- ge_platform (Layer 0)
```

---

## Why Strict Layering?

In most large C++ codebases, modules gradually acquire dependencies on each other. The audio system starts calling the renderer for debug visualization. The physics system starts pulling in the input system for raycasting tests. Before long, every module depends on every other module, and you cannot change one system without breaking three others. This is sometimes called the "big ball of mud" anti-pattern.

Strict layering prevents this:

- **Compile-time enforcement.** If `ge_platform` tries to `#include "core/log.h"`, the build fails because `ge_platform`'s CMake target does not link `ge_core`. There is no way to accidentally introduce an upward or sideways dependency.

- **Independent reasoning.** When you read the platform code, you know it depends only on the OS. When you read the core code, you know it depends only on the platform. This makes each module understandable in isolation.

- **Parallel development.** Independent modules can be worked on simultaneously without merge conflicts in shared headers.

- **Testability.** Lower layers can be tested without instantiating higher layers. Platform tests need no renderer; core tests need no graphics device.

The tradeoff is that when modules on the same layer need to communicate (e.g., physics and audio both reacting to a collision), they must do so through data structures owned by a higher layer (the world/ECS). This is by design: it forces communication to be explicit and data-driven.

---

## Module Layout Convention

Every engine module follows the same directory structure:

```
engine/<module>/
  include/<module>/     Public headers
  src/                  Implementation files
  tests/                Unit tests (own executable, registered with CTest)
  CMakeLists.txt        Build definition for this module
```

### Isolation Principle

Public headers (in `include/<module>/`) must not pull in OS-specific headers. `<windows.h>` and its ilk appear only inside `_win32.cpp` implementation files. When an OS handle (like an HWND) needs to cross the header boundary, it is exposed as `void*`. The consuming code casts it back to the real type inside its own implementation file.

This has two benefits:

1. **Compilation speed.** `<windows.h>` is enormous and defines thousands of macros. Keeping it out of headers prevents it from polluting the entire build.

2. **Portability readiness.** Even though MyEngine only targets Windows today, the public APIs are platform-clean. Adding a Linux backend later would only require new `_linux.cpp` implementation files without changing any headers.

### Include Path Convention

Includes are module-qualified:

```cpp
#include "core/arena.h"       // not "arena.h"
#include "platform/window.h"  // not "window.h"
#include "gfx/renderer.h"     // not "renderer.h"
```

This makes it immediately clear which module a header belongs to and prevents name collisions.

---

## The Build System

MyEngine uses **CMake 3.20+** as its meta-build system, generating a **Visual Studio 2022** solution. The build commands are:

```powershell
cmake -B build -G "Visual Studio 17 2022"      # Configure
cmake --build build --config Debug              # Build (Debug)
cmake --build build --config Release            # Build (Release)
ctest --test-dir build -C Debug --output-on-failure  # Run all tests
```

### How Modules are Connected

The top-level `CMakeLists.txt` adds each module as a subdirectory in layer order. Each module's own `CMakeLists.txt`:

1. Defines a static library target: `add_library(ge_<module> STATIC ...)`
2. Exposes its public headers: `target_include_directories(ge_<module> PUBLIC include)`
3. Links only to lower-layer targets: `target_link_libraries(ge_<module> PUBLIC ge_<lower>)`

This is how layering is enforced at build time. If module A does not link module B, any `#include` of B's headers from A's code will fail to compile.

### Shader Compilation

HLSL shaders live in `engine/gfx/shaders/` and are compiled offline by DXC (the DirectX Shader Compiler) via a CMake custom command. The compiled `.cso` (Compiled Shader Object) files are placed next to the executable under `shaders/` and are gitignored (they are build artifacts, not source).

### Test Executables

Each module builds its own test executable (`ge_<module>_tests`), registered with CTest via `enable_testing()`. Tests use the doctest framework (a single header-only library). Running `ctest` from the build directory executes all test suites across all modules.

---

## Language and Toolchain Choices

### C++20

The engine uses C++20 features selectively:

- **`std::format`** — used by the logging system for compile-time-checked format strings. `GE_INFO("Loaded {} meshes", count)` is checked by the compiler; a missing argument is a compile error, not a runtime crash.
- **Designated initializers** — `WindowDesc desc{.title = "MyEngine", .width = 1280}` makes struct initialization self-documenting.
- **`constexpr`** — the string-hashing function (`string_id()`) can run at compile time, so `GE_SID("player")` computes its hash during compilation rather than at runtime.
- **`std::atomic`** — used in the job system for lock-free counter operations.

### What is NOT Used

- **Exceptions** — not used in core or hot paths. Errors are returned as values (`bool`, status enums, `std::optional`). Exceptions have unpredictable performance costs and make reasoning about control flow difficult.
- **RTTI (Run-Time Type Information)** — disabled. The engine's own reflection system replaces it with explicit, data-driven type metadata.
- **The C runtime heap** — engine code does not call `new`/`malloc` directly (except in bootstrapping the job system). All allocations go through the engine's allocator system, which is backed by `VirtualAlloc`.

### Compiler Flags

MSVC is configured with strict warnings:

- **`/W4`** — warning level 4 (nearly everything).
- **`/permissive-`** — strict standards conformance (no Microsoft extensions).
- **Warnings as errors** — any warning fails the build. This catches issues early and keeps the codebase clean.

In Debug builds, the D3D12 debug layer, GPU-based validation, and DRED (Device Removed Extended Data) are enabled. These act as always-on correctness nets for GPU operations.

---

## Naming Conventions

| Element | Convention | Examples |
|---------|-----------|----------|
| Functions and variables | `snake_case` | `begin_frame()`, `block_size_` |
| Types and structs | `PascalCase` | `Arena`, `BufferHandle`, `RendererDesc` |
| Constants | `k` prefix | `k_pi`, `kMaxPointLights`, `kFrameCount` |
| Private members | Trailing underscore | `base_`, `offset_`, `impl_` |
| Files | `snake_case` | `arena.h`, `renderer_dx12.cpp` |
| Namespaces | `snake_case` | `ge::platform`, `ge::gfx` |
| Macros | `SCREAMING_SNAKE_CASE` | `GE_ASSERT`, `GE_MEGABYTES`, `GE_PROFILE_SCOPE` |

Everything lives under the top-level `ge` namespace, with modules nested: `ge::platform`, `ge::core` (types live directly in `ge`), `ge::gfx`.

---

## Error Handling Philosophy

The engine uses a tiered approach to error handling:

1. **Assertions (`GE_ASSERT`)** — for invariants that must always hold. Failing an assert is a programmer error (a bug), not a runtime condition. Asserts log the failure and break into the debugger. They will be compiled out of shipping builds.

2. **Return values** — for operations that can legitimately fail (file not found, device creation failed). Functions return `bool` for success/failure, or `nullptr`/invalid handle for resource lookups.

3. **Logging** — `GE_TRACE/INFO/WARN/ERROR` provide visibility into engine state. Warnings indicate potential issues; errors indicate problems that may affect correctness but do not crash.

The philosophy is: **trust internal code, validate at boundaries.** Inside the engine, an assert guards impossible states. At system boundaries (user input, file I/O, GPU device creation), return values handle expected failures gracefully.

---

## The pImpl Pattern

Several classes use the "pointer to implementation" (pImpl) pattern:

```cpp
// In the header (public):
class Window {
public:
    bool create(const WindowDesc& desc);
    void destroy();
    void* native_handle() const;
private:
    void* impl_ = nullptr;  // opaque pointer
};

// In the .cpp (private):
struct WindowImpl {
    HWND hwnd;
    // ... all the OS-specific state
};
```

The `impl_` pointer is a `void*` that hides the full implementation struct. The `.cpp` file casts it to the real type. This pattern is used for:

- **`platform::Window`** — hides the HWND and Win32 message-pump details.
- **`gfx::Renderer`** — hides all D3D12 COM objects (device, command queue, swapchain, etc.).
- **`gfx::RhiDevice`** — hides the DX12 backend implementation.
- **`JobSystem`** — hides the thread pool, mutex, and condition variable.

Benefits:

- **Compilation firewall.** Changing the implementation does not force recompilation of every file that includes the header.
- **No OS headers in public APIs.** `<windows.h>` stays inside `.cpp` files.
- **ABI stability.** The class size does not change when the implementation changes.

The cost is one extra heap allocation (for the impl struct) and one pointer indirection per method call. Both are negligible for these infrequently-constructed, long-lived objects.

---

## Data-Oriented Design

MyEngine follows data-oriented design principles:

1. **Plain structs over class hierarchies.** Types like `vec3`, `mat4`, `Camera`, `Material`, `SubMesh` are plain-old-data (POD) structs with public fields. No inheritance, no virtual functions, no hidden state.

2. **Free functions over methods.** Operations on math types are free functions (`normalize(v)`, `cross(a, b)`) rather than member functions. This keeps the types simple and composable.

3. **Contiguous arrays over linked structures.** The engine's `Array<T>` is a contiguous dynamic array. The `HandleTable<T>` is a flat array of slots. The profiler's ring buffer is a fixed-size array. These layouts are cache-friendly because the CPU can prefetch sequential memory.

4. **Explicit data flow.** Systems receive their inputs as parameters rather than reaching into global state. The renderer takes a `Camera` by value. The allocator takes a `Pool*` as context. This makes dependencies visible and testable.

5. **No virtual calls in hot paths.** The allocator interface uses function pointers (set once at creation time), not virtual dispatch. The RHI has no virtual base class — DX12 is the only backend, and adding an abstraction layer before a second backend exists would be premature.

These principles directly serve performance: modern CPUs are bottlenecked by memory access patterns, not arithmetic. A simple `for` loop over a contiguous array is often 10-100x faster than chasing pointers through a linked list, because the CPU cache can prefetch the data.
