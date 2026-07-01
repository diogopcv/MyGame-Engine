#include "physics/solver.h"
#include <algorithm>
#include <cmath>

namespace ge::physics {

namespace {

constexpr f32 kBaumgarteBeta = 0.2f;
constexpr f32 kPenetrationSlop = 0.01f;
constexpr f32 kRestitutionVelocityThreshold = 1.0f;

/** @brief Build an arbitrary orthonormal basis (t1, t2) perpendicular to `normal`. */
void make_tangent_basis(vec3 normal, vec3& t1, vec3& t2) {
    vec3 helper = std::abs(normal.x) < 0.9f ? vec3{1, 0, 0} : vec3{0, 1, 0};
    t1 = normalize(cross(helper, normal));
    t2 = cross(normal, t1);
}

vec3 point_velocity(const RigidBody& b, vec3 r) {
    return b.linear_velocity + cross(b.angular_velocity, r);
}

f32 effective_mass(const RigidBody& a, const RigidBody& b, vec3 r_a, vec3 r_b, vec3 axis) {
    vec3 ra_x = cross(r_a, axis);
    vec3 rb_x = cross(r_b, axis);
    f32 k = a.inv_mass + b.inv_mass + dot(apply_inv_inertia(a, ra_x), ra_x) +
            dot(apply_inv_inertia(b, rb_x), rb_x);
    return k > 1e-8f ? 1.0f / k : 0.0f;
}

void apply_impulse(RigidBody& a, RigidBody& b, vec3 r_a, vec3 r_b, vec3 impulse) {
    a.linear_velocity -= impulse * a.inv_mass;
    b.linear_velocity += impulse * b.inv_mass;
    a.angular_velocity -= apply_inv_inertia(a, cross(r_a, impulse));
    b.angular_velocity += apply_inv_inertia(b, cross(r_b, impulse));
}

} // namespace

void prepare_constraints(const Array<RigidBody>& bodies, Array<ContactConstraint>& constraints, f32 dt) {
    for (usize i = 0; i < constraints.size(); ++i) {
        ContactConstraint& c = constraints[i];
        const RigidBody& a = bodies[c.body_a];
        const RigidBody& b = bodies[c.body_b];

        c.r_a = c.point - a.position;
        c.r_b = c.point - b.position;
        make_tangent_basis(c.normal, c.tangent1, c.tangent2);

        c.normal_mass = effective_mass(a, b, c.r_a, c.r_b, c.normal);
        c.tangent_mass1 = effective_mass(a, b, c.r_a, c.r_b, c.tangent1);
        c.tangent_mass2 = effective_mass(a, b, c.r_a, c.r_b, c.tangent2);

        f32 vn = dot(point_velocity(b, c.r_b) - point_velocity(a, c.r_a), c.normal);
        f32 restitution_bias = vn < -kRestitutionVelocityThreshold ? -c.restitution * vn : 0.0f;
        f32 baumgarte_bias = (kBaumgarteBeta / dt) * std::max(c.penetration - kPenetrationSlop, 0.0f);
        c.velocity_bias = restitution_bias + baumgarte_bias;

        c.accum_normal = 0.0f;
        c.accum_tangent1 = 0.0f;
        c.accum_tangent2 = 0.0f;
    }
}

void solve_velocity_constraints(Array<RigidBody>& bodies, Array<ContactConstraint>& constraints, u32 iterations) {
    for (u32 iter = 0; iter < iterations; ++iter) {
        for (usize i = 0; i < constraints.size(); ++i) {
            ContactConstraint& c = constraints[i];
            RigidBody& a = bodies[c.body_a];
            RigidBody& b = bodies[c.body_b];

            // Normal impulse: non-penetration constraint, accumulated impulse clamped >= 0.
            {
                f32 vn = dot(point_velocity(b, c.r_b) - point_velocity(a, c.r_a), c.normal);
                f32 lambda = c.normal_mass * (c.velocity_bias - vn);
                f32 new_accum = std::max(c.accum_normal + lambda, 0.0f);
                lambda = new_accum - c.accum_normal;
                c.accum_normal = new_accum;
                apply_impulse(a, b, c.r_a, c.r_b, c.normal * lambda);
            }

            // Friction impulses: box approximation of the Coulomb cone, each
            // tangent axis independently clamped to +-friction * normal impulse.
            f32 max_friction = c.friction * c.accum_normal;
            for (int axis = 0; axis < 2; ++axis) {
                vec3 tangent = axis == 0 ? c.tangent1 : c.tangent2;
                f32 tmass = axis == 0 ? c.tangent_mass1 : c.tangent_mass2;
                f32& accum = axis == 0 ? c.accum_tangent1 : c.accum_tangent2;

                f32 vt = dot(point_velocity(b, c.r_b) - point_velocity(a, c.r_a), tangent);
                f32 lambda = tmass * -vt;
                f32 new_accum = std::clamp(accum + lambda, -max_friction, max_friction);
                lambda = new_accum - accum;
                accum = new_accum;
                apply_impulse(a, b, c.r_a, c.r_b, tangent * lambda);
            }
        }
    }
}

} // namespace ge::physics
