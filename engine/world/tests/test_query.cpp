#include <doctest/doctest.h>
#include "core/arena.h"
#include "world/world.h"

using namespace ge;
using namespace ge::world;

namespace {
struct Position { f32 x = 0, y = 0; };
struct Velocity { f32 dx = 0, dy = 0; };
}

TEST_CASE("World: each<T> visits exactly the entities holding T") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity();
        Entity b = w.create_entity();
        Entity c = w.create_entity(); // no Position

        w.add_component<Position>(a, {1.0f, 0.0f});
        w.add_component<Position>(b, {2.0f, 0.0f});
        (void)c;

        u32 visits = 0;
        f32 sum_x = 0.0f;
        w.each<Position>([&](Entity e, Position& p) {
            ++visits;
            sum_x += p.x;
            REQUIRE(w.is_alive(e));
        });

        REQUIRE(visits == 2);
        REQUIRE(sum_x == 3.0f);

        w.destroy_entity(a);
        w.destroy_entity(b);
        w.destroy_entity(c);
        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: each<T> mutation is visible through get_component") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity();
        w.add_component<Position>(a, {1.0f, 1.0f});

        w.each<Position>([](Entity, Position& p) { p.x = 99.0f; });

        REQUIRE(w.get_component<Position>(a)->x == 99.0f);

        w.destroy_entity(a);
        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: each<A, B> joins only entities with both components") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity(); // Position + Velocity
        Entity b = w.create_entity(); // Position only
        Entity c = w.create_entity(); // Velocity only

        w.add_component<Position>(a, {1.0f, 0.0f});
        w.add_component<Velocity>(a, {10.0f, 0.0f});
        w.add_component<Position>(b, {2.0f, 0.0f});
        w.add_component<Velocity>(c, {20.0f, 0.0f});

        u32 visits = 0;
        w.each<Position, Velocity>([&](Entity e, Position& p, Velocity& v) {
            ++visits;
            REQUIRE(e == a);
            REQUIRE(p.x == 1.0f);
            REQUIRE(v.dx == 10.0f);
        });
        REQUIRE(visits == 1);

        w.destroy_entity(a);
        w.destroy_entity(b);
        w.destroy_entity(c);
        w.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("World: each<A, B> integrates a simple movement system") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        World w; w.init(16, arena.allocator());

        Entity a = w.create_entity();
        w.add_component<Position>(a, {0.0f, 0.0f});
        w.add_component<Velocity>(a, {1.0f, 2.0f});

        const f32 dt = 0.5f;
        w.each<Position, Velocity>([dt](Entity, Position& p, Velocity& v) {
            p.x += v.dx * dt;
            p.y += v.dy * dt;
        });

        Position* p = w.get_component<Position>(a);
        REQUIRE(p->x == doctest::Approx(0.5f));
        REQUIRE(p->y == doctest::Approx(1.0f));

        w.destroy_entity(a);
        w.shutdown();
    }
    arena.shutdown();
}
