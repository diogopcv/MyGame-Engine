#pragma once
#include "gfx/rhi_types.h"

/**
 * @file material.h
 * @brief Material resource type (Layer 3 — Graphics).
 */
namespace ge::gfx {

/**
 * @brief A PBR metallic-roughness surface material.
 *
 * Follows the glTF 2.0 metallic-roughness workflow.  All texture handles are
 * optional; when absent the corresponding scalar factor is used directly and
 * the shader's texture-presence flag is false.
 */
struct Material {
    // --- Base colour (albedo) ---
    TextureHandle albedo_texture;              ///< Base-colour / diffuse map (sRGB).
    f32           base_color[4] = {1,1,1,1};  ///< RGBA multiplier; used as flat colour if no texture.
    bool          has_albedo_texture = false;

    // --- Metallic-roughness ---
    TextureHandle metallic_roughness_texture;  ///< G = roughness, B = metallic (linear).
    f32           metallic_factor  = 1.0f;     ///< Multiplied with texture B channel (default 1).
    f32           roughness_factor = 1.0f;     ///< Multiplied with texture G channel (default 1).
    bool          has_metallic_roughness_texture = false;

    // --- Tangent-space normal map ---
    TextureHandle normal_texture;
    bool          has_normal_texture = false;

    // --- Ambient occlusion ---
    TextureHandle occlusion_texture;           ///< R channel = occlusion factor.
    bool          has_occlusion_texture = false;

    // --- Emissive ---
    TextureHandle emissive_texture;            ///< RGB emissive colour (sRGB).
    f32           emissive_factor[3] = {0,0,0}; ///< Linear emissive multiplier.
    bool          has_emissive_texture = false;
};

} // namespace ge::gfx
