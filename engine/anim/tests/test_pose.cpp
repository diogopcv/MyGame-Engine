#include <doctest/doctest.h>
#include "core/arena.h"
#include "core/transform.h"
#include "anim/pose.h"
#include <cmath>

using namespace ge;
using namespace ge::anim;

namespace {

constexpr f32 kEps = 1e-5f;
constexpr f32 kPi = 3.14159265358979f;

bool approx(f32 a, f32 b, f32 eps = kEps) { return std::abs(a - b) < eps; }
bool approx(vec3 a, vec3 b, f32 eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}
bool approx(quat a, quat b, f32 eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps) && approx(a.w, b.w, eps);
}
bool approx_identity(const mat4& m, f32 eps = kEps) {
    mat4 id = mat4_identity();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!approx(m.m[i][j], id.m[i][j], eps)) return false;
    return true;
}

vec3 transform_point(vec3 p, const mat4& m) {
    vec4 r = vec4{p.x, p.y, p.z, 1.0f} * m;
    return {r.x, r.y, r.z};
}

// The hand-computed 2-bone chain from the phase-doc test plan: joint 0 at the
// origin, joint 1 one unit up its parent's +Y. Inverse bind matrices are the
// inverses of the rest-pose model matrices, computed with core's inverse().
struct TwoBoneFixture {
    Skeleton skel;

    void init(Allocator alloc) {
        skel.init(2, alloc);
        skel.parents[0] = -1;
        skel.parents[1] = 0;
        skel.rest_pose[1].translation = {0, 1, 0};
        skel.inverse_bind[0] = mat4_identity();
        skel.inverse_bind[1] = inverse(mat4_translation({0, 1, 0}));
    }
};

} // namespace

TEST_CASE("pose: blend hits both endpoints exactly and interpolates the midpoint") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Pose a; a.init(1, arena.allocator());
        Pose b; b.init(1, arena.allocator());
        Pose out; out.init(1, arena.allocator());
        b.joints[0].translation = {2, 4, 6};
        b.joints[0].rotation = quat_from_axis_angle({0, 0, 1}, kPi * 0.5f);
        b.joints[0].scale = {3, 1, 1};

        blend(a, b, 0.0f, out);
        REQUIRE(approx(out.joints[0].translation, {0, 0, 0}));
        REQUIRE(approx(out.joints[0].rotation, quat_identity()));
        REQUIRE(approx(out.joints[0].scale, {1, 1, 1}));

        blend(a, b, 1.0f, out);
        REQUIRE(approx(out.joints[0].translation, {2, 4, 6}));
        REQUIRE(approx(out.joints[0].rotation, b.joints[0].rotation));
        REQUIRE(approx(out.joints[0].scale, {3, 1, 1}));

        blend(a, b, 0.5f, out);
        REQUIRE(approx(out.joints[0].translation, {1, 2, 3}));
        REQUIRE(approx(out.joints[0].rotation, quat_from_axis_angle({0, 0, 1}, kPi * 0.25f)));
        REQUIRE(approx(out.joints[0].scale, {2, 1, 1}));

        out.shutdown(); b.shutdown(); a.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("pose: 1D blend weights bracket the parameter and always sum to 1") {
    const f32 anchors[3] = {0.0f, 1.5f, 4.0f}; // idle / walk / run speeds
    f32 w[3];

    // At each anchor, that clip gets the full weight.
    blend_1d_weights(anchors, 3, 0.0f, w);
    REQUIRE(approx(w[0], 1.0f)); REQUIRE(approx(w[1], 0.0f)); REQUIRE(approx(w[2], 0.0f));
    blend_1d_weights(anchors, 3, 1.5f, w);
    REQUIRE(approx(w[0], 0.0f)); REQUIRE(approx(w[1], 1.0f)); REQUIRE(approx(w[2], 0.0f));
    blend_1d_weights(anchors, 3, 4.0f, w);
    REQUIRE(approx(w[0], 0.0f)); REQUIRE(approx(w[1], 0.0f)); REQUIRE(approx(w[2], 1.0f));

    // Between anchors, the two bracketing clips split linearly.
    blend_1d_weights(anchors, 3, 0.75f, w);
    REQUIRE(approx(w[0], 0.5f)); REQUIRE(approx(w[1], 0.5f)); REQUIRE(approx(w[2], 0.0f));
    blend_1d_weights(anchors, 3, 2.75f, w);
    REQUIRE(approx(w[0], 0.0f)); REQUIRE(approx(w[1], 0.5f)); REQUIRE(approx(w[2], 0.5f));

    // Outside the range, the parameter clamps to the nearest anchor.
    blend_1d_weights(anchors, 3, -2.0f, w);
    REQUIRE(approx(w[0], 1.0f));
    blend_1d_weights(anchors, 3, 100.0f, w);
    REQUIRE(approx(w[2], 1.0f));

    // Weights sum to 1 across the whole parameter range (including outside it).
    for (f32 p = -1.0f; p <= 5.0f; p += 0.1f) {
        blend_1d_weights(anchors, 3, p, w);
        REQUIRE(approx(w[0] + w[1] + w[2], 1.0f));
        REQUIRE(w[0] >= 0.0f); REQUIRE(w[1] >= 0.0f); REQUIRE(w[2] >= 0.0f);
    }
}

TEST_CASE("pose: 2-bone chain local->model space matches hand-computed positions") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        TwoBoneFixture f; f.init(arena.allocator());
        Pose pose; pose.init(2, arena.allocator());
        pose_from_rest(f.skel, pose);

        // Animate the root: translate (3,0,0) and rotate 90 degrees about Z.
        pose.joints[0].translation = {3, 0, 0};
        pose.joints[0].rotation = quat_from_axis_angle({0, 0, 1}, kPi * 0.5f);

        mat4 model[2];
        pose_to_model_space(f.skel, pose, model);

        // Root origin lands at its translation.
        REQUIRE(approx(transform_point({0, 0, 0}, model[0]), {3, 0, 0}));
        // Joint 1 sits one unit up the root's rotated +Y: (0,1,0) rotated 90
        // degrees about Z is (-1,0,0) in this row-vector convention, plus the
        // root translation -> (2,0,0).
        REQUIRE(approx(transform_point({0, 0, 0}, model[1]), {2, 0, 0}));

        pose.shutdown();
        f.skel.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("pose: rest pose produces an identity skinning palette") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        TwoBoneFixture f; f.init(arena.allocator());
        Pose pose; pose.init(2, arena.allocator());
        pose_from_rest(f.skel, pose);

        mat4 model[2], palette[2];
        pose_to_model_space(f.skel, pose, model);
        pose_to_palette(f.skel, model, palette);

        REQUIRE(approx_identity(palette[0]));
        REQUIRE(approx_identity(palette[1]));

        pose.shutdown();
        f.skel.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("pose: palette skins bind-pose vertices to hand-computed animated positions") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        TwoBoneFixture f; f.init(arena.allocator());
        Pose pose; pose.init(2, arena.allocator());
        pose_from_rest(f.skel, pose);
        pose.joints[0].translation = {3, 0, 0};
        pose.joints[0].rotation = quat_from_axis_angle({0, 0, 1}, kPi * 0.5f);

        mat4 model[2], palette[2];
        pose_to_model_space(f.skel, pose, model);
        pose_to_palette(f.skel, model, palette);

        // A bind-pose vertex at joint 1's bind position follows joint 1.
        REQUIRE(approx(transform_point({0, 1, 0}, palette[1]), {2, 0, 0}));
        // A vertex 0.5 above joint 1 in bind pose: joint space (0,0.5,0),
        // rotated 90 degrees about Z -> offset (-0.5,0,0) from joint 1's
        // animated position (2,0,0) -> (1.5,0,0).
        REQUIRE(approx(transform_point({0, 1.5f, 0}, palette[1]), {1.5f, 0, 0}));
        // A vertex fully weighted to the static root only translates/rotates
        // with the root.
        REQUIRE(approx(transform_point({1, 0, 0}, palette[0]), {3, 1, 0}));

        pose.shutdown();
        f.skel.shutdown();
    }
    arena.shutdown();
}
