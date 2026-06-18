#pragma once
#include "core/vec.h"
#include <cmath>

/**
 * @file mat4.h
 * @brief 4x4 matrix type with row-major storage (Layer 1 — Core).
 *
 * Row-vector convention: transform a point as `p' = p * M`.
 * Composite transforms read left-to-right: `p' = p * S * R * T`.
 */
namespace ge {

/**
 * @brief 4x4 matrix, row-major storage.
 *
 * Element access: `m[row][col]`.  Uses the row-vector convention — points
 * are transformed as `p' = p * M`, and composite transforms read
 * left-to-right.
 */
struct mat4 {
    f32 m[4][4]; ///< Row-major matrix elements: `m[row][col]`.
};

/**
 * @brief Matrix multiplication: `result[i][j] = sum_k A[i][k] * B[k][j]`.
 */
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

/** @brief Row-vector transform: `result[j] = sum_i v[i] * M[i][j]`. */
inline vec4 operator*(vec4 v, const mat4& M) {
    return {
        v.x*M.m[0][0] + v.y*M.m[1][0] + v.z*M.m[2][0] + v.w*M.m[3][0],
        v.x*M.m[0][1] + v.y*M.m[1][1] + v.z*M.m[2][1] + v.w*M.m[3][1],
        v.x*M.m[0][2] + v.y*M.m[1][2] + v.z*M.m[2][2] + v.w*M.m[3][2],
        v.x*M.m[0][3] + v.y*M.m[1][3] + v.z*M.m[2][3] + v.w*M.m[3][3],
    };
}

/** @brief Construct a 4x4 identity matrix. */
inline mat4 mat4_identity() {
    mat4 r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
    return r;
}

/** @brief Transpose a 4x4 matrix. */
inline mat4 transpose(const mat4& a) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[j][i];
    return r;
}

/**
 * @brief Compute the inverse of a 4x4 matrix via cofactor expansion (Cramer's rule).
 * @param a Matrix to invert (must be non-singular).
 * @return Inverse matrix.
 */
inline mat4 inverse(const mat4& a) {
    const auto& m = a.m;

    // 3x3 sub-determinant: rows/cols passed in reading order
    auto d3 = [](f32 a00, f32 a01, f32 a02,
                 f32 a10, f32 a11, f32 a12,
                 f32 a20, f32 a21, f32 a22) -> f32 {
        return a00*(a11*a22 - a12*a21)
             - a01*(a10*a22 - a12*a20)
             + a02*(a10*a21 - a11*a20);
    };

    // Build the adjugate: adj[i][j] = cofactor C[j][i], sign = (-1)^(j+i)
    mat4 adj{};
    adj.m[0][0] = +d3(m[1][1],m[1][2],m[1][3], m[2][1],m[2][2],m[2][3], m[3][1],m[3][2],m[3][3]);
    adj.m[0][1] = -d3(m[0][1],m[0][2],m[0][3], m[2][1],m[2][2],m[2][3], m[3][1],m[3][2],m[3][3]);
    adj.m[0][2] = +d3(m[0][1],m[0][2],m[0][3], m[1][1],m[1][2],m[1][3], m[3][1],m[3][2],m[3][3]);
    adj.m[0][3] = -d3(m[0][1],m[0][2],m[0][3], m[1][1],m[1][2],m[1][3], m[2][1],m[2][2],m[2][3]);

    adj.m[1][0] = -d3(m[1][0],m[1][2],m[1][3], m[2][0],m[2][2],m[2][3], m[3][0],m[3][2],m[3][3]);
    adj.m[1][1] = +d3(m[0][0],m[0][2],m[0][3], m[2][0],m[2][2],m[2][3], m[3][0],m[3][2],m[3][3]);
    adj.m[1][2] = -d3(m[0][0],m[0][2],m[0][3], m[1][0],m[1][2],m[1][3], m[3][0],m[3][2],m[3][3]);
    adj.m[1][3] = +d3(m[0][0],m[0][2],m[0][3], m[1][0],m[1][2],m[1][3], m[2][0],m[2][2],m[2][3]);

    adj.m[2][0] = +d3(m[1][0],m[1][1],m[1][3], m[2][0],m[2][1],m[2][3], m[3][0],m[3][1],m[3][3]);
    adj.m[2][1] = -d3(m[0][0],m[0][1],m[0][3], m[2][0],m[2][1],m[2][3], m[3][0],m[3][1],m[3][3]);
    adj.m[2][2] = +d3(m[0][0],m[0][1],m[0][3], m[1][0],m[1][1],m[1][3], m[3][0],m[3][1],m[3][3]);
    adj.m[2][3] = -d3(m[0][0],m[0][1],m[0][3], m[1][0],m[1][1],m[1][3], m[2][0],m[2][1],m[2][3]);

    adj.m[3][0] = -d3(m[1][0],m[1][1],m[1][2], m[2][0],m[2][1],m[2][2], m[3][0],m[3][1],m[3][2]);
    adj.m[3][1] = +d3(m[0][0],m[0][1],m[0][2], m[2][0],m[2][1],m[2][2], m[3][0],m[3][1],m[3][2]);
    adj.m[3][2] = -d3(m[0][0],m[0][1],m[0][2], m[1][0],m[1][1],m[1][2], m[3][0],m[3][1],m[3][2]);
    adj.m[3][3] = +d3(m[0][0],m[0][1],m[0][2], m[1][0],m[1][1],m[1][2], m[2][0],m[2][1],m[2][2]);

    f32 det = m[0][0]*adj.m[0][0] + m[0][1]*adj.m[1][0]
            + m[0][2]*adj.m[2][0] + m[0][3]*adj.m[3][0];
    f32 inv_det = 1.0f / det;

    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = adj.m[i][j] * inv_det;
    return r;
}

/**
 * @brief Build a translation matrix (row-vector: `p' = p * T`).
 * @param t Translation vector.
 */
inline mat4 mat4_translation(vec3 t) {
    mat4 r = mat4_identity();
    r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
    return r;
}

/**
 * @brief Build a non-uniform scale matrix.
 * @param s Scale factors per axis.
 */
inline mat4 mat4_scale(vec3 s) {
    mat4 r{};
    r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z; r.m[3][3] = 1.0f;
    return r;
}

/**
 * @brief Build a rotation matrix around the X axis (row-vector, left-handed).
 * @param angle Rotation in radians. Positive = clockwise looking along +X.
 */
inline mat4 mat4_rotation_x(f32 angle) {
    f32 c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.m[1][1] =  c; r.m[1][2] = s;
    r.m[2][1] = -s; r.m[2][2] = c;
    return r;
}

/**
 * @brief Build a rotation matrix around the Y axis (row-vector, left-handed).
 * @param angle Rotation in radians. Positive = clockwise looking along +Y.
 */
inline mat4 mat4_rotation_y(f32 angle) {
    f32 c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.m[0][0] =  c; r.m[0][2] = -s;
    r.m[2][0] =  s; r.m[2][2] =  c;
    return r;
}

/**
 * @brief Build a rotation matrix around the Z axis (row-vector, left-handed).
 * @param angle Rotation in radians. Positive = clockwise looking along +Z.
 */
inline mat4 mat4_rotation_z(f32 angle) {
    f32 c = std::cos(angle), s = std::sin(angle);
    mat4 r = mat4_identity();
    r.m[0][0] =  c; r.m[0][1] = s;
    r.m[1][0] = -s; r.m[1][1] = c;
    return r;
}

/**
 * @brief Left-handed perspective projection matrix ([0,1] depth, row-vector).
 * @param fov_y  Vertical field of view in radians.
 * @param aspect Width / height ratio.
 * @param near_z Distance to the near clip plane (> 0).
 * @param far_z  Distance to the far clip plane (> near_z).
 */
inline mat4 mat4_perspective_lh(f32 fov_y, f32 aspect, f32 near_z, f32 far_z) {
    f32 cot_y = 1.0f / std::tan(fov_y * 0.5f);
    mat4 r{};
    r.m[0][0] = cot_y / aspect;
    r.m[1][1] = cot_y;
    r.m[2][2] = far_z / (far_z - near_z);
    r.m[2][3] = 1.0f;                                   // w_clip = z_view
    r.m[3][2] = -(near_z * far_z) / (far_z - near_z);
    return r;
}

/**
 * @brief Left-handed look-at view matrix (row-vector convention).
 * @param eye    Camera position in world space.
 * @param target Point the camera looks at.
 * @param up     World-up direction (typically `{0,1,0}`).
 */
inline mat4 mat4_look_at_lh(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);   // forward  (+Z in LH)
    vec3 r = normalize(cross(up, f));   // right    (+X)
    vec3 u = cross(f, r);               // up reorthogonalized (+Y)

    mat4 v{};
    v.m[0][0] = r.x; v.m[0][1] = u.x; v.m[0][2] = f.x;
    v.m[1][0] = r.y; v.m[1][1] = u.y; v.m[1][2] = f.y;
    v.m[2][0] = r.z; v.m[2][1] = u.z; v.m[2][2] = f.z;
    v.m[3][0] = -dot(r, eye);
    v.m[3][1] = -dot(u, eye);
    v.m[3][2] = -dot(f, eye);
    v.m[3][3] = 1.0f;
    return v;
}

} // namespace ge
