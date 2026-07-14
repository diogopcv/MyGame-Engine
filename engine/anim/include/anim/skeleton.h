#pragma once
#include "core/allocator.h"
#include "core/assert.h"
#include "core/quat.h"
#include "core/vec.h"

/**
 * @file skeleton.h
 * @brief Skeleton joint hierarchy + bind-pose data (Layer 3 — Subsystems).
 *
 * Pure data: joint parent indices, inverse bind matrices, and the rest pose.
 * Filled by hand in tests (Milestone 1) and by the glTF animation loader
 * (Milestone 2). Joint indexing follows the glTF skin's `joints` array order —
 * the shared contract with `ge_gfx`'s skinned-mesh loader (both sides parse
 * the same file, so the order is by-construction consistent).
 * See docs/phases/06-animation.md.
 */
namespace ge::anim {

/// Maximum joints per skeleton — GPU joint indices are u8 (see the phase doc's
/// SkinnedVertex decision), so a skin may not exceed 256 joints.
inline constexpr u32 kMaxJoints = 256;

/**
 * @brief Local-space TRS of a single joint (relative to its parent joint).
 *
 * The unit of pose storage and blending: cheaper to interpolate than a matrix
 * (quat slerp stays on the rotation manifold where matrix lerp would not).
 */
struct JointPose {
    vec3 translation{};
    quat rotation = quat_identity();
    vec3 scale{1.0f, 1.0f, 1.0f};
};

/**
 * @brief A joint hierarchy with bind-pose data.
 *
 * Invariant: parents precede children (`parents[i] < i` for every non-root
 * joint), so a single forward pass resolves local→model space. The root's
 * parent is -1. All arrays are `joint_count` long and owned by the Skeleton
 * (allocated in init(), freed in shutdown()).
 */
struct Skeleton {
    /**
     * @brief Allocate the joint arrays (parents, inverse bind, rest pose).
     *
     * Parents default to -1 (root), inverse bind matrices to identity, and
     * rest-pose joints to identity TRS; callers overwrite what they need.
     *
     * @param count Number of joints (<= kMaxJoints).
     * @param alloc Allocator for all Skeleton storage.
     */
    void init(u32 count, Allocator alloc) {
        GE_ASSERT(count > 0 && count <= kMaxJoints);
        alloc_ = alloc;
        joint_count = count;
        parents      = alloc_.alloc_n<i32>(count);
        inverse_bind = alloc_.alloc_n<mat4>(count);
        rest_pose    = alloc_.alloc_n<JointPose>(count);
        for (u32 i = 0; i < count; ++i) {
            parents[i]      = -1;
            inverse_bind[i] = mat4_identity();
            rest_pose[i]    = JointPose{};
        }
    }

    /** @brief Release all storage. */
    void shutdown() {
        if (parents) {
            alloc_.free(parents, sizeof(i32) * joint_count);
            alloc_.free(inverse_bind, sizeof(mat4) * joint_count);
            alloc_.free(rest_pose, sizeof(JointPose) * joint_count);
        }
        parents = nullptr;
        inverse_bind = nullptr;
        rest_pose = nullptr;
        joint_count = 0;
    }

    u32        joint_count = 0;
    i32*       parents = nullptr;      ///< Parent joint index per joint; -1 = root. Parents precede children.
    mat4*      inverse_bind = nullptr; ///< Per-joint inverse bind matrix (model space -> joint bind space).
    JointPose* rest_pose = nullptr;    ///< Local-space rest (bind) pose per joint.

private:
    Allocator alloc_{};
};

} // namespace ge::anim
