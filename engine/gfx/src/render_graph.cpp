#include "gfx/render_graph.h"
#include "gfx/rhi.h"
#include "core/assert.h"

namespace ge::gfx {

void RenderGraph::begin() {
    pass_count_             = 0;
    access_count_           = 0;
    tracked_count_          = 0;
    final_count_            = 0;
    compiled_barrier_count_ = 0;
    command_count_          = 0;
    current_pass_           = ~0u;
}

void RenderGraph::import(TextureHandle tex, ResourceState state) {
    track(tex, state);
}

void RenderGraph::add_pass(const char* name, PassExecuteFn fn, void* user_data) {
    GE_ASSERT(pass_count_ < kMaxPasses);
    current_pass_ = pass_count_;
    Pass& p       = passes_[pass_count_++];
    p.name        = name;
    p.execute     = fn;
    p.user_data   = user_data;
    p.first_access = access_count_;
    p.access_count = 0;
}

void RenderGraph::pass_read(TextureHandle tex, ResourceState state) {
    GE_ASSERT(current_pass_ != ~0u);
    GE_ASSERT(access_count_ < kMaxAccesses);
    accesses_[access_count_++] = {tex, state};
    passes_[current_pass_].access_count++;
}

void RenderGraph::pass_write(TextureHandle tex, ResourceState state) {
    GE_ASSERT(current_pass_ != ~0u);
    GE_ASSERT(access_count_ < kMaxAccesses);
    accesses_[access_count_++] = {tex, state};
    passes_[current_pass_].access_count++;
}

void RenderGraph::set_final_state(TextureHandle tex, ResourceState state) {
    GE_ASSERT(final_count_ < kMaxResources);
    finals_[final_count_++] = {tex, state};
}

void RenderGraph::compile() {
    compiled_barrier_count_ = 0;
    command_count_          = 0;

    for (u32 pi = 0; pi < pass_count_; ++pi) {
        const Pass& p = passes_[pi];

        for (u32 ai = 0; ai < p.access_count; ++ai) {
            const ResourceAccess& access = accesses_[p.first_access + ai];
            ResourceState* current = find_tracked(access.texture);

            if (current) {
                if (*current != access.state) {
                    emit_barrier(access.texture, *current, access.state);
                    *current = access.state;
                }
            } else {
                track(access.texture, access.state);
            }
        }

        emit_pass(pi);
    }

    for (u32 fi = 0; fi < final_count_; ++fi) {
        const ResourceAccess& final_access = finals_[fi];
        ResourceState* current = find_tracked(final_access.texture);
        if (current && *current != final_access.state) {
            emit_barrier(final_access.texture, *current, final_access.state);
            *current = final_access.state;
        }
    }
}

void RenderGraph::execute(RhiDevice& rhi) {
    for (u32 ci = 0; ci < command_count_; ++ci) {
        const Command& cmd = commands_[ci];
        if (cmd.type == Command::CmdBarrier) {
            const Barrier& b = compiled_barriers_[cmd.barrier.barrier_index];
            rhi.barrier(b.texture, b.before, b.after);
        } else {
            const Pass& p = passes_[cmd.pass.pass_index];
            if (p.execute)
                p.execute(rhi, p.user_data);
        }
    }
}

ResourceState* RenderGraph::find_tracked(TextureHandle tex) {
    for (u32 i = 0; i < tracked_count_; ++i) {
        if (tracked_[i].texture == tex)
            return &tracked_[i].state;
    }
    return nullptr;
}

void RenderGraph::track(TextureHandle tex, ResourceState state) {
    ResourceState* existing = find_tracked(tex);
    if (existing) {
        *existing = state;
        return;
    }
    GE_ASSERT(tracked_count_ < kMaxResources);
    tracked_[tracked_count_++] = {tex, state};
}

void RenderGraph::emit_barrier(TextureHandle tex, ResourceState before, ResourceState after) {
    GE_ASSERT(compiled_barrier_count_ < kMaxBarriers);
    u32 bi = compiled_barrier_count_++;
    compiled_barriers_[bi] = {tex, before, after};

    GE_ASSERT(command_count_ < kMaxBarriers + kMaxPasses);
    Command& cmd = commands_[command_count_++];
    cmd.type = Command::CmdBarrier;
    cmd.barrier.barrier_index = bi;
}

void RenderGraph::emit_pass(u32 pass_index) {
    GE_ASSERT(command_count_ < kMaxBarriers + kMaxPasses);
    Command& cmd = commands_[command_count_++];
    cmd.type = Command::CmdPass;
    cmd.pass.pass_index = pass_index;
}

} // namespace ge::gfx
