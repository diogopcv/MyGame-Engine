#pragma once
#include "gfx/rhi_types.h"

/**
 * @file material.h
 * @brief Material resource type (Layer 3 — Graphics).
 */
namespace ge::gfx {

/**
 * @brief A surface material: albedo texture + base colour fallback.
 *
 * If @c has_albedo_texture is false, the shader uses @c base_color as a
 * flat colour.  Phase 3 uses Blinn-Phong; PBR parameters are future work.
 */
struct Material {
    TextureHandle albedo_texture;            ///< Albedo / diffuse map.
    f32           base_color[4] = {1,1,1,1}; ///< RGBA fallback colour.
    bool          has_albedo_texture = false;
};

} // namespace ge::gfx
