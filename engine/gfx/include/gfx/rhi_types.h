#pragma once
#include "core/types.h"
#include "core/handle.h"

/**
 * @file rhi_types.h
 * @brief RHI enumerations, descriptor structs, and handle typedefs (Layer 3 — Graphics).
 *
 * All types are plain data — no OS or DX12 headers leak through.
 */
namespace ge::gfx {

/**
 * @brief Number of frames the CPU may record ahead of the GPU.
 *
 * Shared by the RHI (swapchain buffering, per-frame fences) and any caller
 * that keeps per-frame CPU-writable resources (e.g. constant-buffer rings):
 * a resource slot indexed by @c RhiDevice::frame_index() is safe to write
 * because @c begin_frame() waits for that slot's previous GPU work.
 */
constexpr u32 kRhiFrameCount = 2;

// ---------------------------------------------------------------------------
// Handle types
// ---------------------------------------------------------------------------

struct BufferTag {};
struct TextureTag {};
struct ShaderTag {};
struct PipelineTag {};

/** @brief Opaque handle to a GPU buffer resource. */
using BufferHandle   = Handle<BufferTag>;
/** @brief Opaque handle to a GPU texture resource. */
using TextureHandle  = Handle<TextureTag>;
/** @brief Opaque handle to compiled shader bytecode. */
using ShaderHandle   = Handle<ShaderTag>;
/** @brief Opaque handle to a pipeline state object. */
using PipelineHandle = Handle<PipelineTag>;

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/** @brief Pixel / vertex-element format. */
enum class Format : u8 {
    Unknown = 0,
    R8G8B8A8_UNORM,
    R8G8B8A8_UNORM_SRGB, ///< sRGB-encoded colour (hardware linearises on sample).
    R8G8B8A8_UINT,       ///< 4x u8 integers (skinned-vertex joint indices).
    R32G32_FLOAT,
    R32G32B32_FLOAT,
    R32G32B32A32_FLOAT,
    D32_FLOAT,
    R16_UINT,
    R32_UINT,
};

/** @brief How a buffer will be used. */
enum class BufferUsage : u8 {
    Vertex,
    Index,
    Constant,
    Upload,
    Readback,
};

/** @brief How a texture will be used (bitmask). */
enum class TextureUsage : u8 {
    ShaderResource = 1 << 0,
    RenderTarget   = 1 << 1,
    DepthStencil   = 1 << 2,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<u8>(a) | static_cast<u8>(b));
}
inline bool operator&(TextureUsage a, TextureUsage b) {
    return (static_cast<u8>(a) & static_cast<u8>(b)) != 0;
}

/** @brief Resource state for barrier transitions. */
enum class ResourceState : u8 {
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    PixelShaderResource,
    CopyDest,
    CopySrc,
    Present,
    GenericRead,
};

/** @brief Triangle culling mode. */
enum class CullMode : u8 {
    None,
    Front,
    Back,
};

/** @brief Polygon fill mode. */
enum class FillMode : u8 {
    Solid,
    Wireframe,
};

/** @brief Depth / stencil comparison function. */
enum class CompareFunc : u8 {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

/** @brief Texture filter mode. */
enum class FilterMode : u8 {
    Point,
    Linear,
    Anisotropic,
};

/** @brief Texture address mode. */
enum class AddressMode : u8 {
    Wrap,
    Clamp,
    Mirror,
};

/** @brief Colour blend mode for a pipeline's single render target. */
enum class BlendMode : u8 {
    Opaque,     ///< Blending disabled; depth write follows @c PipelineDesc::depth_write.
    AlphaBlend, ///< Straight alpha (src.a, 1-src.a); depth test on, depth write forced off.
};

// ---------------------------------------------------------------------------
// Descriptor structs
// ---------------------------------------------------------------------------

/** @brief Descriptor for creating a GPU buffer. */
struct BufferDesc {
    u32         size         = 0;             ///< Size in bytes.
    BufferUsage usage        = BufferUsage::Vertex;
    bool        cpu_visible  = false;         ///< Upload/readback heap (true) vs default heap.
    const void* initial_data = nullptr;       ///< Optional: data to upload at creation.
};

/** @brief Descriptor for creating a GPU texture. */
struct TextureDesc {
    u32          width       = 1;
    u32          height      = 1;
    u32          mip_levels  = 1;
    Format       format      = Format::R8G8B8A8_UNORM;
    TextureUsage usage       = TextureUsage::ShaderResource;
    const void*  initial_data = nullptr;      ///< Optional: pixel data to upload.
    u32          row_pitch   = 0;             ///< Row pitch of initial_data in bytes.
};

/** @brief Descriptor for registering compiled shader bytecode. */
struct ShaderDesc {
    const u8* bytecode      = nullptr;
    u32       bytecode_size = 0;
};

/** @brief A single element in the input layout. */
struct InputElement {
    const char* semantic       = nullptr;     ///< HLSL semantic name (e.g. "POSITION").
    u32         semantic_index = 0;
    Format      format         = Format::Unknown;
    u32         offset         = 0;           ///< Byte offset within the vertex.
};

/**
 * @brief Describes a pipeline's shader binding interface.
 *
 * The RHI derives the root signature from these counts instead of hardcoding
 * per-renderer variants.  Binding slots follow a fixed convention:
 * - slots `[0, cbv_count)` are root CBVs bound to registers `b0..b(N-1)`
 *   (use @c RhiDevice::set_constant_buffer with the slot index);
 * - slots `[cbv_count, cbv_count + srv_table_count)` are single-SRV
 *   descriptor tables bound to registers `t0..t(M-1)` (use
 *   @c RhiDevice::set_texture with the slot index).
 *
 * Identical layouts (including static-sampler state) share one root
 * signature via an internal cache.
 */
struct BindingLayout {
    u32 cbv_count       = 2; ///< Root CBVs b0..b(N-1). Max kMaxBindingCbvs.
    u32 srv_table_count = 2; ///< Single-SRV tables t0..t(M-1). Max kMaxBindingSrvTables.
};

constexpr u32 kMaxBindingCbvs      = 8;
constexpr u32 kMaxBindingSrvTables = 16;

/** @brief Descriptor for creating a graphics pipeline state. */
struct PipelineDesc {
    ShaderHandle       vertex_shader;
    ShaderHandle       pixel_shader;
    const InputElement* input_elements    = nullptr;
    u32                input_element_count = 0;
    u32                input_stride       = 0;
    CullMode           cull_mode          = CullMode::Back;
    FillMode           fill_mode          = FillMode::Solid;
    bool               depth_enable       = true;
    bool               depth_write        = true;
    CompareFunc        depth_func         = CompareFunc::Less;
    Format             rt_format          = Format::R8G8B8A8_UNORM;
    u32                rt_count           = 1;
    Format             ds_format          = Format::D32_FLOAT;
    bool               depth_bias_enable  = false;
    i32                depth_bias         = 0;
    f32                depth_bias_clamp   = 0.0f;
    f32                slope_scaled_depth_bias = 0.0f;
    FilterMode         sampler_filter     = FilterMode::Point; ///< Static sampler s0 filter mode.
    AddressMode        sampler_address    = AddressMode::Wrap;  ///< Static sampler s0 address mode (u/v/w).
    BlendMode          blend_mode         = BlendMode::Opaque;  ///< Render-target colour blend mode.
    BindingLayout      bindings;                                ///< Shader binding interface (root signature).
};

/** @brief Descriptor for initialising the RHI device. */
struct RhiDeviceDesc {
    void* hwnd               = nullptr;       ///< HWND as `void*`.
    u32   width              = 1280;
    u32   height             = 720;
    bool  enable_debug_layer = true;
    bool  vsync              = true;
};

} // namespace ge::gfx
