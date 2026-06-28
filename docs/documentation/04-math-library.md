# Math Library

The math library provides the fundamental types and operations for 3D graphics and game physics: vectors, matrices, quaternions, and transform utilities. Every piece of geometry, every camera, every light, and every object placement in the engine goes through these types.

## Table of Contents

- [Math Conventions](#math-conventions)
- [Vector Types (`vec.h`)](#vector-types-vech)
  - [vec2, vec3, vec4](#vec2-vec3-vec4)
  - [Common Vector Operations](#common-vector-operations)
  - [The Dot Product](#the-dot-product)
  - [The Cross Product](#the-cross-product)
  - [Normalization](#normalization)
- [Matrix Type (`mat4.h`)](#matrix-type-mat4h)
  - [Row-Major vs Column-Major](#row-major-vs-column-major)
  - [Row Vectors vs Column Vectors](#row-vectors-vs-column-vectors)
  - [Matrix Multiplication](#matrix-multiplication)
  - [Matrix Inverse (Cramer's Rule)](#matrix-inverse-cramers-rule)
  - [Transform Matrices](#transform-matrices)
  - [Projection Matrices](#projection-matrices)
  - [The View Matrix (Look-At)](#the-view-matrix-look-at)
- [Quaternions (`quat.h`)](#quaternions-quath)
  - [What is a Quaternion?](#what-is-a-quaternion)
  - [Why Quaternions Over Euler Angles?](#why-quaternions-over-euler-angles)
  - [Creating Rotations](#creating-rotations)
  - [Composing Rotations (Hamilton Product)](#composing-rotations-hamilton-product)
  - [SLERP (Spherical Linear Interpolation)](#slerp-spherical-linear-interpolation)
  - [Quaternion to Matrix Conversion](#quaternion-to-matrix-conversion)
  - [Matrix to Quaternion (Shepperd's Method)](#matrix-to-quaternion-shepperds-method)
- [Transform Composition (`transform.h`)](#transform-composition-transformh)
  - [TRS Composition](#trs-composition)
  - [TRS Decomposition](#trs-decomposition)
- [Mathematical Constants (`math.h`)](#mathematical-constants-mathh)

---

## Math Conventions

Before diving into the types, it is essential to understand the conventions the engine uses. These were chosen to align with DirectX 12 / HLSL defaults, minimizing conversions at the GPU boundary.

| Choice | Convention | Rationale |
|--------|-----------|-----------|
| Coordinate system | **Left-handed** | Matches Direct3D. +X right, +Y up, +Z forward (into the screen). |
| Matrix storage | **Row-major** | `m[row][col]`. Matches how HLSL expects data with `row_major` qualifier. |
| Vector convention | **Row vectors** | Transforms: `p' = p * M`. Composites read left-to-right: `p * S * R * T`. |
| Depth range | **[0, 1]** | Direct3D clip-space convention. Near plane maps to 0, far to 1. |
| Angles | **Radians** | Standard in mathematics and trigonometric functions. |
| Rotation representation | **Quaternions** | Canonical rotation storage; matrices derived for GPU upload. |

**Why left-handed?** In a left-handed system with row-major matrices, the GPU receives data that it can use directly without transposition. This eliminates an entire class of bugs (forgetting to transpose) and a per-frame CPU cost (transposing every matrix).

---

## Vector Types (`vec.h`)

### vec2, vec3, vec4

Vectors are plain-old-data (POD) structs with named components:

```cpp
struct vec2 { f32 x = 0, y = 0; };
struct vec3 { f32 x = 0, y = 0, z = 0; };
struct vec4 { f32 x = 0, y = 0, z = 0, w = 0; };
```

They are default-initialized to zero, trivially copyable, and have no virtual functions or inheritance. This makes them cache-friendly and safe to `memcpy`, upload to the GPU, or store in contiguous arrays.

All operations are free functions and inline operators, not member functions. This keeps the types simple and makes operations composable:

```cpp
vec3 a = {1, 2, 3};
vec3 b = {4, 5, 6};
vec3 c = a + b;           // Component-wise addition: {5, 7, 9}
vec3 d = a * 2.0f;        // Scalar multiplication: {2, 4, 6}
f32  dp = dot(a, b);      // Dot product: 1*4 + 2*5 + 3*6 = 32
vec3 cp = cross(a, b);    // Cross product
vec3 n  = normalize(a);   // Unit-length direction
```

### Common Vector Operations

**Component-wise arithmetic** (`+`, `-`, `*`, `/`): Each component is operated on independently. `{1,2,3} + {4,5,6} = {5,7,9}`.

**Scalar multiplication** (`v * s` or `s * v`): Scales all components by a scalar. `{1,2,3} * 2 = {2,4,6}`.

**Compound assignment** (`+=`, `-=`, `*=`): Modify a vector in place.

### The Dot Product

The dot product (also called the "inner product" or "scalar product") is one of the most fundamental operations in 3D graphics:

```
dot(a, b) = a.x*b.x + a.y*b.y + a.z*b.z
```

Geometrically, `dot(a, b) = |a| * |b| * cos(theta)`, where `theta` is the angle between the vectors. This gives it several critical uses:

1. **Angle between vectors:** If both vectors are unit length, `dot(a, b) = cos(theta)`. So `dot(a, b) > 0` means they face roughly the same direction, `= 0` means perpendicular, `< 0` means opposite.

2. **Projection:** `dot(a, n)` projects vector `a` onto unit vector `n`, giving the signed length of `a`'s component along `n`.

3. **Lighting:** `N dot L` (normal dot light-direction) is the foundation of diffuse lighting. A surface facing the light (`N dot L = 1`) is fully lit; a surface perpendicular to the light (`N dot L = 0`) receives no light.

4. **Length:** `length(v) = sqrt(dot(v, v))`. The dot product of a vector with itself is the squared length, so `length_sq(v) = dot(v, v)` avoids an expensive square root when only comparison is needed.

### The Cross Product

The cross product is defined only for 3D vectors and returns a vector perpendicular to both inputs:

```
cross(a, b) = {
    a.y*b.z - a.z*b.y,
    a.z*b.x - a.x*b.z,
    a.x*b.y - a.y*b.x
}
```

In a left-handed coordinate system, `cross(a, b)` follows the left-hand rule: if you point your left index finger along `a` and curl your fingers toward `b`, your thumb points in the direction of `cross(a, b)`.

Uses in the engine:
- **Computing surface normals:** The cross product of two edges of a triangle gives the triangle's normal.
- **Building coordinate frames:** The view matrix uses cross products to construct the camera's right and up vectors from the forward direction.
- **Area computation:** `|cross(a, b)|` gives the area of the parallelogram spanned by `a` and `b`.

### Normalization

Normalization divides a vector by its length, producing a unit vector (length = 1) pointing in the same direction:

```cpp
inline vec3 normalize(vec3 a) { return a / length(a); }
```

Unit vectors are essential because many operations (dot-product angle tests, lighting calculations, rotation axes) require or produce more meaningful results with unit-length inputs.

**Caution:** Normalizing a zero-length vector produces NaN (division by zero). The engine does not guard against this — it is the caller's responsibility to ensure vectors are non-zero before normalizing.

---

## Matrix Type (`mat4.h`)

### Row-Major vs Column-Major

A 4x4 matrix has 16 elements. The question is how to store them in memory:

```
Mathematically:       Row-major memory layout:         Column-major memory layout:
| m00 m01 m02 m03 |  m[0][0] m[0][1] m[0][2] m[0][3]  m[0][0] m[1][0] m[2][0] m[3][0]
| m10 m11 m12 m13 |  m[1][0] m[1][1] m[1][2] m[1][3]  m[0][1] m[1][1] m[2][1] m[3][1]
| m20 m21 m22 m23 |  m[2][0] m[2][1] m[2][2] m[2][3]  m[0][2] m[1][2] m[2][2] m[3][2]
| m30 m31 m32 m33 |  m[3][0] m[3][1] m[3][2] m[3][3]  m[0][3] m[1][3] m[2][3] m[3][3]
```

MyEngine uses **row-major** storage: `m[i][j]` accesses row `i`, column `j`. Each row is contiguous in memory. This is the natural C/C++ array layout.

### Row Vectors vs Column Vectors

This is the most important convention in the math library, and it affects everything:

**Row-vector convention (what MyEngine uses):**
```
p' = p * M        (point is a row vector, multiplied on the left)
```

**Column-vector convention (what OpenGL uses):**
```
p' = M * p        (point is a column vector, multiplied on the right)
```

With row vectors, composite transforms read left-to-right:
```
p' = p * Scale * Rotate * Translate
```

This means: first scale, then rotate, then translate — reading left to right, which matches the order of application. Many people find this more intuitive than the column-vector convention, where the order reads right to left.

The transform of a row vector by a matrix:

```cpp
inline vec4 operator*(vec4 v, const mat4& M) {
    return {
        v.x*M.m[0][0] + v.y*M.m[1][0] + v.z*M.m[2][0] + v.w*M.m[3][0],
        v.x*M.m[0][1] + v.y*M.m[1][1] + v.z*M.m[2][1] + v.w*M.m[3][1],
        v.x*M.m[0][2] + v.y*M.m[1][2] + v.z*M.m[2][2] + v.w*M.m[3][2],
        v.x*M.m[0][3] + v.y*M.m[1][3] + v.z*M.m[2][3] + v.w*M.m[3][3],
    };
}
```

Each component of the result is the dot product of the row vector with a **column** of the matrix.

### Matrix Multiplication

Matrix multiplication combines two transforms into one. The result `C = A * B` means "apply A first, then B" (in the row-vector convention):

```cpp
inline mat4 operator*(const mat4& a, const mat4& b) {
    mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}
```

Note the loop order: `i, k, j` (not `i, j, k`). This is an optimization: the inner loop sweeps `j` (column of `b`), which is contiguous in row-major storage. The alternative order `i, j, k` would stride across rows of `b`, causing cache misses.

Matrix multiplication is:
- **Not commutative:** `A * B != B * A` in general.
- **Associative:** `(A * B) * C = A * (B * C)`.
- **The foundation of the transform pipeline:** `MVP = Model * View * Projection`.

### Matrix Inverse (Cramer's Rule)

The inverse of a matrix `M` is a matrix `M^-1` such that `M * M^-1 = I` (identity). The engine uses the cofactor expansion method (Cramer's rule):

1. **Compute the cofactor matrix:** For each element, compute the determinant of the 3x3 submatrix obtained by deleting that element's row and column. Apply a checkerboard sign pattern.
2. **Transpose to get the adjugate:** The adjugate (or adjoint) is the transpose of the cofactor matrix.
3. **Divide by the determinant:** `M^-1 = adj(M) / det(M)`.

This is a closed-form formula (no iteration, no pivoting), which makes it efficient for 4x4 matrices. For larger matrices, LU decomposition would be preferred, but 4x4 is the maximum size needed for 3D graphics.

The determinant is computed as a byproduct of the first row of cofactors:
```
det = m[0][0]*C[0][0] + m[0][1]*C[0][1] + m[0][2]*C[0][2] + m[0][3]*C[0][3]
```

If `det = 0`, the matrix is singular (not invertible). The engine does not guard against this — it is the caller's responsibility to ensure the matrix is invertible.

### Transform Matrices

**Translation** (row-vector convention):
```cpp
mat4 mat4_translation(vec3 t) {
    // Translation goes in the last row (row 3):
    // | 1  0  0  0 |
    // | 0  1  0  0 |
    // | 0  0  1  0 |
    // | tx ty tz 1 |
    mat4 r = mat4_identity();
    r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
    return r;
}
```

In the row-vector convention, the translation sits in the **last row** (row 3), not the last column as in the column-vector convention. This is one of the most common sources of confusion when switching between conventions.

**Scale:**
```cpp
mat4 mat4_scale(vec3 s) {
    // | sx 0  0  0 |
    // | 0  sy 0  0 |
    // | 0  0  sz 0 |
    // | 0  0  0  1 |
}
```

**Rotation around an axis** (e.g., Y axis):
```cpp
mat4 mat4_rotation_y(f32 angle) {
    // Left-handed rotation around Y:
    // | cos  0  -sin  0 |
    // |  0   1   0    0 |
    // | sin  0   cos  0 |
    // |  0   0   0    1 |
}
```

The sign of `sin` determines the handedness. In a left-handed system with row vectors, positive rotation around Y goes clockwise when looking along +Y.

### Projection Matrices

**Perspective projection** transforms a 3D frustum (truncated pyramid) into a unit cube in clip space. This is what makes objects farther away appear smaller.

```cpp
mat4 mat4_perspective_lh(f32 fov_y, f32 aspect, f32 near_z, f32 far_z) {
    f32 cot_y = 1.0f / std::tan(fov_y * 0.5f);
    mat4 r{};
    r.m[0][0] = cot_y / aspect;
    r.m[1][1] = cot_y;
    r.m[2][2] = far_z / (far_z - near_z);
    r.m[2][3] = 1.0f;                                // w_clip = z_view
    r.m[3][2] = -(near_z * far_z) / (far_z - near_z);
    return r;
}
```

Key aspects:
- `cot_y = 1/tan(fov_y/2)` scales the Y coordinate to match the field of view.
- `cot_y / aspect` scales X to account for the aspect ratio.
- `m[2][2]` and `m[3][2]` map the Z range `[near_z, far_z]` to `[0, 1]` (D3D convention).
- `m[2][3] = 1` copies `z_view` into `w_clip` for the perspective divide.

After the perspective divide (`x/w, y/w, z/w`), the result is in Normalized Device Coordinates (NDC): X and Y in `[-1, 1]`, Z in `[0, 1]`.

**Orthographic projection** maps a box directly to NDC without perspective distortion:

```cpp
mat4 mat4_ortho_lh(f32 left, f32 right, f32 bottom, f32 top, f32 near_z, f32 far_z);
```

This is used for shadow mapping, where the light "sees" the scene without perspective.

### The View Matrix (Look-At)

The view matrix transforms world-space positions into camera space. It is constructed from the camera's position, target point, and up vector:

```cpp
mat4 mat4_look_at_lh(vec3 eye, vec3 target, vec3 up) {
    vec3 f = normalize(target - eye);   // forward (+Z in left-handed)
    vec3 r = normalize(cross(up, f));   // right (+X)
    vec3 u = cross(f, r);              // up, re-orthogonalized (+Y)

    mat4 v{};
    // Basis vectors become the columns of the rotation part
    v.m[0][0] = r.x; v.m[0][1] = u.x; v.m[0][2] = f.x;
    v.m[1][0] = r.y; v.m[1][1] = u.y; v.m[1][2] = f.y;
    v.m[2][0] = r.z; v.m[2][1] = u.z; v.m[2][2] = f.z;
    // Translation part: negate dot products to "move the world" opposite to the camera
    v.m[3][0] = -dot(r, eye);
    v.m[3][1] = -dot(u, eye);
    v.m[3][2] = -dot(f, eye);
    v.m[3][3] = 1.0f;
    return v;
}
```

The view matrix is the inverse of the camera's world transform. Instead of computing a full matrix inverse, the look-at function exploits the fact that the rotation part of a transform matrix is orthogonal — its inverse is simply its transpose. The translation part is negated by dot-producting with the basis vectors.

---

## Quaternions (`quat.h`)

### What is a Quaternion?

A quaternion is a four-dimensional number of the form `q = w + xi + yj + zk`, where `i, j, k` are imaginary units with the property `i^2 = j^2 = k^2 = ijk = -1`. In the engine, quaternions are stored as `{x, y, z, w}` where `w` is the scalar (real) part:

```cpp
struct quat { f32 x = 0, y = 0, z = 0, w = 1; };
```

The default value `{0, 0, 0, 1}` is the identity quaternion (no rotation).

A unit quaternion (length = 1) represents a 3D rotation. The rotation is around the axis `(x, y, z)` by an angle `2 * acos(w)`. Every possible 3D rotation can be represented by a unit quaternion.

### Why Quaternions Over Euler Angles?

**Euler angles** (pitch, yaw, roll) are intuitive but have a fatal flaw: **gimbal lock**. When two rotation axes align (e.g., pitch reaches 90 degrees), one degree of freedom is lost, and rotations in certain directions become impossible. This manifests as snapping or skipping during animation.

**Rotation matrices** (3x3) work correctly but:
- Use 9 numbers to represent 3 degrees of freedom (wasteful).
- Accumulate floating-point error under repeated multiplication (the matrix gradually deforms, becoming non-orthogonal).
- Cannot be easily interpolated (what does "halfway between two rotations" mean for a matrix?).

**Quaternions** solve all of these problems:
- No gimbal lock — quaternions cover the entire rotation space uniformly.
- Compact: 4 floats instead of 9.
- Easy to normalize (just divide by length), preventing drift.
- Smooth interpolation via SLERP.
- Efficient composition (quaternion multiplication is cheaper than matrix multiplication).

### Creating Rotations

From an axis and angle:

```cpp
quat quat_from_axis_angle(vec3 axis, f32 angle) {
    f32 s = std::sin(angle * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5f)};
}
```

The axis must be unit-length. The half-angle formulation (`angle * 0.5f`) comes from the mathematical definition of quaternion rotations.

### Composing Rotations (Hamilton Product)

Quaternion multiplication composes rotations. `p * q` means "apply rotation p first, then rotation q":

```cpp
quat operator*(quat p, quat q) {
    return {
        p.w*q.x + p.x*q.w + p.y*q.z - p.z*q.y,
        p.w*q.y - p.x*q.z + p.y*q.w + p.z*q.x,
        p.w*q.z + p.x*q.y - p.y*q.x + p.z*q.w,
        p.w*q.w - p.x*q.x - p.y*q.y - p.z*q.z,
    };
}
```

This is the Hamilton product. It is 16 multiplications and 12 additions — much cheaper than a 3x3 matrix multiply (27 multiplications and 18 additions).

### SLERP (Spherical Linear Interpolation)

SLERP smoothly interpolates between two rotations at constant angular velocity. This is essential for animation blending:

```cpp
quat slerp(quat q1, quat q2, f32 t);  // t in [0, 1]
```

The algorithm:
1. Compute `d = dot(q1, q2)` — the cosine of the half-angle between the quaternions.
2. If `d < 0`, negate `q2` to take the shorter arc. (A quaternion and its negation represent the same rotation; we want the shorter interpolation path.)
3. If `d > 0.9995` (quaternions are nearly parallel), fall back to normalized linear interpolation to avoid division-by-zero.
4. Otherwise, use the spherical formula: `result = sin((1-t)*omega) * q1 + sin(t*omega) * q2` where `omega = acos(d)`.

### Quaternion to Matrix Conversion

When a quaternion rotation needs to be uploaded to the GPU (which works with matrices), it must be converted:

```cpp
mat4 quat_to_matrix(quat q) {
    f32 x2 = q.x*q.x, y2 = q.y*q.y, z2 = q.z*q.z;
    f32 xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    f32 wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;

    mat4 r = mat4_identity();
    r.m[0][0] = 1 - 2*(y2+z2);  r.m[0][1] = 2*(xy+wz);      r.m[0][2] = 2*(xz-wy);
    r.m[1][0] = 2*(xy-wz);      r.m[1][1] = 1 - 2*(x2+z2);  r.m[1][2] = 2*(yz+wx);
    r.m[2][0] = 2*(xz+wy);      r.m[2][1] = 2*(yz-wx);       r.m[2][2] = 1 - 2*(x2+y2);
    return r;
}
```

This is a well-known formula derived from the quaternion rotation formula `p' = q * p * q^-1`.

### Matrix to Quaternion (Shepperd's Method)

Converting back from a rotation matrix to a quaternion uses Shepperd's method. The challenge is numerical stability: the straightforward extraction formulas involve square roots and divisions that can produce large errors when certain matrix diagonal elements are close to zero.

Shepperd's method tests which component of the quaternion is largest (by examining the matrix trace and diagonal elements) and extracts that component first (via a square root), then derives the other three components from it (via division). This avoids dividing by a near-zero value.

---

## Transform Composition (`transform.h`)

### TRS Composition

In the engine's row-vector convention, a TRS (Translation-Rotation-Scale) transform is composed as:

```
p' = p * S * R * T
```

This reads left-to-right: first scale, then rotate, then translate. The `trs()` function builds the combined matrix:

```cpp
mat4 trs(vec3 translation, quat rotation, vec3 scale) {
    return mat4_scale(scale) * quat_to_matrix(rotation) * mat4_translation(translation);
}
```

### TRS Decomposition

Given a TRS matrix, `trs_decompose()` recovers the individual components:

1. **Translation** is in the last row (row 3): `{m[3][0], m[3][1], m[3][2]}`.
2. **Scale** is the length of each basis row: `scale.x = |{m[0][0], m[0][1], m[0][2]}|`.
3. **Rotation** is obtained by normalizing each basis row (dividing by scale) to get a pure rotation matrix, then converting to a quaternion.

This decomposition assumes no shear or projection components in the matrix.

---

## Mathematical Constants (`math.h`)

```cpp
namespace ge {
    inline constexpr f32 k_pi         = 3.14159265358979323846f;
    inline constexpr f32 k_pi_over_2  = k_pi / 2.0f;
    inline constexpr f32 k_deg_to_rad = k_pi / 180.0f;
    inline constexpr f32 k_rad_to_deg = 180.0f / k_pi;
}
```

Usage:
```cpp
f32 angle_rad = 45.0f * k_deg_to_rad;  // Convert degrees to radians
f32 angle_deg = angle_rad * k_rad_to_deg;  // Convert back
```

The `math.h` header is an umbrella that includes `vec.h`, `mat4.h`, `quat.h`, and `transform.h`, so including `core/math.h` gives you the entire math library.
