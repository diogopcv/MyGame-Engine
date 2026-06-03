#pragma once
#include <cstdint>
#include <cstddef>

// The engine's primitive vocabulary. Short, unambiguous, fixed-width.
// These live in core (Layer 1); the platform layer below deliberately uses
// the raw std:: types instead, so it depends on nothing.
namespace ge {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;

} // namespace ge

// Readable byte-size literals: GE_MEGABYTES(64) -> 67108864.
#define GE_KILOBYTES(x) ((x) * 1024ull)
#define GE_MEGABYTES(x) (GE_KILOBYTES(x) * 1024ull)
#define GE_GIGABYTES(x) (GE_MEGABYTES(x) * 1024ull)
