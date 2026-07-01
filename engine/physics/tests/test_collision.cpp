#include <doctest/doctest.h>
#include "physics/collision.h"
#include "physics/gjk.h"
#include "physics/shapes.h"
#include <cmath>

namespace {

constexpr float kEps = 1e-3f;

bool approx(float a, float b, float eps = kEps) {
    return std::abs(a - b) < eps;
}

bool vec3_eq(ge::vec3 a, ge::vec3 b, float eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}

} // namespace

using namespace ge;
using namespace ge::physics;

// ============================================================================
// AABB
// ============================================================================
TEST_CASE("aabb_overlap: separated boxes don't overlap") {
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{5, 5, 5}, {6, 6, 6}};
    REQUIRE_FALSE(aabb_overlap(a, b));
    REQUIRE_FALSE(aabb_overlap(b, a));
}

TEST_CASE("aabb_overlap: overlapping boxes overlap") {
    AABB a{{0, 0, 0}, {2, 2, 2}};
    AABB b{{1, 1, 1}, {3, 3, 3}};
    REQUIRE(aabb_overlap(a, b));
    REQUIRE(aabb_overlap(b, a));
}

TEST_CASE("aabb_overlap: exactly touching boxes count as overlapping") {
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 0, 0}, {2, 1, 1}};
    REQUIRE(aabb_overlap(a, b));
}

// ============================================================================
// sphere_sphere
// ============================================================================
TEST_CASE("sphere_sphere: separated spheres don't intersect") {
    Sphere a{{0, 0, 0}, 1.0f};
    Sphere b{{5, 0, 0}, 1.0f};
    Contact c = sphere_sphere(a, b);
    REQUIRE_FALSE(c.intersecting);
}

TEST_CASE("sphere_sphere: overlapping spheres report normal and penetration") {
    Sphere a{{0, 0, 0}, 1.0f};
    Sphere b{{1.5f, 0, 0}, 1.0f};
    Contact c = sphere_sphere(a, b);
    REQUIRE(c.intersecting);
    REQUIRE(vec3_eq(c.normal, {1, 0, 0}));
    REQUIRE(approx(c.penetration, 0.5f)); // r_a + r_b - dist = 1+1-1.5
}

TEST_CASE("sphere_sphere: coincident centers still produce a valid contact") {
    Sphere a{{2, 2, 2}, 1.0f};
    Sphere b{{2, 2, 2}, 1.0f};
    Contact c = sphere_sphere(a, b);
    REQUIRE(c.intersecting);
    REQUIRE(approx(c.penetration, 2.0f));
    REQUIRE(approx(length(c.normal), 1.0f)); // degenerate direction still unit-length
}

// ============================================================================
// sphere_box
// ============================================================================
TEST_CASE("sphere_box: separated sphere and box don't intersect") {
    Sphere s{{10, 0, 0}, 1.0f};
    Box b{{0, 0, 0}, {1, 1, 1}};
    Contact c = sphere_box(s, b);
    REQUIRE_FALSE(c.intersecting);
}

TEST_CASE("sphere_box: sphere resting against a face") {
    Box b{{0, 0, 0}, {1, 1, 1}};
    Sphere s{{1.5f, 0, 0}, 1.0f}; // box face at x=1, sphere's near edge reaches to x=0.5
    Contact c = sphere_box(s, b);
    REQUIRE(c.intersecting);
    REQUIRE(vec3_eq(c.normal, {-1, 0, 0})); // sphere -> box surface convention
    REQUIRE(approx(c.penetration, 0.5f));
}

TEST_CASE("sphere_box: rotated box exposes its corner, not the unrotated face, to the sphere") {
    Box b{{0, 0, 0}, {1, 1, 1}, quat_from_axis_angle({0, 1, 0}, 0.7853982f)}; // 45 deg about Y
    // The box's local +X/+Z corner rotates onto the world +X axis at distance
    // sqrt(2) from the center. If the implementation ignored orientation and
    // treated the box as axis-aligned, it would (incorrectly) use the
    // unrotated face distance of 1.0 instead, giving a penetration of 1.2
    // rather than the true 0.2.
    f32 corner_dist = std::sqrt(2.0f);
    Sphere s{{corner_dist + 1.0f, 0, 0}, 1.2f}; // 1.0 gap from the corner, radius 1.2 -> overlap by 0.2
    Contact c = sphere_box(s, b);
    REQUIRE(c.intersecting);
    REQUIRE(vec3_eq(c.normal, {-1, 0, 0}));
    REQUIRE(approx(c.penetration, 0.2f, 1e-2f));
}

TEST_CASE("sphere_box: sphere center embedded in the box") {
    Box b{{0, 0, 0}, {2, 1, 1}};
    Sphere s{{0, 0, 0}, 0.5f}; // dead center, nearest face is +/-Y or +/-Z at distance 1
    Contact c = sphere_box(s, b);
    REQUIRE(c.intersecting);
    REQUIRE(approx(c.penetration, 1.5f)); // 1.0 (face dist) + 0.5 (radius)
    REQUIRE(approx(length(c.normal), 1.0f));
}

// ============================================================================
// box_box (SAT)
// ============================================================================
TEST_CASE("box_box: separated axis-aligned boxes don't intersect") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{5, 0, 0}, {1, 1, 1}};
    Contact c = box_box(a, b);
    REQUIRE_FALSE(c.intersecting);
}

TEST_CASE("box_box: exactly touching axis-aligned boxes intersect with zero penetration") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{2, 0, 0}, {1, 1, 1}};
    Contact c = box_box(a, b);
    REQUIRE(c.intersecting);
    REQUIRE(approx(c.penetration, 0.0f));
    REQUIRE(vec3_eq(c.normal, {1, 0, 0}));
}

TEST_CASE("box_box: overlapping axis-aligned boxes report face normal and depth") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{1.5f, 0, 0}, {1, 1, 1}};
    Contact c = box_box(a, b);
    REQUIRE(c.intersecting);
    REQUIRE(vec3_eq(c.normal, {1, 0, 0}));
    REQUIRE(approx(c.penetration, 0.5f)); // faces at x=1 and x=0.5
}

TEST_CASE("box_box: a resting stack overlaps with a vertical face normal") {
    Box bottom{{0, 0, 0}, {1, 1, 1}};
    Box top{{0, 1.9f, 0}, {1, 1, 1}}; // slight overlap on Y
    Contact c = box_box(bottom, top);
    REQUIRE(c.intersecting);
    REQUIRE(vec3_eq(c.normal, {0, 1, 0}));
    REQUIRE(approx(c.penetration, 0.1f));
}

TEST_CASE("box_box: separated rotated boxes report no intersection") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{0, 5, 0}, {1, 1, 1}, quat_from_axis_angle({1, 0, 0}, 0.5f)};
    Contact c = box_box(a, b);
    REQUIRE_FALSE(c.intersecting);
}

// The 9 edge-cross-product axes only matter for compound rotations (a single
// shared rotation axis keeps the cross-product axes degenerate with a face
// axis, so a hand-picked case can't cleanly isolate them). Rather than derive
// exact penetration depths by hand for that case, cross-check the SAT
// boolean outcome against GJK -- an independently implemented algorithm
// (physics/gjk.h) that answers the same "do these convex shapes overlap"
// question via a completely different code path (simplex evolution, not
// axis projection). Agreement across compound-rotated configurations is
// strong evidence both are correct; disagreement would point at a bug in
// one of them.
TEST_CASE("box_box: SAT agrees with GJK across compound-rotated configurations") {
    struct Case { Box a, b; };
    quat tilt_a = quat_from_axis_angle(normalize(vec3{1, 1, 0}), 0.5f);
    quat tilt_b = quat_from_axis_angle(normalize(vec3{0, 1, 1}), 0.7f);

    Case cases[] = {
        // Long thin rods crossing near-perpendicular with a compound tilt:
        // the classic configuration where only an edge axis separates them.
        {Box{{0, 0, 0}, {3, 0.2f, 0.2f}, tilt_a}, Box{{0, 0.3f, 0}, {3, 0.2f, 0.2f}, tilt_b}},
        {Box{{0, 0, 0}, {3, 0.2f, 0.2f}, tilt_a}, Box{{0, 1.0f, 0}, {3, 0.2f, 0.2f}, tilt_b}},
        {Box{{0, 0, 0}, {1, 1, 1}, tilt_a}, Box{{1.5f, 1.5f, 1.5f}, {1, 1, 1}, tilt_b}},
        {Box{{0, 0, 0}, {1, 1, 1}, tilt_a}, Box{{3, 3, 3}, {1, 1, 1}, tilt_b}},
    };

    for (const Case& tc : cases) {
        Contact sat = box_box(tc.a, tc.b);
        GjkResult gjk = gjk_distance(&tc.a, support_box, &tc.b, support_box);
        REQUIRE(sat.intersecting == gjk.intersecting);
        if (sat.intersecting) {
            // GJK reports 0 distance once the origin is enclosed; SAT's
            // penetration should be a small, non-negative depth consistent
            // with genuine (not wildly exaggerated) overlap.
            REQUIRE(sat.penetration >= 0.0f);
        } else {
            // Separated: SAT's box_box() default-constructs a non-intersecting
            // Contact, so there's nothing further to compare numerically --
            // the boolean agreement above is the assertion.
            REQUIRE(gjk.distance > 0.0f);
        }
    }
}

// ============================================================================
// capsule_box
// ============================================================================
TEST_CASE("capsule_box: separated capsule and box don't intersect") {
    Box ground{{0, -1, 0}, {5, 1, 5}}; // top face at y=0
    Capsule c{{0, 3, 0}, {0, 4, 0}, 0.5f};
    Contact contact = capsule_box(c, ground);
    REQUIRE_FALSE(contact.intersecting);
}

TEST_CASE("capsule_box: capsule standing on the ground reports the vertical penetration") {
    Box ground{{0, -1, 0}, {5, 1, 5}}; // top face at y=0
    // Bottom hemisphere center at y=0.3, radius 0.5 -> lowest point at
    // y=-0.2, i.e. 0.2 below the ground's top face.
    Capsule c{{0, 0.3f, 0}, {0, 1.3f, 0}, 0.5f};
    Contact contact = capsule_box(c, ground);
    REQUIRE(contact.intersecting);
    REQUIRE(vec3_eq(contact.normal, {0, -1, 0})); // capsule -> box: box is below
    REQUIRE(approx(contact.penetration, 0.2f));
}

TEST_CASE("capsule_box: capsule resting exactly flush has zero penetration") {
    Box ground{{0, -1, 0}, {5, 1, 5}};
    // Bottom hemisphere center at y=0.5, radius 0.5 -> lowest point exactly at y=0.
    Capsule c{{0, 0.5f, 0}, {0, 1.5f, 0}, 0.5f};
    Contact contact = capsule_box(c, ground);
    REQUIRE(contact.intersecting);
    REQUIRE(approx(contact.penetration, 0.0f));
}

TEST_CASE("capsule_box: SAT-style result agrees with GJK across tilted/offset configurations") {
    // capsule_box() isn't SAT (it's an alternating-projection closest-point
    // method, new in this milestone), so cross-check its boolean outcome
    // against GJK -- an independent algorithm using physics/gjk.h's own
    // support_capsule/support_box -- the same strategy used in Milestone 1
    // for box_box(), and valuable here specifically because the
    // alternating-projection code hasn't been exercised by any other test.
    struct Case { Capsule c; Box b; };
    quat tilt = quat_from_axis_angle(normalize(vec3{1, 0, 1}), 0.6f);

    Case cases[] = {
        {Capsule{{0, 0.3f, 0}, {0, 1.3f, 0}, 0.5f}, Box{{0, -1, 0}, {5, 1, 5}}},
        {Capsule{{0, 3, 0}, {0, 4, 0}, 0.5f}, Box{{0, -1, 0}, {5, 1, 5}}},
        {Capsule{{2, 0.2f, 0}, {2, 1, 0}, 0.6f}, Box{{0, 0, 0}, {1, 1, 1}, tilt}},
        {Capsule{{0.5f, 0.5f, 0.5f}, {1.5f, 1.5f, 1.5f}, 0.4f}, Box{{0, 0, 0}, {1, 1, 1}, tilt}},
        {Capsule{{5, 5, 5}, {6, 6, 6}, 0.5f}, Box{{0, 0, 0}, {1, 1, 1}, tilt}},
    };

    for (const Case& tc : cases) {
        Contact contact = capsule_box(tc.c, tc.b);
        GjkResult gjk = gjk_distance(&tc.c, support_capsule, &tc.b, support_box);
        REQUIRE(contact.intersecting == gjk.intersecting);
        if (contact.intersecting) {
            REQUIRE(contact.penetration >= 0.0f);
        } else {
            REQUIRE(gjk.distance > 0.0f);
        }
    }
}
