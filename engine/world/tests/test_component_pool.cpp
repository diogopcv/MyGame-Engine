#include <doctest/doctest.h>
#include "core/arena.h"
#include "world/world.h"

using namespace ge;
using namespace ge::world;

namespace {
struct Position { f32 x = 0, y = 0; };
struct Velocity { f32 dx = 0, dy = 0; };
}

TEST_CASE("World: add/get/has component") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity e = w.create_entity();
        REQUIRE_FALSE(w.has_component<Position>(e));
        REQUIRE(w.get_component<Position>(e) == nullptr);

        w.add_component<Position>(e, {1.0f, 2.0f});
        REQUIRE(w.has_component<Position>(e));

        Position* p = w.get_component<Position>(e);
        REQUIRE(p != nullptr);
        REQUIRE(p->x == 1.0f);
        REQUIRE(p->y == 2.0f);

        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: remove component") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity e = w.create_entity();
        w.add_component<Position>(e, {1.0f, 2.0f});
        w.remove_component<Position>(e);

        REQUIRE_FALSE(w.has_component<Position>(e));
        REQUIRE(w.get_component<Position>(e) == nullptr);

        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: swap-remove does not corrupt other entities' components") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity();
        Entity b = w.create_entity();
        Entity c = w.create_entity();

        w.add_component<Position>(a, {1.0f, 1.0f});
        w.add_component<Position>(b, {2.0f, 2.0f});
        w.add_component<Position>(c, {3.0f, 3.0f});

        // Remove the first (dense index 0) — b's swap-remove fix-up must
        // still find b at its new dense slot, and c must be untouched.
        w.remove_component<Position>(a);

        REQUIRE_FALSE(w.has_component<Position>(a));
        REQUIRE(w.get_component<Position>(b)->x == 2.0f);
        REQUIRE(w.get_component<Position>(c)->x == 3.0f);

        w.destroy_entity(a);
        w.destroy_entity(b);
        w.destroy_entity(c);
        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: destroying an entity strips it from every pool") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity e = w.create_entity();
        w.add_component<Position>(e, {1.0f, 2.0f});
        w.add_component<Velocity>(e, {0.5f, 0.5f});

        w.destroy_entity(e);

        // Handle is stale now; has_component must not read freed/reused slots.
        REQUIRE_FALSE(w.has_component<Position>(e));
        REQUIRE_FALSE(w.has_component<Velocity>(e));

        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: independent component types don't interfere") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity e = w.create_entity();
        w.add_component<Position>(e, {1.0f, 2.0f});
        w.add_component<Velocity>(e, {3.0f, 4.0f});

        REQUIRE(w.get_component<Position>(e)->x == 1.0f);
        REQUIRE(w.get_component<Velocity>(e)->dx == 3.0f);

        w.remove_component<Position>(e);
        REQUIRE_FALSE(w.has_component<Position>(e));
        REQUIRE(w.has_component<Velocity>(e)); // unaffected by removing the other type

        w.destroy_entity(e);
        w.shutdown();
    }
    arena.shutdown();
}
