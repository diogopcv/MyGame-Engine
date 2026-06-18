#pragma once

/**
 * @file math.h
 * @brief Umbrella header for the math library (Layer 1 — Core).
 *
 * Includes vec2/3/4, mat4, quat, and transform helpers, plus common
 * mathematical constants.
 */
#include "core/vec.h"
#include "core/mat4.h"
#include "core/quat.h"
#include "core/transform.h"

namespace ge {
inline constexpr f32 k_pi         = 3.14159265358979323846f; ///< Pi.
inline constexpr f32 k_pi_over_2  = k_pi / 2.0f;            ///< Pi / 2.
inline constexpr f32 k_deg_to_rad = k_pi / 180.0f;          ///< Degrees-to-radians multiplier.
inline constexpr f32 k_rad_to_deg = 180.0f / k_pi;          ///< Radians-to-degrees multiplier.
} // namespace ge
