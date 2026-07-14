#pragma once
#include "anim/clip.h"

/**
 * @file state_machine.h
 * @brief Animation states with 1D blending and crossfade transitions (Layer 3 — Subsystems).
 *
 * A hand-rolled fixed-state machine (per the phase doc's "plain C++ until
 * churn demands more" reasoning, same ethos as ADR-0009) — not a data-driven
 * graph. Each state is a single looping clip or a 1D blend of clips over a
 * gameplay parameter (e.g. speed → idle/walk/run); transitions crossfade the
 * outgoing and incoming poses over a fade time.
 * See docs/phases/06-animation.md (Milestone 4).
 */
namespace ge::anim {

/**
 * @brief One animation state: a clip, or a 1D blend of clips over a parameter.
 *
 * `anchors` are the parameter values at which each clip fully owns the pose
 * (strictly ascending, e.g. speeds {0, 3, 9} for idle/walk/run); between
 * anchors the two bracketing clips blend linearly (see blend_1d_weights()).
 * A single-clip state ignores the parameter. Clip pointers are borrowed —
 * the caller (typically an AnimationSet) must outlive the state machine.
 */
struct AnimState {
    /// Maximum clips in one state's 1D blend.
    static constexpr u32 kMaxBlendClips = 4;

    const AnimationClip* clips[kMaxBlendClips] = {};
    f32  anchors[kMaxBlendClips] = {}; ///< Parameter anchor per clip, strictly ascending.
    u32  clip_count = 0;
    bool looping = true;
};

/**
 * @brief Fixed-capacity animation state machine with crossfade transitions.
 *
 * Usage per fixed tick (deterministic — tick it from the same accumulator
 * as physics): decide transitions from gameplay state (transition_to()),
 * then tick(dt, param, out) to advance clocks and produce the final pose.
 *
 * Each state keeps its own clock, advanced while the state is current or
 * fading out; a state's clock restarts from 0 when transitioned into.
 * Transitioning while a fade is still running drops the old outgoing state
 * (the blend can hold at most two poses) — a small visual pop that a pose
 * snapshot could fix later if the manual pass ever flags it.
 */
class AnimStateMachine {
public:
    static constexpr u32 kMaxStates    = 8;
    static constexpr u32 kInvalidState = ~0u;

    /**
     * @brief Bind the machine to a skeleton and allocate scratch poses.
     * @param skeleton Borrowed; must outlive the machine.
     * @param alloc    Allocator for the internal scratch poses.
     */
    void init(const Skeleton* skeleton, Allocator alloc);

    /** @brief Release the scratch poses. */
    void shutdown();

    /**
     * @brief Register a state. The first state added becomes current.
     * @param state State description (validated: 1..kMaxBlendClips clips,
     *              matching joint counts, strictly ascending anchors).
     * @return The state's id, for transition_to().
     */
    u32 add_state(const AnimState& state);

    /**
     * @brief Start a crossfade to @p state (no-op if it is already current).
     *
     * The incoming state's clock restarts from 0. @p fade_time <= 0 switches
     * instantly.
     *
     * @param state     Target state id from add_state().
     * @param fade_time Crossfade duration in seconds.
     */
    void transition_to(u32 state, f32 fade_time);

    /**
     * @brief Advance clocks and fade timers, then write the blended pose.
     * @param dt       Fixed timestep in seconds.
     * @param param    1D blend parameter (e.g. current speed); ignored by
     *                 single-clip states.
     * @param out_pose Receives the final local-space pose.
     */
    void tick(f32 dt, f32 param, Pose& out_pose);

    /** @brief Id of the current (or fade-target) state. */
    u32  current_state() const { return current_; }
    /** @brief True while a crossfade is in progress. */
    bool in_transition() const { return fading_; }

private:
    void eval_state(u32 index, f32 param, Pose& out);

    const Skeleton* skeleton_ = nullptr;
    AnimState state_[kMaxStates];
    f32       state_time_[kMaxStates] = {};
    u32       state_count_ = 0;

    u32  current_  = kInvalidState;
    u32  previous_ = kInvalidState;
    bool fading_ = false;
    f32  fade_time_ = 0.0f;
    f32  fade_elapsed_ = 0.0f;

    Pose scratch_a_;    ///< 1D-blend bracket clip A.
    Pose scratch_b_;    ///< 1D-blend bracket clip B.
    Pose scratch_prev_; ///< Outgoing state's pose during a crossfade.
};

} // namespace ge::anim
