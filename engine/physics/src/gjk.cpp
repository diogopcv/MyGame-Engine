#include "physics/gjk.h"
#include <cmath>
#include <limits>

namespace ge::physics {

vec3 support_sphere(const void* shape, vec3 dir) {
    const Sphere& s = *static_cast<const Sphere*>(shape);
    f32 len2 = length_sq(dir);
    if (len2 < 1e-12f) return s.center;
    return s.center + (dir / std::sqrt(len2)) * s.radius;
}

vec3 support_box(const void* shape, vec3 dir) {
    const Box& b = *static_cast<const Box*>(shape);
    mat4 r = quat_to_matrix(b.orientation);
    vec3 axis_x{r.m[0][0], r.m[0][1], r.m[0][2]};
    vec3 axis_y{r.m[1][0], r.m[1][1], r.m[1][2]};
    vec3 axis_z{r.m[2][0], r.m[2][1], r.m[2][2]};

    vec3 local_dir{dot(dir, axis_x), dot(dir, axis_y), dot(dir, axis_z)};
    vec3 vertex_local{
        local_dir.x >= 0.0f ? b.half_extents.x : -b.half_extents.x,
        local_dir.y >= 0.0f ? b.half_extents.y : -b.half_extents.y,
        local_dir.z >= 0.0f ? b.half_extents.z : -b.half_extents.z,
    };
    return b.center + axis_x * vertex_local.x + axis_y * vertex_local.y + axis_z * vertex_local.z;
}

vec3 support_capsule(const void* shape, vec3 dir) {
    const Capsule& c = *static_cast<const Capsule*>(shape);
    f32 len2 = length_sq(dir);
    if (len2 < 1e-12f) return c.p0;
    // Farthest of the two segment endpoints along dir, then extended by the radius.
    vec3 base = dot(c.p0, dir) > dot(c.p1, dir) ? c.p0 : c.p1;
    return base + (dir / std::sqrt(len2)) * c.radius;
}

namespace {

// Closest-point-to-origin subroutines, adapted from Ericson's "Real-Time
// Collision Detection" (5.1.5/5.1.6) with the query point fixed at the
// origin. Each returns the closest point and the barycentric weight of every
// simplex vertex, so the caller can drop any vertex with zero weight
// (Voronoi-region simplex reduction).

vec3 closest_on_segment_to_origin(vec3 a, vec3 b, f32& ua, f32& ub) {
    vec3 ab = b - a;
    f32 t = dot(-a, ab);
    if (t <= 0.0f) { ua = 1.0f; ub = 0.0f; return a; }
    f32 denom = dot(ab, ab);
    if (t >= denom) { ua = 0.0f; ub = 1.0f; return b; }
    t /= denom;
    ua = 1.0f - t; ub = t;
    return a + ab * t;
}

vec3 closest_on_triangle_to_origin(vec3 a, vec3 b, vec3 c, f32& ua, f32& ub, f32& uc) {
    vec3 ab = b - a, ac = c - a, ap = -a;
    f32 d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) { ua = 1; ub = 0; uc = 0; return a; }

    vec3 bp = -b;
    f32 d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) { ua = 0; ub = 1; uc = 0; return b; }

    f32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        f32 t = d1 / (d1 - d3);
        ua = 1.0f - t; ub = t; uc = 0.0f;
        return a + ab * t;
    }

    vec3 cp = -c;
    f32 d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) { ua = 0; ub = 0; uc = 1; return c; }

    f32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        f32 t = d2 / (d2 - d6);
        ua = 1.0f - t; ub = 0.0f; uc = t;
        return a + ac * t;
    }

    f32 va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        f32 t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        ua = 0.0f; ub = 1.0f - t; uc = t;
        return b + (c - b) * t;
    }

    f32 denom = 1.0f / (va + vb + vc);
    ub = vb * denom; uc = vc * denom; ua = 1.0f - ub - uc;
    return a + ab * ub + ac * uc;
}

/** @brief True if the origin and @p d fall on opposite sides of plane (a,b,c). */
bool origin_outside_plane(vec3 a, vec3 b, vec3 c, vec3 d) {
    vec3 n = cross(b - a, c - a);
    f32 sign_o = dot(-a, n);
    f32 sign_d = dot(d - a, n);
    return sign_o * sign_d < 0.0f;
}

/** @brief Up to 4 points of the Minkowski-difference simplex GJK is evolving. */
struct Simplex {
    vec3 p[4]{};
    int  count = 0;
};

vec3 reduce_segment(Simplex& s) {
    f32 ua, ub;
    vec3 closest = closest_on_segment_to_origin(s.p[0], s.p[1], ua, ub);
    vec3 pts[2] = {s.p[0], s.p[1]};
    int n = 0;
    if (ua > 0.0f) s.p[n++] = pts[0];
    if (ub > 0.0f) s.p[n++] = pts[1];
    s.count = n;
    return closest;
}

vec3 reduce_triangle(Simplex& s) {
    f32 ua, ub, uc;
    vec3 closest = closest_on_triangle_to_origin(s.p[0], s.p[1], s.p[2], ua, ub, uc);
    vec3 pts[3] = {s.p[0], s.p[1], s.p[2]};
    int n = 0;
    if (ua > 0.0f) s.p[n++] = pts[0];
    if (ub > 0.0f) s.p[n++] = pts[1];
    if (uc > 0.0f) s.p[n++] = pts[2];
    s.count = n;
    return closest;
}

/** @brief Reduces a 4-point simplex; returns true if the origin is enclosed (shapes overlap). */
bool reduce_tetrahedron(Simplex& s, vec3& closest) {
    vec3 a = s.p[0], b = s.p[1], c = s.p[2], d = s.p[3];

    bool out_abc = origin_outside_plane(a, b, c, d);
    bool out_acd = origin_outside_plane(a, c, d, b);
    bool out_adb = origin_outside_plane(a, d, b, c);
    bool out_bdc = origin_outside_plane(b, d, c, a);

    if (!out_abc && !out_acd && !out_adb && !out_bdc) {
        closest = {0, 0, 0};
        return true;
    }

    f32 best_dist2 = std::numeric_limits<f32>::max();
    Simplex best;
    auto consider = [&](vec3 p0, vec3 p1, vec3 p2) {
        Simplex tri; tri.p[0] = p0; tri.p[1] = p1; tri.p[2] = p2; tri.count = 3;
        vec3 q = reduce_triangle(tri);
        f32 d2 = length_sq(q);
        if (d2 < best_dist2) { best_dist2 = d2; best = tri; closest = q; }
    };
    if (out_abc) consider(a, b, c);
    if (out_acd) consider(a, c, d);
    if (out_adb) consider(a, d, b);
    if (out_bdc) consider(b, d, c);

    s = best;
    return false;
}

} // namespace

GjkResult gjk_distance(const void* shape_a, SupportFn support_a,
                        const void* shape_b, SupportFn support_b) {
    auto minkowski_support = [&](vec3 dir) -> vec3 {
        return support_a(shape_a, dir) - support_b(shape_b, -dir);
    };

    constexpr int kMaxIters = 32;
    constexpr f32 kTolerance = 1e-4f;
    constexpr f32 kOriginEpsSq = 1e-10f;

    Simplex simplex;
    simplex.p[0] = minkowski_support({1, 0, 0});
    simplex.count = 1;

    vec3 closest = simplex.p[0];
    if (length_sq(closest) < kOriginEpsSq) return {true, 0.0f};

    for (int iter = 0; iter < kMaxIters; ++iter) {
        f32 dir_len = std::sqrt(length_sq(closest));
        if (dir_len < 1e-8f) return {true, 0.0f};

        vec3 dir_n = -closest / dir_len; // search toward the origin

        vec3 support = minkowski_support(dir_n);
        f32 support_proj = dot(support, dir_n);
        f32 closest_proj = -dir_len; // dot(closest, dir_n) simplifies to -|closest|

        if (support_proj - closest_proj < kTolerance) {
            // The support function can't find a point past the current
            // simplex along the search direction: `closest` is (within
            // tolerance) the true closest point, so `dir_len` is the distance.
            return {false, dir_len};
        }

        simplex.p[simplex.count++] = support;

        bool enclosed = false;
        switch (simplex.count) {
            case 2: closest = reduce_segment(simplex); break;
            case 3: closest = reduce_triangle(simplex); break;
            case 4: enclosed = reduce_tetrahedron(simplex, closest); break;
            default: break;
        }
        if (enclosed) return {true, 0.0f};
        if (length_sq(closest) < kOriginEpsSq) return {true, 0.0f};
    }

    // Iteration budget exhausted without a clean stopping condition (should
    // only happen for pathological/degenerate input): report the best
    // distance found so far rather than looping forever.
    return {false, std::sqrt(length_sq(closest))};
}

} // namespace ge::physics
