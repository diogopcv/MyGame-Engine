#pragma once
#include "gfx/rhi_types.h"

/**
 * @file material.h
 * @brief Material resource type (Layer 3 — Graphics).
 */
namespace ge::gfx {

/**
 * @brief glTF 2.0 alpha rendering mode.
 *
 * - @c Opaque: alpha ignored, fully opaque.
 * - @c Mask:   alpha-tested; fragments with alpha < @c alpha_cutoff are discarded.
 * - @c Blend:  order-dependent alpha blending (straight alpha).
 */
enum class AlphaMode : u8 {
    Opaque,
    Mask,
    Blend,
};

/**
 * @brief A glTF `KHR_texture_transform` + UV-set selector for one texture map.
 *
 * Applied per map as `uv' = offset + R(rotation) * (scale * uv[uv_set])`.
 * Defaults are an identity transform on UV set 0.
 */
struct TexTransform {
    f32 offset[2] = {0.0f, 0.0f}; ///< UV translation.
    f32 scale[2]  = {1.0f, 1.0f}; ///< UV scale.
    f32 rotation  = 0.0f;         ///< CCW rotation in radians.
    u32 uv_set    = 0;            ///< Which vertex UV set this map samples (0 or 1).
};

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
    TexTransform  albedo_xform;                ///< KHR_texture_transform + UV set for the base map.

    // --- Metallic-roughness ---
    TextureHandle metallic_roughness_texture;  ///< G = roughness, B = metallic (linear).
    f32           metallic_factor  = 1.0f;     ///< Multiplied with texture B channel (default 1).
    f32           roughness_factor = 1.0f;     ///< Multiplied with texture G channel (default 1).
    bool          has_metallic_roughness_texture = false;
    TexTransform  mr_xform;

    // --- Tangent-space normal map ---
    TextureHandle normal_texture;
    bool          has_normal_texture = false;
    f32           normal_scale = 1.0f;         ///< Scales sampled normal XY (glTF normalTexture.scale).
    TexTransform  normal_xform;

    // --- Ambient occlusion ---
    TextureHandle occlusion_texture;           ///< R channel = occlusion factor.
    bool          has_occlusion_texture = false;
    f32           occlusion_strength = 1.0f;   ///< AO lerp strength (glTF occlusionTexture.strength).
    TexTransform  occlusion_xform;

    // --- Emissive ---
    TextureHandle emissive_texture;            ///< RGB emissive colour (sRGB).
    f32           emissive_factor[3] = {0,0,0}; ///< Linear emissive multiplier (KHR_materials_emissive_strength folded in).
    bool          has_emissive_texture = false;
    TexTransform  emissive_xform;

    // --- Transparency / culling ---
    AlphaMode     alpha_mode   = AlphaMode::Opaque;
    f32           alpha_cutoff = 0.5f;         ///< Threshold for AlphaMode::Mask.
    bool          double_sided = false;        ///< Disable back-face culling when true.
};

} // namespace ge::gfx
