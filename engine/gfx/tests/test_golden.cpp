#include <doctest/doctest.h>

#include "gfx/renderer.h"
#include "platform/window.h"

#include <cstdlib>
#include <filesystem>
#include <vector>

#include <stb_image.h>
#include <stb_image_write.h>

// Phase 1, Milestone 4 golden-image test (docs/phases/01-dx12-triangle.md):
// run a fixed number of frames at a fixed timestep so the spinning cube's
// pose is deterministic, capture the back buffer, and compare it against a
// checked-in baseline PNG with a small per-channel tolerance.
//
// Set GE_GOLDEN_UPDATE=1 to (re)write the baseline from the captured frame
// instead of comparing -- a deliberate, reviewed action, never part of a
// normal test run.
TEST_CASE("DX12 renderer: golden image of the spinning cube") {
    ge::platform::Window window;
    ge::platform::WindowDesc window_desc{};
    window_desc.title  = "MyEngine - gfx golden image test";
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

    // Fixed timestep: total_time == 1.5s at the moment of capture, regardless
    // of how long this actually takes to run.
    constexpr float kFixedDt = 1.0f / 60.0f;
    constexpr int   kFrames  = 90;
    for (int i = 0; i < kFrames; ++i) {
        window.pump_events();
        renderer.begin_frame(kFixedDt);
        renderer.end_frame();
    }

    CHECK(renderer.debug_error_count() == 0);

    std::vector<std::uint8_t> actual;
    std::uint32_t             width = 0, height = 0;
    REQUIRE(renderer.capture_back_buffer(actual, width, height));

    renderer.shutdown();
    window.destroy();

    const std::filesystem::path baseline_path =
        std::filesystem::path(GE_GFX_GOLDEN_DIR) / "cube.png";

    char* golden_update = nullptr;
    std::size_t golden_update_len = 0;
    _dupenv_s(&golden_update, &golden_update_len, "GE_GOLDEN_UPDATE");
    const bool update_baseline = golden_update != nullptr;
    std::free(golden_update);

    if (update_baseline) {
        REQUIRE(stbi_write_png(baseline_path.string().c_str(), static_cast<int>(width),
                                static_cast<int>(height), 4, actual.data(),
                                static_cast<int>(width) * 4) != 0);
        MESSAGE("Wrote golden baseline: ", baseline_path.string());
        return;
    }

    REQUIRE_MESSAGE(std::filesystem::exists(baseline_path),
                    "Golden baseline missing at ", baseline_path.string(),
                    " -- run with GE_GOLDEN_UPDATE=1 to generate it.");

    int base_w = 0, base_h = 0, base_channels = 0;
    unsigned char* base_pixels =
        stbi_load(baseline_path.string().c_str(), &base_w, &base_h, &base_channels, 4);
    REQUIRE(base_pixels != nullptr);

    REQUIRE(base_w == static_cast<int>(width));
    REQUIRE(base_h == static_cast<int>(height));

    // Small per-channel tolerance for driver/hardware floating-point
    // differences; the scene has no AA so we expect near-exact matches.
    constexpr int kChannelTolerance = 4;
    std::size_t   mismatches        = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const int diff = std::abs(static_cast<int>(actual[i]) - static_cast<int>(base_pixels[i]));
        if (diff > kChannelTolerance) ++mismatches;
    }

    stbi_image_free(base_pixels);

    CHECK_MESSAGE(mismatches == 0, mismatches, " pixel channel(s) exceeded tolerance ",
                  kChannelTolerance);
}
