#pragma once
#include "core/types.h"
#include <atomic>

namespace ge {

struct JobDecl {
    using Fn = void (*)(void* data);
    Fn    fn   = nullptr;
    void* data = nullptr;
};

class JobCounter {
public:
    void  increment(u32 n = 1) { value_.fetch_add(n, std::memory_order_relaxed); }
    void  decrement()          { value_.fetch_sub(1, std::memory_order_release); }
    u32   value() const        { return value_.load(std::memory_order_acquire); }

private:
    std::atomic<u32> value_{0};
};

class JobSystem {
public:
    void init(u32 num_workers = 0);
    void shutdown();

    JobCounter* kick_jobs(const JobDecl* jobs, u32 count);
    void wait_for_counter(JobCounter* counter, u32 target = 0);

    u32 worker_count() const { return worker_count_; }

private:
    void* impl_ = nullptr;
    u32 worker_count_ = 0;
};

} // namespace ge
