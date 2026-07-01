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
 * @brief Load a glTF / GLB model from disk.
 * @param path  File path to the .gltf or .glb file.
 * @param rhi   RHI device used to create GPU resources.
 * @param alloc Allocator for CPU-side arrays (sub-meshes, materials, textures).
 * @param out   Receives the loaded model on success.
 * @return true on success, false on parse/load error.
 */
bool load_model_gltf(const char* path, RhiDevice& rhi, Allocator alloc, Model& out);

/**
 * @brief Destroy a model's GPU resources and free CPU-side memory.
 * @param model The model to destroy.
 * @param rhi   RHI device used to destroy GPU resources.
 */
void destroy_model(Model& model, RhiDevice& rhi);

} // namespace ge::gfx
