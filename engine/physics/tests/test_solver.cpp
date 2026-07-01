#include <doctest/doctest.h>
#include "core/arena.h"
#include "physics/rigidbody.h"
#include "physics/solver.h"
#include <cmath>

namespace {

constexpr float kEps = 1e-2f;

bool approx(float a, float b, float eps = kEps) {
    return std::abs(a - b) < eps;
}

} // namespace

using namespace ge;
using namespace ge::physics;

// A head-on sphere-sphere contact has its point on the line through both
// centers, so r_a/r_b are parallel to the normal and every impulse is
// purely linear (no torque) -- exactly the setup needed to hand-verify the
// solver's 1D collision-response math independent of manifold generation.
namespace {

ContactConstraint make_head_on_constraint(u32 body_a, u32 body_b, f32 penetration, f32 restitution) {
    ContactConstraint c;
    c.body_a = body_a;
    c.body_b = body_b;
    c.point = {0, 0, 0};
    c.normal = {1, 0, 0}; // a -> b
    c.penetration = penetration;
    c.restitution = restitution;
    c.friction = 0.0f;
    return c;
}

} // namespace

TEST_CASE("solver: equal-mass elastic head-on collision exchanges velocities") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Array<RigidBody> bodies(arena.allocator());
        RigidBody a = make_sphere_body({-1, 0, 0}, 0.5f, 1.0f);
        a.linear_velocity = {2, 0, 0};
        RigidBody b = make_sphere_body({1, 0, 0}, 0.5f, 1.0f);
        b.linear_velocity = {-2, 0, 0};
        bodies.push(a);
        bodies.push(b);

        Array<ContactConstraint> constraints(arena.allocator());
        constraints.push(make_head_on_constraint(0, 1, 0.0f, 1.0f));

        prepare_constraints(bodies, constraints, 1.0f / 60.0f);
        solve_velocity_constraints(bodies, constraints, 4);

        REQUIRE(approx(bodies[0].linear_velocity.x, -2.0f));
        REQUIRE(approx(bodies[1].linear_velocity.x, 2.0f));
    }
    arena.shutdown();
}

TEST_CASE("solver: equal-mass perfectly inelastic head-on collision stops both bodies") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Array<RigidBody> bodies(arena.allocator());
        RigidBody a = make_sphere_body({-1, 0, 0}, 0.5f, 1.0f);
        a.linear_velocity = {2, 0, 0};
        RigidBody b = make_sphere_body({1, 0, 0}, 0.5f, 1.0f);
        b.linear_velocity = {-2, 0, 0};
        bodies.push(a);
        bodies.push(b);

        Array<ContactConstraint> constraints(arena.allocator());
        constraints.push(make_head_on_constraint(0, 1, 0.0f, 0.0f));

        prepare_constraints(bodies, constraints, 1.0f / 60.0f);
        solve_velocity_constraints(bodies, constraints, 4);

        // Momentum conservation with equal masses and zero restitution: both
        // end up at the momentum-weighted average, which is zero here.
        REQUIRE(approx(bodies[0].linear_velocity.x, 0.0f));
        REQUIRE(approx(bodies[1].linear_velocity.x, 0.0f));
    }
    arena.shutdown();
}

TEST_CASE("solver: a static body is never moved by the impulse") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Array<RigidBody> bodies(arena.allocator());
        RigidBody ground = make_sphere_body({-1, 0, 0}, 0.5f, 0.0f); // mass 0 -> static
        RigidBody ball = make_sphere_body({1, 0, 0}, 0.5f, 1.0f);
        ball.linear_velocity = {-2, 0, 0};
        bodies.push(ground);
        bodies.push(ball);

        Array<ContactConstraint> constraints(arena.allocator());
        constraints.push(make_head_on_constraint(0, 1, 0.0f, 0.0f));

        prepare_constraints(bodies, constraints, 1.0f / 60.0f);
        solve_velocity_constraints(bodies, constraints, 4);

        REQUIRE(approx(bodies[0].linear_velocity.x, 0.0f));
        REQUIRE(approx(bodies[1].linear_velocity.x, 0.0f)); // stopped by the static ground
    }
    arena.shutdown();
}

TEST_CASE("solver: Baumgarte bias pushes an at-rest penetrating body apart") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Array<RigidBody> bodies(arena.allocator());
        RigidBody ground = make_sphere_body({-1, 0, 0}, 0.5f, 0.0f); // static
        RigidBody ball = make_sphere_body({1, 0, 0}, 0.5f, 1.0f);    // at rest, no approach velocity
        bodies.push(ground);
        bodies.push(ball);

        f32 dt = 1.0f / 60.0f;
        Array<ContactConstraint> constraints(arena.allocator());
        constraints.push(make_head_on_constraint(0, 1, 0.05f, 0.0f)); // 0.05 penetration

        prepare_constraints(bodies, constraints, dt);
        solve_velocity_constraints(bodies, constraints, 1);

        // beta=0.2, slop=0.01 (solver.cpp constants): bias = 0.2/dt * (0.05-0.01) = 0.008/dt.
        f32 expected_bias = 0.2f * (0.05f - 0.01f) / dt;
        REQUIRE(approx(bodies[1].linear_velocity.x, expected_bias, 0.1f));
        REQUIRE(bodies[1].linear_velocity.x > 0.0f); // pushed away from the ground, along a -> b
    }
    arena.shutdown();
}
