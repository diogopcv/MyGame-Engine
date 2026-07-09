#pragma once
#include "core/allocator.h"
#include "core/mat4.h"
#include "gfx/camera.h"
#include "gfx/light.h"
#include <cstdint>
#include <vector>

/**
 * @file renderer.h
 * @brief High-level scene renderer (Layer 3 — Graphics subsystem).
 *
 * Backend-agnostic: all GPU work goes through the RHI (`gfx/rhi.h`), which
 * hides the graphics API behind opaque handles; no OS or D3D types appear in
 * the renderer's interface or implementation.  The window handle travels as
 * an opaque `void*` (from `platform::Window::native_handle()`).
 *
 * Frame flow:
 * @code
 *   renderer.begin_frame(dt);
 *   renderer.submit(model_a, transform_0);   // any number of instances
 *   renderer.submit(model_a, transform_1);   // of any mix of models
 *   renderer.submit(model_b, transform_2);
 *   renderer.end_frame();                    // renders + presents
 * @endcode
 *
 * If nothing is submitted in a frame, the built-in reference cube is drawn
 * (used by the golden-image test and as an empty-scene placeholder).
 */
namespace ge::gfx {

/** @brief Identifies a model loaded via Renderer::load_model(). */
using ModelId = std::uint32_t;

/** @brief Sentinel for "no model" / failed load. */
inline constexpr ModelId kInvalidModelId = ~ModelId(0);

/**
 * @brief Descriptor for initialising the renderer.
 */
struct RendererDesc {
    void*         hwnd               = nullptr; ///< Native window handle as `void*`.
    std::uint32_t width              = 1280;    ///< Initial back-buffer width in pixels.
    std::uint32_t height             = 720;     ///< Initial back-buffer height in pixels.
    bool          enable_debug_layer = true;     ///< Enable GPU validation / debug layer.
    bool          vsync              = true;     ///< Enable vertical sync on present.
};

/**
 * @brief High-level scene renderer with pImpl-hidden state.
 *
 * Owns an RhiDevice, a RenderGraph, the built-in pipelines (PBR, shadow,
 * reference cube), and the table of loaded models.  Draws are collected per
 * frame through submit() and consumed by end_frame().
 */
class Renderer {
public:
    bool init(const RendererDesc& desc);
    void shutdown();

    /** @brief Begin a frame: acquire the frame slot and reset the draw list. */
    void begin_frame(float dt = 0.0f);

    /** @brief Render everything submitted this frame, then present. */
    void end_frame();

    void resize(std::uint32_t width, std::uint32_t height);
    void set_clear_color(float r, float g, float b, float a);

    /**
     * @brief Load a glTF model and prepare it for rendering.
     * @param path  File path to the .gltf or .glb file.
     * @param alloc Allocator for the model's CPU-side arrays.
     * @return Id to pass to submit(), or kInvalidModelId on failure.
     */
    ModelId load_model(const char* path, ge::Allocator alloc);

    /** @brief Destroy a loaded model's GPU resources and free its slot. */
    void unload_model(ModelId id);

    /**
     * @brief Submit one instance of a loaded model for this frame.
     *
     * Valid between begin_frame() and end_frame().  Submissions do not
     * persist across frames — resubmit every frame the instance is visible.
     *
     * @param id        Model returned by load_model().
     * @param transform Model-to-world matrix for this instance.
     */
    void submit(ModelId id, const mat4& transform);

    /** @brief Set the active camera for rendering. Copied each frame. */
    void set_camera(const Camera& cam);

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
