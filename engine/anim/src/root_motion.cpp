#include "anim/root_motion.h"
#include <cmath>

namespace ge::anim {

namespace {

/// Root translation + yaw twist at one (already wrapped/clamped) clip time.
struct RootSample {
    vec3 translation;
    f32  yaw;
};

// Twist of q about +Y: with the engine's apply-left-first product, any
// rotation decomposes as q = swing * twist_y (swing has no Y component;
// the yaw applies last, in the root's parent space — the same space the
// character body's yaw lives in). The twist half-angle is atan2(q.y, q.w),
// scale-invariant, so no normalization is needed. Degenerate 180-degree
// horizontal flips (y and w both ~0) have no defined yaw; return 0.
f32 twist_yaw(quat q) {
    if (q.y * q.y + q.w * q.w < 1e-12f) return 0.0f;
    return 2.0f * std::atan2(q.y, q.w);
}

RootSample sample_root(const AnimationClip& clip, const Skeleton& skel, f32 t) {
    const JointTracks& tr = clip.tracks[0];
    RootSample s;
    s.translation = tr.translation.key_count > 0 ? sample_track(tr.translation, t)
                                                 : skel.rest_pose[0].translation;
    s.yaw = twist_yaw(tr.rotation.key_count > 0 ? sample_track(tr.rotation, t)
                                                : skel.rest_pose[0].rotation);
    return s;
}

} // namespace

RootMotionDelta extract_root_motion(const AnimationClip& clip, const Skeleton& skel,
                                    f32 prev_time, f32 new_time, bool looping) {
    GE_ASSERT(clip.joint_count == skel.joint_count && clip.joint_count > 0);
    GE_ASSERT(skel.parents[0] < 0); // joint 0 is the root (parents-first invariant)

    vec3 translation{};
    f32  yaw = 0.0f;

    if (!looping || clip.duration <= 0.0f) {
        auto clamp_time = [&](f32 t) {
            if (t < 0.0f) return 0.0f;
            return t > clip.duration ? clip.duration : t;
        };
        RootSample a = sample_root(clip, skel, clamp_time(prev_time));
        RootSample b = sample_root(clip, skel, clamp_time(new_time));
        translation = b.translation - a.translation;
        yaw         = b.yaw - a.yaw;
    } else {
        // Unwrapped root position P(t) = floor(t/d) * per_cycle + S(t mod d):
        // continuous across the loop seam, so P(new) - P(prev) is the true
        // travelled delta for any interval, including whole extra loops.
        const RootSample s0 = sample_root(clip, skel, 0.0f);
        const RootSample s1 = sample_root(clip, skel, clip.duration);
        const vec3 cycle_translation = s1.translation - s0.translation;
        const f32  cycle_yaw         = s1.yaw - s0.yaw;

        auto unwrapped = [&](f32 t, vec3& out_translation, f32& out_yaw) {
            const f32 k = std::floor(t / clip.duration);
            const RootSample s = sample_root(clip, skel, t - k * clip.duration);
            out_translation = cycle_translation * k + s.translation;
            out_yaw         = cycle_yaw * k + s.yaw;
        };
        vec3 ta, tb;
        f32  ya, yb;
        unwrapped(prev_time, ta, ya);
        unwrapped(new_time, tb, yb);
        translation = tb - ta;
        yaw         = yb - ya;
    }

    return {vec3{translation.x, 0.0f, translation.z}, yaw};
}

void strip_root_motion(const Skeleton& skel, Pose& pose) {
    GE_ASSERT(pose.joint_count == skel.joint_count && pose.joint_count > 0);
    GE_ASSERT(skel.parents[0] < 0);

    JointPose&       root = pose.joints[0];
    const JointPose& rest = skel.rest_pose[0];

    root.translation.x = rest.translation.x;
    root.translation.z = rest.translation.z;

    // Replace the animated yaw twist with the rest pose's: rotating by
    // (rest_yaw - anim_yaw) about +Y (applied last, matching the q =
    // swing * twist_y decomposition) cancels the animated twist exactly and
    // re-applies the rest twist, leaving the swing component untouched.
    const f32 anim_yaw = twist_yaw(root.rotation);
    const f32 rest_yaw = twist_yaw(rest.rotation);
    if (anim_yaw != rest_yaw)
        root.rotation = root.rotation * quat_from_axis_angle({0, 1, 0}, rest_yaw - anim_yaw);
}

} // namespace ge::anim
