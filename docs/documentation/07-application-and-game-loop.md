# Application and Game Loop

This document covers the sandbox application: how all engine systems come together into a running program, the fixed-timestep game loop, and the frame lifecycle from startup to shutdown.

## Table of Contents

- [The Sandbox Application](#the-sandbox-application)
- [Startup Sequence](#startup-sequence)
- [The Fixed-Timestep Game Loop](#the-fixed-timestep-game-loop)
  - [Why Fixed Timestep?](#why-fixed-timestep)
  - [The Accumulator Pattern](#the-accumulator-pattern)
  - [Frame Time Clamping](#frame-time-clamping)
- [Frame Lifecycle](#frame-lifecycle)
- [Input Handling](#input-handling)
- [FPS Counter and Profiling](#fps-counter-and-profiling)
- [Shutdown Sequence](#shutdown-sequence)
- [The Complete Frame Pipeline](#the-complete-frame-pipeline)

---

## The Sandbox Application

The sandbox (`sandbox/src/main.cpp`) is the engine's test bed application. It sits at Layer 5 (Application) and depends on all lower layers. Its purpose is to exercise the engine systems in a real runtime context: opening a window, initializing the renderer, loading models, handling input, and rendering frames.

The sandbox is not the engine — it is a consumer of the engine. When the editor is built (Phase 13), it will be another application at the same layer, using the same engine modules.

---

## Startup Sequence

The sandbox initializes systems in dependency order (lowest layer first):

```cpp
int main() {
    // 1. Platform timer — needed for frame timing
    platform::time_init();

    // 2. Core systems
    profiler().init();
    cvar_system().init();

    // 3. Register CVars (configuration)
    cvar_system().register_bool("r_vsync", true, "enable vsync");
    cvar_system().register_int("r_width", 1280, "render width");
    cvar_system().register_int("r_height", 720, "render height");

    // 4. Memory — reserve the permanent arena
    Arena perm;
    perm.init(GE_MEGABYTES(64));

    // 5. Platform window
    platform::Window window;
    window.create({.title = "MyEngine - Phase 3", .width = 1280, .height = 720});

    // 6. Graphics — requires the window's HWND
    gfx::Renderer renderer;
    renderer.init({
        .hwnd = window.native_handle(),
        .width = window.width(),
        .height = window.height(),
        .enable_debug_layer = true  // (Debug builds)
    });

    // 7. Load content
    renderer.load_model("assets/models/box.glb", perm.allocator());

    // 8. Set up scene (camera, lights)
    gfx::Camera camera;
    camera.position = {0, 1.5f, -3.0f};
    renderer.set_camera(camera);
    renderer.set_sun({...});
    renderer.set_point_lights(point_lights, 2);

    // 9. Enter the game loop
    // ...
}
```

Each initialization step can fail, and failures are handled by logging an error and returning early. For example, if the window cannot be created or the renderer fails to initialize, the program exits cleanly.

---

## The Fixed-Timestep Game Loop

### Why Fixed Timestep?

A naive game loop passes the variable frame time (`dt`) directly to the update function:

```cpp
// Bad: variable timestep
while (running) {
    dt = measure_frame_time();
    update(dt);  // Physics, AI, gameplay
    render();
}
```

This is problematic because:

1. **Non-determinism.** The simulation produces different results at different frame rates. A physics simulation stepping at 60 FPS behaves differently than one stepping at 30 FPS, even with the same inputs. This makes bugs unreproducible and networked simulations impossible.

2. **Instability.** Variable timesteps can cause physics simulations to become unstable. Large timesteps (during frame-rate drops) can cause objects to tunnel through walls or forces to explode.

3. **Frame-rate dependency.** Movement speed, jump height, and other gameplay behavior changes with frame rate.

### The Accumulator Pattern

MyEngine uses a fixed-timestep game loop with an accumulator:

```cpp
const f64 fixed_dt    = 1.0 / 60.0;  // 60 Hz update rate
f64       accumulator = 0.0;
f64       last        = platform::now_seconds();

while (!window.should_close()) {
    const f64 now        = platform::now_seconds();
    f64       frame_time = now - last;
    last = now;

    if (frame_time > 0.25) frame_time = 0.25;  // Clamp
    accumulator += frame_time;

    // Fixed-rate updates
    while (accumulator >= fixed_dt) {
        update(fixed_dt);     // Always called with the same dt
        accumulator -= fixed_dt;
    }

    // Rendering happens at the actual frame rate
    render();
}
```

How it works:

1. Measure the real elapsed time since the last frame.
2. Add it to the accumulator.
3. While the accumulator has at least one fixed timestep's worth of time, run an update step with exactly `fixed_dt`.
4. The accumulator carries over the remainder.
5. Render once per frame (at the monitor's refresh rate if vsync is on).

This means:
- The update function always receives the same `dt` (1/60 second), making the simulation deterministic.
- If the frame rate drops, multiple update steps run per frame to "catch up."
- If the frame rate is higher than 60, some frames render without an update step (the scene is unchanged).

### Frame Time Clamping

```cpp
if (frame_time > 0.25) frame_time = 0.25;
```

This clamp prevents the "spiral of death": if a frame takes a very long time (e.g., during a debugger breakpoint or system hiccup), the accumulator would pile up a huge amount of time, requiring hundreds of update steps to catch up. This would cause the next frame to take even longer, creating a vicious cycle.

By clamping to 250ms, the simulation "drops" time rather than trying to simulate it. The game slows down briefly rather than freezing while it catches up.

---

## Frame Lifecycle

Each frame in the sandbox follows this sequence:

```
1. profiler().begin_frame()
       |
2. Measure frame time
       |
3. window.pump_events()       — drain OS message queue
       |
4. renderer.resize(...)       — handle window resize
       |
5. Handle input:
   - Mouse capture toggle (right click)
   - Escape to quit
       |
6. Fixed-timestep update loop:
   - Read WASD/QE/Shift keys for movement
   - Read mouse delta for look
   - camera_fly_update(camera, fixed_dt, ...)
       |
7. renderer.set_camera(camera)
       |
8. GE_PROFILE_SCOPE("render")
   renderer.begin_frame(dt)
   renderer.end_frame()
       |
9. profiler().end_frame()
       |
10. FPS counter update
```

Steps 6 (update) and 8 (render) are the two main workloads. The update runs at a fixed rate (potentially multiple times per frame or zero times). The render runs exactly once per frame.

---

## Input Handling

The sandbox implements a simple FPS-style control scheme:

| Input | Action |
|-------|--------|
| W / S | Move forward / backward |
| A / D | Strafe left / right |
| Q / E | Move down / up |
| Left Shift | Triple movement speed |
| Right mouse button (hold) | Capture mouse for camera look |
| Mouse movement (while captured) | Yaw / pitch camera |
| Escape | Quit the application |

Mouse capture is toggled by holding the right mouse button. When captured, the cursor is hidden and raw mouse input provides unbounded, unaccelerated movement deltas. When released, the cursor reappears.

The camera update happens inside the fixed-timestep loop:

```cpp
while (accumulator >= fixed_dt) {
    f32 move_fwd = 0, move_right = 0, move_up = 0;
    if (window.key_down(KeyCode::W)) move_fwd   += 1.0f;
    if (window.key_down(KeyCode::S)) move_fwd   -= 1.0f;
    if (window.key_down(KeyCode::D)) move_right += 1.0f;
    if (window.key_down(KeyCode::A)) move_right -= 1.0f;
    if (window.key_down(KeyCode::E)) move_up    += 1.0f;
    if (window.key_down(KeyCode::Q)) move_up    -= 1.0f;

    f32 speed = 3.0f;
    if (window.key_down(KeyCode::LShift)) speed *= 3.0f;

    camera_fly_update(camera, fixed_dt, move_fwd, move_right, move_up,
                      mouse_dx, mouse_dy, speed);
    accumulator -= fixed_dt;
}
```

This ensures camera movement is frame-rate independent: the camera moves at the same speed regardless of whether the game runs at 30, 60, or 144 FPS.

---

## FPS Counter and Profiling

The sandbox tracks frame rate using a simple counter:

```cpp
f64 fps_timer = 0.0;
u32 frames    = 0;

// In the loop:
++frames;
fps_timer += frame_time;
if (fps_timer >= 1.0) {
    f64 avg_ms = profiler().last_frame_time() * 1000.0;
    GE_INFO("FPS: {} | frame: {:.2f} ms", frames, avg_ms);
    frames = 0;
    fps_timer -= 1.0;
}
```

Every second, it logs the frame count and the profiler's last frame time (in milliseconds). The profiler provides accurate timing because it uses the high-resolution timer and tracks the duration of each `begin_frame()`-to-`end_frame()` cycle.

The `GE_PROFILE_SCOPE("render")` macro wraps the render call, so the profiler records how long rendering takes each frame. Additional scopes can be added to any function to build up a hierarchical timing breakdown.

---

## Shutdown Sequence

Shutdown happens in reverse order of initialization:

```cpp
GE_INFO("Shutting down ...");
renderer.shutdown();      // Flush GPU, release all DX12 resources
window.destroy();         // Destroy the HWND
perm.shutdown();          // Release the permanent arena's virtual memory
cvar_system().shutdown(); // Clear CVars
profiler().shutdown();    // Reset profiler state
GE_INFO("Clean exit.");
```

The renderer must shut down first because it needs to flush the GPU (wait for all in-flight work to complete) before releasing resources. If the arena were freed first, the GPU might still be reading from memory that has been returned to the OS.

The window must be destroyed before the program exits to properly handle Win32 cleanup (unregistering the window class, releasing GDI resources).

---

## The Complete Frame Pipeline

Here is the complete journey of a single frame, from start to finish:

```
CPU Timeline:
  |
  |  profiler.begin_frame()
  |  measure frame_time
  |  window.pump_events()          → OS messages processed
  |  check input                   → camera updated
  |  renderer.set_camera(camera)
  |
  |  renderer.begin_frame(dt):
  |    rhi.begin_frame()
  |      Wait for frame slot fence  → ensures GPU finished frame N-2
  |      Reset command allocator
  |      Reset command list
  |
  |    Build render graph:
  |      Import back_buffer (Present)
  |      Import shadow_map (PixelShaderResource)
  |      Add shadow pass (writes shadow_map as DepthWrite)
  |      Add main pass (reads shadow_map, writes back_buffer)
  |      Set final state: back_buffer → Present
  |
  |    graph.compile()             → barrier list generated
  |    graph.execute(rhi):
  |      Barrier: shadow_map PSR → DepthWrite
  |      Shadow pass callback:
  |        set pipeline (shadow)
  |        set viewport/scissor (2048x2048)
  |        clear depth
  |        for each sub-mesh: set buffers, set CBs, draw_indexed
  |      Barrier: shadow_map DepthWrite → PSR
  |      Barrier: back_buffer Present → RenderTarget
  |      Main pass callback:
  |        set pipeline (model)
  |        set viewport/scissor (window size)
  |        clear render target + depth
  |        set per-frame CB (camera, lights)
  |        for each sub-mesh: set material texture, set per-object CB, draw_indexed
  |      Barrier: back_buffer RenderTarget → Present
  |
  |    rhi.end_frame()              → command list closed and submitted
  |    rhi.present(vsync)           → swap chain present + fence signal
  |
  |  profiler.end_frame()
  |  FPS counter update
  |
  v

GPU Timeline (overlapped with next CPU frame):
  |
  |  Execute submitted command list:
  |    Shadow pass: depth-only rendering to shadow map
  |    Main pass: lit rendering with shadow sampling
  |    Present: flip to display
  |
  v
```

The CPU and GPU work in parallel through double buffering: while the GPU renders frame N, the CPU records frame N+1. The fence synchronization ensures they never interfere with each other's resources.
