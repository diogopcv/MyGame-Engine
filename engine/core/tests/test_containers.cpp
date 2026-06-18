#include <doctest/doctest.h>
#include "core/array.h"
#include "core/hash_map.h"
#include "core/arena.h"

namespace {
    ge::Allocator make_alloc(ge::Arena& arena) { return arena.allocator(); }
}

// --- Array ---

TEST_CASE("Array: push and index") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        arr.push(10);
        arr.push(20);
        arr.push(30);
        REQUIRE(arr.size() == 3);
        REQUIRE(arr[0] == 10);
        REQUIRE(arr[1] == 20);
        REQUIRE(arr[2] == 30);
    }
    arena.shutdown();
}

TEST_CASE("Array: pop") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        arr.push(1); arr.push(2); arr.push(3);
        arr.pop();
        REQUIRE(arr.size() == 2);
        REQUIRE(arr.back() == 2);
    }
    arena.shutdown();
}

TEST_CASE("Array: reserve and capacity growth") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        arr.reserve(100);
        REQUIRE(arr.capacity() >= 100);
        REQUIRE(arr.size() == 0);

        for (int i = 0; i < 100; ++i) arr.push(i);
        REQUIRE(arr.size() == 100);
        for (int i = 0; i < 100; ++i) REQUIRE(arr[i] == i);
    }
    arena.shutdown();
}

TEST_CASE("Array: resize grows and shrinks") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        arr.resize(5);
        REQUIRE(arr.size() == 5);
        for (ge::usize i = 0; i < 5; ++i) REQUIRE(arr[i] == 0);

        arr.resize(2);
        REQUIRE(arr.size() == 2);
    }
    arena.shutdown();
}

TEST_CASE("Array: clear") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        arr.push(1); arr.push(2); arr.push(3);
        arr.clear();
        REQUIRE(arr.size() == 0);
        REQUIRE(arr.empty());
    }
    arena.shutdown();
}

TEST_CASE("Array: iteration") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> arr(make_alloc(arena));
        for (int i = 0; i < 10; ++i) arr.push(i);
        int sum = 0;
        for (int v : arr) sum += v;
        REQUIRE(sum == 45);
    }
    arena.shutdown();
}

TEST_CASE("Array: move constructor") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::Array<int> a(make_alloc(arena));
        a.push(1); a.push(2);

        ge::Array<int> b(std::move(a));
        REQUIRE(b.size() == 2);
        REQUIRE(b[0] == 1);
        REQUIRE(a.size() == 0);
        REQUIRE(a.data() == nullptr);
    }
    arena.shutdown();
}

// --- HashMap ---

TEST_CASE("HashMap: set and get") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(1, 100);
        map.set(2, 200);
        map.set(3, 300);

        REQUIRE(map.size() == 3);
        REQUIRE(*map.get(1) == 100);
        REQUIRE(*map.get(2) == 200);
        REQUIRE(*map.get(3) == 300);
        REQUIRE(map.get(4) == nullptr);
    }
    arena.shutdown();
}

TEST_CASE("HashMap: overwrite existing key") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(1, 10);
        map.set(1, 99);
        REQUIRE(map.size() == 1);
        REQUIRE(*map.get(1) == 99);
    }
    arena.shutdown();
}

TEST_CASE("HashMap: remove") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(1, 10);
        map.set(2, 20);
        REQUIRE(map.remove(1));
        REQUIRE(map.size() == 1);
        REQUIRE(map.get(1) == nullptr);
        REQUIRE(*map.get(2) == 20);
        REQUIRE(!map.remove(99));
    }
    arena.shutdown();
}

TEST_CASE("HashMap: grow under load") {
    ge::Arena arena; arena.init(GE_MEGABYTES(1));
    {
        ge::HashMap<ge::u32, ge::u32> map(make_alloc(arena));
        for (ge::u32 i = 0; i < 200; ++i)
            map.set(i, i * 10);

        REQUIRE(map.size() == 200);
        for (ge::u32 i = 0; i < 200; ++i)
            REQUIRE(*map.get(i) == i * 10);
    }
    arena.shutdown();
}

TEST_CASE("HashMap: for_each") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(1, 10);
        map.set(2, 20);
        map.set(3, 30);

        int sum = 0;
        map.for_each([&](ge::u32, int v) { sum += v; });
        REQUIRE(sum == 60);
    }
    arena.shutdown();
}

TEST_CASE("HashMap: clear") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(1, 10); map.set(2, 20);
        map.clear();
        REQUIRE(map.size() == 0);
        REQUIRE(map.empty());
        REQUIRE(map.get(1) == nullptr);

        map.set(3, 30);
        REQUIRE(map.size() == 1);
        REQUIRE(*map.get(3) == 30);
    }
    arena.shutdown();
}

TEST_CASE("HashMap: lookup after remove still finds other keys") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        for (ge::u32 i = 0; i < 20; ++i)
            map.set(i, static_cast<int>(i));

        map.remove(5);
        map.remove(10);
        map.remove(15);

        for (ge::u32 i = 0; i < 20; ++i) {
            if (i == 5 || i == 10 || i == 15)
                REQUIRE(map.get(i) == nullptr);
            else
                REQUIRE(*map.get(i) == static_cast<int>(i));
        }
    }
    arena.shutdown();
}

// Find two distinct u32 keys that map to the same initial bucket for the
// map's first-grown capacity (16). The map computes `hash_key(k) & (cap-1)`,
// so we reproduce that exactly. By pigeonhole, a collision exists among the
// first 17 small keys.
namespace {
    bool find_colliding_pair(ge::u32& a, ge::u32& b) {
        constexpr ge::u32 kCap = 16; // capacity after the first set()'s grow()
        ge::u32 first_in_bucket[kCap];
        bool    seen[kCap] = {};
        for (ge::u32 x = 1; x <= 64; ++x) {
            ge::u32 bk = static_cast<ge::u32>(ge::detail::hash_key(x) & (kCap - 1));
            if (seen[bk]) { a = first_in_bucket[bk]; b = x; return true; }
            seen[bk] = true;
            first_in_bucket[bk] = x;
        }
        return false;
    }
}

// Reproduces the open-addressing duplicate-insertion bug: when a tombstone
// sits earlier in a key's probe chain than the key itself, set() reinserts at
// the tombstone instead of updating the existing entry. Two colliding keys A,B
// land in adjacent slots; removing A tombstones the bucket B probes through, so
// re-setting B writes a SECOND copy. Currently FAILS (size becomes 2, and the
// stale duplicate shadows the update on lookup/remove).
TEST_CASE("HashMap: re-set after removing a colliding key must not duplicate") {
    ge::u32 a = 0, b = 0;
    REQUIRE(find_colliding_pair(a, b));

    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HashMap<ge::u32, int> map(make_alloc(arena));
        map.set(a, 1);          // A occupies its bucket
        map.set(b, 2);          // B collides, lands in the next slot
        REQUIRE(map.remove(a)); // bucket B probes through becomes a tombstone
        REQUIRE(map.size() == 1);

        map.set(b, 3);          // update B — must NOT create a second entry

        REQUIRE(map.size() == 1);     // bug: becomes 2
        REQUIRE(*map.get(b) == 3);

        // The duplicate (if present) holds the stale value 2 and survives the
        // remove of the first copy, so B must be fully gone afterwards.
        REQUIRE(map.remove(b));
        REQUIRE(map.get(b) == nullptr); // bug: stale duplicate still found
        REQUIRE(map.size() == 0);       // bug: 1 left over
    }
    arena.shutdown();
}
