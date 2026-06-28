#include <doctest/doctest.h>
#include "gfx/camera.h"
#include <cmath>

using namespace ge;
using namespace ge::gfx;

TEST_CASE("Camera: default forward is +Z (left-handed)") {
    Camera cam;
    cam.yaw   = 0.0f;
    cam.pitch = 0.0f;
    vec3 fwd = cam.forward();
    CHECK(std::abs(fwd.x) < 0.001f);
    CHECK(std::abs(fwd.y) < 0.001f);
    CHECK(fwd.z > 0.9f);
}

TEST_CASE("Camera: yaw rotates forward around Y") {
    Camera cam;
    cam.yaw   = 3.14159265f / 2.0f; // 90 degrees -> should face +X
    cam.pitch = 0.0f;
    vec3 fwd = cam.forward();
    CHECK(fwd.x > 0.9f);
    CHECK(std::abs(fwd.z) < 0.1f);
}

TEST_CASE("Camera: pitch tilts forward up/down") {
    Camera cam;
    cam.yaw   = 0.0f;
    cam.pitch = 0.5f; // tilt up
    vec3 fwd = cam.forward();
    CHECK(fwd.y > 0.4f);
}

TEST_CASE("Camera: fly update moves position") {
    Camera cam;
    cam.position = {0, 0, 0};
    cam.yaw   = 0.0f;
    cam.pitch = 0.0f;

    camera_fly_update(cam, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 5.0f);

    // Should have moved forward (+Z in left-handed when yaw=0)
    CHECK(cam.position.z > 4.0f);
}

TEST_CASE("Camera: view and projection matrices are valid") {
    Camera cam;
    mat4 v = cam.view_matrix();
    mat4 p = cam.proj_matrix(16.0f / 9.0f);

    // View matrix should have non-zero determinant-like behavior
    CHECK(v.m[3][3] != 0.0f);
    // Projection near/far should produce finite values
    CHECK(std::isfinite(p.m[0][0]));
    CHECK(std::isfinite(p.m[2][2]));
}
