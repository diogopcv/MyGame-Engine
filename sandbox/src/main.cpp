#include "core/types.h"
#include "core/log.h"
#include "core/arena.h"
#include "core/profiler.h"
#include "core/cvar.h"
#include "platform/window.h"
#include "platform/timer.h"
#include "gfx/renderer.h"

int main() {
    using namespace ge;

    platform::time_init();
    GE_INFO("Booting {} ...", "MyEngine");

    profiler().init();
    cvar_system().init();

    cvar_system().register_bool("r_vsync", true, "enable vsync");
    cvar_system().register_int("r_width", 1280, "render width");
    cvar_system().register_int("r_height", 720, "render height");

    Arena perm;
    perm.init(GE_MEGABYTES(64));
    GE_INFO("Permanent arena reserved: {} MB", perm.capacity() / (1024 * 1024));

    platform::Window window;
    platform::WindowDesc desc{};
    desc.title  = "MyEngine - Phase 2";
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

    const f64 fixed_dt    = 1.0 / 60.0;
    f64       accumulator = 0.0;
    f64       last        = platform::now_seconds();

    f64 fps_timer = 0.0;
    u32 frames    = 0;

    while (!window.should_close()) {
        profiler().begin_frame();

        const f64 now        = platform::now_seconds();
        f64       frame_time = now - last;
        last = now;

        if (frame_time > 0.25) frame_time = 0.25;
        accumulator += frame_time;

        window.pump_events();
        renderer.resize(window.width(), window.height());

        while (accumulator >= fixed_dt) {
            accumulator -= fixed_dt;
        }

        {
            GE_PROFILE_SCOPE("render");
            renderer.begin_frame(static_cast<float>(frame_time));
            renderer.end_frame();
        }

        profiler().end_frame();

        ++frames;
        fps_timer += frame_time;
        if (fps_timer >= 1.0) {
            f64 avg_ms = profiler().last_frame_time() * 1000.0;
            GE_INFO("FPS: {} | frame: {:.2f} ms", frames, avg_ms);
            frames = 0;
            fps_timer -= 1.0;
        }
    }

    GE_INFO("Shutting down ...");
    renderer.shutdown();
    window.destroy();
    perm.shutdown();
    cvar_system().shutdown();
    profiler().shutdown();
    GE_INFO("Clean exit.");
    return 0;
}
