# Physics Engine (Phase 5)

This document covers `ge_physics`, MyEngine's hand-written rigid-body physics engine: the collision shapes, the narrowphase math that detects overlap (SAT, GJK, closest-point methods), the broadphase that avoids testing every pair against every other pair, contact manifold generation, the sequential-impulse solver, integration, and how it all comes together as `PhysicsWorld::step()` and a physics-driven character controller.

## Table of Contents

- [What Does a Physics Engine Do?](#what-does-a-physics-engine-do)
- [Module Layout](#module-layout)
- [Collision Shapes](#collision-shapes)
- [Narrowphase: Detecting Overlap](#narrowphase-detecting-overlap)
  - [AABB Overlap](#aabb-overlap)
  - [Sphere and Point-vs-Box Tests](#sphere-and-point-vs-box-tests)
  - [Box-Box: The Separating Axis Theorem](#box-box-the-separating-axis-theorem)
  - [Capsule-Box: Alternating Projection](#capsule-box-alternating-projection)
  - [GJK: The General Convex Case](#gjk-the-general-convex-case)
- [Broadphase: Culling the Pairs](#broadphase-culling-the-pairs)
- [Contact Manifolds: From "They Touch" to "Here's Where"](#contact-manifolds-from-they-touch-to-heres-where)
  - [Face Contacts: Sutherland-Hodgman Clipping](#face-contacts-sutherland-hodgman-clipping)
  - [Edge Contacts: Closest Points Between Segments](#edge-contacts-closest-points-between-segments)
- [The Sequential-Impulse Solver](#the-sequential-impulse-solver)
  - [Impulses, Not Forces](#impulses-not-forces)
  - [Effective Mass](#effective-mass)
  - [Baumgarte Stabilization](#baumgarte-stabilization)
  - [Friction](#friction)
  - [Why Iterate?](#why-iterate)
- [Integration: Semi-Implicit Euler](#integration-semi-implicit-euler)
  - [Linear Integration](#linear-integration)
  - [Angular Integration and a Composition-Order Gotcha](#angular-integration-and-a-composition-order-gotcha)
- [RigidBody and PhysicsWorld](#rigidbody-and-physicsworld)
  - [Mass and Inertia](#mass-and-inertia)
  - [PhysicsWorld::step()](#physicsworldstep)
- [The Character Controller: Rotation-Locked Capsules](#the-character-controller-rotation-locked-capsules)
- [Testing Philosophy: Baselines and Cross-Validation](#testing-philosophy-baselines-and-cross-validation)

---

## What Does a Physics Engine Do?

A rigid-body physics engine's job, once per fixed timestep, is to answer: given a set of bodies with positions and velocities, which ones are overlapping, and what velocities do they need afterward so that overlap resolves realistically (bodies push apart, bounce, or come to rest — never interpenetrate)? That single question splits into a pipeline of increasingly specific sub-problems:

```
broadphase (cheap, approximate: which pairs might overlap?)
  -> narrowphase (exact: do these two specific shapes actually overlap? by how much?)
  -> manifold generation (where, specifically, are they touching — one point? four?)
  -> solver (what impulse resolves this contact without penetration?)
  -> integration (apply the resulting velocities to actually move the bodies)
```

Each stage exists because the one before it is either too imprecise or too narrow to answer the next question. Broadphase can't tell you *where* two boxes touch — it only prunes the obviously-impossible pairs cheaply. Narrowphase can tell you *that* two shapes overlap and by how much along one axis, but a box resting flush on a floor needs four contact points (one per corner) to keep it from slowly rocking — that's what manifold generation is for. And knowing *where* contacts are still doesn't tell you *what velocity change* resolves them without an explicit collision-response model — that's the solver.

`ge_physics` (Layer 3, `engine/physics/`) implements every stage from scratch, per the project's constraint against third-party physics libraries (see [00-architecture-overview.md](00-architecture-overview.md)).

---

## Module Layout

`ge_physics` depends on `ge_core` only — no dependency on `ge_world`, `ge_gfx`, or `ge_input`, even though it clearly needs to end up driving a rendered, ECS-owned character. The physics *simulation* itself has no business knowing what an `Entity` is; keeping it decoupled means it can be unit-tested in complete isolation (construct a `PhysicsWorld`, add bodies, call `step()`, inspect the result — no window, no renderer, no scene needed), and it stays reusable if a future project wanted the solver without the ECS. The bridge to `ge_world` is a single opaque handle component (`RigidBodyRef`) plus two sandbox-hosted systems — see [08-ecs-and-world.md](08-ecs-and-world.md) and [07-application-and-game-loop.md](07-application-and-game-loop.md) for that side of the story.

| File | Responsibility |
|------|-----------------|
| `include/physics/shapes.h` | `AABB`, `Sphere`, `Box`, `Capsule` — pure data |
| `include/physics/collision.h` + `src/collision.cpp` | Narrowphase: `aabb_overlap`, `sphere_sphere`, `sphere_box`, `box_box`, `capsule_box` |
| `include/physics/gjk.h` + `src/gjk.cpp` | Support-function GJK distance query, for the general convex case |
| `include/physics/broadphase.h` + `src/broadphase.cpp` | Sweep-and-prune candidate pair generation |
| `include/physics/manifold.h` + `src/manifold.cpp` | Contact point generation (up to 4 points) from narrowphase results |
| `include/physics/solver.h` + `src/solver.cpp` | Sequential-impulse velocity solver |
| `include/physics/rigidbody.h` | `RigidBody` data + mass/inertia factory functions |
| `include/physics/physics_world.h` + `src/physics_world.cpp` | `PhysicsWorld`: owns bodies, runs the full pipeline via `step(dt)` |

Two headers are intentionally *not* under `include/` — `src/sat_internal.h` and `src/closest_point_internal.h` hold private helpers shared between `collision.cpp` and `manifold.cpp` (e.g. both need "which SAT axis won," not just the boolean/normal/depth summary). Keeping them out of the public `include/physics/` directory is the same public-header/hidden-impl split used throughout the engine (see `00-architecture-overview.md`'s pImpl section) — callers outside `ge_physics` never need this detail, so it isn't part of the API surface.

---

## Collision Shapes

Three shapes, deliberately not a general convex-hull system:

```cpp
struct AABB     { vec3 min, max; };                                  // axis-aligned bounding box (broadphase)
struct Sphere   { vec3 center; f32 radius = 0.5f; };
struct Box      { vec3 center; vec3 half_extents; quat orientation; }; // oriented box (OBB)
struct Capsule  { vec3 p0, p1; f32 radius = 0.5f; };                  // swept sphere along a segment
```

A capsule is the **Minkowski sum** of a line segment and a sphere: sweep a sphere of `radius` along the segment `[p0, p1]` and the swept-out volume is the capsule. This framing pays off directly in the narrowphase math below — a capsule-vs-anything test almost always reduces to "closest point on the segment to the other shape, then treat that point like a sphere center."

Sphere, box, and capsule cover everything the current scenes need: the ground and stacked test boxes are `Box`; the player character is a `Capsule` (see [The Character Controller](#the-character-controller-rotation-locked-capsules)). General convex hulls are out of scope — adding shape generality before a scene actually needs it would be exactly the kind of speculative work the project avoids.

---

## Narrowphase: Detecting Overlap

Every narrowphase test shares one return type and one sign convention:

```cpp
struct Contact {
    bool intersecting = false;
    vec3 normal{0, 1, 0};   // points from the FIRST shape argument toward the SECOND
    f32  penetration = 0.0f; // overlap depth along normal; only meaningful if intersecting
};
```

Fixing the normal's direction convention once (`sphere_box(a, b)`'s normal always points `a -> b`, never the reverse) means every downstream consumer — manifold generation, the solver — can rely on it without re-deriving which way is "out" for each shape pair.

### AABB Overlap

The simplest possible test — two axis-aligned boxes overlap unless one is entirely to one side of the other on some axis:

```cpp
bool aabb_overlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}
```

This alone isn't enough to resolve real (possibly rotated) shapes, but it's cheap enough to run on *every* candidate pair — which is exactly the broadphase's job (see below).

### Sphere and Point-vs-Box Tests

`sphere_sphere` is a direct distance comparison: two spheres overlap if the distance between centers is less than the sum of radii, and the overlap depth is exactly `(r_a + r_b) - distance`.

`sphere_box` and `capsule_box` share one helper, `point_radius_vs_box(point, radius, box)`, because both problems are really "closest point on the box to a given point, then treat that point like a sphere":

```cpp
Contact point_radius_vs_box(vec3 point, f32 radius, const Box& box) {
    // Project `point` into the box's local axes, clamp each axis to
    // [-half_extent, +half_extent] -> closest point on/in the box.
    vec3 local = /* point projected onto box.orientation's 3 axes */;
    vec3 clamped = clamp(local, -box.half_extents, box.half_extents);
    vec3 closest_world = /* clamped, transformed back to world space */;

    vec3 diff = point - closest_world;
    if (length(diff) > radius) return {};   // not intersecting
    // ... intersecting: normal = direction from point toward the box surface,
    //     penetration = radius - distance(point, closest_world) ...
}
```

`sphere_box(s, b)` calls this with `point = s.center`. `capsule_box(c, b)` calls it with `point` = the closest point on the capsule's *core segment* to the box (found via the alternating-projection method below) — the exact "capsule = swept sphere" idea from the shapes section, applied directly.

One subtlety both share: if `point` lands exactly on or inside the box, the "closest point" projection degenerates (distance zero, direction undefined). The fallback picks the box axis with the *least* penetration and pushes out through that face — the same logic a human would use ("which wall is closest, push out that way").

### Box-Box: The Separating Axis Theorem

Two convex shapes are **not** overlapping if and only if there exists some axis onto which their projections (shadows) don't overlap. This is the Separating Axis Theorem (SAT), and for two boxes, it is enough to check exactly 15 candidate axes:

- The 3 face-normal axes of box A
- The 3 face-normal axes of box B
- The 9 axes formed by every pair of one edge-direction from A and one from B (`cross(A.axis[i], B.axis[j])` for `i, j` in `{0,1,2}`)

```
axis_x(A) [3]  +  axis_x(B) [3]  +  cross-products (3x3) [9]  =  15 axes
```

If *any* one of the 15 axes shows separation (the two boxes' projected intervals don't overlap on it), the boxes don't intersect — full stop, no need to check the rest. If *none* of the 15 separates them, they overlap, and the axis with the **smallest overlap** ("minimum translation vector," MTV) is the natural contact normal: it's the axis along which the least push is needed to separate them.

```cpp
auto test_axis = [&](vec3 axis) -> bool {
    f32 ra = /* box A's half-extent projected onto axis */;
    f32 rb = /* box B's half-extent projected onto axis */;
    f32 dist = dot(t, axis);              // t = center_B - center_A
    f32 overlap = ra + rb - abs(dist);
    if (overlap < 0.0f) return false;     // separating axis found
    if (overlap < best_overlap) { best_overlap = overlap; best_axis = ...; }
    return true;
};
for (each of the 15 axes)
    if (!test_axis(axis)) return {};      // early-out: definitely not intersecting
```

The 9 edge-cross-product axes exist specifically for the case where two boxes are rotated relative to each other such that neither box's face normals alone reveal the separation — think of two long, thin boxes crossing like an X in 3D space, tilted so their closest approach is along neither box's own face axis. Skipping them would be wrong (not just slower) for that case.

One numerical detail: when two edges are parallel, their cross product is a near-zero vector (a degenerate, non-separating axis). The code checks the axis's length before normalizing and simply skips near-zero axes rather than testing a meaningless direction.

### Capsule-Box: Alternating Projection

A capsule's narrowphase problem reduces to "find the point on the capsule's core segment closest to the box" (then treat it like a sphere, as described above). This is a **closest point between two convex sets** problem — the segment and the box are both convex, and for two convex sets, repeatedly projecting a guess onto one set, then the other, converges to the true closest pair:

```cpp
vec3 t = seg_a;
for (a handful of iterations) {
    vec3 on_box = closest_point_on_box(box, t);        // project current guess onto the box
    t = closest_point_on_segment(seg_a, seg_b, on_box); // then project that back onto the segment
}
```

Each step is a metric projection onto a convex set, so the distance between the two running guesses never increases — it's a straightforward convergence argument, not a heuristic, and for shapes this simple (a line vs. a box), a handful of iterations is enough to converge in practice. This is new code, not adapted from anywhere else in the engine, so it was specifically cross-validated against GJK (see below) rather than trusted on hand-derived test cases alone — a theme covered more in [Testing Philosophy](#testing-philosophy-baselines-and-cross-validation).

### GJK: The General Convex Case

SAT works cleanly for box-box because a box has exactly 3 face-normal directions to test. It doesn't generalize well to arbitrary convex shapes. The **Gilbert-Johnson-Keerthi (GJK)** algorithm answers the same "do these two convex shapes overlap, and if not, how far apart are they?" question for *any* pair of convex shapes, given only a **support function**: "what is the farthest point on this shape's boundary in direction `d`?"

```cpp
vec3 support_sphere(const Sphere& s, vec3 dir)  { return s.center + normalize(dir) * s.radius; }
vec3 support_box(const Box& b, vec3 dir)        { /* farthest of the box's 8 corners along dir */ }
vec3 support_capsule(const Capsule& c, vec3 dir){ /* farther endpoint, extended by radius */ }
```

The key idea: define the **Minkowski difference** of two shapes A and B as the set `{a - b : a in A, b in B}`. A and B overlap *if and only if* this difference contains the origin — intuitively, if there's some point of A and some point of B that coincide, then `a - b = 0` for that pair. GJK never actually constructs the (potentially huge) Minkowski difference; it uses the two shapes' support functions to compute the Minkowski difference's own support function on demand — `support_diff(dir) = support_A(dir) - support_B(-dir)` — and iteratively builds a small **simplex** (up to 4 points: a point, then a line segment, then a triangle, then a tetrahedron) inside that difference, evolving it to get as close to the origin as possible:

```
1. Pick an arbitrary starting direction; get one support point.
2. Repeat:
   a. Find the point on the current simplex closest to the origin.
   b. If the origin is inside the simplex (tetrahedron case, all 4 faces
      "contain" it): the shapes overlap. Distance = 0.
   c. Otherwise, search further in the direction from the closest point
      toward the origin. Get a new support point.
   d. If the new point doesn't get meaningfully closer to the origin than
      the current simplex already is: no more progress possible. The
      current closest-point distance is the true separation distance.
   e. Otherwise, add the new point to the simplex (dropping any vertices
      that are no longer needed — "Voronoi region" reduction) and repeat.
```

The "closest point on the simplex to the origin" sub-problem is solved case by case (point / segment / triangle / tetrahedron), each case using the mathematically standard closest-point routines from computational geometry (barycentric-coordinate region tests, in the style of Ericson's *Real-Time Collision Detection*). `ge_physics`'s GJK implementation (`gjk.cpp`) returns `{ bool intersecting, f32 distance }` — enough to answer "do they overlap" and, when they don't, "by how much are they separated," which is what `plan.md`'s Phase 5 critical-test line calls "GJK distance."

GJK is used two ways in `ge_physics`:

1. Directly, as the general-purpose distance query for shape pairs that don't have a dedicated closed-form test.
2. As an **independent correctness check** for the hand-written SAT and alternating-projection code: since GJK and SAT solve the same overlap question through completely different code paths, if they ever disagree on a hard test case (a compound-rotation box pair, say), that disagreement points straight at a bug. This turned out to be a genuinely useful engineering technique, not just a testing footnote — see [Testing Philosophy](#testing-philosophy-baselines-and-cross-validation).

---

## Broadphase: Culling the Pairs

Running narrowphase on every possible pair of N bodies costs O(N²) tests per step — fine for a handful of bodies, wasteful once a scene has dozens. The **broadphase** stage's only job is to cheaply produce a much smaller candidate list, using a test far cheaper than real narrowphase: AABB overlap.

`ge_physics` uses **sweep-and-prune**: sort every body's AABB by its minimum X coordinate, then sweep through the sorted list. Because the list is sorted by `min.x`, once you're scanning forward from body `i` and you reach a body `j` whose `min.x` exceeds body `i`'s `max.x`, every later body (with an even larger `min.x`) is guaranteed out of range too — so the inner loop can `break` early instead of scanning the rest:

```cpp
std::sort(proxies, by min.x);
for (i = 0; i < n; ++i) {
    for (j = i + 1; j < n; ++j) {
        if (proxies[j].box.min.x > proxies[i].box.max.x) break;   // sorted list: no more candidates
        if (aabb_overlap(proxies[i].box, proxies[j].box))
            emit_pair(i, j);
    }
}
```

Two things worth noting:

1. Overlap on the sort axis (X) alone isn't 3D overlap — it's only a *necessary* condition. `aabb_overlap()` (which checks all three axes) is still called to confirm every candidate before it becomes a real pair.
2. This is a fresh sort every step, not a persistent spatial structure. It is `O(n log n)` for the sort plus roughly `O(n + k)` for the sweep, where `k` is the number of overlapping X-intervals — good enough for the scene sizes MyEngine currently has. A dynamic BVH (bounding volume hierarchy) would do better at large scale by *not* re-sorting from scratch every frame, but building and maintaining one is real complexity that isn't justified until profiling actually shows sweep-and-prune as a bottleneck — the same "measure before optimizing" discipline as everywhere else in the engine.

---

## Contact Manifolds: From "They Touch" to "Here's Where"

A `Contact` (normal + one penetration depth) is enough to know *that* two shapes overlap, but not enough to keep a box resting flat on the ground stable. Picture a box sitting flush on a floor: if the solver only knew about *one* contact point (say, the box's center projected onto the floor), nothing would stop the box from slowly rotating around that single point — there's no data telling the solver "the box's four corners are all touching, so don't let it tip." A **manifold** — up to 4 actual contact points, each with its own position and penetration depth — is what supplies that missing information.

### Face Contacts: Sutherland-Hodgman Clipping

When the winning SAT axis is a face normal (of either box), the natural contact patch is the overlap of that face with whatever the other box presents to it. `manifold_box_box()` builds this via:

1. Pick the **reference face** — the face on the box whose axis won SAT.
2. Pick the **incident face** — the face on the *other* box most anti-parallel to the reference face's normal (the face most directly "facing into" the reference face).
3. Clip the incident face's 4 corners against the reference face's 4 side planes (**Sutherland-Hodgman polygon clipping** — walk each edge of the incident quad, keeping the portion that's inside each of the 4 half-spaces one plane at a time).
4. Whatever survives clipping and lies behind the reference plane becomes the manifold's contact points, each with its own penetration depth (how far behind the reference plane that specific point is).

```
Two boxes resting flush, full overlap:        Boxes partially overlapping (shifted sideways):
  incident face (4 corners, unclipped)           incident face clipped down to the true overlap
  +--------+                                     region — fewer/different corners survive
  |        |         -> 4 contact points          +----+
  |        |            (all 4 corners)           |    |  -> 4 contact points at the
  +--------+                                       +----+     true overlap footprint
```

This is exactly the standard technique (face clipping for box-box manifolds, as used in engines like Box2D/Bullet) applied at the scale `ge_physics` needs. One simplification: if clipping ever produces more than 4 points (possible under compound rotation), the implementation keeps the 4 *deepest* points rather than implementing a full area-maximizing point-reduction pass — a documented, deliberate trade-off, revisited only if a real scene shows instability from it.

### Edge Contacts: Closest Points Between Segments

When the winning SAT axis is instead one of the 9 edge-cross-product axes, there's no face-to-face patch to clip — the contact is a single point where two specific edges (the "witness edges" that produced that axis) pass closest to each other. `manifold_box_box()` identifies those two edges, then finds their closest points via the standard clamped skew-line-segment formula, and the manifold gets one contact point at their midpoint.

---

## The Sequential-Impulse Solver

### Impulses, Not Forces

Once contact points are known, the solver's job is to change the involved bodies' *velocities* so they stop interpenetrating — an **impulse-based** approach (an instantaneous velocity change) rather than a *force*-based one (which would require choosing a very small timestep and integrating a spring-like penalty force, a technique that's more prone to instability). At each contact point, the solver computes and applies an impulse `J` along the contact normal:

```cpp
a.linear_velocity  -= J * a.inv_mass;
b.linear_velocity  += J * b.inv_mass;
a.angular_velocity -= apply_inv_inertia(a, cross(r_a, J));
b.angular_velocity += apply_inv_inertia(b, cross(r_b, J));
```

`inv_mass` is `1/mass` (0 for a static/infinite-mass body, so it never moves no matter what impulse is applied). `r_a`/`r_b` are the contact point's offset from each body's center — the lever arm through which a linear impulse also produces rotation (an off-center push spins a body, not just slides it).

### Effective Mass

How large should `J` be? The solver wants the **relative velocity along the contact normal** to reach a target value (0 for a resting contact, or a bounce-back value for restitution — see below). The relationship between an applied impulse and the resulting change in relative normal velocity is governed by each body's **effective mass** at that contact — how much the contact point's velocity actually changes per unit of impulse, accounting for both linear mass and how far the contact point is from the center of rotation:

```cpp
f32 k = a.inv_mass + b.inv_mass
      + dot(apply_inv_inertia(a, cross(r_a, normal)), cross(r_a, normal))
      + dot(apply_inv_inertia(b, cross(r_b, normal)), cross(r_b, normal));
f32 normal_mass = 1.0f / k;
```

A contact point far from a body's center of mass (large lever arm) has a *smaller* effective mass in this sense — the same impulse produces more velocity change there, because it's also producing more rotation. This term is precomputed once per step (`prepare_constraints()`), not recomputed every solver iteration, because body *positions* (and therefore `r_a`/`r_b`) don't change during the velocity-only solve — only velocities do, across the iterations described below.

`apply_inv_inertia()` deserves a note: a body's inertia tensor is only diagonal (simple to invert) in its own *local* space — a spinning box resists rotation differently about its long axis than its short one, and which world-space direction is "the long axis" changes as the box rotates. So applying the inverse inertia tensor means: rotate the input vector into the body's local space (via the same "world axes = rows of the body's rotation matrix" convention used throughout `ge_physics`'s shape code), scale each axis independently by its own inverse-inertia value, then rotate the result back to world space.

### Baumgarte Stabilization

Solving only for zero relative velocity at contacts is not quite enough: floating-point drift and discrete timesteps mean bodies gradually sink into each other by a tiny amount every step even when "resting." **Baumgarte stabilization** adds a small velocity bias proportional to how deep the current penetration is, pushing bodies apart a little faster than the "just stop" solution would:

```cpp
f32 baumgarte_bias = (beta / dt) * max(penetration - slop, 0.0f);
```

`slop` is a small tolerated penetration (avoids the bias fighting to eliminate every last floating-point epsilon of overlap, which would itself cause jitter) and `beta` (0.2 in `ge_physics`) controls how aggressively the bias corrects — too high and resting contacts visibly "pop"; too low and bodies sink noticeably before recovering. This bias is *added* to the restitution-driven target described next, not a separate mechanism — both are folded into one `velocity_bias` the normal-impulse computation aims for.

### Friction

Alongside the normal impulse, the solver applies impulses along two directions **tangent** to the contact normal (an arbitrary orthonormal basis perpendicular to the normal), targeting zero relative tangential velocity — i.e., resisting sliding. The catch: friction can't be unlimited, or bodies would never slide at all. Each tangent impulse is clamped to `±friction * accumulated_normal_impulse` — a **box approximation** of the Coulomb friction cone (the physically correct version bounds the *combined* tangential impulse to a circle; clamping each axis independently is simpler and close enough in practice for this project's scenes).

### Why Iterate?

A scene with multiple contacts (a 2-box stack has contacts both between the two boxes and between the bottom box and the ground) can't be solved with one pass per contact, because resolving one contact's velocity can un-resolve another's — pushing the top box up to fix its contact with the bottom box changes the bottom box's velocity, which affects *its* contact with the ground. `solve_velocity_constraints()` runs a fixed number of passes (8, in `ge_physics`) over every contact, each pass nudging every contact's impulse a bit closer to correct; enough passes and the whole system converges to a mutually consistent solution — this is the "sequential impulse" in the algorithm's name (Gauss-Seidel-style iterative relaxation, not a one-shot linear solve). 8 iterations turned out to be sufficient for every scene this project's simulation-baseline tests cover, from a single resting sphere through a 2-box stack, without needing to tune further.

---

## Integration: Semi-Implicit Euler

### Linear Integration

Once the solver has finished, each body's velocity is known for this step; **semi-implicit Euler** (a.k.a. symplectic Euler) integrates position using that just-updated velocity, not the velocity from the start of the step:

```cpp
body.linear_velocity += gravity * dt;      // velocity updated first ...
body.position += body.linear_velocity * dt; // ... then position uses the NEW velocity
```

This ordering (velocity first, then position from the updated velocity) is more numerically stable for oscillatory/constrained systems than the "obvious" explicit Euler (position from the *old* velocity), at essentially no extra cost — a well-known, cheap win, which is why it's the standard choice for real-time physics rather than higher-order integrators.

### Angular Integration and a Composition-Order Gotcha

Integrating orientation from an angular velocity vector `w` (radians/sec around each world axis) uses the standard small-angle quaternion update: treat `w` as a "pure" quaternion `(w.x, w.y, w.z, 0)`, and the orientation's rate of change is proportional to a quaternion product with the current orientation. The subtlety is *which side* of that product the angular velocity goes on — this depends entirely on the convention already established for `quat`'s multiplication operator elsewhere in the engine, and getting it backwards silently produces a rotation that goes the "wrong way" (or spins around the wrong composed axis) without an obvious crash to reveal the bug.

MyEngine's `quat::operator*(p, q)` is documented as "apply `p` first, then `q`" — matching the engine's row-vector matrix convention (`p' = p * M`, composite transforms read left-to-right). Reasoning from that convention: a body's new orientation should be "apply the body's *existing* orientation first, then apply the small additional world-space spin" — i.e., the existing orientation goes on the *left*:

```cpp
quat spin = {w.x, w.y, w.z, 0.0f};
quat dq   = body.orientation * spin;     // orientation first, then spin — NOT spin * orientation
quat q    = body.orientation;
q.x += 0.5f * dt * dq.x;  q.y += 0.5f * dt * dq.y;
q.z += 0.5f * dt * dq.z;  q.w += 0.5f * dt * dq.w;
body.orientation = normalize(q);          // renormalize: the small-angle update isn't unit-length
```

This derivation was treated as a hypothesis, not a given, precisely because composition-order bugs are exactly this kind of silent-but-wrong — so it was verified with a dedicated test before being trusted: integrate a constant angular velocity for a known total time, and compare the result against directly constructing the equivalent finite rotation via `quat_from_axis_angle()` (an independent, already-trusted primitive). The two agreed on the first run, which is strong evidence the derivation — and specifically the `orientation * spin` ordering — is correct for this engine's conventions.

---

## RigidBody and PhysicsWorld

### Mass and Inertia

`RigidBody` is plain data: a shape (tagged by `ShapeType`), a transform, linear/angular velocity, and mass properties:

```cpp
struct RigidBody {
    ShapeType shape_type;
    vec3 half_extents;  f32 radius;  f32 capsule_half_height;   // shape parameters (only relevant ones used)
    vec3 position;  quat orientation;
    vec3 linear_velocity;  vec3 angular_velocity;
    f32  inv_mass;             // 0 == static (infinite mass, never moved by impulses)
    vec3 inv_inertia_local;    // diagonal inverse inertia tensor, body space; 0 == locked rotation
    f32  restitution;  f32 friction;
};
```

Storing `inv_mass` (not `mass`) and `inv_inertia_local` (not the inertia tensor itself) avoids ever dividing by mass/inertia in the hot solver loop — a static body's `inv_mass = 0` naturally makes every impulse applied to it a no-op (`velocity -= impulse * 0`), with no special-case branch needed.

Factory functions compute the correct inertia tensor for each shape's standard closed-form formula (solid sphere: `I = 2/5 * m * r²`; solid cuboid: `I_xx = m/3 * (h_y² + h_z²)`, and cyclically for the other two axes) so callers don't have to derive them:

```cpp
RigidBody make_sphere_body(vec3 position, f32 radius, f32 mass);   // mass == 0 -> static
RigidBody make_box_body(vec3 position, vec3 half_extents, quat orientation, f32 mass);
RigidBody make_capsule_body(vec3 position, quat orientation, f32 radius, f32 half_height, f32 mass);
```

### PhysicsWorld::step()

`PhysicsWorld` owns an `Array<RigidBody>` plus scratch storage for broadphase pairs and solver constraints, and `step(dt)` runs the entire pipeline described in this document, in order:

```cpp
void PhysicsWorld::step(f32 dt) {
    for each dynamic body: body.linear_velocity += gravity * dt;

    broadphase_.clear();
    for each body: broadphase_.add(id, compute_world_aabb(body));
    broadphase_.compute_pairs(pairs);

    constraints.clear();
    for each pair:
        if both bodies static: skip;
        Manifold m = compute_manifold(a, b);   // dispatches by shape-type pair
        for each point in m: constraints.push(ContactConstraint{...});

    prepare_constraints(bodies, constraints, dt);
    solve_velocity_constraints(bodies, constraints, velocity_iterations);

    for each dynamic body: integrate position + orientation (semi-implicit Euler);
}
```

`compute_manifold()`'s shape-pair dispatch covers every pair the current scenes actually need (sphere-sphere, sphere-box, box-box, capsule-box, in both argument orders) and returns an invalid (empty) manifold for pairs that aren't implemented (capsule-sphere, capsule-capsule) rather than guessing — there is currently only ever one capsule in a scene (the player), so those pairs never occur; they're a documented gap to fill if a future scene needs more than one capsule.

---

## The Character Controller: Rotation-Locked Capsules

The sandbox's player is a `Capsule` `RigidBody` — but a naive dynamic capsule would be knocked over by any off-center collision impulse, which feels wrong for a player-controlled character (compare: a barrel that gets knocked over by an explosion is fine; a player avatar that face-plants because it grazed a doorframe is not). `make_capsule_body()` sidesteps this by always setting `inv_inertia_local = {0, 0, 0}` — the solver's rotation-changing impulses become no-ops (`apply_inv_inertia` scales everything by zero), so the capsule can never be spun or tipped by physics, while it still falls, collides, and gets pushed around linearly exactly like any other dynamic body.

With rotation locked, the character's *facing* has to come from somewhere else — it's set directly from the player's look yaw every fixed step, not derived from any physics simulation (see [07-application-and-game-loop.md](07-application-and-game-loop.md)'s `system_move_player_physics`). This is a common, deliberate simplification in game character controllers generally: physics governs *movement and collision*, not *which way the avatar is drawn facing*.

Movement itself is also velocity-driven rather than force-driven: each fixed step, the gameplay system directly sets the capsule's horizontal (X/Z) velocity from input, and leaves vertical (Y) velocity untouched so gravity and the solver's ground-contact response remain the only things that determine whether the character is falling, landing, or resting.

---

## Testing Philosophy: Baselines and Cross-Validation

Two testing techniques recur throughout `ge_physics` and are worth calling out on their own, because they solve a problem specific to writing geometry/physics code from scratch: for the easy cases (an axis-aligned sphere resting on an axis-aligned box), the expected answer is easy to compute by hand and hard-code into a test. For the *hard* cases — a box-box contact under compound (multi-axis) rotation, say — hand-deriving the exact expected contact geometry is itself error-prone; a test built on a wrong hand-derivation would pass while confirming a bug, not catching one.

**Cross-validation against an independent algorithm.** Where two genuinely different algorithms solve the same problem (SAT and GJK both answer "do these convex shapes overlap"), agreement between them on a hard case is meaningful evidence — a bug would have to be present in *both* independent implementations, in a way that produces the *same* wrong answer, to slip through undetected. This is how the compound-rotation `box_box()` cases and the brand-new `capsule_box()` alternating-projection code were verified, rather than by hand-deriving exact numbers for geometry that's genuinely hard to visualize precisely.

**Simulation-baseline tests.** For the full pipeline (not just one narrowphase function), the most convincing test is letting the simulation run for real, for long enough to settle, and checking the result against a value derived from the scene's simple geometry — a sphere dropped onto a flat box settles at `ground_top + radius`; a 2-box stack settles at two known heights. These aren't unit tests of one function in isolation; they're evidence that broadphase, narrowphase, manifold generation, the solver, and integration are all correctly wired *together*. The strongest version of this check went one step further than the automated test suite: after the character-capsule baseline test passed, the actual `sandbox.exe` was launched and its live console output was read — the capsule, dropped from height, settled at the same value (within numerical tolerance) the hand-derived baseline predicted. Confirming a from-scratch numerical system in the real, running application — not just in an isolated test — is the highest-confidence check available for exactly this kind of code.
