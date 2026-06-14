#include <doctest/doctest.h>

#include "gfx/renderer.h"
#include "platform/window.h"

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
