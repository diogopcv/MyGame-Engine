#include <doctest/doctest.h>
#include "platform/window.h"

TEST_CASE("Window: create, input polling, destroy") {
    ge::platform::Window window;
    ge::platform::WindowDesc desc{};
    desc.title  = "MyEngine - input test";
    desc.width  = 320;
    desc.height = 240;
    REQUIRE(window.create(desc));

    window.pump_events();

    CHECK_FALSE(window.key_down(ge::platform::KeyCode::W));
    CHECK_FALSE(window.key_down(ge::platform::KeyCode::Escape));
    CHECK_FALSE(window.mouse_button_down(ge::platform::MouseButton::Left));

    float mx = -1, my = -1;
    window.mouse_position(mx, my);
    CHECK(mx >= 0.0f);

    float dx = -1, dy = -1;
    window.mouse_delta(dx, dy);
    CHECK(dx == 0.0f);
    CHECK(dy == 0.0f);

    CHECK_FALSE(window.should_close());

    window.destroy();
}
