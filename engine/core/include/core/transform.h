#pragma once
#include "core/quat.h"

/**
 * @file transform.h
 * @brief TRS (Translation-Rotation-Scale) composition and decomposition (Layer 1 — Core).
 */
namespace ge {

/**
 * @brief Compose a TRS matrix (row-vector: `p' = p * S * R * T`).
 *
 * Transforms are applied left-to-right: scale, then rotate, then translate.
 *
 * @param translation World-space position.
 * @param rotation    Orientation as a unit quaternion.
 * @param scale       Non-uniform scale factors.
 * @return Combined 4x4 transformation matrix.
 */
inline mat4 trs(vec3 translation, quat rotation, vec3 scale) {
    return mat4_scale(scale) * quat_to_matrix(rotation) * mat4_translation(translation);
}

/**
 * @brief Decomposed Translation-Rotation-Scale components.
 */
struct trs_components {
    vec3 translation; ///< World-space position.
    quat rotation;    ///< Orientation quaternion.
    vec3 scale;       ///< Non-uniform scale factors.
};

/**
 * @brief Decompose a TRS matrix into its components.
 *
 * Assumes the matrix contains no shear or projection.
 *
 * @param m 4x4 TRS matrix.
 * @return Decomposed translation, rotation, and scale.
 */
inline trs_components trs_decompose(const mat4& m) {
    trs_components out{};

    out.translation = {m.m[3][0], m.m[3][1], m.m[3][2]};

    // Scale = length of each basis row
    out.scale.x = length(vec3{m.m[0][0], m.m[0][1], m.m[0][2]});
    out.scale.y = length(vec3{m.m[1][0], m.m[1][1], m.m[1][2]});
    out.scale.z = length(vec3{m.m[2][0], m.m[2][1], m.m[2][2]});

    // Rotation matrix = basis rows normalized by scale
    mat4 rot{};
    f32 ix = 1.0f / out.scale.x, iy = 1.0f / out.scale.y, iz = 1.0f / out.scale.z;
    rot.m[0][0] = m.m[0][0]*ix; rot.m[0][1] = m.m[0][1]*ix; rot.m[0][2] = m.m[0][2]*ix;
    rot.m[1][0] = m.m[1][0]*iy; rot.m[1][1] = m.m[1][1]*iy; rot.m[1][2] = m.m[1][2]*iy;
    rot.m[2][0] = m.m[2][0]*iz; rot.m[2][1] = m.m[2][1]*iz; rot.m[2][2] = m.m[2][2]*iz;
    rot.m[3][3] = 1.0f;

    out.rotation = quat_from_matrix(rot);
    return out;
}

} // namespace ge
