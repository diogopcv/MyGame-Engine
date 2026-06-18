#include "core/job_system.h"
#include "core/assert.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>

namespace ge {

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

    // Counter pool. Each JobCounter is heap-allocated individually so its
    // address is stable for the counter's whole lifetime — workers and waiters
    // hold raw pointers into it. Counters are recycled only via the free list,
    // never reconstructed while outstanding, so distinct live batches can never
    // alias. `all` owns every block for teardown; `free` holds reusable ones
    // (each returned with value 0). Guarded by counter_mutex.
    std::mutex                  counter_mutex;
    std::vector<JobCounter*>    all;
    std::vector<JobCounter*>    free;

    // Completion signalling. Workers notify done_cv after each decrement;
    // wait_for_counter() sleeps on it instead of spinning.
    std::mutex                  done_mutex;
    std::condition_variable     done_cv;

    JobCounter* alloc_counter() {
        std::lock_guard lock(counter_mutex);
        if (!free.empty()) {
            JobCounter* c = free.back();
            free.pop_back();
            return c; // already at 0 when it was freed
        }
        JobCounter* c = new JobCounter();
        all.push_back(c);
        return c;
    }

    void free_counter(JobCounter* c) {
        std::lock_guard lock(counter_mutex);
        free.push_back(c);
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

            // Wake any waiters. Taking done_mutex briefly closes the race
            // between a waiter testing its predicate and going to sleep inside
            // done_cv.wait(), so a completion can never be missed.
            {
                std::lock_guard<std::mutex> lock(impl->done_mutex);
            }
            impl->done_cv.notify_all();
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
    for (JobCounter* c : impl->all) delete c;
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
    auto* impl = static_cast<JobSystemImpl*>(impl_);
    {
        std::unique_lock<std::mutex> lock(impl->done_mutex);
        impl->done_cv.wait(lock, [&] { return counter->value() == target; });
    }
    // A fully-drained counter (value 0) is done with: recycle it. The pointer
    // is invalid for the caller after this returns. Partial waits (target != 0)
    // leave the counter outstanding and keep it allocated.
    if (target == 0) {
        impl->free_counter(counter);
    }
}

} // namespace ge
