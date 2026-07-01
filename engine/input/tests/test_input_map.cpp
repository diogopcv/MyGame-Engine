#include <doctest/doctest.h>
#include "input/input_map.h"

using namespace ge;
using namespace ge::input;
using KC = ge::platform::KeyCode;

namespace {
// A fake key source lets us test resolution logic without a real OS window.
struct FakeKeys {
    bool w = false, a = false, s = false, d = false, space = false;

    bool key_down(KC key) const {
        switch (key) {
            case KC::W:     return w;
            case KC::A:     return a;
            case KC::S:     return s;
            case KC::D:     return d;
            case KC::Space: return space;
            default:        return false;
        }
    }
};
}

TEST_CASE("InputMap: unbound action/axis resolve to false/0") {
    InputMap map;
    REQUIRE_FALSE(map.action_down(string_id("Jump")));
    REQUIRE(map.axis(string_id("MoveForward")) == 0.0f);
}

TEST_CASE("InputMap: action resolution reflects key state") {
    InputMap map;
    map.bind_action(string_id("Jump"), KC::Space);

    FakeKeys keys;
    map.resolve(keys);
    REQUIRE_FALSE(map.action_down(string_id("Jump")));

    keys.space = true;
    map.resolve(keys);
    REQUIRE(map.action_down(string_id("Jump")));
}

TEST_CASE("InputMap: axis resolves +1/-1/0 from a key pair") {
    InputMap map;
    map.bind_axis(string_id("MoveForward"), KC::W, KC::S);

    FakeKeys keys;
    map.resolve(keys);
    REQUIRE(map.axis(string_id("MoveForward")) == 0.0f);

    keys.w = true;
    map.resolve(keys);
    REQUIRE(map.axis(string_id("MoveForward")) == 1.0f);

    keys.w = false;
    keys.s = true;
    map.resolve(keys);
    REQUIRE(map.axis(string_id("MoveForward")) == -1.0f);
}

TEST_CASE("InputMap: both keys of a pair held cancels out to 0") {
    InputMap map;
    map.bind_axis(string_id("MoveForward"), KC::W, KC::S);

    FakeKeys keys;
    keys.w = true;
    keys.s = true;
    map.resolve(keys);
    REQUIRE(map.axis(string_id("MoveForward")) == 0.0f);
}

TEST_CASE("InputMap: multiple independent bindings don't cross-talk") {
    InputMap map;
    map.bind_axis(string_id("MoveForward"), KC::W, KC::S);
    map.bind_axis(string_id("MoveRight"), KC::D, KC::A);
    map.bind_action(string_id("Jump"), KC::Space);

    FakeKeys keys;
    keys.d = true;
    keys.space = true;
    map.resolve(keys);

    REQUIRE(map.axis(string_id("MoveForward")) == 0.0f);
    REQUIRE(map.axis(string_id("MoveRight")) == 1.0f);
    REQUIRE(map.action_down(string_id("Jump")));
}
