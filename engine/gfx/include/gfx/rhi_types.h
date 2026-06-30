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
 * @brief Root-signature layout variant.
 *
 * Default — 2 CBVs (b0,b1) + 2 SRV tables (t0,t1).
 * PBR     — 3 CBVs (b0,b1,b2) + 6 SRV tables (t0–t5).
 */
enum class RootLayout : u8 {
    Default,
    PBR,
};

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
    RootLayout         root_layout        = RootLayout::Default; ///< Root-signature variant.
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
