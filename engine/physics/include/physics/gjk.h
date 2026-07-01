#pragma once
#include "physics/shapes.h"

/**
 * @file gjk.h
 * @brief Support-function-based GJK distance query (Layer 3 — Subsystems).
 *
 * General convex-vs-convex separated-distance query, used where a closed-form
 * test isn't available (box-box uses SAT instead; see physics/collision.h).
 * See docs/phases/05-physics.md (Milestone 1).
 */
namespace ge::physics {

/** @brief Farthest point on a shape's boundary along world-space direction @p dir. */
using SupportFn = vec3 (*)(const void* shape, vec3 dir);

/** @brief Support function for Sphere (@p shape must point to a `const Sphere`). */
vec3 support_sphere(const void* shape, vec3 dir);

/** @brief Support function for Box (@p shape must point to a `const Box`). */
vec3 support_box(const void* shape, vec3 dir);

/** @brief Support function for Capsule (@p shape must point to a `const Capsule`). */
vec3 support_capsule(const void* shape, vec3 dir);

/** @brief Result of a GJK distance query. */
struct GjkResult {
    bool intersecting = false; ///< True if the shapes overlap (origin enclosed by the Minkowski difference).
    f32  distance = 0.0f;      ///< Separation distance; 0 when `intersecting` is true.
};

/**
 * @brief Compute the separation distance between two convex shapes via GJK.
 *
 * Iteratively evolves a simplex inside the Minkowski difference `A - B`
 * toward the origin (Ericson-style closest-point-to-origin simplex
 * reduction for the point/segment/triangle/tetrahedron cases), terminating
 * either when the origin is enclosed (shapes overlap) or when the support
 * function stops making progress toward it (shapes are separated by
 * `distance`).
 *
 * @param shape_a   Opaque pointer passed to @p support_a.
 * @param support_a Support function for shape A.
 * @param shape_b   Opaque pointer passed to @p support_b.
 * @param support_b Support function for shape B.
 */
GjkResult gjk_distance(const void* shape_a, SupportFn support_a,
                        const void* shape_b, SupportFn support_b);

} // namespace ge::physics
