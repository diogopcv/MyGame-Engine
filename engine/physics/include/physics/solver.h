#pragma once
#include "core/array.h"
#include "physics/rigidbody.h"

/**
 * @file solver.h
 * @brief Sequential-impulse velocity solver (Layer 3 — Subsystems).
 *
 * See docs/phases/05-physics.md (Milestone 4). Iterates every contact point
 * applying a non-penetration normal impulse (with restitution and Baumgarte
 * position-correction bias) and two tangential friction impulses (a box
 * approximation of the Coulomb cone), the standard approach popularized by
 * Box2D/Erin Catto's teaching material.
 */
namespace ge::physics {

/**
 * @brief One contact point's velocity constraint.
 *
 * Callers fill `body_a`/`body_b`/`point`/`normal`/`penetration`/
 * `restitution`/`friction`; everything below that is filled by
 * prepare_constraints() and mutated by solve_velocity_constraints().
 */
struct ContactConstraint {
    u32  body_a = 0, body_b = 0; ///< Indices into the bodies array passed to prepare/solve.
    vec3 point{};
    vec3 normal{0, 1, 0}; ///< a -> b, same convention as Contact/Manifold.
    f32  penetration = 0.0f;
    f32  restitution = 0.0f;
    f32  friction = 0.0f;

    // --- Filled by prepare_constraints(); solve_velocity_constraints() reads/writes these. ---
    vec3 r_a{}, r_b{};
    vec3 tangent1{}, tangent2{};
    f32  normal_mass = 0.0f;
    f32  tangent_mass1 = 0.0f;
    f32  tangent_mass2 = 0.0f;
    f32  velocity_bias = 0.0f;
    f32  accum_normal = 0.0f;
    f32  accum_tangent1 = 0.0f;
    f32  accum_tangent2 = 0.0f;
};

/**
 * @brief Precompute each constraint's lever arms, tangent basis, effective
 *        masses, and restitution/Baumgarte velocity bias, and reset its
 *        accumulated impulses to 0 (no warm-starting across steps yet).
 *
 * Must be called once per step, after body positions/orientations for this
 * step are final and before solve_velocity_constraints() — the precomputed
 * terms assume `r_a`/`r_b` stay fixed across the velocity-iteration loop,
 * which holds because positions aren't touched until integration.
 */
void prepare_constraints(const Array<RigidBody>& bodies, Array<ContactConstraint>& constraints, f32 dt);

/**
 * @brief Run `iterations` passes of sequential-impulse velocity solving over `constraints`.
 *
 * Each pass visits every constraint once: a normal impulse clamped to
 * non-negative (contacts only push, never pull) followed by two independent
 * tangential friction impulses clamped to `+-friction * accumulated normal
 * impulse`. Mutates `bodies`' linear/angular velocities in place.
 */
void solve_velocity_constraints(Array<RigidBody>& bodies, Array<ContactConstraint>& constraints, u32 iterations);

} // namespace ge::physics
