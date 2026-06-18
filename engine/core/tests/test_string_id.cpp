#include <doctest/doctest.h>
#include "core/string_id.h"
#include <string>

TEST_CASE("StringId: consistent hashing") {
    ge::StringId a = ge::string_id("hello");
    ge::StringId b = ge::string_id("hello");
    REQUIRE(a == b);
}

TEST_CASE("StringId: different strings give different hashes") {
    ge::StringId a = ge::string_id("alpha");
    ge::StringId b = ge::string_id("beta");
    REQUIRE(a != b);
}

TEST_CASE("StringId: empty string hashes to the FNV offset basis") {
    ge::StringId id = ge::string_id("");
    REQUIRE(id.hash == 14695981039346656037ull);
}

TEST_CASE("StringId: constexpr evaluation") {
    constexpr ge::StringId id = ge::string_id("constexpr_test");
    REQUIRE(id.hash != 0);
    REQUIRE(id == ge::string_id("constexpr_test"));
}

TEST_CASE("StringId: GE_SID macro") {
    ge::StringId a = GE_SID("macro_test");
    ge::StringId b = ge::string_id("macro_test");
    REQUIRE(a == b);
}

#ifndef NDEBUG
TEST_CASE("StringId: debug reverse lookup") {
    ge::StringId id = GE_SID("debug_lookup");
    const char* str = ge::string_id_str(id);
    REQUIRE(str != nullptr);
    REQUIRE(std::string(str) == "debug_lookup");
}
#endif

TEST_CASE("StringId: ordering") {
    ge::StringId a = ge::string_id("aaa");
    ge::StringId b = ge::string_id("zzz");
    REQUIRE((a < b || b < a));
}
