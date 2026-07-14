#pragma once
#include "core/types.h"
#include <cmath>

/**
 * @file vec.h
 * @brief 2D, 3D, and 4D vector types with arithmetic operators (Layer 1 — Core).
 */
namespace ge {

/** @brief 2-component floating-point vector. */
struct vec2 { f32 x = 0, y = 0; };
/** @brief 3-component floating-point vector. */
struct vec3 { f32 x = 0, y = 0, z = 0; };
/** @brief 4-component floating-point vector. */
struct vec4 { f32 x = 0, y = 0, z = 0, w = 0; };

/** @name vec2 arithmetic
 *  @{ */
inline vec2  operator+(vec2 a, vec2 b)   { return {a.x+b.x, a.y+b.y}; }
inline vec2  operator-(vec2 a, vec2 b)   { return {a.x-b.x, a.y-b.y}; }
inline vec2  operator-(vec2 a)           { return {-a.x, -a.y}; }
inline vec2  operator*(vec2 a, f32 s)    { return {a.x*s, a.y*s}; }
inline vec2  operator*(f32 s, vec2 a)    { return a * s; }
inline vec2  operator/(vec2 a, f32 s)    { return {a.x/s, a.y/s}; }
inline vec2& operator+=(vec2& a, vec2 b) { a = a + b; return a; }
inline vec2& operator-=(vec2& a, vec2 b) { a = a - b; return a; }
inline vec2& operator*=(vec2& a, f32 s)  { a = a * s; return a; }
/** @} */

/** @brief Dot product of two vec2. */
inline f32   dot(vec2 a, vec2 b)         { return a.x*b.x + a.y*b.y; }
/** @brief Squared length of a vec2. */
inline f32   length_sq(vec2 a)           { return dot(a, a); }
/** @brief Euclidean length of a vec2. */
inline f32   length(vec2 a)              { return std::sqrt(length_sq(a)); }
/** @brief Return a unit-length vec2 in the same direction. */
inline vec2  normalize(vec2 a)           { return a / length(a); }

/** @name vec3 arithmetic
 *  @{ */
inline vec3  operator+(vec3 a, vec3 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline vec3  operator-(vec3 a, vec3 b)   { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline vec3  operator-(vec3 a)           { return {-a.x, -a.y, -a.z}; }
inline vec3  operator*(vec3 a, f32 s)    { return {a.x*s, a.y*s, a.z*s}; }
inline vec3  operator*(f32 s, vec3 a)    { return a * s; }
inline vec3  operator/(vec3 a, f32 s)    { return {a.x/s, a.y/s, a.z/s}; }
inline vec3& operator+=(vec3& a, vec3 b) { a = a + b; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a = a - b; return a; }
inline vec3& operator*=(vec3& a, f32 s)  { a = a * s; return a; }
/** @} */

/** @brief Dot product of two vec3. */
inline f32   dot(vec3 a, vec3 b)         { return a.x*b.x + a.y*b.y + a.z*b.z; }
/** @brief Squared length of a vec3. */
inline f32   length_sq(vec3 a)           { return dot(a, a); }
/** @brief Euclidean length of a vec3. */
inline f32   length(vec3 a)              { return std::sqrt(length_sq(a)); }
/** @brief Return a unit-length vec3 in the same direction. */
inline vec3  normalize(vec3 a)           { return a / length(a); }
/**
 * @brief Linear interpolation between two vec3.
 * @param a Value at t = 0.
 * @param b Value at t = 1.
 * @param t Interpolation parameter (not clamped).
 */
inline vec3  lerp(vec3 a, vec3 b, f32 t) { return a + (b - a) * t; }
/**
 * @brief Cross product of two vec3 (left-handed).
 * @param a First vector.
 * @param b Second vector.
 * @return Vector perpendicular to both @p a and @p b.
 */
inline vec3  cross(vec3 a, vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}

/** @name vec4 arithmetic
 *  @{ */
inline vec4  operator+(vec4 a, vec4 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
inline vec4  operator-(vec4 a, vec4 b)   { return {a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w}; }
inline vec4  operator-(vec4 a)           { return {-a.x, -a.y, -a.z, -a.w}; }
inline vec4  operator*(vec4 a, f32 s)    { return {a.x*s, a.y*s, a.z*s, a.w*s}; }
inline vec4  operator*(f32 s, vec4 a)    { return a * s; }
inline vec4  operator/(vec4 a, f32 s)    { return {a.x/s, a.y/s, a.z/s, a.w/s}; }
inline vec4& operator+=(vec4& a, vec4 b) { a = a + b; return a; }
inline vec4& operator-=(vec4& a, vec4 b) { a = a - b; return a; }
inline vec4& operator*=(vec4& a, f32 s)  { a = a * s; return a; }
/** @} */

/** @brief Dot product of two vec4. */
inline f32   dot(vec4 a, vec4 b)         { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
/** @brief Squared length of a vec4. */
inline f32   length_sq(vec4 a)           { return dot(a, a); }
/** @brief Euclidean length of a vec4. */
inline f32   length(vec4 a)              { return std::sqrt(length_sq(a)); }
/** @brief Return a unit-length vec4 in the same direction. */
inline vec4  normalize(vec4 a)           { return a / length(a); }

} // namespace ge
