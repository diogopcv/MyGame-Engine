#pragma once
#include "core/types.h"
#include "core/string_id.h"

/**
 * @file profiler.h
 * @brief Lightweight hierarchical CPU profiler (Layer 1 — Core).
 *
 * Records timed scopes each frame into a ring buffer.  Use GE_PROFILE_SCOPE()
 * to instrument a block; query historical frames for display or telemetry.
 */
namespace ge {

/**
 * @brief A single timed scope recorded by the profiler.
 */
struct ProfileScope {
    StringId name;  ///< Hashed name of the scope.
    f64      start; ///< Start time in seconds (relative to frame start).
    f64      end;   ///< End time in seconds.
    u32      depth; ///< Nesting depth (0 = top level).
};

/**
 * @brief All profiled scopes for one frame.
 */
struct ProfileFrame {
    static constexpr u32 kMaxScopes = 128; ///< Maximum scopes per frame.
    ProfileScope scopes[kMaxScopes];       ///< Recorded scopes.
    u32 scope_count = 0;                   ///< Number of scopes recorded.
    f64 frame_time  = 0.0;                 ///< Total frame duration in seconds.
};

/**
 * @brief Hierarchical CPU profiler with a ring-buffered history.
 */
class Profiler {
public:
    /** @brief Initialise the profiler. */
    void init();
    /** @brief Shut down the profiler and reset state. */
    void shutdown();

    /** @brief Mark the beginning of a new frame. */
    void begin_frame();
    /** @brief Mark the end of the current frame and commit its data. */
    void end_frame();

    /**
     * @brief Open a named profiling scope.
     * @param name Hashed scope name.
     */
    void push_scope(StringId name);
    /** @brief Close the most-recently opened scope. */
    void pop_scope();

    /**
     * @brief Retrieve a historical frame from the ring buffer.
     * @param ago 0 = current/last completed frame, 1 = one frame ago, etc.
     * @return Reference to the ProfileFrame data.
     */
    const ProfileFrame& frame(u32 ago = 0) const;

    /** @brief Duration of the most recently completed frame in seconds. */
    f64 last_frame_time() const;

private:
    static constexpr u32 kRingSize = 120;
    ProfileFrame frames_[kRingSize];
    u32 write_idx_ = 0;
    u32 depth_     = 0;
    f64 frame_start_ = 0.0;
    bool initialized_ = false;
};

/** @brief Access the global Profiler singleton. */
Profiler& profiler();

/**
 * @brief RAII guard that opens a profiling scope on construction and closes it
 *        on destruction.
 */
struct ProfileScopeGuard {
    /** @brief Push a named scope. */
    ProfileScopeGuard(StringId name) { profiler().push_scope(name); }
    /** @brief Pop the scope. */
    ~ProfileScopeGuard()             { profiler().pop_scope(); }
};

} // namespace ge

/**
 * @brief Instrument the current block with a named profiling scope.
 * @param name String literal identifying the scope.
 */
#define GE_PROFILE_SCOPE(name)                                                  \
    ::ge::ProfileScopeGuard ge_profile_guard_##__LINE__(::ge::string_id(name))
