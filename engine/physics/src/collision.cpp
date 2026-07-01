#include "physics/collision.h"
#include "closest_point_internal.h"
#include "sat_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ge::physics {

namespace {

/**
 * @brief Shared "point with a radius vs. box" resolution, used by both
 *        sphere_box() (point = sphere center) and capsule_box() (point =
 *        closest point on the capsule's core segment to the box).
 */
Contact point_radius_vs_box(vec3 point, f32 radius, const Box& box) {
    detail::BoxAxes ax = detail::box_world_axes(box);
    vec3 d = point - box.center;
    vec3 local{dot(d, ax.x), dot(d, ax.y), dot(d, ax.z)};

    vec3 clamped{
        std::clamp(local.x, -box.half_extents.x, box.half_extents.x),
        std::clamp(local.y, -box.half_extents.y, box.half_extents.y),
        std::clamp(local.z, -box.half_extents.z, box.half_extents.z),
    };
    vec3 closest_world = box.center + ax.x * clamped.x + ax.y * clamped.y + ax.z * clamped.z;

    vec3 diff = point - closest_world;
    f32 dist2 = length_sq(diff);
    if (dist2 > radius * radius) return {};

    Contact c;
    c.intersecting = true;
    f32 dist = std::sqrt(dist2);
    if (dist > 1e-6f) {
        c.normal = -(diff / dist); // point-shape -> box surface
        c.penetration = radius - dist;
    } else {
        // Point is inside (or exactly at the surface of) the box: the
        // closest-point projection is degenerate, so fall back to the axis
        // of least penetration and push out through that face.
        f32 pen_x = box.half_extents.x - std::abs(local.x);
        f32 pen_y = box.half_extents.y - std::abs(local.y);
        f32 pen_z = box.half_extents.z - std::abs(local.z);
        vec3 axis;
        f32 pen;
        if (pen_x <= pen_y && pen_x <= pen_z) {
            axis = ax.x; pen = pen_x; if (local.x < 0.0f) axis = -axis;
        } else if (pen_y <= pen_z) {
            axis = ax.y; pen = pen_y; if (local.y < 0.0f) axis = -axis;
        } else {
            axis = ax.z; pen = pen_z; if (local.z < 0.0f) axis = -axis;
        }
        c.normal = -axis; // point-shape -> box: opposite of the box's outward face normal
        c.penetration = pen + radius;
    }
    return c;
}

} // namespace

bool aabb_overlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

Contact sphere_sphere(const Sphere& a, const Sphere& b) {
    vec3 d = b.center - a.center;
    f32 dist2 = length_sq(d);
    f32 r = a.radius + b.radius;
    if (dist2 > r * r) return {};

    f32 dist = std::sqrt(dist2);
    Contact c;
    c.intersecting = true;
    c.normal = dist > 1e-6f ? d / dist : vec3{0, 1, 0};
    c.penetration = r - dist;
    return c;
}

Contact sphere_box(const Sphere& s, const Box& b) {
    return point_radius_vs_box(s.center, s.radius, b);
}

Contact capsule_box(const Capsule& c, const Box& b) {
    vec3 on_seg, on_box;
    detail::closest_points_segment_box(c.p0, c.p1, b, on_seg, on_box);
    return point_radius_vs_box(on_seg, c.radius, b);
}

Contact box_box(const Box& a, const Box& b) {
    detail::SatResult r = detail::box_box_sat(a, b);
    if (!r.intersecting) return {};
    Contact c;
    c.intersecting = true;
    c.normal = r.normal;
    c.penetration = r.penetration;
    return c;
}

} // namespace ge::physics

namespace ge::physics::detail {

SatResult box_box_sat(const Box& a, const Box& b) {
    BoxAxes wa = box_world_axes(a);
    BoxAxes wb = box_world_axes(b);
    vec3 ax[3] = {wa.x, wa.y, wa.z};
    vec3 bx[3] = {wb.x, wb.y, wb.z};
    f32 ea[3] = {a.half_extents.x, a.half_extents.y, a.half_extents.z};
    f32 eb[3] = {b.half_extents.x, b.half_extents.y, b.half_extents.z};

    vec3 t = b.center - a.center;

    f32 best_overlap = std::numeric_limits<f32>::max();
    vec3 best_axis{0, 1, 0};
    SatAxisType best_type = SatAxisType::FaceA;
    int best_ia = 0, best_ib = 0;

    // Projects both boxes onto `axis` and shrinks the running best (smallest
    // positive overlap) separating axis. Returns false the moment a genuine
    // separating axis is found (SAT early-out).
    auto test_axis = [&](vec3 axis, SatAxisType type, int ia, int ib) -> bool {
        f32 len2 = length_sq(axis);
        if (len2 < 1e-10f) return true; // near-parallel edges: degenerate axis, skip

        axis = axis / std::sqrt(len2);
        f32 ra_proj = ea[0] * std::abs(dot(axis, ax[0])) + ea[1] * std::abs(dot(axis, ax[1])) +
                      ea[2] * std::abs(dot(axis, ax[2]));
        f32 rb_proj = eb[0] * std::abs(dot(axis, bx[0])) + eb[1] * std::abs(dot(axis, bx[1])) +
                      eb[2] * std::abs(dot(axis, bx[2]));
        f32 dist = dot(t, axis);
        f32 overlap = ra_proj + rb_proj - std::abs(dist);
        if (overlap < 0.0f) return false;

        if (overlap < best_overlap) {
            best_overlap = overlap;
            best_axis = dist < 0.0f ? -axis : axis; // orient a -> b
            best_type = type;
            best_ia = ia;
            best_ib = ib;
        }
        return true;
    };

    SatResult result;
    for (int i = 0; i < 3; ++i)
        if (!test_axis(ax[i], SatAxisType::FaceA, i, 0)) return result;
    for (int i = 0; i < 3; ++i)
        if (!test_axis(bx[i], SatAxisType::FaceB, 0, i)) return result;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            if (!test_axis(cross(ax[i], bx[j]), SatAxisType::Edge, i, j)) return result;

    result.intersecting = true;
    result.axis_type = best_type;
    result.index_a = best_ia;
    result.index_b = best_ib;
    result.normal = best_axis;
    result.penetration = best_overlap;
    return result;
}

} // namespace ge::physics::detail
