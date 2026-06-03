# MyEngine — Phase 0: Foundation Skeleton

A buildable starting point for the engine. This phase establishes the module
structure and the lowest two architectural layers, ending in a window that
opens, "clears" to a color, runs a real game loop with precise timing, and
shuts down cleanly.

## What's here

```
MyEngine/
├─ CMakeLists.txt              top-level build
├─ engine/
│  ├─ platform/               Layer 0 — OS abstraction (depends on nothing)
│  │  ├─ include/platform/    window.h, timer.h, memory.h  (no <windows.h>)
│  │  └─ src/                 *_win32.cpp                  (all Win32 lives here)
│  └─ core/                   Layer 1 — foundation (depends on platform)
│     ├─ include/core/        types.h, log.h, assert.h, arena.h
│     └─ src/                 log.cpp, arena.cpp
└─ sandbox/                   the application / test bed (depends on core)
   └─ src/main.cpp            window + fixed-timestep loop
```

## The one rule that makes this modular

Dependencies point **downward only**: `sandbox → core → platform`. The build
enforces it — `platform` does not link `core`, so the moment a platform file
tries to `#include "core/..."` it fails to compile. Keep it that way as the
engine grows; it is the single discipline that keeps the codebase from turning
into mud.

Notice the platform headers use raw `std::size_t` / `std::uint32_t` rather than
the engine's `usize` / `u32` aliases — that is deliberate, so the bottom layer
depends on nothing, not even core's type vocabulary.

## Build & run (Windows + Visual Studio)

Option A — generate a solution:

```
cmake -B build -G "Visual Studio 17 2022"
```

Then open `build/MyEngine.sln`, set `sandbox` as the startup project, and run.

Option B — open the folder directly in Visual Studio (File → Open → Folder).
VS detects the CMake project automatically; pick the `sandbox.exe` target.

You should get a console with log output plus a 1280×720 dark-gray window, and
an FPS line printed once per second. Close the window for a clean shutdown.

## Notes & deliberate omissions

- The loop is uncapped (no vsync yet), so FPS will be very high and the CPU
  will spin. Real frame pacing arrives with the DX12 swapchain in Phase 1.
- The logger is single-threaded for now; it gets hardened once the job system
  exists.
- The arena commits its whole block up front. A later refinement reserves a
  large address range and commits pages on demand.

## What's next

1. **Math library** — finishes Phase 0: `vec2/3/4`, `mat4`, `quat`, plus the
   transform helpers needed to build an MVP matrix.
2. **Phase 1 — First triangle** — the DX12 RHI (device, swapchain, command
   queue/list, fences, descriptor heaps, root signature, PSO) culminating in a
   spinning, math-driven cube.
