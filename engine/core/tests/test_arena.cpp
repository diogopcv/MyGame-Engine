#include <doctest/doctest.h>
#include "core/arena.h"
#include <cstdint>

TEST_CASE("Arena init and shutdown") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(64));
    REQUIRE(arena.capacity() == GE_KILOBYTES(64));
    REQUIRE(arena.used()     == 0);
    arena.shutdown();
    REQUIRE(arena.capacity() == 0);
    REQUIRE(arena.used()     == 0);
}

TEST_CASE("Arena alloc advances used()") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(64));

    void* p1 = arena.alloc(16);
    REQUIRE(p1 != nullptr);
    REQUIRE(arena.used() >= 16);

    void* p2 = arena.alloc(32);
    REQUIRE(p2 != nullptr);
    REQUIRE(arena.used() >= 48);

    arena.shutdown();
}

TEST_CASE("Arena alloc alignment — various powers of two") {
    ge::Arena arena;
    arena.init(GE_MEGABYTES(1));

    const ge::usize aligns[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    for (ge::usize align : aligns) {
        void* p = arena.alloc(1, align);
        REQUIRE(p != nullptr);
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        REQUIRE((addr % align) == 0);
    }

    arena.shutdown();
}

TEST_CASE("Arena reset resets used to 0 and allows reuse") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(4));

    void* first = arena.alloc(256);
    REQUIRE(arena.used() > 0);

    arena.reset();
    REQUIRE(arena.used() == 0);

    void* second = arena.alloc(256);
    // After reset, the new allocation should come from the same base address
    REQUIRE(first == second);

    arena.shutdown();
}

TEST_CASE("Arena alloc_n<T> returns correctly aligned typed pointer") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(64));

    struct alignas(16) Vec4 { float x, y, z, w; };
    Vec4* vecs = arena.alloc_n<Vec4>(32);
    REQUIRE(vecs != nullptr);
    REQUIRE((reinterpret_cast<std::uintptr_t>(vecs) % alignof(Vec4)) == 0);

    // Writing into allocated memory must not crash
    for (int i = 0; i < 32; ++i) vecs[i] = {1.0f, 2.0f, 3.0f, 4.0f};

    arena.shutdown();
}

TEST_CASE("Arena: multiple arenas are independent") {
    ge::Arena a, b;
    a.init(GE_KILOBYTES(4));
    b.init(GE_KILOBYTES(4));

    void* pa = a.alloc(64);
    void* pb = b.alloc(64);
    REQUIRE(pa != pb);

    b.reset();
    REQUIRE(b.used() == 0);
    REQUIRE(a.used() > 0);  // a is unaffected

    a.shutdown();
    b.shutdown();
}
