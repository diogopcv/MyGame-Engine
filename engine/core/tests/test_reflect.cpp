#include <doctest/doctest.h>
#include "core/reflect.h"
#include "core/string_id.h"

namespace {

struct SimpleStruct {
    ge::f32 x;
    ge::f32 y;
    ge::f32 z;
    ge::u32 flags;

    GE_REFLECT_BEGIN(SimpleStruct)
        GE_FIELD(x, F32)
        GE_FIELD(y, F32)
        GE_FIELD(z, F32)
        GE_FIELD(flags, U32)
    GE_REFLECT_END(SimpleStruct)
};

struct Inner {
    ge::i32 a;
    ge::i32 b;

    GE_REFLECT_BEGIN(Inner)
        GE_FIELD(a, I32)
        GE_FIELD(b, I32)
    GE_REFLECT_END(Inner)
};

struct Outer {
    Inner inner;
    ge::f64 value;

    GE_REFLECT_BEGIN(Outer)
        GE_FIELD_STRUCT(inner, Inner)
        GE_FIELD(value, F64)
    GE_REFLECT_END(Outer)
};

} // anonymous namespace

TEST_CASE("Reflect: field enumeration — count and names") {
    const ge::TypeInfo* info = SimpleStruct::ge_type_info();
    REQUIRE(info != nullptr);
    REQUIRE(info->field_count == 4);
    REQUIRE(info->fields[0].name == ge::string_id("x"));
    REQUIRE(info->fields[1].name == ge::string_id("y"));
    REQUIRE(info->fields[2].name == ge::string_id("z"));
    REQUIRE(info->fields[3].name == ge::string_id("flags"));
}

TEST_CASE("Reflect: field types") {
    const ge::TypeInfo* info = SimpleStruct::ge_type_info();
    REQUIRE(info->fields[0].type == ge::FieldType::F32);
    REQUIRE(info->fields[3].type == ge::FieldType::U32);
}

TEST_CASE("Reflect: field offsets match offsetof") {
    const ge::TypeInfo* info = SimpleStruct::ge_type_info();
    REQUIRE(info->fields[0].offset == offsetof(SimpleStruct, x));
    REQUIRE(info->fields[1].offset == offsetof(SimpleStruct, y));
    REQUIRE(info->fields[2].offset == offsetof(SimpleStruct, z));
    REQUIRE(info->fields[3].offset == offsetof(SimpleStruct, flags));
}

TEST_CASE("Reflect: type size and alignment") {
    const ge::TypeInfo* info = SimpleStruct::ge_type_info();
    REQUIRE(info->size == sizeof(SimpleStruct));
    REQUIRE(info->alignment == alignof(SimpleStruct));
}

TEST_CASE("Reflect: nested struct") {
    const ge::TypeInfo* info = Outer::ge_type_info();
    REQUIRE(info->field_count == 2);
    REQUIRE(info->fields[0].type == ge::FieldType::Struct);
    REQUIRE(info->fields[0].nested != nullptr);
    REQUIRE(info->fields[0].nested->field_count == 2);
    REQUIRE(info->fields[0].nested->fields[0].name == ge::string_id("a"));
}

TEST_CASE("Reflect: type registry") {
    const ge::TypeInfo* info = SimpleStruct::ge_type_info();
    ge::type_register(info);

    const ge::TypeInfo* found = ge::type_find(ge::string_id("SimpleStruct"));
    REQUIRE(found != nullptr);
    REQUIRE(found == info);

    REQUIRE(ge::type_find(ge::string_id("Nonexistent")) == nullptr);
}
