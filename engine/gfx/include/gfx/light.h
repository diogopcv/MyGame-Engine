#pragma once
#include "core/types.h"
#include "core/vec.h"

/**
 * @file light.h
 * @brief Light source types for forward lighting (Layer 3 — Graphics).
 */
namespace ge::gfx {

/** @brief Directional light (sun). */
struct DirectionalLight {
    vec3 direction = {0.5f, -1.0f, 0.3f}; ///< World-space direction TO the light source.
    vec3 color     = {1.0f, 1.0f, 0.95f};
    f32  intensity = 1.0f;
};

/** @brief Point light with distance attenuation. */
struct PointLight {
    vec3 position = {0, 0, 0};
    f32  radius   = 10.0f;
    vec3 color    = {1.0f, 1.0f, 1.0f};
    f32  intensity = 1.0f;
};

static constexpr u32 kMaxPointLights = 4;

} // namespace ge::gfx
