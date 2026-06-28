#pragma once
#include "gfx/rhi_types.h"

/**
 * @file render_graph.h
 * @brief Frame graph with automatic barrier insertion (Layer 3 — Graphics).
 *
 * Each frame the renderer builds a graph of passes, each declaring what
 * resources it reads and writes.  The graph compiles a barrier list and
 * executes the passes in order, inserting state transitions automatically.
 *
 * Phase 3 scope: ordered passes (no automatic reordering), barrier
 * derivation, and final-state exports.  Transient resource aliasing is
 * future work.
 */
namespace ge::gfx {

class RhiDevice;

class RenderGraph {
public:
    /** @brief Callback signature for pass execution. */
    using PassExecuteFn = void (*)(RhiDevice& rhi, void* user_data);

    /** @brief Reset the graph for a new frame. */
    void begin();

    /**
     * @brief Declare a resource's initial state at frame start.
     * @param tex   Texture handle (from RhiDevice).
     * @param state The state the resource is in before any pass runs.
     */
    void import(TextureHandle tex, ResourceState state);

    /**
     * @brief Add a named render pass.
     *
     * After calling add_pass, use pass_read / pass_write to declare the
     * resource accesses for this pass before adding the next one.
     *
     * @param name      Debug name for the pass.
     * @param fn        Function called during execution (may be null for tests).
     * @param user_data Opaque pointer forwarded to @p fn.
     */
    void add_pass(const char* name, PassExecuteFn fn, void* user_data = nullptr);

    /** @brief Declare that the current pass reads @p tex in @p state. */
    void pass_read(TextureHandle tex, ResourceState state);

    /** @brief Declare that the current pass writes @p tex in @p state. */
    void pass_write(TextureHandle tex, ResourceState state);

    /**
     * @brief Declare the state a resource must be in after all passes.
     *
     * Used for the back buffer (must end in Present) or any resource whose
     * post-frame state matters.
     */
    void set_final_state(TextureHandle tex, ResourceState state);

    /**
     * @brief Compile the barrier list from declared passes.
     *
     * Call after all passes and final states have been declared.
     * Walks the passes in order, comparing each resource's current tracked
     * state to the state the pass requires, and emits a barrier when they
     * differ.  Appends final-state barriers at the end.
     */
    void compile();

    /** @brief Execute: apply barriers and call pass callbacks via the RHI. */
    void execute(RhiDevice& rhi);

    // -- Inspection (for unit tests) ----------------------------------------

    /** @brief A compiled barrier (state transition). */
    struct Barrier {
        TextureHandle texture;
        ResourceState before;
        ResourceState after;
    };

    /** @brief Number of compiled barriers (valid after compile()). */
    u32 barrier_count() const { return compiled_barrier_count_; }

    /** @brief Array of compiled barriers (valid after compile()). */
    const Barrier* barriers() const { return compiled_barriers_; }

    /** @brief Number of passes added this frame. */
    u32 pass_count() const { return pass_count_; }

private:
    static constexpr u32 kMaxPasses    = 16;
    static constexpr u32 kMaxAccesses  = 64;
    static constexpr u32 kMaxResources = 32;
    static constexpr u32 kMaxBarriers  = 64;

    struct ResourceAccess {
        TextureHandle texture;
        ResourceState state;
    };

    struct Pass {
        const char*   name;
        PassExecuteFn execute;
        void*         user_data;
        u32           first_access;
        u32           access_count;
    };

    struct TrackedResource {
        TextureHandle texture;
        ResourceState state;
    };

    // Command: either a barrier or a pass execution
    struct Command {
        enum Type : u8 { CmdBarrier, CmdPass };
        Type type;
        union {
            struct { u32 barrier_index; } barrier;
            struct { u32 pass_index; }    pass;
        };
    };

    Pass             passes_[kMaxPasses]       = {};
    u32              pass_count_               = 0;

    ResourceAccess   accesses_[kMaxAccesses]   = {};
    u32              access_count_             = 0;

    TrackedResource  tracked_[kMaxResources]   = {};
    u32              tracked_count_            = 0;

    ResourceAccess   finals_[kMaxResources]    = {};
    u32              final_count_              = 0;

    Barrier          compiled_barriers_[kMaxBarriers] = {};
    u32              compiled_barrier_count_   = 0;

    Command          commands_[kMaxBarriers + kMaxPasses] = {};
    u32              command_count_            = 0;

    u32              current_pass_             = ~0u;

    ResourceState* find_tracked(TextureHandle tex);
    void           track(TextureHandle tex, ResourceState state);
    void           emit_barrier(TextureHandle tex, ResourceState before, ResourceState after);
    void           emit_pass(u32 pass_index);
};

} // namespace ge::gfx
