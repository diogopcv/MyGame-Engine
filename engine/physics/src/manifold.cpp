#include "physics/manifold.h"
#include "physics/collision.h"
#include "closest_point_internal.h"
#include "sat_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace ge::physics {

namespace {

/**
 * @brief Sutherland-Hodgman: clip a convex polygon against the half-space
 *        `dot(p, normal) <= d`, appending the result to `out`.
 * @return Number of points written to @p out (bounded by @p max_out).
 */
int clip_polygon(const vec3* poly, int n, vec3 normal, f32 d, vec3* out, int max_out) {
    int count = 0;
    for (int i = 0; i < n; ++i) {
        vec3 curr = poly[i];
        vec3 prev = poly[(i - 1 + n) % n];
        f32 curr_d = dot(curr, normal) - d;
        f32 prev_d = dot(prev, normal) - d;
        bool curr_in = curr_d <= 0.0f;
        bool prev_in = prev_d <= 0.0f;

        if (curr_in != prev_in) {
            f32 denom = prev_d - curr_d;
            f32 t = std::abs(denom) > 1e-8f ? prev_d / denom : 0.0f;
            if (count < max_out) out[count++] = prev + (curr - prev) * t;
        }
        if (curr_in && count < max_out) out[count++] = curr;
    }
    return count;
}

/** @brief Closest points between two segments [p1,q1] and [p2,q2] (Ericson 5.1.9). */
void closest_points_segment_segment(vec3 p1, vec3 q1, vec3 p2, vec3 q2, vec3& c1, vec3& c2) {
    vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    f32 a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    constexpr f32 kEps = 1e-8f;
    f32 s, t;

    if (a <= kEps && e <= kEps) { c1 = p1; c2 = p2; return; }
    if (a <= kEps) {
        s = 0.0f;
        t = std::clamp(f / e, 0.0f, 1.0f);
    } else {
        f32 c = dot(d1, r);
        if (e <= kEps) {
            t = 0.0f;
            s = std::clamp(-c / a, 0.0f, 1.0f);
        } else {
            f32 b = dot(d1, d2);
            f32 denom = a * e - b * b;
            s = denom > kEps ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

} // namespace

Manifold manifold_sphere_sphere(const Sphere& a, const Sphere& b) {
    Manifold m;
    Contact c = sphere_sphere(a, b);
    if (!c.intersecting) return m;

    m.valid = true;
    m.normal = c.normal;
    m.count = 1;
    vec3 surface_a = a.center + c.normal * a.radius;
    vec3 surface_b = b.center - c.normal * b.radius;
    m.points[0] = {(surface_a + surface_b) * 0.5f, c.penetration};
    return m;
}

Manifold manifold_sphere_box(const Sphere& s, const Box& b) {
    Manifold m;
    Contact c = sphere_box(s, b);
    if (!c.intersecting) return m;

    m.valid = true;
    m.normal = c.normal;
    m.count = 1;
    // Midpoint between the sphere's near surface and the box's surface,
    // both lying along `normal` at distances `s.radius` and
    // `s.radius - penetration` from the sphere center, respectively.
    m.points[0] = {s.center + c.normal * (s.radius - c.penetration * 0.5f), c.penetration};
    return m;
}

Manifold manifold_capsule_box(const Capsule& c, const Box& b) {
    Manifold m;
    Contact contact = capsule_box(c, b);
    if (!contact.intersecting) return m;

    vec3 on_seg, on_box;
    detail::closest_points_segment_box(c.p0, c.p1, b, on_seg, on_box);

    m.valid = true;
    m.normal = contact.normal;
    m.count = 1;
    // Same midpoint construction as manifold_sphere_box, with the capsule's
    // closest segment point standing in for a sphere center.
    m.points[0] = {on_seg + contact.normal * (c.radius - contact.penetration * 0.5f),
                    contact.penetration};
    return m;
}

Manifold manifold_box_box(const Box& a, const Box& b) {
    Manifold m;
    detail::SatResult r = detail::box_box_sat(a, b);
    if (!r.intersecting) return m;

    m.valid = true;
    m.normal = r.normal;

    if (r.axis_type == detail::SatAxisType::Edge) {
        detail::BoxAxes wa = detail::box_world_axes(a);
        detail::BoxAxes wb = detail::box_world_axes(b);
        vec3 dA = detail::axis_by_index(wa, r.index_a);
        vec3 dB = detail::axis_by_index(wb, r.index_b);
        vec3 t = b.center - a.center;

        // Witness edge on A: the one of the 4 parallel edges along its
        // index_a axis whose other two coordinates extend toward B.
        vec3 pa = a.center;
        for (int k = 0; k < 3; ++k) {
            if (k == r.index_a) continue;
            vec3 axk = detail::axis_by_index(wa, k);
            f32 sign = dot(t, axk) >= 0.0f ? 1.0f : -1.0f;
            pa = pa + axk * (sign * detail::half_extent_by_index(a, k));
        }
        // Witness edge on B: symmetric, extending toward A (i.e. against t).
        vec3 pb = b.center;
        for (int k = 0; k < 3; ++k) {
            if (k == r.index_b) continue;
            vec3 bxk = detail::axis_by_index(wb, k);
            f32 sign = dot(t, bxk) >= 0.0f ? -1.0f : 1.0f;
            pb = pb + bxk * (sign * detail::half_extent_by_index(b, k));
        }

        f32 ea_axis = detail::half_extent_by_index(a, r.index_a);
        f32 eb_axis = detail::half_extent_by_index(b, r.index_b);
        vec3 c1, c2;
        closest_points_segment_segment(pa - dA * ea_axis, pa + dA * ea_axis,
                                        pb - dB * eb_axis, pb + dB * eb_axis, c1, c2);

        m.count = 1;
        m.points[0] = {(c1 + c2) * 0.5f, r.penetration};
        return m;
    }

    // Face contact: clip the incident box's face against the reference
    // face's 4 side planes (Sutherland-Hodgman), then keep whatever lands
    // behind the reference plane as the manifold points.
    bool ref_is_a = r.axis_type == detail::SatAxisType::FaceA;
    const Box& ref_box = ref_is_a ? a : b;
    const Box& inc_box = ref_is_a ? b : a;
    int ref_axis_index = ref_is_a ? r.index_a : r.index_b;

    detail::BoxAxes ref_axes = detail::box_world_axes(ref_box);
    detail::BoxAxes inc_axes = detail::box_world_axes(inc_box);

    // r.normal always points a -> b. The reference face's own outward
    // normal (pointing away from ref_box, into inc_box) is +r.normal when A
    // is the reference (a -> b already points outward from A) and -r.normal
    // when B is the reference (a -> b points *into* B from the outside).
    vec3 ref_normal = ref_is_a ? r.normal : -r.normal;
    f32  ref_e = detail::half_extent_by_index(ref_box, ref_axis_index);
    vec3 ref_face_center = ref_box.center + ref_normal * ref_e;

    int ref_t1 = (ref_axis_index + 1) % 3;
    int ref_t2 = (ref_axis_index + 2) % 3;
    vec3 t1 = detail::axis_by_index(ref_axes, ref_t1);
    vec3 t2 = detail::axis_by_index(ref_axes, ref_t2);
    f32 e_t1 = detail::half_extent_by_index(ref_box, ref_t1);
    f32 e_t2 = detail::half_extent_by_index(ref_box, ref_t2);

    // Incident face: whichever of the incident box's 6 face normals is most
    // anti-parallel to ref_normal (faces most directly into the reference face).
    vec3 inc_candidates[6] = {
        detail::axis_by_index(inc_axes, 0), -detail::axis_by_index(inc_axes, 0),
        detail::axis_by_index(inc_axes, 1), -detail::axis_by_index(inc_axes, 1),
        detail::axis_by_index(inc_axes, 2), -detail::axis_by_index(inc_axes, 2),
    };
    int best_k = 0;
    f32 best_dot = std::numeric_limits<f32>::max();
    for (int k = 0; k < 6; ++k) {
        f32 d = dot(inc_candidates[k], ref_normal);
        if (d < best_dot) { best_dot = d; best_k = k; }
    }
    vec3 inc_normal = inc_candidates[best_k];
    int inc_axis_index = best_k / 2;
    int inc_t1 = (inc_axis_index + 1) % 3;
    int inc_t2 = (inc_axis_index + 2) % 3;
    vec3 it1 = detail::axis_by_index(inc_axes, inc_t1);
    vec3 it2 = detail::axis_by_index(inc_axes, inc_t2);
    f32 ie_t1 = detail::half_extent_by_index(inc_box, inc_t1);
    f32 ie_t2 = detail::half_extent_by_index(inc_box, inc_t2);
    vec3 inc_face_center =
        inc_box.center + inc_normal * detail::half_extent_by_index(inc_box, inc_axis_index);

    vec3 quad[4] = {
        inc_face_center + it1 * ie_t1 + it2 * ie_t2,
        inc_face_center + it1 * ie_t1 - it2 * ie_t2,
        inc_face_center - it1 * ie_t1 - it2 * ie_t2,
        inc_face_center - it1 * ie_t1 + it2 * ie_t2,
    };

    vec3 buf_a[8], buf_b[8];
    for (int i = 0; i < 4; ++i) buf_a[i] = quad[i];
    int n = 4;
    n = clip_polygon(buf_a, n, t1, dot(ref_face_center, t1) + e_t1, buf_b, 8);
    n = clip_polygon(buf_b, n, -t1, -dot(ref_face_center, t1) + e_t1, buf_a, 8);
    n = clip_polygon(buf_a, n, t2, dot(ref_face_center, t2) + e_t2, buf_b, 8);
    n = clip_polygon(buf_b, n, -t2, -dot(ref_face_center, t2) + e_t2, buf_a, 8);

    struct Candidate { vec3 point; f32 depth; };
    Candidate candidates[8];
    int cand_count = 0;
    for (int i = 0; i < n; ++i) {
        vec3 p = buf_a[i];
        f32 depth = ref_e - dot(p - ref_box.center, ref_normal);
        if (depth > -1e-4f) {
            if (depth < 0.0f) depth = 0.0f;
            candidates[cand_count++] = {p + ref_normal * (depth * 0.5f), depth};
        }
    }

    if (cand_count == 0) {
        // Degenerate: clipping produced nothing behind the reference plane
        // (grazing/near-zero-penetration contact). Fall back to a single
        // point using the SAT summary so the caller still gets a contact.
        m.count = 1;
        m.points[0] = {ref_face_center, r.penetration};
        return m;
    }

    if (cand_count <= static_cast<int>(Manifold::kMaxPoints)) {
        m.count = static_cast<u32>(cand_count);
    } else {
        // More than 4 clipped points (possible under compound rotation):
        // keep the 4 deepest rather than a full area-maximizing reduction —
        // revisit if the box-stack baseline test needs better spread.
        std::sort(candidates, candidates + cand_count,
                  [](const Candidate& x, const Candidate& y) { return x.depth > y.depth; });
        m.count = Manifold::kMaxPoints;
    }
    for (u32 i = 0; i < m.count; ++i)
        m.points[i] = {candidates[i].point, candidates[i].depth};

    return m;
}

} // namespace ge::physics
