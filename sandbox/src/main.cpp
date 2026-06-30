#include "core/types.h"
#include "core/log.h"
#include "core/arena.h"
#include "core/profiler.h"
#include "core/cvar.h"
#include "platform/window.h"
#include "platform/timer.h"
#include "gfx/renderer.h"
#include "gfx/camera.h"
#include "gfx/light.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>

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
    desc.title  = "MyEngine - PBR Renderer";
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

    // Load model
    {
        wchar_t exe_path[MAX_PATH];
        ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
        auto model_path = std::filesystem::path(exe_path).parent_path()
                          / ".." / ".." / ".." / "assets" / "models" / "DamagedHelmet.glb";
        std::string model_str = model_path.string();
        if (renderer.load_model(model_str.c_str(), perm.allocator())) {
            GE_INFO("Loaded model: {}", model_str);
        } else {
            GE_WARN("Could not load model — using built-in cube");
        }
    }

    // Set up camera — DamagedHelmet fits in roughly a 2-unit sphere at origin
    ge::gfx::Camera camera;
    camera.position = {0, 0.5f, -3.5f};
    camera.yaw      = 0.0f;
    camera.pitch    = -0.05f;
    renderer.set_camera(camera);

    // Set up lighting
    ge::gfx::DirectionalLight sun;
    sun.direction = {0.5f, 1.0f, -0.3f};
    sun.color     = {1.0f, 1.0f, 0.95f};
    sun.intensity = 1.0f;
    renderer.set_sun(sun);

    ge::gfx::PointLight point_lights[2];
    point_lights[0].position  = {2.0f, 1.0f, -1.0f};
    point_lights[0].radius    = 8.0f;
    point_lights[0].color     = {0.3f, 0.5f, 1.0f};
    point_lights[0].intensity = 1.5f;

    point_lights[1].position  = {-2.0f, 0.5f, 1.0f};
    point_lights[1].radius    = 6.0f;
    point_lights[1].color     = {1.0f, 0.3f, 0.2f};
    point_lights[1].intensity = 1.2f;
    renderer.set_point_lights(point_lights, 2);

    bool mouse_captured = false;

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

        // Toggle mouse capture with right click
        if (window.mouse_button_down(platform::MouseButton::Right) && !mouse_captured) {
            mouse_captured = true;
            window.set_mouse_capture(true);
        } else if (!window.mouse_button_down(platform::MouseButton::Right) && mouse_captured) {
            mouse_captured = false;
            window.set_mouse_capture(false);
        }

        // Escape to close
        if (window.key_down(platform::KeyCode::Escape)) {
            break;
        }

        // Camera input
        while (accumulator >= fixed_dt) {
            f32 move_fwd   = 0.0f;
            f32 move_right = 0.0f;
            f32 move_up    = 0.0f;

            if (window.key_down(platform::KeyCode::W)) move_fwd   += 1.0f;
            if (window.key_down(platform::KeyCode::S)) move_fwd   -= 1.0f;
            if (window.key_down(platform::KeyCode::D)) move_right += 1.0f;
            if (window.key_down(platform::KeyCode::A)) move_right -= 1.0f;
            if (window.key_down(platform::KeyCode::E)) move_up    += 1.0f;
            if (window.key_down(platform::KeyCode::Q)) move_up    -= 1.0f;

            f32 mdx = 0.0f, mdy = 0.0f;
            if (mouse_captured) {
                window.mouse_delta(mdx, mdy);
            }

            f32 speed = 3.0f;
            if (window.key_down(platform::KeyCode::LShift)) speed *= 3.0f;

            ge::gfx::camera_fly_update(camera, static_cast<f32>(fixed_dt),
                                       move_fwd, move_right, move_up,
                                       mdx, mdy, speed);

            accumulator -= fixed_dt;
        }

        renderer.set_camera(camera);

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
