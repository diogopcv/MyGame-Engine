#include <doctest/doctest.h>
#include "core/arena.h"
#include "anim/root_motion.h"
#include <cmath>

using namespace ge;
using namespace ge::anim;

namespace {

constexpr f32 kEps = 1e-5f;
constexpr f32 kPi  = 3.14159265358979f;

bool approx(f32 a, f32 b, f32 eps = kEps) { return std::abs(a - b) < eps; }
bool approx(vec3 a, vec3 b, f32 eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}
bool approx(quat a, quat b, f32 eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps) && approx(a.w, b.w, eps);
}

// 2-joint skeleton (root + child) and a clip whose root translates
// (0,0,0) -> (2,4,0) over 1 s: a known horizontal speed of 2 u/s in x with
// deliberate vertical motion (y) that extraction must exclude.
struct RmFixture {
    Skeleton skel;
    AnimationClip move;
    Pose pose;

    void init(Allocator alloc) {
        skel.init(2, alloc);
        skel.parents[1] = 0;

        move.init(2, alloc);
        move.duration = 1.0f;
        Vec3Track& t = move.alloc_translation_keys(0, 2);
        t.times[0] = 0.0f; t.values[0] = {0, 0, 0};
        t.times[1] = 1.0f; t.values[1] = {2, 4, 0};

        pose.init(2, alloc);
    }
};

} // namespace

TEST_CASE("root motion: a known root translation yields exactly that delta per tick") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        RmFixture f; f.init(arena.allocator());

        // One 60 Hz tick of a 2 u/s clip: exactly 2/60 in x, vertical excluded.
        const f32 dt = 1.0f / 60.0f;
        RootMotionDelta d = extract_root_motion(f.move, f.skel, 0.0f, dt, true);
        CHECK(approx(d.translation, {2.0f * dt, 0, 0}));
        CHECK(approx(d.yaw, 0.0f));

        // Mid-clip interval.
        d = extract_root_motion(f.move, f.skel, 0.25f, 0.75f, true);
        CHECK(approx(d.translation, {1, 0, 0}));

        // 60 accumulated ticks cover the whole clip: total = authored travel.
        vec3 total{};
        f32 t = 0.0f;
        for (int i = 0; i < 60; ++i) {
            total = total + extract_root_motion(f.move, f.skel, t, t + dt, true).translation;
            t += dt;
        }
        CHECK(approx(total, {2, 0, 0}, 1e-4f));
    }
    arena.shutdown();
}

TEST_CASE("root motion: looping unwraps the seam and whole extra loops") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        RmFixture f; f.init(arena.allocator());

        // Across the seam: (2 - 1.96) at the end + 0.04 into the next cycle.
        RootMotionDelta d = extract_root_motion(f.move, f.skel, 0.98f, 1.02f, true);
        CHECK(approx(d.translation, {0.08f, 0, 0}));

        // 2.5 loops = 2 full cycles + half: 2*2 + 1.
        d = extract_root_motion(f.move, f.skel, 0.0f, 2.5f, true);
        CHECK(approx(d.translation, {5, 0, 0}));

        // Zero-length interval, even at the seam itself.
        d = extract_root_motion(f.move, f.skel, 1.0f, 1.0f, true);
        CHECK(approx(d.translation, {0, 0, 0}));
    }
    arena.shutdown();
}

TEST_CASE("root motion: non-looping clamps at the clip ends") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        RmFixture f; f.init(arena.allocator());

        // The clip ends at t=1: only the 0.9 -> 1.0 part moves.
        RootMotionDelta d = extract_root_motion(f.move, f.skel, 0.9f, 1.5f, false);
        CHECK(approx(d.translation, {0.2f, 0, 0}));

        // Entirely past the end: nothing moves.
        d = extract_root_motion(f.move, f.skel, 1.2f, 5.0f, false);
        CHECK(approx(d.translation, {0, 0, 0}));

        // Before the start clamps to t=0.
        d = extract_root_motion(f.move, f.skel, -1.0f, 0.5f, false);
        CHECK(approx(d.translation, {1, 0, 0}));
    }
    arena.shutdown();
}

TEST_CASE("root motion: yaw twist is extracted per tick and stripped from the pose") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        RmFixture f; f.init(arena.allocator());

        // Root turns 0 -> 90 degrees about +Y over 1 s.
        AnimationClip turn;
        turn.init(2, arena.allocator());
        turn.duration = 1.0f;
        QuatTrack& r = turn.alloc_rotation_keys(0, 2);
        r.times[0] = 0.0f; r.values[0] = quat_identity();
        r.times[1] = 1.0f; r.values[1] = quat_from_axis_angle({0, 1, 0}, kPi * 0.5f);

        // Half the clip = half the turn (same-axis slerp is linear in angle).
        RootMotionDelta d = extract_root_motion(turn, f.skel, 0.0f, 0.5f, false);
        CHECK(approx(d.yaw, kPi * 0.25f));
        CHECK(approx(d.translation, {0, 0, 0}));

        // Across the loop seam: (90 - 67.5) + 22.5 = 45 degrees.
        d = extract_root_motion(turn, f.skel, 0.75f, 1.25f, true);
        CHECK(approx(d.yaw, kPi * 0.25f));

        // Stripping removes the animated yaw: mid-turn pose ends up at rest.
        sample(turn, f.skel, 0.5f, false, f.pose);
        strip_root_motion(f.skel, f.pose);
        CHECK(approx(f.pose.joints[0].rotation, quat_identity()));

        turn.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("root motion: strip zeroes root x/z and yaw but keeps vertical, swing, and children") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        RmFixture f; f.init(arena.allocator());

        // Root: constant translation (1,2,3) and rotation pitch(0.6) then
        // yaw(0.8) (apply-left-first product, so the yaw is the outer twist).
        // Child: a rotation strip must not touch.
        AnimationClip c;
        c.init(2, arena.allocator());
        c.duration = 1.0f;
        Vec3Track& t = c.alloc_translation_keys(0, 1);
        t.times[0] = 0.0f; t.values[0] = {1, 2, 3};
        QuatTrack& r = c.alloc_rotation_keys(0, 1);
        const quat pitch = quat_from_axis_angle({1, 0, 0}, 0.6f);
        r.times[0] = 0.0f; r.values[0] = pitch * quat_from_axis_angle({0, 1, 0}, 0.8f);
        QuatTrack& rc = c.alloc_rotation_keys(1, 1);
        const quat child_rot = quat_from_axis_angle({0, 0, 1}, 0.4f);
        rc.times[0] = 0.0f; rc.values[0] = child_rot;

        sample(c, f.skel, 0.5f, false, f.pose);
        strip_root_motion(f.skel, f.pose);

        // Horizontal to rest (0), animated vertical kept.
        CHECK(approx(f.pose.joints[0].translation, {0, 2, 0}));
        // Yaw twist removed, pitch swing kept.
        CHECK(approx(f.pose.joints[0].rotation, pitch));
        // Child untouched.
        CHECK(approx(f.pose.joints[1].rotation, child_rot));

        // A constant root track never produces a delta.
        RootMotionDelta d = extract_root_motion(c, f.skel, 0.1f, 0.9f, true);
        CHECK(approx(d.translation, {0, 0, 0}));
        CHECK(approx(d.yaw, 0.0f));

        c.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("root motion: in-place clips (no root tracks) extract zero and strip to rest") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        // Skeleton with a non-trivial rest root: strip must restore the REST
        // x/z and yaw twist, not zero them (the Fox-like in-place case).
        Skeleton skel; skel.init(2, arena.allocator());
        skel.parents[1] = 0;
        skel.rest_pose[0].translation = {0.5f, 0.25f, -0.75f};
        skel.rest_pose[0].rotation    = quat_from_axis_angle({0, 1, 0}, 0.3f);

        AnimationClip c;
        c.init(2, arena.allocator());
        c.duration = 1.0f;
        QuatTrack& rc = c.alloc_rotation_keys(1, 1); // child-only animation
        rc.times[0] = 0.0f; rc.values[0] = quat_from_axis_angle({0, 0, 1}, 1.0f);

        RootMotionDelta d = extract_root_motion(c, skel, 0.0f, 0.5f, true);
        CHECK(approx(d.translation, {0, 0, 0}));
        CHECK(approx(d.yaw, 0.0f));

        Pose pose; pose.init(2, arena.allocator());
        sample(c, skel, 0.5f, true, pose);
        strip_root_motion(skel, pose);
        CHECK(approx(pose.joints[0].translation, skel.rest_pose[0].translation));
        CHECK(approx(pose.joints[0].rotation, skel.rest_pose[0].rotation));

        pose.shutdown();
        c.shutdown();
        skel.shutdown();
    }
    arena.shutdown();
}
