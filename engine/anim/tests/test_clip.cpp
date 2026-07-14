#include <doctest/doctest.h>
#include "core/arena.h"
#include "anim/clip.h"
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

// One animated joint (translation keys at t = 0/1/2, rotation keys at
// t = 0/1) and one fully untracked joint whose rest pose is non-identity —
// the shared fixture for the sampling tests below.
struct ClipFixture {
    Skeleton skel;
    AnimationClip clip;
    Pose pose;

    void init(Allocator alloc) {
        skel.init(2, alloc);
        skel.rest_pose[1].translation = {5, 6, 7};
        skel.rest_pose[1].scale = {2, 2, 2};

        clip.init(2, alloc);
        clip.duration = 2.0f;

        Vec3Track& t = clip.alloc_translation_keys(0, 3);
        t.times[0] = 0.0f; t.values[0] = {0, 0, 0};
        t.times[1] = 1.0f; t.values[1] = {2, 0, 0};
        t.times[2] = 2.0f; t.values[2] = {2, 4, 0};

        QuatTrack& r = clip.alloc_rotation_keys(0, 2);
        r.times[0] = 0.0f; r.values[0] = quat_identity();
        r.times[1] = 1.0f; r.values[1] = quat_from_axis_angle({0, 1, 0}, kPi * 0.5f);

        pose.init(2, alloc);
    }

    void shutdown() {
        pose.shutdown();
        clip.shutdown();
        skel.shutdown();
    }
};

} // namespace

TEST_CASE("clip: sampling at exact keyframe times returns the exact key values") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ClipFixture f; f.init(arena.allocator());

        sample(f.clip, f.skel, 0.0f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {0, 0, 0}));
        REQUIRE(approx(f.pose.joints[0].rotation, quat_identity()));

        sample(f.clip, f.skel, 1.0f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 0, 0}));
        REQUIRE(approx(f.pose.joints[0].rotation, quat_from_axis_angle({0, 1, 0}, kPi * 0.5f)));

        sample(f.clip, f.skel, 2.0f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 4, 0}));

        f.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("clip: mid-keyframe sampling interpolates (lerp translation, slerp rotation)") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ClipFixture f; f.init(arena.allocator());

        sample(f.clip, f.skel, 0.5f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {1, 0, 0}));
        // Slerp halfway from identity to a 90-degree rotation is exactly the
        // 45-degree rotation about the same axis.
        REQUIRE(approx(f.pose.joints[0].rotation, quat_from_axis_angle({0, 1, 0}, kPi * 0.25f)));

        sample(f.clip, f.skel, 1.5f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 2, 0}));
        // Rotation's last key is at t=1: clamp to the last key past track end.
        REQUIRE(approx(f.pose.joints[0].rotation, quat_from_axis_angle({0, 1, 0}, kPi * 0.5f)));

        f.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("clip: looping wraps time by the clip duration") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ClipFixture f; f.init(arena.allocator());

        // duration = 2: t = 2.5 wraps to 0.5, t = 5 wraps to 1.
        sample(f.clip, f.skel, 2.5f, true, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {1, 0, 0}));

        sample(f.clip, f.skel, 5.0f, true, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 0, 0}));

        // Negative time wraps forward: t = -0.5 -> 1.5.
        sample(f.clip, f.skel, -0.5f, true, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 2, 0}));

        f.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("clip: non-looping clamps to the first/last key outside the clip") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ClipFixture f; f.init(arena.allocator());

        sample(f.clip, f.skel, 10.0f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {2, 4, 0}));

        sample(f.clip, f.skel, -3.0f, false, f.pose);
        REQUIRE(approx(f.pose.joints[0].translation, {0, 0, 0}));

        f.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("clip: untracked channels and joints fall back to the skeleton rest pose") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ClipFixture f; f.init(arena.allocator());

        sample(f.clip, f.skel, 0.5f, false, f.pose);
        // Joint 0 has no scale track -> rest scale (identity).
        REQUIRE(approx(f.pose.joints[0].scale, {1, 1, 1}));
        // Joint 1 has no tracks at all -> full rest pose.
        REQUIRE(approx(f.pose.joints[1].translation, {5, 6, 7}));
        REQUIRE(approx(f.pose.joints[1].rotation, quat_identity()));
        REQUIRE(approx(f.pose.joints[1].scale, {2, 2, 2}));

        f.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("clip: a single-key track is constant at every sample time") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Skeleton skel; skel.init(1, arena.allocator());
        AnimationClip clip; clip.init(1, arena.allocator());
        clip.duration = 1.0f;
        Vec3Track& t = clip.alloc_translation_keys(0, 1);
        t.times[0] = 0.5f; t.values[0] = {3, 2, 1};
        Pose pose; pose.init(1, arena.allocator());

        for (f32 time : {0.0f, 0.25f, 0.5f, 0.9f, 1.0f}) {
            sample(clip, skel, time, false, pose);
            REQUIRE(approx(pose.joints[0].translation, {3, 2, 1}));
        }

        pose.shutdown();
        clip.shutdown();
        skel.shutdown();
    }
    arena.shutdown();
}
