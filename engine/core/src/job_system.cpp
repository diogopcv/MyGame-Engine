#include "core/job_system.h"
#include "core/assert.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>

namespace ge {

namespace {
    constexpr u32 kMaxCounters = 256;
}

struct JobEntry {
    JobDecl    decl;
    JobCounter* counter;
};

struct JobSystemImpl {
    std::vector<std::thread>    workers;
    std::queue<JobEntry>        queue;
    std::mutex                  mutex;
    std::condition_variable     cv;
    bool                        shutting_down = false;

    JobCounter                  counters[kMaxCounters];
    std::atomic<u32>            next_counter{0};

    JobCounter* alloc_counter() {
        u32 idx = next_counter.fetch_add(1, std::memory_order_relaxed) % kMaxCounters;
        counters[idx].~JobCounter();
        new (&counters[idx]) JobCounter();
        return &counters[idx];
    }

    static void worker_thread(JobSystemImpl* impl) {
        while (true) {
            JobEntry entry;
            {
                std::unique_lock lock(impl->mutex);
                impl->cv.wait(lock, [impl] {
                    return !impl->queue.empty() || impl->shutting_down;
                });
                if (impl->shutting_down && impl->queue.empty()) return;
                entry = impl->queue.front();
                impl->queue.pop();
            }
            entry.decl.fn(entry.decl.data);
            entry.counter->decrement();
        }
    }
};

void JobSystem::init(u32 num_workers) {
    auto* impl = new JobSystemImpl();
    impl_ = impl;

    if (num_workers == 0) {
        u32 hw = std::thread::hardware_concurrency();
        num_workers = hw > 1 ? hw - 1 : 1;
    }
    worker_count_ = num_workers;

    impl->workers.reserve(num_workers);
    for (u32 i = 0; i < num_workers; ++i)
        impl->workers.emplace_back(JobSystemImpl::worker_thread, impl);
}

void JobSystem::shutdown() {
    if (!impl_) return;
    auto* impl = static_cast<JobSystemImpl*>(impl_);
    {
        std::lock_guard lock(impl->mutex);
        impl->shutting_down = true;
    }
    impl->cv.notify_all();
    for (auto& t : impl->workers) t.join();
    delete impl;
    impl_ = nullptr;
    worker_count_ = 0;
}

JobCounter* JobSystem::kick_jobs(const JobDecl* jobs, u32 count) {
    auto* impl = static_cast<JobSystemImpl*>(impl_);
    JobCounter* counter = impl->alloc_counter();
    counter->increment(count);
    {
        std::lock_guard lock(impl->mutex);
        for (u32 i = 0; i < count; ++i)
            impl->queue.push({ jobs[i], counter });
    }
    impl->cv.notify_all();
    return counter;
}

void JobSystem::wait_for_counter(JobCounter* counter, u32 target) {
    while (counter->value() != target)
        std::this_thread::yield();
}

} // namespace ge
