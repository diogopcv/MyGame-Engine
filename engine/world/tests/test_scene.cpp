#include <doctest/doctest.h>
#include "core/arena.h"
#include "world/scene.h"
#include <cstdio>

using namespace ge;
using namespace ge::world;

namespace {
constexpr const char* kTestPath = "test_scene_roundtrip.tmp";
}

TEST_CASE("Scene: create_entity registers a lookup name") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Scene scene; scene.init(16, arena.allocator());

        Entity e = scene.create_entity("player");
        REQUIRE(scene.find(string_id("player")) == e);
        REQUIRE_FALSE(scene.find(string_id("nonexistent")).is_valid());

        scene.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Scene: destroy_entity removes it from the roster and world") {
    Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Scene scene; scene.init(16, arena.allocator());

        Entity e = scene.create_entity("thing");
        scene.destroy_entity(e);

        REQUIRE_FALSE(scene.world().is_alive(e));

        scene.shutdown();
    }
    arena.shutdown();
}

TEST_CASE("Scene: save/load round-trips Transform components") {
    Arena arena; arena.init(GE_KILOBYTES(256));
    {
        Scene scene; scene.init(16, arena.allocator());

        Entity player = scene.create_entity("player");
        Transform t{};
        t.position = {1.0f, 2.0f, 3.0f};
        t.rotation = quat_from_axis_angle({0, 1, 0}, 0.5f);
        t.scale    = {2.0f, 2.0f, 2.0f};
        scene.world().add_component<Transform>(player, t);

        Entity marker = scene.create_entity(); // anonymous, no Transform

        REQUIRE(scene.save(kTestPath));

        Scene loaded; loaded.init(16, arena.allocator());
        REQUIRE(loaded.load(kTestPath));

        Entity loaded_player = loaded.find(string_id("player"));
        REQUIRE(loaded_player.is_valid());
        REQUIRE(loaded.world().is_alive(loaded_player));

        Transform* lt = loaded.world().get_component<Transform>(loaded_player);
        REQUIRE(lt != nullptr);
        REQUIRE(lt->position.x == doctest::Approx(1.0f));
        REQUIRE(lt->position.y == doctest::Approx(2.0f));
        REQUIRE(lt->position.z == doctest::Approx(3.0f));
        REQUIRE(lt->scale.x == doctest::Approx(2.0f));
        REQUIRE(lt->rotation.w == doctest::Approx(t.rotation.w));

        (void)marker;

        loaded.shutdown();
        scene.shutdown();
        std::remove(kTestPath);
    }
    arena.shutdown();
}

TEST_CASE("Scene: load resets any prior contents") {
    Arena arena; arena.init(GE_KILOBYTES(256));
    {
        Scene scene; scene.init(16, arena.allocator());
        scene.create_entity("stale");
        REQUIRE(scene.save(kTestPath));

        Scene target; target.init(16, arena.allocator());
        Entity leftover = target.create_entity("leftover");
        REQUIRE(target.load(kTestPath));

        REQUIRE_FALSE(target.world().is_alive(leftover));
        REQUIRE(target.find(string_id("stale")).is_valid());
        REQUIRE_FALSE(target.find(string_id("leftover")).is_valid());

        target.shutdown();
        scene.shutdown();
        std::remove(kTestPath);
    }
    arena.shutdown();
}
