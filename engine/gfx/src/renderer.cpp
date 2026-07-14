#include "gfx/renderer.h"
#include "gfx/rhi.h"
#include "gfx/render_graph.h"
#include "gfx/model_loader.h"
#include "gfx/mesh.h"
#include "gfx/material.h"

#include "core/log.h"
#include "core/assert.h"
#include "core/types.h"
#include "core/mat4.h"

#include "platform/file.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ge::gfx {

namespace {

struct Vertex {
    f32 position[3];
    f32 uv[2];
};

struct CubeCB {
    mat4 mvp;
};

// Must match the HLSL cbuffer PerFrame : register(b0) layout exactly.
struct GpuPerFrameCB {
    mat4  view;                    // 64
    mat4  proj;                    // 64
    f32   camera_pos[3];           // 12
    f32   _pad0;                   // 4
    f32   sun_direction[3];        // 12
    f32   _pad1;                   // 4
    f32   sun_color[3];            // 12
    f32   sun_intensity;           // 4
    f32   point_positions[4][4];   // 64 (xyz=pos, w=radius)
    f32   point_colors[4][4];      // 64 (xyz=color, w=intensity)
    i32   point_light_count;       // 4
    f32   shadow_enabled;          // 4
    f32   _pad2[2];                // 8
    mat4  light_view_proj;         // 64
};                                 // Total: 384 bytes
static_assert(sizeof(GpuPerFrameCB) == 384);

// Shared by the shadow and PBR passes (both bind it as register(b1)).
struct GpuPerObjectCB {
    mat4 model;
    mat4 normal_matrix;
};

struct GpuShadowCB {
    mat4 light_view_proj;
};

// Per-texture KHR_texture_transform + UV-set selector (2 float4 = 32 bytes).
struct GpuTexXform {
    f32 offset_scale[4]; // xy = offset, zw = scale
    f32 rot_uvset[4];    // x = rotation (rad), y = uv_set (0/1), zw = pad
};

// Must match PerMaterial cbuffer in pbr_ps.hlsl exactly.
struct GpuPerMaterialCB {
    f32 base_color_factor[4]; // RGBA multiplier
    f32 mr_params[4];         // x=metallic y=roughness z=normal_scale w=occlusion_strength
    f32 emissive_alpha[4];    // xyz=emissive factor, w=alpha_cutoff
    f32 flags0[4];            // x=alpha_mode(0/1/2) y=has_base z=has_mr w=has_normal
    f32 flags1[4];            // x=has_occlusion y=has_emissive zw=pad
    GpuTexXform xform_base;
    GpuTexXform xform_mr;
    GpuTexXform xform_normal;
    GpuTexXform xform_occlusion;
    GpuTexXform xform_emissive;
};                            // Total: 80 + 160 = 240 bytes
static_assert(sizeof(GpuPerMaterialCB) == 240);

/// Normal matrix for a model transform: transpose(inverse(M)). For a pure
/// rotation this equals M; it stays correct under non-uniform scale.
static mat4 normal_matrix_of(const mat4& m) {
    return transpose(inverse(m));
}

constexpr u32 kPerMaterialCBSize = 256; // >= sizeof(GpuPerMaterialCB), 256-aligned
constexpr u32 kShadowMapSize     = 2048;
constexpr u32 kMaxPbrMaterials   = 64;
constexpr u32 kMaxModels         = 16;
constexpr u32 kUploadRingSize    = 1024 * 1024; // per frame in flight; ~4k draws
constexpr u32 kAddressModeCount  = 3;           // Wrap / Clamp / Mirror

std::vector<u8> load_shader_bytecode(const char* filename) {
    char exe_dir[512];
    if (platform::executable_dir(exe_dir, sizeof(exe_dir)) == 0) {
        GE_ERROR("Failed to query the executable directory");
        return {};
    }
    // exe_dir is UTF-8; go through char8_t so non-ASCII paths stay correct.
    std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(exe_dir)));
    path = path / "shaders" / filename;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        GE_ERROR("Failed to open shader file: {}", path.string());
        return {};
    }

    const auto size = static_cast<usize>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<u8> bytecode(size);
    file.read(reinterpret_cast<char*>(bytecode.data()), static_cast<std::streamsize>(size));
    return bytecode;
}

/**
 * Per-frame constant-buffer ring: one persistently-mapped upload buffer per
 * frame in flight, bump-allocated in 256-byte-aligned chunks and reset each
 * frame. Every dynamic CB (per-frame, per-object, shadow, cube) is allocated
 * from here, so each draw gets its own GPU addresses — no shared-buffer
 * last-write-wins hazards. Safe because RhiDevice::begin_frame() waits on
 * the frame slot's fence before the CPU writes that slot's buffer again.
 */
struct UploadRing {
    BufferHandle buffers[kRhiFrameCount];
    u8*          mapped[kRhiFrameCount] = {};
    u32          capacity = 0;
    u32          cursor   = 0;

    bool init(RhiDevice& rhi, u32 cap) {
        capacity = cap;
        for (u32 i = 0; i < kRhiFrameCount; ++i) {
            buffers[i] = rhi.create_buffer({cap, BufferUsage::Constant, true, nullptr});
            if (!buffers[i].is_valid()) return false;
            mapped[i] = static_cast<u8*>(rhi.map_buffer(buffers[i]));
            if (!mapped[i]) return false;
        }
        return true;
    }

    void shutdown(RhiDevice& rhi) {
        for (u32 i = 0; i < kRhiFrameCount; ++i) {
            if (buffers[i].is_valid()) {
                rhi.unmap_buffer(buffers[i]);
                rhi.destroy_buffer(buffers[i]);
                buffers[i] = {};
                mapped[i]  = nullptr;
            }
        }
    }

    void begin_frame() { cursor = 0; }

    /// Allocate a 256-byte-aligned chunk in this frame's buffer.
    /// @return CPU write pointer, or nullptr if the ring is exhausted.
    void* alloc(u32 frame, u32 size, u32& out_offset) {
        const u32 aligned = (size + 255u) & ~255u;
        if (cursor + aligned > capacity) {
            GE_ERROR("Upload ring overflow: {} + {} exceeds {} bytes", cursor, aligned, capacity);
            return nullptr;
        }
        out_offset = cursor;
        cursor += aligned;
        return mapped[frame] + out_offset;
    }
};

} // namespace

/// A loaded model plus its GPU-side per-material constant buffers.
struct LoadedModel {
    Model        model{};
    BufferHandle material_cbs[kMaxPbrMaterials];
    u32          material_cb_count = 0;
    bool         in_use = false;
};

/// One instance submitted for the current frame.
struct DrawItem {
    ModelId model_id;
    mat4    transform;
    u32     object_cb_offset; ///< Ring offset of this item's GpuPerObjectCB.
};

/// (item, submesh) pair used for opaque/transparent partitioning and sorting.
struct SubmeshRef {
    u32 item;
    u32 submesh;
};

struct RendererImpl {
    RhiDevice    rhi;
    RenderGraph  graph;

    // Reference cube (empty-scene fallback + golden-image baseline)
    ShaderHandle   cube_vs;
    ShaderHandle   cube_ps;
    PipelineHandle cube_pipeline;
    BufferHandle   cube_vb;
    BufferHandle   cube_ib;
    TextureHandle  checkerboard_tex;

    // Shadow resources
    ShaderHandle   shadow_vs_shader;
    PipelineHandle shadow_pipeline;
    TextureHandle  shadow_map;
    mat4           cached_light_vp{};
    bool           has_shadows = false;

    // PBR resources.
    // PSO variants: [address mode] x [cull: 0=Back, 1=None] x [blend: 0=Opaque,
    // 1=AlphaBlend]. Each address-mode block is built lazily on the first model
    // that needs it, so models with different sampler wrap modes coexist.
    ShaderHandle   pbr_vs;
    ShaderHandle   pbr_ps;
    PipelineHandle pbr_psos[kAddressModeCount][2][2];
    TextureHandle  default_white_tex;  ///< 1×1 white fallback for missing PBR maps.
    TextureHandle  default_normal_tex; ///< 1×1 flat normal {128,128,255}.
    bool           pbr_shaders_ready = false;

    // Loaded models
    LoadedModel models[kMaxModels];

    // Per-frame submission state
    UploadRing              ring;
    std::vector<DrawItem>   draw_list;        // cleared each begin_frame
    std::vector<SubmeshRef> opaque_refs;      // persistent scratch (capacity reused)
    std::vector<SubmeshRef> transparent_refs; // persistent scratch (capacity reused)

    // Scene state (set by user, read each frame)
    Camera           camera;
    DirectionalLight sun;
    PointLight       point_lights[kMaxPointLights];
    u32              point_light_count = 0;

    f32  total_time = 0.0f;
    f32  clear_color[4] = {0.05f, 0.1f, 0.2f, 1.0f};
    bool vsync = true;

    PipelineHandle pbr_pso(AddressMode address, bool double_sided, bool blend) const {
        return pbr_psos[static_cast<u32>(address)][double_sided ? 1 : 0][blend ? 1 : 0];
    }

    bool create_cube_resources() {
        std::vector<u8> vs_bytecode = load_shader_bytecode("cube_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode("cube_ps.cso");
        if (vs_bytecode.empty() || ps_bytecode.empty()) return false;

        cube_vs = rhi.create_shader({vs_bytecode.data(), static_cast<u32>(vs_bytecode.size())});
        cube_ps = rhi.create_shader({ps_bytecode.data(), static_cast<u32>(ps_bytecode.size())});
        if (!cube_vs.is_valid() || !cube_ps.is_valid()) return false;

        InputElement input_elements[] = {
            {"POSITION", 0, Format::R32G32B32_FLOAT, 0},
            {"TEXCOORD", 0, Format::R32G32_FLOAT,    12},
        };

        PipelineDesc pipe_desc{};
        pipe_desc.vertex_shader       = cube_vs;
        pipe_desc.pixel_shader        = cube_ps;
        pipe_desc.input_elements      = input_elements;
        pipe_desc.input_element_count = 2;
        pipe_desc.input_stride        = sizeof(Vertex);
        pipe_desc.cull_mode           = CullMode::None;
        pipe_desc.depth_enable        = true;
        pipe_desc.depth_func          = CompareFunc::Less;
        pipe_desc.rt_format           = Format::R8G8B8A8_UNORM;
        pipe_desc.ds_format           = Format::D32_FLOAT;
        pipe_desc.bindings            = {2, 2};
        cube_pipeline = rhi.create_pipeline(pipe_desc);
        if (!cube_pipeline.is_valid()) return false;

        constexpr f32 k = 0.5f;
        const Vertex vertices[] = {
            {{-k,-k,-k},{0,1}}, {{-k,-k, k},{1,1}}, {{-k, k, k},{1,0}}, {{-k, k,-k},{0,0}},
            {{ k,-k,-k},{1,1}}, {{ k, k,-k},{1,0}}, {{ k, k, k},{0,0}}, {{ k,-k, k},{0,1}},
            {{-k,-k,-k},{0,1}}, {{ k,-k,-k},{1,1}}, {{ k,-k, k},{1,0}}, {{-k,-k, k},{0,0}},
            {{-k, k,-k},{0,0}}, {{-k, k, k},{0,1}}, {{ k, k, k},{1,1}}, {{ k, k,-k},{1,0}},
            {{-k,-k,-k},{1,1}}, {{-k, k,-k},{1,0}}, {{ k, k,-k},{0,0}}, {{ k,-k,-k},{0,1}},
            {{-k,-k, k},{0,1}}, {{ k,-k, k},{1,1}}, {{ k, k, k},{1,0}}, {{-k, k, k},{0,0}},
        };
        cube_vb = rhi.create_buffer({sizeof(vertices), BufferUsage::Vertex, false, vertices});
        if (!cube_vb.is_valid()) return false;

        u16 indices[36];
        for (u32 face = 0; face < 6; ++face) {
            const u16 base = static_cast<u16>(face * 4);
            const u32 off  = face * 6;
            indices[off+0] = u16(base+0); indices[off+1] = u16(base+1); indices[off+2] = u16(base+2);
            indices[off+3] = u16(base+0); indices[off+4] = u16(base+2); indices[off+5] = u16(base+3);
        }
        cube_ib = rhi.create_buffer({sizeof(indices), BufferUsage::Index, false, indices});
        if (!cube_ib.is_valid()) return false;

        constexpr u32 kTexW = 4, kTexH = 4;
        u8 pixels[kTexH][kTexW][4];
        for (u32 y = 0; y < kTexH; ++y)
            for (u32 x = 0; x < kTexW; ++x) {
                const bool light = (x + y) % 2 == 0;
                pixels[y][x][0] = light ? 230 : 30;
                pixels[y][x][1] = light ? 230 : 30;
                pixels[y][x][2] = light ? 230 : 30;
                pixels[y][x][3] = 255;
            }

        TextureDesc tex_desc{};
        tex_desc.width        = kTexW;
        tex_desc.height       = kTexH;
        tex_desc.format       = Format::R8G8B8A8_UNORM;
        tex_desc.usage        = TextureUsage::ShaderResource;
        tex_desc.initial_data = pixels;
        tex_desc.row_pitch    = kTexW * 4;
        checkerboard_tex = rhi.create_texture(tex_desc);
        return checkerboard_tex.is_valid();
    }

    bool create_shadow_resources() {
        std::vector<u8> vs_bytecode = load_shader_bytecode("shadow_vs.cso");
        if (vs_bytecode.empty()) return false;

        shadow_vs_shader = rhi.create_shader({vs_bytecode.data(), static_cast<u32>(vs_bytecode.size())});
        if (!shadow_vs_shader.is_valid()) return false;

        InputElement input_elements[] = {
            {"POSITION", 0, Format::R32G32B32_FLOAT, 0},
            {"NORMAL",   0, Format::R32G32B32_FLOAT, 12},
            {"TEXCOORD", 0, Format::R32G32_FLOAT,    24},
        };

        PipelineDesc pipe_desc{};
        pipe_desc.vertex_shader            = shadow_vs_shader;
        pipe_desc.input_elements           = input_elements;
        pipe_desc.input_element_count      = 3;
        pipe_desc.input_stride             = sizeof(LitVertex);
        pipe_desc.cull_mode                = CullMode::Back;
        pipe_desc.depth_enable             = true;
        pipe_desc.depth_func               = CompareFunc::Less;
        pipe_desc.rt_count                 = 0;
        pipe_desc.ds_format                = Format::D32_FLOAT;
        pipe_desc.depth_bias_enable        = true;
        pipe_desc.depth_bias               = 1000;
        pipe_desc.slope_scaled_depth_bias  = 1.0f;
        pipe_desc.bindings                 = {2, 2};
        shadow_pipeline = rhi.create_pipeline(pipe_desc);
        if (!shadow_pipeline.is_valid()) return false;

        TextureDesc tex_desc{};
        tex_desc.width  = kShadowMapSize;
        tex_desc.height = kShadowMapSize;
        tex_desc.format = Format::D32_FLOAT;
        tex_desc.usage  = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
        shadow_map = rhi.create_texture(tex_desc);
        if (!shadow_map.is_valid()) return false;

        has_shadows = true;
        return true;
    }

    /// Build the 2x2 (cull x blend) PBR PSO block for one static-sampler
    /// address mode, if not built yet. Blocks persist for the renderer's
    /// lifetime, so models with different wrap modes can coexist.
    bool ensure_pbr_psos(AddressMode address) {
        auto& block = pbr_psos[static_cast<u32>(address)];
        if (block[0][0].is_valid()) return true;

        InputElement input_elements[] = {
            {"POSITION", 0, Format::R32G32B32_FLOAT,    0},
            {"NORMAL",   0, Format::R32G32B32_FLOAT,   12},
            {"TEXCOORD", 0, Format::R32G32_FLOAT,       24},
            {"TEXCOORD", 1, Format::R32G32_FLOAT,       32},
            {"COLOR",    0, Format::R32G32B32A32_FLOAT, 40},
            {"TANGENT",  0, Format::R32G32B32A32_FLOAT, 56},
        };

        const CullMode  culls[2]  = {CullMode::Back, CullMode::None};
        const BlendMode blends[2] = {BlendMode::Opaque, BlendMode::AlphaBlend};

        for (u32 c = 0; c < 2; ++c) {
            for (u32 b = 0; b < 2; ++b) {
                PipelineDesc pipe_desc{};
                pipe_desc.vertex_shader       = pbr_vs;
                pipe_desc.pixel_shader        = pbr_ps;
                pipe_desc.input_elements      = input_elements;
                pipe_desc.input_element_count = 6;
                pipe_desc.input_stride        = sizeof(LitVertex);
                pipe_desc.cull_mode           = culls[c];
                pipe_desc.depth_enable        = true;
                pipe_desc.depth_func          = CompareFunc::Less;
                pipe_desc.rt_format           = Format::R8G8B8A8_UNORM;
                pipe_desc.ds_format           = Format::D32_FLOAT;
                pipe_desc.sampler_filter      = FilterMode::Linear;
                pipe_desc.sampler_address     = address;
                pipe_desc.blend_mode          = blends[b];
                pipe_desc.bindings            = {3, 6};
                block[c][b] = rhi.create_pipeline(pipe_desc);
                if (!block[c][b].is_valid()) return false;
            }
        }
        return true;
    }

    bool create_pbr_resources() {
        std::vector<u8> vs_bytecode = load_shader_bytecode("pbr_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode("pbr_ps.cso");
        if (vs_bytecode.empty() || ps_bytecode.empty()) return false;

        pbr_vs = rhi.create_shader({vs_bytecode.data(), static_cast<u32>(vs_bytecode.size())});
        pbr_ps = rhi.create_shader({ps_bytecode.data(), static_cast<u32>(ps_bytecode.size())});
        if (!pbr_vs.is_valid() || !pbr_ps.is_valid()) return false;

        // Default 1×1 textures for missing PBR maps
        const u8 white_px[4]  = {255, 255, 255, 255};
        const u8 normal_px[4] = {128, 128, 255, 255}; // flat tangent-space normal

        TextureDesc td{};
        td.width = td.height = 1;
        td.format       = Format::R8G8B8A8_UNORM;
        td.usage        = TextureUsage::ShaderResource;
        td.row_pitch    = 4;

        td.initial_data = white_px;
        default_white_tex = rhi.create_texture(td);

        td.initial_data = normal_px;
        default_normal_tex = rhi.create_texture(td);

        if (!default_white_tex.is_valid() || !default_normal_tex.is_valid()) return false;

        pbr_shaders_ready = true;
        return true;
    }

    void destroy_material_cbs(LoadedModel& lm) {
        for (u32 i = 0; i < lm.material_cb_count; ++i) {
            if (lm.material_cbs[i].is_valid()) {
                rhi.destroy_buffer(lm.material_cbs[i]);
                lm.material_cbs[i] = {};
            }
        }
        lm.material_cb_count = 0;
    }

    bool create_material_cbs(LoadedModel& lm) {
        const u32 count = (lm.model.material_count < kMaxPbrMaterials)
                          ? lm.model.material_count : kMaxPbrMaterials;
        lm.material_cb_count = count;

        for (u32 i = 0; i < count; ++i) {
            lm.material_cbs[i] = rhi.create_buffer(
                {kPerMaterialCBSize, BufferUsage::Constant, true, nullptr});
            if (!lm.material_cbs[i].is_valid()) return false;

            const Material& mat = lm.model.materials[i];

            auto fill_xform = [](GpuTexXform& g, const TexTransform& x) {
                g.offset_scale[0] = x.offset[0]; g.offset_scale[1] = x.offset[1];
                g.offset_scale[2] = x.scale[0];  g.offset_scale[3] = x.scale[1];
                g.rot_uvset[0]    = x.rotation;
                g.rot_uvset[1]    = static_cast<f32>(x.uv_set);
                g.rot_uvset[2]    = 0.0f; g.rot_uvset[3] = 0.0f;
            };

            GpuPerMaterialCB cb{};
            cb.base_color_factor[0] = mat.base_color[0];
            cb.base_color_factor[1] = mat.base_color[1];
            cb.base_color_factor[2] = mat.base_color[2];
            cb.base_color_factor[3] = mat.base_color[3];
            cb.mr_params[0]         = mat.metallic_factor;
            cb.mr_params[1]         = mat.roughness_factor;
            cb.mr_params[2]         = mat.normal_scale;
            cb.mr_params[3]         = mat.occlusion_strength;
            cb.emissive_alpha[0]    = mat.emissive_factor[0];
            cb.emissive_alpha[1]    = mat.emissive_factor[1];
            cb.emissive_alpha[2]    = mat.emissive_factor[2];
            cb.emissive_alpha[3]    = mat.alpha_cutoff;
            cb.flags0[0]            = static_cast<f32>(static_cast<u32>(mat.alpha_mode));
            cb.flags0[1]            = mat.has_albedo_texture ? 1.0f : 0.0f;
            cb.flags0[2]            = mat.has_metallic_roughness_texture ? 1.0f : 0.0f;
            cb.flags0[3]            = mat.has_normal_texture ? 1.0f : 0.0f;
            cb.flags1[0]            = mat.has_occlusion_texture ? 1.0f : 0.0f;
            cb.flags1[1]            = mat.has_emissive_texture ? 1.0f : 0.0f;
            fill_xform(cb.xform_base,      mat.albedo_xform);
            fill_xform(cb.xform_mr,        mat.mr_xform);
            fill_xform(cb.xform_normal,    mat.normal_xform);
            fill_xform(cb.xform_occlusion, mat.occlusion_xform);
            fill_xform(cb.xform_emissive,  mat.emissive_xform);

            void* mapped = rhi.map_buffer(lm.material_cbs[i]);
            if (mapped) std::memcpy(mapped, &cb, sizeof(cb));
            rhi.unmap_buffer(lm.material_cbs[i]);
        }
        return true;
    }

    /// Write the per-frame camera + lighting constant block.
    void fill_frame_cb(GpuPerFrameCB& frame_cb, f32 aspect) const {
        frame_cb.view = camera.view_matrix();
        frame_cb.proj = camera.proj_matrix(aspect);
        frame_cb.camera_pos[0] = camera.position.x;
        frame_cb.camera_pos[1] = camera.position.y;
        frame_cb.camera_pos[2] = camera.position.z;

        const vec3 sun_dir = normalize(sun.direction);
        frame_cb.sun_direction[0] = sun_dir.x;
        frame_cb.sun_direction[1] = sun_dir.y;
        frame_cb.sun_direction[2] = sun_dir.z;
        frame_cb.sun_color[0]     = sun.color.x;
        frame_cb.sun_color[1]     = sun.color.y;
        frame_cb.sun_color[2]     = sun.color.z;
        frame_cb.sun_intensity    = sun.intensity;

        frame_cb.point_light_count = static_cast<i32>(point_light_count);
        for (u32 i = 0; i < point_light_count && i < kMaxPointLights; ++i) {
            const PointLight& pl = point_lights[i];
            frame_cb.point_positions[i][0] = pl.position.x;
            frame_cb.point_positions[i][1] = pl.position.y;
            frame_cb.point_positions[i][2] = pl.position.z;
            frame_cb.point_positions[i][3] = pl.radius;
            frame_cb.point_colors[i][0]    = pl.color.x;
            frame_cb.point_colors[i][1]    = pl.color.y;
            frame_cb.point_colors[i][2]    = pl.color.z;
            frame_cb.point_colors[i][3]    = pl.intensity;
        }

        frame_cb.shadow_enabled  = has_shadows ? 1.0f : 0.0f;
        frame_cb.light_view_proj = cached_light_vp;
    }
};

// -- Pass callbacks ---------------------------------------------------------

static void cube_pass_execute(RhiDevice& rhi, void* user_data) {
    auto* impl = static_cast<RendererImpl*>(user_data);

    TextureHandle bb = rhi.back_buffer();
    TextureHandle db = rhi.depth_buffer();

    rhi.clear_render_target(bb, impl->clear_color);
    rhi.clear_depth(db, 1.0f);
    rhi.set_render_targets(&bb, 1, db);

    constexpr f32 kFovY = 1.0471975512f;
    const f32 angle_x = impl->total_time * 0.5f;
    const f32 angle_y = impl->total_time * 0.8f;
    const mat4 model  = mat4_rotation_x(angle_x) * mat4_rotation_y(angle_y);
    const mat4 view   = mat4_look_at_lh({0, 1.5f, -3.0f}, {0, 0, 0}, {0, 1.0f, 0});
    const f32  aspect = static_cast<f32>(rhi.width()) / static_cast<f32>(rhi.height());
    const mat4 proj   = mat4_perspective_lh(kFovY, aspect, 0.1f, 100.0f);

    CubeCB cb{};
    cb.mvp = model * view * proj;

    const u32 fi = rhi.frame_index();
    u32 cb_offset = 0;
    if (void* mapped = impl->ring.alloc(fi, sizeof(cb), cb_offset))
        std::memcpy(mapped, &cb, sizeof(cb));

    const f32 w = static_cast<f32>(rhi.width());
    const f32 h = static_cast<f32>(rhi.height());

    rhi.set_pipeline(impl->cube_pipeline);
    rhi.set_constant_buffer(0, impl->ring.buffers[fi], cb_offset);
    rhi.set_texture(2, impl->checkerboard_tex);
    rhi.set_viewport(0, 0, w, h, 0, 1);
    rhi.set_scissor(0, 0, rhi.width(), rhi.height());
    rhi.set_vertex_buffer(impl->cube_vb, sizeof(Vertex));
    rhi.set_index_buffer(impl->cube_ib, Format::R16_UINT);
    rhi.draw_indexed(36);
}

static void shadow_pass_execute(RhiDevice& rhi, void* user_data) {
    auto* impl = static_cast<RendererImpl*>(user_data);

    rhi.clear_depth(impl->shadow_map, 1.0f);
    rhi.set_render_targets(nullptr, 0, impl->shadow_map);

    vec3 sun_dir = normalize(impl->sun.direction);
    vec3 light_pos = sun_dir * -20.0f;
    mat4 light_view = mat4_look_at_lh(light_pos, {0, 0, 0}, {0, 1, 0});
    mat4 light_proj = mat4_ortho_lh(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 50.0f);
    impl->cached_light_vp = light_view * light_proj;

    const u32 fi = rhi.frame_index();

    GpuShadowCB shadow_cb{};
    shadow_cb.light_view_proj = impl->cached_light_vp;
    u32 shadow_cb_offset = 0;
    if (void* mapped = impl->ring.alloc(fi, sizeof(shadow_cb), shadow_cb_offset))
        std::memcpy(mapped, &shadow_cb, sizeof(shadow_cb));

    const f32 sz = static_cast<f32>(kShadowMapSize);

    rhi.set_pipeline(impl->shadow_pipeline);
    rhi.set_constant_buffer(0, impl->ring.buffers[fi], shadow_cb_offset);
    rhi.set_viewport(0, 0, sz, sz, 0, 1);
    rhi.set_scissor(0, 0, kShadowMapSize, kShadowMapSize);

    ModelId bound_model = kInvalidModelId;
    for (const DrawItem& item : impl->draw_list) {
        const LoadedModel& lm = impl->models[item.model_id];

        if (item.model_id != bound_model) {
            rhi.set_vertex_buffer(lm.model.mesh.vertex_buffer, lm.model.mesh.vertex_stride);
            rhi.set_index_buffer(lm.model.mesh.index_buffer, Format::R32_UINT);
            bound_model = item.model_id;
        }

        rhi.set_constant_buffer(1, impl->ring.buffers[fi], item.object_cb_offset);

        const Mesh& mesh = lm.model.mesh;
        for (u32 si = 0; si < mesh.sub_mesh_count; ++si) {
            const SubMesh& sub = mesh.sub_meshes[si];
            rhi.draw_indexed(sub.index_count, sub.index_offset, sub.base_vertex);
        }
    }
}

static void pbr_pass_execute(RhiDevice& rhi, void* user_data) {
    auto* impl = static_cast<RendererImpl*>(user_data);

    TextureHandle bb = rhi.back_buffer();
    TextureHandle db = rhi.depth_buffer();

    rhi.clear_render_target(bb, impl->clear_color);
    rhi.clear_depth(db, 1.0f);
    rhi.set_render_targets(&bb, 1, db);

    const f32 aspect = static_cast<f32>(rhi.width()) / static_cast<f32>(rhi.height());
    const u32 fi     = rhi.frame_index();

    GpuPerFrameCB frame_cb{};
    impl->fill_frame_cb(frame_cb, aspect);
    u32 frame_cb_offset = 0;
    if (void* mapped = impl->ring.alloc(fi, sizeof(frame_cb), frame_cb_offset))
        std::memcpy(mapped, &frame_cb, sizeof(frame_cb));

    const f32 w = static_cast<f32>(rhi.width());
    const f32 h = static_cast<f32>(rhi.height());

    // IA / RS state is independent of the PSO and persists across pipeline changes.
    rhi.set_viewport(0, 0, w, h, 0, 1);
    rhi.set_scissor(0, 0, rhi.width(), rhi.height());

    // Partition (item, submesh) pairs into opaque/masked (drawn first) and
    // blended. Scratch arrays live on the impl so their capacity is reused
    // across frames instead of reallocating per frame.
    auto& opaque      = impl->opaque_refs;
    auto& transparent = impl->transparent_refs;
    opaque.clear();
    transparent.clear();

    for (u32 ii = 0; ii < impl->draw_list.size(); ++ii) {
        const LoadedModel& lm = impl->models[impl->draw_list[ii].model_id];
        for (u32 si = 0; si < lm.model.mesh.sub_mesh_count; ++si) {
            const u32 mat_idx = (lm.model.mesh.sub_meshes[si].material_index < lm.material_cb_count)
                                ? lm.model.mesh.sub_meshes[si].material_index : 0;
            if (lm.model.materials[mat_idx].alpha_mode == AlphaMode::Blend)
                transparent.push_back({ii, si});
            else
                opaque.push_back({ii, si});
        }
    }

    // Sort blended submeshes far→near by view-space centroid depth.
    if (transparent.size() > 1) {
        const mat4 view = impl->camera.view_matrix();
        auto view_z = [&](const SubmeshRef& ref) {
            const DrawItem& item = impl->draw_list[ref.item];
            const LoadedModel& lm = impl->models[item.model_id];
            const f32* c = lm.model.mesh.sub_meshes[ref.submesh].centroid;
            vec4 p = vec4{c[0], c[1], c[2], 1.0f} * (item.transform * view);
            return p.z;
        };
        std::sort(transparent.begin(), transparent.end(),
                  [&](const SubmeshRef& a, const SubmeshRef& b) { return view_z(a) > view_z(b); });
    }

    // Track bound state to skip redundant re-binds between consecutive draws.
    ModelId        bound_model = kInvalidModelId;
    PipelineHandle bound_pso{};

    auto draw_ref = [&](const SubmeshRef& ref) {
        const DrawItem&    item = impl->draw_list[ref.item];
        const LoadedModel& lm   = impl->models[item.model_id];
        const Mesh&        mesh = lm.model.mesh;
        const SubMesh&     sub  = mesh.sub_meshes[ref.submesh];

        const u32 mat_idx = (sub.material_index < lm.material_cb_count)
                            ? sub.material_index : 0;
        const Material& mat = lm.model.materials[mat_idx];
        const bool blend = (mat.alpha_mode == AlphaMode::Blend);

        if (item.model_id != bound_model) {
            rhi.set_vertex_buffer(mesh.vertex_buffer, mesh.vertex_stride);
            rhi.set_index_buffer(mesh.index_buffer, Format::R32_UINT);
            bound_model = item.model_id;
        }

        const PipelineHandle pso =
            impl->pbr_pso(lm.model.sampler_address, mat.double_sided, blend);
        if (pso != bound_pso) {
            rhi.set_pipeline(pso);
            bound_pso = pso;
        }

        // Root arguments are invalidated by root-signature changes, and the
        // per-object/material CBVs vary per draw — bind them every draw.
        rhi.set_constant_buffer(0, impl->ring.buffers[fi], frame_cb_offset);
        rhi.set_constant_buffer(1, impl->ring.buffers[fi], item.object_cb_offset);
        rhi.set_constant_buffer(2, lm.material_cbs[mat_idx]);

        rhi.set_texture(3, mat.has_albedo_texture
                           ? mat.albedo_texture : impl->default_white_tex);
        rhi.set_texture(4, mat.has_metallic_roughness_texture
                           ? mat.metallic_roughness_texture : impl->default_white_tex);
        rhi.set_texture(5, mat.has_normal_texture
                           ? mat.normal_texture : impl->default_normal_tex);
        rhi.set_texture(6, mat.has_occlusion_texture
                           ? mat.occlusion_texture : impl->default_white_tex);
        rhi.set_texture(7, mat.has_emissive_texture
                           ? mat.emissive_texture : impl->default_white_tex);
        rhi.set_texture(8, impl->has_shadows ? impl->shadow_map : impl->default_white_tex);

        rhi.draw_indexed(sub.index_count, sub.index_offset, sub.base_vertex);
    };

    for (const SubmeshRef& ref : opaque)      draw_ref(ref);
    for (const SubmeshRef& ref : transparent) draw_ref(ref);
}

// -- Renderer implementation ------------------------------------------------

bool Renderer::init(const RendererDesc& desc) {
    auto* impl = new RendererImpl();
    impl_ = impl;
    impl->vsync = desc.vsync;

    RhiDeviceDesc rhi_desc{};
    rhi_desc.hwnd               = desc.hwnd;
    rhi_desc.width              = desc.width;
    rhi_desc.height             = desc.height;
    rhi_desc.enable_debug_layer = desc.enable_debug_layer;
    rhi_desc.vsync              = desc.vsync;

    if (!impl->rhi.init(rhi_desc)) {
        shutdown();
        return false;
    }

    if (!impl->ring.init(impl->rhi, kUploadRingSize)) {
        GE_ERROR("Failed to create the per-frame upload ring");
        shutdown();
        return false;
    }

    if (!impl->create_cube_resources()) {
        shutdown();
        return false;
    }

    if (!impl->create_shadow_resources()) {
        GE_WARN("Shadow resources creation failed (shadow shader may not be compiled yet)");
    }

    if (!impl->create_pbr_resources()) {
        GE_WARN("PBR pipeline creation failed (pbr shaders may not be compiled yet)");
    }

    GE_INFO("Renderer initialized ({}x{})", desc.width, desc.height);
    return true;
}

void Renderer::shutdown() {
    auto* impl = static_cast<RendererImpl*>(impl_);
    if (!impl) return;

    // In-flight frames may still reference the resources destroyed below.
    impl->rhi.wait_idle();

    for (u32 i = 0; i < kMaxModels; ++i) {
        if (impl->models[i].in_use) {
            impl->destroy_material_cbs(impl->models[i]);
            destroy_model(impl->models[i].model, impl->rhi);
            impl->models[i].in_use = false;
        }
    }

    impl->ring.shutdown(impl->rhi);
    if (impl->default_white_tex.is_valid())  impl->rhi.destroy_texture(impl->default_white_tex);
    if (impl->default_normal_tex.is_valid()) impl->rhi.destroy_texture(impl->default_normal_tex);

    impl->rhi.shutdown();
    delete impl;
    impl_ = nullptr;
}

ModelId Renderer::load_model(const char* path, ge::Allocator alloc) {
    auto* impl = static_cast<RendererImpl*>(impl_);

    if (!impl->pbr_shaders_ready) {
        GE_ERROR("load_model: PBR pipeline unavailable, cannot render models");
        return kInvalidModelId;
    }

    ModelId id = kInvalidModelId;
    for (u32 i = 0; i < kMaxModels; ++i) {
        if (!impl->models[i].in_use) { id = i; break; }
    }
    if (id == kInvalidModelId) {
        GE_ERROR("load_model: all {} model slots in use", kMaxModels);
        return kInvalidModelId;
    }

    LoadedModel& lm = impl->models[id];
    lm = {};
    if (!load_model_gltf(path, impl->rhi, alloc, lm.model)) {
        return kInvalidModelId;
    }

    if (!impl->ensure_pbr_psos(lm.model.sampler_address)) {
        GE_ERROR("load_model: failed to build PBR PSOs for the model's sampler mode");
        destroy_model(lm.model, impl->rhi);
        return kInvalidModelId;
    }

    if (!impl->create_material_cbs(lm)) {
        GE_ERROR("load_model: failed to create per-material constant buffers");
        impl->destroy_material_cbs(lm);
        destroy_model(lm.model, impl->rhi);
        return kInvalidModelId;
    }

    lm.in_use = true;
    return id;
}

void Renderer::unload_model(ModelId id) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    if (!impl || id >= kMaxModels || !impl->models[id].in_use) return;

    // In-flight frames may still reference the model's GPU resources.
    impl->rhi.wait_idle();

    impl->destroy_material_cbs(impl->models[id]);
    destroy_model(impl->models[id].model, impl->rhi);
    impl->models[id].in_use = false;
}

void Renderer::submit(ModelId id, const mat4& transform) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    GE_ASSERT(id < kMaxModels && impl->models[id].in_use); // invalid or unloaded ModelId
    if (id >= kMaxModels || !impl->models[id].in_use) return;

    impl->draw_list.push_back({id, transform, 0});
}

void Renderer::set_camera(const Camera& cam) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->camera = cam;
}

void Renderer::set_sun(const DirectionalLight& sun) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->sun = sun;
}

void Renderer::set_point_lights(const PointLight* lights, std::uint32_t count) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->point_light_count = (count > kMaxPointLights) ? kMaxPointLights : count;
    for (u32 i = 0; i < impl->point_light_count; ++i)
        impl->point_lights[i] = lights[i];
}

void Renderer::begin_frame(float dt) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->total_time += dt;

    impl->rhi.begin_frame();
    impl->ring.begin_frame();
    impl->draw_list.clear();
}

void Renderer::end_frame() {
    auto* impl = static_cast<RendererImpl*>(impl_);

    const u32 fi = impl->rhi.frame_index();
    const bool draw_models = !impl->draw_list.empty() && impl->pbr_shaders_ready;

    // One per-object CB per submitted instance, shared by the shadow and PBR
    // passes (both bind it as b1 with the same layout).
    if (draw_models) {
        for (DrawItem& item : impl->draw_list) {
            GpuPerObjectCB obj_cb{};
            obj_cb.model         = item.transform;
            obj_cb.normal_matrix = normal_matrix_of(item.transform);
            if (void* mapped = impl->ring.alloc(fi, sizeof(obj_cb), item.object_cb_offset))
                std::memcpy(mapped, &obj_cb, sizeof(obj_cb));
        }
    }

    TextureHandle bb = impl->rhi.back_buffer();
    TextureHandle db = impl->rhi.depth_buffer();

    impl->graph.begin();
    impl->graph.import(bb, ResourceState::Present);
    impl->graph.import(db, ResourceState::DepthWrite);

    if (draw_models) {
        if (impl->has_shadows) {
            impl->graph.import(impl->shadow_map, ResourceState::DepthWrite);
            impl->graph.add_pass("shadow", shadow_pass_execute, impl);
            impl->graph.pass_write(impl->shadow_map, ResourceState::DepthWrite);
        }

        impl->graph.add_pass("pbr", pbr_pass_execute, impl);
        impl->graph.pass_write(bb, ResourceState::RenderTarget);
        impl->graph.pass_write(db, ResourceState::DepthWrite);
        if (impl->has_shadows)
            impl->graph.pass_read(impl->shadow_map, ResourceState::PixelShaderResource);
    } else {
        impl->graph.add_pass("cube", cube_pass_execute, impl);
        impl->graph.pass_write(bb, ResourceState::RenderTarget);
        impl->graph.pass_write(db, ResourceState::DepthWrite);
    }

    impl->graph.set_final_state(bb, ResourceState::Present);
    if (draw_models && impl->has_shadows)
        impl->graph.set_final_state(impl->shadow_map, ResourceState::DepthWrite);

    impl->graph.compile();
    impl->graph.execute(impl->rhi);

    impl->rhi.end_frame();
    impl->rhi.present(impl->vsync);
}

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->rhi.resize(width, height);
}

void Renderer::set_clear_color(float r, float g, float b, float a) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->clear_color[0] = r;
    impl->clear_color[1] = g;
    impl->clear_color[2] = b;
    impl->clear_color[3] = a;
}

std::uint32_t Renderer::debug_error_count() const {
    auto* impl = static_cast<RendererImpl*>(impl_);
    if (!impl) return 0;
    return impl->rhi.debug_error_count();
}

bool Renderer::capture_back_buffer(std::vector<std::uint8_t>& out_rgba, std::uint32_t& out_width,
                                   std::uint32_t& out_height) const {
    auto* impl = static_cast<RendererImpl*>(impl_);
    if (!impl) return false;
    return impl->rhi.capture_back_buffer(out_rgba, out_width, out_height);
}

} // namespace ge::gfx
