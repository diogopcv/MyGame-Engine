#include <doctest/doctest.h>
#include "core/job_system.h"
#include <atomic>
#include <thread>
#include <vector>
#include <set>

namespace {
    void increment_fn(void* data) {
        static_cast<std::atomic<int>*>(data)->fetch_add(1, std::memory_order_relaxed);
    }

    struct BlockCtx {
        std::atomic<bool> release{false};
        std::atomic<int>  ran{0};
    };

    // Spins until released, so the job (and its counter) stays outstanding.
    void block_fn(void* data) {
        auto* ctx = static_cast<BlockCtx*>(data);
        while (!ctx->release.load(std::memory_order_acquire))
            std::this_thread::yield();
        ctx->ran.fetch_add(1, std::memory_order_relaxed);
    }
}

TEST_CASE("JobSystem: init and shutdown") {
    ge::JobSystem js;
    js.init(2);
    REQUIRE(js.worker_count() == 2);
    js.shutdown();
}

TEST_CASE("JobSystem: single job completes") {
    ge::JobSystem js;
    js.init(2);

    std::atomic<int> counter{0};
    ge::JobDecl job{increment_fn, &counter};
    ge::JobCounter* c = js.kick_jobs(&job, 1);
    js.wait_for_counter(c);

    REQUIRE(counter.load() == 1);
    js.shutdown();
}

TEST_CASE("JobSystem: 100 jobs all complete") {
    ge::JobSystem js;
    js.init(4);

    std::atomic<int> sum{0};
    ge::JobDecl jobs[100];
    for (auto& j : jobs) {
        j.fn   = increment_fn;
        j.data = &sum;
    }

    ge::JobCounter* c = js.kick_jobs(jobs, 100);
    js.wait_for_counter(c);

    REQUIRE(sum.load() == 100);
    js.shutdown();
}

TEST_CASE("JobSystem: stress test — 1000 jobs for race detection") {
    ge::JobSystem js;
    js.init();

    std::atomic<int> sum{0};
    constexpr int kCount = 1000;
    ge::JobDecl jobs[kCount];
    for (auto& j : jobs) {
        j.fn   = increment_fn;
        j.data = &sum;
    }

    ge::JobCounter* c = js.kick_jobs(jobs, kCount);
    js.wait_for_counter(c);

    REQUIRE(sum.load() == kCount);
    js.shutdown();
}

TEST_CASE("JobSystem: multiple batches") {
    ge::JobSystem js;
    js.init(2);

    std::atomic<int> a{0}, b{0};

    ge::JobDecl ja{increment_fn, &a};
    ge::JobCounter* ca = js.kick_jobs(&ja, 1);

    ge::JobDecl jb[10];
    for (auto& j : jb) { j.fn = increment_fn; j.data = &b; }
    ge::JobCounter* cb = js.kick_jobs(jb, 10);

    js.wait_for_counter(ca);
    js.wait_for_counter(cb);

    REQUIRE(a.load() == 1);
    REQUIRE(b.load() == 10);

    js.shutdown();
}

// The old implementation recycled a fixed ring of 256 JobCounters, so once 256
// allocations were live the next batch aliased an earlier, still-outstanding
// counter. The real invariant: counters for batches that are outstanding at the
// same time must all be distinct — and the pool must scale past 256. Jobs block
// until released, so all batches stay outstanding while we check uniqueness.
TEST_CASE("JobSystem: simultaneously outstanding counters must be unique") {
    ge::JobSystem js;
    js.init(2);

    BlockCtx blocker;
    constexpr int kBatches = 300; // exceeds the old fixed ring of 256

    std::vector<ge::JobCounter*> counters;
    counters.reserve(kBatches);
    ge::JobDecl jb{block_fn, &blocker};
    for (int i = 0; i < kBatches; ++i)
        counters.push_back(js.kick_jobs(&jb, 1));

    std::set<ge::JobCounter*> unique(counters.begin(), counters.end());
    REQUIRE(unique.size() == static_cast<size_t>(kBatches)); // no aliasing

    // Release the blocked jobs and drain (this also recycles each counter).
    blocker.release.store(true, std::memory_order_release);
    for (ge::JobCounter* c : counters)
        js.wait_for_counter(c);
    REQUIRE(blocker.ran.load() == kBatches);

    js.shutdown();
}

// Practical consequence of the recycling: while one job is still outstanding,
// 256 further allocations reconstruct its counter in place, resetting the value
// out from under it. A waiter would then observe "done" before the job ran.
// Currently FAILS: the outstanding counter reads 0 instead of 1.
TEST_CASE("JobSystem: an outstanding job's counter must not be reset by reuse") {
    ge::JobSystem js;
    js.init(2);

    BlockCtx blocker;
    ge::JobDecl jb{block_fn, &blocker};
    ge::JobCounter* c0 = js.kick_jobs(&jb, 1); // outstanding: value should stay 1

    // Drive 256 more allocations; the ring wraps and reconstructs c0's slot.
    std::atomic<int> dummy{0};
    ge::JobDecl jd{increment_fn, &dummy};
    for (int i = 0; i < 256; ++i) {
        ge::JobCounter* c = js.kick_jobs(&jd, 1);
        js.wait_for_counter(c);
    }

    // The blocker never ran, so its counter must still read 1.
    REQUIRE(c0->value() == 1); // bug: reads 0 after the slot was reconstructed

    // Release the blocker and drain so shutdown can join cleanly.
    blocker.release.store(true, std::memory_order_release);
    while (blocker.ran.load(std::memory_order_acquire) == 0)
        std::this_thread::yield();
    js.shutdown();
}
