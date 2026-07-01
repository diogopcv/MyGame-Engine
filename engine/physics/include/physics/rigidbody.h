#pragma once
#include "core/quat.h"
#include "core/vec.h"

/**
 * @file rigidbody.h
 * @brief Rigid body state + mass properties (Layer 3 — Subsystems).
 *
 * `RigidBody` is plain data owned by `PhysicsWorld` (Milestone 4); ECS
 * component wrappers come in Milestone 5. See docs/phases/05-physics.md.
 */
namespace ge::physics {

/** @brief Which shape a RigidBody uses. */
enum class ShapeType { Sphere, Box, Capsule };

/**
 * @brief A simulated rigid body: shape, transform, velocities, and mass properties.
 *
 * `inv_mass == 0` marks a static (infinite-mass) body — never integrated,
 * never pushed by impulses. `inv_inertia_local` is the inverse inertia
 * tensor's diagonal in body space (valid because the shapes in the current
 * set — sphere, box — are all principal-axis-aligned in their own local
 * frame); see apply_inv_inertia() for how it's used in world space.
 */
struct RigidBody {
    ShapeType shape_type = ShapeType::Sphere;
    vec3 half_extents{0.5f, 0.5f, 0.5f}; ///< Box shape parameters.
    f32  radius = 0.5f;                   ///< Sphere/Capsule shape parameter.
    f32  capsule_half_height = 0.0f;      ///< Capsule: center-to-hemisphere-center distance along local +Y.

    vec3 position{};
    quat orientation = quat_identity();
    vec3 linear_velocity{};
    vec3 angular_velocity{}; ///< World-space.

    f32  inv_mass = 0.0f;       ///< 0 = static.
    vec3 inv_inertia_local{};   ///< Diagonal inverse inertia tensor, body space; 0 = static.

    f32  restitution = 0.0f;
    f32  friction = 0.5f;

    bool is_static() const { return inv_mass == 0.0f; }
};

/**
 * @brief Apply @p body's world-space inverse inertia tensor to a torque-like vector.
 *
 * The tensor is diagonal only in body space, so this rotates @p v into body
 * space (via the body's world-space axes — the rows of quat_to_matrix(),
 * the same convention used throughout physics/shapes.h and physics/gjk.h),
 * scales componentwise, and rotates the result back to world space.
 */
inline vec3 apply_inv_inertia(const RigidBody& body, vec3 v) {
    mat4 r = quat_to_matrix(body.orientation);
    vec3 axis_x{r.m[0][0], r.m[0][1], r.m[0][2]};
    vec3 axis_y{r.m[1][0], r.m[1][1], r.m[1][2]};
    vec3 axis_z{r.m[2][0], r.m[2][1], r.m[2][2]};

    vec3 local{dot(v, axis_x), dot(v, axis_y), dot(v, axis_z)};
    vec3 scaled{
        local.x * body.inv_inertia_local.x,
        local.y * body.inv_inertia_local.y,
        local.z * body.inv_inertia_local.z,
    };
    return axis_x * scaled.x + axis_y * scaled.y + axis_z * scaled.z;
}

/**
 * @brief Build a sphere rigid body with a solid-sphere inertia tensor.
 * @param mass Mass in kg; pass 0 for a static body (infinite mass).
 */
inline RigidBody make_sphere_body(vec3 position, f32 radius, f32 mass) {
    RigidBody b;
    b.shape_type = ShapeType::Sphere;
    b.radius = radius;
    b.position = position;
    if (mass > 0.0f) {
        b.inv_mass = 1.0f / mass;
        f32 i = (2.0f / 5.0f) * mass * radius * radius; // solid sphere: I = 2/5 m r^2
        b.inv_inertia_local = {1.0f / i, 1.0f / i, 1.0f / i};
    }
    return b;
}

/**
 * @brief Build a box rigid body with a solid-cuboid inertia tensor.
 * @param mass Mass in kg; pass 0 for a static body (infinite mass).
 */
inline RigidBody make_box_body(vec3 position, vec3 half_extents, quat orientation, f32 mass) {
    RigidBody b;
    b.shape_type = ShapeType::Box;
    b.half_extents = half_extents;
    b.position = position;
    b.orientation = orientation;
    if (mass > 0.0f) {
        b.inv_mass = 1.0f / mass;
        vec3 h = half_extents;
        // Solid cuboid, full extents (2h): I_xx = m/12 * ((2h.y)^2 + (2h.z)^2) = m/3 * (h.y^2 + h.z^2).
        f32 ixx = (mass / 3.0f) * (h.y * h.y + h.z * h.z);
        f32 iyy = (mass / 3.0f) * (h.x * h.x + h.z * h.z);
        f32 izz = (mass / 3.0f) * (h.x * h.x + h.y * h.y);
        b.inv_inertia_local = {1.0f / ixx, 1.0f / iyy, 1.0f / izz};
    }
    return b;
}

/**
 * @brief Build a capsule rigid body for a controllable character.
 *
 * Rotation is always locked (`inv_inertia_local = 0`), even when `mass > 0`:
 * a player-controlled character shouldn't tip over from solver torques the
 * way a thrown/dropped object would. Its facing is driven directly by the
 * gameplay system that owns it instead (see docs/phases/05-physics.md,
 * Milestone 5). Pass `mass == 0` for a non-player, non-collidable-by-nothing
 * static capsule if one is ever needed.
 *
 * @param orientation Sets the capsule's initial resting orientation only —
 *                     rotation locking means it never changes on its own.
 * @param half_height Center-to-hemisphere-center distance along local +Y
 *                     (so total tip-to-tip length is `2 * (half_height + radius)`).
 */
inline RigidBody make_capsule_body(vec3 position, quat orientation, f32 radius, f32 half_height,
                                    f32 mass) {
    RigidBody b;
    b.shape_type = ShapeType::Capsule;
    b.radius = radius;
    b.capsule_half_height = half_height;
    b.position = position;
    b.orientation = orientation;
    if (mass > 0.0f) b.inv_mass = 1.0f / mass;
    return b;
}

} // namespace ge::physics
