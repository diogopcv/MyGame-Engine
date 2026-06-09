#pragma once
#include "core/types.h"
#include <cmath>

namespace ge {

struct vec2 { f32 x = 0, y = 0; };
struct vec3 { f32 x = 0, y = 0, z = 0; };
struct vec4 { f32 x = 0, y = 0, z = 0, w = 0; };

// --- vec2 -------------------------------------------------------------------
inline vec2  operator+(vec2 a, vec2 b)   { return {a.x+b.x, a.y+b.y}; }
inline vec2  operator-(vec2 a, vec2 b)   { return {a.x-b.x, a.y-b.y}; }
inline vec2  operator-(vec2 a)           { return {-a.x, -a.y}; }
inline vec2  operator*(vec2 a, f32 s)    { return {a.x*s, a.y*s}; }
inline vec2  operator*(f32 s, vec2 a)    { return a * s; }
inline vec2  operator/(vec2 a, f32 s)    { return {a.x/s, a.y/s}; }
inline vec2& operator+=(vec2& a, vec2 b) { a = a + b; return a; }
inline vec2& operator-=(vec2& a, vec2 b) { a = a - b; return a; }
inline vec2& operator*=(vec2& a, f32 s)  { a = a * s; return a; }
inline f32   dot(vec2 a, vec2 b)         { return a.x*b.x + a.y*b.y; }
inline f32   length_sq(vec2 a)           { return dot(a, a); }
inline f32   length(vec2 a)              { return std::sqrt(length_sq(a)); }
inline vec2  normalize(vec2 a)           { return a / length(a); }

// --- vec3 -------------------------------------------------------------------
inline vec3  operator+(vec3 a, vec3 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline vec3  operator-(vec3 a, vec3 b)   { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline vec3  operator-(vec3 a)           { return {-a.x, -a.y, -a.z}; }
inline vec3  operator*(vec3 a, f32 s)    { return {a.x*s, a.y*s, a.z*s}; }
inline vec3  operator*(f32 s, vec3 a)    { return a * s; }
inline vec3  operator/(vec3 a, f32 s)    { return {a.x/s, a.y/s, a.z/s}; }
inline vec3& operator+=(vec3& a, vec3 b) { a = a + b; return a; }
inline vec3& operator-=(vec3& a, vec3 b) { a = a - b; return a; }
inline vec3& operator*=(vec3& a, f32 s)  { a = a * s; return a; }
inline f32   dot(vec3 a, vec3 b)         { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline f32   length_sq(vec3 a)           { return dot(a, a); }
inline f32   length(vec3 a)              { return std::sqrt(length_sq(a)); }
inline vec3  normalize(vec3 a)           { return a / length(a); }
inline vec3  cross(vec3 a, vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}

// --- vec4 -------------------------------------------------------------------
inline vec4  operator+(vec4 a, vec4 b)   { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
inline vec4  operator-(vec4 a, vec4 b)   { return {a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w}; }
inline vec4  operator-(vec4 a)           { return {-a.x, -a.y, -a.z, -a.w}; }
inline vec4  operator*(vec4 a, f32 s)    { return {a.x*s, a.y*s, a.z*s, a.w*s}; }
inline vec4  operator*(f32 s, vec4 a)    { return a * s; }
inline vec4  operator/(vec4 a, f32 s)    { return {a.x/s, a.y/s, a.z/s, a.w/s}; }
inline vec4& operator+=(vec4& a, vec4 b) { a = a + b; return a; }
inline vec4& operator-=(vec4& a, vec4 b) { a = a - b; return a; }
inline vec4& operator*=(vec4& a, f32 s)  { a = a * s; return a; }
inline f32   dot(vec4 a, vec4 b)         { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
inline f32   length_sq(vec4 a)           { return dot(a, a); }
inline f32   length(vec4 a)              { return std::sqrt(length_sq(a)); }
inline vec4  normalize(vec4 a)           { return a / length(a); }

} // namespace ge
