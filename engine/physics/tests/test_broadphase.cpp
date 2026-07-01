#include <doctest/doctest.h>
#include "core/arena.h"
#include "physics/broadphase.h"
#include "physics/collision.h"
#include <algorithm>
#include <utility>
#include <vector>

using namespace ge;
using namespace ge::physics;

namespace {

/** @brief Brute-force O(n^2) reference: every pair confirmed by aabb_overlap directly. */
std::vector<std::pair<u32, u32>> brute_force_pairs(const std::vector<AABB>& boxes) {
    std::vector<std::pair<u32, u32>> out;
    for (u32 i = 0; i < boxes.size(); ++i)
        for (u32 j = i + 1; j < boxes.size(); ++j)
            if (aabb_overlap(boxes[i], boxes[j])) out.push_back({i, j});
    return out;
}

std::vector<std::pair<u32, u32>> to_sorted_pairs(const Array<Pair>& pairs) {
    std::vector<std::pair<u32, u32>> out;
    for (usize i = 0; i < pairs.size(); ++i) {
        u32 a = pairs[i].a, b = pairs[i].b;
        out.push_back(a < b ? std::make_pair(a, b) : std::make_pair(b, a));
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("Broadphase: no proxies produces no pairs") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(16, arena.allocator());
        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);
        REQUIRE(pairs.size() == 0);
        bp.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Broadphase: two overlapping AABBs produce one pair") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(16, arena.allocator());
        bp.add(0, AABB{{0, 0, 0}, {2, 2, 2}});
        bp.add(1, AABB{{1, 1, 1}, {3, 3, 3}});

        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);
        REQUIRE(pairs.size() == 1);
        REQUIRE(((pairs[0].a == 0 && pairs[0].b == 1) || (pairs[0].a == 1 && pairs[0].b == 0)));
        bp.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Broadphase: two separated AABBs produce no pairs") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(16, arena.allocator());
        bp.add(0, AABB{{0, 0, 0}, {1, 1, 1}});
        bp.add(1, AABB{{10, 0, 0}, {11, 1, 1}});

        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);
        REQUIRE(pairs.size() == 0);
        bp.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Broadphase: overlapping on X but separated on Y produces no pairs") {
    // Regression case for the sweep: X-interval overlap alone isn't enough,
    // compute_pairs must confirm the full 3D AABB overlap.
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(16, arena.allocator());
        bp.add(0, AABB{{0, 0, 0}, {2, 1, 1}});
        bp.add(1, AABB{{0, 5, 0}, {2, 6, 1}});

        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);
        REQUIRE(pairs.size() == 0);
        bp.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Broadphase: clear() drops previously added proxies") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(16, arena.allocator());
        bp.add(0, AABB{{0, 0, 0}, {2, 2, 2}});
        bp.add(1, AABB{{1, 1, 1}, {3, 3, 3}});
        bp.clear();
        bp.add(2, AABB{{0, 0, 0}, {1, 1, 1}}); // no partner, alone in this step

        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);
        REQUIRE(pairs.size() == 0);
        bp.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Broadphase: matches a brute-force O(n^2) reference on a mixed scene") {
    // Deterministic (not random) mix: a dense overlapping cluster near the
    // origin, several isolated singletons far apart on X, and a couple of
    // boxes that overlap on X/Z but not Y -- exercises both the sweep's
    // early-out and its need to fall back on full 3D confirmation.
    std::vector<AABB> boxes = {
        {{0, 0, 0}, {1, 1, 1}},
        {{0.5f, 0.5f, 0.5f}, {1.5f, 1.5f, 1.5f}},
        {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}},
        {{0.9f, 0.9f, 0.9f}, {2.0f, 2.0f, 2.0f}},
        {{20, 0, 0}, {21, 1, 1}},
        {{40, 0, 0}, {41, 1, 1}},
        {{20.5f, 0, 0}, {21.5f, 1, 1}},
        {{0, 10, 0}, {1, 11, 1}},   // overlaps box 0 on X/Z only
        {{-2, -2, -2}, {-1, -1, -1}},
        {{-1.5f, -1.5f, -1.5f}, {-0.5f, -0.5f, -0.5f}},
    };

    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Broadphase bp; bp.init(64, arena.allocator());
        for (u32 i = 0; i < boxes.size(); ++i) bp.add(i, boxes[i]);

        Array<Pair> pairs(arena.allocator());
        bp.compute_pairs(pairs);

        auto expected = brute_force_pairs(boxes);
        auto actual = to_sorted_pairs(pairs);
        REQUIRE(actual == expected);
        REQUIRE(expected.size() > 0); // sanity: the scene actually has overlaps to find

        bp.shutdown();
    }
    arena.shutdown();
}
