#include <doctest/doctest.h>
#include "physics/collision.h"
#include "physics/manifold.h"
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

/** @brief True if `points` contains a point within `eps` of `expected` (order-independent check). */
bool has_point_near(const ge::physics::Manifold& m, ge::vec3 expected, float eps = kEps) {
    for (ge::u32 i = 0; i < m.count; ++i)
        if (vec3_eq(m.points[i].point, expected, eps)) return true;
    return false;
}

} // namespace

using namespace ge;
using namespace ge::physics;

// ============================================================================
// sphere_sphere / sphere_box manifolds
// ============================================================================
TEST_CASE("manifold_sphere_sphere: separated spheres yield an invalid manifold") {
    Manifold m = manifold_sphere_sphere({{0, 0, 0}, 1}, {{10, 0, 0}, 1});
    REQUIRE_FALSE(m.valid);
    REQUIRE(m.count == 0);
}

TEST_CASE("manifold_sphere_sphere: overlapping spheres yield the surface midpoint") {
    Manifold m = manifold_sphere_sphere({{0, 0, 0}, 1.0f}, {{1.5f, 0, 0}, 1.0f});
    REQUIRE(m.valid);
    REQUIRE(m.count == 1);
    REQUIRE(approx(m.points[0].penetration, 0.5f));
    // Sphere a's surface toward b: (1,0,0). Sphere b's surface toward a: (0.5,0,0). Midpoint: (0.75,0,0).
    REQUIRE(vec3_eq(m.points[0].point, {0.75f, 0, 0}));
}

TEST_CASE("manifold_sphere_box: resting sphere yields the surface midpoint") {
    Manifold m = manifold_sphere_box({{1.5f, 0, 0}, 1.0f}, Box{{0, 0, 0}, {1, 1, 1}});
    REQUIRE(m.valid);
    REQUIRE(m.count == 1);
    REQUIRE(approx(m.points[0].penetration, 0.5f));
    // Sphere surface toward box: (0.5,0,0). Box face: (1,0,0). Midpoint: (0.75,0,0).
    REQUIRE(vec3_eq(m.points[0].point, {0.75f, 0, 0}));
}

TEST_CASE("manifold_capsule_box: separated capsule yields an invalid manifold") {
    Manifold m = manifold_capsule_box(Capsule{{0, 3, 0}, {0, 4, 0}, 0.5f}, Box{{0, -1, 0}, {5, 1, 5}});
    REQUIRE_FALSE(m.valid);
    REQUIRE(m.count == 0);
}

TEST_CASE("manifold_capsule_box: standing capsule yields the surface midpoint below its bottom hemisphere") {
    // Same geometry as test_collision.cpp's "capsule standing on the ground"
    // case: bottom hemisphere center (0,0.3,0), radius 0.5 -> 0.2 penetration.
    Manifold m = manifold_capsule_box(Capsule{{0, 0.3f, 0}, {0, 1.3f, 0}, 0.5f}, Box{{0, -1, 0}, {5, 1, 5}});
    REQUIRE(m.valid);
    REQUIRE(m.count == 1);
    REQUIRE(approx(m.points[0].penetration, 0.2f));
    // Capsule surface (bottom of hemisphere): (0,-0.2,0). Box surface (top face): (0,0,0). Midpoint: (0,-0.1,0).
    REQUIRE(vec3_eq(m.points[0].point, {0, -0.1f, 0}));
}

// ============================================================================
// box_box manifolds: face contacts
// ============================================================================
TEST_CASE("manifold_box_box: separated boxes yield an invalid manifold") {
    Manifold m = manifold_box_box(Box{{0, 0, 0}, {1, 1, 1}}, Box{{10, 0, 0}, {1, 1, 1}});
    REQUIRE_FALSE(m.valid);
    REQUIRE(m.count == 0);
}

TEST_CASE("manifold_box_box: flush face contact yields the 4 overlap corners") {
    // Ground box (footprint 4x4 in XZ) with a smaller box resting on top,
    // overlapping the ground's +Y face by 0.1 -- fully within the ground's
    // footprint, so clipping shouldn't remove any of the incident box's
    // 4 face corners.
    Box ground{{0, 0, 0}, {2, 1, 2}};
    Box resting{{0, 1.9f, 0}, {1, 1, 1}};
    Manifold m = manifold_box_box(ground, resting);

    REQUIRE(m.valid);
    REQUIRE(vec3_eq(m.normal, {0, 1, 0}));
    REQUIRE(m.count == 4);
    for (u32 i = 0; i < m.count; ++i) REQUIRE(approx(m.points[i].penetration, 0.1f));

    REQUIRE(has_point_near(m, {1, 0.95f, 1}));
    REQUIRE(has_point_near(m, {-1, 0.95f, 1}));
    REQUIRE(has_point_near(m, {-1, 0.95f, -1}));
    REQUIRE(has_point_near(m, {1, 0.95f, -1}));
}

TEST_CASE("manifold_box_box: face contact clips to the true overlap footprint") {
    // Same vertical overlap as above, but the resting box is shifted along X
    // so only part of its footprint is over the ground -- clipping must
    // trim the incident quad down to the actual overlap region.
    Box ground{{0, 0, 0}, {2, 1, 2}};
    Box resting{{2.5f, 1.9f, 0}, {1, 1, 1}};
    Manifold m = manifold_box_box(ground, resting);

    REQUIRE(m.valid);
    REQUIRE(vec3_eq(m.normal, {0, 1, 0}));
    REQUIRE(m.count == 4);
    for (u32 i = 0; i < m.count; ++i) REQUIRE(approx(m.points[i].penetration, 0.1f));

    REQUIRE(has_point_near(m, {2.0f, 0.95f, 1}));
    REQUIRE(has_point_near(m, {1.5f, 0.95f, 1}));
    REQUIRE(has_point_near(m, {1.5f, 0.95f, -1}));
    REQUIRE(has_point_near(m, {2.0f, 0.95f, -1}));
}

// ============================================================================
// box_box manifolds: general invariants across face and edge contacts
// ============================================================================
TEST_CASE("manifold_box_box: every intersecting configuration yields a physically sane manifold") {
    // Reuses the axis-aligned, single-axis-rotated, and compound-rotated
    // (tilt_a/tilt_b) configurations already cross-validated against GJK in
    // test_collision.cpp, so these are known-intersecting. Exact contact
    // geometry for compound rotations isn't hand-derived here (see that
    // file's comment on why); instead this checks the invariants every
    // manifold must satisfy regardless of which SAT axis (face or edge) won.
    quat tilt_a = quat_from_axis_angle(normalize(vec3{1, 1, 0}), 0.5f);
    quat tilt_b = quat_from_axis_angle(normalize(vec3{0, 1, 1}), 0.7f);

    struct Case { Box a, b; };
    Case cases[] = {
        {Box{{0, 0, 0}, {1, 1, 1}}, Box{{1.5f, 0, 0}, {1, 1, 1}}},
        {Box{{0, 0, 0}, {1, 1, 1}}, Box{{0, 1.5f, 0}, {1, 1, 1}, quat_from_axis_angle({1, 0, 0}, 0.3f)}},
        {Box{{0, 0, 0}, {3, 0.2f, 0.2f}, tilt_a}, Box{{0, 0.3f, 0}, {3, 0.2f, 0.2f}, tilt_b}},
        {Box{{0, 0, 0}, {1, 1, 1}, tilt_a}, Box{{1.5f, 1.5f, 1.5f}, {1, 1, 1}, tilt_b}},
    };

    for (const Case& tc : cases) {
        Contact sat = box_box(tc.a, tc.b);
        REQUIRE(sat.intersecting); // precondition: these are all known-overlapping pairs

        Manifold m = manifold_box_box(tc.a, tc.b);
        REQUIRE(m.valid);
        REQUIRE(m.count >= 1);
        REQUIRE(m.count <= Manifold::kMaxPoints);
        REQUIRE(approx(length(m.normal), 1.0f, 1e-2f));
        for (u32 i = 0; i < m.count; ++i)
            REQUIRE(m.points[i].penetration >= -1e-3f);
    }
}
