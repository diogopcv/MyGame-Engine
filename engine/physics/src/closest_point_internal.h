#pragma once
#include "physics/shapes.h"
#include "sat_internal.h"
#include <algorithm>

/**
 * @file closest_point_internal.h
 * @brief Shared closest-point routines (private to ge_physics's src/, not a public header).
 *
 * Used by capsule-box narrowphase/manifold generation: a capsule is the
 * Minkowski sum of a segment and a sphere, so capsule-vs-box reduces to
 * "closest point on the segment to the box" treated like a sphere center
 * (see collision.cpp's point_radius_vs_box()).
 */
namespace ge::physics::detail {

/** @brief Closest point on @p box (as a solid volume) to world-space point @p p. */
inline vec3 closest_point_on_box(const Box& box, vec3 p) {
    BoxAxes ax = box_world_axes(box);
    vec3 d = p - box.center;
    vec3 local{dot(d, ax.x), dot(d, ax.y), dot(d, ax.z)};
    vec3 clamped{
        std::clamp(local.x, -box.half_extents.x, box.half_extents.x),
        std::clamp(local.y, -box.half_extents.y, box.half_extents.y),
        std::clamp(local.z, -box.half_extents.z, box.half_extents.z),
    };
    return box.center + ax.x * clamped.x + ax.y * clamped.y + ax.z * clamped.z;
}

/** @brief Closest point on segment [a, b] to world-space point @p p. */
inline vec3 closest_point_on_segment(vec3 a, vec3 b, vec3 p) {
    vec3 ab = b - a;
    f32 denom = dot(ab, ab);
    if (denom < 1e-12f) return a;
    f32 t = std::clamp(dot(p - a, ab) / denom, 0.0f, 1.0f);
    return a + ab * t;
}

/**
 * @brief Closest points between segment [seg_a, seg_b] and a solid box.
 *
 * Alternating projection: each step projects the current guess onto the box,
 * then projects that result back onto the segment. Both projections are
 * onto convex sets, so the distance between the two guesses is
 * non-increasing and converges to the true minimum; a handful of iterations
 * is enough for shapes this simple (segment vs. box).
 */
inline void closest_points_segment_box(vec3 seg_a, vec3 seg_b, const Box& box,
                                        vec3& on_segment, vec3& on_box) {
    vec3 t = seg_a;
    on_box = closest_point_on_box(box, t);
    for (int i = 0; i < 6; ++i) {
        t = closest_point_on_segment(seg_a, seg_b, on_box);
        on_box = closest_point_on_box(box, t);
    }
    on_segment = t;
}

} // namespace ge::physics::detail
