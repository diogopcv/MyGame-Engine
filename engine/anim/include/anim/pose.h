#pragma once
#include "anim/skeleton.h"

/**
 * @file pose.h
 * @brief Animation pose storage, blending, and palette generation (Layer 3 — Subsystems).
 *
 * A `Pose` is an array of local-space joint TRS values — the currency of the
 * animation pipeline: clips sample into poses, poses blend with poses, and a
 * final pose is flattened to model space and then to the GPU bone palette.
 * See docs/phases/06-animation.md (Milestone 1).
 */
namespace ge::anim {

/**
 * @brief Local-space TRS per joint, indexed in skeleton joint order.
 *
 * Owns its joint array (allocated in init(), freed in shutdown()).
 */
struct Pose {
    /**
     * @brief Allocate the joint array, initialised to identity TRS.
     * @param count Number of joints (must match the skeleton it's used with).
     * @param alloc Allocator for the Pose storage.
     */
    void init(u32 count, Allocator alloc) {
        GE_ASSERT(count > 0 && count <= kMaxJoints);
        alloc_ = alloc;
        joint_count = count;
        joints = alloc_.alloc_n<JointPose>(count);
        for (u32 i = 0; i < count; ++i) joints[i] = JointPose{};
    }

    /** @brief Release the joint array. */
    void shutdown() {
        if (joints) alloc_.free(joints, sizeof(JointPose) * joint_count);
        joints = nullptr;
        joint_count = 0;
    }

    u32        joint_count = 0;
    JointPose* joints = nullptr; ///< Local-space TRS per joint.

private:
    Allocator alloc_{};
};

/**
 * @brief Copy a skeleton's rest pose into @p out.
 * @param skel Source skeleton.
 * @param out  Destination pose (joint_count must match).
 */
void pose_from_rest(const Skeleton& skel, Pose& out);

/**
 * @brief Blend two poses: `out = (1 - t) * a + t * b`, per joint.
 *
 * Translation and scale interpolate linearly; rotation uses shortest-arc
 * slerp. The two implicit weights sum to 1 by construction.
 *
 * @param a Pose at t = 0.
 * @param b Pose at t = 1.
 * @param t Blend parameter in [0, 1].
 * @param out Destination pose (may alias @p a or @p b).
 */
void blend(const Pose& a, const Pose& b, f32 t, Pose& out);

/**
 * @brief Weights for a 1D blend space (e.g. speed -> idle/walk/run).
 *
 * Given clip anchor points sorted ascending (each clip's nominal parameter
 * value), produces one weight per anchor: zero everywhere except the two
 * anchors bracketing @p param, which split linearly. @p param outside the
 * anchor range clamps to the nearest end. Weights always sum to exactly 1.
 * Consumed by the locomotion blend in the Milestone 4 state machine.
 *
 * @param anchors      Anchor parameter values, sorted ascending.
 * @param anchor_count Number of anchors (>= 1).
 * @param param        Blend parameter (e.g. current speed).
 * @param out_weights  Receives @p anchor_count weights.
 */
void blend_1d_weights(const f32* anchors, u32 anchor_count, f32 param, f32* out_weights);

/**
 * @brief Flatten a local-space pose to per-joint model-space matrices.
 *
 * One forward pass: `model[j] = local[j] * model[parents[j]]` (row-vector
 * convention — the joint's local transform applies first, then the parent
 * chain). Valid because parents precede children in the skeleton.
 *
 * @param skel      Skeleton providing the parent hierarchy.
 * @param pose      Local-space pose (joint_count must match).
 * @param out_model Receives joint_count model-space matrices.
 */
void pose_to_model_space(const Skeleton& skel, const Pose& pose, mat4* out_model);

/**
 * @brief Build the GPU skinning palette: `palette[j] = inverse_bind[j] * model[j]`.
 *
 * Row-vector convention: a bind-pose model-space vertex is first taken into
 * joint bind space (inverse bind), then out through the animated joint
 * (model). Sampling the rest pose therefore yields identity palettes.
 *
 * @param skel        Skeleton providing the inverse bind matrices.
 * @param model_space joint_count model-space matrices (from pose_to_model_space()).
 * @param out_palette Receives joint_count palette matrices.
 */
void pose_to_palette(const Skeleton& skel, const mat4* model_space, mat4* out_palette);

} // namespace ge::anim
