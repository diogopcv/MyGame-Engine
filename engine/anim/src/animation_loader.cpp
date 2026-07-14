#include "anim/animation_loader.h"
#include "core/log.h"
#include "core/transform.h"

#include <cgltf.h>

#include <cstdio>
#include <cstring>

namespace ge::anim {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// cgltf writes matrices as column-major float[16]. Copying element
/// arr[i*4+j] into row-major m[i][j] yields the transpose, which is exactly
/// the row-vector-convention matrix this engine wants (same conversion as
/// ge_gfx's model loader — duplicated because siblings may not share code).
static mat4 column_major_to_row_vector(const float w[16]) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = w[i * 4 + j];
    return r;
}

/// Index of @p node in the skin's joints array, or -1 if it isn't a joint.
static i32 joint_index_of(const cgltf_skin* skin, const cgltf_node* node) {
    for (cgltf_size i = 0; i < skin->joints_count; ++i)
        if (skin->joints[i] == node) return static_cast<i32>(i);
    return -1;
}

/// A joint node's local TRS (decomposing the matrix form if present).
static JointPose node_local_pose(const cgltf_node* node) {
    if (node->has_matrix) {
        trs_components c = trs_decompose(column_major_to_row_vector(node->matrix));
        return {c.translation, c.rotation, c.scale};
    }
    JointPose p;
    p.translation = {node->translation[0], node->translation[1], node->translation[2]};
    p.rotation    = {node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
    p.scale       = {node->scale[0], node->scale[1], node->scale[2]};
    return p;
}

// ---------------------------------------------------------------------------
// AnimationSet
// ---------------------------------------------------------------------------

i32 AnimationSet::find_clip(const char* name) const {
    for (u32 i = 0; i < clip_count; ++i)
        if (std::strcmp(clip_names[i], name) == 0) return static_cast<i32>(i);
    return -1;
}

void AnimationSet::shutdown() {
    for (u32 i = 0; i < clip_count; ++i)
        clips[i].shutdown();
    if (clips)      alloc.free(clips, sizeof(AnimationClip) * clip_count);
    if (clip_names) alloc.free(clip_names, sizeof(*clip_names) * clip_count);
    clips = nullptr;
    clip_names = nullptr;
    clip_count = 0;
    skeleton.shutdown();
}

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

bool load_animation_gltf(const char* path, Allocator alloc, AnimationSet& out) {
    cgltf_options options{};
    cgltf_data* data = nullptr;

    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        GE_ERROR("anim: cgltf_parse_file failed: {}", path);
        return false;
    }
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        GE_ERROR("anim: cgltf_load_buffers failed: {}", path);
        cgltf_free(data);
        return false;
    }

    if (data->skins_count == 0) {
        GE_ERROR("anim: no skin in {}", path);
        cgltf_free(data);
        return false;
    }
    const cgltf_skin* skin = &data->skins[0];
    if (data->skins_count > 1)
        GE_WARN("anim: {} has {} skins; importing skin 0 only", path, data->skins_count);

    const u32 joint_count = static_cast<u32>(skin->joints_count);
    if (joint_count == 0 || joint_count > kMaxJoints) {
        GE_ERROR("anim: {} joint count {} outside (0, {}]", path, joint_count, kMaxJoints);
        cgltf_free(data);
        return false;
    }

    out.alloc = alloc;

    // --- Skeleton: parents, rest pose, inverse bind matrices ---
    out.skeleton.init(joint_count, alloc);
    for (u32 j = 0; j < joint_count; ++j) {
        const cgltf_node* node = skin->joints[j];
        i32 parent = node->parent ? joint_index_of(skin, node->parent) : -1;
        if (parent >= static_cast<i32>(j)) {
            GE_ERROR("anim: {} skin joints are not parents-first (joint {} has parent {})",
                     path, j, parent);
            out.shutdown();
            cgltf_free(data);
            return false;
        }
        out.skeleton.parents[j]   = parent;
        out.skeleton.rest_pose[j] = node_local_pose(node);
    }
    if (skin->inverse_bind_matrices) {
        for (u32 j = 0; j < joint_count; ++j) {
            f32 m[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j, m, 16);
            out.skeleton.inverse_bind[j] = column_major_to_row_vector(m);
        }
    } // else: identity IBMs from Skeleton::init(), per the glTF default.

    // --- Clips ---
    out.clip_count = static_cast<u32>(data->animations_count);
    if (out.clip_count > 0) {
        out.clips      = alloc.alloc_n<AnimationClip>(out.clip_count);
        out.clip_names = alloc.alloc_n<char[AnimationSet::kMaxClipName + 1]>(out.clip_count);

        for (u32 ci = 0; ci < out.clip_count; ++ci) {
            const cgltf_animation& ganim = data->animations[ci];
            AnimationClip& clip = out.clips[ci];
            clip = AnimationClip{};
            clip.init(joint_count, alloc);

            if (ganim.name && ganim.name[0])
                std::snprintf(out.clip_names[ci], sizeof(out.clip_names[ci]), "%s", ganim.name);
            else
                std::snprintf(out.clip_names[ci], sizeof(out.clip_names[ci]), "clip_%u", ci);

            for (cgltf_size chi = 0; chi < ganim.channels_count; ++chi) {
                const cgltf_animation_channel& ch = ganim.channels[chi];
                if (!ch.target_node || !ch.sampler) continue;
                i32 joint = joint_index_of(skin, ch.target_node);
                if (joint < 0) continue; // channel targets a non-joint node
                if (ch.sampler->interpolation == cgltf_interpolation_type_cubic_spline) {
                    GE_WARN("anim: {} clip '{}' uses CUBICSPLINE (unsupported); channel skipped",
                            path, out.clip_names[ci]);
                    continue;
                }
                // STEP is read as LINEAR: keys land exactly, in-betweens differ
                // only within a keyframe interval.

                const cgltf_accessor* input  = ch.sampler->input;
                const cgltf_accessor* output = ch.sampler->output;
                const u32 key_count = static_cast<u32>(input->count);
                if (key_count == 0 || output->count < input->count) continue;

                switch (ch.target_path) {
                    case cgltf_animation_path_type_translation: {
                        Vec3Track& t = clip.alloc_translation_keys(static_cast<u32>(joint), key_count);
                        for (u32 k = 0; k < key_count; ++k) {
                            cgltf_accessor_read_float(input, k, &t.times[k], 1);
                            f32 v[3];
                            cgltf_accessor_read_float(output, k, v, 3);
                            t.values[k] = {v[0], v[1], v[2]};
                        }
                        break;
                    }
                    case cgltf_animation_path_type_rotation: {
                        QuatTrack& t = clip.alloc_rotation_keys(static_cast<u32>(joint), key_count);
                        for (u32 k = 0; k < key_count; ++k) {
                            cgltf_accessor_read_float(input, k, &t.times[k], 1);
                            f32 v[4];
                            cgltf_accessor_read_float(output, k, v, 4);
                            t.values[k] = {v[0], v[1], v[2], v[3]};
                        }
                        break;
                    }
                    case cgltf_animation_path_type_scale: {
                        Vec3Track& t = clip.alloc_scale_keys(static_cast<u32>(joint), key_count);
                        for (u32 k = 0; k < key_count; ++k) {
                            cgltf_accessor_read_float(input, k, &t.times[k], 1);
                            f32 v[3];
                            cgltf_accessor_read_float(output, k, v, 3);
                            t.values[k] = {v[0], v[1], v[2]};
                        }
                        break;
                    }
                    default:
                        break; // morph-target weights: out of scope this phase
                }

                // Clip duration = the latest key time across all channels.
                f32 last_time = 0.0f;
                cgltf_accessor_read_float(input, input->count - 1, &last_time, 1);
                if (last_time > clip.duration) clip.duration = last_time;
            }
        }
    }

    cgltf_free(data);

    GE_INFO("anim: loaded {} ({} joints, {} clips)", path, joint_count, out.clip_count);
    return true;
}

} // namespace ge::anim
