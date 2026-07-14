#pragma once
#include "anim/pose.h"

/**
 * @file clip.h
 * @brief Animation clip storage and sampling (Layer 3 — Subsystems).
 *
 * An `AnimationClip` holds raw per-joint keyframe tracks (times + values,
 * SoA) sampled with binary search + lerp/slerp — no compression or
 * fixed-rate resampling until memory or cache pressure is measured (see the
 * phase doc's tech decisions). Filled by hand in tests (Milestone 1) and by
 * the glTF animation loader (Milestone 2).
 * See docs/phases/06-animation.md.
 */
namespace ge::anim {

/** @brief Keyframe track of vec3 values (translation or scale). Empty (`key_count == 0`) = channel not animated. */
struct Vec3Track {
    f32*  times = nullptr;  ///< Key times in seconds, strictly increasing.
    vec3* values = nullptr; ///< One value per key time.
    u32   key_count = 0;
};

/** @brief Keyframe track of quaternion rotations. Empty (`key_count == 0`) = channel not animated. */
struct QuatTrack {
    f32*  times = nullptr;  ///< Key times in seconds, strictly increasing.
    quat* values = nullptr; ///< One unit quaternion per key time.
    u32   key_count = 0;
};

/** @brief The three animatable channels of one joint. */
struct JointTracks {
    Vec3Track translation;
    QuatTrack rotation;
    Vec3Track scale;
};

/**
 * @brief A named animation: per-joint keyframe tracks over a fixed duration.
 *
 * Owns all track storage: allocate key arrays through the alloc_*_keys()
 * helpers so shutdown() can free everything. Joints with empty tracks fall
 * back to the skeleton's rest pose when sampled.
 */
struct AnimationClip {
    /**
     * @brief Allocate the per-joint track array (all tracks empty).
     * @param count Number of joints (must match the target skeleton).
     * @param alloc Allocator for all clip storage, including later key arrays.
     */
    void init(u32 count, Allocator alloc);

    /** @brief Release all track storage. */
    void shutdown();

    /** @brief Allocate @p key_count keys for @p joint's translation track. */
    Vec3Track& alloc_translation_keys(u32 joint, u32 key_count);
    /** @brief Allocate @p key_count keys for @p joint's rotation track. */
    QuatTrack& alloc_rotation_keys(u32 joint, u32 key_count);
    /** @brief Allocate @p key_count keys for @p joint's scale track. */
    Vec3Track& alloc_scale_keys(u32 joint, u32 key_count);

    f32          duration = 0.0f;   ///< Clip length in seconds.
    u32          joint_count = 0;
    JointTracks* tracks = nullptr;  ///< One JointTracks per joint, skeleton joint order.

private:
    Allocator alloc_{};
};

/**
 * @brief Sample a single vec3 track at @p t (no looping — the caller wraps time).
 *
 * Times before the first key clamp to the first value, after the last key to
 * the last value; between keys, linear interpolation. The track must not be
 * empty. Used by sample() per channel and by root-motion extraction, which
 * samples just the root joint's tracks (see root_motion.h).
 */
vec3 sample_track(const Vec3Track& track, f32 t);

/** @brief Quaternion-track overload of sample_track(): slerp between keys, clamped ends. */
quat sample_track(const QuatTrack& track, f32 t);

/**
 * @brief Sample @p clip at @p time into @p out_pose.
 *
 * Looping wraps time by the clip duration; non-looping clamps to
 * [0, duration]. Within a track, times before the first key clamp to the
 * first value and times after the last key clamp to the last value;
 * between keys, translation/scale lerp and rotation slerps. Joints whose
 * channel has no track take that channel from @p skel's rest pose.
 *
 * @param clip     Clip to sample (joint_count must match @p skel and @p out_pose).
 * @param skel     Skeleton providing rest-pose fallback for untracked channels.
 * @param time     Sample time in seconds.
 * @param looping  Wrap (true) or clamp (false) time outside the clip.
 * @param out_pose Receives the sampled local-space pose.
 */
void sample(const AnimationClip& clip, const Skeleton& skel, f32 time, bool looping,
            Pose& out_pose);

} // namespace ge::anim
