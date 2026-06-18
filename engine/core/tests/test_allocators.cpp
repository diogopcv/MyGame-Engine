#include <doctest/doctest.h>
#include "core/allocator.h"
#include "core/arena.h"
#include "core/pool.h"
#include "core/stack_alloc.h"
#include <cstdint>

// --- Pool allocator ---

TEST_CASE("Pool: init builds a valid free list") {
    ge::Pool pool;
    pool.init(64, 16);
    REQUIRE(pool.total_blocks() == 16);
    REQUIRE(pool.used_blocks()  == 0);
    REQUIRE(pool.block_size()   >= 64);
    pool.shutdown();
}

TEST_CASE("Pool: alloc/free round-trip") {
    ge::Pool pool;
    pool.init(sizeof(int), 8);

    void* ptrs[8];
    for (int i = 0; i < 8; ++i) {
        ptrs[i] = pool.alloc();
        REQUIRE(ptrs[i] != nullptr);
    }
    REQUIRE(pool.used_blocks() == 8);

    for (int i = 7; i >= 0; --i)
        pool.free(ptrs[i]);
    REQUIRE(pool.used_blocks() == 0);

    pool.shutdown();
}

TEST_CASE("Pool: freed slots are recycled") {
    ge::Pool pool;
    pool.init(32, 4);

    void* a = pool.alloc();
    void* b = pool.alloc();
    pool.free(b);
    void* c = pool.alloc();
    REQUIRE(c == b);

    pool.free(c);
    pool.free(a);
    pool.shutdown();
}

TEST_CASE("Pool: block alignment is at least max_align_t") {
    ge::Pool pool;
    pool.init(1, 16);
    for (int i = 0; i < 16; ++i) {
        void* p = pool.alloc();
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        REQUIRE((addr % alignof(std::max_align_t)) == 0);
        pool.free(p);
    }
    pool.shutdown();
}

TEST_CASE("Pool: minimum block size holds a pointer") {
    ge::Pool pool;
    pool.init(1, 4);
    REQUIRE(pool.block_size() >= sizeof(void*));
    void* a = pool.alloc();
    void* b = pool.alloc();
    pool.free(a);
    pool.free(b);
    pool.shutdown();
}

// --- Stack allocator ---

TEST_CASE("StackAlloc: init and shutdown") {
    ge::StackAlloc stack;
    stack.init(GE_KILOBYTES(4));
    REQUIRE(stack.capacity() == GE_KILOBYTES(4));
    REQUIRE(stack.used() == 0);
    stack.shutdown();
}

TEST_CASE("StackAlloc: alloc advances used") {
    ge::StackAlloc stack;
    stack.init(GE_KILOBYTES(4));

    void* p = stack.alloc(64);
    REQUIRE(p != nullptr);
    REQUIRE(stack.used() >= 64);

    stack.free(p);
    REQUIRE(stack.used() == 0);
    stack.shutdown();
}

TEST_CASE("StackAlloc: LIFO free restores state") {
    ge::StackAlloc stack;
    stack.init(GE_KILOBYTES(4));

    void* a = stack.alloc(32);
    ge::usize after_a = stack.used();

    void* b = stack.alloc(64);
    REQUIRE(stack.used() > after_a);

    stack.free(b);
    REQUIRE(stack.used() == after_a);

    stack.free(a);
    REQUIRE(stack.used() == 0);

    stack.shutdown();
}

TEST_CASE("StackAlloc: alignment — various powers of two") {
    ge::StackAlloc stack;
    stack.init(GE_MEGABYTES(1));

    const ge::usize aligns[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    void* ptrs[9];
    int count = 0;
    for (ge::usize align : aligns) {
        void* p = stack.alloc(1, align);
        REQUIRE(p != nullptr);
        auto addr = reinterpret_cast<std::uintptr_t>(p);
        REQUIRE((addr % align) == 0);
        ptrs[count++] = p;
    }

    for (int i = count - 1; i >= 0; --i)
        stack.free(ptrs[i]);

    stack.shutdown();
}

TEST_CASE("StackAlloc: reset clears all") {
    ge::StackAlloc stack;
    stack.init(GE_KILOBYTES(4));

    stack.alloc(128);
    stack.alloc(256);
    REQUIRE(stack.used() > 0);

    stack.reset();
    REQUIRE(stack.used() == 0);

    stack.shutdown();
}

// --- Allocator interface (polymorphic usage) ---

TEST_CASE("Allocator interface: arena") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(4));
    ge::Allocator alloc = arena.allocator();

    REQUIRE(alloc.is_valid());
    void* p = alloc.alloc(64);
    REQUIRE(p != nullptr);
    alloc.free(p);

    arena.shutdown();
}

TEST_CASE("Allocator interface: pool") {
    ge::Pool pool;
    pool.init(64, 8);
    ge::Allocator alloc = pool.allocator();

    REQUIRE(alloc.is_valid());
    void* p = alloc.alloc(64);
    REQUIRE(p != nullptr);
    alloc.free(p);

    pool.shutdown();
}

TEST_CASE("Allocator interface: stack") {
    ge::StackAlloc stack;
    stack.init(GE_KILOBYTES(4));
    ge::Allocator alloc = stack.allocator();

    REQUIRE(alloc.is_valid());
    void* p = alloc.alloc(128);
    REQUIRE(p != nullptr);
    alloc.free(p);

    stack.shutdown();
}

TEST_CASE("Allocator interface: alloc_t and alloc_n") {
    ge::Arena arena;
    arena.init(GE_KILOBYTES(4));
    ge::Allocator alloc = arena.allocator();

    struct alignas(16) Thing { float x, y, z, w; };
    Thing* single = alloc.alloc_t<Thing>();
    REQUIRE(single != nullptr);
    REQUIRE((reinterpret_cast<std::uintptr_t>(single) % alignof(Thing)) == 0);

    Thing* many = alloc.alloc_n<Thing>(10);
    REQUIRE(many != nullptr);
    REQUIRE((reinterpret_cast<std::uintptr_t>(many) % alignof(Thing)) == 0);

    arena.shutdown();
}
