#pragma once
#include "core/allocator.h"
#include "core/array.h"
#include "physics/broadphase.h"
#include "physics/rigidbody.h"
#include "physics/solver.h"

/**
 * @file physics_world.h
 * @brief Owns rigid bodies and drives the fixed-step simulation pipeline (Layer 3 — Subsystems).
 *
 * See docs/phases/05-physics.md (Milestone 4). Named `PhysicsWorld` (not
 * `World`) to avoid confusion with `ge::world::World` (the ECS) — this class
 * has no dependency on it (ADR-0001); Milestone 5 adds the ECS-facing glue.
 */
namespace ge::physics {

/**
 * @brief Rigid-body simulation: gravity -> broadphase -> narrowphase ->
 *        manifold -> solve -> integrate, one fixed step at a time.
 */
class PhysicsWorld {
public:
    /**
     * @brief Prepare body/pair/constraint storage.
     * @param max_bodies Maximum number of bodies addable via add_body().
     * @param alloc      Allocator for all PhysicsWorld storage.
     */
    void init(u32 max_bodies, Allocator alloc);

    /** @brief Release all storage. */
    void shutdown();

    /** @brief Add a body, returning its stable index (used as the id for body()). */
    u32 add_body(const RigidBody& body);

    RigidBody&       body(u32 id);
    const RigidBody& body(u32 id) const;
    u32               body_count() const;

    /**
     * @brief Advance the simulation by one fixed step.
     *
     * Applies gravity, rebuilds the broadphase from current AABBs, generates
     * narrowphase contact manifolds for every candidate pair, solves the
     * resulting velocity constraints, and integrates positions/orientations
     * (semi-implicit Euler).
     */
    void step(f32 dt);

    vec3 gravity{0, -9.81f, 0};
    u32  velocity_iterations = 8;

private:
    Allocator alloc_;
    Array<RigidBody>* bodies_ = nullptr;
    Broadphase broadphase_;
    Array<Pair>* pairs_ = nullptr;
    Array<ContactConstraint>* constraints_ = nullptr;
};

} // namespace ge::physics
