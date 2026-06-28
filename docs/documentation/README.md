# MyEngine Documentation

Comprehensive documentation and study material for the MyEngine game engine, covering the implementation completed in Phases 0-3. Each document is self-contained and explains not only what was implemented, but the underlying concepts, algorithms, and design decisions.

## Reading Order

The documents are numbered to suggest a reading order, starting from the high-level architecture and working down to specific systems:

| # | Document | Topics |
|---|----------|--------|
| 00 | [Architecture Overview](00-architecture-overview.md) | Layered architecture, module layout, build system, naming conventions, pImpl pattern, data-oriented design |
| 01 | [Platform Layer](01-platform-layer.md) | Virtual memory (VirtualAlloc), high-resolution timer (QueryPerformanceCounter), window and message pump (Win32), raw mouse input, file I/O |
| 02 | [Core Foundation](02-core-foundation.md) | Fixed-width type aliases, logging system (std::format), assertion macros, debug break |
| 03 | [Memory Management](03-memory-management.md) | Arena (bump) allocator, pool allocator (embedded free list), stack allocator (LIFO), Allocator interface (function pointers), alignment |
| 04 | [Math Library](04-math-library.md) | Vectors, matrices (row-major, row-vector), quaternions, SLERP, perspective/orthographic projection, look-at view matrix, TRS transforms |
| 05 | [Core Systems](05-core-systems.md) | Array\<T\>, HashMap\<K,V\> (open addressing), StringId (FNV-1a), reflection system, binary serialization, job system (fork/join), generational handles, profiler, CVars |
| 06 | [Graphics and Rendering](06-graphics-and-rendering.md) | DirectX 12 fundamentals, RHI abstraction (handle-based), render graph (automatic barriers), mesh/material/texture, glTF model loading, camera, Blinn-Phong lighting, shadow mapping (PCF) |
| 07 | [Application and Game Loop](07-application-and-game-loop.md) | Sandbox application, fixed-timestep loop, frame lifecycle, input handling, startup/shutdown sequences |

## Prerequisites

These documents assume basic familiarity with:
- C++ syntax and concepts (pointers, templates, operator overloading)
- General programming concepts (arrays, linked lists, hash tables)

No prior knowledge of graphics programming, game engines, or Windows APIs is assumed. Concepts like DirectX 12, virtual memory, and quaternion math are explained from first principles.

## Scope

This documentation covers Phases 0-3 of the engine:

- **Phase 0 (Foundation):** Platform layer, core types, logging, arena allocator, math library
- **Phase 1 (First Triangle):** DX12 device/swapchain initialization, shader compilation, vertex buffers, spinning textured cube
- **Phase 2 (Harden the Core):** Allocator suite, containers, string system, reflection, serialization, job system, handles, profiler, CVars
- **Phase 3 (Renderer Proper):** RHI abstraction, render graph, mesh/material/texture, glTF import, camera, forward Blinn-Phong lighting, shadow mapping
