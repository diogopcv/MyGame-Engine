#include <doctest/doctest.h>
#include "physics/gjk.h"
#include "physics/shapes.h"
#include <cmath>

namespace {

constexpr float kEps = 1e-2f;

bool approx(float a, float b, float eps = kEps) {
    return std::abs(a - b) < eps;
}

} // namespace

using namespace ge;
using namespace ge::physics;

TEST_CASE("gjk_distance: separated spheres match the closed-form distance") {
    Sphere a{{0, 0, 0}, 1.0f};
    Sphere b{{10, 0, 0}, 2.0f};
    GjkResult r = gjk_distance(&a, support_sphere, &b, support_sphere);
    REQUIRE_FALSE(r.intersecting);
    REQUIRE(approx(r.distance, 10.0f - 1.0f - 2.0f)); // = 7
}

TEST_CASE("gjk_distance: overlapping spheres report intersecting") {
    Sphere a{{0, 0, 0}, 1.0f};
    Sphere b{{1.0f, 0, 0}, 1.0f};
    GjkResult r = gjk_distance(&a, support_sphere, &b, support_sphere);
    REQUIRE(r.intersecting);
    REQUIRE(approx(r.distance, 0.0f));
}

TEST_CASE("gjk_distance: touching spheres are at (approximately) zero distance") {
    Sphere a{{0, 0, 0}, 1.0f};
    Sphere b{{2.0f, 0, 0}, 1.0f};
    GjkResult r = gjk_distance(&a, support_sphere, &b, support_sphere);
    if (!r.intersecting) REQUIRE(approx(r.distance, 0.0f, 1e-2f));
}

TEST_CASE("gjk_distance: separated axis-aligned boxes match the slab distance") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{5, 0, 0}, {1, 1, 1}};
    GjkResult r = gjk_distance(&a, support_box, &b, support_box);
    REQUIRE_FALSE(r.intersecting);
    REQUIRE(approx(r.distance, 3.0f)); // gap between faces at x=1 and x=4
}

TEST_CASE("gjk_distance: overlapping axis-aligned boxes report intersecting") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{1.5f, 0, 0}, {1, 1, 1}};
    GjkResult r = gjk_distance(&a, support_box, &b, support_box);
    REQUIRE(r.intersecting);
}

TEST_CASE("gjk_distance: separated sphere and box match the closest-point distance") {
    Box b{{0, 0, 0}, {1, 1, 1}};
    Sphere s{{5, 0, 0}, 1.0f};
    GjkResult r = gjk_distance(&s, support_sphere, &b, support_box);
    REQUIRE_FALSE(r.intersecting);
    REQUIRE(approx(r.distance, 3.0f)); // 5 - box_face(1) - sphere_radius(1)
}

TEST_CASE("gjk_distance: sphere resting against a rotated box's corner") {
    Box b{{0, 0, 0}, {1, 1, 1}, quat_from_axis_angle({0, 1, 0}, 0.7853982f)}; // 45 deg about Y
    f32 corner_dist = std::sqrt(2.0f);
    Sphere s{{corner_dist + 2.0f, 0, 0}, 1.0f};
    GjkResult r = gjk_distance(&s, support_sphere, &b, support_box);
    REQUIRE_FALSE(r.intersecting);
    REQUIRE(approx(r.distance, 1.0f)); // gap 2.0 minus sphere radius 1.0
}

TEST_CASE("gjk_distance: diagonally separated boxes") {
    Box a{{0, 0, 0}, {1, 1, 1}};
    Box b{{4, 4, 4}, {1, 1, 1}};
    GjkResult r = gjk_distance(&a, support_box, &b, support_box);
    REQUIRE_FALSE(r.intersecting);
    // Closest points are the two facing corners, each 1 unit (per axis) in
    // from center along the diagonal: gap per axis = 4 - 1 - 1 = 2.
    f32 expected = std::sqrt(2.0f * 2.0f * 3.0f);
    REQUIRE(approx(r.distance, expected));
}
