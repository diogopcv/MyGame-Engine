#include "gfx/renderer.h"
#include "gfx/rhi.h"
#include "gfx/render_graph.h"
#include "gfx/model_loader.h"
#include "gfx/mesh.h"
#include "gfx/material.h"

#include "core/log.h"
#include "core/types.h"
#include "core/mat4.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

constexpr u32 kPerFrameCBSize    = 512; // >= sizeof(GpuPerFrameCB), 256-aligned
constexpr u32 kPerObjectCBSize   = 256;
constexpr u32 kPerMaterialCBSize = 256; // >= sizeof(GpuPerMaterialCB), 256-aligned
constexpr u32 kCubeCBSize        = 256;
constexpr u32 kShadowCBSize      = 256;
constexpr u32 kShadowMapSize     = 2048;
constexpr u32 kFrameCount        = 2;
constexpr u32 kMaxPbrMaterials   = 64;

std::vector<u8> load_shader_bytecode(const wchar_t* filename) {
    wchar_t exe_path[MAX_PATH];
    ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path path = std::filesystem::path(exe_path).parent_path() / "shaders" / filename;

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

} // namespace

struct RendererImpl {
    RhiDevice    rhi;
    RenderGraph  graph;

    // Cube resources (Phase 1 compat)
    ShaderHandle   cube_vs;
    ShaderHandle   cube_ps;
    PipelineHandle cube_pipeline;
    BufferHandle   cube_vb;
    BufferHandle   cube_ib;
    TextureHandle  checkerboard_tex;
    BufferHandle   cube_cb[kFrameCount];

    // Model resources
    ShaderHandle   model_vs;
    ShaderHandle   model_ps;
    PipelineHandle model_pipeline;
    Model          loaded_model{};
    BufferHandle   per_frame_cb[kFrameCount];
    BufferHandle   per_object_cb[kFrameCount];
    bool           has_model = false;

    // Shadow resources
    ShaderHandle   shadow_vs_shader;
    PipelineHandle shadow_pipeline;
    TextureHandle  shadow_map;
    BufferHandle   shadow_cb[kFrameCount];
    mat4           cached_light_vp{};
    bool           has_shadows = false;

    // PBR resources
    ShaderHandle   pbr_vs;
    ShaderHandle   pbr_ps;
    // PSO variants: [cull: 0=Back, 1=None] x [blend: 0=Opaque, 1=AlphaBlend].
    PipelineHandle pbr_psos[2][2];
    AddressMode    pbr_sampler_address = AddressMode::Wrap;
    TextureHandle  default_white_tex;  ///< 1×1 white fallback for missing PBR maps.
    TextureHandle  default_normal_tex; ///< 1×1 flat normal {128,128,255}.
    BufferHandle   per_material_cbs[kMaxPbrMaterials];
    u32            per_material_cb_count = 0;
    bool           has_pbr_model = false;

    bool pbr_ready() const { return pbr_psos[0][0].is_valid(); }
    PipelineHandle pbr_pso(bool double_sided, bool blend) const {
        return pbr_psos[double_sided ? 1 : 0][blend ? 1 : 0];
    }

    // Scene state (set by user, read each frame)
    Camera           camera;
    DirectionalLight sun;
    PointLight       point_lights[kMaxPointLights];
    u32              point_light_count = 0;

    mat4 model_transform = mat4_identity();

    f32  total_time = 0.0f;
    f32  clear_color[4] = {0.05f, 0.1f, 0.2f, 1.0f};
    bool vsync = true;

    bool create_cube_resources() {
        std::vector<u8> vs_bytecode = load_shader_bytecode(L"cube_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode(L"cube_ps.cso");
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
        cube_vb = rhi.create_buffer({sizeof(vertices), BufferUsage::Vertex, true, vertices});
        if (!cube_vb.is_valid()) return false;

        u16 indices[36];
        for (u32 face = 0; face < 6; ++face) {
            const u16 base = static_cast<u16>(face * 4);
            const u32 off  = face * 6;
            indices[off+0] = u16(base+0); indices[off+1] = u16(base+1); indices[off+2] = u16(base+2);
            indices[off+3] = u16(base+0); indices[off+4] = u16(base+2); indices[off+5] = u16(base+3);
        }
        cube_ib = rhi.create_buffer({sizeof(indices), BufferUsage::Index, true, indices});
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
        if (!checkerboard_tex.is_valid()) return false;

        for (u32 i = 0; i < kFrameCount; ++i) {
            cube_cb[i] = rhi.create_buffer({kCubeCBSize, BufferUsage::Constant, true, nullptr});
            if (!cube_cb[i].is_valid()) return false;
            rhi.map_buffer(cube_cb[i]);
        }

        return true;
    }

    bool create_model_pipeline() {
        std::vector<u8> vs_bytecode = load_shader_bytecode(L"model_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode(L"model_ps.cso");
        if (vs_bytecode.empty() || ps_bytecode.empty()) return false;

        model_vs = rhi.create_shader({vs_bytecode.data(), static_cast<u32>(vs_bytecode.size())});
        model_ps = rhi.create_shader({ps_bytecode.data(), static_cast<u32>(ps_bytecode.size())});
        if (!model_vs.is_valid() || !model_ps.is_valid()) return false;

        InputElement input_elements[] = {
            {"POSITION", 0, Format::R32G32B32_FLOAT, 0},
            {"NORMAL",   0, Format::R32G32B32_FLOAT, 12},
            {"TEXCOORD", 0, Format::R32G32_FLOAT,    24},
        };

        PipelineDesc pipe_desc{};
        pipe_desc.vertex_shader       = model_vs;
        pipe_desc.pixel_shader        = model_ps;
        pipe_desc.input_elements      = input_elements;
        pipe_desc.input_element_count = 3;
        pipe_desc.input_stride        = sizeof(LitVertex);
        pipe_desc.cull_mode           = CullMode::Back;
        pipe_desc.depth_enable        = true;
        pipe_desc.depth_func          = CompareFunc::Less;
        pipe_desc.rt_format           = Format::R8G8B8A8_UNORM;
        pipe_desc.ds_format           = Format::D32_FLOAT;
        pipe_desc.sampler_filter      = FilterMode::Linear;
        model_pipeline = rhi.create_pipeline(pipe_desc);
        if (!model_pipeline.is_valid()) return false;

        for (u32 i = 0; i < kFrameCount; ++i) {
            per_frame_cb[i]  = rhi.create_buffer({kPerFrameCBSize, BufferUsage::Constant, true, nullptr});
            per_object_cb[i] = rhi.create_buffer({kPerObjectCBSize, BufferUsage::Constant, true, nullptr});
            if (!per_frame_cb[i].is_valid() || !per_object_cb[i].is_valid()) return false;
            rhi.map_buffer(per_frame_cb[i]);
            rhi.map_buffer(per_object_cb[i]);
        }

        return true;
    }

    bool create_shadow_resources() {
        std::vector<u8> vs_bytecode = load_shader_bytecode(L"shadow_vs.cso");
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
        shadow_pipeline = rhi.create_pipeline(pipe_desc);
        if (!shadow_pipeline.is_valid()) return false;

        TextureDesc tex_desc{};
        tex_desc.width  = kShadowMapSize;
        tex_desc.height = kShadowMapSize;
        tex_desc.format = Format::D32_FLOAT;
        tex_desc.usage  = TextureUsage::DepthStencil | TextureUsage::ShaderResource;
        shadow_map = rhi.create_texture(tex_desc);
        if (!shadow_map.is_valid()) return false;

        for (u32 i = 0; i < kFrameCount; ++i) {
            shadow_cb[i] = rhi.create_buffer({kShadowCBSize, BufferUsage::Constant, true, nullptr});
            if (!shadow_cb[i].is_valid()) return false;
            rhi.map_buffer(shadow_cb[i]);
        }

        has_shadows = true;
        return true;
    }

    /// Build the 2x2 PBR PSO variant matrix for a given static-sampler address
    /// mode. Destroys any previously built variants first (load-time rebuild).
    bool build_pbr_psos(AddressMode address) {
        for (auto& row : pbr_psos)
            for (auto& p : row)
                if (p.is_valid()) { rhi.destroy_pipeline(p); p = {}; }

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
                pipe_desc.root_layout         = RootLayout::PBR;
                pbr_psos[c][b] = rhi.create_pipeline(pipe_desc);
                if (!pbr_psos[c][b].is_valid()) return false;
            }
        }
        pbr_sampler_address = address;
        return true;
    }

    bool create_pbr_pipeline() {
        std::vector<u8> vs_bytecode = load_shader_bytecode(L"pbr_vs.cso");
        std::vector<u8> ps_bytecode = load_shader_bytecode(L"pbr_ps.cso");
        if (vs_bytecode.empty() || ps_bytecode.empty()) return false;

        pbr_vs = rhi.create_shader({vs_bytecode.data(), static_cast<u32>(vs_bytecode.size())});
        pbr_ps = rhi.create_shader({ps_bytecode.data(), static_cast<u32>(ps_bytecode.size())});
        if (!pbr_vs.is_valid() || !pbr_ps.is_valid()) return false;

        if (!build_pbr_psos(AddressMode::Wrap)) return false;

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

        return default_white_tex.is_valid() && default_normal_tex.is_valid();
    }

    void destroy_pbr_material_cbs() {
        for (u32 i = 0; i < per_material_cb_count; ++i) {
            if (per_material_cbs[i].is_valid()) {
                rhi.unmap_buffer(per_material_cbs[i]);
                rhi.destroy_buffer(per_material_cbs[i]);
                per_material_cbs[i] = {};
            }
        }
        per_material_cb_count = 0;
        has_pbr_model = false;
    }

    bool create_pbr_material_cbs() {
        destroy_pbr_material_cbs();
        const u32 count = (loaded_model.material_count < kMaxPbrMaterials)
                          ? loaded_model.material_count : kMaxPbrMaterials;
        per_material_cb_count = count;

        for (u32 i = 0; i < count; ++i) {
            per_material_cbs[i] = rhi.create_buffer(
                {kPerMaterialCBSize, BufferUsage::Constant, true, nullptr});
            if (!per_material_cbs[i].is_valid()) return false;

            const Material& mat = loaded_model.materials[i];

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

            void* mapped = rhi.map_buffer(per_material_cbs[i]);
            if (mapped) std::memcpy(mapped, &cb, sizeof(cb));
        }
        return true;
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

    u32 fi = rhi.back_buffer().index % kFrameCount;
    void* mapped = rhi.map_buffer(impl->cube_cb[fi]);
    if (mapped) std::memcpy(mapped, &cb, sizeof(cb));

    const f32 w = static_cast<f32>(rhi.width());
    const f32 h = static_cast<f32>(rhi.height());

    rhi.set_pipeline(impl->cube_pipeline);
    rhi.set_constant_buffer(0, impl->cube_cb[fi]);
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

    u32 fi = rhi.back_buffer().index % kFrameCount;

    GpuShadowCB shadow_cb{};
    shadow_cb.light_view_proj = impl->cached_light_vp;
    void* mapped = rhi.map_buffer(impl->shadow_cb[fi]);
    if (mapped) std::memcpy(mapped, &shadow_cb, sizeof(shadow_cb));

    GpuPerObjectCB obj_cb{};
    obj_cb.model         = impl->model_transform;
    obj_cb.normal_matrix = impl->model_transform; // pure rotation: inverse-transpose = itself
    void* obj_mapped = rhi.map_buffer(impl->per_object_cb[fi]);
    if (obj_mapped) std::memcpy(obj_mapped, &obj_cb, sizeof(obj_cb));

    const f32 sz = static_cast<f32>(kShadowMapSize);

    rhi.set_pipeline(impl->shadow_pipeline);
    rhi.set_constant_buffer(0, impl->shadow_cb[fi]);
    rhi.set_constant_buffer(1, impl->per_object_cb[fi]);
    rhi.set_viewport(0, 0, sz, sz, 0, 1);
    rhi.set_scissor(0, 0, kShadowMapSize, kShadowMapSize);
    rhi.set_vertex_buffer(impl->loaded_model.mesh.vertex_buffer, sizeof(LitVertex));
    rhi.set_index_buffer(impl->loaded_model.mesh.index_buffer, Format::R32_UINT);

    const Mesh& mesh = impl->loaded_model.mesh;
    for (u32 si = 0; si < mesh.sub_mesh_count; ++si) {
        const SubMesh& sub = mesh.sub_meshes[si];
        rhi.draw_indexed(sub.index_count, sub.index_offset, sub.base_vertex);
    }
}

static void model_pass_execute(RhiDevice& rhi, void* user_data) {
    auto* impl = static_cast<RendererImpl*>(user_data);

    TextureHandle bb = rhi.back_buffer();
    TextureHandle db = rhi.depth_buffer();

    rhi.clear_render_target(bb, impl->clear_color);
    rhi.clear_depth(db, 1.0f);
    rhi.set_render_targets(&bb, 1, db);

    const f32 aspect = static_cast<f32>(rhi.width()) / static_cast<f32>(rhi.height());

    u32 fi = rhi.back_buffer().index % kFrameCount;

    // Fill per-frame CB with camera + lighting data
    GpuPerFrameCB frame_cb{};
    frame_cb.view = impl->camera.view_matrix();
    frame_cb.proj = impl->camera.proj_matrix(aspect);
    frame_cb.camera_pos[0] = impl->camera.position.x;
    frame_cb.camera_pos[1] = impl->camera.position.y;
    frame_cb.camera_pos[2] = impl->camera.position.z;

    vec3 sun_dir = normalize(impl->sun.direction);
    frame_cb.sun_direction[0] = sun_dir.x;
    frame_cb.sun_direction[1] = sun_dir.y;
    frame_cb.sun_direction[2] = sun_dir.z;
    frame_cb.sun_color[0]     = impl->sun.color.x;
    frame_cb.sun_color[1]     = impl->sun.color.y;
    frame_cb.sun_color[2]     = impl->sun.color.z;
    frame_cb.sun_intensity    = impl->sun.intensity;

    frame_cb.point_light_count = static_cast<i32>(impl->point_light_count);
    for (u32 i = 0; i < impl->point_light_count && i < kMaxPointLights; ++i) {
        const PointLight& pl = impl->point_lights[i];
        frame_cb.point_positions[i][0] = pl.position.x;
        frame_cb.point_positions[i][1] = pl.position.y;
        frame_cb.point_positions[i][2] = pl.position.z;
        frame_cb.point_positions[i][3] = pl.radius;
        frame_cb.point_colors[i][0]    = pl.color.x;
        frame_cb.point_colors[i][1]    = pl.color.y;
        frame_cb.point_colors[i][2]    = pl.color.z;
        frame_cb.point_colors[i][3]    = pl.intensity;
    }

    frame_cb.shadow_enabled  = impl->has_shadows ? 1.0f : 0.0f;
    frame_cb.light_view_proj = impl->cached_light_vp;

    void* frame_mapped = rhi.map_buffer(impl->per_frame_cb[fi]);
    if (frame_mapped) std::memcpy(frame_mapped, &frame_cb, sizeof(frame_cb));

    // Per-object CB: identity model matrix for now (model is at origin)
    GpuPerObjectCB obj_cb{};
    obj_cb.model         = mat4_identity();
    obj_cb.normal_matrix = mat4_identity();
    void* obj_mapped = rhi.map_buffer(impl->per_object_cb[fi]);
    if (obj_mapped) std::memcpy(obj_mapped, &obj_cb, sizeof(obj_cb));

    const f32 w = static_cast<f32>(rhi.width());
    const f32 h = static_cast<f32>(rhi.height());

    rhi.set_pipeline(impl->model_pipeline);
    rhi.set_constant_buffer(0, impl->per_frame_cb[fi]);
    rhi.set_constant_buffer(1, impl->per_object_cb[fi]);
    rhi.set_texture(2, impl->checkerboard_tex);
    if (impl->has_shadows)
        rhi.set_texture(3, impl->shadow_map);
    rhi.set_viewport(0, 0, w, h, 0, 1);
    rhi.set_scissor(0, 0, rhi.width(), rhi.height());
    rhi.set_vertex_buffer(impl->loaded_model.mesh.vertex_buffer, sizeof(LitVertex));
    rhi.set_index_buffer(impl->loaded_model.mesh.index_buffer, Format::R32_UINT);

    const Mesh& mesh = impl->loaded_model.mesh;
    for (u32 si = 0; si < mesh.sub_mesh_count; ++si) {
        const SubMesh& sub = mesh.sub_meshes[si];

        if (sub.material_index < impl->loaded_model.material_count) {
            const Material& mat = impl->loaded_model.materials[sub.material_index];
            if (mat.has_albedo_texture) {
                rhi.set_texture(2, mat.albedo_texture);
            }
        }

        rhi.draw_indexed(sub.index_count, sub.index_offset, sub.base_vertex);
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
    const u32 fi     = rhi.back_buffer().index % kFrameCount;

    // Per-frame CB (same layout as Blinn-Phong pass)
    GpuPerFrameCB frame_cb{};
    frame_cb.view         = impl->camera.view_matrix();
    frame_cb.proj         = impl->camera.proj_matrix(aspect);
    frame_cb.camera_pos[0] = impl->camera.position.x;
    frame_cb.camera_pos[1] = impl->camera.position.y;
    frame_cb.camera_pos[2] = impl->camera.position.z;

    const vec3 sun_dir       = normalize(impl->sun.direction);
    frame_cb.sun_direction[0] = sun_dir.x;
    frame_cb.sun_direction[1] = sun_dir.y;
    frame_cb.sun_direction[2] = sun_dir.z;
    frame_cb.sun_color[0]     = impl->sun.color.x;
    frame_cb.sun_color[1]     = impl->sun.color.y;
    frame_cb.sun_color[2]     = impl->sun.color.z;
    frame_cb.sun_intensity    = impl->sun.intensity;
    frame_cb.point_light_count = static_cast<i32>(impl->point_light_count);
    for (u32 i = 0; i < impl->point_light_count && i < kMaxPointLights; ++i) {
        const PointLight& pl = impl->point_lights[i];
        frame_cb.point_positions[i][0] = pl.position.x;
        frame_cb.point_positions[i][1] = pl.position.y;
        frame_cb.point_positions[i][2] = pl.position.z;
        frame_cb.point_positions[i][3] = pl.radius;
        frame_cb.point_colors[i][0]    = pl.color.x;
        frame_cb.point_colors[i][1]    = pl.color.y;
        frame_cb.point_colors[i][2]    = pl.color.z;
        frame_cb.point_colors[i][3]    = pl.intensity;
    }
    frame_cb.shadow_enabled  = impl->has_shadows ? 1.0f : 0.0f;
    frame_cb.light_view_proj = impl->cached_light_vp;

    void* fm = rhi.map_buffer(impl->per_frame_cb[fi]);
    if (fm) std::memcpy(fm, &frame_cb, sizeof(frame_cb));

    // Per-object CB (model transform set via set_model_transform).
    // Normal matrix is the inverse-transpose so non-uniform scale stays correct.
    GpuPerObjectCB obj_cb{};
    obj_cb.model         = impl->model_transform;
    obj_cb.normal_matrix = normal_matrix_of(impl->model_transform);
    void* om = rhi.map_buffer(impl->per_object_cb[fi]);
    if (om) std::memcpy(om, &obj_cb, sizeof(obj_cb));

    const f32 w = static_cast<f32>(rhi.width());
    const f32 h = static_cast<f32>(rhi.height());

    // IA / RS state is independent of the PSO and persists across pipeline changes.
    rhi.set_viewport(0, 0, w, h, 0, 1);
    rhi.set_scissor(0, 0, rhi.width(), rhi.height());
    rhi.set_vertex_buffer(impl->loaded_model.mesh.vertex_buffer, sizeof(LitVertex));
    rhi.set_index_buffer(impl->loaded_model.mesh.index_buffer, Format::R32_UINT);

    const Mesh& mesh = impl->loaded_model.mesh;

    // Each PBR PSO variant owns its own root signature, so all root arguments
    // (CBVs + texture tables) are re-bound for every draw.
    auto draw_submesh = [&](u32 si) {
        const SubMesh& sub = mesh.sub_meshes[si];
        const u32 mat_idx  = (sub.material_index < impl->per_material_cb_count)
                             ? sub.material_index : 0;
        const Material& mat = impl->loaded_model.materials[mat_idx];
        const bool blend = (mat.alpha_mode == AlphaMode::Blend);

        rhi.set_pipeline(impl->pbr_pso(mat.double_sided, blend));
        rhi.set_constant_buffer(0, impl->per_frame_cb[fi]);
        rhi.set_constant_buffer(1, impl->per_object_cb[fi]);
        rhi.set_constant_buffer(2, impl->per_material_cbs[mat_idx]);

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

    // Partition sub-meshes into opaque/masked (drawn first) and blended.
    std::vector<u32> opaque, transparent;
    opaque.reserve(mesh.sub_mesh_count);
    for (u32 si = 0; si < mesh.sub_mesh_count; ++si) {
        const u32 mat_idx = (mesh.sub_meshes[si].material_index < impl->per_material_cb_count)
                            ? mesh.sub_meshes[si].material_index : 0;
        if (impl->loaded_model.materials[mat_idx].alpha_mode == AlphaMode::Blend)
            transparent.push_back(si);
        else
            opaque.push_back(si);
    }

    // Sort blended sub-meshes far→near by view-space centroid depth.
    if (transparent.size() > 1) {
        const mat4 mv = impl->model_transform * impl->camera.view_matrix();
        auto view_z = [&](u32 si) {
            const f32* c = mesh.sub_meshes[si].centroid;
            vec4 p = vec4{c[0], c[1], c[2], 1.0f} * mv;
            return p.z;
        };
        std::sort(transparent.begin(), transparent.end(),
                  [&](u32 a, u32 b) { return view_z(a) > view_z(b); });
    }

    for (u32 si : opaque)      draw_submesh(si);
    for (u32 si : transparent) draw_submesh(si);
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

    if (!impl->create_cube_resources()) {
        shutdown();
        return false;
    }

    if (!impl->create_model_pipeline()) {
        GE_WARN("Model pipeline creation failed (model shaders may not be compiled yet)");
    }

    if (impl->model_pipeline.is_valid() && !impl->create_shadow_resources()) {
        GE_WARN("Shadow resources creation failed (shadow shader may not be compiled yet)");
    }

    if (!impl->create_pbr_pipeline()) {
        GE_WARN("PBR pipeline creation failed (pbr shaders may not be compiled yet)");
    }

    GE_INFO("Renderer initialized ({}x{})", desc.width, desc.height);
    return true;
}

void Renderer::shutdown() {
    auto* impl = static_cast<RendererImpl*>(impl_);
    if (!impl) return;

    for (u32 i = 0; i < kFrameCount; ++i) {
        if (impl->cube_cb[i].is_valid())
            impl->rhi.unmap_buffer(impl->cube_cb[i]);
        if (impl->per_frame_cb[i].is_valid())
            impl->rhi.unmap_buffer(impl->per_frame_cb[i]);
        if (impl->per_object_cb[i].is_valid())
            impl->rhi.unmap_buffer(impl->per_object_cb[i]);
        if (impl->shadow_cb[i].is_valid())
            impl->rhi.unmap_buffer(impl->shadow_cb[i]);
    }

    impl->destroy_pbr_material_cbs();
    if (impl->default_white_tex.is_valid())  impl->rhi.destroy_texture(impl->default_white_tex);
    if (impl->default_normal_tex.is_valid()) impl->rhi.destroy_texture(impl->default_normal_tex);

    impl->rhi.shutdown();
    delete impl;
    impl_ = nullptr;
}

bool Renderer::load_model(const char* path, ge::Allocator alloc) {
    auto* impl = static_cast<RendererImpl*>(impl_);

    if (impl->has_model) {
        impl->destroy_pbr_material_cbs();
        destroy_model(impl->loaded_model, impl->rhi);
        impl->has_model = false;
    }

    if (!load_model_gltf(path, impl->rhi, alloc, impl->loaded_model)) {
        return false;
    }

    impl->has_model = true;

    if (impl->pbr_ready()) {
        // Rebuild the PBR PSO variants if this model needs a different static
        // sampler wrap mode than the ones currently built.
        if (impl->loaded_model.sampler_address != impl->pbr_sampler_address) {
            if (!impl->build_pbr_psos(impl->loaded_model.sampler_address))
                GE_WARN("Failed to rebuild PBR PSOs for the model's sampler wrap mode");
        }
        if (impl->create_pbr_material_cbs()) {
            impl->has_pbr_model = true;
        } else {
            GE_WARN("Failed to create per-material CBs — falling back to Blinn-Phong");
        }
    }

    return true;
}

void Renderer::set_camera(const Camera& cam) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->camera = cam;
}

void Renderer::set_model_transform(const mat4& transform) {
    auto* impl = static_cast<RendererImpl*>(impl_);
    impl->model_transform = transform;
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

    TextureHandle bb = impl->rhi.back_buffer();
    TextureHandle db = impl->rhi.depth_buffer();

    impl->graph.begin();
    impl->graph.import(bb, ResourceState::Present);
    impl->graph.import(db, ResourceState::DepthWrite);

    if (impl->has_model && impl->has_pbr_model && impl->pbr_ready()) {
        // PBR render path
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
    } else if (impl->has_model && impl->model_pipeline.is_valid()) {
        // Blinn-Phong fallback
        if (impl->has_shadows) {
            impl->graph.import(impl->shadow_map, ResourceState::DepthWrite);
            impl->graph.add_pass("shadow", shadow_pass_execute, impl);
            impl->graph.pass_write(impl->shadow_map, ResourceState::DepthWrite);
        }

        impl->graph.add_pass("model", model_pass_execute, impl);
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
    if (impl->has_shadows && impl->has_model)
        impl->graph.set_final_state(impl->shadow_map, ResourceState::DepthWrite);

    impl->graph.compile();
    impl->graph.execute(impl->rhi);
}

void Renderer::end_frame() {
    auto* impl = static_cast<RendererImpl*>(impl_);
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
