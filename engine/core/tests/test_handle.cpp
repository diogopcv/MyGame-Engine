#include <doctest/doctest.h>
#include "core/handle.h"
#include "core/arena.h"

namespace {
    struct TestResource {
        int value = 0;
    };
}

TEST_CASE("Handle: create and lookup") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HandleTable<TestResource> table;
        table.init(16, arena.allocator());

        auto h = table.add({42});
        REQUIRE(h.is_valid());
        REQUIRE(table.count() == 1);

        auto* r = table.get(h);
        REQUIRE(r != nullptr);
        REQUIRE(r->value == 42);

        table.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Handle: remove and stale detection") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HandleTable<TestResource> table;
        table.init(16, arena.allocator());

        auto h = table.add({10});
        table.remove(h);
        REQUIRE(table.count() == 0);
        REQUIRE(table.get(h) == nullptr);

        table.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Handle: generation mismatch returns nullptr") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HandleTable<TestResource> table;
        table.init(16, arena.allocator());

        auto h1 = table.add({1});
        table.remove(h1);
        auto h2 = table.add({2});

        REQUIRE(h1.index == h2.index);
        REQUIRE(h1.generation != h2.generation);
        REQUIRE(table.get(h1) == nullptr);
        REQUIRE(table.get(h2)->value == 2);

        table.remove(h2);
        table.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Handle: free-list recycling") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HandleTable<TestResource> table;
        table.init(4, arena.allocator());

        auto a = table.add({1});
        auto b = table.add({2});
        auto c = table.add({3});
        auto d = table.add({4});
        REQUIRE(table.count() == 4);

        table.remove(b);
        table.remove(d);
        REQUIRE(table.count() == 2);

        auto e = table.add({5});
        auto f = table.add({6});
        REQUIRE(table.count() == 4);
        REQUIRE(table.get(e)->value == 5);
        REQUIRE(table.get(f)->value == 6);

        table.remove(a);
        table.remove(c);
        table.remove(e);
        table.remove(f);
        table.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Handle: invalid handle returns nullptr") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        ge::HandleTable<TestResource> table;
        table.init(4, arena.allocator());

        ge::Handle<TestResource> invalid;
        REQUIRE(!invalid.is_valid());
        REQUIRE(table.get(invalid) == nullptr);

        table.shutdown();
    }
    arena.shutdown();
}
