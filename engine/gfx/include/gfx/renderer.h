#pragma once
#include "core/allocator.h"
#include "core/mat4.h"
#include "gfx/camera.h"
#include "gfx/light.h"
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
    bool init(const RendererDesc& desc);
    void shutdown();

    void begin_frame(float dt = 0.0f);
    void end_frame();

    void resize(std::uint32_t width, std::uint32_t height);
    void set_clear_color(float r, float g, float b, float a);

    /** @brief Load a glTF model and prepare it for rendering. */
    bool load_model(const char* path, ge::Allocator alloc);

    /** @brief Set the active camera for rendering. Copied each frame. */
    void set_camera(const Camera& cam);

    /**
     * @brief Set the model-to-world transform for the currently loaded model.
     *
     * For a pure rotation matrix the normal matrix equals the rotation itself.
     * Call this before @c begin_frame to take effect on the current frame.
     */
    void set_model_transform(const mat4& transform);

    /** @brief Set the directional (sun) light. */
    void set_sun(const DirectionalLight& sun);

    /**
     * @brief Set point lights for this frame.
     * @param lights Array of point lights.
     * @param count  Number of lights (max kMaxPointLights).
     */
    void set_point_lights(const PointLight* lights, std::uint32_t count);

    std::uint32_t debug_error_count() const;
    bool capture_back_buffer(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_width,
                              std::uint32_t& out_height) const;

private:
    void* impl_ = nullptr;
};

} // namespace ge::gfx
