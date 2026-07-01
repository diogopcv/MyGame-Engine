#pragma once
#include "core/vec.h"
#include "core/quat.h"
#include <cmath>

/**
 * @file shapes.h
 * @brief Collision shape primitives (Layer 3 — Subsystems).
 *
 * Pure data, no behavior. See docs/phases/05-physics.md for the shape set
 * rationale (sphere, box, and later capsule — no general convex hulls).
 */
namespace ge::physics {

/** @brief Axis-aligned bounding box, given by its min/max corners. */
struct AABB {
    vec3 min{};
    vec3 max{};
};

/** @brief Sphere, given by center and radius. */
struct Sphere {
    vec3 center{};
    f32  radius = 0.5f;
};

/** @brief Oriented box, given by center, per-axis half-extents, and orientation. */
struct Box {
    vec3 center{};
    vec3 half_extents{0.5f, 0.5f, 0.5f};
    quat orientation = quat_identity();
};

/** @brief Capsule: a swept sphere along segment [p0, p1] (the Minkowski sum of the segment and a sphere). */
struct Capsule {
    vec3 p0{0, -0.5f, 0};
    vec3 p1{0, 0.5f, 0};
    f32  radius = 0.5f;
};

/** @brief Expand @p a to also cover @p b. */
inline AABB aabb_union(const AABB& a, const AABB& b) {
    return {
        {a.min.x < b.min.x ? a.min.x : b.min.x,
         a.min.y < b.min.y ? a.min.y : b.min.y,
         a.min.z < b.min.z ? a.min.z : b.min.z},
        {a.max.x > b.max.x ? a.max.x : b.max.x,
         a.max.y > b.max.y ? a.max.y : b.max.y,
         a.max.z > b.max.z ? a.max.z : b.max.z},
    };
}

/** @brief Tight AABB enclosing a sphere. */
inline AABB aabb_from_sphere(const Sphere& s) {
    return {s.center - vec3{s.radius, s.radius, s.radius},
            s.center + vec3{s.radius, s.radius, s.radius}};
}

/** @brief World-space AABB enclosing an oriented box (conservative for non-identity rotations). */
inline AABB aabb_from_box(const Box& b) {
    mat4 r = quat_to_matrix(b.orientation);
    vec3 extent{
        std::abs(r.m[0][0]) * b.half_extents.x + std::abs(r.m[1][0]) * b.half_extents.y +
            std::abs(r.m[2][0]) * b.half_extents.z,
        std::abs(r.m[0][1]) * b.half_extents.x + std::abs(r.m[1][1]) * b.half_extents.y +
            std::abs(r.m[2][1]) * b.half_extents.z,
        std::abs(r.m[0][2]) * b.half_extents.x + std::abs(r.m[1][2]) * b.half_extents.y +
            std::abs(r.m[2][2]) * b.half_extents.z,
    };
    return {b.center - extent, b.center + extent};
}

/** @brief Tight AABB enclosing a capsule (its segment's bounding box, expanded by the radius). */
inline AABB aabb_from_capsule(const Capsule& c) {
    vec3 r{c.radius, c.radius, c.radius};
    vec3 seg_min{
        c.p0.x < c.p1.x ? c.p0.x : c.p1.x,
        c.p0.y < c.p1.y ? c.p0.y : c.p1.y,
        c.p0.z < c.p1.z ? c.p0.z : c.p1.z,
    };
    vec3 seg_max{
        c.p0.x > c.p1.x ? c.p0.x : c.p1.x,
        c.p0.y > c.p1.y ? c.p0.y : c.p1.y,
        c.p0.z > c.p1.z ? c.p0.z : c.p1.z,
    };
    return {seg_min - r, seg_max + r};
}

} // namespace ge::physics
