#include <doctest/doctest.h>
#include "core/cvar.h"

TEST_CASE("CVar: register and find int") {
    ge::cvar_system().init();

    ge::CVar* cv = ge::cvar_system().register_int("r_width", 1280, "render width");
    REQUIRE(cv != nullptr);
    REQUIRE(cv->type == ge::CVarType::Int);
    REQUIRE(cv->i == 1280);

    ge::CVar* found = ge::cvar_system().find("r_width");
    REQUIRE(found == cv);

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: register and find float") {
    ge::cvar_system().init();

    ge::CVar* cv = ge::cvar_system().register_float("r_fov", 60.0f, "field of view");
    REQUIRE(cv->type == ge::CVarType::Float);
    REQUIRE(cv->f == doctest::Approx(60.0f));

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: register and find bool") {
    ge::cvar_system().init();

    ge::CVar* cv = ge::cvar_system().register_bool("r_vsync", true, "vsync enabled");
    REQUIRE(cv->type == ge::CVarType::Bool);
    REQUIRE(cv->b == true);

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: execute command sets int") {
    ge::cvar_system().init();
    ge::cvar_system().register_int("test_int", 0, "test");

    REQUIRE(ge::cvar_system().execute("test_int 42"));
    REQUIRE(ge::cvar_system().find("test_int")->i == 42);

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: execute command sets float") {
    ge::cvar_system().init();
    ge::cvar_system().register_float("test_float", 0.0f, "test");

    REQUIRE(ge::cvar_system().execute("test_float 3.14"));
    REQUIRE(ge::cvar_system().find("test_float")->f == doctest::Approx(3.14f));

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: execute command sets bool") {
    ge::cvar_system().init();
    ge::cvar_system().register_bool("test_bool", false, "test");

    REQUIRE(ge::cvar_system().execute("test_bool true"));
    REQUIRE(ge::cvar_system().find("test_bool")->b == true);

    REQUIRE(ge::cvar_system().execute("test_bool 0"));
    REQUIRE(ge::cvar_system().find("test_bool")->b == false);

    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: execute unknown name returns false") {
    ge::cvar_system().init();
    REQUIRE(!ge::cvar_system().execute("nonexistent 123"));
    ge::cvar_system().shutdown();
}

TEST_CASE("CVar: find missing returns nullptr") {
    ge::cvar_system().init();
    REQUIRE(ge::cvar_system().find("missing") == nullptr);
    ge::cvar_system().shutdown();
}
