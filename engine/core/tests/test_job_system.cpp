#include <doctest/doctest.h>
#include "core/job_system.h"
#include <atomic>

namespace {
    void increment_fn(void* data) {
        static_cast<std::atomic<int>*>(data)->fetch_add(1, std::memory_order_relaxed);
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
