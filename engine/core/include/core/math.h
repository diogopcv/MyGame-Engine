#pragma once

// Umbrella header for the entire math library.
// Includes vec2/3/4, mat4, quat, and transform helpers.
#include "core/vec.h"
#include "core/mat4.h"
#include "core/quat.h"
#include "core/transform.h"

namespace ge {
inline constexpr f32 k_pi         = 3.14159265358979323846f;
inline constexpr f32 k_pi_over_2  = k_pi / 2.0f;
inline constexpr f32 k_deg_to_rad = k_pi / 180.0f;
inline constexpr f32 k_rad_to_deg = 180.0f / k_pi;
} // namespace ge
