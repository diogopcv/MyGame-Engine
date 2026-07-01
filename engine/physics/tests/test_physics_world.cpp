#include <doctest/doctest.h>
#include "core/arena.h"
#include "physics/physics_world.h"
#include <cmath>

namespace {

constexpr float kEps = 1e-2f;

bool approx(float a, float b, float eps = kEps) {
    return std::abs(a - b) < eps;
}

} // namespace

using namespace ge;
using namespace ge::physics;

TEST_CASE("PhysicsWorld: a free-falling body matches the semi-implicit Euler recurrence") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(4, arena.allocator());
        world.gravity = {0, -10.0f, 0};

        u32 id = world.add_body(make_sphere_body({0, 100, 0}, 0.5f, 1.0f));

        f32 dt = 0.1f;
        f32 expected_v = 0.0f, expected_y = 100.0f;
        for (int i = 0; i < 5; ++i) {
            world.step(dt);
            // Same recurrence PhysicsWorld::step() applies: velocity from
            // gravity, then position from the just-updated velocity.
            expected_v += world.gravity.y * dt;
            expected_y += expected_v * dt;
        }

        REQUIRE(approx(world.body(id).linear_velocity.y, expected_v));
        REQUIRE(approx(world.body(id).position.y, expected_y));

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld: a static body never moves regardless of gravity") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(4, arena.allocator());

        u32 id = world.add_body(make_sphere_body({0, 5, 0}, 0.5f, 0.0f)); // mass 0 -> static
        for (int i = 0; i < 60; ++i) world.step(1.0f / 60.0f);

        REQUIRE(approx(world.body(id).position.y, 5.0f));
        REQUIRE(approx(length(world.body(id).linear_velocity), 0.0f));

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld: angular velocity integration matches quat_from_axis_angle for a constant spin") {
    // No gravity/contacts -- isolates the orientation integration formula.
    // Rather than assume an external right-hand-rule convention, this checks
    // internal consistency: integrating a constant world-space angular
    // velocity should agree with directly constructing the equivalent
    // finite rotation via quat_from_axis_angle (the engine's own primitive
    // for "rotate by this angle about this axis").
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(4, arena.allocator());
        world.gravity = {0, 0, 0};

        RigidBody b = make_sphere_body({0, 0, 0}, 0.5f, 1.0f);
        b.angular_velocity = {0, 1.0f, 0}; // 1 rad/s about world Y
        u32 id = world.add_body(b);

        f32 dt = 0.001f;
        for (int i = 0; i < 1000; ++i) world.step(dt); // 1.0s of spin -> total angle 1.0 rad

        quat expected = quat_from_axis_angle({0, 1, 0}, 1.0f);
        quat actual = world.body(id).orientation;

        vec4 v{1, 0, 0, 0};
        vec4 expected_rotated = v * quat_to_matrix(expected);
        vec4 actual_rotated = v * quat_to_matrix(actual);

        REQUIRE(approx(expected_rotated.x, actual_rotated.x, 0.02f));
        REQUIRE(approx(expected_rotated.y, actual_rotated.y, 0.02f));
        REQUIRE(approx(expected_rotated.z, actual_rotated.z, 0.02f));

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld baseline: a dropped sphere settles to rest on static ground without sinking") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(8, arena.allocator());
        world.gravity = {0, -9.81f, 0};

        RigidBody ground = make_box_body({0, 0, 0}, {5, 1, 5}, quat_identity(), 0.0f);
        ground.friction = 0.5f;
        world.add_body(ground);

        RigidBody sphere = make_sphere_body({0, 3, 0}, 0.5f, 1.0f);
        sphere.restitution = 0.0f;
        sphere.friction = 0.5f;
        u32 sid = world.add_body(sphere);

        f32 dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i) world.step(dt); // 5 simulated seconds

        f32 expected_rest_y = 1.0f + 0.5f; // ground top (half-extent 1) + sphere radius
        REQUIRE(approx(world.body(sid).position.y, expected_rest_y, 0.05f));
        REQUIRE(std::abs(world.body(sid).linear_velocity.y) < 0.2f);

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld baseline: a dropped box settles to rest on static ground without sinking") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(8, arena.allocator());
        world.gravity = {0, -9.81f, 0};

        RigidBody ground = make_box_body({0, 0, 0}, {5, 1, 5}, quat_identity(), 0.0f);
        ground.friction = 0.5f;
        world.add_body(ground);

        RigidBody box = make_box_body({0, 3, 0}, {0.5f, 0.5f, 0.5f}, quat_identity(), 1.0f);
        box.restitution = 0.0f;
        box.friction = 0.5f;
        u32 bid = world.add_body(box);

        f32 dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i) world.step(dt);

        f32 expected_rest_y = 1.0f + 0.5f;
        REQUIRE(approx(world.body(bid).position.y, expected_rest_y, 0.05f));
        REQUIRE(std::abs(world.body(bid).linear_velocity.y) < 0.2f);
        // Should settle upright (face contact, no torque to tip it): a small
        // tolerance on the identity quaternion's components.
        REQUIRE(approx(world.body(bid).orientation.w, 1.0f, 0.05f));

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld baseline: a 2-box stack settles without interpenetration or tunneling") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(8, arena.allocator());
        world.gravity = {0, -9.81f, 0};

        RigidBody ground = make_box_body({0, 0, 0}, {5, 1, 5}, quat_identity(), 0.0f);
        ground.friction = 0.6f;
        world.add_body(ground);

        RigidBody box1 = make_box_body({0, 3, 0}, {0.5f, 0.5f, 0.5f}, quat_identity(), 1.0f);
        box1.friction = 0.6f;
        u32 id1 = world.add_body(box1);

        RigidBody box2 = make_box_body({0.05f, 5, 0}, {0.5f, 0.5f, 0.5f}, quat_identity(), 1.0f);
        box2.friction = 0.6f;
        u32 id2 = world.add_body(box2);

        f32 dt = 1.0f / 60.0f;
        for (int i = 0; i < 360; ++i) world.step(dt); // 6 simulated seconds

        f32 expected_y1 = 1.0f + 0.5f;       // ground top + box1 half-height
        f32 expected_y2 = expected_y1 + 1.0f; // box1 top + box2 half-height
        REQUIRE(approx(world.body(id1).position.y, expected_y1, 0.1f));
        REQUIRE(approx(world.body(id2).position.y, expected_y2, 0.1f));
        REQUIRE(std::abs(world.body(id1).linear_velocity.y) < 0.3f);
        REQUIRE(std::abs(world.body(id2).linear_velocity.y) < 0.3f);

        world.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("PhysicsWorld baseline: a dropped character capsule settles to rest on static ground") {
    // Mirrors the sphere/box settling tests, but for the Milestone-5 shape:
    // a rotation-locked capsule like the sandbox's player character.
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        PhysicsWorld world;
        world.init(8, arena.allocator());
        world.gravity = {0, -9.81f, 0};

        RigidBody ground = make_box_body({0, 0, 0}, {5, 1, 5}, quat_identity(), 0.0f); // top at y=1
        ground.friction = 0.5f;
        world.add_body(ground);

        f32 radius = 0.5f, half_height = 0.5f;
        RigidBody capsule = make_capsule_body({0, 4, 0}, quat_identity(), radius, half_height, 1.0f);
        capsule.restitution = 0.0f;
        capsule.friction = 0.5f;
        u32 cid = world.add_body(capsule);

        f32 dt = 1.0f / 60.0f;
        for (int i = 0; i < 300; ++i) world.step(dt); // 5 simulated seconds

        // Rests when the bottom hemisphere's lowest point touches the
        // ground: body center = ground_top + radius + half_height.
        f32 expected_rest_y = 1.0f + radius + half_height;
        REQUIRE(approx(world.body(cid).position.y, expected_rest_y, 0.05f));
        REQUIRE(std::abs(world.body(cid).linear_velocity.y) < 0.2f);
        // Rotation-locked: gravity/impulses never touch angular velocity or orientation.
        REQUIRE(approx(length(world.body(cid).angular_velocity), 0.0f));
        REQUIRE(approx(world.body(cid).orientation.w, 1.0f));

        world.shutdown();
    }
    arena.shutdown();
}
