#pragma once
#include "core/types.h"
#include "core/string_id.h"

namespace ge {

struct ProfileScope {
    StringId name;
    f64      start;
    f64      end;
    u32      depth;
};

struct ProfileFrame {
    static constexpr u32 kMaxScopes = 128;
    ProfileScope scopes[kMaxScopes];
    u32 scope_count = 0;
    f64 frame_time  = 0.0;
};

class Profiler {
public:
    void init();
    void shutdown();

    void begin_frame();
    void end_frame();

    void push_scope(StringId name);
    void pop_scope();

    const ProfileFrame& frame(u32 ago = 0) const;
    f64 last_frame_time() const;

private:
    static constexpr u32 kRingSize = 120;
    ProfileFrame frames_[kRingSize];
    u32 write_idx_ = 0;
    u32 depth_     = 0;
    f64 frame_start_ = 0.0;
    bool initialized_ = false;
};

Profiler& profiler();

struct ProfileScopeGuard {
    ProfileScopeGuard(StringId name) { profiler().push_scope(name); }
    ~ProfileScopeGuard()             { profiler().pop_scope(); }
};

} // namespace ge

#define GE_PROFILE_SCOPE(name)                                                  \
    ::ge::ProfileScopeGuard ge_profile_guard_##__LINE__(::ge::string_id(name))
