#include "gfx/model_loader.h"
#include "gfx/rhi.h"

#include "core/log.h"
#include "core/assert.h"
#include "core/mat4.h"
#include "core/vec.h"

#include <cgltf.h>
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ge::gfx {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static TextureHandle load_image(const cgltf_image* img, const char* base_dir,
                                RhiDevice& rhi) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = nullptr;

    if (img->buffer_view) {
        const u8* data = static_cast<const u8*>(img->buffer_view->buffer->data);
        data += img->buffer_view->offset;
        pixels = stbi_load_from_memory(data, static_cast<int>(img->buffer_view->size),
                                       &w, &h, &channels, 4);
    } else if (img->uri) {
        std::string path = std::string(base_dir) + "/" + img->uri;
        pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    }

    if (!pixels) {
        GE_WARN("Failed to load image: {}", img->uri ? img->uri : "(embedded)");
        return {};
    }

    TextureDesc desc{};
    desc.width        = static_cast<u32>(w);
    desc.height       = static_cast<u32>(h);
    desc.format       = Format::R8G8B8A8_UNORM;
    desc.usage        = TextureUsage::ShaderResource;
    desc.initial_data = pixels;
    desc.row_pitch    = static_cast<u32>(w) * 4;

    TextureHandle tex = rhi.create_texture(desc);
    stbi_image_free(pixels);
    return tex;
}

/// Map a glTF sampler wrap constant to the engine address mode.
static AddressMode wrap_to_address(int gltf_wrap) {
    switch (gltf_wrap) {
        case 33071: return AddressMode::Clamp;  // CLAMP_TO_EDGE
        case 33648: return AddressMode::Mirror;  // MIRRORED_REPEAT
        case 10497: default: return AddressMode::Wrap; // REPEAT
    }
}

/// Read KHR_texture_transform + the UV-set index from a glTF texture view.
static void read_tex_transform(const cgltf_texture_view& view, TexTransform& out) {
    out.uv_set = static_cast<u32>(view.texcoord);
    if (view.has_transform) {
        out.offset[0] = view.transform.offset[0];
        out.offset[1] = view.transform.offset[1];
        out.scale[0]  = view.transform.scale[0];
        out.scale[1]  = view.transform.scale[1];
        out.rotation  = view.transform.rotation;
        // A transform may override which UV set is used.
        if (view.transform.has_texcoord)
            out.uv_set = static_cast<u32>(view.transform.texcoord);
    }
}

/// Resolve a texture view's image to an already-loaded engine texture handle.
static bool resolve_texture(const cgltf_data* data, const cgltf_texture_view& view,
                            const TextureHandle* textures, u32 texture_count,
                            TextureHandle& out) {
    if (!view.texture || !view.texture->image) return false;
    cgltf_size idx = cgltf_image_index(data, view.texture->image);
    if (idx >= texture_count || !textures[idx].is_valid()) return false;
    out = textures[idx];
    return true;
}

// ---------------------------------------------------------------------------
// Node instancing: flatten the scene graph into (mesh, world-transform) pairs.
// ---------------------------------------------------------------------------

struct MeshInstance {
    const cgltf_mesh* mesh = nullptr;
    mat4 world{};         ///< Object→world (row-vector convention).
    mat4 normal_world{};  ///< transpose(inverse(world)) for normals/tangents.
};

/// cgltf writes world transforms as a column-major float[16]. Copying element
/// arr[i*4+j] into row-major m[i][j] yields the transpose, which is exactly the
/// row-vector matrix we want (`p_world = p_local * world`).
static mat4 column_major_to_row_vector(const float w[16]) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = w[i * 4 + j];
    return r;
}

static void gather_instances(const cgltf_data* data, std::vector<MeshInstance>& out) {
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh) continue;
        float wm[16];
        cgltf_node_transform_world(&node, wm);
        MeshInstance inst{};
        inst.mesh        = node.mesh;
        inst.world       = column_major_to_row_vector(wm);
        inst.normal_world = transpose(inverse(inst.world));
        out.push_back(inst);
    }
    // Fallback: no node references a mesh — bake every mesh at identity.
    if (out.empty()) {
        for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
            MeshInstance inst{};
            inst.mesh         = &data->meshes[mi];
            inst.world        = mat4_identity();
            inst.normal_world = mat4_identity();
            out.push_back(inst);
        }
    }
}

static vec3 transform_point(vec3 p, const mat4& m) {
    vec4 r = vec4{p.x, p.y, p.z, 1.0f} * m;
    return {r.x, r.y, r.z};
}

static vec3 transform_dir(vec3 v, const mat4& m) {
    vec4 r = vec4{v.x, v.y, v.z, 0.0f} * m;
    return {r.x, r.y, r.z};
}

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

bool load_model_gltf(const char* path, RhiDevice& rhi, Allocator alloc, Model& out) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        GE_ERROR("cgltf_parse_file failed: {}", path);
        return false;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        GE_ERROR("cgltf_load_buffers failed: {}", path);
        cgltf_free(data);
        return false;
    }

    std::string base_dir = std::filesystem::path(path).parent_path().string();

    out.alloc = alloc;

    // --- Load images/textures ---
    out.texture_count = static_cast<u32>(data->images_count);
    if (out.texture_count > 0) {
        out.textures = alloc.alloc_n<TextureHandle>(out.texture_count);
        for (u32 i = 0; i < out.texture_count; ++i) {
            out.textures[i] = load_image(&data->images[i], base_dir.c_str(), rhi);
        }
    }

    // --- Determine the dominant sampler wrap mode (single static sampler) ---
    out.sampler_address = AddressMode::Wrap;
    for (cgltf_size i = 0; i < data->materials_count; ++i) {
        const cgltf_material& m = data->materials[i];
        const cgltf_texture* t = m.has_pbr_metallic_roughness
                                 ? m.pbr_metallic_roughness.base_color_texture.texture
                                 : nullptr;
        if (t && t->sampler) {
            out.sampler_address = wrap_to_address(t->sampler->wrap_s);
            break;
        }
    }

    // --- Load materials ---
    out.material_count = static_cast<u32>(data->materials_count);
    if (out.material_count == 0) {
        out.material_count = 1;
        out.materials = alloc.alloc_n<Material>(1);
        out.materials[0] = {};
    } else {
        out.materials = alloc.alloc_n<Material>(out.material_count);
        for (u32 i = 0; i < out.material_count; ++i) {
            const cgltf_material& gmat = data->materials[i];
            Material& mat = out.materials[i];
            mat = {};

            if (gmat.has_pbr_metallic_roughness) {
                const auto& pbr = gmat.pbr_metallic_roughness;
                mat.base_color[0] = pbr.base_color_factor[0];
                mat.base_color[1] = pbr.base_color_factor[1];
                mat.base_color[2] = pbr.base_color_factor[2];
                mat.base_color[3] = pbr.base_color_factor[3];
                mat.metallic_factor  = pbr.metallic_factor;
                mat.roughness_factor = pbr.roughness_factor;

                if (resolve_texture(data, pbr.base_color_texture, out.textures,
                                    out.texture_count, mat.albedo_texture)) {
                    mat.has_albedo_texture = true;
                    read_tex_transform(pbr.base_color_texture, mat.albedo_xform);
                }
                if (resolve_texture(data, pbr.metallic_roughness_texture, out.textures,
                                    out.texture_count, mat.metallic_roughness_texture)) {
                    mat.has_metallic_roughness_texture = true;
                    read_tex_transform(pbr.metallic_roughness_texture, mat.mr_xform);
                }
            }

            if (resolve_texture(data, gmat.normal_texture, out.textures,
                                out.texture_count, mat.normal_texture)) {
                mat.has_normal_texture = true;
                mat.normal_scale = gmat.normal_texture.scale;
                read_tex_transform(gmat.normal_texture, mat.normal_xform);
            }
            if (resolve_texture(data, gmat.occlusion_texture, out.textures,
                                out.texture_count, mat.occlusion_texture)) {
                mat.has_occlusion_texture = true;
                mat.occlusion_strength = gmat.occlusion_texture.scale; // cgltf stores strength in .scale
                read_tex_transform(gmat.occlusion_texture, mat.occlusion_xform);
            }
            if (resolve_texture(data, gmat.emissive_texture, out.textures,
                                out.texture_count, mat.emissive_texture)) {
                mat.has_emissive_texture = true;
                read_tex_transform(gmat.emissive_texture, mat.emissive_xform);
            }

            // Emissive factor, with KHR_materials_emissive_strength folded in.
            f32 emissive_strength = gmat.has_emissive_strength
                                    ? gmat.emissive_strength.emissive_strength : 1.0f;
            mat.emissive_factor[0] = gmat.emissive_factor[0] * emissive_strength;
            mat.emissive_factor[1] = gmat.emissive_factor[1] * emissive_strength;
            mat.emissive_factor[2] = gmat.emissive_factor[2] * emissive_strength;

            // Transparency / culling.
            switch (gmat.alpha_mode) {
                case cgltf_alpha_mode_mask:  mat.alpha_mode = AlphaMode::Mask;  break;
                case cgltf_alpha_mode_blend: mat.alpha_mode = AlphaMode::Blend; break;
                default:                     mat.alpha_mode = AlphaMode::Opaque; break;
            }
            mat.alpha_cutoff = gmat.alpha_cutoff;
            mat.double_sided = gmat.double_sided != 0;
            if (mat.alpha_mode == AlphaMode::Blend) out.has_transparency = true;
        }
    }

    // --- Flatten the scene graph into mesh instances ---
    std::vector<MeshInstance> instances;
    gather_instances(data, instances);

    // --- Count total vertices and indices across all primitives ---
    u32 total_vertices = 0;
    u32 total_indices  = 0;
    u32 total_prims    = 0;

    for (const MeshInstance& inst : instances) {
        for (cgltf_size pi = 0; pi < inst.mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = inst.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            u32 vert_count = 0;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                if (prim.attributes[ai].type == cgltf_attribute_type_position) {
                    vert_count = static_cast<u32>(prim.attributes[ai].data->count);
                    break;
                }
            }

            total_vertices += vert_count;
            total_indices += prim.indices ? static_cast<u32>(prim.indices->count) : vert_count;
            ++total_prims;
        }
    }

    if (total_prims == 0) {
        GE_ERROR("No triangle primitives found in: {}", path);
        cgltf_free(data);
        return false;
    }

    // --- Build merged vertex/index data ---
    std::vector<LitVertex> vertices(total_vertices);
    std::vector<u32> indices(total_indices);
    out.mesh.sub_meshes = alloc.alloc_n<SubMesh>(total_prims);
    out.mesh.sub_mesh_count = total_prims;

    u32 vert_offset  = 0;
    u32 index_offset = 0;
    u32 prim_index   = 0;

    for (const MeshInstance& inst : instances) {
        for (cgltf_size pi = 0; pi < inst.mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = inst.mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            u32  vert_count   = 0;
            bool has_normals  = false;
            bool has_tangents = false;
            bool has_uv0      = false;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& attr = prim.attributes[ai];
                const cgltf_accessor*  acc  = attr.data;

                if (attr.type == cgltf_attribute_type_position) {
                    vert_count = static_cast<u32>(acc->count);
                    for (u32 vi = 0; vi < vert_count; ++vi) {
                        f32 v[3] = {};
                        cgltf_accessor_read_float(acc, vi, v, 3);
                        vertices[vert_offset + vi].position[0] = v[0];
                        vertices[vert_offset + vi].position[1] = v[1];
                        vertices[vert_offset + vi].position[2] = v[2];
                    }
                } else if (attr.type == cgltf_attribute_type_normal) {
                    has_normals = true;
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 n[3] = {};
                        cgltf_accessor_read_float(acc, vi, n, 3);
                        vertices[vert_offset + vi].normal[0] = n[0];
                        vertices[vert_offset + vi].normal[1] = n[1];
                        vertices[vert_offset + vi].normal[2] = n[2];
                    }
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                    has_uv0 = true;
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 uv[2] = {};
                        cgltf_accessor_read_float(acc, vi, uv, 2);
                        vertices[vert_offset + vi].uv[0] = uv[0];
                        vertices[vert_offset + vi].uv[1] = uv[1];
                    }
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 1) {
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 uv[2] = {};
                        cgltf_accessor_read_float(acc, vi, uv, 2);
                        vertices[vert_offset + vi].uv1[0] = uv[0];
                        vertices[vert_offset + vi].uv1[1] = uv[1];
                    }
                } else if (attr.type == cgltf_attribute_type_color && attr.index == 0) {
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 c[4] = {1, 1, 1, 1};
                        cgltf_accessor_read_float(acc, vi, c, 4); // vec3 leaves c[3]=1
                        vertices[vert_offset + vi].color[0] = c[0];
                        vertices[vert_offset + vi].color[1] = c[1];
                        vertices[vert_offset + vi].color[2] = c[2];
                        vertices[vert_offset + vi].color[3] = c[3];
                    }
                } else if (attr.type == cgltf_attribute_type_tangent) {
                    has_tangents = true;
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 t[4] = {1, 0, 0, 1};
                        cgltf_accessor_read_float(acc, vi, t, 4);
                        vertices[vert_offset + vi].tangent[0] = t[0];
                        vertices[vert_offset + vi].tangent[1] = t[1];
                        vertices[vert_offset + vi].tangent[2] = t[2];
                        vertices[vert_offset + vi].tangent[3] = t[3];
                    }
                }
            }

            // --- Indices (local to this primitive's vertex range) ---
            u32 idx_count = 0;
            if (prim.indices) {
                idx_count = static_cast<u32>(prim.indices->count);
                for (u32 ii = 0; ii < idx_count; ++ii) {
                    indices[index_offset + ii] = static_cast<u32>(
                        cgltf_accessor_read_index(prim.indices, ii));
                }
            } else {
                idx_count = vert_count;
                for (u32 ii = 0; ii < idx_count; ++ii) {
                    indices[index_offset + ii] = ii;
                }
            }

            // --- Fallback: generate normals (area-weighted) when absent ---
            if (!has_normals && vert_count > 0) {
                for (u32 vi = 0; vi < vert_count; ++vi) {
                    vertices[vert_offset + vi].normal[0] = 0.0f;
                    vertices[vert_offset + vi].normal[1] = 0.0f;
                    vertices[vert_offset + vi].normal[2] = 0.0f;
                }
                for (u32 t = 0; t + 2 < idx_count; t += 3) {
                    u32 i0 = indices[index_offset + t + 0];
                    u32 i1 = indices[index_offset + t + 1];
                    u32 i2 = indices[index_offset + t + 2];
                    const f32* p0 = vertices[vert_offset + i0].position;
                    const f32* p1 = vertices[vert_offset + i1].position;
                    const f32* p2 = vertices[vert_offset + i2].position;
                    vec3 e1{p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2]};
                    vec3 e2{p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2]};
                    vec3 fn = cross(e1, e2); // unnormalized → area-weighted
                    for (u32 k : {i0, i1, i2}) {
                        vertices[vert_offset + k].normal[0] += fn.x;
                        vertices[vert_offset + k].normal[1] += fn.y;
                        vertices[vert_offset + k].normal[2] += fn.z;
                    }
                }
                for (u32 vi = 0; vi < vert_count; ++vi) {
                    f32* n = vertices[vert_offset + vi].normal;
                    vec3 nn = normalize(vec3{n[0], n[1], n[2]});
                    n[0] = nn.x; n[1] = nn.y; n[2] = nn.z;
                }
                has_normals = true;
            }

            // --- Fallback: generate tangents (Lengyel) when absent + UVs exist ---
            if (!has_tangents && has_uv0 && vert_count > 0) {
                std::vector<vec3> tan(vert_count, vec3{0, 0, 0});
                std::vector<vec3> bit(vert_count, vec3{0, 0, 0});
                for (u32 t = 0; t + 2 < idx_count; t += 3) {
                    u32 i0 = indices[index_offset + t + 0];
                    u32 i1 = indices[index_offset + t + 1];
                    u32 i2 = indices[index_offset + t + 2];
                    const LitVertex& v0 = vertices[vert_offset + i0];
                    const LitVertex& v1 = vertices[vert_offset + i1];
                    const LitVertex& v2 = vertices[vert_offset + i2];
                    vec3 e1{v1.position[0]-v0.position[0], v1.position[1]-v0.position[1], v1.position[2]-v0.position[2]};
                    vec3 e2{v2.position[0]-v0.position[0], v2.position[1]-v0.position[1], v2.position[2]-v0.position[2]};
                    f32 du1 = v1.uv[0]-v0.uv[0], dv1 = v1.uv[1]-v0.uv[1];
                    f32 du2 = v2.uv[0]-v0.uv[0], dv2 = v2.uv[1]-v0.uv[1];
                    f32 denom = du1*dv2 - du2*dv1;
                    f32 r = (std::fabs(denom) > 1e-12f) ? 1.0f / denom : 0.0f;
                    vec3 sdir{(dv2*e1.x - dv1*e2.x)*r, (dv2*e1.y - dv1*e2.y)*r, (dv2*e1.z - dv1*e2.z)*r};
                    vec3 tdir{(du1*e2.x - du2*e1.x)*r, (du1*e2.y - du2*e1.y)*r, (du1*e2.z - du2*e1.z)*r};
                    for (u32 k : {i0, i1, i2}) {
                        tan[k].x += sdir.x; tan[k].y += sdir.y; tan[k].z += sdir.z;
                        bit[k].x += tdir.x; bit[k].y += tdir.y; bit[k].z += tdir.z;
                    }
                }
                for (u32 vi = 0; vi < vert_count; ++vi) {
                    LitVertex& v = vertices[vert_offset + vi];
                    vec3 n{v.normal[0], v.normal[1], v.normal[2]};
                    vec3 t = tan[vi];
                    // Gram-Schmidt orthonormalize against the normal.
                    f32 ndt = dot(n, t);
                    vec3 to{t.x - n.x*ndt, t.y - n.y*ndt, t.z - n.z*ndt};
                    f32 len = std::sqrt(to.x*to.x + to.y*to.y + to.z*to.z);
                    if (len > 1e-8f) { to.x/=len; to.y/=len; to.z/=len; }
                    else             { to = vec3{1, 0, 0}; }
                    // Handedness sign.
                    vec3 nxt = cross(n, t);
                    f32 sign = (dot(nxt, bit[vi]) < 0.0f) ? -1.0f : 1.0f;
                    v.tangent[0] = to.x; v.tangent[1] = to.y; v.tangent[2] = to.z; v.tangent[3] = sign;
                }
                has_tangents = true;
            } else if (!has_tangents) {
                // No UVs to derive from — keep a stable default frame.
                for (u32 vi = 0; vi < vert_count; ++vi) {
                    LitVertex& v = vertices[vert_offset + vi];
                    v.tangent[0] = 1.0f; v.tangent[1] = 0.0f; v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
                }
            }

            // --- Bake the node world transform into the vertices ---
            vec3 centroid_accum{0, 0, 0};
            for (u32 vi = 0; vi < vert_count; ++vi) {
                LitVertex& v = vertices[vert_offset + vi];
                vec3 p = transform_point(vec3{v.position[0], v.position[1], v.position[2]}, inst.world);
                v.position[0] = p.x; v.position[1] = p.y; v.position[2] = p.z;
                centroid_accum.x += p.x; centroid_accum.y += p.y; centroid_accum.z += p.z;

                vec3 n = normalize(transform_dir(vec3{v.normal[0], v.normal[1], v.normal[2]}, inst.normal_world));
                v.normal[0] = n.x; v.normal[1] = n.y; v.normal[2] = n.z;

                vec3 t = normalize(transform_dir(vec3{v.tangent[0], v.tangent[1], v.tangent[2]}, inst.world));
                v.tangent[0] = t.x; v.tangent[1] = t.y; v.tangent[2] = t.z; // w (sign) unchanged
            }

            SubMesh& sub = out.mesh.sub_meshes[prim_index];
            sub.index_offset   = index_offset;
            sub.index_count    = idx_count;
            sub.base_vertex    = static_cast<i32>(vert_offset);
            sub.material_index = 0;
            if (prim.material) {
                sub.material_index = static_cast<u32>(cgltf_material_index(data, prim.material));
            }
            f32 inv_n = vert_count ? 1.0f / static_cast<f32>(vert_count) : 0.0f;
            sub.centroid[0] = centroid_accum.x * inv_n;
            sub.centroid[1] = centroid_accum.y * inv_n;
            sub.centroid[2] = centroid_accum.z * inv_n;

            vert_offset  += vert_count;
            index_offset += idx_count;
            ++prim_index;
        }
    }

    cgltf_free(data);

    // --- Create GPU buffers (default heap: static geometry lives in VRAM,
    //     uploaded once through the RHI's staging path) ---
    BufferDesc vb_desc{};
    vb_desc.size         = total_vertices * sizeof(LitVertex);
    vb_desc.usage        = BufferUsage::Vertex;
    vb_desc.cpu_visible  = false;
    vb_desc.initial_data = vertices.data();
    out.mesh.vertex_buffer = rhi.create_buffer(vb_desc);

    BufferDesc ib_desc{};
    ib_desc.size         = total_indices * sizeof(u32);
    ib_desc.usage        = BufferUsage::Index;
    ib_desc.cpu_visible  = false;
    ib_desc.initial_data = indices.data();
    out.mesh.index_buffer = rhi.create_buffer(ib_desc);

    out.mesh.vertex_count  = total_vertices;
    out.mesh.index_count   = total_indices;
    out.mesh.vertex_stride = sizeof(LitVertex);

    if (!out.mesh.vertex_buffer.is_valid() || !out.mesh.index_buffer.is_valid()) {
        GE_ERROR("Failed to create GPU buffers for: {}", path);
        return false;
    }

    GE_INFO("Loaded model: {} ({} verts, {} indices, {} submeshes, {} materials, transparency={})",
            path, total_vertices, total_indices, total_prims, out.material_count,
            out.has_transparency ? 1 : 0);
    return true;
}

void destroy_model(Model& model, RhiDevice& rhi) {
    if (model.mesh.vertex_buffer.is_valid())
        rhi.destroy_buffer(model.mesh.vertex_buffer);
    if (model.mesh.index_buffer.is_valid())
        rhi.destroy_buffer(model.mesh.index_buffer);

    for (u32 i = 0; i < model.texture_count; ++i) {
        if (model.textures[i].is_valid())
            rhi.destroy_texture(model.textures[i]);
    }

    if (model.mesh.sub_meshes)
        model.alloc.free(model.mesh.sub_meshes, sizeof(SubMesh) * model.mesh.sub_mesh_count);
    if (model.materials)
        model.alloc.free(model.materials, sizeof(Material) * model.material_count);
    if (model.textures)
        model.alloc.free(model.textures, sizeof(TextureHandle) * model.texture_count);

    model = {};
}

} // namespace ge::gfx
