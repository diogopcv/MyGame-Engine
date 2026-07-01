#pragma once
#include "core/vec.h"
#include "core/quat.h"
#include "core/reflect.h"

/**
 * @file components.h
 * @brief Baseline gameplay components (Layer 4 — Frameworks).
 */
namespace ge::world {

/**
 * @brief Position/rotation/scale of an entity in world space.
 *
 * Reflected via GE_REFLECT so it round-trips through core/serialize.h
 * unmodified (see docs/phases/04-world-gameplay.md).
 */
struct Transform {
    vec3 position = {0, 0, 0};
    quat rotation = quat_identity();
    vec3 scale    = {1, 1, 1};

    GE_REFLECT_BEGIN(Transform)
        GE_FIELD(position, Vec3)
        GE_FIELD(rotation, Quat)
        GE_FIELD(scale, Vec3)
    GE_REFLECT_END(Transform)
};

/**
 * @brief Tags an entity as player-movable and carries its movement tunables.
 *
 * The entity's yaw/pitch are stored here (not derived from `Transform`'s
 * quaternion) because the movement system accumulates them directly from
 * mouse-look input, exactly like `gfx::Camera` does.
 */
struct CharacterController {
    f32 move_speed       = 3.0f;
    f32 look_sensitivity = 0.003f;
    f32 yaw              = 0.0f;
    f32 pitch            = 0.0f;
};

/** @brief Sentinel `RigidBodyRef::body_id` value meaning "no rigid body". */
constexpr u32 kInvalidBodyId = ~0u;

/**
 * @brief Links an entity to a rigid body owned by a `ge::physics::PhysicsWorld`.
 *
 * Deliberately just an opaque index, not a copy of (or pointer/reference
 * into) any `ge::physics` type: `ge_world` stays independent of `ge_physics`
 * (ADR-0001 permits Layer 4 depending on Layer 3, but nothing in the ECS
 * itself needs to — only the gameplay code that owns both a `World` and a
 * `PhysicsWorld` does). See docs/phases/05-physics.md (Milestone 5): the
 * sandbox is the one place that resolves this id against its `PhysicsWorld`,
 * mirroring how `system_move_player` already lives there rather than here.
 */
struct RigidBodyRef {
    u32 body_id = kInvalidBodyId;
};

} // namespace ge::world
