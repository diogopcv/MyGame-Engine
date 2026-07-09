#include <doctest/doctest.h>

#include "gfx/renderer.h"
#include "core/arena.h"
#include "core/mat4.h"
#include "platform/window.h"
#include "platform/file.h"

#include <filesystem>
#include <string>
#include <cmath>

namespace {

/// Resolve a model path relative to the repo's assets folder, or an empty
/// string if the file doesn't exist (tests skip gracefully in that case).
std::string find_model(const char* name) {
    char exe_dir[512];
    if (ge::platform::executable_dir(exe_dir, sizeof(exe_dir)) == 0) return {};
    std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(exe_dir)));
    path = path / ".." / ".." / ".." / "assets" / "models" / name;
    if (!std::filesystem::exists(path)) return {};
    return path.string();
}

} // namespace

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
    const std::string model_path = find_model("box.glb");
    if (model_path.empty()) {
        MESSAGE("Skipping model test: box.glb not found");
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

    const ge::gfx::ModelId box = renderer.load_model(model_path.c_str(), arena.allocator());
    REQUIRE(box != ge::gfx::kInvalidModelId);

    constexpr int kFrames = 30;
    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        renderer.begin_frame(1.0f / 60.0f);
        renderer.submit(box, ge::mat4_identity());
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
    const std::string model_path = find_model("DamagedHelmet.glb");
    if (model_path.empty()) {
        MESSAGE("Skipping PBR test: DamagedHelmet.glb not found");
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

    const ge::gfx::ModelId helmet = renderer.load_model(model_path.c_str(), arena.allocator());
    REQUIRE(helmet != ge::gfx::kInvalidModelId);

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
        renderer.begin_frame(kFixedDt);
        renderer.submit(helmet, ge::mat4_rotation_y(time));
        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    renderer.shutdown();
    window.destroy();
    arena.shutdown();
}

// Multi-instance / multi-model submission test: several instances of the same
// model, plus a second distinct model when available, all in one frame. This
// exercises the per-draw constant-buffer ring (each instance gets its own
// transform) and per-model vertex/index/material binding in the same pass.
TEST_CASE("DX12 renderer: many instances of multiple models, no debug errors") {
    const std::string box_path = find_model("box.glb");
    if (box_path.empty()) {
        MESSAGE("Skipping multi-instance test: box.glb not found");
        return;
    }
    const std::string helmet_path = find_model("DamagedHelmet.glb");

    ge::Arena arena;
    arena.init(GE_MEGABYTES(32));

    ge::platform::Window window;
    ge::platform::WindowDesc window_desc{};
    window_desc.title  = "MyEngine - multi-instance smoke test";
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

    const ge::gfx::ModelId box = renderer.load_model(box_path.c_str(), arena.allocator());
    REQUIRE(box != ge::gfx::kInvalidModelId);

    ge::gfx::ModelId helmet = ge::gfx::kInvalidModelId;
    if (!helmet_path.empty()) {
        helmet = renderer.load_model(helmet_path.c_str(), arena.allocator());
        REQUIRE(helmet != ge::gfx::kInvalidModelId);
        REQUIRE(helmet != box);
    }

    ge::gfx::Camera camera;
    camera.position = {0.0f, 1.0f, -8.0f};
    renderer.set_camera(camera);

    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr int   kFrames  = 30;
    float           time     = 0.0f;

    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        time += kFixedDt;
        renderer.begin_frame(kFixedDt);

        // A row of spinning boxes, each with its own transform.
        for (int b = 0; b < 5; ++b) {
            const ge::mat4 xf = ge::mat4_rotation_y(time + static_cast<float>(b)) *
                                ge::mat4_translation({static_cast<float>(b - 2) * 2.0f, 0, 0});
            renderer.submit(box, xf);
        }
        if (helmet != ge::gfx::kInvalidModelId) {
            renderer.submit(helmet, ge::mat4_translation({0, 2.0f, 0}));
        }

        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    renderer.shutdown();
    window.destroy();
    arena.shutdown();
}
