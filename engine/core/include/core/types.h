#pragma once
#include <cstdint>
#include <cstddef>

/**
 * @file types.h
 * @brief Engine-wide fixed-width type aliases (Layer 1 — Core).
 *
 * Short, unambiguous aliases for standard integer and floating-point types.
 * The platform layer (Layer 0) deliberately uses raw `std::` types instead,
 * so it depends on nothing.
 */
namespace ge {

using u8  = std::uint8_t;   ///< Unsigned  8-bit integer.
using u16 = std::uint16_t;  ///< Unsigned 16-bit integer.
using u32 = std::uint32_t;  ///< Unsigned 32-bit integer.
using u64 = std::uint64_t;  ///< Unsigned 64-bit integer.

using i8  = std::int8_t;    ///< Signed  8-bit integer.
using i16 = std::int16_t;   ///< Signed 16-bit integer.
using i32 = std::int32_t;   ///< Signed 32-bit integer.
using i64 = std::int64_t;   ///< Signed 64-bit integer.

using f32 = float;          ///< 32-bit IEEE 754 float.
using f64 = double;         ///< 64-bit IEEE 754 double.

using usize = std::size_t;  ///< Unsigned pointer-sized integer.

} // namespace ge

/** @brief Convert a value from kilobytes to bytes. */
#define GE_KILOBYTES(x) ((x) * 1024ull)
/** @brief Convert a value from megabytes to bytes. */
#define GE_MEGABYTES(x) (GE_KILOBYTES(x) * 1024ull)
/** @brief Convert a value from gigabytes to bytes. */
#define GE_GIGABYTES(x) (GE_MEGABYTES(x) * 1024ull)
