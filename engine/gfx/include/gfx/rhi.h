#pragma once
#include "gfx/rhi_types.h"
#include <cstdint>
#include <vector>

/**
 * @file rhi.h
 * @brief Render Hardware Interface — handle-based GPU resource management (Layer 3).
 *
 * RhiDevice owns all GPU state (device, queues, heaps, resources) behind a
 * pImpl.  Resources are identified by opaque handles backed by core
 * HandleTables.  DX12 is the only backend — no virtual dispatch.
 */
namespace ge::gfx {

class RhiDevice {
public:
    // -- Lifecycle ----------------------------------------------------------

    /** @brief Create the GPU device, command infrastructure, and swapchain. */
    bool init(const RhiDeviceDesc& desc);

    /** @brief Flush the GPU and release all resources. */
    void shutdown();

    // -- Resource creation / destruction ------------------------------------

    /**
     * @brief Create a GPU buffer.
     * @param desc Buffer creation parameters.
     * @return Handle to the new buffer, or invalid handle on failure.
     */
    BufferHandle create_buffer(const BufferDesc& desc);

    /**
     * @brief Create a GPU texture.
     * @param desc Texture creation parameters.
     * @return Handle to the new texture, or invalid handle on failure.
     */
    TextureHandle create_texture(const TextureDesc& desc);

    /**
     * @brief Register compiled shader bytecode.
     * @param desc Shader creation parameters.
     * @return Handle to the shader, or invalid handle on failure.
     */
    ShaderHandle create_shader(const ShaderDesc& desc);

    /**
     * @brief Create a graphics pipeline state object.
     * @param desc Pipeline creation parameters.
     * @return Handle to the pipeline, or invalid handle on failure.
     */
    PipelineHandle create_pipeline(const PipelineDesc& desc);

    void destroy_buffer(BufferHandle h);
    void destroy_texture(TextureHandle h);
    void destroy_shader(ShaderHandle h);
    void destroy_pipeline(PipelineHandle h);

    // -- Data transfer ------------------------------------------------------

    /**
     * @brief Write data into a CPU-visible buffer.
     * @param h      Buffer handle (must be CPU-visible).
     * @param data   Source data pointer.
     * @param size   Number of bytes to write.
     * @param offset Byte offset into the buffer.
     */
    void update_buffer(BufferHandle h, const void* data, u32 size, u32 offset = 0);

    /** @brief Map a CPU-visible buffer for writing. */
    void* map_buffer(BufferHandle h);

    /** @brief Unmap a previously mapped buffer. */
    void unmap_buffer(BufferHandle h);

    // -- Frame lifecycle ----------------------------------------------------

    /** @brief Begin a new frame: wait for the frame slot, reset allocator/list. */
    void begin_frame();

    /** @brief End the frame: submit the command list. Does NOT present. */
    void end_frame();

    /** @brief Present the current back buffer and signal the frame fence. */
    void present(bool vsync);

    /** @brief Handle a window resize (flush, resize swapchain, recreate targets). */
    void resize(u32 width, u32 height);

    // -- Command recording --------------------------------------------------

    /** @brief Insert a resource barrier (texture state transition). */
    void barrier(TextureHandle tex, ResourceState before, ResourceState after);

    /** @brief Set render target(s) and depth stencil for subsequent draws. */
    void set_render_targets(const TextureHandle* rts, u32 rt_count, TextureHandle depth);

    /** @brief Clear a render target to the given colour. */
    void clear_render_target(TextureHandle rt, const f32 color[4]);

    /** @brief Clear a depth-stencil target. */
    void clear_depth(TextureHandle ds, f32 depth);

    /** @brief Bind a graphics pipeline for subsequent draws. */
    void set_pipeline(PipelineHandle h);

    /** @brief Bind a vertex buffer to slot 0. */
    void set_vertex_buffer(BufferHandle h, u32 stride);

    /** @brief Bind an index buffer. */
    void set_index_buffer(BufferHandle h, Format format);

    /** @brief Bind a constant buffer to a root parameter slot. */
    void set_constant_buffer(u32 slot, BufferHandle h);

    /** @brief Bind a texture SRV to a root parameter slot. */
    void set_texture(u32 slot, TextureHandle h);

    /** @brief Set the viewport. */
    void set_viewport(f32 x, f32 y, f32 w, f32 h, f32 min_depth, f32 max_depth);

    /** @brief Set the scissor rect. */
    void set_scissor(i32 x, i32 y, u32 w, u32 h);

    /** @brief Issue an indexed draw call. */
    void draw_indexed(u32 index_count, u32 start_index = 0, i32 base_vertex = 0);

    // -- Swapchain queries --------------------------------------------------

    /** @brief Get a handle to the current back buffer texture. */
    TextureHandle back_buffer() const;

    /** @brief Get a handle to the swapchain depth buffer. */
    TextureHandle depth_buffer() const;

    /** @brief Current back-buffer width. */
    u32 width() const;

    /** @brief Current back-buffer height. */
    u32 height() const;

    // -- Debug --------------------------------------------------------------

    /** @brief Number of DX12 debug-layer errors since init(). */
    u32 debug_error_count() const;

    /**
     * @brief Read back the current back buffer as RGBA8.
     * @param[out] out_rgba   Receives the pixel data.
     * @param[out] out_width  Width.
     * @param[out] out_height Height.
     * @return true on success.
     */
    bool capture_back_buffer(std::vector<std::uint8_t>& out_rgba,
                             std::uint32_t& out_width,
                             std::uint32_t& out_height) const;

private:
    void* impl_ = nullptr;
};

} // namespace ge::gfx
