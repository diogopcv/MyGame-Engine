# Application and Game Loop

This document covers the sandbox application: how all engine systems come together into a running program, the fixed-timestep game loop, and the frame lifecycle from startup to shutdown. As of Phase 5, the sandbox's "player" is a physics-driven capsule character moving through an ECS-owned `Scene` and a `PhysicsWorld` — not a raw fly-through camera — so this document also covers how those two owners hand data to each other every frame.

## Table of Contents

- [The Sandbox Application](#the-sandbox-application)
- [Startup Sequence](#startup-sequence)
- [The Fixed-Timestep Game Loop](#the-fixed-timestep-game-loop)
  - [Why Fixed Timestep?](#why-fixed-timestep)
  - [The Accumulator Pattern](#the-accumulator-pattern)
  - [Frame Time Clamping](#frame-time-clamping)
- [Two Owners, One Frame: Scene and PhysicsWorld](#two-owners-one-frame-scene-and-physicsworld)
- [Frame Lifecycle](#frame-lifecycle)
- [Input Handling and the Physics-Driven Character](#input-handling-and-the-physics-driven-character)
- [The Chase Camera](#the-chase-camera)
- [FPS Counter and Profiling](#fps-counter-and-profiling)
- [Shutdown Sequence](#shutdown-sequence)
- [The Complete Frame Pipeline](#the-complete-frame-pipeline)

---

## The Sandbox Application

The sandbox (`sandbox/src/main.cpp`) is the engine's test bed application. It sits at Layer 5 (Application) and depends on all lower layers — including every Layer 3 subsystem (`ge_gfx`, `ge_input`, `ge_physics`) and Layer 4 (`ge_world`). Its purpose is to exercise the engine systems in a real runtime context: opening a window, initializing the renderer, loading models, running a physics simulation, handling input, and rendering frames.

The sandbox is not the engine — it is a consumer of the engine. When the editor is built (Phase 13), it will be another application at the same layer, using the same engine modules. It is also, today, the *only* module that legitimately depends on more than one Layer-3 subsystem at once: `ge_gfx`, `ge_input`, and `ge_physics` never depend on each other (siblings on the same layer cannot), so anything that needs to react to physics by driving a rendered, input-controlled character has to live one layer up, where all three are visible. See [08-ecs-and-world.md](08-ecs-and-world.md) and [09-physics-engine.md](09-physics-engine.md) for why the glue code specifically lives in the sandbox rather than in `ge_world`.

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
    window.create({.title = "MyEngine - PBR Renderer", .width = 1280, .height = 720});

    // 6. Graphics — requires the window's HWND
    gfx::Renderer renderer;
    renderer.init({
        .hwnd = window.native_handle(),
        .width = window.width(),
        .height = window.height(),
        .enable_debug_layer = true  // (Debug builds)
    });

    // 7. Load content
    renderer.load_model("assets/models/DamagedHelmet.glb", perm.allocator());

    // 8. World + physics — Phase 4/5 additions
    world::Scene scene;
    scene.init(64, perm.allocator());

    physics::PhysicsWorld phys;
    phys.init(16, perm.allocator());

    // Static ground the character falls onto and walks on. No visual
    // representation yet — the renderer only draws the loaded glTF model,
    // so this is a physics-only volume (see 09-physics-engine.md).
    phys.add_body(physics::make_box_body({0, -1, 0}, {20, 1, 20}, quat_identity(), 0.0f));

    // The player: a rotation-locked capsule rigid body.
    u32 player_body_id = phys.add_body(
        physics::make_capsule_body({0, 3, 0}, quat_identity(), 0.4f, 0.5f, 1.0f));

    world::Entity player = scene.create_entity("player");
    scene.world().add_component<world::Transform>(player, world::Transform{});
    scene.world().add_component<world::RigidBodyRef>(player, world::RigidBodyRef{player_body_id});
    scene.world().add_component<world::CharacterController>(player, world::CharacterController{});

    // 9. Input bindings
    input::InputMap input_map;
    input_map.bind_axis(string_id("MoveForward"), platform::KeyCode::W, platform::KeyCode::S);
    input_map.bind_axis(string_id("MoveRight"), platform::KeyCode::D, platform::KeyCode::A);
    input_map.bind_action(string_id("Sprint"), platform::KeyCode::LShift);
    input_map.bind_action(string_id("Quit"), platform::KeyCode::Escape);

    // 10. Set up lighting
    renderer.set_sun({...});
    renderer.set_point_lights(point_lights, 2);

    // 11. Enter the game loop
    // ...
}
```

Each initialization step can fail, and failures are handled by logging an error and returning early. For example, if the window cannot be created or the renderer fails to initialize, the program exits cleanly.

Step 8 is new since Phase 4/5. Two separate owners are created here — `Scene` (the ECS, holding gameplay-facing data like `Transform`) and `PhysicsWorld` (the simulation, holding `RigidBody` state like velocity and mass) — and one `RigidBodyRef` component is the only link between them. See [Two Owners, One Frame](#two-owners-one-frame-scene-and-physicsworld) below for why they stay separate rather than merging into one system.

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

1. **Non-determinism.** The simulation produces different results at different frame rates. A physics simulation stepping at 60 FPS behaves differently than one stepping at 30 FPS, even with the same inputs. This makes bugs unreproducible and networked simulations impossible. This is not hypothetical for MyEngine: `PhysicsWorld::step(dt)` (Phase 5) is a real fixed-step numerical integrator, and every simulation-baseline test that pins down its behavior assumes a fixed `dt`.

2. **Instability.** Variable timesteps can cause physics simulations to become unstable. Large timesteps (during frame-rate drops) can cause objects to tunnel through walls or forces to explode. The sequential-impulse solver's Baumgarte position-correction term is explicitly scaled by `1/dt` (see [09-physics-engine.md](09-physics-engine.md)), so a wildly varying `dt` would make resting contacts jitter.

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

In the sandbox, "update" is no longer a single call — it is a small pipeline (input → physics → ECS sync) run once per fixed step. See [Frame Lifecycle](#frame-lifecycle).

### Frame Time Clamping

```cpp
if (frame_time > 0.25) frame_time = 0.25;
```

This clamp prevents the "spiral of death": if a frame takes a very long time (e.g., during a debugger breakpoint or system hiccup), the accumulator would pile up a huge amount of time, requiring hundreds of update steps to catch up. This would cause the next frame to take even longer, creating a vicious cycle.

By clamping to 250ms, the simulation "drops" time rather than trying to simulate it. The game slows down briefly rather than freezing while it catches up.

---

## Two Owners, One Frame: Scene and PhysicsWorld

Phase 4 introduced `world::Scene` (an ECS: entities plus components like `Transform`). Phase 5 introduced `physics::PhysicsWorld` (a rigid-body simulation: bodies with position, velocity, mass). Both "own" a version of the player's position each frame, and they disagree about who is allowed to write it:

- `PhysicsWorld` is authoritative for anything the solver touches: position, orientation, linear/angular velocity. Nothing outside `PhysicsWorld::step()` should write these directly, or the next `step()` call would silently discard the change (see [09-physics-engine.md](09-physics-engine.md)).
- `Transform` (in the ECS) is what the renderer and camera actually read. It has no opinion about physics at all — it is just a position/rotation/scale a `Scene` entity happens to carry.

The bridge is a single, deliberately thin component:

```cpp
struct RigidBodyRef {
    u32 body_id = kInvalidBodyId;  // index into a PhysicsWorld, nothing more
};
```

`RigidBodyRef` is not a copy of `RigidBody`, and not a pointer into `PhysicsWorld` — just an opaque integer. This keeps `ge_world` free of any dependency on `ge_physics` (`world::components.h` never includes a physics header), at the cost of one indirection every time gameplay code needs the actual `RigidBody` state.

Two free functions in the sandbox (not in `ge_world` — see [08-ecs-and-world.md](08-ecs-and-world.md) for why the glue lives at the application layer) resolve that indirection once per fixed step, in a fixed order:

```cpp
// Before PhysicsWorld::step(): gameplay -> physics.
// Sets the desired horizontal velocity on the RigidBody from input.
void system_move_player_physics(world::World& w, world::Entity player,
                                 physics::PhysicsWorld& phys,
                                 f32 move_fwd, f32 move_right, f32 mouse_dx, f32 mouse_dy);

// After PhysicsWorld::step(): physics -> gameplay.
// Copies the RigidBody's resulting position/orientation into Transform.
void system_sync_transform_from_rigidbody(world::World& w, physics::PhysicsWorld& phys);
```

The ordering matters: input has to reach the `RigidBody` *before* `step()` runs (so the solver integrates this frame's desired velocity, not last frame's), and `Transform` has to be refreshed *after* `step()` runs (so the renderer sees where the body actually ended up, including anything the solver's collision response changed). Swapping the order would make movement lag by exactly one fixed step.

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
5. input_map.resolve(window)  — re-read all bound keys
       |
6. Handle non-gameplay input:
   - Mouse capture toggle (right click)
   - Escape to quit
       |
7. Fixed-timestep update loop (per iteration):
   a. system_move_player_physics(...)   — input -> RigidBody velocity/orientation
   b. phys.step(fixed_dt)               — gravity -> broadphase -> narrowphase ->
                                           manifold -> solve -> integrate
   c. system_sync_transform_from_rigidbody(...) — RigidBody -> Transform
       |
8. Chase camera derived from the player's (now-updated) Transform
       |
9. renderer.set_camera(...), renderer.set_model_transform(...)
       |
10. GE_PROFILE_SCOPE("render")
    renderer.begin_frame(dt)
    renderer.end_frame()
       |
11. profiler().end_frame()
       |
12. FPS counter update
```

Step 7 is the fixed-rate workload (potentially multiple iterations per frame, or zero); step 10 (render) runs exactly once per frame regardless of how many physics steps happened. This is the same accumulator discipline described above, just with three sub-steps instead of one.

---

## Input Handling and the Physics-Driven Character

The sandbox no longer moves a `gfx::Camera` directly from WASD — Phase 3's fly-through camera has been replaced by a physics-driven character (Phase 4 made it an ECS entity; Phase 5 made it a `PhysicsWorld` rigid body). The bindings are also simpler, since vertical motion is no longer a direct input:

| Input | Action |
|-------|--------|
| W / S | Move forward / backward (horizontal, relative to look yaw) |
| A / D | Strafe left / right |
| Left Shift | Triple movement speed |
| Right mouse button (hold) | Capture mouse for look |
| Mouse movement (while captured) | Yaw / pitch look direction |
| Escape | Quit the application |

There is no longer a bound "move up/down" axis (the old Q/E keys) — gravity and the physics solver own vertical motion entirely; a player capsule that could freely fly up and down would just tunnel through the ground it is supposed to be resting on.

`system_move_player_physics()` runs once per fixed step, before `phys.step()`:

```cpp
void system_move_player_physics(world::World& w, world::Entity player, physics::PhysicsWorld& phys,
                                 f32 move_fwd, f32 move_right, f32 mouse_dx, f32 mouse_dy) {
    auto* cc     = w.get_component<world::CharacterController>(player);
    auto* rb_ref = w.get_component<world::RigidBodyRef>(player);
    if (cc == nullptr || rb_ref == nullptr || rb_ref->body_id == world::kInvalidBodyId) return;

    cc->yaw   += mouse_dx * cc->look_sensitivity;
    cc->pitch += mouse_dy * cc->look_sensitivity;
    // ... clamp pitch to +-89 degrees ...

    // Yaw-only facing for movement direction: looking up/down should not
    // tilt the character into the ground or the sky.
    gfx::Camera facing;
    facing.yaw = cc->yaw;
    vec3 fwd   = facing.forward();
    vec3 right = facing.right();

    vec3 move = fwd * move_fwd + right * move_right;
    if (length_sq(move) > 0.0001f) move = normalize(move);

    physics::RigidBody& body = phys.body(rb_ref->body_id);
    body.linear_velocity.x = move.x * cc->move_speed;
    body.linear_velocity.z = move.z * cc->move_speed;   // .y untouched: gravity/solver own it
    body.orientation = quat_from_axis_angle({0, 1, 0}, cc->yaw);
}
```

Three things are worth calling out:

1. **Only X/Z velocity is set.** `body.linear_velocity.y` is left exactly as `phys.step()`'s gravity and solver left it from the previous step. Overwriting it here would fight gravity and make the character unable to fall or land.
2. **Facing is set directly, not integrated by the solver.** The player capsule's `inv_inertia_local` is always zero (see [09-physics-engine.md](09-physics-engine.md)) — a player-controlled character shouldn't be tipped over by an unlucky collision impulse the way a thrown object could be. Its facing is a gameplay decision (which way is the player looking), not a physical one, so it is written directly from `yaw` every step.
3. **Movement direction ignores pitch.** `facing.pitch` is left at its default (0), so looking up or down changes where the camera looks without changing which way "forward" pushes the character.

---

## The Chase Camera

The camera is not a persistent object with its own state — it is recomputed every frame from the player's current `Transform` and look angles, matching the pattern established in Phase 4:

```cpp
world::Transform* pt = scene.world().get_component<world::Transform>(player);
world::CharacterController* cc = scene.world().get_component<world::CharacterController>(player);

gfx::Camera follow_cam;
follow_cam.yaw   = cc->yaw;
follow_cam.pitch = cc->pitch;
vec3 fwd = follow_cam.forward();
follow_cam.position = pt->position - fwd * 3.5f + vec3{0, 1.0f, 0};  // behind and above
renderer.set_camera(follow_cam);
renderer.set_model_transform(trs(pt->position, pt->rotation, pt->scale));
```

Because this runs *after* `system_sync_transform_from_rigidbody()`, `pt->position` already reflects this frame's physics step — the camera and the rendered model are never a frame behind the simulation.

---

## FPS Counter and Profiling

The sandbox tracks frame rate using a simple counter, and — since Phase 5 — also logs the player's height once a second, which turned out to be a useful zero-cost debugging aid: there is no rendered ground plane yet (the renderer only draws the loaded glTF model), so this log line is the easiest way to confirm the character is actually falling and settling under gravity rather than floating or sinking through the floor.

```cpp
f64 fps_timer = 0.0;
u32 frames    = 0;

// In the loop:
++frames;
fps_timer += frame_time;
if (fps_timer >= 1.0) {
    f64 avg_ms = profiler().last_frame_time() * 1000.0;
    const vec3& player_pos = scene.world().get_component<world::Transform>(player)->position;
    GE_INFO("FPS: {} | frame: {:.2f} ms | player.y: {:.2f}", frames, avg_ms, player_pos.y);
    frames = 0;
    fps_timer -= 1.0;
}
```

Every second, it logs the frame count, the profiler's last frame time (in milliseconds), and the player's current Y position. The profiler provides accurate timing because it uses the high-resolution timer and tracks the duration of each `begin_frame()`-to-`end_frame()` cycle.

The `GE_PROFILE_SCOPE("render")` macro wraps the render call, so the profiler records how long rendering takes each frame. Additional scopes can be added to any function to build up a hierarchical timing breakdown.

---

## Shutdown Sequence

Shutdown happens in reverse order of initialization:

```cpp
GE_INFO("Shutting down ...");
scene.shutdown();         // Destroy all entities and their components
phys.shutdown();          // Release the PhysicsWorld's body/pair/constraint storage
renderer.shutdown();      // Flush GPU, release all DX12 resources
window.destroy();         // Destroy the HWND
perm.shutdown();          // Release the permanent arena's virtual memory
cvar_system().shutdown(); // Clear CVars
profiler().shutdown();    // Reset profiler state
GE_INFO("Clean exit.");
```

`scene` and `phys` are shut down before `renderer` and `window`, but the relative order between `scene`/`phys` and `renderer`/`window` does not matter — they own disjoint arena-backed memory with no cross-references. What *does* matter: `renderer.shutdown()` must come before `perm.shutdown()`, because it needs to flush the GPU (wait for all in-flight work to complete) before releasing resources. If the arena were freed first, the GPU might still be reading from memory that has been returned to the OS.

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
  |  input_map.resolve(window)     → action/axis states refreshed
  |  check mouse-capture / quit
  |
  |  fixed-timestep loop (0+ iterations):
  |    system_move_player_physics(...)     → RigidBody velocity/orientation set from input
  |    phys.step(fixed_dt):
  |      apply gravity to dynamic bodies
  |      broadphase: rebuild AABBs, sweep-and-prune pair generation
  |      narrowphase + manifold: per candidate pair, up to 4 contact points
  |      solver: sequential-impulse velocity iterations (Baumgarte + friction)
  |      integrate: semi-implicit Euler (position, then quaternion orientation)
  |    system_sync_transform_from_rigidbody(...) → Transform updated from RigidBody
  |
  |  chase camera recomputed from player Transform + look angles
  |  renderer.set_camera(camera)
  |  renderer.set_model_transform(trs(player_transform))
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
  |        set pipeline (PBR or model)
  |        set viewport/scissor (window size)
  |        clear render target + depth
  |        set per-frame CB (camera, lights)
  |        for each sub-mesh: set material texture(s), set per-object CB, draw_indexed
  |      Barrier: back_buffer RenderTarget → Present
  |
  |    rhi.end_frame()              → command list closed and submitted
  |    rhi.present(vsync)           → swap chain present + fence signal
  |
  |  profiler.end_frame()
  |  FPS + player.y counter update
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

The CPU and GPU work in parallel through double buffering: while the GPU renders frame N, the CPU records frame N+1. The fence synchronization ensures they never interfere with each other's resources. The physics/ECS work (`phys.step()` and the two sync systems) is pure CPU work inside the fixed-timestep loop, entirely separate from — and finished well before — the GPU command recording that follows it.
