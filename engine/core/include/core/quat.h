#pragma once
#include "core/mat4.h"
#include <cmath>

namespace ge {

// Unit quaternion: q = (x, y, z, w) where w is the scalar part.
// The canonical rotation representation per ADR-0005.
struct quat { f32 x = 0, y = 0, z = 0, w = 1; };

inline quat quat_identity() { return {0, 0, 0, 1}; }

// Builds a rotation quaternion from a unit axis and an angle in radians.
inline quat quat_from_axis_angle(vec3 axis, f32 angle) {
    f32 s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}

inline f32  length_sq(quat q) { return q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w; }
inline f32  length(quat q)    { return std::sqrt(length_sq(q)); }
inline quat normalize(quat q) {
    f32 inv = 1.0f / length(q);
    return {q.x*inv, q.y*inv, q.z*inv, q.w*inv};
}

// Hamilton product: apply p first, then q (left-to-right composition).
inline quat operator*(quat p, quat q) {
    return {
        p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,
        p.w*q.y - p.x*q.z + p.y*q.w + p.z*q.x,
        p.w*q.z + p.x*q.y - p.y*q.x + p.z*q.w,
        p.w*q.w - p.x*q.x - p.y*q.y - p.z*q.z,
    };
}

// Spherical linear interpolation; always takes the shorter arc.
inline quat slerp(quat q1, quat q2, f32 t) {
    f32 d = q1.x*q2.x + q1.y*q2.y + q1.z*q2.z + q1.w*q2.w;
    if (d < 0.0f) { q2 = {-q2.x, -q2.y, -q2.z, -q2.w}; d = -d; }
    if (d > 0.9995f) {
        quat r = {q1.x + t*(q2.x-q1.x), q1.y + t*(q2.y-q1.y),
                  q1.z + t*(q2.z-q1.z), q1.w + t*(q2.w-q1.w)};
        return normalize(r);
    }
    f32 omega   = std::acos(d);
    f32 inv_sin = 1.0f / std::sin(omega);
    f32 s1 = std::sin((1.0f - t) * omega) * inv_sin;
    f32 s2 = std::sin(t * omega) * inv_sin;
    return {s1*q1.x + s2*q2.x, s1*q1.y + s2*q2.y,
            s1*q1.z + s2*q2.z, s1*q1.w + s2*q2.w};
}

// Converts a unit quaternion to a 4x4 rotation matrix (row-vector convention).
inline mat4 quat_to_matrix(quat q) {
    f32 x2 = q.x*q.x, y2 = q.y*q.y, z2 = q.z*q.z;
    f32 xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    f32 wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;

    mat4 r = mat4_identity();
    r.m[0][0] = 1.0f - 2.0f*(y2+z2); r.m[0][1] = 2.0f*(xy+wz);       r.m[0][2] = 2.0f*(xz-wy);
    r.m[1][0] = 2.0f*(xy-wz);        r.m[1][1] = 1.0f - 2.0f*(x2+z2); r.m[1][2] = 2.0f*(yz+wx);
    r.m[2][0] = 2.0f*(xz+wy);        r.m[2][1] = 2.0f*(yz-wx);        r.m[2][2] = 1.0f - 2.0f*(x2+y2);
    return r;
}

// Recovers a unit quaternion from a rotation matrix (row-vector convention).
// Uses Shepperd's method to pick the numerically largest component first.
inline quat quat_from_matrix(const mat4& m) {
    quat q{};
    f32 trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    if (trace > 0.0f) {
        f32 s = 2.0f * std::sqrt(trace + 1.0f);  // s = 4w
        q.w = 0.25f * s;
        q.x = (m.m[1][2] - m.m[2][1]) / s;
        q.y = (m.m[2][0] - m.m[0][2]) / s;
        q.z = (m.m[0][1] - m.m[1][0]) / s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        f32 s = 2.0f * std::sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]);  // s = 4x
        q.w = (m.m[1][2] - m.m[2][1]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        f32 s = 2.0f * std::sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]);  // s = 4y
        q.w = (m.m[2][0] - m.m[0][2]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        f32 s = 2.0f * std::sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]);  // s = 4z
        q.w = (m.m[0][1] - m.m[1][0]) / s;
        q.x = (m.m[0][2] + m.m[2][0]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

} // namespace ge
