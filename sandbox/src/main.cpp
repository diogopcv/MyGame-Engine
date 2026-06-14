#include "core/types.h"
#include "core/log.h"
#include "core/arena.h"
#include "platform/window.h"
#include "platform/timer.h"
#include "gfx/renderer.h"

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

    ge::gfx::Renderer renderer;
    ge::gfx::RendererDesc renderer_desc{};
    renderer_desc.hwnd   = window.native_handle();
    renderer_desc.width  = window.width();
    renderer_desc.height = window.height();
#ifdef _DEBUG
    renderer_desc.enable_debug_layer = true;
#else
    renderer_desc.enable_debug_layer = false;
#endif
    if (!renderer.init(renderer_desc)) {
        GE_ERROR("Failed to initialize the DX12 renderer");
        window.destroy();
        return 1;
    }
    renderer.set_clear_color(0.05f, 0.1f, 0.2f, 1.0f);

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
        renderer.resize(window.width(), window.height());

        while (accumulator >= fixed_dt) {
            // Fixed simulation step. Gameplay and physics will live here.
            accumulator -= fixed_dt;
        }

        // const f64 alpha = accumulator / fixed_dt; // render interpolation (Phase 4+)

        renderer.begin_frame(static_cast<float>(frame_time));
        renderer.end_frame();

        ++frames;
        fps_timer += frame_time;
        if (fps_timer >= 1.0) {
            GE_INFO("FPS: {}", frames);
            frames = 0;
            fps_timer -= 1.0;
        }
    }

    GE_INFO("Shutting down ...");
    renderer.shutdown();
    window.destroy();
    perm.shutdown();
    GE_INFO("Clean exit.");
    return 0;
}
