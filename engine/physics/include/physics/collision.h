#pragma once
#include "physics/shapes.h"

/**
 * @file collision.h
 * @brief Narrowphase collision queries between concrete shape pairs (Layer 3 — Subsystems).
 *
 * See docs/phases/05-physics.md (Milestone 1) for the SAT/closest-point
 * design rationale. General convex-vs-convex separated-distance queries are
 * handled by physics/gjk.h instead.
 */
namespace ge::physics {

/**
 * @brief Result of a narrowphase overlap test.
 *
 * `normal` points from the *first* shape argument toward the *second* (e.g.
 * for `sphere_sphere(a, b)`, from `a` to `b`). It is only meaningful when
 * `intersecting` is true; `penetration` is the overlap depth measured along
 * `normal` and is always >= 0.
 */
struct Contact {
    bool intersecting = false;
    vec3 normal{0, 1, 0};
    f32  penetration = 0.0f;
};

/** @brief Overlap test between two AABBs (touching counts as overlapping). */
bool aabb_overlap(const AABB& a, const AABB& b);

/** @brief Sphere-sphere overlap test with contact normal (a -> b) and penetration depth. */
Contact sphere_sphere(const Sphere& a, const Sphere& b);

/** @brief Sphere-box overlap test with contact normal (sphere -> box) and penetration depth. */
Contact sphere_box(const Sphere& s, const Box& b);

/**
 * @brief Box-box overlap test via the 15-axis SAT (3 + 3 face normals, 9 edge
 *        cross-products), returning the minimum-translation-vector normal
 *        (a -> b) and penetration depth.
 */
Contact box_box(const Box& a, const Box& b);

/**
 * @brief Capsule-box overlap test with contact normal (capsule -> box) and penetration depth.
 *
 * A capsule is the Minkowski sum of its core segment and a sphere, so this
 * finds the closest point on the segment to the box and resolves it exactly
 * like sphere_box().
 */
Contact capsule_box(const Capsule& c, const Box& b);

} // namespace ge::physics
