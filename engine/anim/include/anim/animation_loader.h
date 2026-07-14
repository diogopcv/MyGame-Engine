#pragma once
#include "anim/clip.h"

/**
 * @file animation_loader.h
 * @brief glTF 2.0 skeleton + animation-clip import (Layer 3 — Subsystems).
 *
 * Uses cgltf (third-party, vendored) for parsing; cgltf does not leak into
 * this header. This is `ge_anim`'s own parse of the .glb — `ge_gfx` parses
 * the same file separately for the skinned mesh (see the phase doc's
 * sibling-isolation contract; the dual parse disappears into the Phase 11
 * asset pipeline). **Joint ordering contract:** the skeleton's joints are
 * indexed in the glTF skin's `joints` array order — the same order
 * `ge_gfx`'s skinned vertices reference via JOINTS_0.
 */
namespace ge::anim {

/**
 * @brief Everything animation-related loaded from one glTF file:
 *        the skeleton (from skin 0) plus all animation clips.
 *
 * Owns all storage (allocated from the load-time allocator; released by
 * shutdown()).
 */
struct AnimationSet {
    /// Maximum clip-name length stored (longer glTF names are truncated).
    static constexpr u32 kMaxClipName = 63;

    Skeleton       skeleton;
    AnimationClip* clips = nullptr;
    char         (*clip_names)[kMaxClipName + 1] = nullptr; ///< Parallel to clips; always NUL-terminated.
    u32            clip_count = 0;

    /**
     * @brief Find a clip by its glTF animation name.
     * @return Clip index, or -1 if not found.
     */
    i32 find_clip(const char* name) const;

    /** @brief Release the skeleton, all clips, and the name table. */
    void shutdown();

    Allocator alloc; ///< Allocator all storage came from (set by the loader).
};

/**
 * @brief Load skeleton + animation clips from a glTF / GLB file.
 *
 * Imports skin 0 (joint hierarchy, inverse bind matrices, rest pose from the
 * joint nodes' local TRS) and every animation whose channels target skin
 * joints. Matrices are converted from glTF column-major to the engine's
 * row-vector convention. Only LINEAR (and STEP, read as linear) samplers are
 * supported; CUBICSPLINE channels are skipped with a warning.
 *
 * Constraints validated at load (returning false, with an error logged):
 * - the file must contain at least one skin;
 * - joint count <= kMaxJoints (u8 GPU joint indices);
 * - the skin's `joints` array must list parents before children (the
 *   Skeleton invariant pose_to_model_space() relies on).
 *
 * @param path  File path to the .gltf or .glb file.
 * @param alloc Allocator for all AnimationSet storage.
 * @param out   Receives the loaded set on success.
 * @return true on success, false on parse/validation error.
 */
bool load_animation_gltf(const char* path, Allocator alloc, AnimationSet& out);

} // namespace ge::anim
