#pragma once
#include "gfx/mesh.h"
#include "gfx/material.h"
#include "core/allocator.h"

/**
 * @file model_loader.h
 * @brief glTF 2.0 model import (Layer 3 — Graphics).
 *
 * Uses cgltf (third-party, vendored) for parsing.  Converts glTF meshes,
 * materials, and textures into engine resource types at load time.  cgltf
 * does not leak into this header.
 */
namespace ge::gfx {

class RhiDevice;

/**
 * @brief A loaded 3D model: one mesh (with sub-meshes) + materials + textures.
 */
struct Model {
    Mesh       mesh;
    Material*  materials       = nullptr;
    u32        material_count  = 0;
    TextureHandle* textures    = nullptr; ///< All loaded textures (for cleanup).
    u32        texture_count   = 0;
    Allocator  alloc;                     ///< Allocator used for CPU-side arrays.
    AddressMode sampler_address = AddressMode::Wrap; ///< Dominant glTF sampler wrap mode (drives the PBR static sampler).
    bool       has_transparency = false;  ///< True if any material uses AlphaMode::Blend.
};

/**
 * @brief Optional CPU-side copy of the merged vertex/index data.
 *
 * Filled by load_model_gltf() when requested — used by tests (asserting what
 * the importer produced without a GPU readback) and future offline tooling.
 * Arrays are allocated from the same allocator passed to the loader; the
 * caller owns them (arena callers simply reset).  Exactly one of
 * `vertices` / `skinned_vertices` is non-null, matching `Mesh::skinned`.
 */
struct ModelCpu {
    LitVertex*     vertices         = nullptr; ///< When the model is not skinned.
    SkinnedVertex* skinned_vertices = nullptr; ///< When the model is skinned.
    u32*           indices          = nullptr;
    u32            vertex_count     = 0;
    u32            index_count      = 0;
};

/**
 * @brief Load a glTF / GLB model from disk.
 *
 * Meshes with JOINTS_0/WEIGHTS_0 attributes load as skinned
 * (`SkinnedVertex`, `Mesh::skinned = true`); their node transforms are
 * **not** baked into vertices (per glTF, a skinned node's transform is
 * ignored — the bone palette positions them). Static meshes keep the
 * Phase 3 node-baking behavior.
 *
 * @param path    File path to the .gltf or .glb file.
 * @param rhi     RHI device used to create GPU resources.
 * @param alloc   Allocator for CPU-side arrays (sub-meshes, materials, textures).
 * @param out     Receives the loaded model on success.
 * @param cpu_out Optional: receives a CPU-side copy of the vertex/index data.
 * @return true on success, false on parse/load error.
 */
bool load_model_gltf(const char* path, RhiDevice& rhi, Allocator alloc, Model& out,
                     ModelCpu* cpu_out = nullptr);

/**
 * @brief Destroy a model's GPU resources and free CPU-side memory.
 * @param model The model to destroy.
 * @param rhi   RHI device used to destroy GPU resources.
 */
void destroy_model(Model& model, RhiDevice& rhi);

} // namespace ge::gfx
