#include <doctest/doctest.h>

#include "gfx/renderer.h"
#include "core/arena.h"
#include "core/mat4.h"
#include "platform/window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <cmath>

// Phase 1, Milestone 1 smoke test (docs/phases/01-dx12-triangle.md):
// device init, present N frames, clean shutdown, zero DX12 debug-layer
// errors. A real HWND is required for the swapchain, so we create (and show)
// a small window for the duration of the test.
TEST_CASE("DX12 renderer: init, N frames, clean shutdown, no debug errors") {
    ge::platform::Window window;
    ge::platform::WindowDesc window_desc{};
    window_desc.title  = "MyEngine - gfx smoke test";
    window_desc.width  = 640;
    window_desc.height = 480;
    REQUIRE(window.create(window_desc));

    ge::gfx::Renderer renderer;
    ge::gfx::RendererDesc renderer_desc{};
    renderer_desc.hwnd               = window.native_handle();
    renderer_desc.width              = window.width();
    renderer_desc.height             = window.height();
    renderer_desc.enable_debug_layer = true;
    renderer_desc.vsync              = false;

    REQUIRE(renderer.init(renderer_desc));

    constexpr int kFrames = 60;
    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        renderer.begin_frame();
        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    renderer.shutdown();
    window.destroy();
}

TEST_CASE("DX12 renderer: load glTF model and render frames without errors") {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    auto model_path = std::filesystem::path(exe_path).parent_path()
                      / ".." / ".." / ".." / "assets" / "models" / "box.glb";

    if (!std::filesystem::exists(model_path)) {
        MESSAGE("Skipping model test: box.glb not found at ", model_path.string());
        return;
    }

    ge::Arena arena;
    arena.init(GE_MEGABYTES(4));

    ge::platform::Window window;
    ge::platform::WindowDesc window_desc{};
    window_desc.title  = "MyEngine - model smoke test";
    window_desc.width  = 640;
    window_desc.height = 480;
    REQUIRE(window.create(window_desc));

    ge::gfx::Renderer renderer;
    ge::gfx::RendererDesc renderer_desc{};
    renderer_desc.hwnd               = window.native_handle();
    renderer_desc.width              = window.width();
    renderer_desc.height             = window.height();
    renderer_desc.enable_debug_layer = true;
    renderer_desc.vsync              = false;
    REQUIRE(renderer.init(renderer_desc));

    std::string path_str = model_path.string();
    REQUIRE(renderer.load_model(path_str.c_str(), arena.allocator()));

    constexpr int kFrames = 30;
    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        renderer.begin_frame(1.0f / 60.0f);
        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    renderer.shutdown();
    window.destroy();
    arena.shutdown();
}

// Phase 3 Addendum — PBR test (docs/phases/03-renderer.md):
// Loads DamagedHelmet.glb, renders 5 seconds (~300 frames at 60 fps) with a
// white directional light and a model rotating around Y, and asserts zero
// D3D12 debug-layer errors.  Camera is pulled back so the full helmet fits
// in the 640×480 window.
TEST_CASE("DX12 renderer: PBR DamagedHelmet rotates 5 seconds, no debug errors") {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    auto model_path = std::filesystem::path(exe_path).parent_path()
                      / ".." / ".." / ".." / "assets" / "models" / "DamagedHelmet.glb";

    if (!std::filesystem::exists(model_path)) {
        MESSAGE("Skipping PBR test: DamagedHelmet.glb not found at ", model_path.string());
        return;
    }

    ge::Arena arena;
    arena.init(GE_MEGABYTES(16));

    ge::platform::Window window;
    ge::platform::WindowDesc window_desc{};
    window_desc.title  = "MyEngine - PBR smoke test";
    window_desc.width  = 640;
    window_desc.height = 480;
    REQUIRE(window.create(window_desc));

    ge::gfx::Renderer renderer;
    ge::gfx::RendererDesc renderer_desc{};
    renderer_desc.hwnd               = window.native_handle();
    renderer_desc.width              = window.width();
    renderer_desc.height             = window.height();
    renderer_desc.enable_debug_layer = true;
    renderer_desc.vsync              = false;
    REQUIRE(renderer.init(renderer_desc));

    std::string path_str = model_path.string();
    REQUIRE(renderer.load_model(path_str.c_str(), arena.allocator()));

    // Camera: pulled back along -Z so the ~1-unit helmet fills the frame.
    ge::gfx::Camera camera;
    camera.position = {0.0f, 0.0f, -2.5f};
    camera.yaw      = 0.0f;
    camera.pitch    = 0.0f;
    renderer.set_camera(camera);

    // White directional light from upper-right — chosen to show all PBR features.
    ge::gfx::DirectionalLight sun;
    sun.direction = {0.4f, 1.0f, -0.5f};
    sun.color     = {1.0f, 1.0f, 1.0f};
    sun.intensity = 2.0f;
    renderer.set_sun(sun);

    // White fill light from the left to soften shadows.
    ge::gfx::PointLight fill;
    fill.position  = {-2.0f, 0.5f, -1.5f};
    fill.radius    = 8.0f;
    fill.color     = {1.0f, 1.0f, 1.0f};
    fill.intensity = 0.8f;
    renderer.set_point_lights(&fill, 1);

    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr int   kFrames  = 300; // 5 seconds at 60 fps
    float           time     = 0.0f;

    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        time += kFixedDt;
        renderer.set_model_transform(ge::mat4_rotation_y(time));
        renderer.begin_frame(kFixedDt);
        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    renderer.shutdown();
    window.destroy();
    arena.shutdown();
}
