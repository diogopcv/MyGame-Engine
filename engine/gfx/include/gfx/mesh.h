#pragma once
#include "gfx/rhi_types.h"

/**
 * @file mesh.h
 * @brief Mesh and sub-mesh resource types (Layer 3 — Graphics).
 */
namespace ge::gfx {

/** @brief Vertex format for lit geometry: position + normal + UV. */
struct LitVertex {
    f32 position[3]; ///< World-space position.
    f32 normal[3];   ///< Unit normal.
    f32 uv[2];       ///< Texture coordinates.
};

/** @brief A sub-range within a shared vertex/index buffer pair. */
struct SubMesh {
    u32 index_offset;    ///< First index in the index buffer.
    u32 index_count;     ///< Number of indices to draw.
    i32 base_vertex;     ///< Added to each index before fetching the vertex.
    u32 material_index;  ///< Index into the parent Model's material array.
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
