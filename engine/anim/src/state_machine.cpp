#include "anim/state_machine.h"
#include "anim/pose.h"

namespace ge::anim {

void AnimStateMachine::init(const Skeleton* skeleton, Allocator alloc) {
    GE_ASSERT(skeleton != nullptr && skeleton->joint_count > 0);
    skeleton_ = skeleton;
    scratch_a_.init(skeleton->joint_count, alloc);
    scratch_b_.init(skeleton->joint_count, alloc);
    scratch_prev_.init(skeleton->joint_count, alloc);
}

void AnimStateMachine::shutdown() {
    scratch_prev_.shutdown();
    scratch_b_.shutdown();
    scratch_a_.shutdown();
    skeleton_ = nullptr;
    state_count_ = 0;
    current_ = kInvalidState;
    previous_ = kInvalidState;
    fading_ = false;
}

u32 AnimStateMachine::add_state(const AnimState& state) {
    GE_ASSERT(state_count_ < kMaxStates);
    GE_ASSERT(state.clip_count >= 1 && state.clip_count <= AnimState::kMaxBlendClips);
    for (u32 i = 0; i < state.clip_count; ++i) {
        GE_ASSERT(state.clips[i] != nullptr);
        GE_ASSERT(state.clips[i]->joint_count == skeleton_->joint_count);
        if (i > 0) GE_ASSERT(state.anchors[i] > state.anchors[i - 1]); // strictly ascending
    }

    const u32 id = state_count_++;
    state_[id] = state;
    state_time_[id] = 0.0f;
    if (current_ == kInvalidState) current_ = id;
    return id;
}

void AnimStateMachine::transition_to(u32 state, f32 fade_time) {
    GE_ASSERT(state < state_count_);
    if (state == current_) return;

    if (fade_time <= 0.0f) {
        current_ = state;
        state_time_[state] = 0.0f;
        fading_ = false;
        return;
    }

    // If a fade is still running, the old outgoing state is dropped (see the
    // class doc) — the new fade starts from the current state's pose alone.
    previous_ = current_;
    current_ = state;
    state_time_[state] = 0.0f;
    fade_time_ = fade_time;
    fade_elapsed_ = 0.0f;
    fading_ = true;
}

void AnimStateMachine::tick(f32 dt, f32 param, Pose& out_pose) {
    GE_ASSERT(current_ != kInvalidState); // add_state() before the first tick
    GE_ASSERT(out_pose.joint_count == skeleton_->joint_count);

    state_time_[current_] += dt;
    if (fading_) {
        state_time_[previous_] += dt; // the outgoing clip keeps playing under the fade
        fade_elapsed_ += dt;
        if (fade_elapsed_ >= fade_time_) fading_ = false;
    }

    eval_state(current_, param, out_pose);
    if (fading_) {
        eval_state(previous_, param, scratch_prev_);
        // Incoming weight ramps 0 -> 1 over the fade (weights sum to 1).
        blend(scratch_prev_, out_pose, fade_elapsed_ / fade_time_, out_pose);
    }
}

void AnimStateMachine::eval_state(u32 index, f32 param, Pose& out) {
    const AnimState& s = state_[index];
    const f32 t = state_time_[index];

    if (s.clip_count == 1) {
        sample(*s.clips[0], *skeleton_, t, s.looping, out);
        return;
    }

    f32 weights[AnimState::kMaxBlendClips];
    blend_1d_weights(s.anchors, s.clip_count, param, weights);

    // blend_1d_weights() leaves at most two adjacent non-zero weights.
    u32 i = 0;
    while (i + 1 < s.clip_count && weights[i] == 0.0f) ++i;

    if (i + 1 < s.clip_count && weights[i + 1] > 0.0f) {
        sample(*s.clips[i],     *skeleton_, t, s.looping, scratch_a_);
        sample(*s.clips[i + 1], *skeleton_, t, s.looping, scratch_b_);
        blend(scratch_a_, scratch_b_, weights[i + 1], out); // weights[i] == 1 - weights[i+1]
    } else {
        sample(*s.clips[i], *skeleton_, t, s.looping, out);
    }
}

} // namespace ge::anim
