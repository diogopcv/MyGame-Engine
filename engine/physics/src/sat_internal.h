#pragma once
#include "physics/shapes.h"

/**
 * @file sat_internal.h
 * @brief Shared box-box SAT internals (private to ge_physics's src/, not a public header).
 *
 * physics/collision.h's box_box() only needs the boolean/normal/penetration
 * summary; physics/manifold.h's manifold_box_box() additionally needs to
 * know *which* of the 15 axes won (a face of A, a face of B, or an edge
 * pair) to decide between face clipping and edge-edge closest-point contact
 * generation. Both call into this one SAT implementation so the two never
 * disagree about which axis is the true separating/MTV axis.
 */
namespace ge::physics::detail {

/** @brief The box's 3 local axes, expressed as unit vectors in world space. */
struct BoxAxes { vec3 x, y, z; };

/** @brief Compute @p box's local axes in world space (rows of its rotation matrix). */
inline BoxAxes box_world_axes(const Box& box) {
    mat4 r = quat_to_matrix(box.orientation);
    return {
        {r.m[0][0], r.m[0][1], r.m[0][2]},
        {r.m[1][0], r.m[1][1], r.m[1][2]},
        {r.m[2][0], r.m[2][1], r.m[2][2]},
    };
}

/** @brief Index into a BoxAxes/half-extents triple by 0/1/2 without a switch at each call site. */
inline vec3 axis_by_index(const BoxAxes& a, int i) { return i == 0 ? a.x : (i == 1 ? a.y : a.z); }
inline f32  half_extent_by_index(const Box& b, int i) {
    return i == 0 ? b.half_extents.x : (i == 1 ? b.half_extents.y : b.half_extents.z);
}

/** @brief Which of the 15 SAT axes produced the minimum-translation-vector. */
enum class SatAxisType { FaceA, FaceB, Edge };

/**
 * @brief Full box-box SAT result, including which axis won.
 *
 * `normal`/`penetration` are the same MTV summary `collision.h`'s `box_box()`
 * exposes. When `axis_type` is `FaceA`/`FaceB`, `index_a`/`index_b`
 * (respectively) is the winning local face axis (0/1/2). When `Edge`,
 * `index_a` is A's edge axis and `index_b` is B's edge axis (the winning
 * axis was `cross(A.axis[index_a], B.axis[index_b])`).
 */
struct SatResult {
    bool intersecting = false;
    SatAxisType axis_type = SatAxisType::FaceA;
    int index_a = 0;
    int index_b = 0;
    vec3 normal{0, 1, 0}; // a -> b
    f32  penetration = 0.0f;
};

SatResult box_box_sat(const Box& a, const Box& b);

} // namespace ge::physics::detail
