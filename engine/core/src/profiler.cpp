#include "core/profiler.h"
#include "core/assert.h"
#include "platform/timer.h"

namespace ge {

Profiler& profiler() {
    static Profiler p;
    return p;
}

void Profiler::init() {
    for (auto& f : frames_) f = {};
    write_idx_    = 0;
    depth_        = 0;
    initialized_  = true;
}

void Profiler::shutdown() {
    initialized_ = false;
}

void Profiler::begin_frame() {
    ProfileFrame& f = frames_[write_idx_ % kRingSize];
    f.scope_count = 0;
    depth_ = 0;
    frame_start_ = platform::now_seconds();
}

void Profiler::end_frame() {
    ProfileFrame& f = frames_[write_idx_ % kRingSize];
    f.frame_time = platform::now_seconds() - frame_start_;
    ++write_idx_;
}

void Profiler::push_scope(StringId name) {
    ProfileFrame& f = frames_[write_idx_ % kRingSize];
    if (f.scope_count >= ProfileFrame::kMaxScopes) return;

    ProfileScope& s = f.scopes[f.scope_count++];
    s.name  = name;
    s.start = platform::now_seconds();
    s.end   = 0.0;
    s.depth = depth_;
    ++depth_;
}

void Profiler::pop_scope() {
    GE_ASSERT(depth_ > 0);
    --depth_;

    ProfileFrame& f = frames_[write_idx_ % kRingSize];
    for (u32 i = f.scope_count; i > 0; --i) {
        if (f.scopes[i - 1].depth == depth_ && f.scopes[i - 1].end == 0.0) {
            f.scopes[i - 1].end = platform::now_seconds();
            return;
        }
    }
}

const ProfileFrame& Profiler::frame(u32 ago) const {
    GE_ASSERT(ago < kRingSize);
    u32 idx = (write_idx_ - 1 - ago) % kRingSize;
    return frames_[idx];
}

f64 Profiler::last_frame_time() const {
    return frame(0).frame_time;
}

} // namespace ge
