#include <doctest/doctest.h>
#include "core/arena.h"
#include "world/world.h"

using namespace ge;
using namespace ge::world;

TEST_CASE("World: create/destroy entity lifecycle") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity();
        REQUIRE(a.is_valid());
        REQUIRE(w.is_alive(a));

        w.destroy_entity(a);
        REQUIRE_FALSE(w.is_alive(a));

        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: destroyed entity handle is stale after recycling") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(4, arena.allocator());

        Entity a = w.create_entity();
        w.destroy_entity(a);
        Entity b = w.create_entity();

        REQUIRE(a.index == b.index);
        REQUIRE(a.generation != b.generation);
        REQUIRE_FALSE(w.is_alive(a));
        REQUIRE(w.is_alive(b));

        w.destroy_entity(b);
        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: multiple entities are independently alive") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(8, arena.allocator());

        Entity a = w.create_entity();
        Entity b = w.create_entity();
        Entity c = w.create_entity();

        w.destroy_entity(b);

        REQUIRE(w.is_alive(a));
        REQUIRE_FALSE(w.is_alive(b));
        REQUIRE(w.is_alive(c));

        w.destroy_entity(a);
        w.destroy_entity(c);
        w.shutdown();
    }
    arena.shutdown();
}
