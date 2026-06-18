#pragma once
#include <cstdint>
#include <vector>

/**
 * @file renderer.h
 * @brief DX12 renderer facade (Layer 3 — Graphics subsystem).
 *
 * All `<d3d12.h>`, `<dxgi1_6.h>`, and `<windows.h>` types are hidden inside
 * `renderer_dx12.cpp` behind an opaque impl pointer, mirroring the Window
 * pImpl pattern.
 */
namespace ge::gfx {

/**
 * @brief Descriptor for initialising the DX12 renderer.
 */
struct RendererDesc {
    void*         hwnd               = nullptr; ///< HWND as `void*` (from platform::Window::native_handle()).
    std::uint32_t width              = 1280;    ///< Initial back-buffer width in pixels.
    std::uint32_t height             = 720;     ///< Initial back-buffer height in pixels.
    bool          enable_debug_layer = true;     ///< Enable the D3D12 debug layer, GPU-based validation, and DRED.
    bool          vsync              = true;     ///< Enable vertical sync on present.
};

/**
 * @brief High-level DX12 renderer with pImpl-hidden GPU state.
 */
class Renderer {
public:
    /**
     * @brief Create the DX12 device, swap chain, command queue, and GPU resources.
     * @param desc Renderer creation parameters.
     * @return `true` on success.
     */
    bool init(const RendererDesc& desc);

    /** @brief Flush the GPU and release all DX12 resources. */
    void shutdown();

    /**
     * @brief Begin a new frame.
     *
     * Waits for this frame's slot, resets the command allocator/list, and
     * transitions + clears the current back buffer and depth buffer.
     *
     * @param dt Delta time added to an internal clock (drives the cube
     *           rotation in Milestone 4).
     */
    void begin_frame(float dt = 0.0f);

    /**
     * @brief End the current frame.
     *
     * Transitions the back buffer to PRESENT, submits the command list,
     * presents, and signals the frame's fence value.
     */
    void end_frame();

    /**
     * @brief Handle a window resize.
     *
     * Flushes the GPU, resizes the swap-chain buffers, and recreates RTVs
     * and the depth buffer.
     *
     * @param width  New client-area width in pixels.
     * @param height New client-area height in pixels.
     */
    void resize(std::uint32_t width, std::uint32_t height);

    /**
     * @brief Set the clear colour for subsequent frames.
     * @param r Red   [0, 1].
     * @param g Green [0, 1].
     * @param b Blue  [0, 1].
     * @param a Alpha [0, 1].
     */
    void set_clear_color(float r, float g, float b, float a);

    /**
     * @brief Query the number of DX12 debug-layer error messages since init().
     * @return Error count. Always 0 when `enable_debug_layer` was false.
     */
    std::uint32_t debug_error_count() const;

    /**
     * @brief Read back the most recently presented back buffer as RGBA8.
     *
     * The output is tightly-packed, row-major, top-to-bottom.  Flushes the
     * GPU — not for use in the hot path.
     *
     * @param[out] out_rgba   Receives the pixel data.
     * @param[out] out_width  Width of the captured image.
     * @param[out] out_height Height of the captured image.
     * @return `true` on success.
     */
    bool capture_back_buffer(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_width,
                              std::uint32_t& out_height) const;

private:
    void* impl_ = nullptr; ///< Opaque pointer to RendererImpl (renderer_dx12.cpp).
};

} // namespace ge::gfx
