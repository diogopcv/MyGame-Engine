#pragma once
#include <cstdint>
#include <vector>

// The DX12 renderer facade. All <d3d12.h>/<dxgi1_6.h> types and <windows.h>
// stay inside renderer_dx12.cpp behind an opaque impl pointer, mirroring the
// Window pImpl pattern (engine/platform/include/platform/window.h).
namespace ge::gfx {

struct RendererDesc {
    // The HWND, as a void* (from platform::Window::native_handle()).
    void*         hwnd               = nullptr;
    std::uint32_t width              = 1280;
    std::uint32_t height             = 720;
    // Enables the D3D12 debug layer, GPU-based validation, and DRED. Pass
    // false in Release or when the smoke test wants a quieter run.
    bool          enable_debug_layer = true;
    bool          vsync              = true;
};

class Renderer {
public:
    bool init(const RendererDesc& desc);
    void shutdown();

    // Wait for this frame's slot, reset the command allocator/list, and
    // transition + clear the current back buffer and depth buffer. `dt` is
    // added to an internal clock that drives the cube's rotation (Milestone 4).
    void begin_frame(float dt = 0.0f);

    // Transition the back buffer to PRESENT, submit, present, and signal the
    // frame's fence value.
    void end_frame();

    // Flushes the GPU, resizes the swapchain buffers, and recreates RTVs +
    // the depth buffer.
    void resize(std::uint32_t width, std::uint32_t height);

    void set_clear_color(float r, float g, float b, float a);

    // Number of ERROR/CORRUPTION-severity messages recorded by the D3D12
    // debug info queue since init(). Always 0 if enable_debug_layer was
    // false. Used by the smoke test.
    std::uint32_t debug_error_count() const;

    // Reads back the back buffer presented by the most recent end_frame() as
    // tightly-packed RGBA8 (row-major, top-to-bottom). Used by the
    // golden-image test. Flushes the GPU; not for use in the hot path.
    bool capture_back_buffer(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_width,
                              std::uint32_t& out_height) const;

private:
    void* impl_ = nullptr; // -> RendererImpl, defined in renderer_dx12.cpp
};

} // namespace ge::gfx
