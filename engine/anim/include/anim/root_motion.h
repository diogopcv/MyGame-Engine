#pragma once
#include "anim/clip.h"

/**
 * @file root_motion.h
 * @brief Root-motion extraction and stripping (Layer 3 — Subsystems).
 *
 * Root motion turns the root joint's authored movement into gameplay
 * movement: per fixed tick, extract_root_motion() measures how far the root
 * travelled between two sample times (loop-aware), and strip_root_motion()
 * removes that same movement from the sampled pose so it isn't applied
 * twice. The extracted delta becomes a desired velocity for the physics
 * body (delta / dt) — the physics solver stays the authority on actual
 * movement, so collisions still stop the character and animation never
 * teleports the body. Vertical root motion (jumps) is out of scope this
 * phase. See docs/phases/06-animation.md (Milestone 5).
 */
namespace ge::anim {

/**
 * @brief The root joint's movement over one tick.
 *
 * Both fields are expressed in the asset's model space (the root joint's
 * parent space): the consumer rotates the translation by the character's
 * world orientation and applies any asset scale before feeding physics.
 */
struct RootMotionDelta {
    vec3 translation{}; ///< Horizontal root displacement (y is always 0).
    f32  yaw = 0.0f;    ///< Root yaw delta about +Y, in radians.
};

/**
 * @brief Measure the root joint's translation/yaw delta between two clip times.
 *
 * Samples the root (joint 0) translation and rotation tracks at @p prev_time
 * and @p new_time and returns the difference. Looping is handled by
 * unwrapping: crossing the loop seam adds the clip's full per-cycle
 * displacement, so a tick that spans the seam (or several whole loops)
 * yields the exact travelled distance, not the snap-back. Non-looping clips
 * clamp both times to [0, duration]. Untracked channels sample the rest
 * pose, so an in-place clip (no root tracks) extracts a zero delta.
 *
 * The yaw is the root rotation's twist about +Y. Limitation (documented, not
 * defended): a clip whose root yaw passes through exactly 360 degrees
 * mid-cycle wraps the twist angle and misbehaves — acceptable until a real
 * turning asset exists.
 *
 * @param clip      Clip to measure (joint_count must match @p skel).
 * @param skel      Skeleton providing the rest pose for untracked channels.
 * @param prev_time Sample time at the start of the tick, in seconds.
 * @param new_time  Sample time at the end of the tick, in seconds.
 * @param looping   Wrap (true) or clamp (false) time outside the clip.
 * @return Horizontal translation + yaw delta, asset model space.
 */
RootMotionDelta extract_root_motion(const AnimationClip& clip, const Skeleton& skel,
                                    f32 prev_time, f32 new_time, bool looping);

/**
 * @brief Remove root motion from a sampled pose (the counterpart of extraction).
 *
 * Resets the root joint's horizontal translation (x, z) and yaw twist to the
 * skeleton's rest pose, leaving vertical translation and the remaining
 * swing rotation animated. Exactly what extract_root_motion() reports is
 * removed, so applying the extracted delta to the body and rendering the
 * stripped pose reproduces the authored motion without double-counting.
 * A no-op for poses sampled from in-place clips.
 *
 * @param skel Skeleton providing the rest pose.
 * @param pose Pose to strip in place (joint_count must match).
 */
void strip_root_motion(const Skeleton& skel, Pose& pose);

} // namespace ge::anim
