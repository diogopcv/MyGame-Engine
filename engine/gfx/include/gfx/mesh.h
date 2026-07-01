#pragma once
#include "gfx/rhi_types.h"

/**
 * @file mesh.h
 * @brief Mesh and sub-mesh resource types (Layer 3 — Graphics).
 */
namespace ge::gfx {

/**
 * @brief Vertex format for lit geometry: position + normal + UVs + colour + tangent.
 *
 * Stride = 72 bytes.  Two UV sets are carried so materials can reference either
 * `TEXCOORD_0` or `TEXCOORD_1` per texture.  `color` holds the glTF `COLOR_0`
 * vertex colour (defaults to white).  The tangent w-component stores the
 * bitangent sign (+1 or -1) as per the glTF 2.0 convention.
 *
 * The depth-only/shadow and Blinn-Phong input layouts read only the first three
 * attributes (position\@0, normal\@12, uv\@24), so those offsets are fixed.
 */
struct LitVertex {
    f32 position[3];           ///< Object-space position.            (offset 0)
    f32 normal[3];             ///< Unit normal.                      (offset 12)
    f32 uv[2];                 ///< Texture coordinates, set 0.       (offset 24)
    f32 uv1[2]    = {0, 0};    ///< Texture coordinates, set 1.       (offset 32)
    f32 color[4]  = {1,1,1,1}; ///< Vertex colour (COLOR_0), RGBA.    (offset 40)
    f32 tangent[4];            ///< xyz = tangent, w = bitangent sign.(offset 56)
};

/** @brief A sub-range within a shared vertex/index buffer pair. */
struct SubMesh {
    u32 index_offset;       ///< First index in the index buffer.
    u32 index_count;        ///< Number of indices to draw.
    i32 base_vertex;        ///< Added to each index before fetching the vertex.
    u32 material_index;     ///< Index into the parent Model's material array.
    f32 centroid[3] = {0, 0, 0}; ///< Object-space centroid, for back-to-front transparent sorting.
};

/**
 * @brief GPU-resident mesh: shared vertex/index buffers + sub-mesh table.
 *
 * All sub-meshes share the same vertex and index buffers; each sub-mesh
 * records its offset/count and base vertex for DrawIndexed.
 */
struct Mesh {
    BufferHandle vertex_buffer;
    BufferHandle index_buffer;
    u32          vertex_count  = 0;
    u32          index_count   = 0;
    u32          vertex_stride = sizeof(LitVertex);
    SubMesh*     sub_meshes    = nullptr;
    u32          sub_mesh_count = 0;
};

} // namespace ge::gfx
