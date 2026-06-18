#include <doctest/doctest.h>
#include "core/serialize.h"
#include "core/arena.h"
#include <cstring>

namespace {

struct Point {
    ge::f32 x, y, z;

    GE_REFLECT_BEGIN(Point)
        GE_FIELD(x, F32)
        GE_FIELD(y, F32)
        GE_FIELD(z, F32)
    GE_REFLECT_END(Point)
};

struct Composite {
    Point  position;
    ge::u32 id;
    ge::f64 weight;

    GE_REFLECT_BEGIN(Composite)
        GE_FIELD_STRUCT(position, Point)
        GE_FIELD(id, U32)
        GE_FIELD(weight, F64)
    GE_REFLECT_END(Composite)
};

struct AllPrimitives {
    bool    b;
    ge::u8  u8v;
    ge::u16 u16v;
    ge::u32 u32v;
    ge::u64 u64v;
    ge::i8  i8v;
    ge::i16 i16v;
    ge::i32 i32v;
    ge::i64 i64v;
    ge::f32 f32v;
    ge::f64 f64v;

    GE_REFLECT_BEGIN(AllPrimitives)
        GE_FIELD(b,    Bool)
        GE_FIELD(u8v,  U8)
        GE_FIELD(u16v, U16)
        GE_FIELD(u32v, U32)
        GE_FIELD(u64v, U64)
        GE_FIELD(i8v,  I8)
        GE_FIELD(i16v, I16)
        GE_FIELD(i32v, I32)
        GE_FIELD(i64v, I64)
        GE_FIELD(f32v, F32)
        GE_FIELD(f64v, F64)
    GE_REFLECT_END(AllPrimitives)
};

} // anonymous namespace

TEST_CASE("Serialize: simple struct round-trip") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Point src{1.0f, 2.5f, -3.0f};
        ge::BinaryWriter w(arena.allocator());
        ge::serialize(w, &src, *Point::ge_type_info());

        Point dst{};
        ge::BinaryReader r(w.data(), w.size());
        REQUIRE(ge::deserialize(r, &dst, *Point::ge_type_info()));
        REQUIRE(dst.x == src.x);
        REQUIRE(dst.y == src.y);
        REQUIRE(dst.z == src.z);
    }
    arena.shutdown();
}

TEST_CASE("Serialize: nested struct round-trip") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Composite src{};
        src.position = {10.0f, 20.0f, 30.0f};
        src.id = 42;
        src.weight = 99.99;

        ge::BinaryWriter w(arena.allocator());
        ge::serialize(w, &src, *Composite::ge_type_info());

        Composite dst{};
        ge::BinaryReader r(w.data(), w.size());
        REQUIRE(ge::deserialize(r, &dst, *Composite::ge_type_info()));
        REQUIRE(dst.position.x == src.position.x);
        REQUIRE(dst.position.y == src.position.y);
        REQUIRE(dst.position.z == src.position.z);
        REQUIRE(dst.id == src.id);
        REQUIRE(dst.weight == doctest::Approx(src.weight));
    }
    arena.shutdown();
}

TEST_CASE("Serialize: all primitive types round-trip") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        AllPrimitives src{};
        src.b    = true;
        src.u8v  = 255;
        src.u16v = 65535;
        src.u32v = 0xDEADBEEF;
        src.u64v = 0xCAFEBABEDEADFACEull;
        src.i8v  = -42;
        src.i16v = -1234;
        src.i32v = -999999;
        src.i64v = -123456789012345LL;
        src.f32v = 3.14159f;
        src.f64v = 2.71828182845904;

        ge::BinaryWriter w(arena.allocator());
        ge::serialize(w, &src, *AllPrimitives::ge_type_info());

        AllPrimitives dst{};
        ge::BinaryReader r(w.data(), w.size());
        REQUIRE(ge::deserialize(r, &dst, *AllPrimitives::ge_type_info()));
        REQUIRE(dst.b    == src.b);
        REQUIRE(dst.u8v  == src.u8v);
        REQUIRE(dst.u16v == src.u16v);
        REQUIRE(dst.u32v == src.u32v);
        REQUIRE(dst.u64v == src.u64v);
        REQUIRE(dst.i8v  == src.i8v);
        REQUIRE(dst.i16v == src.i16v);
        REQUIRE(dst.i32v == src.i32v);
        REQUIRE(dst.i64v == src.i64v);
        REQUIRE(dst.f32v == src.f32v);
        REQUIRE(dst.f64v == doctest::Approx(src.f64v));
    }
    arena.shutdown();
}

TEST_CASE("Serialize: deserialization detects truncated data") {
    ge::Arena arena; arena.init(GE_KILOBYTES(64));
    {
        Point src{1.0f, 2.0f, 3.0f};
        ge::BinaryWriter w(arena.allocator());
        ge::serialize(w, &src, *Point::ge_type_info());

        Point dst{};
        ge::BinaryReader r(w.data(), w.size() / 2);
        REQUIRE(!ge::deserialize(r, &dst, *Point::ge_type_info()));
    }
    arena.shutdown();
}
