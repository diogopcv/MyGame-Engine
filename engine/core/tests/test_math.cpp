#include <doctest/doctest.h>
#include "core/math.h"
#include <cmath>
#include <cstring>

// Helpers --------------------------------------------------------------------
namespace {

constexpr float kEps = 1e-4f;

bool approx(float a, float b, float eps = kEps) {
    return std::abs(a - b) < eps;
}

bool vec3_eq(ge::vec3 a, ge::vec3 b, float eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps) && approx(a.z, b.z, eps);
}

bool vec4_eq(ge::vec4 a, ge::vec4 b, float eps = kEps) {
    return approx(a.x, b.x, eps) && approx(a.y, b.y, eps)
        && approx(a.z, b.z, eps) && approx(a.w, b.w, eps);
}

bool mat4_eq(const ge::mat4& a, const ge::mat4& b, float eps = kEps) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (!approx(a.m[i][j], b.m[i][j], eps)) return false;
    return true;
}

} // namespace

// ============================================================================
// vec2
// ============================================================================
TEST_CASE("vec2 arithmetic") {
    using namespace ge;
    vec2 a = {3, 4};
    vec2 b = {1, 2};

    auto s = a + b;
    REQUIRE(approx(s.x, 4)); REQUIRE(approx(s.y, 6));

    auto d = a - b;
    REQUIRE(approx(d.x, 2)); REQUIRE(approx(d.y, 2));

    auto sc = a * 2.0f;
    REQUIRE(approx(sc.x, 6)); REQUIRE(approx(sc.y, 8));

    auto dv = a / 2.0f;
    REQUIRE(approx(dv.x, 1.5f)); REQUIRE(approx(dv.y, 2.0f));

    auto neg = -a;
    REQUIRE(approx(neg.x, -3)); REQUIRE(approx(neg.y, -4));
}

TEST_CASE("vec2 dot and length") {
    using namespace ge;
    vec2 a = {3, 4};
    REQUIRE(approx(dot(a, a), 25.0f));
    REQUIRE(approx(length_sq(a), 25.0f));
    REQUIRE(approx(length(a), 5.0f));
}

TEST_CASE("vec2 normalize produces unit vector") {
    using namespace ge;
    vec2 v = normalize(vec2{3, 4});
    REQUIRE(approx(length(v), 1.0f));
    REQUIRE(approx(v.x, 0.6f));
    REQUIRE(approx(v.y, 0.8f));
}

// ============================================================================
// vec3
// ============================================================================
TEST_CASE("vec3 arithmetic") {
    using namespace ge;
    vec3 a = {1, 2, 3};
    vec3 b = {4, 5, 6};

    auto s = a + b;
    REQUIRE(approx(s.x, 5)); REQUIRE(approx(s.y, 7)); REQUIRE(approx(s.z, 9));

    REQUIRE(approx(dot(a, b), 1*4 + 2*5 + 3*6));
    REQUIRE(approx(length_sq(vec3{3,4,0}), 25.0f));
    REQUIRE(approx(length(vec3{3,4,0}), 5.0f));
}

TEST_CASE("vec3 cross product") {
    using namespace ge;
    // +X cross +Y = +Z in any right-hand system, but we verify our formula
    vec3 x = {1, 0, 0};
    vec3 y = {0, 1, 0};
    vec3 z = cross(x, y);
    REQUIRE(approx(z.x, 0)); REQUIRE(approx(z.y, 0)); REQUIRE(approx(z.z, 1));

    // cross(y, x) = -z
    auto nz = cross(y, x);
    REQUIRE(approx(nz.z, -1.0f));

    // cross is anti-commutative: a x b = -(b x a)
    vec3 a = {1, 2, 3};
    vec3 b = {4, 5, 6};
    auto ab = cross(a, b);
    auto ba = cross(b, a);
    REQUIRE(approx(ab.x, -ba.x)); REQUIRE(approx(ab.y, -ba.y)); REQUIRE(approx(ab.z, -ba.z));

    // cross product is perpendicular to both inputs
    REQUIRE(approx(dot(ab, a), 0.0f));
    REQUIRE(approx(dot(ab, b), 0.0f));
}

TEST_CASE("vec3 normalize produces unit vector") {
    using namespace ge;
    vec3 v = normalize(vec3{1, 2, 3});
    REQUIRE(approx(length(v), 1.0f));
}

// ============================================================================
// vec4
// ============================================================================
TEST_CASE("vec4 arithmetic") {
    using namespace ge;
    vec4 a = {1, 2, 3, 4};
    vec4 b = {5, 6, 7, 8};
    auto s = a + b;
    REQUIRE(approx(s.x, 6)); REQUIRE(approx(s.w, 12));
    REQUIRE(approx(dot(a, b), 1*5+2*6+3*7+4*8));
    REQUIRE(approx(length(vec4{0,0,3,4}), 5.0f));
}

// ============================================================================
// mat4 — identity and multiply
// ============================================================================
TEST_CASE("mat4 identity") {
    using namespace ge;
    mat4 I = mat4_identity();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            REQUIRE(approx(I.m[i][j], (i == j) ? 1.0f : 0.0f));
}

TEST_CASE("mat4 multiply by identity is no-op") {
    using namespace ge;
    mat4 T = mat4_translation({1, 2, 3});
    mat4 I = mat4_identity();
    REQUIRE(mat4_eq(T * I, T));
    REQUIRE(mat4_eq(I * T, T));
}

TEST_CASE("mat4 multiply is associative") {
    using namespace ge;
    mat4 A = mat4_translation({1,0,0});
    mat4 B = mat4_scale({2,2,2});
    mat4 C = mat4_rotation_y(0.5f);
    REQUIRE(mat4_eq((A*B)*C, A*(B*C)));
}

// ============================================================================
// mat4 — transpose
// ============================================================================
TEST_CASE("mat4 transpose") {
    using namespace ge;
    mat4 a{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            a.m[i][j] = static_cast<f32>(i*4 + j);
    mat4 t = transpose(a);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            REQUIRE(approx(t.m[i][j], a.m[j][i]));
}

TEST_CASE("mat4 double-transpose is identity") {
    using namespace ge;
    mat4 m = mat4_translation({3, 1, 4});
    REQUIRE(mat4_eq(transpose(transpose(m)), m));
}

// ============================================================================
// mat4 — inverse
// ============================================================================
TEST_CASE("mat4 inverse: M * inv(M) = identity") {
    using namespace ge;
    // Translation inverse
    mat4 T  = mat4_translation({3, -1, 5});
    mat4 TI = inverse(T);
    REQUIRE(mat4_eq(T * TI, mat4_identity()));
    REQUIRE(mat4_eq(TI * T, mat4_identity()));
}

TEST_CASE("mat4 inverse: scale matrix") {
    using namespace ge;
    mat4 S  = mat4_scale({2, 4, 0.5f});
    mat4 SI = inverse(S);
    REQUIRE(mat4_eq(S * SI, mat4_identity()));
}

TEST_CASE("mat4 inverse: rotation matrix equals transpose") {
    using namespace ge;
    // For orthogonal matrices, inv = transpose
    mat4 R  = mat4_rotation_z(1.2f);
    mat4 RI = inverse(R);
    REQUIRE(mat4_eq(RI, transpose(R)));
}

// ============================================================================
// mat4 — translation
// ============================================================================
TEST_CASE("mat4_translation moves a point") {
    using namespace ge;
    mat4 T   = mat4_translation({5, -2, 3});
    vec4 p   = {1, 2, 3, 1};
    vec4 out = p * T;
    REQUIRE(approx(out.x, 6.0f));
    REQUIRE(approx(out.y, 0.0f));
    REQUIRE(approx(out.z, 6.0f));
    REQUIRE(approx(out.w, 1.0f));
}

TEST_CASE("mat4_translation does not move a direction vector (w=0)") {
    using namespace ge;
    mat4 T   = mat4_translation({5, -2, 3});
    vec4 dir = {1, 0, 0, 0};
    vec4 out = dir * T;
    REQUIRE(vec4_eq(out, dir));
}

// ============================================================================
// mat4 — scale
// ============================================================================
TEST_CASE("mat4_scale scales a point") {
    using namespace ge;
    mat4 S   = mat4_scale({2, 3, 4});
    vec4 p   = {1, 1, 1, 1};
    vec4 out = p * S;
    REQUIRE(approx(out.x, 2)); REQUIRE(approx(out.y, 3)); REQUIRE(approx(out.z, 4));
}

// ============================================================================
// mat4 — rotation
// ============================================================================
TEST_CASE("mat4_rotation_x rotates Y toward Z") {
    using namespace ge;
    // +90° around X: +Y -> +Z (LH clockwise from +X)
    mat4 Rx  = mat4_rotation_x(k_pi_over_2);
    vec4 y   = {0, 1, 0, 0};
    vec4 out = y * Rx;
    REQUIRE(approx(out.x, 0.0f));
    REQUIRE(approx(out.y, 0.0f));
    REQUIRE(approx(out.z, 1.0f));
}

TEST_CASE("mat4_rotation_y rotates +X toward -Z (LH)") {
    using namespace ge;
    mat4 Ry  = mat4_rotation_y(k_pi_over_2);
    vec4 x   = {1, 0, 0, 0};
    vec4 out = x * Ry;
    REQUIRE(approx(out.x,  0.0f));
    REQUIRE(approx(out.y,  0.0f));
    REQUIRE(approx(out.z, -1.0f));
}

TEST_CASE("mat4_rotation_z rotates +X toward +Y") {
    using namespace ge;
    mat4 Rz  = mat4_rotation_z(k_pi_over_2);
    vec4 x   = {1, 0, 0, 0};
    vec4 out = x * Rz;
    REQUIRE(approx(out.x, 0.0f));
    REQUIRE(approx(out.y, 1.0f));
    REQUIRE(approx(out.z, 0.0f));
}

TEST_CASE("mat4_rotation preserves vector length") {
    using namespace ge;
    mat4 R = mat4_rotation_y(1.23f) * mat4_rotation_x(0.45f);
    vec4 v = {3, -1, 2, 0};
    vec4 rv = v * R;
    f32 len_before = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    f32 len_after  = std::sqrt(rv.x*rv.x + rv.y*rv.y + rv.z*rv.z);
    REQUIRE(approx(len_after, len_before));
}

// ============================================================================
// quat
// ============================================================================
TEST_CASE("quat_identity is unit length") {
    using namespace ge;
    quat q = quat_identity();
    REQUIRE(approx(length(q), 1.0f));
}

TEST_CASE("quat normalize produces unit quaternion") {
    using namespace ge;
    quat q = normalize(quat{1, 2, 3, 4});
    REQUIRE(approx(length(q), 1.0f));
}

TEST_CASE("quat_from_axis_angle + quat_to_matrix matches mat4_rotation_y") {
    using namespace ge;
    float angle = 0.8f;
    mat4 m_direct = mat4_rotation_y(angle);
    mat4 m_via_q  = quat_to_matrix(normalize(quat_from_axis_angle({0,1,0}, angle)));
    REQUIRE(mat4_eq(m_direct, m_via_q));
}

TEST_CASE("quat_from_axis_angle + quat_to_matrix matches mat4_rotation_x") {
    using namespace ge;
    float angle = 1.3f;
    mat4 m_direct = mat4_rotation_x(angle);
    mat4 m_via_q  = quat_to_matrix(normalize(quat_from_axis_angle({1,0,0}, angle)));
    REQUIRE(mat4_eq(m_direct, m_via_q));
}

TEST_CASE("quat multiply: two 90-degree Y rotations equal 180-degree rotation") {
    using namespace ge;
    quat q90  = quat_from_axis_angle({0,1,0}, k_pi_over_2);
    quat q180 = quat_from_axis_angle({0,1,0}, k_pi);
    quat q180c = normalize(q90 * q90);
    // q and -q represent the same rotation; compare absolute dot product
    float d = std::abs(q180.x*q180c.x + q180.y*q180c.y + q180.z*q180c.z + q180.w*q180c.w);
    REQUIRE(approx(d, 1.0f));
}

TEST_CASE("slerp endpoints") {
    using namespace ge;
    quat q1 = normalize(quat_from_axis_angle({1,0,0}, 0.5f));
    quat q2 = normalize(quat_from_axis_angle({0,1,0}, 1.2f));

    quat r0 = slerp(q1, q2, 0.0f);
    REQUIRE(approx(r0.x, q1.x)); REQUIRE(approx(r0.w, q1.w));

    quat r1 = slerp(q1, q2, 1.0f);
    REQUIRE(approx(r1.x, q2.x)); REQUIRE(approx(r1.w, q2.w));
}

TEST_CASE("slerp result is unit length") {
    using namespace ge;
    quat q1 = normalize(quat_from_axis_angle({1,0,0}, 0.3f));
    quat q2 = normalize(quat_from_axis_angle({0,0,1}, 2.0f));
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        quat r = slerp(q1, q2, t);
        REQUIRE(approx(length(r), 1.0f));
    }
}

TEST_CASE("quat_from_matrix round-trips quat_to_matrix") {
    using namespace ge;
    quat q_in  = normalize(quat_from_axis_angle({1, 2, 3}, 1.1f));
    mat4 m     = quat_to_matrix(q_in);
    quat q_out = quat_from_matrix(m);
    // q and -q are the same rotation
    float d = std::abs(q_in.x*q_out.x + q_in.y*q_out.y + q_in.z*q_out.z + q_in.w*q_out.w);
    REQUIRE(approx(d, 1.0f));
}

// ============================================================================
// transform — compose / decompose
// ============================================================================
TEST_CASE("trs compose then decompose round-trips") {
    using namespace ge;
    vec3 t = {3, -1, 7};
    quat r = normalize(quat_from_axis_angle({0, 1, 0}, 1.2f));
    vec3 s = {2, 3, 1.5f};

    mat4 m = trs(t, r, s);
    trs_components c = trs_decompose(m);

    REQUIRE(vec3_eq(c.translation, t));
    REQUIRE(vec3_eq(c.scale, s));
    // Rotation: q and -q represent the same rotation
    float d = std::abs(c.rotation.x*r.x + c.rotation.y*r.y
                     + c.rotation.z*r.z + c.rotation.w*r.w);
    REQUIRE(approx(d, 1.0f));
}

TEST_CASE("trs with uniform scale transforms a point correctly") {
    using namespace ge;
    // Scale by 2, then translate by (10, 0, 0) — no rotation
    mat4 m = trs({10, 0, 0}, quat_identity(), {2, 2, 2});
    vec4 p = {1, 0, 0, 1};
    vec4 r = p * m;
    // p * S = (2,0,0,1), then * T → (12, 0, 0, 1)
    REQUIRE(approx(r.x, 12.0f));
    REQUIRE(approx(r.y,  0.0f));
    REQUIRE(approx(r.z,  0.0f));
}

// ============================================================================
// Perspective / look-at / MVP round-trip
// ============================================================================
TEST_CASE("mat4_perspective_lh maps near to 0 and far to 1 in NDC") {
    using namespace ge;
    // Simple case: fov=90°, aspect=1, near=1, far=2
    mat4 P = mat4_perspective_lh(k_pi_over_2, 1.0f, 1.0f, 2.0f);

    auto ndc_z = [&](float z) {
        vec4 clip = vec4{0, 0, z, 1} * P;
        return clip.z / clip.w;
    };

    REQUIRE(approx(ndc_z(1.0f), 0.0f));
    REQUIRE(approx(ndc_z(2.0f), 1.0f));
    // mid-point should be strictly between 0 and 1
    float mid = ndc_z(1.5f);
    REQUIRE(mid > 0.0f);
    REQUIRE(mid < 1.0f);
}

TEST_CASE("MVP round-trip: camera at origin looking +Z") {
    using namespace ge;
    mat4 model = mat4_identity();
    mat4 view  = mat4_look_at_lh({0,0,0}, {0,0,1}, {0,1,0});
    mat4 proj  = mat4_perspective_lh(k_pi_over_2, 1.0f, 1.0f, 2.0f);
    mat4 mvp   = model * view * proj;

    // Point on near plane → NDC z = 0
    vec4 clip_near = vec4{0, 0, 1, 1} * mvp;
    REQUIRE(approx(clip_near.z / clip_near.w, 0.0f));

    // Point on far plane → NDC z = 1
    vec4 clip_far = vec4{0, 0, 2, 1} * mvp;
    REQUIRE(approx(clip_far.z / clip_far.w, 1.0f));

    // Centred point has NDC x == y == 0
    vec4 clip_center = vec4{0, 0, 1.5f, 1} * mvp;
    REQUIRE(approx(clip_center.x / clip_center.w, 0.0f));
    REQUIRE(approx(clip_center.y / clip_center.w, 0.0f));
}

TEST_CASE("mat4_look_at_lh: camera offset from origin") {
    using namespace ge;
    // Camera at (0, 0, -5) looking at origin: the origin should be 5 units in front
    mat4 V = mat4_look_at_lh({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
    vec4 origin_view = vec4{0, 0, 0, 1} * V;
    REQUIRE(approx(origin_view.z, 5.0f));
    REQUIRE(approx(origin_view.x, 0.0f));
    REQUIRE(approx(origin_view.y, 0.0f));
}
