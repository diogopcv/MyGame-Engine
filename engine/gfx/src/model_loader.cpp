#include "gfx/model_loader.h"
#include "gfx/rhi.h"

#include "core/log.h"
#include "core/assert.h"

#include <cgltf.h>
#include <stb_image.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ge::gfx {

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

                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                    cgltf_size img_idx = cgltf_image_index(data, pbr.base_color_texture.texture->image);
                    if (img_idx < out.texture_count && out.textures[img_idx].is_valid()) {
                        mat.albedo_texture = out.textures[img_idx];
                        mat.has_albedo_texture = true;
                    }
                }
            }
        }
    }

    // --- Count total vertices and indices across all primitives ---
    u32 total_vertices = 0;
    u32 total_indices  = 0;
    u32 total_prims    = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
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

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;

            u32 vert_count = 0;

            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& attr = prim.attributes[ai];
                const cgltf_accessor* acc = attr.data;

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
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 n[3] = {};
                        cgltf_accessor_read_float(acc, vi, n, 3);
                        vertices[vert_offset + vi].normal[0] = n[0];
                        vertices[vert_offset + vi].normal[1] = n[1];
                        vertices[vert_offset + vi].normal[2] = n[2];
                    }
                } else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) {
                    for (cgltf_size vi = 0; vi < acc->count; ++vi) {
                        f32 uv[2] = {};
                        cgltf_accessor_read_float(acc, vi, uv, 2);
                        vertices[vert_offset + vi].uv[0] = uv[0];
                        vertices[vert_offset + vi].uv[1] = uv[1];
                    }
                }
            }

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

            SubMesh& sub = out.mesh.sub_meshes[prim_index];
            sub.index_offset   = index_offset;
            sub.index_count    = idx_count;
            sub.base_vertex    = static_cast<i32>(vert_offset);
            sub.material_index = 0;
            if (prim.material) {
                sub.material_index = static_cast<u32>(cgltf_material_index(data, prim.material));
            }

            vert_offset  += vert_count;
            index_offset += idx_count;
            ++prim_index;
        }
    }

    cgltf_free(data);

    // --- Create GPU buffers ---
    BufferDesc vb_desc{};
    vb_desc.size         = total_vertices * sizeof(LitVertex);
    vb_desc.usage        = BufferUsage::Vertex;
    vb_desc.cpu_visible  = true;
    vb_desc.initial_data = vertices.data();
    out.mesh.vertex_buffer = rhi.create_buffer(vb_desc);

    BufferDesc ib_desc{};
    ib_desc.size         = total_indices * sizeof(u32);
    ib_desc.usage        = BufferUsage::Index;
    ib_desc.cpu_visible  = true;
    ib_desc.initial_data = indices.data();
    out.mesh.index_buffer = rhi.create_buffer(ib_desc);

    out.mesh.vertex_count  = total_vertices;
    out.mesh.index_count   = total_indices;
    out.mesh.vertex_stride = sizeof(LitVertex);

    if (!out.mesh.vertex_buffer.is_valid() || !out.mesh.index_buffer.is_valid()) {
        GE_ERROR("Failed to create GPU buffers for: {}", path);
        return false;
    }

    GE_INFO("Loaded model: {} ({} verts, {} indices, {} submeshes, {} materials)",
            path, total_vertices, total_indices, total_prims, out.material_count);
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
