#pragma once
#include "core/types.h"
#include <atomic>

/**
 * @file job_system.h
 * @brief Multi-threaded job/task system (Layer 1 — Core).
 *
 * Provides a simple fork/join style API: submit an array of job declarations,
 * receive a counter, and wait for the counter to reach zero.  Worker threads
 * are created at init() and joined at shutdown().
 */
namespace ge {

/**
 * @brief Description of a single job to be executed by the job system.
 */
struct JobDecl {
    /** @brief Job entry-point signature. */
    using Fn = void (*)(void* data);
    Fn    fn   = nullptr; ///< Function to execute.
    void* data = nullptr; ///< User data passed to @p fn.
};

/**
 * @brief Atomic counter used to track outstanding job completions.
 *
 * Incremented when jobs are submitted, decremented by each worker upon
 * completion.  Use JobSystem::wait_for_counter() to block until the
 * counter reaches a target value.
 */
class JobCounter {
public:
    /** @brief Atomically add @p n to the counter. */
    void  increment(u32 n = 1) { value_.fetch_add(n, std::memory_order_relaxed); }
    /** @brief Atomically decrement the counter by one. */
    void  decrement()          { value_.fetch_sub(1, std::memory_order_release); }
    /** @brief Read the current counter value. */
    u32   value() const        { return value_.load(std::memory_order_acquire); }

private:
    std::atomic<u32> value_{0};
};

/**
 * @brief Fork/join job system with a fixed-size worker thread pool.
 */
class JobSystem {
public:
    /**
     * @brief Start the worker threads.
     * @param num_workers Number of threads to spawn.  0 = hardware_concurrency - 1.
     */
    void init(u32 num_workers = 0);

    /** @brief Signal workers to exit and join all threads. */
    void shutdown();

    /**
     * @brief Submit an array of jobs for parallel execution.
     * @param jobs  Pointer to the first JobDecl.
     * @param count Number of jobs to submit.
     * @return Pointer to a counter that reaches 0 when all jobs finish.
     */
    JobCounter* kick_jobs(const JobDecl* jobs, u32 count);

    /**
     * @brief Block the calling thread until @p counter reaches @p target.
     * @param counter Counter returned by kick_jobs().
     * @param target  Value to wait for (default 0 = all jobs done).
     */
    void wait_for_counter(JobCounter* counter, u32 target = 0);

    /** @brief Number of worker threads. */
    u32 worker_count() const { return worker_count_; }

private:
    void* impl_ = nullptr;  ///< Opaque pointer to the internal implementation.
    u32 worker_count_ = 0;
};

} // namespace ge
