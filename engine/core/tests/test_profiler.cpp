#include <doctest/doctest.h>
#include "core/profiler.h"
#include "platform/timer.h"

TEST_CASE("Profiler: init and shutdown") {
    ge::profiler().init();
    ge::profiler().shutdown();
}

TEST_CASE("Profiler: begin/end frame records time") {
    ge::platform::time_init();
    ge::profiler().init();

    ge::profiler().begin_frame();
    ge::profiler().end_frame();

    const ge::ProfileFrame& f = ge::profiler().frame(0);
    REQUIRE(f.frame_time >= 0.0);

    ge::profiler().shutdown();
}

TEST_CASE("Profiler: push/pop scope") {
    ge::platform::time_init();
    ge::profiler().init();

    ge::profiler().begin_frame();
    ge::profiler().push_scope(ge::string_id("test_scope"));
    ge::profiler().pop_scope();
    ge::profiler().end_frame();

    const ge::ProfileFrame& f = ge::profiler().frame(0);
    REQUIRE(f.scope_count == 1);
    REQUIRE(f.scopes[0].name == ge::string_id("test_scope"));
    REQUIRE(f.scopes[0].end >= f.scopes[0].start);
    REQUIRE(f.scopes[0].depth == 0);

    ge::profiler().shutdown();
}

TEST_CASE("Profiler: nested scopes") {
    ge::platform::time_init();
    ge::profiler().init();

    ge::profiler().begin_frame();
    ge::profiler().push_scope(ge::string_id("outer"));
    ge::profiler().push_scope(ge::string_id("inner"));
    ge::profiler().pop_scope();
    ge::profiler().pop_scope();
    ge::profiler().end_frame();

    const ge::ProfileFrame& f = ge::profiler().frame(0);
    REQUIRE(f.scope_count == 2);
    REQUIRE(f.scopes[0].depth == 0);
    REQUIRE(f.scopes[1].depth == 1);

    ge::profiler().shutdown();
}

TEST_CASE("Profiler: GE_PROFILE_SCOPE macro") {
    ge::platform::time_init();
    ge::profiler().init();

    ge::profiler().begin_frame();
    {
        GE_PROFILE_SCOPE("macro_test");
    }
    ge::profiler().end_frame();

    const ge::ProfileFrame& f = ge::profiler().frame(0);
    REQUIRE(f.scope_count == 1);

    ge::profiler().shutdown();
}
