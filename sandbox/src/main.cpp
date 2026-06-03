#include "core/types.h"
#include "core/log.h"
#include "core/arena.h"
#include "platform/window.h"
#include "platform/timer.h"

int main() {
    using namespace ge;

    platform::time_init();
    GE_INFO("Booting {} ...", "MyEngine");

    // A long-lived "permanent" arena. Subsystems will sub-allocate from
    // arenas like this rather than calling new/malloc all over the place.
    Arena perm;
    perm.init(GE_MEGABYTES(64));
    GE_INFO("Permanent arena reserved: {} MB", perm.capacity() / (1024 * 1024));

    platform::Window window;
    platform::WindowDesc desc{};
    desc.title  = "MyEngine - Phase 0";
    desc.width  = 1280;
    desc.height = 720;
    if (!window.create(desc)) {
        GE_ERROR("Failed to create window");
        return 1;
    }
    GE_INFO("Window created ({}x{})", window.width(), window.height());

    // --- The game loop -------------------------------------------------------
    // Fixed-timestep simulation decoupled from a variable-rate render. The
    // accumulator pattern keeps physics/gameplay deterministic regardless of
    // frame rate; rendering then interpolates between sim steps (Phase 4+).
    const f64 fixed_dt    = 1.0 / 60.0; // 60 simulation steps per second
    f64       accumulator = 0.0;
    f64       last        = platform::now_seconds();

    f64 fps_timer = 0.0;
    u32 frames    = 0;

    while (!window.should_close()) {
        const f64 now        = platform::now_seconds();
        f64       frame_time = now - last;
        last = now;

        // Clamp to avoid the "spiral of death" after a long stall (e.g. a
        // breakpoint or a window drag).
        if (frame_time > 0.25) frame_time = 0.25;
        accumulator += frame_time;

        window.pump_events();

        while (accumulator >= fixed_dt) {
            // Fixed simulation step. Gameplay and physics will live here.
            accumulator -= fixed_dt;
        }

        // const f64 alpha = accumulator / fixed_dt; // render interpolation (Phase 4+)
        //
        // Render goes here. In Phase 1 this becomes a DX12 clear + present.
        // For now the window's background brush is our visible "clear".

        ++frames;
        fps_timer += frame_time;
        if (fps_timer >= 1.0) {
            GE_INFO("FPS: {}", frames);
            frames = 0;
            fps_timer -= 1.0;
        }
    }

    GE_INFO("Shutting down ...");
    window.destroy();
    perm.shutdown();
    GE_INFO("Clean exit.");
    return 0;
}
