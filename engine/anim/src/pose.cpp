#include "anim/pose.h"
#include "core/transform.h"

namespace ge::anim {

void pose_from_rest(const Skeleton& skel, Pose& out) {
    GE_ASSERT(out.joint_count == skel.joint_count);
    for (u32 i = 0; i < skel.joint_count; ++i)
        out.joints[i] = skel.rest_pose[i];
}

void blend(const Pose& a, const Pose& b, f32 t, Pose& out) {
    GE_ASSERT(a.joint_count == b.joint_count && a.joint_count == out.joint_count);
    for (u32 i = 0; i < a.joint_count; ++i) {
        out.joints[i].translation = lerp(a.joints[i].translation, b.joints[i].translation, t);
        out.joints[i].rotation    = slerp(a.joints[i].rotation, b.joints[i].rotation, t);
        out.joints[i].scale       = lerp(a.joints[i].scale, b.joints[i].scale, t);
    }
}

void blend_1d_weights(const f32* anchors, u32 anchor_count, f32 param, f32* out_weights) {
    GE_ASSERT(anchor_count >= 1);
    for (u32 i = 0; i < anchor_count; ++i)
        out_weights[i] = 0.0f;

    if (param <= anchors[0]) {
        out_weights[0] = 1.0f;
        return;
    }
    if (param >= anchors[anchor_count - 1]) {
        out_weights[anchor_count - 1] = 1.0f;
        return;
    }
    // Find the segment [anchors[i], anchors[i+1]] containing param and split
    // linearly; u and (1 - u) sum to 1 by construction.
    for (u32 i = 0; i + 1 < anchor_count; ++i) {
        if (param < anchors[i + 1]) {
            f32 u = (param - anchors[i]) / (anchors[i + 1] - anchors[i]);
            out_weights[i]     = 1.0f - u;
            out_weights[i + 1] = u;
            return;
        }
    }
}

void pose_to_model_space(const Skeleton& skel, const Pose& pose, mat4* out_model) {
    GE_ASSERT(pose.joint_count == skel.joint_count);
    for (u32 i = 0; i < skel.joint_count; ++i) {
        const JointPose& j = pose.joints[i];
        mat4 local = trs(j.translation, j.rotation, j.scale);
        i32 parent = skel.parents[i];
        GE_ASSERT(parent < static_cast<i32>(i)); // parents precede children
        out_model[i] = parent < 0 ? local : local * out_model[parent];
    }
}

void pose_to_palette(const Skeleton& skel, const mat4* model_space, mat4* out_palette) {
    for (u32 i = 0; i < skel.joint_count; ++i)
        out_palette[i] = skel.inverse_bind[i] * model_space[i];
}

} // namespace ge::anim
