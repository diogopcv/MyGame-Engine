#include "anim/clip.h"
#include <cmath>

namespace ge::anim {

void AnimationClip::init(u32 count, Allocator alloc) {
    GE_ASSERT(count > 0 && count <= kMaxJoints);
    alloc_ = alloc;
    joint_count = count;
    tracks = alloc_.alloc_n<JointTracks>(count);
    for (u32 i = 0; i < count; ++i)
        tracks[i] = JointTracks{};
}

void AnimationClip::shutdown() {
    if (tracks) {
        for (u32 i = 0; i < joint_count; ++i) {
            JointTracks& t = tracks[i];
            if (t.translation.times) {
                alloc_.free(t.translation.times, sizeof(f32) * t.translation.key_count);
                alloc_.free(t.translation.values, sizeof(vec3) * t.translation.key_count);
            }
            if (t.rotation.times) {
                alloc_.free(t.rotation.times, sizeof(f32) * t.rotation.key_count);
                alloc_.free(t.rotation.values, sizeof(quat) * t.rotation.key_count);
            }
            if (t.scale.times) {
                alloc_.free(t.scale.times, sizeof(f32) * t.scale.key_count);
                alloc_.free(t.scale.values, sizeof(vec3) * t.scale.key_count);
            }
        }
        alloc_.free(tracks, sizeof(JointTracks) * joint_count);
    }
    tracks = nullptr;
    joint_count = 0;
    duration = 0.0f;
}

Vec3Track& AnimationClip::alloc_translation_keys(u32 joint, u32 key_count) {
    GE_ASSERT(joint < joint_count && key_count > 0);
    Vec3Track& t = tracks[joint].translation;
    t.times = alloc_.alloc_n<f32>(key_count);
    t.values = alloc_.alloc_n<vec3>(key_count);
    t.key_count = key_count;
    return t;
}

QuatTrack& AnimationClip::alloc_rotation_keys(u32 joint, u32 key_count) {
    GE_ASSERT(joint < joint_count && key_count > 0);
    QuatTrack& t = tracks[joint].rotation;
    t.times = alloc_.alloc_n<f32>(key_count);
    t.values = alloc_.alloc_n<quat>(key_count);
    t.key_count = key_count;
    return t;
}

Vec3Track& AnimationClip::alloc_scale_keys(u32 joint, u32 key_count) {
    GE_ASSERT(joint < joint_count && key_count > 0);
    Vec3Track& t = tracks[joint].scale;
    t.times = alloc_.alloc_n<f32>(key_count);
    t.values = alloc_.alloc_n<vec3>(key_count);
    t.key_count = key_count;
    return t;
}

namespace {

// Binary search for the last key with times[i] <= t. Preconditions: t is
// within (times[0], times[key_count-1]) — the clamped ends are handled by
// the callers — and times are strictly increasing.
u32 find_key(const f32* times, u32 key_count, f32 t) {
    u32 lo = 0, hi = key_count - 1;
    while (hi - lo > 1) {
        u32 mid = (lo + hi) / 2;
        if (times[mid] <= t) lo = mid;
        else                 hi = mid;
    }
    return lo;
}

vec3 sample_vec3(const Vec3Track& track, f32 t) {
    if (track.key_count == 1 || t <= track.times[0]) return track.values[0];
    u32 last = track.key_count - 1;
    if (t >= track.times[last]) return track.values[last];
    u32 i = find_key(track.times, track.key_count, t);
    f32 u = (t - track.times[i]) / (track.times[i + 1] - track.times[i]);
    return lerp(track.values[i], track.values[i + 1], u);
}

quat sample_quat(const QuatTrack& track, f32 t) {
    if (track.key_count == 1 || t <= track.times[0]) return track.values[0];
    u32 last = track.key_count - 1;
    if (t >= track.times[last]) return track.values[last];
    u32 i = find_key(track.times, track.key_count, t);
    f32 u = (t - track.times[i]) / (track.times[i + 1] - track.times[i]);
    return slerp(track.values[i], track.values[i + 1], u);
}

} // namespace

void sample(const AnimationClip& clip, const Skeleton& skel, f32 time, bool looping,
            Pose& out_pose) {
    GE_ASSERT(clip.joint_count == skel.joint_count && clip.joint_count == out_pose.joint_count);

    f32 t = time;
    if (looping && clip.duration > 0.0f) {
        t = std::fmod(t, clip.duration);
        if (t < 0.0f) t += clip.duration;
    } else {
        if (t < 0.0f) t = 0.0f;
        if (t > clip.duration) t = clip.duration;
    }

    for (u32 i = 0; i < clip.joint_count; ++i) {
        const JointTracks& tr = clip.tracks[i];
        JointPose& out = out_pose.joints[i];
        out = skel.rest_pose[i];
        if (tr.translation.key_count > 0) out.translation = sample_vec3(tr.translation, t);
        if (tr.rotation.key_count > 0)    out.rotation    = sample_quat(tr.rotation, t);
        if (tr.scale.key_count > 0)       out.scale       = sample_vec3(tr.scale, t);
    }
}

} // namespace ge::anim
